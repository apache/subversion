/*
 * shelve.c:  implementation of the 'shelve' commands
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

/* ==================================================================== */

/* We define this here to remove any further warnings about the usage of
   experimental functions in this file. */
#define SVN_EXPERIMENTAL

#include "svn_client.h"
#include "svn_wc.h"
#include "svn_pools.h"
#include "svn_dirent_uri.h"
#include "svn_path.h"
#include "svn_hash.h"
#include "svn_utf.h"
#include "svn_ctype.h"
#include "svn_props.h"

#include "client.h"
#include "private/svn_client_private.h"
#include "private/svn_wc_private.h"
#include "private/svn_sorts_private.h"
#include "svn_private_config.h"


static svn_error_t *
shelf_name_encode(char **encoded_name_p,
                  const char *name,
                  apr_pool_t *result_pool)
{
  char *encoded_name
    = apr_palloc(result_pool, strlen(name) * 2 + 1);
  char *out_pos = encoded_name;

  if (name[0] == '\0')
    return svn_error_create(SVN_ERR_BAD_CHANGELIST_NAME, NULL,
                            _("Shelf name cannot be the empty string"));

  while (*name)
    {
      apr_snprintf(out_pos, 3, "%02x", (unsigned char)(*name++));
      out_pos += 2;
    }
  *encoded_name_p = encoded_name;
  return SVN_NO_ERROR;
}

static svn_error_t *
shelf_name_decode(char **decoded_name_p,
                  const char *codename,
                  apr_pool_t *result_pool)
{
  svn_stringbuf_t *sb
    = svn_stringbuf_create_ensure(strlen(codename) / 2, result_pool);
  const char *input = codename;

  while (*input)
    {
      int c;
      int nchars;
      int nitems = sscanf(input, "%02x%n", &c, &nchars);

      if (nitems != 1 || nchars != 2)
        return svn_error_createf(SVN_ERR_BAD_CHANGELIST_NAME, NULL,
                                 _("Shelve: Bad encoded name '%s'"), codename);
      svn_stringbuf_appendbyte(sb, c);
      input += 2;
    }
  *decoded_name_p = sb->data;
  return SVN_NO_ERROR;
}

/* Set *NAME to the shelf name from FILENAME, if FILENAME names a '.current'
 * file, else to NULL. */
static svn_error_t *
shelf_name_from_filename(char **name,
                         const char *filename,
                         apr_pool_t *result_pool)
{
  size_t len = strlen(filename);
  static const char suffix[] = ".current";
  int suffix_len = sizeof(suffix) - 1;

  if (len > suffix_len && strcmp(filename + len - suffix_len, suffix) == 0)
    {
      char *codename = apr_pstrndup(result_pool, filename, len - suffix_len);
      SVN_ERR(shelf_name_decode(name, codename, result_pool));
    }
  else
    {
      *name = NULL;
    }
  return SVN_NO_ERROR;
}

/* Set *PATCH_ABSPATH to the abspath of the file storage dir for SHELF
 * version VERSION, no matter whether it exists.
 */
static svn_error_t *
shelf_version_files_dir_abspath(const char **abspath,
                                svn_client_shelf_t *shelf,
                                int version,
                                apr_pool_t *result_pool,
                                apr_pool_t *scratch_pool)
{
  char *codename;
  char *filename;

  SVN_ERR(shelf_name_encode(&codename, shelf->name, result_pool));
  filename = apr_psprintf(scratch_pool, "%s-%03d.d", codename, version);
  *abspath = svn_dirent_join(shelf->shelves_dir, filename, result_pool);
  return SVN_NO_ERROR;
}

/* Set *PATCH_ABSPATH to the abspath of the patch file for SHELF
 * version VERSION, no matter whether it exists.
 */
static svn_error_t *
get_patch_abspath(const char **abspath,
                  svn_client_shelf_t *shelf,
                  int version,
                  apr_pool_t *result_pool,
                  apr_pool_t *scratch_pool)
{
  char *codename;
  char *filename;

  SVN_ERR(shelf_name_encode(&codename, shelf->name, result_pool));
  filename = apr_psprintf(scratch_pool, "%s-%03d.patch", codename, version);
  *abspath = svn_dirent_join(shelf->shelves_dir, filename, result_pool);
  return SVN_NO_ERROR;
}

/* Set *PATCH_ABSPATH to the abspath of the patch file for SHELF
 * version VERSION. Error if VERSION is invalid or nonexistent.
 */
static svn_error_t *
get_existing_patch_abspath(const char **abspath,
                           svn_client_shelf_t *shelf,
                           int version,
                           apr_pool_t *result_pool,
                           apr_pool_t *scratch_pool)
{
  if (shelf->max_version <= 0)
    return svn_error_createf(SVN_ERR_CLIENT_BAD_REVISION, NULL,
                             _("shelf '%s': no versions available"),
                             shelf->name);
  if (version <= 0 || version > shelf->max_version)
    return svn_error_createf(SVN_ERR_CLIENT_BAD_REVISION, NULL,
                             _("shelf '%s' has no version %d: max version is %d"),
                             shelf->name, version, shelf->max_version);

  SVN_ERR(get_patch_abspath(abspath, shelf, version,
                            result_pool, scratch_pool));
  return SVN_NO_ERROR;
}

/* Delete the storage for SHELF:VERSION. */
static svn_error_t *
shelf_version_delete(svn_client_shelf_t *shelf,
                     int version,
                     apr_pool_t *scratch_pool)
{
  const char *patch_abspath;
  const char *files_dir_abspath;

  SVN_ERR(get_existing_patch_abspath(&patch_abspath, shelf, version,
                                     scratch_pool, scratch_pool));
  SVN_ERR(svn_io_remove_file2(patch_abspath, TRUE /*ignore_enoent*/,
                              scratch_pool));
  SVN_ERR(shelf_version_files_dir_abspath(&files_dir_abspath,
                                          shelf, version,
                                          scratch_pool, scratch_pool));
  SVN_ERR(svn_io_remove_dir2(files_dir_abspath, TRUE /*ignore_enoent*/,
                             NULL, NULL, /*cancel*/
                             scratch_pool));
  return SVN_NO_ERROR;
}

