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

#ifndef _
#define _(x) x
#endif

#define ERROR_TAG "diff: "

typedef struct noderev_t noderev_t;
typedef struct revision_info_t revision_info_t;

enum fragment_kind_t
{
  header_fragment,
  changes_fragment,
  noderep_fragment,
  property_fragment,
  dir_fragment,
  file_fragment
};

typedef struct fragment_t
{
  apr_off_t position;
  void *data;
  enum fragment_kind_t kind;
} fragment_t;

typedef struct revision_location_t
{
  apr_off_t offset;
  apr_off_t changes;
  apr_off_t changes_len;
  apr_off_t end;  
} revision_location_t;

typedef struct location_t
{
  apr_off_t offset;
  apr_off_t size;
} location_t;

typedef struct direntry_t
{
  const char *name;
  apr_size_t name_len;
  noderev_t *node;
} direntry_t;

typedef struct directory_t
{
  apr_array_header_t *entries;
  unsigned char target_md5[16];
  apr_size_t size;
} directory_t;

typedef struct representation_t
{
  location_t original;
  location_t target;
  apr_off_t header_size;
  struct representation_t *delta_base;
  revision_info_t *revision;
  directory_t *dir;
  svn_boolean_t is_plain;
  svn_boolean_t covered;
} representation_t;

struct noderev_t
{
  location_t original;
  location_t target;
  noderev_t *predecessor;
  representation_t *text;
  representation_t *props;
  revision_info_t *revision;
  svn_boolean_t covered;
};

struct revision_info_t
{
  svn_revnum_t revision;
  revision_location_t original;
  revision_location_t target;
  noderev_t *root_noderev;
  apr_array_header_t *node_revs;
  apr_array_header_t *representations;
};

typedef struct revision_pack_t
{
  svn_revnum_t base;
  apr_array_header_t *info;
  apr_array_header_t *fragments;
  apr_size_t filesize;
  apr_size_t target_offset;
} revision_pack_t;

typedef struct content_cache_t
{
  apr_pool_t *pool;
  apr_pool_t *hash_pool;

  apr_hash_t *hash;

  char *data;
  apr_size_t limit;

  apr_size_t total_size;
  apr_size_t insert_count;
} content_cache_t;

typedef struct dir_cache_entry_t
{
  svn_revnum_t revision;
  apr_off_t offset;
  
  apr_hash_t *hash;
} dir_cache_entry_t;

typedef struct dir_cache_t
{
  dir_cache_entry_t *entries;

  apr_pool_t *pool1;
  apr_pool_t *pool2;
  apr_size_t entry_count;
  apr_size_t insert_count;
} dir_cache_t;

typedef struct window_cache_entry_t
{
  svn_revnum_t revision;
  apr_off_t offset;

  svn_stringbuf_t *window;
} window_cache_entry_t;

typedef struct window_cache_t
{
  window_cache_entry_t *entries;

  apr_pool_t *pool;
  apr_size_t entry_count;
  apr_size_t capacity;
  apr_size_t used;
} window_cache_t;

typedef struct fs_fs_t
{
  const char *path;
  svn_revnum_t start_revision;
  int format;

  svn_revnum_t max_revision;
  svn_revnum_t min_unpacked_rev;
  int max_files_per_dir;

  apr_array_header_t *revisions;
  apr_array_header_t *packs;

  representation_t *null_base;
  content_cache_t *cache;
  dir_cache_t *dir_cache;
  window_cache_t *window_cache;
} fs_fs_t;

static const char *
get_pack_folder(fs_fs_t *fs,
                svn_revnum_t rev,
                apr_pool_t *pool)
{
  return apr_psprintf(pool, "%s/db/revs/%ld.pack",
                      fs->path, rev / fs->max_files_per_dir);
}

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

static content_cache_t *
create_content_cache(apr_pool_t *pool,
                     apr_size_t limit)
{
  content_cache_t *result = apr_pcalloc(pool, sizeof(*result));

  result->pool = pool;
  result->hash_pool = svn_pool_create(pool);
  result->hash = svn_hash__make(result->hash_pool);
  result->limit = limit;
  result->total_size = 0;
  result->insert_count = 0;
  result->data = apr_palloc(pool, limit);
  
  return result;
}

static svn_string_t *
get_cached_content(content_cache_t *cache,
                   svn_revnum_t revision)
{
  return apr_hash_get(cache->hash, &revision, sizeof(revision));
}

