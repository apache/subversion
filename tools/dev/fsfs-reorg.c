/* fsfs-reorg.c -- prototypic tool to reorganize packed FSFS repositories
 *                 to reduce seeks
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

#include <apr.h>
#include <apr_general.h>
#include <apr_file_io.h>
#include <apr_poll.h>

#include "svn_pools.h"
#include "svn_diff.h"
#include "svn_io.h"
#include "svn_utf.h"
#include "svn_dirent_uri.h"
#include "svn_sorts.h"
#include "svn_delta.h"
#include "svn_hash.h"

#include "private/svn_string_private.h"
#include "private/svn_subr_private.h"
#include "private/svn_dep_compat.h"

#ifndef _
#define _(x) x
#endif

#define ERROR_TAG "fsfs-reporg: "

/* forward declarations */
typedef struct noderev_t noderev_t;
typedef struct revision_info_t revision_info_t;

/* A FSFS rev file is sequence of fragments and unused space (the latter
 * only being inserted by this tool and not during ordinary SVN operation).
 *
 * This type defines the type of any fragment.
 *
 * Please note that the classification as "property", "dir" or "file"
 * fragments is only to be used while determining the future placement
 * of a representation.  If the rep is shared, the same rep may be used
 * as *any* of the 3 kinds.
 */
enum fragment_kind_t
{
  /* the 2 number line containing changes and root node offsets */
  header_fragment,

  /* list of all changes in a revision */
  changes_fragment,

  /* (the textual representation of) a noderev */
  noderev_fragment,

  /* a property rep (including PLAIN / DELTA header) */
  property_fragment,

  /* a directory rep (including PLAIN / DELTA header) */
  dir_fragment,

  /* a file rep (including PLAIN / DELTA header) */
  file_fragment
};

/* A fragment.  This is used to represent the final ordering, i.e. there
 * will be an array containing elements of this type that basically put
 * a fragment at some location in the target file.
 */
typedef struct fragment_t
{
  /* position in the target file */
  apr_size_t position;

  /* kind of fragment */
  enum fragment_kind_t kind;

  /* pointer to the  fragment struct; type depends on KIND */
  void *data;
} fragment_t;

/* Location info for a single revision.
 */
typedef struct revision_location_t
{
  /* pack file offset (manifest value), 0 for non-packed files */
  apr_size_t offset;

  /* offset of the changes list relative to OFFSET */
  apr_size_t changes;

  /* length of the changes list on bytes */
  apr_size_t changes_len;

  /* first offset behind the revision data in the pack file (file length
   * for non-packed revs) */
  apr_size_t end;
} revision_location_t;

/* Absolute position and size of some item.
 */
typedef struct location_t
{
  /* absolute offset in the file */
  apr_size_t offset;

  /* item length in bytes */
  apr_size_t size;
} location_t;

/* A parsed directory entry. Note that instances of this struct may be
 * shared between different DIRECTORY_T containers.
 */
typedef struct direntry_t
{
  /* (local) entry / path name */
  const char *name;

  /* strlen (name) */
  apr_size_t name_len;

  /* node rev providing ID and representation(s) */
  noderev_t *node;
} direntry_t;

/* Representation of a parsed directory content.
 */
typedef struct directory_t
{
  /* array of pointers to DIRENTRY_T */
  apr_array_header_t *entries;

  /* MD5 of the textual representation. Will be set lazily as a side-effect
   * of determining the length of this dir's textual representation. */
  unsigned char target_md5[16];

  /* (expanded) length of the textual representation.
   * Determined lazily during the write process. */
  apr_size_t size;
} directory_t;

/* A representation fragment.
 */
typedef struct representation_t
{
  /* location in the source file */
  location_t original;

  /* location in the reordered target file */
  location_t target;

  /* length of the PLAIN / DELTA line in the source file in bytes */
  apr_size_t header_size;

  /* deltification base, or NULL if there is none */
  struct representation_t *delta_base;

  /* revision that contains this representation
   * (may be referenced by other revisions, though) */
  revision_info_t *revision;

  /* representation content parsed as a directory. This will be NULL, if
   * *no* directory noderev uses this representation. */
  directory_t *dir;

  /* the source content has a PLAIN header, so we may simply copy the
   * source content into the target */
  svn_boolean_t is_plain;

  /* coloring flag used in the reordering algorithm to keep track of
   * representations that still need to be placed. */
  svn_boolean_t covered;
} representation_t;

/* A node rev.
 */
struct noderev_t
{
  /* location within the source file */
  location_t original;

  /* location within the reorganized target file. */
  location_t target;

  /* predecessor node, or NULL if there is none */
  noderev_t *predecessor;

  /* content representation; may be NULL if there is none */
  representation_t *text;

  /* properties representation; may be NULL if there is none */
  representation_t *props;

  /* revision that this noderev belongs to */
  revision_info_t *revision;

  /* coloring flag used in the reordering algorithm to keep track of
   * representations that still need to be placed. */
  svn_boolean_t covered;
};

/* Represents a single revision.
 * There will be only one instance per revision. */
struct revision_info_t
{
  /* number of this revision */
  svn_revnum_t revision;

  /* position in the source file */
  revision_location_t original;

  /* position in the reorganized target file */
  revision_location_t target;

  /* noderev of the root directory */
  noderev_t *root_noderev;

  /* all noderevs_t of this revision (ordered by source file offset),
   * i.e. those that point back to this struct */
  apr_array_header_t *node_revs;

  /* all representation_t of this revision (ordered by source file offset),
   * i.e. those that point back to this struct */
  apr_array_header_t *representations;
};

/* Represents a packed revision file.
 */
typedef struct revision_pack_t
{
  /* first revision in the pack file */
  svn_revnum_t base;

  /* revision_info_t* of all revisions in the pack file; in revision order. */
  apr_array_header_t *info;

  /* list of fragments to place in the target pack file; in target order. */
  apr_array_header_t *fragments;

  /* source pack file length */
  apr_size_t filesize;

  /* temporary value. Equal to the number of bytes in the target pack file
   * already allocated to fragments. */
  apr_size_t target_offset;
} revision_pack_t;

/* Cache for revision source content.  All content is stored in DATA and
 * the HASH maps revision number to an svn_string_t instance whose data
 * member points into DATA.
 *
 * Once TOTAL_SIZE exceeds LIMIT, all content will be discarded.  Similarly,
 * the hash gets cleared every 10000 insertions to keep the HASH_POOL
 * memory usage in check.
 */
typedef struct content_cache_t
{
  /* pool used for HASH */
  apr_pool_t *hash_pool;

  /* svn_revnum_t -> svn_string_t.
   * The strings become (potentially) invalid when adding new cache entries. */
  apr_hash_t *hash;

  /* data buffer. the first TOTAL_SIZE bytes are actually being used. */
  char *data;

  /* DATA capacity */
  apr_size_t limit;

  /* number of bytes used in DATA */
  apr_size_t total_size;

  /* number of insertions since the last hash cleanup */
  apr_size_t insert_count;
} content_cache_t;

/* A cached directory. In contrast to directory_t, this stored the data as
 * the plain hash that the normal FSFS will use to serialize & diff dirs.
 */
typedef struct dir_cache_entry_t
{
  /* revision containing the representation */
  svn_revnum_t revision;

  /* offset of the representation within that revision */
  apr_size_t offset;

  /* key-value representation of the directory entries */
  apr_hash_t *hash;
} dir_cache_entry_t;

/* Directory cache. (revision, offset) will be mapped directly into the
 * ENTRIES array of ENTRY_COUNT buckets (many entries will be NULL).
 * Two alternating pools will be used to allocate dir content.
 *
 * If the INSERT_COUNT exceeds a given limit, the pools get exchanged and
 * the older of the two will be cleared. This is to keep dir objects valid
 * for at least one insertion.
 */
typedef struct dir_cache_t
{
  /* fixed-size array of ENTRY_COUNT elements */
  dir_cache_entry_t *entries;

  /* currently used for entry allocations */
  apr_pool_t *pool1;

  /* previously used for entry allocations */
  apr_pool_t *pool2;

  /* size of ENTRIES in elements */
  apr_size_t entry_count;

  /* number of directory elements added. I.e. usually >> #cached dirs */
  apr_size_t insert_count;
} dir_cache_t;

/* A cached, undeltified txdelta window.
 */
typedef struct window_cache_entry_t
{
  /* revision containing the window */
  svn_revnum_t revision;

  /* offset of the deltified window within that revision */
  apr_size_t offset;

  /* window content */
  svn_stringbuf_t *window;
} window_cache_entry_t;

/* Cache for undeltified txdelta windows. (revision, offset) will be mapped
 * directly into the ENTRIES array of INSERT_COUNT buckets (most entries
 * will be NULL).
 *
 * The cache will be cleared when USED exceeds CAPACITY.
 */
typedef struct window_cache_t
{
  /* fixed-size array of ENTRY_COUNT elements */
  window_cache_entry_t *entries;

  /* used to allocate windows */
  apr_pool_t *pool;

  /* size of ENTRIES in elements */
  apr_size_t entry_count;

  /* maximum combined size of all cached windows */
  apr_size_t capacity;

  /* current combined size of all cached windows */
  apr_size_t used;
} window_cache_t;

/* Root data structure containing all information about a given repository.
 */
typedef struct fs_fs_t
{
  /* repository to reorg */
  const char *path;

  /* revision to start at (must be 0, ATM) */
  svn_revnum_t start_revision;

  /* FSFS format number */
  int format;

  /* highest revision number in the repo */
  svn_revnum_t max_revision;

  /* first non-packed revision */
  svn_revnum_t min_unpacked_rev;

  /* sharing size*/
  int max_files_per_dir;

  /* all revisions */
  apr_array_header_t *revisions;

  /* all packed files */
  apr_array_header_t *packs;

  /* empty representation.
   * Used as a dummy base for DELTA reps without base. */
  representation_t *null_base;

  /* revision content cache */
  content_cache_t *cache;

  /* directory hash cache */
  dir_cache_t *dir_cache;

  /* undeltified txdelta window cache */
  window_cache_t *window_cache;
} fs_fs_t;

/* Return the rev pack folder for revision REV in FS.
 */
static const char *
get_pack_folder(fs_fs_t *fs,
                svn_revnum_t rev,
                apr_pool_t *pool)
{
  return apr_psprintf(pool, "%s/db/revs/%ld.pack",
                      fs->path, rev / fs->max_files_per_dir);
}

/* Return the path of the file containing revision REV in FS.
 */
static const char *
rev_or_pack_file_name(fs_fs_t *fs,
                      svn_revnum_t rev,
                      apr_pool_t *pool)
{
  return fs->min_unpacked_rev > rev
     ? svn_dirent_join(get_pack_folder(fs, rev, pool), "pack", pool)
     : apr_psprintf(pool, "%s/db/revs/%ld/%ld", fs->path,
                          rev / fs->max_files_per_dir, rev);
}

/* Open the file containing revision REV in FS and return it in *FILE.
 */
static svn_error_t *
open_rev_or_pack_file(apr_file_t **file,
                      fs_fs_t *fs,
                      svn_revnum_t rev,
                      apr_pool_t *pool)
{
  return svn_io_file_open(file,
                          rev_or_pack_file_name(fs, rev, pool),
                          APR_READ | APR_BUFFERED,
                          APR_OS_DEFAULT,
                          pool);
}

/* Read the whole content of the file containing REV in FS and return that
 * in *CONTENT.
 */
static svn_error_t *
read_rev_or_pack_file(svn_stringbuf_t **content,
                      fs_fs_t *fs,
                      svn_revnum_t rev,
                      apr_pool_t *pool)
{
  return svn_stringbuf_from_file2(content,
                                  rev_or_pack_file_name(fs, rev, pool),
                                  pool);
}

/* Return a new content cache with the given size LIMIT.  Use POOL for
 * all cache-related allocations.
 */
static content_cache_t *
create_content_cache(apr_pool_t *pool,
                     apr_size_t limit)
{
  content_cache_t *result = apr_pcalloc(pool, sizeof(*result));

  result->hash_pool = svn_pool_create(pool);
  result->hash = svn_hash__make(result->hash_pool);
  result->limit = limit;
  result->total_size = 0;
  result->insert_count = 0;
  result->data = apr_palloc(pool, limit);

  return result;
}

/* Return the content of revision REVISION from CACHE. Return NULL upon a
 * cache miss. This is a cache-internal function.
 */
static svn_string_t *
get_cached_content(content_cache_t *cache,
                   svn_revnum_t revision)
{
  return apr_hash_get(cache->hash, &revision, sizeof(revision));
}

/* Take the content in DATA and store it under REVISION in CACHE.
 * This is a cache-internal function.
 */
