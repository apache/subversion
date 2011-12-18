/*
 * svn_file_handle_cache.c: open file handle caching for Subversion
 *
 * ====================================================================
 *    Licensed to the Apache Software Foundation (ASF) under one
 *    or more contributor license agreements.  See the NOTICE file
 *    distributed with this work for additional information
 *    regarding copyright ownership.  The ASF licenses this file
 *    to you under the Apache License, Version 2.0 (the
 *    "License"); you may not use this file except in compliance
 *    with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing,
 *    software distributed under the License is distributed on an
 *    "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 *    KIND, either express or implied.  See the License for the
 *    specific language governing permissions and limitations
 *    under the License.
 * ====================================================================
 */

#include <assert.h>

#include "private/svn_file_handle_cache.h"
#include "private/svn_mutex.h"
#include "svn_private_config.h"
#include "svn_pools.h"
#include "svn_io.h"

/* Subversion using FSFS often opens files for a only a short number of
 * accesses. Since many revisions are stored in a single file, it is
 * often a relatively small number of files that gets gets opened and
 * closed again and again.
 *
 * That results in a high OS overhead for access control and handle setup.
 * Furthermore, buffered file access results in the same file sections to
 * be read repeatedly as well as in reading significantly more data than
 * what will actually be processed.
 *
 * The file cache is meant to basically keep files open and handing them
 * out to the application again and again. In that case, it is a simple
 * facade to the APR file function. However, the cached file handles use
 * a specialized data structure that allows for determining whether a
 * given handle has already been returned to the cache or invalidated by
 * destroying the cache object itself.
 *
 * Once opened, the APR file handles are kept in the cache even if they
 * are "idle", i.e. not currently held by the application. A LRU schme
 * will limit the number of open handles.
 *
 * Any file may be opened multiple times and the cache will try to hand out
 * the handle that probably has the lowest access overhead. To that end,
 * the current file pointer gets (optionally) compared with the location
 * of the next access. If a cached file is found that may already have the
 * desired data in its buffer, that handle will be returned instead of a
 * random one.
 *
 * For read-after-write scenarios, it is imperative to flush the APR file
 * buffer before attempting to read that file. Therefore, all idle handles
 * for that file should be closed before opening a file with different
 * parameters. Because buffering may have affects on EOF detection etc.
 * without the application being aware of it, no distinction is being made
 * between read-after-write, write-after-read or others.
 *
 * For similar reasons, an application may want to close all idle handles
 * explicitly, i.e. without opening new ones. svn_file_handle_cache__flush
 * is the function to call in that case.
 */

/* Size of the APR per-file data buffer. We don't rely on this value to
 * be accurate but will only use it as a tuning parameter.
 *
 * As long as we support APR 0.9, we can't change that value.
 */
#define FILE_BUFFER_SIZE 0x1000

/* forward-declarations */
typedef struct cache_entry_t cache_entry_t;
typedef struct entry_link_t entry_link_t;
typedef struct entry_list_t entry_list_t;

/* Element in a double-linked list.
 */
struct entry_link_t
{
  /* pointer to the actual data; must not be NULL */
  cache_entry_t *item;

  /* pointer to the next list element. NULL for the last element */
  entry_link_t *next;

  /* pointer to the previous list element. NULL for the first element */
  entry_link_t *previous;
};

/* Header of a double-linked list. For empty lists, all elements
 * must be NULL. Otherwise, none must be NULL.
 */
struct entry_list_t
{
  /* pointer to the first element of list */
  entry_link_t *first;

  /* pointer to the last element of list */
  entry_link_t *last;

  /* number of elements in the list */
  size_t count;
};

/* A cache entry. It represents a single file handle. Since APR buffered
 * files consume several kB of memory, we keep a private pool instance.
 * We keep the entry objects around even after closing the file handle
 * so we can reuse the memory by clearing the pool.
 *
 * The cache entry is linked in three lists:
 * - the global list of either used or unused entries
 *   (unused ones are those having no APR file handle)
 * - list of sibblings, i.e. file handles to the same file
 * - global LRU list of idle file handles, i.e. those that are currently
 *   not used by the application
 *
 * The list elements (links) are members of this structure instead of
 * heap-allocated objects.
 */