/*  */
static svn_error_t *
get_log_abspath(char **log_abspath,
                svn_client_shelf_t *shelf,
                apr_pool_t *result_pool,
                apr_pool_t *scratch_pool)
{
  char *codename;
  const char *filename;

  SVN_ERR(shelf_name_encode(&codename, shelf->name, result_pool));
  filename = apr_pstrcat(scratch_pool, codename, ".log", SVN_VA_NULL);
  *log_abspath = svn_dirent_join(shelf->shelves_dir, filename, result_pool);
  return SVN_NO_ERROR;
}

/* Set SHELF->revprops by reading from its storage (the '.log' file).
 * Set SHELF->revprops to empty if the storage file does not exist; this
 * is not an error.
 */
static svn_error_t *
shelf_read_revprops(svn_client_shelf_t *shelf,
                    apr_pool_t *scratch_pool)
{
  char *log_abspath;
  svn_error_t *err;
  svn_stream_t *stream;

  SVN_ERR(get_log_abspath(&log_abspath, shelf, scratch_pool, scratch_pool));

  shelf->revprops = apr_hash_make(shelf->pool);
  err = svn_stream_open_readonly(&stream, log_abspath,
                                 scratch_pool, scratch_pool);
  if (err && APR_STATUS_IS_ENOENT(err->apr_err))
    {
      svn_error_clear(err);
      return SVN_NO_ERROR;
    }
  else
    SVN_ERR(err);
  SVN_ERR(svn_hash_read2(shelf->revprops, stream, "PROPS-END", shelf->pool));
  SVN_ERR(svn_stream_close(stream));
  return SVN_NO_ERROR;
}

/* Write SHELF's revprops to its file storage.
 */