static void
set_cached_content(content_cache_t *cache,
                   svn_revnum_t revision,
                   svn_string_t *data)
{
  svn_string_t *content;
  svn_revnum_t *key;

  /* double insertion? -> broken cache logic */
  assert(get_cached_content(cache, revision) == NULL);

  /* purge the cache upon overflow */
  if (cache->total_size + data->len > cache->limit)
    {
      /* the hash pool grows slowly over time; clear it once in a while */
      if (cache->insert_count > 10000)
        {
          svn_pool_clear(cache->hash_pool);
          cache->hash = svn_hash__make(cache->hash_pool);
          cache->insert_count = 0;
        }
      else
        cache->hash = svn_hash__make(cache->hash_pool);

      cache->total_size = 0;

      /* buffer overflow / revision too large */
      if (data->len > cache->limit)
        SVN_ERR_MALFUNCTION_NO_RETURN();
    }

  /* copy data to cache and update he index (hash) */
  content = apr_palloc(cache->hash_pool, sizeof(*content));
  content->data = cache->data + cache->total_size;
  content->len = data->len;

  memcpy(cache->data + cache->total_size, data->data, data->len);
  cache->total_size += data->len;

  key = apr_palloc(cache->hash_pool, sizeof(*key));
  *key = revision;

  apr_hash_set(cache->hash, key, sizeof(*key), content);
  ++cache->insert_count;
}

/* Get the file content of revision REVISION in FS and return it in *DATA.
 * Use SCRATCH_POOL for temporary allocations.
 */
static svn_error_t *
get_content(svn_string_t **data,
            fs_fs_t *fs,
            svn_revnum_t revision,
            apr_pool_t *scratch_pool)
{
  apr_file_t *file;
  revision_info_t *revision_info;
  svn_stringbuf_t *temp;
  apr_off_t temp_offset;

  /* try getting the data from our cache */
  svn_string_t *result = get_cached_content(fs->cache, revision);
  if (result)
    {
      *data = result;
      return SVN_NO_ERROR;
    }

  /* not in cache. Is the revision valid at all? */
  if (revision - fs->start_revision > fs->revisions->nelts)
    return svn_error_createf(SVN_ERR_BAD_VERSION_FILE_FORMAT, NULL,
                             _("Unknown revision %ld"), revision);
  revision_info = APR_ARRAY_IDX(fs->revisions,
                                revision - fs->start_revision,
                                revision_info_t*);

  /* read the revision content. Assume that the file has *not* been
   * reorg'ed, yet, i.e. all data is in one place. */
  temp = svn_stringbuf_create_ensure(  revision_info->original.end
                                     - revision_info->original.offset,
                                     scratch_pool);
  temp->len = revision_info->original.end - revision_info->original.offset;
  SVN_ERR(open_rev_or_pack_file(&file, fs, revision, scratch_pool));

  temp_offset = revision_info->original.offset;
  SVN_ERR(svn_io_file_seek(file, APR_SET, &temp_offset,
                           scratch_pool));
  SVN_ERR_ASSERT(temp_offset < APR_SIZE_MAX);
  revision_info->original.offset = (apr_size_t)temp_offset;
  SVN_ERR(svn_io_file_read(file, temp->data, &temp->len, scratch_pool));

  /* cache the result and return it */
  set_cached_content(fs->cache, revision,
                     svn_stringbuf__morph_into_string(temp));
  *data = get_cached_content(fs->cache, revision);

  return SVN_NO_ERROR;
}

/* Return a new directory cache with ENTRY_COUNT buckets in its index.
 * Use POOL for all cache-related allocations.
 */
static dir_cache_t *
create_dir_cache(apr_pool_t *pool,
                 apr_size_t entry_count)
{
  dir_cache_t *result = apr_pcalloc(pool, sizeof(*result));

  result->pool1 = svn_pool_create(pool);
  result->pool2 = svn_pool_create(pool);
  result->entry_count = entry_count;
  result->insert_count = 0;
  result->entries = apr_pcalloc(pool, sizeof(*result->entries) * entry_count);

  return result;
}

/* Return the position within FS' dir cache ENTRIES index for the given
 * (REVISION, OFFSET) pair. This is a cache-internal function.
 */
static apr_size_t
get_dir_cache_index(fs_fs_t *fs,
                    svn_revnum_t revision,
                    apr_size_t offset)
{
  return (revision + offset * 0xd1f3da69) % fs->dir_cache->entry_count;
}

/* Return the currently active pool of FS' dir cache. Note that it may be
 * cleared after *2* insertions.
 */
static apr_pool_t *
get_cached_dir_pool(fs_fs_t *fs)
{
  return fs->dir_cache->pool1;
}

/* Return the cached directory content stored in REPRESENTATION within FS.
 * If that has not been found in cache, return NULL.
 */
static apr_hash_t *
get_cached_dir(fs_fs_t *fs,
               representation_t *representation)
{
  svn_revnum_t revision = representation->revision->revision;
  apr_size_t offset = representation->original.offset;

  apr_size_t i = get_dir_cache_index(fs, revision, offset);
  dir_cache_entry_t *entry = &fs->dir_cache->entries[i];

  return entry->offset == offset && entry->revision == revision
    ? entry->hash
    : NULL;
}

/* Cache the directory HASH for  REPRESENTATION within FS.
 */
static void
set_cached_dir(fs_fs_t *fs,
               representation_t *representation,
               apr_hash_t *hash)
{
  /* select the entry to use */
  svn_revnum_t revision = representation->revision->revision;
  apr_size_t offset = representation->original.offset;

  apr_size_t i = get_dir_cache_index(fs, revision, offset);
  dir_cache_entry_t *entry = &fs->dir_cache->entries[i];

  /* clean the cache and rotate pools at regular intervals */
  fs->dir_cache->insert_count += apr_hash_count(hash);
  if (fs->dir_cache->insert_count >= fs->dir_cache->entry_count * 100)
    {
      apr_pool_t *pool;

      svn_pool_clear(fs->dir_cache->pool2);
      memset(fs->dir_cache->entries,
             0,
             sizeof(*fs->dir_cache->entries) * fs->dir_cache->entry_count);
      fs->dir_cache->insert_count = 0;

      pool = fs->dir_cache->pool2;
      fs->dir_cache->pool2 = fs->dir_cache->pool1;
      fs->dir_cache->pool1 = pool;
    }

  /* write data to cache */
  entry->hash = hash;
  entry->offset = offset;
  entry->revision = revision;
}

/* Return a new txdelta window cache with ENTRY_COUNT buckets in its index
 * and a the total CAPACITY given in bytes.
 * Use POOL for all cache-related allocations.
 */
static window_cache_t *
create_window_cache(apr_pool_t *pool,
                    apr_size_t entry_count,
                    apr_size_t capacity)
{
  window_cache_t *result = apr_pcalloc(pool, sizeof(*result));

  result->pool = svn_pool_create(pool);
  result->entry_count = entry_count;
  result->capacity = capacity;
  result->used = 0;
  result->entries = apr_pcalloc(pool, sizeof(*result->entries) * entry_count);

  return result;
}

/* Return the position within FS' window cache ENTRIES index for the given
 * (REVISION, OFFSET) pair. This is a cache-internal function.
 */
static apr_size_t
get_window_cache_index(fs_fs_t *fs,
                       svn_revnum_t revision,
                       apr_size_t offset)
{
  return (revision + offset * 0xd1f3da69) % fs->window_cache->entry_count;
}

/* Return the cached txdelta window stored in REPRESENTATION within FS.
 * If that has not been found in cache, return NULL.
 */
static svn_stringbuf_t *
get_cached_window(fs_fs_t *fs,
                  representation_t *representation,
                  apr_pool_t *pool)
{
  svn_revnum_t revision = representation->revision->revision;
  apr_size_t offset = representation->original.offset;

  apr_size_t i = get_window_cache_index(fs, revision, offset);
  window_cache_entry_t *entry = &fs->window_cache->entries[i];

  return entry->offset == offset && entry->revision == revision
    ? svn_stringbuf_dup(entry->window, pool)
    : NULL;
}

/* Cache the undeltified txdelta WINDOW for REPRESENTATION within FS.
 */
static void
set_cached_window(fs_fs_t *fs,
                  representation_t *representation,
                  svn_stringbuf_t *window)
{
  /* select entry */
  svn_revnum_t revision = representation->revision->revision;
  apr_size_t offset = representation->original.offset;

  apr_size_t i = get_window_cache_index(fs, revision, offset);
  window_cache_entry_t *entry = &fs->window_cache->entries[i];

  /* if the capacity is exceeded, clear the cache */
  fs->window_cache->used += window->len;
  if (fs->window_cache->used >= fs->window_cache->capacity)
    {
      svn_pool_clear(fs->window_cache->pool);
      memset(fs->window_cache->entries,
             0,
             sizeof(*fs->window_cache->entries) * fs->window_cache->entry_count);
      fs->window_cache->used = window->len;
    }

  /* set the entry to a copy of the window data */
  entry->window = svn_stringbuf_dup(window, fs->window_cache->pool);
  entry->offset = offset;
  entry->revision = revision;
}

/* Given rev pack PATH in FS, read the manifest file and return the offsets
 * in *MANIFEST. Use POOL for allocations.
 */
static svn_error_t *
read_manifest(apr_array_header_t **manifest,
              fs_fs_t *fs,
              const char *path,
              apr_pool_t *pool)
{
  svn_stream_t *manifest_stream;
  apr_pool_t *iterpool;

  /* Open the manifest file. */
  SVN_ERR(svn_stream_open_readonly(&manifest_stream,
                                   svn_dirent_join(path, "manifest", pool),
                                   pool, pool));

  /* While we're here, let's just read the entire manifest file into an array,
     so we can cache the entire thing. */
  iterpool = svn_pool_create(pool);
  *manifest = apr_array_make(pool, fs->max_files_per_dir, sizeof(apr_size_t));
  while (1)
    {
      svn_stringbuf_t *sb;
      svn_boolean_t eof;
      apr_uint64_t val;
      svn_error_t *err;

      svn_pool_clear(iterpool);
      SVN_ERR(svn_stream_readline(manifest_stream, &sb, "\n", &eof, iterpool));
      if (eof)
        break;

      err = svn_cstring_strtoui64(&val, sb->data, 0, APR_SIZE_MAX, 10);
      if (err)
        return svn_error_createf(SVN_ERR_FS_CORRUPT, err,
                                 _("Manifest offset '%s' too large"),
                                 sb->data);
      APR_ARRAY_PUSH(*manifest, apr_size_t) = (apr_size_t)val;
    }
  svn_pool_destroy(iterpool);

  return svn_stream_close(manifest_stream);
}

/* Read header information for the revision stored in FILE_CONTENT at
 * offsets START or END.  Return the offsets within FILE_CONTENT for the
 * *ROOT_NODEREV, the list of *CHANGES and its len in *CHANGES_LEN.
 * Use POOL for temporary allocations. */
static svn_error_t *
read_revision_header(apr_size_t *changes,
                     apr_size_t *changes_len,
                     apr_size_t *root_noderev,
                     svn_stringbuf_t *file_content,
                     apr_size_t start,
                     apr_size_t end,
                     apr_pool_t *pool)
{
  char buf[64];
  const char *line;
  char *space;
  apr_uint64_t val;
  apr_size_t len;

  /* Read in this last block, from which we will identify the last line. */
  len = sizeof(buf);
  if (start + len > end)
    len = end - start;

  memcpy(buf, file_content->data + end - len, len);

  /* The last byte should be a newline. */
  if (buf[(apr_ssize_t)len - 1] != '\n')
    return svn_error_create(SVN_ERR_FS_CORRUPT, NULL,
                            _("Revision lacks trailing newline"));

  /* Look for the next previous newline. */
  buf[len - 1] = 0;
  line = strrchr(buf, '\n');
  if (line == NULL)
    return svn_error_create(SVN_ERR_FS_CORRUPT, NULL,
                            _("Final line in revision file longer "
                              "than 64 characters"));

  space = strchr(line, ' ');
  if (space == NULL)
    return svn_error_create(SVN_ERR_FS_CORRUPT, NULL,
                            _("Final line in revision file missing space"));

  /* terminate the header line */
  *space = 0;

  /* extract information */
  SVN_ERR(svn_cstring_strtoui64(&val, line+1, 0, APR_SIZE_MAX, 10));
  *root_noderev = (apr_size_t)val;
  SVN_ERR(svn_cstring_strtoui64(&val, space+1, 0, APR_SIZE_MAX, 10));
  *changes = (apr_size_t)val;
  *changes_len = end - *changes - start - (buf + len - line) + 1;

  return SVN_NO_ERROR;
}