struct cache_entry_t
{
  /* mainly used to allocate the file name and handle */
  apr_pool_t *pool;

  /* the open file handle. If NULL, this is an unused (recyclable) entry */
  apr_file_t *file;

  /* The cached file handle object handed out to the application.
   * If this is NULL, the entry is either idle or unused. */
  svn_file_handle_cache__handle_t *open_handle;

  /* the file name. NULL for unused entries */
  const char *name;

  /* position of the file pointer. Valid only for idle entries. */
  apr_off_t position;

  /* link to the either the global list of used or unused entries
   * (file_handle_cache_t.used_entries, file_handle_cache_t.unused_entries,
   * respectively). */
  entry_link_t global_link;

  /* link to other used entries for the same file */
  entry_link_t sibling_link;

  /* link to the global LRU list of idle entries. 
   * Valid only for idle entries. */
  entry_link_t idle_link;
};

/* The file handle cache structure.
 */
struct svn_file_handle_cache_t
{
  /* all cache sub-structures are allocated from this pool */
  apr_pool_t *pool;

  /* a limit to the number of APR file handles. It can be exceeded only by
   * the application actually opening more cached file handles. Otherwise,
   * idle entries will be closed as soon as the limit has been reached. */
  size_t max_used_count;

  /* list of recyclable entries, currently not holding an APR file handle. */
  entry_list_t unused_entries;

  /* list of entries holding an (open) APR file handle. */
  entry_list_t used_entries;

  /* subset of used_entries, containing all entries not currently in use
   * by the application. */
  entry_list_t idle_entries;

  /* A handle index, mapping the file name to at most *one* used entry.
   * The respective other entries for the same file name are then found
   * by following the cache_entry_t.sibling_link list. */
  apr_hash_t *first_by_name;

  /* A lock for intra-process synchronization to the cache, or NULL if
   * the cache's creator doesn't feel the cache needs to be
   * thread-safe. */
  svn_mutex__t *mutex;
};

/* Internal structure behind the opaque "cached file handle" returned to
 * the application when it opens a file. Both members may be NULL, if
 * either the handle has already been returned to the cache or the cache
 * itself has been destroyed already.
 */
struct svn_file_handle_cache__handle_t
{
  /* the issuing cache. Having that element here simplifies function
   * signatures dealing with cached file handles. It also makes them
   * harder to use incorrectly. */
  svn_file_handle_cache_t *cache;

  /* the handle-specific information */
  cache_entry_t *entry;
};

/* Initialize LIST as empty.
 */
static void 
init_list(entry_list_t *list)
{
  list->first = NULL;
  list->last = NULL;
  list->count = 0;
}

/* Initialize a list element LINK and connect it to the data item ENTRY.
 */
static void 
init_link(entry_link_t *link, cache_entry_t *entry)
{
  link->item = entry;
  link->previous = NULL;
  link->next = NULL;
}

/* Insert element LINK into the a list just after PREVIOUS.
 * None may be NULL. This function does *not* update the link header.
 */
static void 
link_link(entry_link_t *link, entry_link_t *previous)
{
  /* link with next item, if that exists
   */
  if (previous->next)
    {
      previous->next->previous = link;
      link->next = previous->next;
    }

  /* link with previous item
   */
  previous->next = link;
  link->previous = previous;
}

/* Drop the element LINK from the list.
 * This function does *not* update the link header.
 */
static void 
unlink_link(entry_link_t *link)
{
  if (link->previous)
    link->previous->next = link->next;
  if (link->next)
    link->next->previous = link->previous;

  link->next = NULL;
  link->previous = NULL;
}

/* Return the entry referenced from the previous element in the list.
 * Returns NULL, if LINK is the list head.
 */
static APR_INLINE cache_entry_t *
get_previous_entry(entry_link_t *link)
{
  return link->previous ? link->previous->item : NULL;
}