static void
set_cached_content(content_cache_t *cache,
                   svn_revnum_t revision,
                   svn_string_t *data)
{
  svn_string_t *content;
  svn_revnum_t *key;
  
  assert(get_cached_content(cache, revision) == NULL);

  if (cache->total_size + data->len > cache->limit)
    {
      if (cache->insert_count > 10000)
        {
          svn_pool_clear(cache->hash_pool);
          cache->hash = svn_hash__make(cache->hash_pool);
          cache->insert_count = 0;
        }
      else
        cache->hash = svn_hash__make(cache->hash_pool);

      cache->total_size = 0;
    }

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

static svn_error_t *
get_content(svn_string_t **data,
            fs_fs_t *fs,
            svn_revnum_t revision,
            apr_pool_t *scratch_pool)
{
  apr_file_t *file;
  revision_info_t *revision_info;
  svn_stringbuf_t *temp;
  
  svn_string_t *result = get_cached_content(fs->cache, revision);
  if (result)
    {
      *data = result;
      return SVN_NO_ERROR;
    }

  if (revision - fs->start_revision > fs->revisions->nelts)
    return svn_error_createf(SVN_ERR_BAD_VERSION_FILE_FORMAT, NULL,
                             _("Unknown revision %ld"), revision);
  revision_info = APR_ARRAY_IDX(fs->revisions,
                                revision - fs->start_revision,
                                revision_info_t*);

  temp = svn_stringbuf_create_ensure(  revision_info->original.end
                                     - revision_info->original.offset,
                                     scratch_pool);
  temp->len = revision_info->original.end - revision_info->original.offset;
  SVN_ERR(open_rev_or_pack_file(&file, fs, revision, scratch_pool));
  SVN_ERR(svn_io_file_seek(file, APR_SET, &revision_info->original.offset,
                           scratch_pool));
  SVN_ERR(svn_io_file_read(file, temp->data, &temp->len, scratch_pool));

  set_cached_content(fs->cache, revision,
                     svn_stringbuf__morph_into_string(temp));
  *data = get_cached_content(fs->cache, revision);

  return SVN_NO_ERROR;
}

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

static apr_size_t
get_dir_cache_index(fs_fs_t *fs,
                    svn_revnum_t revision,
                    apr_off_t offset)
{
  return (revision + offset * 0xd1f3da69) % fs->dir_cache->entry_count;
}

static apr_pool_t *
get_cached_dir_pool(fs_fs_t *fs)
{
  return fs->dir_cache->pool1;
}

static apr_hash_t *
get_cached_dir(fs_fs_t *fs,
               representation_t *representation)
{
  svn_revnum_t revision = representation->revision->revision;
  apr_off_t offset = representation->original.offset;

  apr_size_t i = get_dir_cache_index(fs, revision, offset);
  dir_cache_entry_t *entry = &fs->dir_cache->entries[i];
  
  return entry->offset == offset && entry->revision == revision
    ? entry->hash
    : NULL;
}

static void
set_cached_dir(fs_fs_t *fs,
               representation_t *representation,
               apr_hash_t *hash)
{
  svn_revnum_t revision = representation->revision->revision;
  apr_off_t offset = representation->original.offset;

  apr_size_t i = get_dir_cache_index(fs, revision, offset);
  dir_cache_entry_t *entry = &fs->dir_cache->entries[i];

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

  entry->hash = hash;
  entry->offset = offset;
  entry->revision = revision;
}

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

static apr_size_t
get_window_cache_index(fs_fs_t *fs,
                       svn_revnum_t revision,
                       apr_off_t offset)
{
  return (revision + offset * 0xd1f3da69) % fs->window_cache->entry_count;
}

static svn_stringbuf_t *
get_cached_window(fs_fs_t *fs,
                  representation_t *representation,
                  apr_pool_t *pool)
{
  svn_revnum_t revision = representation->revision->revision;
  apr_off_t offset = representation->original.offset;

  apr_size_t i = get_window_cache_index(fs, revision, offset);
  window_cache_entry_t *entry = &fs->window_cache->entries[i];

  return entry->offset == offset && entry->revision == revision
    ? svn_stringbuf_dup(entry->window, pool)
    : NULL;
}

static void
set_cached_window(fs_fs_t *fs,
                  representation_t *representation,
                  svn_stringbuf_t *window)
{
  svn_revnum_t revision = representation->revision->revision;
  apr_off_t offset = representation->original.offset;

  apr_size_t i = get_window_cache_index(fs, revision, offset);
  window_cache_entry_t *entry = &fs->window_cache->entries[i];

  fs->window_cache->used += window->len;
  if (fs->window_cache->used >= fs->window_cache->capacity)
    {
      svn_pool_clear(fs->window_cache->pool);
      memset(fs->window_cache->entries,
             0,
             sizeof(*fs->window_cache->entries) * fs->window_cache->entry_count);
      fs->window_cache->used = window->len;
    }

  entry->window = svn_stringbuf_dup(window, fs->window_cache->pool);
  entry->offset = offset;
  entry->revision = revision;
}

/* Given REV in FS, set *REV_OFFSET to REV's offset in the packed file.
   Use POOL for temporary allocations. */
static svn_error_t *
read_manifest(apr_array_header_t **manifest,
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
  *manifest = apr_array_make(pool, 1000, sizeof(apr_off_t));
  while (1)
    {
      svn_stringbuf_t *sb;
      svn_boolean_t eof;
      apr_int64_t val;
      svn_error_t *err;

      svn_pool_clear(iterpool);
      SVN_ERR(svn_stream_readline(manifest_stream, &sb, "\n", &eof, iterpool));
      if (eof)
        break;

      err = svn_cstring_atoi64(&val, sb->data);
      if (err)
        return svn_error_createf(SVN_ERR_FS_CORRUPT, err,
                                 _("Manifest offset '%s' too large"),
                                 sb->data);
      APR_ARRAY_PUSH(*manifest, apr_off_t) = (apr_off_t)val;
    }
  svn_pool_destroy(iterpool);

  return svn_stream_close(manifest_stream);
}

static svn_error_t *
read_revision_header(apr_off_t *changes,
                     apr_off_t *changes_len,
                     apr_off_t *root_noderev,
                     svn_stringbuf_t *file_content,
                     apr_off_t start,
                     apr_off_t end,
                     apr_pool_t *pool)
{
  char buf[64];
  const char *line;
  const char *space;
  apr_int64_t val;
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

  *(char *)space = 0;
  
  SVN_ERR(svn_cstring_atoi64(&val, line+1));
  *root_noderev = (apr_off_t)val;
  SVN_ERR(svn_cstring_atoi64(&val, space+1));
  *changes = (apr_off_t)val;
  *changes_len = end - *changes - start - (buf + len - line) + 1;

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
compare_noderev_offsets(const void *data, const void *key)
{
  return (*(const noderev_t **)data)->original.offset
       - *(const apr_off_t *)key;
}

static svn_error_t *
parse_revnode_pos(revision_info_t **revision_info,
                  apr_off_t *offset,
                  fs_fs_t *fs,
                  svn_string_t *id)
{
  int revision;

  const char *revision_pos = strrchr(id->data, 'r');
  char *offset_pos = (char *)strchr(id->data, '/');

  if (revision_pos == NULL || offset_pos == NULL)
    return svn_error_createf(SVN_ERR_BAD_VERSION_FILE_FORMAT, NULL,
                             _("Invalid node id '%s'"), id->data);

  *offset_pos = 0;
  SVN_ERR(svn_cstring_atoi(&revision, revision_pos + 1));
  SVN_ERR(svn_cstring_atoi64(offset, offset_pos + 1));
  *offset_pos = '/';

  if (revision - fs->start_revision > fs->revisions->nelts)
    return svn_error_createf(SVN_ERR_BAD_VERSION_FILE_FORMAT, NULL,
                             _("Unknown revision %d"), revision);

  *revision_info = APR_ARRAY_IDX(fs->revisions,
                                 revision - fs->start_revision,
                                 revision_info_t*);

  return SVN_NO_ERROR;
}

static svn_error_t *
find_noderev(noderev_t **result,
            revision_info_t *revision_info,
            apr_off_t offset)
{
  int idx = svn_sort__bsearch_lower_bound(&offset,
                                          revision_info->node_revs,
                                          compare_noderev_offsets);
  if ((idx < 0) || (idx >= revision_info->node_revs->nelts))
    return svn_error_createf(SVN_ERR_BAD_VERSION_FILE_FORMAT, NULL,
                             _("No noderev found at offset %" APR_OFF_T_FMT),
                             offset);

  *result = APR_ARRAY_IDX(revision_info->node_revs, idx, noderev_t *);
  if ((*result)->original.offset != offset)
    return svn_error_createf(SVN_ERR_BAD_VERSION_FILE_FORMAT, NULL,
                             _("No noderev found at offset %" APR_OFF_T_FMT),
                             offset);

  return SVN_NO_ERROR;
}

static svn_error_t *
parse_pred(noderev_t **result,
           fs_fs_t *fs,
           svn_string_t *id)
{
  apr_off_t offset;
  revision_info_t *revision_info;

  SVN_ERR(parse_revnode_pos(&revision_info, &offset, fs, id));
  SVN_ERR(find_noderev(result, revision_info, offset));

  return SVN_NO_ERROR;
}

static int
compare_representation_offsets(const void *data, const void *key)
{
  return (*(const representation_t **)data)->original.offset
       - *(const apr_off_t *)key;
}

static representation_t *
find_representation(int *idx,
                    fs_fs_t *fs,
                    revision_info_t **revision_info,
                    int revision,
                    apr_off_t offset)
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
      if (result->original.offset == offset)
        return result;
    }

  return NULL;
}