/* Read the FSFS format number and sharding size from the format file at
 * PATH and return it in *PFORMAT and *MAX_FILES_PER_DIR respectively.
 * Use POOL for temporary allocations.
 */
static svn_error_t *
read_format(int *pformat, int *max_files_per_dir,
            const char *path, apr_pool_t *pool)
{
  svn_error_t *err;
  apr_file_t *file;
  char buf[80];
  apr_size_t len;

  /* open format file and read the first line */
  err = svn_io_file_open(&file, path, APR_READ | APR_BUFFERED,
                         APR_OS_DEFAULT, pool);
  if (err && APR_STATUS_IS_ENOENT(err->apr_err))
    {
      /* Treat an absent format file as format 1.  Do not try to
         create the format file on the fly, because the repository
         might be read-only for us, or this might be a read-only
         operation, and the spirit of FSFS is to make no changes
         whatseover in read-only operations.  See thread starting at
         http://subversion.tigris.org/servlets/ReadMsg?list=dev&msgNo=97600
         for more. */
      svn_error_clear(err);
      *pformat = 1;
      *max_files_per_dir = 0;

      return SVN_NO_ERROR;
    }
  SVN_ERR(err);

  len = sizeof(buf);
  err = svn_io_read_length_line(file, buf, &len, pool);
  if (err && APR_STATUS_IS_EOF(err->apr_err))
    {
      /* Return a more useful error message. */
      svn_error_clear(err);
      return svn_error_createf(SVN_ERR_BAD_VERSION_FILE_FORMAT, NULL,
                               _("Can't read first line of format file '%s'"),
                               svn_dirent_local_style(path, pool));
    }
  SVN_ERR(err);

  /* Check that the first line contains only digits. */
  SVN_ERR(svn_cstring_atoi(pformat, buf));

  /* Set the default values for anything that can be set via an option. */
  *max_files_per_dir = 0;

  /* Read any options. */
  while (1)
    {
      len = sizeof(buf);
      err = svn_io_read_length_line(file, buf, &len, pool);
      if (err && APR_STATUS_IS_EOF(err->apr_err))
        {
          /* No more options; that's okay. */
          svn_error_clear(err);
          break;
        }
      SVN_ERR(err);

      if (strncmp(buf, "layout ", 7) == 0)
        {
          if (strcmp(buf+7, "linear") == 0)
            {
              *max_files_per_dir = 0;
              continue;
            }

          if (strncmp(buf+7, "sharded ", 8) == 0)
            {
              /* Check that the argument is numeric. */
              SVN_ERR(svn_cstring_atoi(max_files_per_dir, buf + 15));
              continue;
            }
        }

      return svn_error_createf(SVN_ERR_BAD_VERSION_FILE_FORMAT, NULL,
         _("'%s' contains invalid filesystem format option '%s'"),
         svn_dirent_local_style(path, pool), buf);
    }

  return svn_io_file_close(file, pool);
}

/* Read the content of the file at PATH and return it in *RESULT.
 * Use POOL for temporary allocations.
 */
static svn_error_t *
read_number(svn_revnum_t *result, const char *path, apr_pool_t *pool)
{
  svn_stringbuf_t *content;
  apr_uint64_t number;

  SVN_ERR(svn_stringbuf_from_file2(&content, path, pool));

  content->data[content->len-1] = 0;
  SVN_ERR(svn_cstring_strtoui64(&number, content->data, 0, LONG_MAX, 10));
  *result = (svn_revnum_t)number;

  return SVN_NO_ERROR;
}

/* Create *FS for the repository at PATH and read the format and size info.
 * Use POOL for temporary allocations.
 */
static svn_error_t *
fs_open(fs_fs_t **fs, const char *path, apr_pool_t *pool)
{
  *fs = apr_pcalloc(pool, sizeof(**fs));
  (*fs)->path = apr_pstrdup(pool, path);
  (*fs)->max_files_per_dir = 1000;

  /* Read the FS format number. */
  SVN_ERR(read_format(&(*fs)->format,
                      &(*fs)->max_files_per_dir,
                      svn_dirent_join(path, "db/format", pool),
                      pool));
  if (((*fs)->format != 4) && ((*fs)->format != 6))
    return svn_error_create(SVN_ERR_FS_UNSUPPORTED_FORMAT, NULL, NULL);

  /* read size (HEAD) info */
  SVN_ERR(read_number(&(*fs)->min_unpacked_rev,
                      svn_dirent_join(path, "db/min-unpacked-rev", pool),
                      pool));
  return read_number(&(*fs)->max_revision,
                     svn_dirent_join(path, "db/current", pool),
                     pool);
}

/* Utility function that returns true if STRING->DATA matches KEY.
 */
static svn_boolean_t
key_matches(svn_string_t *string, const char *key)
{
  return strcmp(string->data, key) == 0;
}

/* Comparator used for binary search comparing the absolute file offset
 * of a noderev to some other offset. DATA is a *noderev_t, KEY is pointer
 * to an apr_size_t.
 */
static int
compare_noderev_offsets(const void *data, const void *key)
{
  apr_ssize_t diff = (*(const noderev_t *const *)data)->original.offset
                     - *(const apr_size_t *)key;

  /* sizeof(int) may be < sizeof(ssize_t) */
  if (diff < 0)
    return -1;
  return diff > 0 ? 1 : 0;
}

/* Get the revision and offset info from the node ID with FS. Return the
 * data as *REVISION_INFO and *OFFSET, respectively.
 *
 * Note that we assume that the revision_info_t object ID's revision has
 * already been created. That can be guaranteed for standard FSFS pack
 * files as IDs never point to future revisions.
 */
static svn_error_t *
parse_revnode_pos(revision_info_t **revision_info,
                  apr_size_t *offset,
                  fs_fs_t *fs,
                  svn_string_t *id)
{
  int revision;
  apr_uint64_t temp;

  /* split the ID and verify the format */
  const char *revision_pos = strrchr(id->data, 'r');
  char *offset_pos = (char *)strchr(id->data, '/');

  if (revision_pos == NULL || offset_pos == NULL)
    return svn_error_createf(SVN_ERR_BAD_VERSION_FILE_FORMAT, NULL,
                             _("Invalid node id '%s'"), id->data);

  /* extract the numbers (temp. modifying the ID)*/
  *offset_pos = 0;
  SVN_ERR(svn_cstring_atoi(&revision, revision_pos + 1));
  SVN_ERR(svn_cstring_strtoui64(&temp, offset_pos + 1, 0, APR_SIZE_MAX, 10));
  *offset = (apr_size_t)temp;
  *offset_pos = '/';

  /* validate the revision number and return the revision info */
  if (revision - fs->start_revision > fs->revisions->nelts)
    return svn_error_createf(SVN_ERR_BAD_VERSION_FILE_FORMAT, NULL,
                             _("Unknown revision %d"), revision);

  *revision_info = APR_ARRAY_IDX(fs->revisions,
                                 revision - fs->start_revision,
                                 revision_info_t*);

  return SVN_NO_ERROR;
}

/* Returns in *RESULT the noderev at OFFSET relative the revision given in
 * REVISION_INFO.  If no such noderev has been parsed, yet, error out.
 *
 * Since we require the noderev to already have been parsed, we can use
 * this functions only to access "older", i.e. predecessor noderevs.
 */
static svn_error_t *
find_noderev(noderev_t **result,
            revision_info_t *revision_info,
            apr_size_t offset)
{
  int idx = svn_sort__bsearch_lower_bound(&offset,
                                          revision_info->node_revs,
                                          compare_noderev_offsets);
  if ((idx < 0) || (idx >= revision_info->node_revs->nelts))
    return svn_error_createf(SVN_ERR_BAD_VERSION_FILE_FORMAT, NULL,
                             _("No noderev found at offset %ld"),
                             (long)offset);

  *result = APR_ARRAY_IDX(revision_info->node_revs, idx, noderev_t *);
  if ((*result)->original.offset != offset)
    return svn_error_createf(SVN_ERR_BAD_VERSION_FILE_FORMAT, NULL,
                             _("No noderev found at offset %ld"),
                             (long)offset);

  return SVN_NO_ERROR;
}

/* In *RESULT, return the noderev given by ID in FS.  The noderev must
 * already have been parsed and put into the FS data structures.
 */
static svn_error_t *
parse_pred(noderev_t **result,
           fs_fs_t *fs,
           svn_string_t *id)
{
  apr_size_t offset;
  revision_info_t *revision_info;

  SVN_ERR(parse_revnode_pos(&revision_info, &offset, fs, id));
  SVN_ERR(find_noderev(result, revision_info, offset));

  return SVN_NO_ERROR;
}

/* Comparator used for binary search comparing the absolute file offset
 * of a representation to some other offset. DATA is a *representation_t,
 * KEY is a pointer to an apr_size_t.
 */
static int
compare_representation_offsets(const void *data, const void *key)
{
  apr_ssize_t diff = (*(const representation_t *const *)data)->original.offset
                     - *(const apr_size_t *)key;

  /* sizeof(int) may be < sizeof(ssize_t) */
  if (diff < 0)
    return -1;
  return diff > 0 ? 1 : 0;
}

/* Find the revision_info_t object to the given REVISION in FS and return
 * it in *REVISION_INFO. For performance reasons, we skip the lookup if
 * the info is already provided.
 *
 * In that revision, look for the representation_t object for offset OFFSET.
 * If it already exists, set *idx to its index in *REVISION_INFO's
 * representations list and return the representation object. Otherwise,
 * set the index to where it must be inserted and return NULL.
 */
static representation_t *
find_representation(int *idx,
                    fs_fs_t *fs,
                    revision_info_t **revision_info,
                    int revision,
                    apr_size_t offset)
{
  revision_info_t *info;
  *idx = -1;

  /* first let's find the revision '*/
  info = revision_info ? *revision_info : NULL;
  if (info == NULL || info->revision != revision)
    {
      info = APR_ARRAY_IDX(fs->revisions,
                           revision - fs->start_revision,
                           revision_info_t*);
      if (revision_info)
        *revision_info = info;
    }

  /* not found -> no result */
  if (info == NULL)
    return NULL;

  assert(revision == info->revision);

  /* look for the representation */
  *idx = svn_sort__bsearch_lower_bound(&offset,
                                       info->representations,
                                       compare_representation_offsets);
  if (*idx < info->representations->nelts)
    {
      /* return the representation, if this is the one we were looking for */
      representation_t *result
        = APR_ARRAY_IDX(info->representations, *idx, representation_t *);
      if (result->original.offset == offset)
        return result;
    }

  /* not parsed, yet */
  return NULL;
}

/* Read the representation header in FILE_CONTENT at OFFSET.  Return its
 * size in *HEADER_SIZE, set *IS_PLAIN if no deltification was used and
 * return the deltification base representation in *REPRESENTATION.  If
 * there is none, set it to NULL.  Use FS to it look up.
 *
 * Use SCRATCH_POOL for temporary allocations.
 */
static svn_error_t *
read_rep_base(representation_t **representation,
              apr_size_t *header_size,
              svn_boolean_t *is_plain,
              fs_fs_t *fs,
              svn_stringbuf_t *file_content,
              apr_size_t offset,
              apr_pool_t *scratch_pool)
{
  char *str, *last_str;
  int idx, revision;
  apr_uint64_t temp;

  /* identify representation header (1 line) */
  const char *buffer = file_content->data + offset;
  const char *line_end = strchr(buffer, '\n');
  *header_size = line_end - buffer + 1;

  /* check for PLAIN rep */
  if (strncmp(buffer, "PLAIN\n", *header_size) == 0)
    {
      *is_plain = TRUE;
      *representation = NULL;
      return SVN_NO_ERROR;
    }

  /* check for DELTA against empty rep */
  *is_plain = FALSE;
  if (strncmp(buffer, "DELTA\n", *header_size) == 0)
    {
      /* This is a delta against the empty stream. */
      *representation = fs->null_base;
      return SVN_NO_ERROR;
    }

  /* it's delta against some other rep. Duplicate the header info such
   * that we may modify it during parsing. */
  str = apr_pstrndup(scratch_pool, buffer, line_end - buffer);
  last_str = str;

  /* parse it. */
  str = svn_cstring_tokenize(" ", &last_str);
  str = svn_cstring_tokenize(" ", &last_str);
  SVN_ERR(svn_cstring_atoi(&revision, str));

  str = svn_cstring_tokenize(" ", &last_str);
  SVN_ERR(svn_cstring_strtoui64(&temp, str, 0, APR_SIZE_MAX, 10));

  /* it should refer to a rep in an earlier revision.  Look it up */
  *representation = find_representation(&idx, fs, NULL, revision, (apr_size_t)temp);
  return SVN_NO_ERROR;
}