/* Return the entry referenced from the next element in the list.
 * Returns NULL, if LINK is the last element in the list.
 */
static APR_INLINE cache_entry_t *
get_next_entry(entry_link_t *link)
{
  return link->next ? link->next->item : NULL;
}

/* Append list element LINK to the LIST.
 * LINK must not already be an element of any list.
 */
static void 
append_to_list(entry_list_t *list, entry_link_t *link)
{
  if (list->last)
    link_link(link, list->last);
  else
    list->first = link;

  list->last = link;
  list->count++;
}

/* Remove list element LINK from the LIST.
 * LINK must actually be an element of LIST.
 */
static void 
remove_from_list(entry_list_t *list, entry_link_t *link)
{
  list->count--;

  if (list->first == link)
    list->first = link->next;
  if (list->last == link)
    list->last = link->previous;

  unlink_link(link);
}

/* Returns the first CACHE entry for the given file NAME.
 * If no such entry exists, the result is NULL.
 */
static cache_entry_t *
find_first(svn_file_handle_cache_t *cache, const char *name)
{
  cache_entry_t *result =
    (cache_entry_t *)apr_hash_get(cache->first_by_name,
                                  name,
                                  APR_HASH_KEY_STRING);

  /* the index must contain only used entries, i.e. those that actually
   * contain an open APR file handle. */
  assert(!result || result->file);
  return result;
}

/* "Destructor" (APR pool item cleanup code) for cache entries.
 * It ensures that cached file handles currently held by the application
 * will be invalidated properly when the cache is destroyed, for instance.
 */
static apr_status_t 
auto_close_cached_handle(void *entry_void)
{
  cache_entry_t *entry = entry_void;
  if (entry->open_handle)
    {
      /* There is a cached file handle held by the application. Reset its
       * internal pointers so it won't try to call cache functions.
       */
      entry->open_handle->cache = NULL;
      entry->open_handle->entry = NULL;
      entry->open_handle = NULL;
    }

  return APR_SUCCESS;
}

/* Create a new APR-level file handle with the specified file NAME in CACHE.
 * The corresponding cache entry will be returned in RESULT.
 */
static svn_error_t *
internal_file_open(cache_entry_t **result,
                   svn_file_handle_cache_t *cache,
                   const char *name)
{
  cache_entry_t *entry;
  cache_entry_t *sibling;

  /* Can we recycle an existing, currently unused, cache entry?
   */
  if (cache->unused_entries.first)
    {
      /* yes, extract it from the "unused" list 
       */
      entry = cache->unused_entries.first->item;
      remove_from_list(&cache->unused_entries, &entry->global_link);
    }
  else
    {
      /* no, create a new entry and initialize it (except for the file info)
       */
      entry = apr_palloc(cache->pool, sizeof(cache_entry_t));
      entry->file = NULL;
      entry->open_handle = NULL;
      entry->pool = svn_pool_create(cache->pool);

      init_link(&entry->global_link, entry);
      init_link(&entry->sibling_link, entry);
      init_link(&entry->idle_link, entry);
    }

  /* (try to) open the requested file */
  SVN_ERR(svn_io_file_open(&entry->file, name, APR_READ | APR_BUFFERED, 
                           APR_OS_DEFAULT, entry->pool));
  assert(entry->file);

  /* make sure we auto-close cached file handles held by the application
   * before actually closing the file. */
  apr_pool_cleanup_register(entry->pool,
                            entry,
                            auto_close_cached_handle,
                            apr_pool_cleanup_null);

  /* set file info */
  entry->name = apr_pstrdup(entry->pool, name);
  entry->position = 0;

  /* This cache entry is now "used" (has an APR file handle) and "idle"
   * (not held by the application, yet).
   */
  append_to_list(&cache->used_entries, &entry->global_link);
  append_to_list(&cache->idle_entries, &entry->idle_link);

  /* link with other entries for the same file in the index, or add it
   * to the index if no entry for this file name exists so far */
  sibling = find_first(cache, name);
  if (sibling)
    link_link(&entry->sibling_link, &sibling->sibling_link);
  else
    apr_hash_set(cache->first_by_name,
                 entry->name,
                 APR_HASH_KEY_STRING,
                 entry);

  /* done */
  *result = entry;

  return SVN_NO_ERROR;
}

