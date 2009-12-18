/*
 * cache-inprocess.c: in-memory caching for Subversion
 *
 * ====================================================================
 * Copyright (c) 2008 CollabNet.  All rights reserved.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution.  The terms
 * are also available at http://subversion.tigris.org/license-1.html.
 * If newer versions of this license are posted there, you may use a
 * newer version instead, at your option.
 *
 * This software consists of voluntary contributions made by many
 * individuals.  For exact contribution history, see the revision
 * history and logs, available at http://subversion.tigris.org/.
 * ====================================================================
 */

#include <assert.h>

#include <apr_thread_mutex.h>

#include "svn_pools.h"

#include "svn_private_config.h"

#include "cache.h"

/* The (internal) cache object. */
typedef struct {
  /* Maps from a key (of size CACHE->KLEN) to a struct cache_entry. */
  apr_hash_t *hash;
  apr_ssize_t klen;

  /* Used to copy values in and out of the cache. */
  svn_cache__dup_func_t dup_func;

  /* The number of pages we're allowed to allocate before having to
   * try to reuse one. */
  apr_int64_t unallocated_pages;
  /* Number of cache entries stored on each page.  Must be at least 1. */
  apr_int64_t items_per_page;

  /* A dummy cache_page serving as the head of a circular doubly
   * linked list of cache_pages.  SENTINEL->NEXT is the most recently
   * used page, and SENTINEL->PREV is the least recently used page.
   * All pages in this list are "full"; the page currently being
   * filled (PARTIAL_PAGE) is not in the list. */
  struct cache_page *sentinel;

  /* A page currently being filled with entries, or NULL if there's no
   * partially-filled page.  This page is not in SENTINEL's list. */
  struct cache_page *partial_page;
  /* If PARTIAL_PAGE is not null, this is the number of entries
   * currently on PARTIAL_PAGE. */
  apr_int64_t partial_page_number_filled;

  /* The pool that the svn_cache__t itself, HASH, and all pages are
   * allocated in; subpools of this pool are used for the cache_entry
   * structs, as well as the dup'd values and hash keys.
   */
  apr_pool_t *cache_pool;

#if APR_HAS_THREADS
  /* A lock for intra-process synchronization to the cache, or NULL if
   * the cache's creator doesn't feel the cache needs to be
   * thread-safe. */
  apr_thread_mutex_t *mutex;
#endif
} inprocess_cache_t;

/* A cache page; all items on the page are allocated from the same
 * pool. */
struct cache_page {
  /* Pointers for the LRU list anchored at the cache's SENTINEL.
   * (NULL for the PARTIAL_PAGE.) */
  struct cache_page *prev;
  struct cache_page *next;

  /* The pool in which cache_entry structs, hash keys, and dup'd
   * values are allocated. */
  apr_pool_t *page_pool;

  /* A singly linked list of the entries on this page; used to remove
   * them from the cache's HASH before reusing the page. */
  struct cache_entry *first_entry;
};

/* An cache entry. */
struct cache_entry {
  const void *key;
  void *value;

  /* The page it's on (needed so that the LRU list can be
   * maintained). */
  struct cache_page *page;

  /* Next entry on the page. */
  struct cache_entry *next_entry;
};


/* Removes PAGE from the doubly-linked list it is in (leaving its PREV
 * and NEXT fields undefined). */
static void
remove_page_from_list(struct cache_page *page)
{
  page->prev->next = page->next;
  page->next->prev = page->prev;
}

/* Inserts PAGE after CACHE's sentinel. */
static void
insert_page(inprocess_cache_t *cache,
            struct cache_page *page)
{
  struct cache_page *pred = cache->sentinel;

  page->prev = pred;
  page->next = pred->next;
  page->prev->next = page;
  page->next->prev = page;
}

/* If PAGE is in the circularly linked list (eg, its NEXT isn't NULL),
 * move it to the front of the list. */
static void
move_page_to_front(inprocess_cache_t *cache,
                   struct cache_page *page)
{
  assert(page != cache->sentinel);

  if (! page->next)
    return;

  remove_page_from_list(page);
  insert_page(cache, page);
}

/* Uses CACHE->dup_func to copy VALUE into *VALUE_P inside POOL, or
   just sets *VALUE_P to NULL if VALUE is NULL. */
static svn_error_t *
duplicate_value(void **value_p,
                inprocess_cache_t *cache,
                void *value,
                apr_pool_t *pool)
{
  if (value)
    SVN_ERR((cache->dup_func)(value_p, value, pool));
  else
    *value_p = NULL;
  return SVN_NO_ERROR;
}