/* Parse the representation reference (text: or props:) in VALUE, look
 * it up in FS and return it in *REPRESENTATION.  To be able to parse the
 * base rep, we pass the FILE_CONTENT as well.
 *
 * If necessary, allocate the result in POOL; use SCRATCH_POOL for temp.
 * allocations.
 */
static svn_error_t *
parse_representation(representation_t **representation,
                     fs_fs_t *fs,
                     svn_stringbuf_t *file_content,
                     svn_string_t *value,
                     revision_info_t *revision_info,
                     apr_pool_t *pool,
                     apr_pool_t *scratch_pool)
{
  representation_t *result;
  int revision;

  apr_uint64_t offset;
  apr_uint64_t size;
  int idx;

  /* read location (revision, offset) and size */
  char *c = (char *)value->data;
  SVN_ERR(svn_cstring_atoi(&revision, svn_cstring_tokenize(" ", &c)));
  SVN_ERR(svn_cstring_strtoui64(&offset, svn_cstring_tokenize(" ", &c), 0, APR_SIZE_MAX, 10));
  SVN_ERR(svn_cstring_strtoui64(&size, svn_cstring_tokenize(" ", &c), 0, APR_SIZE_MAX, 10));

  /* look it up */
  result = find_representation(&idx, fs, &revision_info, revision, (apr_size_t)offset);
  if (!result)
    {
      /* not parsed, yet (probably a rep in the same revision).
       * Create a new rep object and determine its base rep as well.
       */
      result = apr_pcalloc(pool, sizeof(*result));
      result->revision = revision_info;
      result->original.offset = (apr_size_t)offset;
      result->original.size = (apr_size_t)size;
      SVN_ERR(read_rep_base(&result->delta_base, &result->header_size,
                            &result->is_plain, fs, file_content,
                            (apr_size_t)offset + revision_info->original.offset,
                            scratch_pool));

      svn_sort__array_insert(&result, revision_info->representations, idx);
    }

  *representation = result;

  return SVN_NO_ERROR;
}

/* Read the delta window contents of all windows in REPRESENTATION in FS.
 * Return the data as svn_txdelta_window_t* instances in *WINDOWS.
 * Use POOL for allocations.
 */
static svn_error_t *
read_windows(apr_array_header_t **windows,
             fs_fs_t *fs,
             representation_t *representation,
             apr_pool_t *pool)
{
  svn_string_t *content;
  svn_string_t data;
  svn_stream_t *stream;
  apr_size_t offset = representation->original.offset
                    + representation->header_size;
  char version;
  apr_size_t len = sizeof(version);

  *windows = apr_array_make(pool, 0, sizeof(svn_txdelta_window_t *));

  /* get the whole revision content */
  SVN_ERR(get_content(&content, fs, representation->revision->revision, pool));

  /* create a read stream and position it directly after the rep header */
  data.data = content->data + offset + 3;
  data.len = representation->original.size - 3;
  stream = svn_stream_from_string(&data, pool);
  SVN_ERR(svn_stream_read(stream, &version, &len));

  /* read the windows from that stream */
  while (TRUE)
    {
      svn_txdelta_window_t *window;
      svn_stream_mark_t *mark;
      char dummy;

      len = sizeof(dummy);
      SVN_ERR(svn_stream_mark(stream, &mark, pool));
      SVN_ERR(svn_stream_read(stream, &dummy, &len));
      if (len == 0)
        break;

      SVN_ERR(svn_stream_seek(stream, mark));
      SVN_ERR(svn_txdelta_read_svndiff_window(&window, stream, version, pool));
      APR_ARRAY_PUSH(*windows, svn_txdelta_window_t *) = window;
    }

  return SVN_NO_ERROR;
}

/* Read the content of the PLAIN REPRESENTATION in FS and return it in
 * *CONTENT.  Use POOL for allocations.
 */
static svn_error_t *
read_plain(svn_stringbuf_t **content,
           fs_fs_t *fs,
           representation_t *representation,
           apr_pool_t *pool)
{
  svn_string_t *data;
  apr_size_t offset = representation->original.offset
                    + representation->header_size;

  SVN_ERR(get_content(&data, fs, representation->revision->revision, pool));

  /* content is stored as fulltext already */
  *content = svn_stringbuf_ncreate(data->data + offset,
                                   representation->original.size,
                                   pool);

  return SVN_NO_ERROR;
}

/* Get the undeltified representation that is a result of combining all
 * deltas from the current desired REPRESENTATION in FS with its base
 * representation.  Store the result in *CONTENT.
 * Use POOL for allocations. */
static svn_error_t *
get_combined_window(svn_stringbuf_t **content,
                    fs_fs_t *fs,
                    representation_t *representation,
                    apr_pool_t *pool)
{
  int i;
  apr_array_header_t *windows;
  svn_stringbuf_t *base_content, *result;
  const char *source;
  apr_pool_t *sub_pool;
  apr_pool_t *iter_pool;

  /* special case: no un-deltification necessary */
  if (representation->is_plain)
    return read_plain(content, fs, representation, pool);

  /* special case: data already in cache */
  *content = get_cached_window(fs, representation, pool);
  if (*content)
    return SVN_NO_ERROR;

  /* read the delta windows for this representation */
  sub_pool = svn_pool_create(pool);
  iter_pool = svn_pool_create(pool);
  SVN_ERR(read_windows(&windows, fs, representation, sub_pool));

  /* fetch the / create a base content */
  if (representation->delta_base && representation->delta_base->revision)
    SVN_ERR(get_combined_window(&base_content, fs,
                                representation->delta_base, sub_pool));
  else
    base_content = svn_stringbuf_create_empty(sub_pool);

  /* apply deltas */
  result = svn_stringbuf_create_empty(pool);
  source = base_content->data;

  for (i = 0; i < windows->nelts; ++i)
    {
      svn_txdelta_window_t *window
        = APR_ARRAY_IDX(windows, i, svn_txdelta_window_t *);
      svn_stringbuf_t *buf
        = svn_stringbuf_create_ensure(window->tview_len, iter_pool);

      buf->len = window->tview_len;
      svn_txdelta_apply_instructions(window, window->src_ops ? source : NULL,
                                     buf->data, &buf->len);

      svn_stringbuf_appendbytes(result, buf->data, buf->len);
      source += window->sview_len;

      svn_pool_clear(iter_pool);
    }

  svn_pool_destroy(iter_pool);
  svn_pool_destroy(sub_pool);

  /* cache result and return it */
  set_cached_window(fs, representation, result);
  *content = result;

  return SVN_NO_ERROR;
}

/* forward declaration */
static svn_error_t *
read_noderev(noderev_t **noderev,
             fs_fs_t *fs,
             svn_stringbuf_t *file_content,
             apr_size_t offset,
             revision_info_t *revision_info,
             apr_pool_t *pool,
             apr_pool_t *scratch_pool);

/* Get the noderev at OFFSET in FILE_CONTENT in FS.  The file content must
 * pertain to the revision given in REVISION_INFO.  If the data has not
 * been read yet, parse it and store it in REVISION_INFO.  Return the result
 * in *NODEREV.
 *
 * Use POOL for allocations and SCRATCH_POOL for temporaries.
 */
static svn_error_t *
get_noderev(noderev_t **noderev,
            fs_fs_t *fs,
            svn_stringbuf_t *file_content,
            apr_size_t offset,
            revision_info_t *revision_info,
            apr_pool_t *pool,
            apr_pool_t *scratch_pool)
{
  int idx = svn_sort__bsearch_lower_bound(&offset,
                                          revision_info->node_revs,
                                          compare_noderev_offsets);
  if ((idx < 0) || (idx >= revision_info->node_revs->nelts))
    SVN_ERR(read_noderev(noderev, fs, file_content, offset, revision_info,
                         pool, scratch_pool));
  else
    {
      *noderev = APR_ARRAY_IDX(revision_info->node_revs, idx, noderev_t *);
      if ((*noderev)->original.offset != offset)
        SVN_ERR(read_noderev(noderev, fs, file_content, offset, revision_info,
                             pool, scratch_pool));
    }

  return SVN_NO_ERROR;
}

/* Read the directory stored in REPRESENTATION in FS into *HASH.  The result
 * will be allocated in FS' directory cache and it will be plain key-value
 * hash.  Use SCRATCH_POOL for temporary allocations.
 */
static svn_error_t *
read_dir(apr_hash_t **hash,
         fs_fs_t *fs,
         representation_t *representation,
         apr_pool_t *scratch_pool)
{
  svn_stringbuf_t *text;
  apr_pool_t *text_pool;
  svn_stream_t *stream;
  apr_pool_t *pool;

  /* chances are, we find the info in cache already */
  *hash = get_cached_dir(fs, representation);
  if (*hash)
    return SVN_NO_ERROR;

  /* create the result container */
  pool = get_cached_dir_pool(fs);
  *hash = svn_hash__make(pool);

  /* if this is a non-empty rep, read it and de-serialize the hash */
  if (representation != NULL)
    {
      text_pool = svn_pool_create(scratch_pool);
      SVN_ERR(get_combined_window(&text, fs, representation, text_pool));
      stream = svn_stream_from_stringbuf(text, text_pool);
      SVN_ERR(svn_hash_read2(*hash, stream, SVN_HASH_TERMINATOR, pool));
      svn_pool_destroy(text_pool);
    }

  /* cache the result */
  set_cached_dir(fs, representation, *hash);

  return SVN_NO_ERROR;
}

/* Starting at the directory in REPRESENTATION in FILE_CONTENT, read all
 * DAG nodes, directories and representations linked in that tree structure.
 * Store them in FS and read them only once.
 *
 * Use POOL for persistent allocations and SCRATCH_POOL for temporaries.
 */
static svn_error_t *
parse_dir(fs_fs_t *fs,
          svn_stringbuf_t *file_content,
          representation_t *representation,
          apr_pool_t *pool,
          apr_pool_t *scratch_pool)
{
  apr_hash_t *hash;
  apr_hash_index_t *hi;
  apr_pool_t *iter_pool = svn_pool_create(scratch_pool);
  apr_hash_t *base_dir = svn_hash__make(scratch_pool);

  /* special case: empty dir rep */
  if (representation == NULL)
    return SVN_NO_ERROR;

  /* if we have a previous representation of that dir, hash it by name */
  if (representation->delta_base && representation->delta_base->dir)
    {
      apr_array_header_t *dir = representation->delta_base->dir->entries;
      int i;

      for (i = 0; i < dir->nelts; ++i)
        {
          direntry_t *entry = APR_ARRAY_IDX(dir, i, direntry_t *);
          apr_hash_set(base_dir, entry->name, entry->name_len, entry);
        }
    }

  /* read this directory */
  SVN_ERR(read_dir(&hash, fs, representation, scratch_pool));

  /* add it as an array to the representation (entries yet to be filled) */
  representation->dir = apr_pcalloc(pool, sizeof(*representation->dir));
  representation->dir->entries
    = apr_array_make(pool, apr_hash_count(hash), sizeof(direntry_t *));

  /* Translate the string dir entries into real entries.  Reuse existing
   * objects as much as possible to keep memory consumption low.
   */
  for (hi = apr_hash_first(pool, hash); hi; hi = apr_hash_next(hi))
    {
      const char *name = svn__apr_hash_index_key(hi);
      svn_string_t *str_val = svn__apr_hash_index_val(hi);
      apr_size_t offset;
      revision_info_t *revision_info;

      /* look for corresponding entry in previous version */
      apr_size_t name_len = strlen(name);
      direntry_t *entry = base_dir
                        ? apr_hash_get(base_dir, name, name_len)
                        : NULL;

      /* parse the new target revnode ID (revision, offset) */
      SVN_ERR(parse_revnode_pos(&revision_info, &offset, fs, str_val));

      /* if this is a new entry or if the content changed, create a new
       * instance for it. */
      if (   !entry
          || !entry->node->text
          || entry->node->text->revision != revision_info
          || entry->node->original.offset != offset)
        {
          /* create & init the new entry. Reuse the name string if possible */
          direntry_t *new_entry = apr_pcalloc(pool, sizeof(*entry));
          new_entry->name_len = name_len;
          if (entry)
            new_entry->name = entry->name;
          else
            new_entry->name = apr_pstrdup(pool, name);

          /* Link it to the content noderev. Recurse. */
          entry = new_entry;
          SVN_ERR(get_noderev(&entry->node, fs, file_content, offset,
                              revision_info, pool, iter_pool));
        }

      /* set the directory entry */
      APR_ARRAY_PUSH(representation->dir->entries, direntry_t *) = entry;
      svn_pool_clear(iter_pool);
    }

  svn_pool_destroy(iter_pool);
  return SVN_NO_ERROR;
}