/* actually close the underlying APR file handle in the ENTRY.
 * The entry will be in "unused" state afterwards.
 */
static svn_error_t *
internal_close_file(svn_file_handle_cache_t *cache, cache_entry_t *entry)
{
  /* If the application still used this file, disconnect it from the cache.
   */
  if (entry->open_handle)
    {
      entry->open_handle->cache = NULL;
      entry->open_handle->entry = NULL;
      
      entry->open_handle = NULL;
      entry->file = NULL;
    }

  /* remove entry from the index (if it is in there) and the
   * list of entries for the same file name
   */
  if (entry->sibling_link.previous == NULL)
    {
      cache_entry_t *sibling = get_next_entry(&entry->sibling_link);
      assert(!sibling || sibling->file);

      /* make sure the hash key does not depend on entry->pool
       * by removing and possibly re-adding the hash entry
       */
      apr_hash_set(cache->first_by_name,
                   entry->name,
                   APR_HASH_KEY_STRING,
                   NULL);
      if (sibling)
        apr_hash_set(cache->first_by_name,
                     sibling->name,
                     APR_HASH_KEY_STRING,
                     sibling);
    }

  unlink_link(&entry->sibling_link);

  /* remove entry from the idle and global list */
  remove_from_list(&cache->idle_entries, &entry->idle_link);
  remove_from_list(&cache->used_entries, &entry->global_link);

  /* actually close the file handle. */
  if (entry->file)
    SVN_ERR(svn_io_file_close(entry->file, entry->pool));
  
  entry->file = NULL;
  entry->name = NULL;
  svn_pool_clear(entry->pool);

  /* entry may now be reused */
  append_to_list(&cache->unused_entries, &entry->global_link);

  return SVN_NO_ERROR;
}

/* "Destructor" (APR pool item cleanup code) for cached file handles passed
 * to the application. It ensures that the handle will be returned to the
 * cache automatically upon pool cleanup.
 */
static apr_status_t
close_handle_before_cleanup(void *handle_void)
{
  svn_file_handle_cache__handle_t *f = handle_void;
  svn_error_t *err = SVN_NO_ERROR;
  apr_status_t result = APR_SUCCESS;

  /* if this hasn't been done before: 
   * "close" the handle, i.e. return it to the cache 
   */
  if (f->entry)
    err = svn_file_handle_cache__close(f);

  /* fully reset all members to prevent zombies doing damage */
  f->entry = NULL;
  f->cache = NULL;

  /* process error returns */
  if (err)
    {
      result = err->apr_err;
      svn_error_clear(err);
    }

  return result;
}

/* Create a cached file handle to be returned to the application in F for
 * an idle ENTRY in CACHE. The cached file handle will be allocated in
 * POOL and will automatically be returned to the cache when that pool
 * is cleared or destroyed.
 */
static svn_error_t *
open_entry(svn_file_handle_cache__handle_t **f,
           svn_file_handle_cache_t *cache,
           cache_entry_t *entry,
           apr_pool_t *pool)
{
  /* any entry can be handed out to the application only once */
  assert(!entry->open_handle);

  /* the entry will no longer be idle */
  remove_from_list(&cache->idle_entries, &entry->idle_link);

  /* create and initialize the cached file handle structure */
  *f = apr_palloc(pool, sizeof(svn_file_handle_cache__handle_t));
  (*f)->cache = cache;
  (*f)->entry = entry;
  entry->open_handle = *f;

  /* ensure proper cleanup, i.e. prevent handle leaks */
  apr_pool_cleanup_register(pool,
                            *f,
                            close_handle_before_cleanup,
                            apr_pool_cleanup_null);

  return SVN_NO_ERROR;
}

