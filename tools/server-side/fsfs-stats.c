/* diff.c -- test driver for text diffs
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
#include <sys/stat.h>

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
#include <../test/testutil.h>

#ifndef _
#define _(x) x
#endif

#define ERROR_TAG "diff: "

typedef enum rep_kind_t
{
  unused_rep,

  dir_property_rep,

  file_property_rep,

  /* a directory rep (including PLAIN / DELTA header) */
  dir_rep,

  /* a file rep (including PLAIN / DELTA header) */
  file_rep
} rep_kind_t;

/* A representation fragment.
 */
typedef struct representation_t
{
  /* absolute offset in the file */
  apr_size_t offset;

  /* item length in bytes */
  apr_size_t size;

  apr_size_t expanded_size;

  /* deltification base, or NULL if there is none */
  struct representation_t *delta_base;

  /* revision that contains this representation
   * (may be referenced by other revisions, though) */
  
  apr_uint32_t revision;
  apr_uint32_t ref_count;

  struct
    {
      /* length of the PLAIN / DELTA line in the source file in bytes */
      apr_size_t header_size : 12;

      rep_kind_t kind : 3;

      /* the source content has a PLAIN header, so we may simply copy the
      * source content into the target */
      svn_boolean_t is_plain : 1;
    };
  
} representation_t;

/* Represents a single revision.
 * There will be only one instance per revision. */
typedef struct revision_info_t
{
  /* number of this revision */
  svn_revnum_t revision;

  /* pack file offset (manifest value), 0 for non-packed files */
  apr_size_t offset;

  /* offset of the changes list relative to OFFSET */
  apr_size_t changes;

  /* length of the changes list on bytes */
  apr_size_t changes_len;

  /* offset of the changes list relative to OFFSET */
  apr_size_t change_count;

  /* first offset behind the revision data in the pack file (file length
   * for non-packed revs) */
  apr_size_t end;

  apr_size_t dir_noderev_count;
  apr_size_t file_noderev_count;
  apr_size_t dir_noderev_size;
  apr_size_t file_noderev_size;
  
  /* all representation_t of this revision (in no particular order),
   * i.e. those that point back to this struct */
  apr_array_header_t *representations;
} revision_info_t;

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

  /* empty representation.
   * Used as a dummy base for DELTA reps without base. */
  representation_t *null_base;

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
rev_or_pack_file_size(apr_off_t *file_size,
                      fs_fs_t *fs,
                      svn_revnum_t rev,
                      apr_pool_t *pool)
{
  apr_file_t *file;
  apr_finfo_t finfo;

  SVN_ERR(open_rev_or_pack_file(&file, fs, rev, pool));
  SVN_ERR(svn_io_file_info_get(&finfo, APR_FINFO_SIZE, file, pool));
  SVN_ERR(svn_io_file_close(file, pool));

  *file_size = finfo.size;
  return SVN_NO_ERROR;
}

/* Get the file content of revision REVISION in FS and return it in *DATA.
 * Use SCRATCH_POOL for temporary allocations.
 */