/* Starting at the noderev at OFFSET in FILE_CONTENT, read all DAG nodes,
 * directories and representations linked in that tree structure.  Store
 * them in FS and read them only once.  Return the result in *NODEREV.
 *
 * Use POOL for persistent allocations and SCRATCH_POOL for temporaries.
 */
static svn_error_t *
read_noderev(noderev_t **noderev,
             fs_fs_t *fs,
             svn_stringbuf_t *file_content,
             apr_size_t offset,
             revision_info_t *revision_info,
             apr_pool_t *pool,
             apr_pool_t *scratch_pool)
{
  noderev_t *result = apr_pcalloc(pool, sizeof(*result));
  svn_string_t *line;
  svn_boolean_t is_dir = FALSE;

  scratch_pool = svn_pool_create(scratch_pool);

  /* parse the noderev line-by-line until we find an empty line */
  result->original.offset = offset;
  while (1)
    {
      /* for this line, extract key and value. Ignore invalid values */
      svn_string_t key;
      svn_string_t value;
      char *sep;
      const char *start = file_content->data + offset
                        + revision_info->original.offset;
      const char *end = strchr(start, '\n');

      line = svn_string_ncreate(start, end - start, scratch_pool);
      offset += end - start + 1;

      /* empty line -> end of noderev data */
      if (line->len == 0)
        break;

      sep = strchr(line->data, ':');
      if (sep == NULL)
        continue;

      key.data = line->data;
      key.len = sep - key.data;
      *sep = 0;

      if (key.len + 2 > line->len)
        continue;

      value.data = sep + 2;
      value.len = line->len - (key.len + 2);

      /* translate (key, value) into noderev elements */
      if (key_matches(&key, "type"))
        is_dir = strcmp(value.data, "dir") == 0;
      else if (key_matches(&key, "pred"))
        SVN_ERR(parse_pred(&result->predecessor, fs, &value));
      else if (key_matches(&key, "text"))
        SVN_ERR(parse_representation(&result->text, fs, file_content,
                                     &value, revision_info,
                                     pool, scratch_pool));
      else if (key_matches(&key, "props"))
        SVN_ERR(parse_representation(&result->props, fs, file_content,
                                     &value, revision_info,
                                     pool, scratch_pool));
    }

  /* link noderev to revision info */
  result->revision = revision_info;
  result->original.size = offset - result->original.offset;

  svn_sort__array_insert(&result,
                         revision_info->node_revs,
                         svn_sort__bsearch_lower_bound(&offset,
                                                       revision_info->node_revs,
                                                       compare_noderev_offsets));

  /* if this is a directory, read and process that recursively */
  if (is_dir)
    SVN_ERR(parse_dir(fs, file_content, result->text,
                      pool, scratch_pool));

  /* done */
  svn_pool_destroy(scratch_pool);
  *noderev = result;

  return SVN_NO_ERROR;
}

/* Simple utility to print a REVISION number and make it appear immediately.
 */
static void
print_progress(svn_revnum_t revision)
{
  printf("%8ld", revision);
  fflush(stdout);
}

/* Read the content of the pack file staring at revision BASE and store it
 * in FS.  Use POOL for allocations.
 */
static svn_error_t *
read_pack_file(fs_fs_t *fs,
               svn_revnum_t base,
               apr_pool_t *pool)
{
  apr_array_header_t *manifest = NULL;
  apr_pool_t *local_pool = svn_pool_create(pool);
  apr_pool_t *iter_pool = svn_pool_create(local_pool);
  int i;
  svn_stringbuf_t *file_content;
  revision_pack_t *revisions;
  const char *pack_folder = get_pack_folder(fs, base, local_pool);

  /* read the whole pack file into memory */
  SVN_ERR(read_rev_or_pack_file(&file_content, fs, base, local_pool));

  /* create the revision container */
  revisions = apr_pcalloc(pool, sizeof(*revisions));
  revisions->base = base;
  revisions->fragments = NULL;
  revisions->info = apr_array_make(pool,
                                   fs->max_files_per_dir,
                                   sizeof(revision_info_t*));
  revisions->filesize = file_content->len;
  APR_ARRAY_PUSH(fs->packs, revision_pack_t*) = revisions;

  /* parse the manifest file */
  SVN_ERR(read_manifest(&manifest, fs, pack_folder, local_pool));
  if (manifest->nelts != fs->max_files_per_dir)
    return svn_error_create(SVN_ERR_FS_CORRUPT, NULL, NULL);

  /* process each revision in the pack file */
  for (i = 0; i < manifest->nelts; ++i)
    {
      apr_size_t root_node_offset;
      svn_string_t rev_content;

      /* create the revision info for the current rev */
      revision_info_t *info = apr_pcalloc(pool, sizeof(*info));
      info->node_revs = apr_array_make(iter_pool, 4, sizeof(noderev_t*));
      info->representations = apr_array_make(iter_pool, 4, sizeof(representation_t*));

      info->revision = base + i;
      info->original.offset = APR_ARRAY_IDX(manifest, i, apr_size_t);
      info->original.end = i+1 < manifest->nelts
                         ? APR_ARRAY_IDX(manifest, i+1 , apr_size_t)
                         : file_content->len;
      SVN_ERR(read_revision_header(&info->original.changes,
                                   &info->original.changes_len,
                                   &root_node_offset,
                                   file_content,
                                   APR_ARRAY_IDX(manifest, i , apr_size_t),
                                   info->original.end,
                                   iter_pool));

      /* put it into our containers */
      APR_ARRAY_PUSH(revisions->info, revision_info_t*) = info;
      APR_ARRAY_PUSH(fs->revisions, revision_info_t*) = info;

      /* cache the revision content */
      rev_content.data = file_content->data + info->original.offset;
      rev_content.len = info->original.end - info->original.offset;
      set_cached_content(fs->cache, info->revision, &rev_content);

      /* parse the revision content recursively. */
      SVN_ERR(read_noderev(&info->root_noderev, fs, file_content,
                           root_node_offset, info, pool, iter_pool));

      /* copy dynamically grown containers from temp into result pool */
      info->node_revs = apr_array_copy(pool, info->node_revs);
      info->representations = apr_array_copy(pool, info->representations);

      /* destroy temps */
      svn_pool_clear(iter_pool);
    }

  /* one more pack file processed */
  print_progress(base);
  svn_pool_destroy(local_pool);

  return SVN_NO_ERROR;
}

/* Read the content of REVSION file and store it in FS.
 * Use POOL for allocations.
 */
static svn_error_t *
read_revision_file(fs_fs_t *fs,
                   svn_revnum_t revision,
                   apr_pool_t *pool)
{
  apr_size_t root_node_offset;
  apr_pool_t *local_pool = svn_pool_create(pool);
  svn_stringbuf_t *file_content;
  svn_string_t rev_content;
  revision_pack_t *revisions = apr_pcalloc(pool, sizeof(*revisions));
  revision_info_t *info = apr_pcalloc(pool, sizeof(*info));

  /* read the whole pack file into memory */
  SVN_ERR(read_rev_or_pack_file(&file_content, fs, revision, local_pool));

  /* create the revision info for the current rev */
  info->node_revs = apr_array_make(pool, 4, sizeof(noderev_t*));
  info->representations = apr_array_make(pool, 4, sizeof(representation_t*));

  info->revision = revision;
  info->original.offset = 0;
  info->original.end = file_content->len;
  SVN_ERR(read_revision_header(&info->original.changes,
                               &info->original.changes_len,
                               &root_node_offset,
                               file_content,
                               0,
                               info->original.end,
                               local_pool));

  /* put it into our containers */
  APR_ARRAY_PUSH(fs->revisions, revision_info_t*) = info;

  /* create a pseudo-pack file container for just this rev to keep our
   * data structures as uniform as possible.
   */
  revisions->base = revision;
  revisions->fragments = NULL;
  revisions->info = apr_array_make(pool, 1, sizeof(revision_info_t*));
  revisions->filesize = file_content->len;
  APR_ARRAY_PUSH(revisions->info, revision_info_t*) = info;
  APR_ARRAY_PUSH(fs->packs, revision_pack_t*) = revisions;

  /* cache the revision content */
  rev_content.data = file_content->data + info->original.offset;
  rev_content.len = info->original.end - info->original.offset;
  set_cached_content(fs->cache, info->revision, &rev_content);

  /* parse the revision content recursively. */
  SVN_ERR(read_noderev(&info->root_noderev, fs, file_content,
                       root_node_offset, info,
                       pool, local_pool));
  APR_ARRAY_PUSH(info->node_revs, noderev_t*) = info->root_noderev;

  /* show progress every 1000 revs or so */
  if (revision % fs->max_files_per_dir == 0)
    print_progress(revision);

  svn_pool_destroy(local_pool);

  return SVN_NO_ERROR;
}

/* Read the repository at PATH beginning with revision START_REVISION and
 * return the result in *FS.  Allocate caches with MEMSIZE bytes total
 * capacity.  Use POOL for non-cache allocations.
 */
static svn_error_t *
read_revisions(fs_fs_t **fs,
               const char *path,
               svn_revnum_t start_revision,
               apr_size_t memsize,
               apr_pool_t *pool)
{
  svn_revnum_t revision;
  apr_size_t content_cache_size;
  apr_size_t window_cache_size;
  apr_size_t dir_cache_size;

  /* determine cache sizes */
  if (memsize < 100)
    memsize = 100;

  content_cache_size = memsize * 7 / 10 > 4000 ? 4000 : memsize * 7 / 10;
  window_cache_size = memsize * 2 / 10 * 1024 * 1024;
  dir_cache_size = (memsize / 10) * 16000;

  /* read repo format and such */
  SVN_ERR(fs_open(fs, path, pool));

  /* create data containers and caches */
  (*fs)->start_revision = start_revision
                        - (start_revision % (*fs)->max_files_per_dir);
  (*fs)->revisions = apr_array_make(pool,
                                    (*fs)->max_revision + 1 - (*fs)->start_revision,
                                    sizeof(revision_info_t *));
  (*fs)->packs = apr_array_make(pool,
                                ((*fs)->min_unpacked_rev - (*fs)->start_revision)
                                   / (*fs)->max_files_per_dir,
                                sizeof(revision_pack_t *));
  (*fs)->null_base = apr_pcalloc(pool, sizeof(*(*fs)->null_base));
  (*fs)->cache = create_content_cache
                    (apr_allocator_owner_get
                         (svn_pool_create_allocator(FALSE)),
                          content_cache_size * 1024 * 1024);
  (*fs)->dir_cache = create_dir_cache
                    (apr_allocator_owner_get
                         (svn_pool_create_allocator(FALSE)),
                          dir_cache_size);
  (*fs)->window_cache = create_window_cache
                    (apr_allocator_owner_get
                         (svn_pool_create_allocator(FALSE)),
                          10000, window_cache_size);

  /* read all packed revs */
  for ( revision = start_revision
      ; revision < (*fs)->min_unpacked_rev
      ; revision += (*fs)->max_files_per_dir)
    SVN_ERR(read_pack_file(*fs, revision, pool));

  /* read non-packed revs */
  for ( ; revision <= (*fs)->max_revision; ++revision)
    SVN_ERR(read_revision_file(*fs, revision, pool));

  return SVN_NO_ERROR;
}

/* Return the maximum number of decimal digits required to represent offsets
 * in the given PACK file.
 */
static apr_size_t
get_max_offset_len(const revision_pack_t *pack)
{
  /* the pack files may grow a few percent.
   * Fudge it up to be on safe side.
   */
  apr_size_t max_future_size = pack->filesize * 2 + 10000;
  apr_size_t result = 0;

  while (max_future_size > 0)
    {
      ++result;
      max_future_size /= 10;
    }

  return result;
}

/* Create the fragments container in PACK and add revision header fragments
 * to it.  Use POOL for allocations.
 */