/* If there are idle entries, close the oldest one, i.e. closing the
 * underlying APR file handle rendering the entry "unused".
 */
static svn_error_t *
close_oldest_idle(svn_file_handle_cache_t *cache)
{
  return cache->idle_entries.first
    ? internal_close_file(cache, cache->idle_entries.first->item)
    : SVN_NO_ERROR;
}

/* If we hold too many open files, close the oldest idle entry,
 * if there is such an entry.
 */
static svn_error_t *
auto_close_oldest(svn_file_handle_cache_t *cache)
{
  return cache->used_entries.count > cache->max_used_count
    ? close_oldest_idle(cache)
    : SVN_NO_ERROR;
}

/* Test whether the file pointer in ENTRY is close enough to OFFSET to
 * benefit from buffered access and is closer to it CLOSEST_ENTRY.
 */
static svn_boolean_t
pointer_is_closer(const cache_entry_t *entry,
                  apr_off_t offset,
                  const cache_entry_t *closest_entry)
{
  apr_off_t old_delta;
  apr_off_t new_delta;
  
  /* if the offset is unspecified, no entry will be considered a good
   * match based on the file pointer's current position.
   */
  if (offset == -1)
    return FALSE;

  /* we also ignore entries who are no close enough to fit into the
   * read buffer. */
  if ((entry->position - FILE_BUFFER_SIZE > offset) ||
      (entry->position + FILE_BUFFER_SIZE < offset))
    return FALSE;

  /* this is the closest match if we don't have a match, yet, at all */
  if (closest_entry == NULL)
    return TRUE;

  /* is it a better match? */
  old_delta = offset > closest_entry->position 
            ? offset - closest_entry->position
            : closest_entry->position - offset;
  new_delta = offset > entry->position 
            ? offset - entry->position
            : entry->position - offset;

  return old_delta > new_delta ? TRUE : FALSE;
}

/* Returns true if LHS and RHS refer to the same file.
 */
static svn_boolean_t
are_siblings(const cache_entry_t *lhs, const cache_entry_t *rhs)
{
  return (lhs == rhs) || !strcmp(lhs->name, rhs->name);
}

/* Set file pointer of ENTRY->file to OFFSET. As an optimization, make sure
 * that a few hundred bytes before that OFFSET are also pre-fetched as SVN
 * tends to read data "backwards".
 */
static svn_error_t *
aligned_seek(cache_entry_t *entry, apr_off_t offset)
{
  char dummy;

  /* (try to) access a position aligned to 1KB. Since we align most files
   * like this, repeated accesses will use the same alignment. As a result,
   * "close-by" access will lie within the same pre-fetched block */
  apr_off_t aligned_offset = offset & (-(FILE_BUFFER_SIZE / 4));
  
  /* do the seek and force data to be prefetched. Ignore the results as
   * this is merely meant to help APR make the right decissions later on. */
  apr_file_seek(entry->file, APR_SET, &aligned_offset);
  apr_file_getc(&dummy, entry->file);
  
  /* the actual seek that was requested */
  return svn_io_file_seek(entry->file, APR_SET, &offset, entry->pool);
}

/* Get an open file handle in F, for the file named FNAME with the open
 * flag(s) in FLAG and permissions in PERM. These parameters are the same
 * as in svn_io_file_open(). The file pointer will be moved to the specified
 * OFFSET, if it is different from -1.
 */