static svn_error_t *
read_rep_base(representation_t **representation,
              apr_off_t *header_size,
              svn_boolean_t *is_plain,
              fs_fs_t *fs,
              svn_stringbuf_t *file_content,
              apr_off_t offset,
              apr_pool_t *pool,
              apr_pool_t *scratch_pool)
{
  char *str, *last_str;
  int idx, revision;

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
  SVN_ERR(svn_cstring_atoi64(&offset, str));

  *representation = find_representation(&idx, fs, NULL, revision, offset);
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

  apr_off_t offset;
  svn_filesize_t size;
  int idx;

  char *c = (char *)value->data;
  SVN_ERR(svn_cstring_atoi(&revision, svn_cstring_tokenize(" ", &c)));
  SVN_ERR(svn_cstring_atoi64(&offset, svn_cstring_tokenize(" ", &c)));
  SVN_ERR(svn_cstring_atoi64(&size, svn_cstring_tokenize(" ", &c)));

  result = find_representation(&idx, fs, &revision_info, revision, offset);
  if (!result)
    {
      result = apr_pcalloc(pool, sizeof(*result));
      result->revision = revision_info;
      result->original.offset = offset;
      result->original.size = size;
      SVN_ERR(read_rep_base(&result->delta_base, &result->header_size,
                            &result->is_plain, fs, file_content,
                            offset + revision_info->original.offset,
                            pool, scratch_pool));

      svn_sort__array_insert(&result, revision_info->representations, idx);
    }
    
  *representation = result;

  return SVN_NO_ERROR;
}

/* Skip forwards to THIS_CHUNK in REP_STATE and then read the next delta
   window into *NWIN. */