static svn_error_t *
add_revisions_pack_heads(revision_pack_t *pack,
                         apr_pool_t *pool)
{
  int i;
  revision_info_t *info;
  apr_size_t offset_len = get_max_offset_len(pack);
  fragment_t fragment;

  /* allocate fragment arrays */

  int fragment_count = 1;
  for (i = 0; i < pack->info->nelts; ++i)
    {
      info = APR_ARRAY_IDX(pack->info, i, revision_info_t*);
      fragment_count += info->node_revs->nelts
                      + info->representations->nelts
                      + 2;
    }

  pack->target_offset = pack->info->nelts > 1 ? 64 : 0;
  pack->fragments = apr_array_make(pool,
                                   fragment_count,
                                   sizeof(fragment_t));

  /* put revision headers first */

  for (i = 0; i < pack->info->nelts - 1; ++i)
    {
      info = APR_ARRAY_IDX(pack->info, i, revision_info_t*);
      info->target.offset = pack->target_offset;

      fragment.data = info;
      fragment.kind = header_fragment;
      fragment.position = pack->target_offset;
      APR_ARRAY_PUSH(pack->fragments, fragment_t) = fragment;

      pack->target_offset += 2 * offset_len + 3;
    }

  info = APR_ARRAY_IDX(pack->info, pack->info->nelts - 1, revision_info_t*);
  info->target.offset = pack->target_offset;

  /* followed by the changes list */

  for (i = 0; i < pack->info->nelts; ++i)
    {
      info = APR_ARRAY_IDX(pack->info, i, revision_info_t*);

      info->target.changes = pack->target_offset - info->target.offset;
      info->target.changes_len = info->original.changes_len;

      fragment.data = info;
      fragment.kind = changes_fragment;
      fragment.position = pack->target_offset;
      APR_ARRAY_PUSH(pack->fragments, fragment_t) = fragment;

      pack->target_offset += info->original.changes_len;
    }

  return SVN_NO_ERROR;
}

/* For the revision given by INFO in FS, return the fragment container in
 * *FRAGMENTS and the current placement offset in *CURRENT_POS.
 */
static svn_error_t *
get_target_offset(apr_size_t **current_pos,
                  apr_array_header_t **fragments,
                  fs_fs_t *fs,
                  revision_info_t *info)
{
  int i;
  revision_pack_t *pack;
  svn_revnum_t revision = info->revision;

  /* identify the pack object */
  if (fs->min_unpacked_rev > revision)
    {
      i = (revision - fs->start_revision) / fs->max_files_per_dir;
    }
  else
    {
      i = (fs->min_unpacked_rev - fs->start_revision) / fs->max_files_per_dir;
      i += revision - fs->min_unpacked_rev;
    }

  /* extract the desired info from it */
  pack = APR_ARRAY_IDX(fs->packs, i, revision_pack_t*);
  *current_pos = &pack->target_offset;
  *fragments = pack->fragments;

  return SVN_NO_ERROR;
}

/* forward declaration */
static svn_error_t *
add_noderev_recursively(fs_fs_t *fs,
                        noderev_t *node,
                        apr_pool_t *pool);

/* Place fragments for the given REPRESENTATION of the given KIND, iff it
 * has not been covered, yet.  Place the base reps along the deltification
 * chain as far as those reps have not been covered, yet.  If REPRESENTATION
 * is a directory, recursively place its elements.
 *
 * Use POOL for allocations.
 */
static svn_error_t *
add_representation_recursively(fs_fs_t *fs,
                               representation_t *representation,
                               enum fragment_kind_t kind,
                               apr_pool_t *pool)
{
  apr_size_t *current_pos;
  apr_array_header_t *fragments;
  fragment_t fragment;

  /* place REPRESENTATION only once and only if it exists and will not
   * be covered later as a directory. */
  if (   representation == NULL
      || representation->covered
      || (representation->dir && kind != dir_fragment)
      || representation == fs->null_base)
    return SVN_NO_ERROR;

  /* add and place a fragment for REPRESENTATION */
  SVN_ERR(get_target_offset(&current_pos, &fragments,
                            fs, representation->revision));
  representation->target.offset = *current_pos;
  representation->covered = TRUE;

  fragment.data = representation;
  fragment.kind = kind;
  fragment.position = *current_pos;
  APR_ARRAY_PUSH(fragments, fragment_t) = fragment;

  /* determine the size of data to be added to the target file */
  if (   kind != dir_fragment
      && representation->delta_base && representation->delta_base->dir)
    {
      /* base rep is a dir -> would change -> need to store it as fulltext
       * in our target file */
      apr_pool_t *text_pool = svn_pool_create(pool);
      svn_stringbuf_t *content;

      SVN_ERR(get_combined_window(&content, fs, representation, text_pool));
      representation->target.size = content->len;
      *current_pos += representation->target.size + 13;

      svn_pool_destroy(text_pool);
    }
  else
    if (   kind == dir_fragment
        || (representation->delta_base && representation->delta_base->dir))
      {
        /* deltified directories may grow considerably */
        if (representation->original.size < 50)
          *current_pos += 300;
        else
          *current_pos += representation->original.size * 3 + 150;
      }
    else
      {
        /* plain / deltified content will not change but the header may
         * grow slightly due to larger offsets. */
        representation->target.size = representation->original.size;

        if (representation->delta_base &&
            (representation->delta_base != fs->null_base))
          *current_pos += representation->original.size + 50;
        else
          *current_pos += representation->original.size + 13;
      }

  /* follow the delta chain and place base revs immediately after this */
  if (representation->delta_base)
    SVN_ERR(add_representation_recursively(fs,
                                           representation->delta_base,
                                           kind,
                                           pool));

  /* finally, recurse into directories */
  if (representation->dir)
    {
      int i;
      apr_array_header_t *entries = representation->dir->entries;

      for (i = 0; i < entries->nelts; ++i)
        {
          direntry_t *entry = APR_ARRAY_IDX(entries, i, direntry_t *);
          if (entry->node)
            SVN_ERR(add_noderev_recursively(fs, entry->node, pool));
        }
    }

  return SVN_NO_ERROR;
}

/* Place fragments for the given NODE in FS, iff it has not been covered,
 * yet.  Place the reps (text, props) immediately after the node.
 *
 * Use POOL for allocations.
 */
static svn_error_t *
add_noderev_recursively(fs_fs_t *fs,
                        noderev_t *node,
                        apr_pool_t *pool)
{
  apr_size_t *current_pos;
  apr_array_header_t *fragments;
  fragment_t fragment;

  /* don't add it twice */
  if (node->covered)
    return SVN_NO_ERROR;

  /* add and place a fragment for NODE */
  SVN_ERR(get_target_offset(&current_pos, &fragments, fs, node->revision));
  node->covered = TRUE;
  node->target.offset = *current_pos;

  fragment.data = node;
  fragment.kind = noderev_fragment;
  fragment.position = *current_pos;
  APR_ARRAY_PUSH(fragments, fragment_t) = fragment;

  /* size may slightly increase */
  *current_pos += node->original.size + 40;

  /* recurse into representations */
  if (node->text && node->text->dir)
    SVN_ERR(add_representation_recursively(fs, node->text, dir_fragment, pool));
  else
    SVN_ERR(add_representation_recursively(fs, node->text, file_fragment, pool));

  SVN_ERR(add_representation_recursively(fs, node->props, property_fragment, pool));

  return SVN_NO_ERROR;
}

/* Place a fragment for the last revision in PACK. Use POOL for allocations.
 */
static svn_error_t *
add_revisions_pack_tail(revision_pack_t *pack,
                        apr_pool_t *pool)
{
  int i;
  revision_info_t *info;
  apr_size_t offset_len = get_max_offset_len(pack);
  fragment_t fragment;

  /* put final revision header last and fix up revision lengths */

  info = APR_ARRAY_IDX(pack->info, pack->info->nelts-1, revision_info_t*);

  fragment.data = info;
  fragment.kind = header_fragment;
  fragment.position = pack->target_offset;
  APR_ARRAY_PUSH(pack->fragments, fragment_t) = fragment;

  pack->target_offset += 2 * offset_len + 3;

  /* end of target file reached.  Store that info in all revs. */
  for (i = 0; i < pack->info->nelts; ++i)
    {
      info = APR_ARRAY_IDX(pack->info, i, revision_info_t*);
      info->target.end = pack->target_offset;
    }

  return SVN_NO_ERROR;
}

/* Place all fragments for all revisions / packs in FS.
 * Use POOL for allocations.
 */
static svn_error_t *
reorder_revisions(fs_fs_t *fs,
                  apr_pool_t *pool)
{
  int i, k;

  /* headers and changes */

  for (i = 0; i < fs->packs->nelts; ++i)
    {
      revision_pack_t *pack = APR_ARRAY_IDX(fs->packs, i, revision_pack_t*);
      SVN_ERR(add_revisions_pack_heads(pack, pool));
    }

  /* representations & nodes */

  for (i = fs->revisions->nelts-1; i >= 0; --i)
    {
      revision_info_t *info = APR_ARRAY_IDX(fs->revisions, i, revision_info_t*);
      for (k = info->node_revs->nelts - 1; k >= 0; --k)
        {
          noderev_t *node = APR_ARRAY_IDX(info->node_revs, k, noderev_t*);
          SVN_ERR(add_noderev_recursively(fs, node, pool));
        }

      if (info->revision % fs->max_files_per_dir == 0)
        print_progress(info->revision);
    }

  /* pack file tails */

  for (i = 0; i < fs->packs->nelts; ++i)
    {
      revision_pack_t *pack = APR_ARRAY_IDX(fs->packs, i, revision_pack_t*);
      SVN_ERR(add_revisions_pack_tail(pack, pool));
    }

  return SVN_NO_ERROR;
}

/* forward declaration */
static svn_error_t *
get_fragment_content(svn_string_t **content,
                     fs_fs_t *fs,
                     fragment_t *fragment,
                     apr_pool_t *pool);

/* Directory content may change and with it, the deltified representations
 * may significantly.  This function causes all directory target reps in
 * PACK of FS to be built and their new MD5 as well as rep sizes be updated.
 * We must do that before attempting to write noderevs.
 *
 * Use POOL for allocations.
 */
static svn_error_t *
update_noderevs(fs_fs_t *fs,
                revision_pack_t *pack,
                apr_pool_t *pool)
{
  int i;
  apr_pool_t *itempool = svn_pool_create(pool);

  for (i = 0; i < pack->fragments->nelts; ++i)
    {
      fragment_t *fragment = &APR_ARRAY_IDX(pack->fragments, i, fragment_t);
      if (fragment->kind == dir_fragment)
        {
          svn_string_t *content;

          /* request updated rep content but ignore the result.
           * We are only interested in the MD5, content and rep size updates. */
          SVN_ERR(get_fragment_content(&content, fs, fragment, itempool));
          svn_pool_clear(itempool);
        }
    }

  svn_pool_destroy(itempool);

  return SVN_NO_ERROR;
}

/* Determine the target size of the FRAGMENT in FS and return the value
 * in *LENGTH.  If ADD_PADDING has been set, slightly fudge the numbers
 * to account for changes in offset lengths etc.  Use POOL for temporary
 * allocations.
 */
static svn_error_t *
get_content_length(apr_size_t *length,
                   fs_fs_t *fs,
                   fragment_t *fragment,
                   svn_boolean_t add_padding,
                   apr_pool_t *pool)
{
  svn_string_t *content;

  SVN_ERR(get_fragment_content(&content, fs, fragment, pool));
  if (add_padding)
    switch (fragment->kind)
      {
        case dir_fragment:
          *length = content->len + 16;
          break;
        case noderev_fragment:
          *length = content->len + 3;
          break;
        default:
          *length = content->len;
          break;
      }
  else
    *length = content->len;

  return SVN_NO_ERROR;
}

/* Move the FRAGMENT to global file offset NEW_POSITION.  Update the target
 * location info of the underlying object as well.
 */
static void
move_fragment(fragment_t *fragment,
              apr_size_t new_position)
{
  revision_info_t *info;
  representation_t *representation;
  noderev_t *node;

  /* move the fragment */
  fragment->position = new_position;

  /* move the underlying object */
  switch (fragment->kind)
    {
      case header_fragment:
        info = fragment->data;
        info->target.offset = new_position;
        break;

      case changes_fragment:
        info = fragment->data;
        info->target.changes = new_position - info->target.offset;
        break;

      case property_fragment:
      case file_fragment:
      case dir_fragment:
        representation = fragment->data;
        representation->target.offset = new_position;
        break;

      case noderev_fragment:
        node = fragment->data;
        node->target.offset = new_position;
        break;
    }
}

/* Move the fragments in PACK's target fragment list to their final offsets.
 * This may require several iterations if the fudge factors turned out to
 * be insufficient.  Use POOL for allocations.
 */