static svn_error_t *
get_content(svn_stringbuf_t **content,
            fs_fs_t *fs,
            svn_revnum_t revision,
            apr_off_t offset,
            apr_size_t len,
            apr_pool_t *pool)
{
  apr_file_t *file;
  apr_pool_t * file_pool = svn_pool_create(pool);

  SVN_ERR(open_rev_or_pack_file(&file, fs, revision, file_pool));
  SVN_ERR(svn_io_file_seek(file, APR_SET, &offset, pool));

  *content = svn_stringbuf_create_ensure(len, pool);
  (*content)->len = len;
  SVN_ERR(svn_io_file_read_full2(file, (*content)->data, len,
                                 NULL, NULL, pool));
  svn_pool_destroy(file_pool);

  return SVN_NO_ERROR;
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

/* Return the cached txdelta window stored in REPRESENTAION within FS.
 * If that has not been found in cache, return NULL.
 */
static svn_stringbuf_t *
get_cached_window(fs_fs_t *fs,
                  representation_t *representation,
                  apr_pool_t *pool)
{
  svn_revnum_t revision = representation->revision;
  apr_size_t offset = representation->offset;

  apr_size_t i = get_window_cache_index(fs, revision, offset);
  window_cache_entry_t *entry = &fs->window_cache->entries[i];

  return entry->offset == offset && entry->revision == revision
    ? svn_stringbuf_dup(entry->window, pool)
    : NULL;
}

/* Cache the undeltified txdelta WINDOW for REPRESENTAION within FS.
 */
static void
set_cached_window(fs_fs_t *fs,
                  representation_t *representation,
                  svn_stringbuf_t *window)
{
  /* select entry */
  svn_revnum_t revision = representation->revision;
  apr_size_t offset = representation->offset;

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

/* Given REV in FS, set *REV_OFFSET to REV's offset in the packed file.
   Use POOL for temporary allocations. */
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

static svn_error_t *
read_revision_header(apr_size_t *changes,
                     apr_size_t *changes_len,
                     apr_size_t *root_noderev,
                     svn_stringbuf_t *file_content,
                     apr_pool_t *pool)
{
  char buf[64];
  const char *line;
  const char *space;
  apr_uint64_t val;
  apr_size_t len;
  
  /* Read in this last block, from which we will identify the last line. */
  len = sizeof(buf);
  if (len > file_content->len)
    len = file_content->len;
  
  memcpy(buf, file_content->data + file_content->len - len, len);

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

  *(char *)space = 0;
  
  SVN_ERR(svn_cstring_strtoui64(&val, line+1, 0, APR_SIZE_MAX, 10));
  *root_noderev = (apr_size_t)val;
  SVN_ERR(svn_cstring_strtoui64(&val, space+1, 0, APR_SIZE_MAX, 10));
  *changes = (apr_size_t)val;
  *changes_len = file_content->len - *changes - (buf + len - line) + 1;

  return SVN_NO_ERROR;
}

static svn_error_t *
read_format(int *pformat, int *max_files_per_dir,
            const char *path, apr_pool_t *pool)
{
  svn_error_t *err;
  apr_file_t *file;
  char buf[80];
  apr_size_t len;

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

static svn_error_t *
read_number(svn_revnum_t *result, const char *path, apr_pool_t *pool)
{
  svn_stringbuf_t *content;
  apr_int64_t number;
  
  SVN_ERR(svn_stringbuf_from_file2(&content, path, pool));

  content->data[content->len-1] = 0;
  SVN_ERR(svn_cstring_atoi64(&number, content->data));
  *result = (svn_revnum_t)number;

  return SVN_NO_ERROR;
}

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
    
  SVN_ERR(read_number(&(*fs)->min_unpacked_rev,
                      svn_dirent_join(path, "db/min-unpacked-rev", pool),
                      pool));
  return read_number(&(*fs)->max_revision,
                     svn_dirent_join(path, "db/current", pool),
                     pool);
}

static svn_boolean_t
key_matches(svn_string_t *string, const char *key)
{
  return strcmp(string->data, key) == 0;
}

static int
compare_representation_offsets(const void *data, const void *key)
{
  apr_ssize_t diff = (*(const representation_t **)data)->offset
                   - *(const apr_size_t *)key;

  /* sizeof(int) may be < sizeof(ssize_t) */
  if (diff < 0)
    return -1;
  return diff > 0 ? 1 : 0;
}

static representation_t *
find_representation(int *idx,
                    fs_fs_t *fs,
                    revision_info_t **revision_info,
                    int revision,
                    apr_size_t offset)
{
  revision_info_t *info;
  *idx = -1;
  
  info = revision_info ? *revision_info : NULL;
  if (info == NULL || info->revision != revision)
    {
      info = APR_ARRAY_IDX(fs->revisions,
                           revision - fs->start_revision,
                           revision_info_t*);
      if (revision_info)
        *revision_info = info;
    }

  if (info == NULL)
    return NULL;

  *idx = svn_sort__bsearch_lower_bound(&offset,
                                       info->representations,
                                       compare_representation_offsets);
  if (*idx < info->representations->nelts)
    {
      representation_t *result
        = APR_ARRAY_IDX(info->representations, *idx, representation_t *);
      if (result->offset == offset)
        return result;
    }

  return NULL;
}

static svn_error_t *
read_rep_base(representation_t **representation,
              apr_size_t *header_size,
              svn_boolean_t *is_plain,
              fs_fs_t *fs,
              svn_stringbuf_t *file_content,
              apr_size_t offset,
              apr_pool_t *pool,
              apr_pool_t *scratch_pool)
{
  char *str, *last_str;
  int idx, revision;
  apr_uint64_t temp;

  const char *buffer = file_content->data + offset;
  const char *line_end = strchr(buffer, '\n');
  *header_size = line_end - buffer + 1;

  if (strncmp(buffer, "PLAIN\n", *header_size) == 0)
    {
      *is_plain = TRUE;
      *representation = NULL;
      return SVN_NO_ERROR;
    }

  *is_plain = FALSE;
  if (strncmp(buffer, "DELTA\n", *header_size) == 0)
    {
      /* This is a delta against the empty stream. */
      *representation = fs->null_base;
      return SVN_NO_ERROR;
    }

  str = apr_pstrndup(scratch_pool, buffer, line_end - buffer);
  last_str = str;

  /* We hopefully have a DELTA vs. a non-empty base revision. */
  str = svn_cstring_tokenize(" ", &last_str);
  str = svn_cstring_tokenize(" ", &last_str);
  SVN_ERR(svn_cstring_atoi(&revision, str));

  str = svn_cstring_tokenize(" ", &last_str);
  SVN_ERR(svn_cstring_strtoui64(&temp, str, 0, APR_SIZE_MAX, 10));

  *representation = find_representation(&idx, fs, NULL, revision, (apr_size_t)temp);
  return SVN_NO_ERROR;
}

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
  apr_uint64_t expanded_size;
  int idx;

  char *c = (char *)value->data;
  SVN_ERR(svn_cstring_atoi(&revision, svn_cstring_tokenize(" ", &c)));
  SVN_ERR(svn_cstring_strtoui64(&offset, svn_cstring_tokenize(" ", &c), 0, APR_SIZE_MAX, 10));
  SVN_ERR(svn_cstring_strtoui64(&size, svn_cstring_tokenize(" ", &c), 0, APR_SIZE_MAX, 10));
  SVN_ERR(svn_cstring_strtoui64(&expanded_size, svn_cstring_tokenize(" ", &c), 0, APR_SIZE_MAX, 10));

  result = find_representation(&idx, fs, &revision_info, revision, (apr_size_t)offset);
  if (!result)
    {
      apr_size_t header_size;
      svn_boolean_t is_plain;
      
      result = apr_pcalloc(pool, sizeof(*result));
      result->revision = revision;
      result->expanded_size = (apr_size_t)(expanded_size ? expanded_size : size);
      result->offset = (apr_size_t)offset;
      result->size = (apr_size_t)size;
      SVN_ERR(read_rep_base(&result->delta_base, &header_size,
                            &is_plain, fs, file_content,
                            (apr_size_t)offset,
                            pool, scratch_pool));

      result->header_size = header_size;
      result->is_plain = is_plain;
      svn_sort__array_insert(&result, revision_info->representations, idx);
    }
    
  *representation = result;

  return SVN_NO_ERROR;
}

/* Get the file content of revision REVISION in FS and return it in *DATA.
 * Use SCRATCH_POOL for temporary allocations.
 */
static svn_error_t *
get_rep_content(svn_stringbuf_t **content,
                fs_fs_t *fs,
                representation_t *representation,
                svn_stringbuf_t *file_content,
                apr_pool_t *pool)
{
  apr_off_t offset;
  svn_revnum_t revision = representation->revision;
  revision_info_t *revision_info = APR_ARRAY_IDX(fs->revisions,
                                            revision - fs->start_revision,
                                            revision_info_t*);

  /* not in cache. Is the revision valid at all? */
  if (revision - fs->start_revision > fs->revisions->nelts)
    return svn_error_createf(SVN_ERR_FS_CORRUPT, NULL,
                             _("Unknown revision %ld"), revision);

  if (file_content)
    {
      offset = representation->offset
            +  representation->header_size;
      *content = svn_stringbuf_ncreate(file_content->data + offset,
                                       representation->size, pool);
    }
  else
    {
      offset = revision_info->offset
             + representation->offset
             + representation->header_size;
      SVN_ERR(get_content(content, fs, revision, offset,
                          representation->size, pool));
    }

  return SVN_NO_ERROR;
}


/* Skip forwards to THIS_CHUNK in REP_STATE and then read the next delta
   window into *NWIN. */
static svn_error_t *
read_windows(apr_array_header_t **windows,
             fs_fs_t *fs,
             representation_t *representation,
             svn_stringbuf_t *file_content,
             apr_pool_t *pool)
{
  svn_stringbuf_t *content;
  svn_stream_t *stream;
  char version;
  apr_size_t len = sizeof(version);

  *windows = apr_array_make(pool, 0, sizeof(svn_txdelta_window_t *));

  SVN_ERR(get_rep_content(&content, fs, representation, file_content, pool));

  content->data += 3;
  content->len -= 3;
  stream = svn_stream_from_stringbuf(content, pool);
  SVN_ERR(svn_stream_read(stream, &version, &len));

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

/* Get the undeltified window that is a result of combining all deltas
   from the current desired representation identified in *RB with its
   base representation.  Store the window in *RESULT. */
static svn_error_t *
get_combined_window(svn_stringbuf_t **content,
                    fs_fs_t *fs,
                    representation_t *representation,
                    svn_stringbuf_t *file_content,
                    apr_pool_t *pool)
{
  int i;
  apr_array_header_t *windows;
  svn_stringbuf_t *base_content, *result;
  const char *source;
  apr_pool_t *sub_pool = svn_pool_create(pool);
  apr_pool_t *iter_pool = svn_pool_create(pool);

  if (representation->is_plain)
    return get_rep_content(content, fs, representation, file_content, pool);

  *content = get_cached_window(fs, representation, pool);
  if (*content)
    return SVN_NO_ERROR;
  
  SVN_ERR(read_windows(&windows, fs, representation, file_content, sub_pool));
  if (representation->delta_base && representation->delta_base->revision)
    SVN_ERR(get_combined_window(&base_content, fs,
                                representation->delta_base, NULL, sub_pool));
  else
    base_content = svn_stringbuf_create_empty(sub_pool);

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
  
  set_cached_window(fs, representation, result);
  *content = result;
  return SVN_NO_ERROR;
}

static svn_error_t *
read_noderev(fs_fs_t *fs,
             svn_stringbuf_t *file_content,
             apr_size_t offset,
             revision_info_t *revision_info,
             apr_pool_t *pool,
             apr_pool_t *scratch_pool);

static svn_error_t *
parse_dir(fs_fs_t *fs,
          svn_stringbuf_t *file_content,
          representation_t *representation,
          revision_info_t *revision_info,
          apr_pool_t *pool,
          apr_pool_t *scratch_pool)
{
  svn_stringbuf_t *text;
  apr_pool_t *iter_pool;
  apr_pool_t *text_pool;
  const char *current;
  const char *revision_key;
  apr_size_t key_len;

  if (representation == NULL)
    return SVN_NO_ERROR;

  iter_pool = svn_pool_create(scratch_pool);
  text_pool = svn_pool_create(scratch_pool);

  SVN_ERR(get_combined_window(&text, fs, representation, file_content,
                              text_pool));
  current = text->data;

  revision_key = apr_psprintf(text_pool, "r%d/", representation->revision);
  key_len = strlen(revision_key);
  
  /* Translate the string dir entries into real entries. */
  while (*current != 'E')
    {
      char *next;

      current = strchr(current, '\n');
      if (current)
        current = strchr(current+1, '\n');
      if (current)
        current = strchr(current+1, '\n');
      next = current ? strchr(++current, '\n') : NULL;
      if (next == NULL)
        return svn_error_createf(SVN_ERR_FS_CORRUPT, NULL,
           _("Corrupt directory representation in rev %d at offset %ld"),
                                 representation->revision,
                                 representation->offset);
      
      *next = 0;
      current = strstr(current, revision_key);
      if (current)
        {
          apr_uint64_t offset;

          SVN_ERR(svn_cstring_strtoui64(&offset, current + key_len, 0,
                                        APR_SIZE_MAX, 10));
          SVN_ERR(read_noderev(fs, file_content, (apr_size_t)offset,
                               revision_info, pool, iter_pool));

          svn_pool_clear(iter_pool);
        }
      current = next+1;
    }

  svn_pool_destroy(iter_pool);
  svn_pool_destroy(text_pool);
  return SVN_NO_ERROR;
}

static svn_error_t *
read_noderev(fs_fs_t *fs,
             svn_stringbuf_t *file_content,
             apr_size_t offset,
             revision_info_t *revision_info,
             apr_pool_t *pool,
             apr_pool_t *scratch_pool)
{
  svn_string_t *line;
  representation_t *text = NULL;
  representation_t *props = NULL;
  apr_size_t start_offset = offset;
  svn_boolean_t is_dir = FALSE;

  scratch_pool = svn_pool_create(scratch_pool);
  
  while (1)
    {
      svn_string_t key;
      svn_string_t value;
      char *sep;
      const char *start = file_content->data + offset;
      const char *end = strchr(start, '\n');

      line = svn_string_ncreate(start, end - start, scratch_pool);
      offset += end - start + 1;
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

      if (key_matches(&key, "type"))
        is_dir = strcmp(value.data, "dir") == 0;
      else if (key_matches(&key, "text"))
        {
          SVN_ERR(parse_representation(&text, fs, file_content,
                                       &value, revision_info,
                                       pool, scratch_pool));
          if (++text->ref_count == 1)
            text->kind = is_dir ? dir_rep : file_rep;
        }
      else if (key_matches(&key, "props"))
        {
          SVN_ERR(parse_representation(&props, fs, file_content,
                                       &value, revision_info,
                                       pool, scratch_pool));
          if (++props->ref_count == 1)
            props->kind = is_dir ? dir_property_rep : file_property_rep;
        }
    }

  if (is_dir && text && text->ref_count == 1)
    SVN_ERR(parse_dir(fs, file_content, text, revision_info,
                      pool, scratch_pool));

  if (is_dir)
    {
      revision_info->dir_noderev_size += offset - start_offset;
      revision_info->dir_noderev_count++;
    }
  else
    {
      revision_info->file_noderev_size += offset - start_offset;
      revision_info->file_noderev_count++;
    }
  svn_pool_destroy(scratch_pool);

  return SVN_NO_ERROR;
}

static apr_size_t
get_change_count(const char *changes,
                 apr_size_t len)
{
  apr_size_t lines = 0;
  const char *end = changes + len;

  for (; changes < end; ++changes)
    if (*changes == '\n')
      ++lines;

  return lines / 2;
}

static void print_progress(svn_revnum_t revision)
{
  printf("%8ld", revision);
  fflush(stdout);
}

static svn_error_t *
read_pack_file(fs_fs_t *fs,
               svn_revnum_t base,
               apr_pool_t *pool)
{
  apr_array_header_t *manifest = NULL;
  apr_pool_t *local_pool = svn_pool_create(pool);
  apr_pool_t *iter_pool = svn_pool_create(local_pool);
  int i;
  apr_off_t file_size = 0;
  const char *pack_folder = get_pack_folder(fs, base, local_pool);

  SVN_ERR(read_manifest(&manifest, fs, pack_folder, local_pool));
  if (manifest->nelts != fs->max_files_per_dir)
    return svn_error_create(SVN_ERR_FS_CORRUPT, NULL, NULL);

  SVN_ERR(rev_or_pack_file_size(&file_size, fs, base, pool));

  for (i = 0; i < manifest->nelts; ++i)
    {
      apr_size_t root_node_offset;
      svn_stringbuf_t *rev_content;
  
      revision_info_t *info = apr_pcalloc(pool, sizeof(*info));
      info->representations = apr_array_make(iter_pool, 4, sizeof(representation_t*));

      info->revision = base + i;
      info->offset = APR_ARRAY_IDX(manifest, i, apr_size_t);
      info->end = i+1 < manifest->nelts
                         ? APR_ARRAY_IDX(manifest, i+1 , apr_size_t)
                         : file_size;

      SVN_ERR(get_content(&rev_content, fs, info->revision,
                          info->offset,
                          info->end - info->offset,
                          iter_pool));
      
      SVN_ERR(read_revision_header(&info->changes,
                                   &info->changes_len,
                                   &root_node_offset,
                                   rev_content,
                                   iter_pool));
      
      info->change_count
        = get_change_count(rev_content->data + info->changes,
                           info->changes_len);
      SVN_ERR(read_noderev(fs, rev_content,
                           root_node_offset, info, pool, iter_pool));

      info->representations = apr_array_copy(pool, info->representations);
      APR_ARRAY_PUSH(fs->revisions, revision_info_t*) = info;
      
      svn_pool_clear(iter_pool);
    }

  print_progress(base);
  apr_pool_destroy(local_pool);

  return SVN_NO_ERROR;
}

static svn_error_t *
read_revision_file(fs_fs_t *fs,
                   svn_revnum_t revision,
                   apr_pool_t *pool)
{
  apr_size_t root_node_offset;
  apr_pool_t *local_pool = svn_pool_create(pool);
  svn_stringbuf_t *rev_content;
  revision_info_t *info = apr_pcalloc(pool, sizeof(*info));
  apr_off_t file_size = 0;

  SVN_ERR(rev_or_pack_file_size(&file_size, fs, revision, pool));

  info->representations = apr_array_make(pool, 4, sizeof(representation_t*));

  info->revision = revision;
  info->offset = 0;
  info->end = file_size;

  SVN_ERR(get_content(&rev_content, fs, revision, 0, file_size, local_pool));

  SVN_ERR(read_revision_header(&info->changes,
                               &info->changes_len,
                               &root_node_offset,
                               rev_content,
                               local_pool));

  APR_ARRAY_PUSH(fs->revisions, revision_info_t*) = info;

  info->change_count
    = get_change_count(rev_content->data + info->changes,
                       info->changes_len);

  SVN_ERR(read_noderev(fs, rev_content,
                       root_node_offset, info,
                       pool, local_pool));

  if (revision % fs->max_files_per_dir == 0)
    print_progress(revision);

  apr_pool_destroy(local_pool);

  return SVN_NO_ERROR;
}

static svn_error_t *
read_revisions(fs_fs_t **fs,
               const char *path,
               svn_revnum_t start_revision,
               apr_size_t memsize,
               apr_pool_t *pool)
{
  svn_revnum_t revision;
  apr_size_t window_cache_size;

  /* determine cache sizes */

  if (memsize < 100)
    memsize = 100;
  
  window_cache_size = memsize * 1024 * 1024;
  
  SVN_ERR(fs_open(fs, path, pool));

  (*fs)->start_revision = start_revision
                        - (start_revision % (*fs)->max_files_per_dir);
  (*fs)->revisions = apr_array_make(pool,
                                    (*fs)->max_revision + 1 - (*fs)->start_revision,
                                    sizeof(revision_info_t *));
  (*fs)->null_base = apr_pcalloc(pool, sizeof(*(*fs)->null_base));
  (*fs)->window_cache = create_window_cache
                    (apr_allocator_owner_get
                         (svn_pool_create_allocator(FALSE)),
                          10000, window_cache_size);

  for ( revision = start_revision
      ; revision < (*fs)->min_unpacked_rev
      ; revision += (*fs)->max_files_per_dir)
    SVN_ERR(read_pack_file(*fs, revision, pool));
    
  for ( ; revision <= (*fs)->max_revision; ++revision)
    SVN_ERR(read_revision_file(*fs, revision, pool));

  return SVN_NO_ERROR;
}

typedef struct rep_pack_stats_t
{
  apr_int64_t count;
  apr_int64_t packed_size;
  apr_int64_t expanded_size;
  apr_int64_t overhead_size;
} rep_pack_stats_t;

typedef struct representation_stats_t
{
  rep_pack_stats_t total;
  rep_pack_stats_t uniques;
  rep_pack_stats_t shared;
  
  apr_int64_t references;
  apr_int64_t expanded_size;
} representation_stats_t;

typedef struct node_stats_t
{
  apr_int64_t count;
  apr_int64_t size;
} node_stats_t;

static void
add_rep_pack_stats(rep_pack_stats_t *stats,
                   representation_t *rep)
{
  stats->count++;
  
  stats->packed_size += rep->size;
  stats->expanded_size += rep->expanded_size;
  stats->overhead_size += rep->header_size + 7;
}

static void
add_rep_stats(representation_stats_t *stats,
              representation_t *rep)
{
  add_rep_pack_stats(&stats->total, rep);
  if (rep->ref_count == 1)
    add_rep_pack_stats(&stats->uniques, rep);
  else
    add_rep_pack_stats(&stats->shared, rep);

  stats->references += rep->ref_count;
  stats->expanded_size += rep->ref_count * rep->expanded_size;
}

static void
print_rep_stats(representation_stats_t *stats,
                apr_pool_t *pool)
{
  printf(_("%20s bytes in %12s reps\n"
           "%20s bytes in %12s shared reps\n"
           "%20s bytes expanded size\n"
           "%20s bytes expanded shared size\n"
           "%20s bytes with rep-sharing off\n"
           "%20s shared references\n"),
         svn__i64toa_sep(stats->total.packed_size, ',', pool),
         svn__i64toa_sep(stats->total.count, ',', pool),
         svn__i64toa_sep(stats->shared.packed_size, ',', pool),
         svn__i64toa_sep(stats->shared.count, ',', pool),
         svn__i64toa_sep(stats->total.expanded_size, ',', pool),
         svn__i64toa_sep(stats->shared.expanded_size, ',', pool),
         svn__i64toa_sep(stats->expanded_size, ',', pool),
         svn__i64toa_sep(stats->references - stats->total.count, ',', pool));
}

static void
print_stats(fs_fs_t *fs,
            apr_pool_t *pool)
{
  int i, k;
  
  representation_stats_t file_rep_stats = { { 0 } };
  representation_stats_t dir_rep_stats = { { 0 } };
  representation_stats_t file_prop_rep_stats = { { 0 } };
  representation_stats_t dir_prop_rep_stats = { { 0 } };
  representation_stats_t total_rep_stats = { { 0 } };

  node_stats_t dir_node_stats = { 0 };
  node_stats_t file_node_stats = { 0 };
  node_stats_t total_node_stats = { 0 };

  apr_int64_t total_size = 0;
  apr_int64_t change_count = 0;
  apr_int64_t change_len = 0;
  
  for (i = 0; i < fs->revisions->nelts; ++i)
    {
      revision_info_t *revision = APR_ARRAY_IDX(fs->revisions, i,
                                                revision_info_t *);
      change_count += revision->change_count;
      change_len += revision->changes_len;
      total_size += revision->end - revision->offset;

      dir_node_stats.count += revision->dir_noderev_count;
      dir_node_stats.size += revision->dir_noderev_size;
      file_node_stats.count += revision->file_noderev_count;
      file_node_stats.size += revision->file_noderev_size;
      total_node_stats.count += revision->dir_noderev_count
                              + revision->file_noderev_count;
      total_node_stats.size += revision->dir_noderev_size
                             + revision->file_noderev_size;
      
      for (k = 0; k < revision->representations->nelts; ++k)
        {
          representation_t *rep = APR_ARRAY_IDX(revision->representations,
                                                k, representation_t *);
          switch(rep->kind)
            {
              case file_rep:
                add_rep_stats(&file_rep_stats, rep);
                break;
              case dir_rep:
                add_rep_stats(&dir_rep_stats, rep);
                break;
              case file_property_rep:
                add_rep_stats(&file_prop_rep_stats, rep);
                break;
              case dir_property_rep:
                add_rep_stats(&dir_prop_rep_stats, rep);
                break;
              default:
                break;
            }

          add_rep_stats(&total_rep_stats, rep);
        }
    }

  printf("\nGlobal statistics:\n");
  printf(_("%20s bytes in %12s revisions\n"
           "%20s bytes in %12s changes\n"
           "%20s bytes in %12s node revision records\n"
           "%20s bytes in %12s representations\n"
           "%20s bytes expanded representation size\n"
           "%20s bytes with rep-sharing off\n"),
         svn__i64toa_sep(total_size, ',', pool),
         svn__i64toa_sep(fs->revisions->nelts, ',', pool),
         svn__i64toa_sep(change_len, ',', pool),
         svn__i64toa_sep(change_count, ',', pool),
         svn__i64toa_sep(total_node_stats.size, ',', pool),
         svn__i64toa_sep(total_node_stats.count, ',', pool),
         svn__i64toa_sep(total_rep_stats.total.packed_size, ',', pool),
         svn__i64toa_sep(total_rep_stats.total.count, ',', pool),
         svn__i64toa_sep(total_rep_stats.total.expanded_size, ',', pool),
         svn__i64toa_sep(total_rep_stats.expanded_size, ',', pool));

  printf("\nNoderev statistics:\n");
  printf(_("%20s bytes in %12s nodes total\n"
           "%20s bytes in %12s directory noderevs\n"
           "%20s bytes in %12s file noderevs\n"),
         svn__i64toa_sep(total_node_stats.size, ',', pool),
         svn__i64toa_sep(total_node_stats.count, ',', pool),
         svn__i64toa_sep(dir_node_stats.size, ',', pool),
         svn__i64toa_sep(dir_node_stats.count, ',', pool),
         svn__i64toa_sep(file_node_stats.size, ',', pool),
         svn__i64toa_sep(file_node_stats.count, ',', pool));

  printf("\nRepresentation statistics:\n");
  printf(_("%20s bytes in %12s representations total\n"
           "%20s bytes in %12s directory representations\n"
           "%20s bytes in %12s file representations\n"
           "%20s bytes in %12s directory property representations\n"
           "%20s bytes in %12s file property representations\n"
           "%20s bytes in header & footer overhead\n"),
         svn__i64toa_sep(total_rep_stats.total.packed_size, ',', pool),
         svn__i64toa_sep(total_rep_stats.total.count, ',', pool),
         svn__i64toa_sep(dir_rep_stats.total.packed_size, ',', pool),
         svn__i64toa_sep(dir_rep_stats.total.count, ',', pool),
         svn__i64toa_sep(file_rep_stats.total.packed_size, ',', pool),
         svn__i64toa_sep(file_rep_stats.total.count, ',', pool),
         svn__i64toa_sep(dir_prop_rep_stats.total.packed_size, ',', pool),
         svn__i64toa_sep(dir_prop_rep_stats.total.count, ',', pool),
         svn__i64toa_sep(file_prop_rep_stats.total.packed_size, ',', pool),
         svn__i64toa_sep(file_prop_rep_stats.total.count, ',', pool),
         svn__i64toa_sep(total_rep_stats.total.overhead_size, ',', pool));

  printf("\nDirectory representation statistics:\n");
  print_rep_stats(&dir_rep_stats, pool);
  printf("\nFile representation statistics:\n");
  print_rep_stats(&file_rep_stats, pool);
  printf("\nDirectory property representation statistics:\n");
  print_rep_stats(&dir_prop_rep_stats, pool);
  printf("\nFile property representation statistics:\n");
  print_rep_stats(&file_prop_rep_stats, pool);
}

static void
print_usage(svn_stream_t *ostream, const char *progname,
            apr_pool_t *pool)
{
  svn_error_clear(svn_stream_printf(ostream, pool,
     "\n"
     "Usage: %s <repo> <cachesize>\n"
     "\n"
     "Read the repository at local path <repo> starting at revision 0,\n"
     "count statistical information and write that data to stdout.\n"
     "Use up to <cachesize> MB of memory for caching. This does not include\n"
     "temporary representation of the repository structure, i.e. the actual\n"
     "memory may be considerably higher.\n",
     progname));
}

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

  printf("Reading revisions\n");
  svn_err = read_revisions(&fs, repo_path, start_revision, memsize, pool);
  printf("\n");

  print_stats(fs, pool);
  
  if (svn_err)
    {
      svn_handle_error2(svn_err, stdout, FALSE, ERROR_TAG);
      return 2;
    }

  return 0;
}