static svn_error_t *
read_windows(apr_array_header_t **windows,
             fs_fs_t *fs,
             representation_t *representation,
             apr_pool_t *pool)
{
  svn_string_t *content;
  svn_string_t data;
  svn_stream_t *stream;
  apr_off_t offset = representation->original.offset
                   + representation->header_size;
  char version;
  apr_size_t len = sizeof(version);

  *windows = apr_array_make(pool, 0, sizeof(svn_txdelta_window_t *));

  SVN_ERR(get_content(&content, fs, representation->revision->revision, pool));

  data.data = content->data + offset + 3;
  data.len = representation->original.size - 3;
  stream = svn_stream_from_string(&data, pool);
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

static svn_error_t *
read_plain(svn_stringbuf_t **content,
           fs_fs_t *fs,
           representation_t *representation,
           apr_pool_t *pool)
{
  svn_string_t *data;
  apr_off_t offset = representation->original.offset
                   + representation->header_size;

  SVN_ERR(get_content(&data, fs, representation->revision->revision, pool));

  *content = svn_stringbuf_ncreate(data->data + offset,
                                   representation->original.size,
                                   pool);

  return SVN_NO_ERROR;
}

/* Get the undeltified window that is a result of combining all deltas
   from the current desired representation identified in *RB with its
   base representation.  Store the window in *RESULT. */
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
  apr_pool_t *sub_pool = svn_pool_create(pool);
  apr_pool_t *iter_pool = svn_pool_create(pool);

  if (representation->is_plain)
    return read_plain(content, fs, representation, pool);

  *content = get_cached_window(fs, representation, pool);
  if (*content)
    return SVN_NO_ERROR;
  
  SVN_ERR(read_windows(&windows, fs, representation, sub_pool));
  if (representation->delta_base && representation->delta_base->revision)
    SVN_ERR(get_combined_window(&base_content, fs,
                                representation->delta_base, sub_pool));
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
read_noderev(noderev_t **noderev,
             fs_fs_t *fs,
             svn_stringbuf_t *file_content,
             apr_off_t offset,
             revision_info_t *revision_info,
             apr_pool_t *pool,
             apr_pool_t *scratch_pool);

static svn_error_t *
get_noderev(noderev_t **noderev,
            fs_fs_t *fs,
            svn_stringbuf_t *file_content,
            apr_off_t offset,
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

  *hash = get_cached_dir(fs, representation);
  if (*hash)
    return SVN_NO_ERROR;

  pool = get_cached_dir_pool(fs);
  *hash = svn_hash__make(pool);
  if (representation != NULL)
    {
      text_pool = svn_pool_create(scratch_pool);
      SVN_ERR(get_combined_window(&text, fs, representation, text_pool));
      stream = svn_stream_from_stringbuf(text, text_pool);
      SVN_ERR(svn_hash_read2(*hash, stream, SVN_HASH_TERMINATOR, pool));
      svn_pool_destroy(text_pool);
    }

  set_cached_dir(fs, representation, *hash);
  
  return SVN_NO_ERROR;
}

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

  if (representation == NULL)
    return SVN_NO_ERROR;

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

  SVN_ERR(read_dir(&hash, fs, representation, scratch_pool));

  representation->dir = apr_pcalloc(pool, sizeof(*representation->dir));
  representation->dir->entries
    = apr_array_make(pool, apr_hash_count(hash), sizeof(direntry_t *));

  /* Translate the string dir entries into real entries. */
  for (hi = apr_hash_first(pool, hash); hi; hi = apr_hash_next(hi))
    {
      const char *name = svn__apr_hash_index_key(hi);
      svn_string_t *str_val = svn__apr_hash_index_val(hi);
      apr_off_t offset;
      revision_info_t *revision_info;
      apr_size_t name_len = strlen(name);
      direntry_t *entry = base_dir
                        ? apr_hash_get(base_dir, name, name_len)
                        : NULL;

      SVN_ERR(parse_revnode_pos(&revision_info, &offset, fs, str_val));

      if (   !entry
          || !entry->node->text
          || entry->node->text->revision != revision_info
          || entry->node->original.offset != offset)
        {
          direntry_t *new_entry = apr_pcalloc(pool, sizeof(*entry));
          new_entry->name_len = name_len;
          if (entry)
            new_entry->name = entry->name;
          else
            new_entry->name = apr_pstrdup(pool, name);

          entry = new_entry;
          SVN_ERR(get_noderev(&entry->node, fs, file_content, offset,
                              revision_info, pool, iter_pool));
        }

      APR_ARRAY_PUSH(representation->dir->entries, direntry_t *) = entry;
      svn_pool_clear(iter_pool);
    }

  svn_pool_destroy(iter_pool);
  return SVN_NO_ERROR;
}