static svn_error_t *
shelf_write_revprops(svn_client_shelf_t *shelf,
                     apr_pool_t *scratch_pool)
{
  char *log_abspath;
  apr_file_t *file;
  svn_stream_t *stream;

  SVN_ERR(get_log_abspath(&log_abspath, shelf, scratch_pool, scratch_pool));

  SVN_ERR(svn_io_file_open(&file, log_abspath,
                           APR_FOPEN_WRITE | APR_FOPEN_CREATE | APR_FOPEN_TRUNCATE,
                           APR_FPROT_OS_DEFAULT, scratch_pool));
  stream = svn_stream_from_aprfile2(file, FALSE /*disown*/, scratch_pool);

  SVN_ERR(svn_hash_write2(shelf->revprops, stream, "PROPS-END", scratch_pool));
  SVN_ERR(svn_stream_close(stream));
  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_shelf_revprop_set(svn_client_shelf_t *shelf,
                             const char *prop_name,
                             const svn_string_t *prop_val,
                             apr_pool_t *scratch_pool)
{
  svn_hash_sets(shelf->revprops, apr_pstrdup(shelf->pool, prop_name),
                svn_string_dup(prop_val, shelf->pool));
  SVN_ERR(shelf_write_revprops(shelf, scratch_pool));
  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_shelf_revprop_set_all(svn_client_shelf_t *shelf,
                                 apr_hash_t *revprop_table,
                                 apr_pool_t *scratch_pool)
{
  if (revprop_table)
    shelf->revprops = svn_prop_hash_dup(revprop_table, shelf->pool);
  else
    shelf->revprops = apr_hash_make(shelf->pool);

  SVN_ERR(shelf_write_revprops(shelf, scratch_pool));
  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_shelf_revprop_get(svn_string_t **prop_val,
                             svn_client_shelf_t *shelf,
                             const char *prop_name,
                             apr_pool_t *result_pool)
{
  *prop_val = svn_hash_gets(shelf->revprops, prop_name);
  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_shelf_revprop_list(apr_hash_t **props,
                              svn_client_shelf_t *shelf,
                              apr_pool_t *result_pool)
{
  *props = shelf->revprops;
  return SVN_NO_ERROR;
}

/*  */
static svn_error_t *
get_current_abspath(char **current_abspath,
                    svn_client_shelf_t *shelf,
                    apr_pool_t *result_pool)
{
  char *codename;
  char *filename;

  SVN_ERR(shelf_name_encode(&codename, shelf->name, result_pool));
  filename = apr_psprintf(result_pool, "%s.current", codename);
  *current_abspath = svn_dirent_join(shelf->shelves_dir, filename, result_pool);
  return SVN_NO_ERROR;
}

/* Read SHELF->max_version from its storage (the '.current' file).
 * Set SHELF->max_version to -1 if that file does not exist.
 */
static svn_error_t *
shelf_read_current(svn_client_shelf_t *shelf,
                   apr_pool_t *scratch_pool)
{
  char *current_abspath;
  FILE *fp;

  SVN_ERR(get_current_abspath(&current_abspath, shelf, scratch_pool));
  fp = fopen(current_abspath, "r");
  if (! fp)
    {
      shelf->max_version = -1;
      return SVN_NO_ERROR;
    }
  fscanf(fp, "%d", &shelf->max_version);
  fclose(fp);
  return SVN_NO_ERROR;
}

/*  */
static svn_error_t *
shelf_write_current(svn_client_shelf_t *shelf,
                    apr_pool_t *scratch_pool)
{
  char *current_abspath;
  FILE *fp;

  SVN_ERR(get_current_abspath(&current_abspath, shelf, scratch_pool));
  fp = fopen(current_abspath, "w");
  fprintf(fp, "%d", shelf->max_version);
  fclose(fp);
  return SVN_NO_ERROR;
}

/* A visitor function type for use with walk_shelved_files(). */
typedef svn_error_t *(*shelved_files_walk_func_t)(void *baton,
                                                  const char *relpath,
                                                  apr_pool_t *scratch_pool);

/* Baton for io_visit_shelved_file(). */
struct shelved_files_walk_baton_t
{
  const char *walk_root_abspath;
  shelved_files_walk_func_t walk_func;
  void *walk_baton;
};

/* Call BATON->walk_func(BATON->walk_baton, relpath, ...) for the shelved
 * 'binary' file stored at ABSPATH.
 * Implements svn_io_walk_func_t. */
static svn_error_t *
shelved_files_walk_visitor(void *baton,
                           const char *abspath,
                           const apr_finfo_t *finfo,
                           apr_pool_t *scratch_pool)
{
  struct shelved_files_walk_baton_t *b = baton;
  const char *relpath;

  relpath = svn_dirent_skip_ancestor(b->walk_root_abspath, abspath);
  if (finfo->filetype == APR_REG)
    {
      SVN_ERR(b->walk_func(b->walk_baton, relpath, scratch_pool));
    }
  return SVN_NO_ERROR;
}

/* Walk all the shelved 'binary' files in SHELF_VERSION, calling
 * WALK_FUNC(WALK_BATON, relpath, ...) for each one.
 */
static svn_error_t *
walk_shelved_files(svn_client_shelf_version_t *shelf_version,
                   shelved_files_walk_func_t walk_func,
                   void *walk_baton,
                   apr_pool_t *scratch_pool)
{
  const char *files_dir_abspath;
  struct shelved_files_walk_baton_t baton;
  svn_error_t *err;

  SVN_ERR(shelf_version_files_dir_abspath(&files_dir_abspath,
                                          shelf_version->shelf,
                                          shelf_version->version_number,
                                          scratch_pool, scratch_pool));
  baton.walk_root_abspath = files_dir_abspath;
  baton.walk_func = walk_func;
  baton.walk_baton = walk_baton;
  err = svn_io_dir_walk2(files_dir_abspath, 0 /*wanted*/,
                         shelved_files_walk_visitor, &baton,
                         scratch_pool);
  if (err && APR_STATUS_IS_ENOENT(err->apr_err))
    svn_error_clear(err);
  else
    SVN_ERR(err);

  return SVN_NO_ERROR;
}

/* A baton for use with walk_callback(). */
typedef struct walk_baton_t {
  const char *wc_root_abspath;
  const char *files_dir_abspath;
  svn_stream_t *outstream;
  svn_stream_t *errstream;
  svn_client_ctx_t *ctx;
  svn_boolean_t any_shelved;  /* were any paths successfully shelved? */
  apr_array_header_t *unshelvable;  /* paths unshelvable */
  apr_pool_t *pool;  /* pool for data in 'unshelvable', etc. */
} walk_baton_t;

/*  */
static svn_error_t *
note_shelved(apr_array_header_t *shelved,
             const char *relpath,
             apr_pool_t *pool)
{
  APR_ARRAY_PUSH(shelved, const char *) = apr_pstrdup(pool, relpath);
  return SVN_NO_ERROR;
}

/* Set *IS_BINARY to true iff the pristine or working version of
 * LOCAL_ABSPATH has a MIME-type that we regard as 'binary'.
 */
static svn_error_t *
is_binary_file(svn_boolean_t *is_binary,
               const char *local_abspath,
               svn_client_ctx_t *ctx,
               apr_pool_t *scratch_pool)
{
  apr_hash_t *props;
  const svn_string_t *value;

  SVN_ERR(svn_wc_get_pristine_props(&props, ctx->wc_ctx,
                                    local_abspath,
                                    scratch_pool, scratch_pool));
  value = props ? svn_hash_gets(props, SVN_PROP_MIME_TYPE)
                : NULL;
  *is_binary = value && svn_mime_type_is_binary(value->data);

  SVN_ERR(svn_wc_prop_get2(&value, ctx->wc_ctx, local_abspath,
                           SVN_PROP_MIME_TYPE,
                           scratch_pool, scratch_pool));
  if (value && svn_mime_type_is_binary(value->data))
    *is_binary = TRUE;

  return SVN_NO_ERROR;
}

/* Copy the WC working file at FROM_WC_ABSPATH to a storage location within
 * the shelf-version storage area at FILES_DIR_ABSPATH.
 */
static svn_error_t *
store_file(const char *from_wc_abspath,
           const char *wc_relpath,
           const char *files_dir_abspath,
           apr_pool_t *scratch_pool)
{
  const char *stored_abspath = svn_dirent_join(files_dir_abspath, wc_relpath,
                                               scratch_pool);

  SVN_ERR(svn_io_make_dir_recursively(svn_dirent_dirname(stored_abspath,
                                                         scratch_pool),
                                      scratch_pool));
  SVN_ERR(svn_io_copy_file(from_wc_abspath, stored_abspath,
                           TRUE /*copy_perms*/, scratch_pool));
  return SVN_NO_ERROR;
}

/* An implementation of svn_wc_status_func4_t. */
static svn_error_t *
walk_callback(void *baton,
              const char *local_abspath,
              const svn_wc_status3_t *status,
              apr_pool_t *scratch_pool)
{
  walk_baton_t *wb = baton;
  svn_opt_revision_t peg_revision = {svn_opt_revision_unspecified, {0}};
  svn_opt_revision_t start_revision = {svn_opt_revision_base, {0}};
  svn_opt_revision_t end_revision = {svn_opt_revision_working, {0}};
  const char *wc_relpath = svn_dirent_skip_ancestor(wb->wc_root_abspath,
                                                    local_abspath);

  switch (status->node_status)
    {
      case svn_wc_status_modified:
      case svn_wc_status_deleted:
      case svn_wc_status_added:
      case svn_wc_status_replaced:
      {
        svn_boolean_t binary = FALSE;
        svn_boolean_t store_whole_file = FALSE;

        if (status->kind == svn_node_file)
          {
            SVN_ERR(is_binary_file(&binary, local_abspath,
                                   wb->ctx, scratch_pool));
            if (status->node_status == svn_wc_status_added
                || (binary && status->node_status != svn_wc_status_deleted))
              {
                store_whole_file = TRUE;
              }
          }
        /* Store 'binary' files (except deletes) as complete files;
           store everything else in the patch. */
        /* ### Need a way to record 'modified' vs. 'replaced' */
        /* ### Need a way to store 'deleted' efficiently */
        if (store_whole_file)
          {
            SVN_ERR(store_file(local_abspath, wc_relpath, wb->files_dir_abspath,
                               scratch_pool));
          }
        SVN_ERR(svn_client_diff_peg7(NULL /*options*/,
                                     local_abspath,
                                     &peg_revision,
                                     &start_revision,
                                     &end_revision,
                                     wb->wc_root_abspath,
                                     svn_depth_empty,
                                     TRUE /*notice_ancestry*/,
                                     FALSE /*no_diff_added*/,
                                     FALSE /*no_diff_deleted*/,
                                     TRUE /*show_copies_as_adds*/,
                                     FALSE /*ignore_content_type*/,
                                     FALSE /*ignore_properties*/,
                                     store_whole_file /*properties_only*/,
                                     binary /*use_git_diff_format*/,
                                     FALSE /*pretty_print_mergeinfo*/,
                                     SVN_APR_LOCALE_CHARSET,
                                     wb->outstream,
                                     wb->errstream,
                                     NULL /*changelists*/,
                                     wb->ctx, scratch_pool));
        wb->any_shelved = TRUE;
        break;
      }

      case svn_wc_status_incomplete:
        if ((status->text_status != svn_wc_status_normal
             && status->text_status != svn_wc_status_none)
            || (status->prop_status != svn_wc_status_normal
                && status->prop_status != svn_wc_status_none))
          {
            /* Incomplete, but local modifications */
            SVN_ERR(note_shelved(wb->unshelvable, wc_relpath, wb->pool));
          }
        break;

      case svn_wc_status_conflicted:
      case svn_wc_status_missing:
      case svn_wc_status_obstructed:
        SVN_ERR(note_shelved(wb->unshelvable, wc_relpath, wb->pool));
        break;

      case svn_wc_status_normal:
      case svn_wc_status_ignored:
      case svn_wc_status_none:
      case svn_wc_status_external:
      case svn_wc_status_unversioned:
      default:
        break;
    }

  return SVN_NO_ERROR;
}

/* A baton for use with changelist_filter_func(). */
struct changelist_filter_baton_t {
  apr_hash_t *changelist_hash;
  svn_wc_status_func4_t status_func;
  void *status_baton;
};

/* Filter out paths that are not in the requested changelist(s).
 * Implements svn_wc_status_func4_t. */
static svn_error_t *
changelist_filter_func(void *baton,
                       const char *local_abspath,
                       const svn_wc_status3_t *status,
                       apr_pool_t *scratch_pool)
{
  struct changelist_filter_baton_t *b = baton;

  if (b->changelist_hash
      && (! status->changelist
          || ! svn_hash_gets(b->changelist_hash, status->changelist)))
    {
      return SVN_NO_ERROR;
    }

  SVN_ERR(b->status_func(b->status_baton, local_abspath, status,
                         scratch_pool));
  return SVN_NO_ERROR;
}

/*
 * Walk the WC tree(s) rooted at PATHS, to depth DEPTH, omitting paths that
 * are not in one of the CHANGELISTS (if not null).
 *
 * Call STATUS_FUNC(STATUS_BATON, ...) for each visited path.
 *
 * PATHS are absolute, or relative to CWD.
 */
static svn_error_t *
wc_walk_status_multi(const apr_array_header_t *paths,
                     svn_depth_t depth,
                     const apr_array_header_t *changelists,
                     svn_wc_status_func4_t status_func,
                     void *status_baton,
                     svn_client_ctx_t *ctx,
                     apr_pool_t *scratch_pool)
{
  struct changelist_filter_baton_t cb = {0};
  int i;

  if (changelists && changelists->nelts)
    SVN_ERR(svn_hash_from_cstring_keys(&cb.changelist_hash,
                                       changelists, scratch_pool));
  cb.status_func = status_func;
  cb.status_baton = status_baton;

  for (i = 0; i < paths->nelts; i++)
    {
      const char *path = APR_ARRAY_IDX(paths, i, const char *);

      if (svn_path_is_url(path))
        return svn_error_createf(SVN_ERR_ILLEGAL_TARGET, NULL,
                                 _("'%s' is not a local path"), path);
      SVN_ERR(svn_dirent_get_absolute(&path, path, scratch_pool));

      SVN_ERR(svn_wc_walk_status(ctx->wc_ctx, path, depth,
                                 FALSE /*get_all*/, FALSE /*no_ignore*/,
                                 FALSE /*ignore_text_mods*/,
                                 NULL /*ignore_patterns*/,
                                 changelist_filter_func, &cb,
                                 ctx->cancel_func, ctx->cancel_baton,
                                 scratch_pool));
    }

  return SVN_NO_ERROR;
}

/** Write local changes to a patch file.
 *
 * @a paths, @a depth, @a changelists: The selection of local paths to diff.
 *
 * @a paths are relative to CWD (or absolute). Paths in patch are relative
 * to WC root (@a wc_root_abspath).
 *
 * ### TODO: Ignore any external diff cmd as configured in config file.
 *     This might also solve the buffering problem.
 */
static svn_error_t *
write_patch(svn_boolean_t *any_shelved,
            apr_array_header_t **unshelvable,
            const char *files_dir_abspath,
            const char *patch_abspath,
            const apr_array_header_t *paths,
            svn_depth_t depth,
            const apr_array_header_t *changelists,
            const char *wc_root_abspath,
            svn_client_ctx_t *ctx,
            apr_pool_t *result_pool,
            apr_pool_t *scratch_pool)
{
  walk_baton_t walk_baton = { 0 };
  apr_int32_t flag;
  apr_file_t *outfile;

  walk_baton.wc_root_abspath = wc_root_abspath;
  walk_baton.files_dir_abspath = files_dir_abspath;
  walk_baton.ctx = ctx;
  walk_baton.any_shelved = FALSE;
  walk_baton.unshelvable = apr_array_make(result_pool, 0, sizeof(char *));
  walk_baton.pool = result_pool;

  /* Get streams for the output and any error output of the diff. */
  /* ### svn_stream_open_writable() doesn't work here: the buffering
         goes wrong so that diff headers appear after their hunks.
         For now, fix by opening the file without APR_BUFFERED. */
  flag = APR_FOPEN_WRITE | APR_FOPEN_CREATE | APR_FOPEN_TRUNCATE;
  SVN_ERR(svn_io_file_open(&outfile, patch_abspath,
                           flag, APR_FPROT_OS_DEFAULT, scratch_pool));
  walk_baton.outstream = svn_stream_from_aprfile2(outfile, FALSE /*disown*/,
                                                  scratch_pool);
  walk_baton.errstream = svn_stream_empty(scratch_pool);

  /* Walk the WC */
  SVN_ERR(wc_walk_status_multi(paths, depth, changelists,
                               walk_callback, &walk_baton,
                               ctx, scratch_pool));

  SVN_ERR(svn_stream_close(walk_baton.outstream));
  SVN_ERR(svn_stream_close(walk_baton.errstream));

  *any_shelved = walk_baton.any_shelved;
  *unshelvable = walk_baton.unshelvable;
  return SVN_NO_ERROR;
}

/* Construct a shelf object representing an empty shelf: no versions,
 * no revprops, no looking to see if such a shelf exists on disk.
 */
static svn_error_t *
shelf_construct(svn_client_shelf_t **shelf_p,
                const char *name,
                const char *local_abspath,
                svn_client_ctx_t *ctx,
                apr_pool_t *result_pool)
{
  svn_client_shelf_t *shelf = apr_palloc(result_pool, sizeof(*shelf));
  char *shelves_dir;

  SVN_ERR(svn_client_get_wc_root(&shelf->wc_root_abspath,
                                 local_abspath, ctx,
                                 result_pool, result_pool));
  SVN_ERR(svn_wc__get_shelves_dir(&shelves_dir,
                                  ctx->wc_ctx, local_abspath,
                                  result_pool, result_pool));
  shelf->shelves_dir = shelves_dir;
  shelf->ctx = ctx;
  shelf->pool = result_pool;

  shelf->name = apr_pstrdup(result_pool, name);
  shelf->revprops = apr_hash_make(result_pool);
  shelf->max_version = 0;

  *shelf_p = shelf;
  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_shelf_open_existing(svn_client_shelf_t **shelf_p,
                               const char *name,
                               const char *local_abspath,
                               svn_client_ctx_t *ctx,
                               apr_pool_t *result_pool)
{
  SVN_ERR(shelf_construct(shelf_p, name,
                          local_abspath, ctx, result_pool));
  SVN_ERR(shelf_read_revprops(*shelf_p, result_pool));
  SVN_ERR(shelf_read_current(*shelf_p, result_pool));
  if ((*shelf_p)->max_version < 0)
    {
      return svn_error_createf(SVN_ERR_ILLEGAL_TARGET, NULL,
                               _("Shelf '%s' not found"),
                               name);
    }
  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_shelf_open_or_create(svn_client_shelf_t **shelf_p,
                                const char *name,
                                const char *local_abspath,
                                svn_client_ctx_t *ctx,
                                apr_pool_t *result_pool)
{
  svn_client_shelf_t *shelf;

  SVN_ERR(shelf_construct(&shelf, name,
                          local_abspath, ctx, result_pool));
  SVN_ERR(shelf_read_revprops(shelf, result_pool));
  SVN_ERR(shelf_read_current(shelf, result_pool));
  if (shelf->max_version < 0)
    {
      shelf->max_version = 0;
      SVN_ERR(shelf_write_current(shelf, result_pool));
    }
  *shelf_p = shelf;
  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_shelf_close(svn_client_shelf_t *shelf,
                       apr_pool_t *scratch_pool)
{
  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_shelf_delete(const char *name,
                        const char *local_abspath,
                        svn_boolean_t dry_run,
                        svn_client_ctx_t *ctx,
                        apr_pool_t *scratch_pool)
{
  svn_client_shelf_t *shelf;
  int i;
  char *abspath;

  SVN_ERR(svn_client_shelf_open_existing(&shelf, name,
                                         local_abspath, ctx, scratch_pool));

  /* Remove the patches. */
  for (i = shelf->max_version; i > 0; i--)
    {
      SVN_ERR(shelf_version_delete(shelf, i, scratch_pool));
    }

  /* Remove the other files */
  SVN_ERR(get_log_abspath(&abspath, shelf, scratch_pool, scratch_pool));
  SVN_ERR(svn_io_remove_file2(abspath, TRUE /*ignore_enoent*/, scratch_pool));
  SVN_ERR(get_current_abspath(&abspath, shelf, scratch_pool));
  SVN_ERR(svn_io_remove_file2(abspath, TRUE /*ignore_enoent*/, scratch_pool));

  SVN_ERR(svn_client_shelf_close(shelf, scratch_pool));
  return SVN_NO_ERROR;
}

/* Baton for paths_changed_visitor(). */
struct paths_changed_walk_baton_t
{
  apr_hash_t *paths_hash;
  apr_array_header_t *paths_array;
  svn_boolean_t as_abspath;
  const char *wc_root_abspath;
  apr_pool_t *pool;
};

/* Add to the list(s) in BATON, the RELPATH of a shelved 'binary' file.
 * Implements shelved_files_walk_func_t. */
static svn_error_t *
paths_changed_visitor(void *baton,
                      const char *relpath,
                      apr_pool_t *scratch_pool)
{
  struct paths_changed_walk_baton_t *b = baton;

  relpath = (b->as_abspath
             ? svn_dirent_join(b->wc_root_abspath, relpath, b->pool)
             : apr_pstrdup(b->pool, relpath));
  if (b->paths_hash)
    svn_hash_sets(b->paths_hash, relpath, relpath);
  return SVN_NO_ERROR;
}

/* Get the paths changed, relative to WC root or as abspaths, as a hash
 * and/or an array (in no particular order).
 */
static svn_error_t *
shelf_paths_changed(apr_hash_t **paths_hash_p,
                    apr_array_header_t **paths_array_p,
                    svn_client_shelf_version_t *shelf_version,
                    svn_boolean_t as_abspath,
                    apr_pool_t *result_pool,
                    apr_pool_t *scratch_pool)
{
  svn_client_shelf_t *shelf = shelf_version->shelf;
  svn_patch_file_t *patch_file;
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);
  apr_hash_t *paths_hash = apr_hash_make(result_pool);

  /* Add the paths stored in the patch file */
  SVN_ERR(svn_diff_open_patch_file(&patch_file, shelf_version->patch_abspath,
                                   result_pool));
  while (1)
    {
      svn_patch_t *patch;
      char *path;

      svn_pool_clear(iterpool);
      SVN_ERR(svn_diff_parse_next_patch(&patch, patch_file,
                                        FALSE /*reverse*/,
                                        FALSE /*ignore_whitespace*/,
                                        iterpool, iterpool));
      if (! patch)
        break;

      path = (as_abspath
              ? svn_dirent_join(shelf->wc_root_abspath,
                                patch->new_filename, result_pool)
              : apr_pstrdup(result_pool, patch->new_filename));
      svn_hash_sets(paths_hash, path, path);
    }
  SVN_ERR(svn_diff_close_patch_file(patch_file, iterpool));

  /* Add the paths of files stored outside the patch file ('binary' files) */
  {
    struct paths_changed_walk_baton_t baton;

    baton.paths_hash = paths_hash;
    baton.as_abspath = as_abspath;
    baton.wc_root_abspath = shelf->wc_root_abspath;
    baton.pool = result_pool;
    SVN_ERR(walk_shelved_files(shelf_version,
                               paths_changed_visitor, &baton,
                               scratch_pool));
  }

  svn_pool_destroy(iterpool);

  if (paths_hash_p)
    *paths_hash_p = paths_hash;
  if (paths_array_p)
    SVN_ERR(svn_hash_keys(paths_array_p, paths_hash, result_pool));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_shelf_paths_changed(apr_hash_t **affected_paths,
                               svn_client_shelf_version_t *shelf_version,
                               apr_pool_t *result_pool,
                               apr_pool_t *scratch_pool)
{
  SVN_ERR(shelf_paths_changed(affected_paths, NULL, shelf_version,
                              FALSE /*as_abspath*/,
                              result_pool, scratch_pool));
  return SVN_NO_ERROR;
}

/* Baton for apply_file_visitor(). */
struct apply_files_baton_t
{
  const char *files_dir_abspath, *wc_root_abspath;
  const char *file_relpath;  /* only process this file, no others */
  svn_boolean_t found;  /* was FILE_RELPATH found? */
  svn_boolean_t test_only;  /* only check whether it would conflict */
  svn_boolean_t conflict;  /* would it conflict? */
  svn_client_ctx_t *ctx;
};

/* Copy the file RELPATH from shelf binary file storage to the WC.
 *
 * If it is not already versioned, schedule the file for addition.
 *
 * Make any missing parent directories.
 *
 * Implements shelved_files_walk_func_t. */
static svn_error_t *
apply_file_visitor(void *baton,
                   const char *relpath,
                   apr_pool_t *scratch_pool)
{
  struct apply_files_baton_t *b = baton;
  const char *stored_abspath = svn_dirent_join(b->files_dir_abspath, relpath,
                                               scratch_pool);
  const char *to_wc_abspath = svn_dirent_join(b->wc_root_abspath, relpath,
                                              scratch_pool);
  const char *to_dir_abspath = svn_dirent_dirname(to_wc_abspath, scratch_pool);

  if (b->file_relpath && strcmp(relpath, b->file_relpath) != 0)
    {
      return SVN_NO_ERROR;
    }
  b->found = TRUE;

  if (b->test_only)
    {
      svn_wc_status3_t *status;

      SVN_ERR(svn_wc_status3(&status, b->ctx->wc_ctx, to_wc_abspath,
                             scratch_pool, scratch_pool));
      switch (status->node_status)
        {
        case svn_wc_status_normal:
        case svn_wc_status_none:
          break;
        default:
          b->conflict = TRUE;
        }

      return SVN_NO_ERROR;
    }

  SVN_ERR(svn_io_make_dir_recursively(to_dir_abspath, scratch_pool));
  SVN_ERR(svn_io_copy_file(stored_abspath, to_wc_abspath,
                           TRUE /*copy_perms*/, scratch_pool));
  /* If it was not already versioned, schedule the file for addition.
     (Do not apply autoprops, because this isn't a user-facing "add" but
     restoring a previously saved state.) */
  SVN_ERR(svn_client_add5(to_wc_abspath, svn_depth_infinity,
                          TRUE /*force: ok if already versioned*/,
                          TRUE /*no_ignore*/,
                          TRUE /*no_autoprops*/,
                          TRUE /*add_parents*/,
                          b->ctx, scratch_pool));
  return SVN_NO_ERROR;
}

/* A filter to only apply the patch to a particular file. */
struct patch_filter_baton_t
{
  /* The single path to be selected for patching */
  const char *path;
};

static svn_error_t *
patch_filter(void *baton,
             svn_boolean_t *filtered,
             const char *canon_path_from_patchfile,
             const char *patch_abspath,
             const char *reject_abspath,
             apr_pool_t *scratch_pool)
{
  struct patch_filter_baton_t *fb = baton;

  *filtered = (strcmp(canon_path_from_patchfile, fb->path) != 0);
  return SVN_NO_ERROR;
}

/* Intercept patch notifications to detect when there is a conflict */
struct patch_notify_baton_t
{
  svn_boolean_t conflict;
};

/* Intercept patch notifications to detect when there is a conflict */
static void
patch_notify(void *baton,
             const svn_wc_notify_t *notify,
             apr_pool_t *pool)
{
  struct patch_notify_baton_t *nb = baton;

  if (notify->action == svn_wc_notify_patch_rejected_hunk
      || notify->action == svn_wc_notify_skip)
    nb->conflict = TRUE;
}

svn_error_t *
svn_client_shelf_test_apply_file(svn_boolean_t *conflict_p,
                                 svn_client_shelf_version_t *shelf_version,
                                 const char *file_relpath,
                                 apr_pool_t *scratch_pool)
{
  svn_client_ctx_t *ctx = shelf_version->shelf->ctx;
  svn_wc_notify_func2_t ctx_notify_func;
  void *ctx_notify_baton;
  struct patch_filter_baton_t fb;
  struct patch_notify_baton_t nb;

  /* Apply the whole files stored outside the patch file ('binary' files) */
  {
    const char *files_dir_abspath;
    struct apply_files_baton_t baton = {0};

    SVN_ERR(shelf_version_files_dir_abspath(&files_dir_abspath,
                                            shelf_version->shelf,
                                            shelf_version->version_number,
                                            scratch_pool, scratch_pool));
    baton.files_dir_abspath = files_dir_abspath;
    baton.wc_root_abspath = shelf_version->shelf->wc_root_abspath;
    baton.file_relpath = file_relpath;
    baton.found = FALSE;
    baton.test_only = TRUE;
    baton.conflict = FALSE;
    baton.ctx = shelf_version->shelf->ctx;
    SVN_ERR(walk_shelved_files(shelf_version,
                               apply_file_visitor, &baton,
                               scratch_pool));
    if (baton.found)
      {
        *conflict_p = baton.conflict;
        return SVN_NO_ERROR;
      }
  }

  /* Apply the patch. Any files added above can now have their props set. */
  fb.path = file_relpath;

  nb.conflict = FALSE;
  ctx_notify_func = ctx->notify_func2;
  ctx_notify_baton = ctx->notify_baton2;
  ctx->notify_func2 = patch_notify;
  ctx->notify_baton2 = &nb;

  SVN_ERR(svn_client_patch(shelf_version->patch_abspath,
                           shelf_version->shelf->wc_root_abspath,
                           TRUE /*dry_run*/, 0 /*strip*/,
                           FALSE /*reverse*/,
                           FALSE /*ignore_whitespace*/,
                           TRUE /*remove_tempfiles*/,
                           patch_filter, &fb,
                           shelf_version->shelf->ctx, scratch_pool));

  ctx->notify_func2 = ctx_notify_func;
  ctx->notify_baton2 = ctx_notify_baton;

  *conflict_p = nb.conflict;
  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_shelf_apply(svn_client_shelf_version_t *shelf_version,
                       svn_boolean_t dry_run,
                       apr_pool_t *scratch_pool)
{
  /* Apply the whole files stored outside the patch file ('binary' files) */
  {
    const char *files_dir_abspath;
    struct apply_files_baton_t baton = {0};

    SVN_ERR(shelf_version_files_dir_abspath(&files_dir_abspath,
                                            shelf_version->shelf,
                                            shelf_version->version_number,
                                            scratch_pool, scratch_pool));
    baton.files_dir_abspath = files_dir_abspath;
    baton.wc_root_abspath = shelf_version->shelf->wc_root_abspath;
    baton.ctx = shelf_version->shelf->ctx;
    SVN_ERR(walk_shelved_files(shelf_version,
                               apply_file_visitor, &baton,
                               scratch_pool));
  }

  /* Apply the changes stored in the patch file */
  SVN_ERR(svn_client_patch(shelf_version->patch_abspath,
                           shelf_version->shelf->wc_root_abspath,
                           dry_run, 0 /*strip*/,
                           FALSE /*reverse*/,
                           FALSE /*ignore_whitespace*/,
                           TRUE /*remove_tempfiles*/, NULL, NULL,
                           shelf_version->shelf->ctx, scratch_pool));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_shelf_unapply(svn_client_shelf_version_t *shelf_version,
                         svn_boolean_t dry_run,
                         apr_pool_t *scratch_pool)
{
  apr_array_header_t *targets;

  SVN_ERR(shelf_paths_changed(NULL, &targets, shelf_version,
                              TRUE /*as_abspath*/,
                              scratch_pool, scratch_pool));
  if (!dry_run)
    {
      SVN_ERR(svn_client_revert4(targets, svn_depth_empty,
                                 NULL /*changelists*/,
                                 FALSE /*clear_changelists*/,
                                 FALSE /*metadata_only*/,
                                 FALSE /*added_keep_local*/,
                                 shelf_version->shelf->ctx, scratch_pool));
    }
  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_shelf_set_current_version(svn_client_shelf_t *shelf,
                                     int version_number,
                                     apr_pool_t *scratch_pool)
{
  svn_client_shelf_version_t *shelf_version;

  SVN_ERR(svn_client_shelf_version_open(&shelf_version, shelf, version_number,
                                        scratch_pool, scratch_pool));
  SVN_ERR(svn_client_shelf_delete_newer_versions(shelf, shelf_version,
                                                 scratch_pool));
  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_shelf_delete_newer_versions(svn_client_shelf_t *shelf,
                                       svn_client_shelf_version_t *shelf_version,
                                       apr_pool_t *scratch_pool)
{
  int previous_version = shelf_version ? shelf_version->version_number : 0;
  int i;

  /* Delete any newer checkpoints */
  for (i = shelf->max_version; i > previous_version; i--)
    {
      SVN_ERR(shelf_version_delete(shelf, i, scratch_pool));
    }

  shelf->max_version = previous_version;
  SVN_ERR(shelf_write_current(shelf, scratch_pool));
  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_shelf_export_patch(svn_client_shelf_version_t *shelf_version,
                              svn_stream_t *outstream,
                              apr_pool_t *scratch_pool)
{
  svn_stream_t *instream;

  SVN_ERR(svn_stream_open_readonly(&instream, shelf_version->patch_abspath,
                                   scratch_pool, scratch_pool));
  SVN_ERR(svn_stream_copy3(instream,
                           svn_stream_disown(outstream, scratch_pool),
                           NULL, NULL, scratch_pool));
  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_shelf_save_new_version2(svn_client_shelf_version_t **new_version_p,
                                   svn_client_shelf_t *shelf,
                                   const apr_array_header_t *paths,
                                   svn_depth_t depth,
                                   const apr_array_header_t *changelists,
                                   apr_pool_t *scratch_pool)
{
  int next_version = shelf->max_version + 1;
  const char *files_dir_abspath;
  const char *patch_abspath;
  svn_boolean_t any_shelved;
  apr_array_header_t *unshelvable;

  SVN_ERR(shelf_version_files_dir_abspath(&files_dir_abspath, shelf, next_version,
                                    scratch_pool, scratch_pool));
  SVN_ERR(get_patch_abspath(&patch_abspath, shelf, next_version,
                            scratch_pool, scratch_pool));
  SVN_ERR(write_patch(&any_shelved, &unshelvable,
                      files_dir_abspath, patch_abspath,
                      paths, depth, changelists,
                      shelf->wc_root_abspath,
                      shelf->ctx, scratch_pool, scratch_pool));

  if (unshelvable->nelts > 0)
    {
      return svn_error_createf(SVN_ERR_ILLEGAL_TARGET, NULL,
                               Q_("%d path could not be shelved",
                                  "%d paths could not be shelved",
                                  unshelvable->nelts),
                               unshelvable->nelts);
    }

  if (any_shelved)
    {
      shelf->max_version = next_version;
      SVN_ERR(shelf_write_current(shelf, scratch_pool));

      if (new_version_p)
        SVN_ERR(svn_client_shelf_version_open(new_version_p, shelf, next_version,
                                              scratch_pool, scratch_pool));
    }
  else
    {
      if (new_version_p)
        *new_version_p = NULL;
    }
  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_shelf_save_new_version(svn_client_shelf_t *shelf,
                                  const apr_array_header_t *paths,
                                  svn_depth_t depth,
                                  const apr_array_header_t *changelists,
                                  apr_pool_t *scratch_pool)
{
  SVN_ERR(svn_client_shelf_save_new_version2(NULL, shelf,
                                             paths, depth, changelists,
                                             scratch_pool));
  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_shelf_get_log_message(char **log_message,
                                 svn_client_shelf_t *shelf,
                                 apr_pool_t *result_pool)
{
  svn_string_t *propval = svn_hash_gets(shelf->revprops, SVN_PROP_REVISION_LOG);

  if (propval)
    *log_message = apr_pstrdup(result_pool, propval->data);
  else
    *log_message = NULL;
  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_shelf_set_log_message(svn_client_shelf_t *shelf,
                                 const char *message,
                                 apr_pool_t *scratch_pool)
{
  svn_string_t *propval
    = message ? svn_string_create(message, shelf->pool) : NULL;

  SVN_ERR(svn_client_shelf_revprop_set(shelf, SVN_PROP_REVISION_LOG, propval,
                                       scratch_pool));
  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_shelf_list(apr_hash_t **shelf_infos,
                      const char *local_abspath,
                      svn_client_ctx_t *ctx,
                      apr_pool_t *result_pool,
                      apr_pool_t *scratch_pool)
{
  const char *wc_root_abspath;
  char *shelves_dir;
  apr_hash_t *dirents;
  apr_hash_index_t *hi;

  SVN_ERR(svn_wc__get_wcroot(&wc_root_abspath, ctx->wc_ctx, local_abspath,
                             scratch_pool, scratch_pool));
  SVN_ERR(svn_wc__get_shelves_dir(&shelves_dir, ctx->wc_ctx, local_abspath,
                                  scratch_pool, scratch_pool));
  SVN_ERR(svn_io_get_dirents3(&dirents, shelves_dir, FALSE /*only_check_type*/,
                              result_pool, scratch_pool));

  *shelf_infos = apr_hash_make(result_pool);

  /* Remove non-shelves */
  for (hi = apr_hash_first(scratch_pool, dirents); hi; hi = apr_hash_next(hi))
    {
      const char *filename = apr_hash_this_key(hi);
      svn_io_dirent2_t *dirent = apr_hash_this_val(hi);
      char *name = NULL;

      svn_error_clear(shelf_name_from_filename(&name, filename, result_pool));
      if (name && dirent->kind == svn_node_file)
        {
          svn_client_shelf_info_t *info
            = apr_palloc(result_pool, sizeof(*info));

          info->mtime = dirent->mtime;
          svn_hash_sets(*shelf_infos, name, info);
        }
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_shelf_version_open(svn_client_shelf_version_t **shelf_version_p,
                              svn_client_shelf_t *shelf,
                              int version_number,
                              apr_pool_t *result_pool,
                              apr_pool_t *scratch_pool)
{
  svn_client_shelf_version_t *shelf_version
    = apr_palloc(result_pool, sizeof(*shelf_version));
  const svn_io_dirent2_t *dirent;

  shelf_version->shelf = shelf;
  SVN_ERR(get_existing_patch_abspath(&shelf_version->patch_abspath,
                                     shelf, version_number,
                                     result_pool, scratch_pool));
  SVN_ERR(svn_io_stat_dirent2(&dirent,
                              shelf_version->patch_abspath,
                              FALSE /*verify_truename*/,
                              TRUE /*ignore_enoent*/,
                              result_pool, scratch_pool));
  shelf_version->mtime = dirent->mtime;
  shelf_version->version_number = version_number;
  *shelf_version_p = shelf_version;
  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_shelf_get_newest_version(svn_client_shelf_version_t **shelf_version_p,
                                    svn_client_shelf_t *shelf,
                                    apr_pool_t *result_pool,
                                    apr_pool_t *scratch_pool)
{
  if (shelf->max_version == 0)
    {
      *shelf_version_p = NULL;
      return SVN_NO_ERROR;
    }

  SVN_ERR(svn_client_shelf_version_open(shelf_version_p,
                                        shelf, shelf->max_version,
                                        result_pool, scratch_pool));
  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_shelf_get_all_versions(apr_array_header_t **versions_p,
                                  svn_client_shelf_t *shelf,
                                  apr_pool_t *result_pool,
                                  apr_pool_t *scratch_pool)
{
  int i;

  *versions_p = apr_array_make(result_pool, shelf->max_version - 1,
                               sizeof(svn_client_shelf_version_t *));

  for (i = 1; i <= shelf->max_version; i++)
    {
      svn_client_shelf_version_t *shelf_version;

      SVN_ERR(svn_client_shelf_version_open(&shelf_version,
                                            shelf, i,
                                            result_pool, scratch_pool));
      APR_ARRAY_PUSH(*versions_p, svn_client_shelf_version_t *) = shelf_version;
    }
  return SVN_NO_ERROR;
}