static svn_error_t *
svn_file_handle_cache__open_internal
   (svn_file_handle_cache__handle_t **f,
    svn_file_handle_cache_t *cache,
    const char *fname,
    apr_off_t offset,
    apr_pool_t *pool)
{
  cache_entry_t *entry;
  cache_entry_t *first_entry;
  cache_entry_t *near_entry = NULL;
  cache_entry_t *any_entry = NULL;
  cache_entry_t *last_entry = NULL;
  cache_entry_t *entry_found = NULL;
 
  int idle_entry_count = 0;

  /* look through all idle entries for this filename and find suitable ones */
  first_entry = find_first(cache, fname);
  for ( entry = first_entry
      ; entry
      ; entry = get_next_entry (&entry->sibling_link))
    {
      last_entry = entry;
      assert(entry->file != NULL);
      
      if (! entry->open_handle)
        {
          idle_entry_count++;
          
          if (! any_entry)
            any_entry = entry;

          /* is it a particularly close match? */
          if (pointer_is_closer(entry, offset, near_entry))
            near_entry = entry;
        }
    }

  /* select the most suitable idle file handle */
  if (near_entry)
    {
      /* best option: a file whose buffer propably already contains
       * the data that we are looking for. */
      entry_found = near_entry;
    }
  else if (any_entry)
    {
      /* Re-using an open file is also a good idea.
       * 
       * However, it may be better to open packed files a few more times
       * since later we are likely to read data later close to the current
       * location. Keep the number open handles / file reasonably low.
       */      
      if (   (idle_entry_count >= 4) 
          || (   (cache->unused_entries.count == 0)
              /* auto-closing a suitable file doesn't make sense */
              && (are_siblings(cache->idle_entries.first->item, any_entry))))
        {
          /* Ensure that any_entry will be the last one to be re-used in
           * successive calls: put it at the end of the siblings list for
           * this file name.
           */
          if (last_entry != any_entry)
            {
              if (any_entry == first_entry)
                { 
                  first_entry = get_next_entry(&any_entry->sibling_link);
                  assert(first_entry->file != NULL);
                  apr_hash_set(cache->first_by_name,
                               any_entry->name,
                               APR_HASH_KEY_STRING,
                               NULL);
                  apr_hash_set(cache->first_by_name,
                               first_entry->name,
                               APR_HASH_KEY_STRING,
                               first_entry);
                }
                
              unlink_link(&any_entry->sibling_link);
              link_link(&any_entry->sibling_link, &last_entry->sibling_link);
            }

           entry_found = any_entry;
        }
    }
    
  if (entry_found)
    {
      /* we can use an idle entry */
      entry = entry_found;

      /* move the file pointer to the desired position */
      if (offset != -1)
        SVN_ERR(aligned_seek(entry, offset));
    }
  else
    {
      /* we need a new entry. Make room for it */
      SVN_ERR(auto_close_oldest(cache));

      /* create a suitable idle entry */
      SVN_ERR(internal_file_open(&entry, cache, fname));

      /* move the file pointer to the desired position */
      if (offset > 0)
        SVN_ERR(aligned_seek(entry, offset));
    }

  assert(entry->file);

  /* pass the cached file handle to the application 
   * (if there was no previous error).
   */
  return open_entry(f, cache, entry, pool);
}

/* Same as svn_file_handle_cache__open_internal but using the mutex to
 * serialize accesss to the internal data.
 */
svn_error_t *
svn_file_handle_cache__open(svn_file_handle_cache__handle_t **f,
                            svn_file_handle_cache_t *cache,
                            const char *fname,
                            apr_off_t offset,
                            apr_pool_t *pool)
{
  SVN_MUTEX__WITH_LOCK(cache->mutex,
                       svn_file_handle_cache__open_internal(f,
                                                            cache,
                                                            fname,
                                                            offset,
                                                            pool));

  return SVN_NO_ERROR;
}

/* Return the APR level file handle underlying the cache file handle F.
 * Returns NULL, if f is NULL, has already been closed or otherwise
 * invalidated.
 */
apr_file_t *
svn_file_handle_cache__get_apr_handle(svn_file_handle_cache__handle_t *f)
{
  return (f && f->entry) ? f->entry->file : NULL;
}

/* Return the name of the file that the cached handle f refers to.
 * Returns NULL, if f is NULL, has already been closed or otherwise
 * invalidated.
 */
const char *
svn_file_handle_cache__get_name(svn_file_handle_cache__handle_t *f)
{
  return (f && f->entry) ? f->entry->name : NULL;
}

/* Return the ENTRY to the CACHE. Depending on the number of open handles,
 * the underlying handle may actually get closed.
 */