static svn_error_t *
read_noderev(noderev_t **noderev,
             fs_fs_t *fs,
             svn_stringbuf_t *file_content,
             apr_off_t offset,
             revision_info_t *revision_info,
             apr_pool_t *pool,
             apr_pool_t *scratch_pool)
{
  noderev_t *result = apr_pcalloc(pool, sizeof(*result));
  svn_string_t *line;
  svn_boolean_t is_dir = FALSE;

  scratch_pool = svn_pool_create(scratch_pool);
  
  result->original.offset = offset;
  while (1)
    {
      svn_string_t key;
      svn_string_t value;
      char *sep;
      const char *start = file_content->data + offset
                        + revision_info->original.offset;
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

  result->revision = revision_info;
  result->original.size = offset - result->original.offset;

  svn_sort__array_insert(&result,
                         revision_info->node_revs,
                         svn_sort__bsearch_lower_bound(&offset,
                                                       revision_info->node_revs,
                                                       compare_noderev_offsets));

  if (is_dir)
    SVN_ERR(parse_dir(fs, file_content, result->text,
                      pool, scratch_pool));

  svn_pool_destroy(scratch_pool);
  *noderev = result;

  return SVN_NO_ERROR;
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
  svn_stringbuf_t *file_content;
  revision_pack_t *revisions;
  const char *pack_folder = get_pack_folder(fs, base, local_pool);
  SVN_ERR(read_rev_or_pack_file(&file_content, fs, base, local_pool));

  revisions = apr_pcalloc(pool, sizeof(*revisions));
  revisions->base = base;
  revisions->fragments = NULL;
  revisions->info = apr_array_make(pool,
                                   fs->max_files_per_dir,
                                   sizeof(revision_info_t*));
  revisions->filesize = file_content->len;
  APR_ARRAY_PUSH(fs->packs, revision_pack_t*) = revisions;

  SVN_ERR(read_manifest(&manifest, pack_folder, local_pool));
  if (manifest->nelts != fs->max_files_per_dir)
    return svn_error_create(SVN_ERR_FS_CORRUPT, NULL, NULL);

  for (i = 0; i < manifest->nelts; ++i)
    {
      apr_off_t root_node_offset;
      svn_string_t rev_content;
  
      revision_info_t *info = apr_pcalloc(pool, sizeof(*info));
      info->node_revs = apr_array_make(iter_pool, 4, sizeof(noderev_t*));
      info->representations = apr_array_make(iter_pool, 4, sizeof(representation_t*));

      info->revision = base + i;
      info->original.offset = APR_ARRAY_IDX(manifest, i, apr_off_t);
      info->original.end = i+1 < manifest->nelts
                         ? APR_ARRAY_IDX(manifest, i+1 , apr_off_t)
                         : file_content->len;
      SVN_ERR(read_revision_header(&info->original.changes,
                                   &info->original.changes_len,
                                   &root_node_offset,
                                   file_content,
                                   APR_ARRAY_IDX(manifest, i , apr_off_t),
                                   info->original.end,
                                   iter_pool));

      APR_ARRAY_PUSH(revisions->info, revision_info_t*) = info;
      APR_ARRAY_PUSH(fs->revisions, revision_info_t*) = info;
      
      rev_content.data = file_content->data + info->original.offset;
      rev_content.len = info->original.end - info->original.offset;
      set_cached_content(fs->cache, info->revision, &rev_content);

      SVN_ERR(read_noderev(&info->root_noderev, fs, file_content,
                           root_node_offset, info, pool, iter_pool));

      info->node_revs = apr_array_copy(pool, info->node_revs);
      info->representations = apr_array_copy(pool, info->representations);
      
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
  apr_off_t root_node_offset;
  apr_pool_t *local_pool = svn_pool_create(pool);
  svn_stringbuf_t *file_content;
  svn_string_t rev_content;
  revision_pack_t *revisions = apr_pcalloc(pool, sizeof(*revisions));
  revision_info_t *info = apr_pcalloc(pool, sizeof(*info));

  SVN_ERR(read_rev_or_pack_file(&file_content, fs, revision, local_pool));

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

  APR_ARRAY_PUSH(fs->revisions, revision_info_t*) = info;

  revisions->base = revision;
  revisions->fragments = NULL;
  revisions->info = apr_array_make(pool, 1, sizeof(revision_info_t*));
  revisions->filesize = file_content->len;
  APR_ARRAY_PUSH(revisions->info, revision_info_t*) = info;
  APR_ARRAY_PUSH(fs->packs, revision_pack_t*) = revisions;

  rev_content.data = file_content->data + info->original.offset;
  rev_content.len = info->original.end - info->original.offset;
  set_cached_content(fs->cache, info->revision, &rev_content);

  SVN_ERR(read_noderev(&info->root_noderev, fs, file_content,
                       root_node_offset, info,
                       pool, local_pool));
  APR_ARRAY_PUSH(info->node_revs, noderev_t*) = info->root_noderev;

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
  apr_size_t content_cache_size;
  apr_size_t window_cache_size;
  apr_size_t dir_cache_size;

  /* determine cache sizes */

  if (memsize < 100)
    memsize = 100;
  
  content_cache_size = memsize * 7 / 10 > 4000 ? 4000 : memsize * 7 / 10;
  window_cache_size = memsize * 2 / 10 * 1024 * 1024;
  dir_cache_size = (memsize / 10) * 16000;
  
  SVN_ERR(fs_open(fs, path, pool));

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

  for ( revision = start_revision
      ; revision < (*fs)->min_unpacked_rev
      ; revision += (*fs)->max_files_per_dir)
    SVN_ERR(read_pack_file(*fs, revision, pool));
    
  for ( ; revision <= (*fs)->max_revision; ++revision)
    SVN_ERR(read_revision_file(*fs, revision, pool));

  return SVN_NO_ERROR;
}

static apr_size_t
get_max_offset_len(const revision_pack_t *pack)
{
  apr_size_t max_future_size = pack->filesize * 2 + 10000;
  apr_size_t result = 0;

  while (max_future_size > 0)
    {
      ++result;
      max_future_size /= 10;
    }

  return result;
}

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

static svn_error_t *
get_target_offset(apr_size_t **current_pos,
                  apr_array_header_t **fragments,
                  fs_fs_t *fs,
                  revision_info_t *info)
{
  int i;
  revision_pack_t *pack;
  svn_revnum_t revision = info->revision;

  if (fs->min_unpacked_rev > revision)
    {
      i = (revision - fs->start_revision) / fs->max_files_per_dir;
    }
  else
    {
      i = (fs->min_unpacked_rev - fs->start_revision) / fs->max_files_per_dir;
      i += revision - fs->min_unpacked_rev;
    }

  pack = APR_ARRAY_IDX(fs->packs, i, revision_pack_t*);
  *current_pos = &pack->target_offset;
  *fragments = pack->fragments;

  return SVN_NO_ERROR;
}

static svn_error_t *
add_noderev_recursively(fs_fs_t *fs,
                        noderev_t *node,
                        apr_pool_t *pool);

static svn_error_t *
add_representation_recursively(fs_fs_t *fs,
                               representation_t *representation,
                               enum fragment_kind_t kind,
                               apr_pool_t *pool)
{
  apr_size_t *current_pos;
  apr_array_header_t *fragments;
  fragment_t fragment;
  
  if (   representation == NULL
      || representation->covered
      || (representation->dir && kind != dir_fragment)
      || representation == fs->null_base)
    return SVN_NO_ERROR;

  SVN_ERR(get_target_offset(&current_pos, &fragments,
                            fs, representation->revision));
  representation->target.offset = *current_pos;
  representation->covered = TRUE;
  
  fragment.data = representation;
  fragment.kind = kind;
  fragment.position = *current_pos;
  APR_ARRAY_PUSH(fragments, fragment_t) = fragment;

  if (   kind != dir_fragment
      && representation->delta_base && representation->delta_base->dir)
    {
      apr_pool_t *text_pool = svn_pool_create(pool);
      svn_stringbuf_t *content;

      get_combined_window(&content, fs, representation, text_pool);
      representation->target.size = content->len;
      *current_pos += representation->target.size + 13;

      svn_pool_destroy(text_pool);
    }
  else
    if (   kind == dir_fragment
        || (representation->delta_base && representation->delta_base->dir))
      {
        if (representation->original.size < 50)
          *current_pos += 300;
        else
          *current_pos += representation->original.size * 3 + 150;
      }
    else
      {
        representation->target.size = representation->original.size;

        if (representation->delta_base &&
            (representation->delta_base != fs->null_base))
          *current_pos += representation->original.size + 50;
        else
          *current_pos += representation->original.size + 13;
      }

  if (representation->delta_base)
    SVN_ERR(add_representation_recursively(fs,
                                           representation->delta_base,
                                           kind,
                                           pool));

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

static svn_error_t *
add_noderev_recursively(fs_fs_t *fs,
                        noderev_t *node,
                        apr_pool_t *pool)
{
  apr_size_t *current_pos;
  apr_array_header_t *fragments;
  fragment_t fragment;

  if (node->covered)
    return SVN_NO_ERROR;

  SVN_ERR(get_target_offset(&current_pos, &fragments, fs, node->revision));
  node->covered = TRUE;
  node->target.offset = *current_pos;

  fragment.data = node;
  fragment.kind = noderep_fragment;
  fragment.position = *current_pos;
  APR_ARRAY_PUSH(fragments, fragment_t) = fragment;

  *current_pos += node->original.size + 40;
  
  if (node->text && node->text->dir)
    SVN_ERR(add_representation_recursively(fs, node->text, dir_fragment, pool));
  else
    SVN_ERR(add_representation_recursively(fs, node->text, file_fragment, pool));

  SVN_ERR(add_representation_recursively(fs, node->props, property_fragment, pool));

  return SVN_NO_ERROR;
}

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

  for (i = 0; i < pack->info->nelts; ++i)
    {
      info = APR_ARRAY_IDX(pack->info, i, revision_info_t*);
      info->target.end = pack->target_offset;
    }

  return SVN_NO_ERROR;
}

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

      if (info->revision % 1000 == 0)
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

static svn_error_t *
get_fragment_content(svn_string_t **content,
                     fs_fs_t *fs,
                     fragment_t *fragment,
                     apr_pool_t *pool);

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

          SVN_ERR(get_fragment_content(&content, fs, fragment, itempool));
          svn_pool_clear(itempool);
        }
    }

  svn_pool_destroy(itempool);

  return SVN_NO_ERROR;
}

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
        case noderep_fragment:
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