/* Return a copy of KEY inside POOL, using CACHE->KLEN to figure out
 * how. */
static const void *
duplicate_key(inprocess_cache_t *cache,
              const void *key,
              apr_pool_t *pool)
{
  if (cache->klen == APR_HASH_KEY_STRING)
    return apr_pstrdup(pool, key);
  else
    return apr_pmemdup(pool, key, cache->klen);
}

/* If applicable, locks CACHE's mutex. */
static svn_error_t *
lock_cache(inprocess_cache_t *cache)
{
#if APR_HAS_THREADS
  apr_status_t status;
  if (! cache->mutex)
    return SVN_NO_ERROR;

  status = apr_thread_mutex_lock(cache->mutex);
  if (status)
    return svn_error_wrap_apr(status, _("Can't lock cache mutex"));
#endif

  return SVN_NO_ERROR;
}

/* If applicable, unlocks CACHE's mutex, then returns ERR. */
static svn_error_t *
unlock_cache(inprocess_cache_t *cache,
             svn_error_t *err)
{
#if APR_HAS_THREADS
  apr_status_t status;
  if (! cache->mutex)
    return err;

  status = apr_thread_mutex_unlock(cache->mutex);
  if (status && !err)
    return svn_error_wrap_apr(status, _("Can't unlock cache mutex"));
#endif

  return err;
}

static svn_error_t *
inprocess_cache_get(void **value_p,
                    svn_boolean_t *found,
                    void *cache_void,
                    const void *key,
                    apr_pool_t *pool)
{
  inprocess_cache_t *cache = cache_void;
  struct cache_entry *entry;
  svn_error_t *err;

  SVN_ERR(lock_cache(cache));

  entry = apr_hash_get(cache->hash, key, cache->klen);
  if (! entry)
    {
      *found = FALSE;
      return unlock_cache(cache, SVN_NO_ERROR);
    }

  move_page_to_front(cache, entry->page);

  *found = TRUE;
  err = duplicate_value(value_p, cache, entry->value, pool);
  return unlock_cache(cache, err);
}

/* Removes PAGE from the LRU list, removes all of its entries from
 * CACHE's hash, clears its pool, and sets its entry pointer to NULL.
 * Finally, puts it in the "partial page" slot in the cache and sets
 * partial_page_number_filled to 0.  Must be called on a page actually
 * in the list. */
static void
erase_page(inprocess_cache_t *cache,
           struct cache_page *page)
{
  struct cache_entry *e;

  remove_page_from_list(page);

  for (e = page->first_entry;
       e;
       e = e->next_entry)
    {
      apr_hash_set(cache->hash, e->key, cache->klen, NULL);
    }

  svn_pool_clear(page->page_pool);

  page->first_entry = NULL;
  page->prev = NULL;
  page->next = NULL;

  cache->partial_page = page;
  cache->partial_page_number_filled = 0;
}


static svn_error_t *
inprocess_cache_set(void *cache_void,
                    const void *key,
                    void *value,
                    apr_pool_t *pool)
{
  inprocess_cache_t *cache = cache_void;
  struct cache_entry *existing_entry;
  svn_error_t *err = SVN_NO_ERROR;

  SVN_ERR(lock_cache(cache));

  existing_entry = apr_hash_get(cache->hash, key, cache->klen);

  /* Is it already here, but we can do the one-item-per-page
   * optimization? */
  if (existing_entry && cache->items_per_page == 1)
    {
      /* Special case!  ENTRY is the *only* entry on this page, so
       * why not wipe it (so as not to leak the previous value).
       */
      struct cache_page *page = existing_entry->page;

      /* This can't be the partial page: items_per_page == 1
       * *never* has a partial page (except for in the temporary state
       * that we're about to fake). */
      SVN_ERR_ASSERT(page->next != NULL);
      SVN_ERR_ASSERT(cache->partial_page == NULL);

      erase_page(cache, page);
      existing_entry = NULL;
    }

  /* Is it already here, and we just have to leak the old value? */
  if (existing_entry)
    {
      struct cache_page *page = existing_entry->page;

      move_page_to_front(cache, page);
      err = duplicate_value(&(existing_entry->value), cache,
                            value, page->page_pool);
      goto cleanup;
    }

  /* Do we not have a partial page to put it on, but we are allowed to
   * allocate more? */
  if (cache->partial_page == NULL && cache->unallocated_pages > 0)
    {
      cache->partial_page = apr_pcalloc(cache->cache_pool,
                                        sizeof(*(cache->partial_page)));
      cache->partial_page->page_pool = svn_pool_create(cache->cache_pool);
      cache->partial_page_number_filled = 0;
      (cache->unallocated_pages)--;
    }

  /* Do we really not have a partial page to put it on, even after the
   * one-item-per-page optimization and checking the unallocated page
   * count? */
  if (cache->partial_page == NULL)
    {
      struct cache_page *oldest_page = cache->sentinel->prev;

      SVN_ERR_ASSERT(oldest_page != cache->sentinel);

      /* Erase the page and put it in cache->partial_page. */
      erase_page(cache, oldest_page);
    }

  SVN_ERR_ASSERT(cache->partial_page != NULL);

  {
    struct cache_page *page = cache->partial_page;
    struct cache_entry *new_entry = apr_pcalloc(page->page_pool,
                                                sizeof(*new_entry));

    /* Copy the key and value into the page's pool.  */
    new_entry->key = duplicate_key(cache, key, page->page_pool);
    err = duplicate_value(&(new_entry->value), cache, value,
                          page->page_pool);
    if (err)
      goto cleanup;

    /* Add the entry to the page's list. */
    new_entry->page = page;
    new_entry->next_entry = page->first_entry;
    page->first_entry = new_entry;

    /* Add the entry to the hash, using the *entry's* copy of the
     * key. */
    apr_hash_set(cache->hash, new_entry->key, cache->klen, new_entry);

    /* We've added something else to the partial page. */
    (cache->partial_page_number_filled)++;

    /* Is it full? */
    if (cache->partial_page_number_filled >= cache->items_per_page)
      {
        insert_page(cache, page);
        cache->partial_page = NULL;
      }
  }

 cleanup:
  return unlock_cache(cache, err);
}