static svn_error_t *
svn_file_handle_cache__close_internal(svn_file_handle_cache_t *cache,
                                      cache_entry_t *entry)
{
  /* mark cache entry as idle.
   * It must actually manage the handle we are about to close.
   */
  entry->open_handle = NULL;
  append_to_list(&cache->idle_entries, &entry->idle_link);

  /* remember the current file pointer so we can prefer this entry for
   * accesses in the vicinity of this position. 
   */
  entry->position = 0;
  SVN_ERR(svn_io_file_seek(entry->file,
                           APR_CUR,
                           &entry->position,
                           entry->pool));

  /* if all went well so far, reduce the number of cached file handles */
  return auto_close_oldest(cache);
}

/* Return the cached file handle F to the cache. Depending on the number
 * of open handles, the underlying handle may actually get closed.
 */
svn_error_t *
svn_file_handle_cache__close(svn_file_handle_cache__handle_t *f)
{
  svn_file_handle_cache_t *cache = f ? f->cache : NULL;
  cache_entry_t *entry = f ? f->entry : NULL;

  /* no-op for closed or invalidated cached file handles */
  if (cache == NULL || entry == NULL)
    return SVN_NO_ERROR;

  /* mark the handle as "closed" / "invalid" */
  f->cache = NULL;
  f->entry = NULL;

  /* now, mark the entry as again available */
  SVN_MUTEX__WITH_LOCK(cache->mutex,
                       svn_file_handle_cache__close_internal(cache, 
                                                             entry));

  return SVN_NO_ERROR;
}

/* Close all cached file handles pertaining to FILE_NAME.
 */
static svn_error_t *
svn_file_handle_cache__flush_internal(svn_file_handle_cache_t *cache, 
                                      const char *file_name)
{
  cache_entry_t *next;
  cache_entry_t *entry = find_first(cache, file_name);
  
  if (entry)
    {
      while (get_previous_entry(&entry->sibling_link))
        entry = get_previous_entry(&entry->sibling_link);

      for (next = get_next_entry (&entry->sibling_link); entry; entry = next)
        {
          next = get_next_entry (&entry->sibling_link); 

          /* Handles still held by the application will simply be
           * disconnected from the cache but the underlying file
           * will not be closed.*/
          SVN_ERR(internal_close_file(cache, entry));
        }
    }

  return SVN_NO_ERROR;
}

/* Same as svn_file_handle_cache__flush_internal but using the mutex to
 * serialize accesss to the internal data.
 */
svn_error_t *
svn_file_handle_cache__flush(svn_file_handle_cache_t *cache,
                             const char *file_name)
{
  SVN_MUTEX__WITH_LOCK(cache->mutex, 
                       svn_file_handle_cache__flush_internal(cache));
  return SVN_NO_ERROR;
}

/* Creates a new file handle cache in CACHE. Up to MAX_HANDLES file handles
 * will be kept open. All cache-internal memory allocations during the caches'
 * lifetime will be done from POOL.
 *
 * If the caller ensures that there are no concurrent accesses to the cache,
 * THREAD_SAFE may be FALSE. Otherwise, it must be TRUE.
 */
svn_error_t *
svn_file_handle_cache__create_cache(svn_file_handle_cache_t **cache,
                                    size_t max_handles,
                                    svn_boolean_t thread_safe,
                                    apr_pool_t *pool)
{
  /* allocate cache header */
  svn_file_handle_cache_t *new_cache =
      (svn_file_handle_cache_t *)apr_palloc(pool, sizeof(*new_cache));

  /* create sub-pool for all cache sub-structures */
  new_cache->pool = svn_pool_create(pool);

  /* initialize struct members */
  new_cache->max_used_count = max_handles;

  init_list(&new_cache->used_entries);
  init_list(&new_cache->idle_entries);
  init_list(&new_cache->unused_entries);

  new_cache->first_by_name = apr_hash_make(new_cache->pool);

  svn_mutex__init(&new_cache->mutex, thread_safe, pool);

  /* done */
  *cache = new_cache;
  return SVN_NO_ERROR;
}