static svn_error_t *
pack_revisions(fs_fs_t *fs,
               revision_pack_t *pack,
               apr_pool_t *pool)
{
  int i;
  fragment_t *fragment, *next;
  svn_boolean_t needed_to_expand;
  revision_info_t *info;
  apr_size_t current_pos, len, old_len;

  apr_pool_t *itempool = svn_pool_create(pool);

  /* update all directory reps. Chances are that most of the target rep
   * sizes are now close to accurate. */
  SVN_ERR(update_noderevs(fs, pack, pool));

  /* compression phase: pack all fragments tightly with only a very small
   * fudge factor.  This should cause offsets to shrink, thus all the
   * actual fragment rate should tend to be even smaller afterwards. */
  current_pos = pack->info->nelts > 1 ? 64 : 0;
  for (i = 0; i + 1 < pack->fragments->nelts; ++i)
    {
      fragment = &APR_ARRAY_IDX(pack->fragments, i, fragment_t);
      SVN_ERR(get_content_length(&len, fs, fragment, TRUE, itempool));
      move_fragment(fragment, current_pos);
      current_pos += len;

      svn_pool_clear(itempool);
    }

  /* don't forget the final fragment (last revision's revision header) */
  fragment = &APR_ARRAY_IDX(pack->fragments, pack->fragments->nelts-1, fragment_t);
  fragment->position = current_pos;

  /* expansion phase: check whether all fragments fit into their allotted
   * slots.  Grow them geometrically if they don't fit.  Retry until they
   * all do fit.
   * Note: there is an upper limit to which fragments can grow.  So, this
   * loop will terminate.  Often, no expansion will be necessary at all. */
  do
    {
      needed_to_expand = FALSE;
      current_pos = pack->info->nelts > 1 ? 64 : 0;

      for (i = 0; i + 1 < pack->fragments->nelts; ++i)
        {
          fragment = &APR_ARRAY_IDX(pack->fragments, i, fragment_t);
          next = &APR_ARRAY_IDX(pack->fragments, i + 1, fragment_t);
          old_len = next->position - fragment->position;

          SVN_ERR(get_content_length(&len, fs, fragment, FALSE, itempool));

          if (len > old_len)
            {
              len = (apr_size_t)(len * 1.1) + 10;
              needed_to_expand = TRUE;
            }
          else
            len = old_len;

          if (i == pack->info->nelts - 1)
            {
              info = APR_ARRAY_IDX(pack->info, pack->info->nelts - 1, revision_info_t*);
              info->target.offset = current_pos;
            }

          move_fragment(fragment, current_pos);
          current_pos += len;

          svn_pool_clear(itempool);
        }

      fragment = &APR_ARRAY_IDX(pack->fragments, pack->fragments->nelts-1, fragment_t);
      fragment->position = current_pos;

      /* update the revision
       * sizes (they all end at the end of the pack file now) */
      SVN_ERR(get_content_length(&len, fs, fragment, FALSE, itempool));
      current_pos += len;

      for (i = 0; i < pack->info->nelts; ++i)
        {
          info = APR_ARRAY_IDX(pack->info, i, revision_info_t*);
          info->target.end = current_pos;
        }
    }
  while (needed_to_expand);

  svn_pool_destroy(itempool);

  return SVN_NO_ERROR;
}

/* Write reorg'ed target content for PACK in FS.  Use POOL for allocations.
 */