/* Baton type for svn_cache__iter. */
struct cache_iter_baton {
  svn_iter_apr_hash_cb_t user_cb;
  void *user_baton;
};

/* Call the user's callback with the actual value, not the
   cache_entry.  Implements the svn_iter_apr_hash_cb_t
   prototype. */
static svn_error_t *
iter_cb(void *baton,
        const void *key,
        apr_ssize_t klen,
        void *val,
        apr_pool_t *pool)
{
  struct cache_iter_baton *b = baton;
  struct cache_entry *entry = val;
  return (b->user_cb)(b->user_baton, key, klen, entry->value, pool);
}

static svn_error_t *
inprocess_cache_iter(svn_boolean_t *completed,
                     void *cache_void,
                     svn_iter_apr_hash_cb_t user_cb,
                     void *user_baton,
                     apr_pool_t *pool)
{
  inprocess_cache_t *cache = cache_void;
  struct cache_iter_baton b;
  b.user_cb = user_cb;
  b.user_baton = user_baton;

  SVN_ERR(lock_cache(cache));
  return unlock_cache(cache,
                      svn_iter_apr_hash(completed, cache->hash, iter_cb, &b,
                                        pool));

}

static svn_cache__vtable_t inprocess_cache_vtable = {
  inprocess_cache_get,
  inprocess_cache_set,
  inprocess_cache_iter
};

svn_error_t *
svn_cache__create_inprocess(svn_cache__t **cache_p,
                            svn_cache__dup_func_t dup_func,
                            apr_ssize_t klen,
                            apr_int64_t pages,
                            apr_int64_t items_per_page,
                            svn_boolean_t thread_safe,
                            apr_pool_t *pool)
{
  svn_cache__t *wrapper = apr_pcalloc(pool, sizeof(*wrapper));
  inprocess_cache_t *cache = apr_pcalloc(pool, sizeof(*cache));

  cache->hash = apr_hash_make(pool);
  cache->klen = klen;

  cache->dup_func = dup_func;

  SVN_ERR_ASSERT(pages >= 1);
  cache->unallocated_pages = pages;
  SVN_ERR_ASSERT(items_per_page >= 1);
  cache->items_per_page = items_per_page;

  cache->sentinel = apr_pcalloc(pool, sizeof(*(cache->sentinel)));
  cache->sentinel->prev = cache->sentinel;
  cache->sentinel->next = cache->sentinel;
  /* The sentinel doesn't need a pool.  (We're happy to crash if we
   * accidentally try to treat it like a real page.) */

#if APR_HAS_THREADS
  if (thread_safe)
    {
      apr_status_t status = apr_thread_mutex_create(&(cache->mutex),
                                                    APR_THREAD_MUTEX_DEFAULT,
                                                    pool);
      if (status)
        return svn_error_wrap_apr(status,
                                  _("Can't create cache mutex"));
    }
#endif

  cache->cache_pool = pool;

  wrapper->vtable = &inprocess_cache_vtable;
  wrapper->cache_internal = cache;

  *cache_p = wrapper;
  return SVN_NO_ERROR;
}