static void
move_fragment(fragment_t *fragment,
              apr_size_t new_position)
{
  revision_info_t *info;
  representation_t *representation;
  noderev_t *node;
  
  fragment->position = new_position; 

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

      case noderep_fragment:
        node = fragment->data;
        node->target.offset = new_position;
        break;
    }
}

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

  SVN_ERR(update_noderevs(fs, pack, pool));

  current_pos = pack->info->nelts > 1 ? 64 : 0;
  for (i = 0; i + 1 < pack->fragments->nelts; ++i)
    {
      fragment = &APR_ARRAY_IDX(pack->fragments, i, fragment_t);
      SVN_ERR(get_content_length(&len, fs, fragment, TRUE, itempool));
      move_fragment(fragment, current_pos);
      current_pos += len;

      svn_pool_clear(itempool);
    }

  fragment = &APR_ARRAY_IDX(pack->fragments, pack->fragments->nelts-1, fragment_t);
  fragment->position = current_pos;

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

  const char *dir = apr_psprintf(iterpool, "%s/new/%ld%s",
                                  fs->path, pack->base / 1000,
                                  pack->info->nelts > 1 ? ".pack" : "");
  SVN_ERR(svn_io_make_dir_recursively(dir, pool));
  SVN_ERR(svn_io_file_open(&file,
                            pack->info->nelts > 1
                              ? apr_psprintf(iterpool, "%s/pack", dir)
                              : apr_psprintf(iterpool, "%s/%ld", dir, pack->base),
                            APR_WRITE | APR_CREATE | APR_BUFFERED,
                            APR_OS_DEFAULT,
                            iterpool));

  for (i = 0; i < pack->fragments->nelts; ++i)
    {
      apr_size_t padding;
      fragment = &APR_ARRAY_IDX(pack->fragments, i, fragment_t);
      SVN_ERR(get_fragment_content(&content, fs, fragment, itempool));

      SVN_ERR_ASSERT(fragment->position >= current_pos);
      if (   fragment->kind == header_fragment
          && i+1 < pack->fragments->nelts)
        padding = APR_ARRAY_IDX(pack->fragments, i+1, fragment_t).position -
                  content->len - current_pos;
      else
        padding = fragment->position - current_pos;

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

      SVN_ERR(svn_io_file_write_full(file,
                                      content->data,
                                      content->len,
                                      NULL,
                                      itempool));
      current_pos += content->len;

      svn_pool_clear(itempool);
    }

  apr_file_close(file);

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
          revision_info_t *info = APR_ARRAY_IDX(pack->info, i, revision_info_t*);
          svn_stream_printf(stream, itempool, "%ld\n", info->target.offset);
          svn_pool_clear(itempool);
        }
    }

  svn_pool_destroy(itempool);
  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

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
  
  SVN_ERR(read_dir(&hash, fs, representation, scratch_pool));
  hash = apr_hash_copy(hash_pool, hash);
  for (i = 0; i < dir->nelts; ++i)
    {
      char buffer[256];
      svn_string_t *new_val;
      apr_size_t pos;
      direntry_t *entry = APR_ARRAY_IDX(dir, i, direntry_t *);
      svn_string_t *str_val = apr_hash_get(hash, entry->name, entry->name_len);
      if (str_val == NULL)
        return svn_error_createf(SVN_ERR_FS_CORRUPT, NULL,
                                 _("Dir entry '%s' not found"), entry->name);

      SVN_ERR_ASSERT(str_val->len < sizeof(buffer));
      
      memcpy(buffer, str_val->data, str_val->len+1);
      pos = strchr(buffer, '/') - buffer + 1;
      pos += svn__ui64toa(buffer + pos, entry->node->target.offset - entry->node->revision->target.offset);
      new_val = svn_string_ncreate(buffer, pos, hash_pool);

      apr_hash_set(hash, entry->name, entry->name_len, new_val);
    }

  result = svn_stringbuf_create_ensure(representation->target.size, pool);
  stream = svn_stream_from_stringbuf(result, hash_pool);
  SVN_ERR(svn_hash_write2(hash, stream, SVN_HASH_TERMINATOR, hash_pool));
  svn_pool_destroy(hash_pool);

  *content = svn_stringbuf__morph_into_string(result);
  
  return SVN_NO_ERROR;
}