static svn_error_t *
write_revisions(fs_fs_t *fs,
                revision_pack_t *pack,
                apr_pool_t *pool)
{
  int i;
  fragment_t *fragment = NULL;
  svn_string_t *content;

  apr_pool_t *itempool = svn_pool_create(pool);
  apr_pool_t *iterpool = svn_pool_create(pool);

  apr_file_t *file;
  apr_size_t current_pos = 0;
  svn_stringbuf_t *null_buffer = svn_stringbuf_create_empty(iterpool);

  /* create the target file */
  const char *dir = apr_psprintf(iterpool, "%s/new/%ld%s",
                                  fs->path, pack->base / fs->max_files_per_dir,
                                  pack->info->nelts > 1 ? ".pack" : "");
  SVN_ERR(svn_io_make_dir_recursively(dir, pool));
  SVN_ERR(svn_io_file_open(&file,
                            pack->info->nelts > 1
                              ? apr_psprintf(iterpool, "%s/pack", dir)
                              : apr_psprintf(iterpool, "%s/%ld", dir, pack->base),
                            APR_WRITE | APR_CREATE | APR_BUFFERED,
                            APR_OS_DEFAULT,
                            iterpool));

  /* write all fragments */
  for (i = 0; i < pack->fragments->nelts; ++i)
    {
      apr_size_t padding;

      /* get fragment content to write */
      fragment = &APR_ARRAY_IDX(pack->fragments, i, fragment_t);
      SVN_ERR(get_fragment_content(&content, fs, fragment, itempool));
      SVN_ERR_ASSERT(fragment->position >= current_pos);

      /* number of bytes between this and the previous fragment */
      if (   fragment->kind == header_fragment
          && i+1 < pack->fragments->nelts)
        /* special case: header fragments are aligned to the slot end */
        padding = APR_ARRAY_IDX(pack->fragments, i+1, fragment_t).position -
                  content->len - current_pos;
      else
        /* standard case: fragments are aligned to the slot start */
        padding = fragment->position - current_pos;

      /* write padding between fragments */
      if (padding)
        {
          while (null_buffer->len < padding)
            svn_stringbuf_appendbyte(null_buffer, 0);

          SVN_ERR(svn_io_file_write_full(file,
                                         null_buffer->data,
                                         padding,
                                         NULL,
                                         itempool));
          current_pos += padding;
        }

      /* write fragment content */
      SVN_ERR(svn_io_file_write_full(file,
                                     content->data,
                                     content->len,
                                     NULL,
                                     itempool));
      current_pos += content->len;

      svn_pool_clear(itempool);
    }

  apr_file_close(file);

  /* write new manifest file */
  if (pack->info->nelts > 1)
    {
      svn_stream_t *stream;
      SVN_ERR(svn_io_file_open(&file,
                                apr_psprintf(iterpool, "%s/manifest", dir),
                                APR_WRITE | APR_CREATE | APR_BUFFERED,
                                APR_OS_DEFAULT,
                                iterpool));
      stream = svn_stream_from_aprfile2(file, FALSE, iterpool);

      for (i = 0; i < pack->info->nelts; ++i)
        {
          revision_info_t *info = APR_ARRAY_IDX(pack->info, i,
                                                revision_info_t *);
          SVN_ERR(svn_stream_printf(stream, itempool,
                                    "%" APR_SIZE_T_FMT "\n",
                                    info->target.offset));
          svn_pool_clear(itempool);
        }
    }

  /* cleanup */
  svn_pool_destroy(itempool);
  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

/* Write reorg'ed target content for all revisions in FS.  To maximize
 * data locality, pack and write in one go per pack file.
 * Use POOL for allocations.
 */
static svn_error_t *
pack_and_write_revisions(fs_fs_t *fs,
                         apr_pool_t *pool)
{
  int i;

  SVN_ERR(svn_io_make_dir_recursively(apr_psprintf(pool, "%s/new",
                                                   fs->path),
                                      pool));

  for (i = 0; i < fs->packs->nelts; ++i)
    {
      revision_pack_t *pack = APR_ARRAY_IDX(fs->packs, i, revision_pack_t*);
      if (pack->base % fs->max_files_per_dir == 0)
        print_progress(pack->base);

      SVN_ERR(pack_revisions(fs, pack, pool));
      SVN_ERR(write_revisions(fs, pack, pool));
    }

  return SVN_NO_ERROR;
}

/* For the directory REPRESENTATION in FS, construct the new (target)
 * serialized plaintext representation and return it in *CONTENT.
 * Allocate the result in POOL and temporaries in SCRATCH_POOL.
 */
static svn_error_t *
get_updated_dir(svn_string_t **content,
                fs_fs_t *fs,
                representation_t *representation,
                apr_pool_t *pool,
                apr_pool_t *scratch_pool)
{
  apr_hash_t *hash;
  apr_pool_t *hash_pool = svn_pool_create(scratch_pool);
  apr_array_header_t *dir = representation->dir->entries;
  int i;
  svn_stream_t *stream;
  svn_stringbuf_t *result;

  /* get the original content */
  SVN_ERR(read_dir(&hash, fs, representation, scratch_pool));
  hash = apr_hash_copy(hash_pool, hash);

  /* update all entries */
  for (i = 0; i < dir->nelts; ++i)
    {
      char buffer[256];
      svn_string_t *new_val;
      apr_size_t pos;

      /* find the original entry for for the current name */
      direntry_t *entry = APR_ARRAY_IDX(dir, i, direntry_t *);
      svn_string_t *str_val = apr_hash_get(hash, entry->name, entry->name_len);
      if (str_val == NULL)
        return svn_error_createf(SVN_ERR_FS_CORRUPT, NULL,
                                 _("Dir entry '%s' not found"), entry->name);

      SVN_ERR_ASSERT(str_val->len < sizeof(buffer));

      /* create and updated node ID */
      memcpy(buffer, str_val->data, str_val->len+1);
      pos = strchr(buffer, '/') - buffer + 1;
      pos += svn__ui64toa(buffer + pos, entry->node->target.offset - entry->node->revision->target.offset);
      new_val = svn_string_ncreate(buffer, pos, hash_pool);

      /* store it in the hash */
      apr_hash_set(hash, entry->name, entry->name_len, new_val);
    }

  /* serialize the updated hash */
  result = svn_stringbuf_create_ensure(representation->target.size, pool);
  stream = svn_stream_from_stringbuf(result, hash_pool);
  SVN_ERR(svn_hash_write2(hash, stream, SVN_HASH_TERMINATOR, hash_pool));
  svn_pool_destroy(hash_pool);

  /* done */
  *content = svn_stringbuf__morph_into_string(result);

  return SVN_NO_ERROR;
}

/* Calculate the delta representation for the given CONTENT and BASE.
 * Return the rep in *DIFF.  Use POOL for allocations.
 */
static svn_error_t *
diff_stringbufs(svn_stringbuf_t *diff,
                svn_string_t *base,
                svn_string_t *content,
                apr_pool_t *pool)
{
  svn_txdelta_window_handler_t diff_wh;
  void *diff_whb;

  svn_stream_t *stream;
  svn_stream_t *source = svn_stream_from_string(base, pool);
  svn_stream_t *target = svn_stream_from_stringbuf(diff, pool);

  /* Prepare to write the svndiff data. */
  svn_txdelta_to_svndiff3(&diff_wh,
                          &diff_whb,
                          target,
                          1,
                          SVN_DELTA_COMPRESSION_LEVEL_DEFAULT,
                          pool);

  /* create delta stream */
  stream = svn_txdelta_target_push(diff_wh, diff_whb, source, pool);

  /* run delta */
  SVN_ERR(svn_stream_write(stream, content->data, &content->len));
  SVN_ERR(svn_stream_close(stream));

  return SVN_NO_ERROR;
}

/* Update the noderev id value for KEY in the textual noderev representation
 * in NODE_REV.  Take the new id from NODE.  This is a no-op if the KEY
 * cannot be found.
 */
static void
update_id(svn_stringbuf_t *node_rev,
          const char *key,
          noderev_t *node)
{
  char *newline_pos = 0;
  char *pos;

  /* we need to update the offset only -> find its position */
  pos = strstr(node_rev->data, key);
  if (pos)
    pos = strchr(pos, '/');
  if (pos)
    newline_pos = strchr(++pos, '\n');

  if (pos && newline_pos)
    {
      /* offset data has been found -> replace it */
      char temp[SVN_INT64_BUFFER_SIZE];
      apr_size_t len = svn__i64toa(temp, node->target.offset - node->revision->target.offset);
      svn_stringbuf_replace(node_rev,
                            pos - node_rev->data, newline_pos - pos,
                            temp, len);
    }
}

/* Update the representation id value for KEY in the textual noderev
 * representation in NODE_REV.  Take the offset, sizes and new MD5 from
 * REPRESENTATION.  Use SCRATCH_POOL for allocations.
 * This is a no-op if the KEY cannot be found.
 */
static void
update_text(svn_stringbuf_t *node_rev,
            const char *key,
            representation_t *representation,
            apr_pool_t *scratch_pool)
{
  apr_size_t key_len = strlen(key);
  char *pos = strstr(node_rev->data, key);
  char *val_pos;

  if (!pos)
    return;

  val_pos = pos + key_len;
  if (representation->dir)
    {
      /* for directories, we need to write all rep info anew */
      char *newline_pos = strchr(val_pos, '\n');
      svn_checksum_t checksum;
      const char* temp = apr_psprintf(scratch_pool, "%ld %" APR_SIZE_T_FMT " %"
                                      APR_SIZE_T_FMT" %" APR_SIZE_T_FMT " %s",
                                      representation->revision->revision,
                                      representation->target.offset - representation->revision->target.offset,
                                      representation->target.size,
                                      representation->dir->size,
                                      svn_checksum_to_cstring(&checksum,
                                                              scratch_pool));

      checksum.digest = representation->dir->target_md5;
      checksum.kind = svn_checksum_md5;
      svn_stringbuf_replace(node_rev,
                            val_pos - node_rev->data, newline_pos - val_pos,
                            temp, strlen(temp));
    }
  else
    {
      /* ordinary representation: replace offset and rep size only.
       * Content size and checksums are unchanged. */
      const char* temp;
      char *end_pos = strchr(val_pos, ' ');

      val_pos = end_pos + 1;
      end_pos = strchr(strchr(val_pos, ' ') + 1, ' ');
      temp = apr_psprintf(scratch_pool, "%" APR_SIZE_T_FMT " %" APR_SIZE_T_FMT,
                          representation->target.offset - representation->revision->target.offset,
                          representation->target.size);

      svn_stringbuf_replace(node_rev,
                            val_pos - node_rev->data, end_pos - val_pos,
                            temp, strlen(temp));
    }
}

/* Get the target content (data block as to be written to the file) for
 * the given FRAGMENT in FS.  Return the content in *CONTENT.  Use POOL
 * for allocations.
 *
 * Note that, as a side-effect, this will update the target rep. info for
 * directories.
 */
static svn_error_t *
get_fragment_content(svn_string_t **content,
                     fs_fs_t *fs,
                     fragment_t *fragment,
                     apr_pool_t *pool)
{
  revision_info_t *info;
  representation_t *representation;
  noderev_t *node;
  svn_string_t *revision_content, *base_content;
  svn_stringbuf_t *header, *node_rev, *text;
  apr_size_t header_size;
  svn_checksum_t *checksum = NULL;

  switch (fragment->kind)
    {
      /* revision headers can be constructed from target position info */
      case header_fragment:
        info = fragment->data;
        *content = svn_string_createf(pool,
                                      "\n%" APR_SIZE_T_FMT " %" APR_SIZE_T_FMT "\n",
                                      info->root_noderev->target.offset - info->target.offset,
                                      info->target.changes);
        return SVN_NO_ERROR;

      /* The changes list remains untouched */
      case changes_fragment:
        info = fragment->data;
        SVN_ERR(get_content(&revision_content, fs, info->revision, pool));

        *content = svn_string_create_empty(pool);
        (*content)->data = revision_content->data + info->original.changes;
        (*content)->len = info->target.changes_len;
        return SVN_NO_ERROR;

      /* property and file reps get new headers any need to be rewritten,
       * iff the base rep is a directory.  The actual (deltified) content
       * remains unchanged, though.  MD5 etc. do not change. */
      case property_fragment:
      case file_fragment:
        representation = fragment->data;
        SVN_ERR(get_content(&revision_content, fs,
                            representation->revision->revision, pool));

        if (representation->delta_base)
          if (representation->delta_base->dir)
            {
              /* if the base happens to be a directory, reconstruct the
               * full text and represent it as PLAIN rep. */
              SVN_ERR(get_combined_window(&text, fs, representation, pool));
              representation->target.size = text->len;

              svn_stringbuf_insert(text, 0, "PLAIN\n", 6);
              svn_stringbuf_appendcstr(text, "ENDREP\n");
              *content = svn_stringbuf__morph_into_string(text);

              return SVN_NO_ERROR;
            }
          else
            /* construct a new rep header */
            if (representation->delta_base == fs->null_base)
              header = svn_stringbuf_create("DELTA\n", pool);
            else
              header = svn_stringbuf_createf(pool,
                                             "DELTA %ld %" APR_SIZE_T_FMT " %" APR_SIZE_T_FMT "\n",
                                             representation->delta_base->revision->revision,
                                             representation->delta_base->target.offset
                                             - representation->delta_base->revision->target.offset,
                                             representation->delta_base->target.size);
        else
          header = svn_stringbuf_create("PLAIN\n", pool);

        /* if it exists, the actual delta base is unchanged. Hence, this
         * rep is unchanged even if it has been deltified. */
        header_size = strchr(revision_content->data +
                             representation->original.offset, '\n') -
                      revision_content->data -
                      representation->original.offset + 1;
        svn_stringbuf_appendbytes(header,
                                  revision_content->data +
                                  representation->original.offset +
                                  header_size,
                                  representation->original.size);
        svn_stringbuf_appendcstr(header, "ENDREP\n");
        *content = svn_stringbuf__morph_into_string(header);
        return SVN_NO_ERROR;

      /* directory reps need to be rewritten (and deltified) completely.
       * As a side-effect, update the MD5 and target content size. */
      case dir_fragment:
        /* construct new content and update MD5 */
        representation = fragment->data;
        SVN_ERR(get_updated_dir(&revision_content, fs, representation,
                                pool, pool));
        SVN_ERR(svn_checksum(&checksum, svn_checksum_md5,
                             revision_content->data, revision_content->len,
                             pool));
        memcpy(representation->dir->target_md5,
               checksum->digest,
               sizeof(representation->dir->target_md5));

        /* deltify against the base rep if necessary */
        if (representation->delta_base)
          {
            if (representation->delta_base->dir == NULL)
              {
                /* dummy or non-dir base rep -> self-compress only */
                header = svn_stringbuf_create("DELTA\n", pool);
                base_content = svn_string_create_empty(pool);
              }
            else
              {
                /* deltify against base rep (which is a directory, too)*/
                representation_t *base_rep = representation->delta_base;
                header = svn_stringbuf_createf(pool,
                                               "DELTA %ld %" APR_SIZE_T_FMT " %" APR_SIZE_T_FMT "\n",
                                               base_rep->revision->revision,
                                               base_rep->target.offset - base_rep->revision->target.offset,
                                               base_rep->target.size);
                SVN_ERR(get_updated_dir(&base_content, fs, base_rep,
                                        pool, pool));
              }

            /* run deltification and update target content size */
            header_size = header->len;
            SVN_ERR(diff_stringbufs(header, base_content,
                                    revision_content, pool));
            representation->dir->size = revision_content->len;
            representation->target.size = header->len - header_size;
            svn_stringbuf_appendcstr(header, "ENDREP\n");
            *content = svn_stringbuf__morph_into_string(header);
          }
        else
          {
            /* no delta base (not even a dummy) -> PLAIN rep */
            representation->target.size = revision_content->len;
            representation->dir->size = revision_content->len;
            *content = svn_string_createf(pool, "PLAIN\n%sENDREP\n",
                                          revision_content->data);
          }

        return SVN_NO_ERROR;

      /* construct the new noderev content.  No side-effects.*/
      case noderev_fragment:
        /* get the original noderev as string */
        node = fragment->data;
        SVN_ERR(get_content(&revision_content, fs,
                            node->revision->revision, pool));
        node_rev = svn_stringbuf_ncreate(revision_content->data +
                                         node->original.offset,
                                         node->original.size,
                                         pool);

        /* update the values that may have hanged for target */
        update_id(node_rev, "id: ", node);
        update_id(node_rev, "pred: ", node->predecessor);
        update_text(node_rev, "text: ", node->text, pool);
        update_text(node_rev, "props: ", node->props, pool);

        *content = svn_stringbuf__morph_into_string(node_rev);
        return SVN_NO_ERROR;
    }

  SVN_ERR_ASSERT(0);

  return SVN_NO_ERROR;
}

/* In the repository at PATH, restore the original content in case we ran
 * this reorg tool before.  Use POOL for allocations.
 */
static svn_error_t *
prepare_repo(const char *path, apr_pool_t *pool)
{
  svn_node_kind_t kind;

  const char *old_path = svn_dirent_join(path, "db/old", pool);
  const char *new_path = svn_dirent_join(path, "new", pool);
  const char *revs_path = svn_dirent_join(path, "db/revs", pool);
  const char *old_rep_cache_path = svn_dirent_join(path, "db/rep-cache.db.old", pool);
  const char *rep_cache_path = svn_dirent_join(path, "db/rep-cache.db", pool);

  /* is there a backup? */
  SVN_ERR(svn_io_check_path(old_path, &kind, pool));
  if (kind == svn_node_dir)
    {
      /* yes, restore the org content from it */
      SVN_ERR(svn_io_remove_dir2(new_path, TRUE, NULL, NULL, pool));
      SVN_ERR(svn_io_file_move(revs_path, new_path, pool));
      SVN_ERR(svn_io_file_move(old_path, revs_path, pool));
      SVN_ERR(svn_io_remove_dir2(new_path, TRUE, NULL, NULL, pool));
    }

  /* same for the rep cache db */
  SVN_ERR(svn_io_check_path(old_rep_cache_path, &kind, pool));
  if (kind == svn_node_file)
    SVN_ERR(svn_io_file_move(old_rep_cache_path, rep_cache_path, pool));

  return SVN_NO_ERROR;
}

/* In the repository at PATH, create a backup of the orig content and
 * replace it with the reorg'ed. Use POOL for allocations.
 */
static svn_error_t *
activate_new_revs(const char *path, apr_pool_t *pool)
{
  svn_node_kind_t kind;

  const char *old_path = svn_dirent_join(path, "db/old", pool);
  const char *new_path = svn_dirent_join(path, "new", pool);
  const char *revs_path = svn_dirent_join(path, "db/revs", pool);
  const char *old_rep_cache_path = svn_dirent_join(path, "db/rep-cache.db.old", pool);
  const char *rep_cache_path = svn_dirent_join(path, "db/rep-cache.db", pool);

  /* if there is no backup, yet, move the current repo content to the backup
   * and place it with the new (reorg'ed) data. */
  SVN_ERR(svn_io_check_path(old_path, &kind, pool));
  if (kind == svn_node_none)
    {
      SVN_ERR(svn_io_file_move(revs_path, old_path, pool));
      SVN_ERR(svn_io_file_move(new_path, revs_path, pool));
    }

  /* same for the rep cache db */
  SVN_ERR(svn_io_check_path(old_rep_cache_path, &kind, pool));
  if (kind == svn_node_none)
    SVN_ERR(svn_io_file_move(rep_cache_path, old_rep_cache_path, pool));

  return SVN_NO_ERROR;
}

/* Write tool usage info text to OSTREAM using PROGNAME as a prefix and
 * POOL for allocations.
 */
static void
print_usage(svn_stream_t *ostream, const char *progname,
            apr_pool_t *pool)
{
  svn_error_clear(svn_stream_printf(ostream, pool,
     "\n"
     "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!! WARNING !!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n"
     "!!! This is an experimental tool. Don't use it on production data !!!\n"
     "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n"
     "\n"
     "Usage: %s <repo> <cachesize>\n"
     "\n"
     "Optimize the repository at local path <repo> staring from revision 0.\n"
     "Use up to <cachesize> MB of memory for caching. This does not include\n"
     "temporary representation of the repository structure, i.e. the actual\n"
     "memory will be higher and <cachesize> be the lower limit.\n",
     progname));
}

/* linear control flow */
int main(int argc, const char *argv[])
{
  apr_pool_t *pool;
  svn_stream_t *ostream;
  svn_error_t *svn_err;
  const char *repo_path = NULL;
  svn_revnum_t start_revision = 0;
  apr_size_t memsize = 0;
  apr_uint64_t temp = 0;
  fs_fs_t *fs;

  apr_initialize();
  atexit(apr_terminate);

  pool = apr_allocator_owner_get(svn_pool_create_allocator(FALSE));

  svn_err = svn_stream_for_stdout(&ostream, pool);
  if (svn_err)
    {
      svn_handle_error2(svn_err, stdout, FALSE, ERROR_TAG);
      return 2;
    }

  if (argc != 3)
    {
      print_usage(ostream, argv[0], pool);
      return 2;
    }

  svn_err = svn_cstring_strtoui64(&temp, argv[2], 0, APR_SIZE_MAX, 10);
  if (svn_err)
    {
      print_usage(ostream, argv[0], pool);
      svn_error_clear(svn_err);
      return 2;
    }

  memsize = (apr_size_t)temp;
  repo_path = argv[1];
  start_revision = 0;

  printf("\nPreparing repository\n");
  svn_err = prepare_repo(repo_path, pool);

  if (!svn_err)
    {
      printf("Reading revisions\n");
      svn_err = read_revisions(&fs, repo_path, start_revision, memsize, pool);
    }

  if (!svn_err)
    {
      printf("\nReordering revision content\n");
      svn_err = reorder_revisions(fs, pool);
    }

  if (!svn_err)
    {
      printf("\nPacking and writing revisions\n");
      svn_err = pack_and_write_revisions(fs, pool);
    }

  if (!svn_err)
    {
      printf("\nSwitch to new revs\n");
      svn_err = activate_new_revs(repo_path, pool);
    }

  if (svn_err)
    {
      svn_handle_error2(svn_err, stdout, FALSE, ERROR_TAG);
      return 2;
    }

  return 0;
}