struct diff_write_baton_t
{
  svn_stream_t *stream;
  apr_size_t size;
};

static svn_error_t *
diff_write_handler(void *baton,
                   const char *data,
                   apr_size_t *len)
{
  struct diff_write_baton_t *whb = baton;

  SVN_ERR(svn_stream_write(whb->stream, data, len));
  whb->size += *len;

  return SVN_NO_ERROR;
}

static svn_error_t *
diff_stringbufs(svn_stringbuf_t *diff,
                apr_size_t *inflated_size,
                svn_string_t *base,
                svn_string_t *content,
                apr_pool_t *pool)
{
  svn_txdelta_window_handler_t diff_wh;
  void *diff_whb;
  struct diff_write_baton_t whb;

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

  whb.stream = svn_txdelta_target_push(diff_wh, diff_whb, source, pool);
  whb.size = 0;

  stream = svn_stream_create(&whb, pool);
  svn_stream_set_write(stream, diff_write_handler);

  SVN_ERR(svn_stream_write(stream, content->data, &content->len));
  SVN_ERR(svn_stream_close(whb.stream));
  SVN_ERR(svn_stream_close(stream));

  *inflated_size = whb.size;
  return SVN_NO_ERROR;
}

static void
update_id(svn_stringbuf_t *node_rev,
          const char *key,
          noderev_t *node)
{
  char *newline_pos = 0;
  char *pos;

  pos = strstr(node_rev->data, key);
  if (pos)
    pos = strchr(pos, '/');
  if (pos)
    newline_pos = strchr(++pos, '\n');

  if (pos && newline_pos)
    {
      char temp[SVN_INT64_BUFFER_SIZE];
      apr_size_t len = svn__i64toa(temp, node->target.offset - node->revision->target.offset);
      svn_stringbuf_replace(node_rev,
                            pos - node_rev->data, newline_pos - pos,
                            temp, len);
    }
}

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
      char *newline_pos = strchr(val_pos, '\n');
      svn_checksum_t checksum = {representation->dir->target_md5,
                                 svn_checksum_md5};
      const char* temp = apr_psprintf(scratch_pool, "%ld %ld %ld %ld %s",
                                      representation->revision->revision,
                                      representation->target.offset - representation->revision->target.offset,
                                      representation->target.size,
                                      representation->dir->size,
                                      svn_checksum_to_cstring(&checksum,
                                                              scratch_pool));

      svn_stringbuf_replace(node_rev,
                            val_pos - node_rev->data, newline_pos - val_pos,
                            temp, strlen(temp));
    }
  else
    {
      const char* temp;
      char *end_pos = strchr(val_pos, ' ');
      
      val_pos = end_pos + 1;
      end_pos = strchr(strchr(val_pos, ' ') + 1, ' ');
      temp = apr_psprintf(scratch_pool, "%ld %ld",
                          representation->target.offset - representation->revision->target.offset,
                          representation->target.size);

      svn_stringbuf_replace(node_rev,
                            val_pos - node_rev->data, end_pos - val_pos,
                            temp, strlen(temp));
    }
}

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
      case header_fragment:
        info = fragment->data;
        *content = svn_string_createf(pool,
                                      "\n%ld %ld\n",
                                      info->root_noderev->target.offset - info->target.offset,
                                      info->target.changes);
        return SVN_NO_ERROR;

      case changes_fragment:
        info = fragment->data;
        SVN_ERR(get_content(&revision_content, fs, info->revision, pool));
        
        *content = svn_string_create_empty(pool);
        (*content)->data = revision_content->data + info->original.changes;
        (*content)->len = info->target.changes_len;
        return SVN_NO_ERROR;

      case property_fragment:
      case file_fragment:
        representation = fragment->data;
        SVN_ERR(get_content(&revision_content, fs,
                            representation->revision->revision, pool));

        if (representation->delta_base)
          if (representation->delta_base->dir)
            {
              SVN_ERR(get_combined_window(&text, fs, representation, pool));
              representation->target.size = text->len;

              svn_stringbuf_insert(text, 0, "PLAIN\n", 6);
              svn_stringbuf_appendcstr(text, "ENDREP\n");
              *content = svn_stringbuf__morph_into_string(text);

              return SVN_NO_ERROR;
            }
          else
            if (representation->delta_base == fs->null_base)
              header = svn_stringbuf_create("DELTA\n", pool);
            else
              header = svn_stringbuf_createf(pool,
                                             "DELTA %ld %ld %ld\n",
                                             representation->delta_base->revision->revision,
                                             representation->delta_base->target.offset
                                             - representation->delta_base->revision->target.offset,
                                             representation->delta_base->target.size);
        else
          header = svn_stringbuf_create("PLAIN\n", pool);

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

      case dir_fragment:
        representation = fragment->data;
        SVN_ERR(get_updated_dir(&revision_content, fs, representation,
                                pool, pool));
        SVN_ERR(svn_checksum(&checksum, svn_checksum_md5,
                             revision_content->data, revision_content->len,
                             pool));
        memcpy(representation->dir->target_md5,
               checksum->digest,
               sizeof(representation->dir->target_md5));

        if (representation->delta_base)
          {
            if (representation->delta_base->dir == NULL)
              {
                header = svn_stringbuf_create("DELTA\n", pool);
                base_content = svn_string_create_empty(pool);
              }
            else
              {
                representation_t *base_rep = representation->delta_base;
                header = svn_stringbuf_createf(pool,
                                               "DELTA %ld %ld %ld\n",
                                               base_rep->revision->revision,
                                               base_rep->target.offset - base_rep->revision->target.offset,
                                               base_rep->target.size);
                SVN_ERR(get_updated_dir(&base_content, fs, base_rep,
                                        pool, pool));
              }

            header_size = header->len;
            SVN_ERR(diff_stringbufs(header, &representation->dir->size,
                                    base_content,
                                    revision_content, pool));
            representation->target.size = header->len - header_size;
            svn_stringbuf_appendcstr(header, "ENDREP\n");
            *content = svn_stringbuf__morph_into_string(header);
          }
        else
          {
            representation->target.size = revision_content->len;
            representation->dir->size = revision_content->len;
            *content = svn_string_createf(pool, "PLAIN\n%sENDREP\n",
                                          revision_content->data);
          }

        return SVN_NO_ERROR;

      case noderep_fragment:
        node = fragment->data;
        SVN_ERR(get_content(&revision_content, fs,
                            node->revision->revision, pool));
        node_rev = svn_stringbuf_ncreate(revision_content->data +
                                         node->original.offset,
                                         node->original.size,
                                         pool);

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

static svn_error_t *
prepare_repo(const char *path, apr_pool_t *pool)
{
  svn_node_kind_t kind;
  
  const char *old_path = svn_dirent_join(path, "db/old", pool);
  const char *new_path = svn_dirent_join(path, "new", pool);
  const char *revs_path = svn_dirent_join(path, "db/revs", pool);
  
  SVN_ERR(svn_io_check_path(old_path, &kind, pool));
  if (kind == svn_node_dir)
    {
      SVN_ERR(svn_io_remove_dir2(new_path, TRUE, NULL, NULL, pool));
      SVN_ERR(svn_io_file_move(revs_path, new_path, pool));
      SVN_ERR(svn_io_file_move(old_path, revs_path, pool));
      SVN_ERR(svn_io_remove_dir2(new_path, TRUE, NULL, NULL, pool));
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
activate_new_revs(const char *path, apr_pool_t *pool)
{
  svn_node_kind_t kind;

  const char *old_path = svn_dirent_join(path, "db/old", pool);
  const char *new_path = svn_dirent_join(path, "new", pool);
  const char *revs_path = svn_dirent_join(path, "db/revs", pool);

  SVN_ERR(svn_io_check_path(old_path, &kind, pool));
  if (kind == svn_node_none)
    {
      SVN_ERR(svn_io_file_move(revs_path, old_path, pool));
      SVN_ERR(svn_io_file_move(new_path, revs_path, pool));
    }

  return SVN_NO_ERROR;
}

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

int main(int argc, const char *argv[])
{
  apr_pool_t *pool;
  svn_stream_t *ostream;
  svn_error_t *svn_err;
  const char *repo_path = NULL;
  svn_revnum_t start_revision = 0;
  apr_int64_t memsize = 0;
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

  svn_err = svn_cstring_atoi64(&memsize, argv[2]);
  if (svn_err)
    {
      print_usage(ostream, argv[0], pool);
      svn_error_clear(svn_err);
      return 2;
    }

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
