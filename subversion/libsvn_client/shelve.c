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



/*** Includes. ***/

#include "svn_client.h"
#include "svn_wc.h"
#include "svn_pools.h"
#include "svn_dirent_uri.h"
#include "svn_path.h"
#include "svn_hash.h"
#include "svn_utf.h"

#include "client.h"
#include "private/svn_wc_private.h"
#include "svn_private_config.h"


/*  */
static svn_error_t *
validate_shelf_name(const char *shelf_name,
                    apr_pool_t *scratch_pool)
{
  if (shelf_name[0] == '\0' || strchr(shelf_name, '/'))
    return svn_error_createf(SVN_ERR_BAD_CHANGELIST_NAME, NULL,
                             _("Shelve: Bad name '%s'"), shelf_name);

  return SVN_NO_ERROR;
}

/*  */
static svn_error_t *
get_patch_abspath(char **patch_abspath,
                  const char *local_path,
                  const char *shelf_name,
                  svn_client_ctx_t *ctx,
                  apr_pool_t *result_pool,
                  apr_pool_t *scratch_pool)
{
  char *dir;
  const char *local_abspath;
  const char *filename;

  SVN_ERR(svn_dirent_get_absolute(&local_abspath, local_path, scratch_pool));
  SVN_ERR(svn_wc__get_shelves_dir(&dir, ctx->wc_ctx, local_abspath,
                                  scratch_pool, scratch_pool));
  filename = apr_pstrcat(scratch_pool, shelf_name, ".patch", SVN_VA_NULL);
  *patch_abspath = svn_dirent_join(dir, filename, result_pool);
  return SVN_NO_ERROR;
}

/*  */
static svn_error_t *
write_patch(const char *patch_abspath,
            const apr_array_header_t *paths,
            svn_depth_t depth,
            const apr_array_header_t *changelists,
            svn_client_ctx_t *ctx,
            apr_pool_t *scratch_pool)
{
  svn_stream_t *outstream;
  svn_stream_t *errstream;
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);
  int i;
  svn_opt_revision_t peg_revision = {svn_opt_revision_unspecified, {0}};
  svn_opt_revision_t start_revision = {svn_opt_revision_base, {0}};
  svn_opt_revision_t end_revision = {svn_opt_revision_working, {0}};

  /* Get streams for the output and any error output of the diff. */
  SVN_ERR(svn_stream_open_writable(&outstream, patch_abspath,
                                   scratch_pool, scratch_pool));
  SVN_ERR(svn_stream_for_stderr(&errstream, scratch_pool));

  for (i = 0; i < paths->nelts; i++)
    {
      const char *path = APR_ARRAY_IDX(paths, i, const char *);

      if (svn_path_is_url(path))
        return svn_error_createf(SVN_ERR_ILLEGAL_TARGET, NULL,
                                 _("'%s' is not a local path"), path);

      SVN_ERR(svn_client_diff_peg6(
                     NULL /*options*/,
                     path,
                     &peg_revision,
                     &start_revision,
                     &end_revision,
                     NULL,
                     depth,
                     TRUE /*notice_ancestry*/,
                     FALSE /*no_diff_added*/,
                     FALSE /*no_diff_deleted*/,
                     TRUE /*show_copies_as_adds*/,
                     FALSE /*ignore_content_type: FALSE -> omit binary files*/,
                     FALSE /*ignore_properties*/,
                     FALSE /*properties_only*/,
                     FALSE /*use_git_diff_format*/,
                     SVN_APR_LOCALE_CHARSET,
                     outstream,
                     errstream,
                     changelists,
                     ctx, iterpool));
    }
  SVN_ERR(svn_stream_close(outstream));
  SVN_ERR(svn_stream_close(errstream));

  return SVN_NO_ERROR;
}

/*  */
static svn_error_t *
apply_patch(const char *patch_abspath,
            const char *wc_dir_abspath,
            svn_boolean_t reverse,
            svn_boolean_t dry_run,
            svn_client_ctx_t *ctx,
            apr_pool_t *scratch_pool)
{
  SVN_ERR(svn_client_patch(patch_abspath, wc_dir_abspath,
                           dry_run, 0 /*strip*/,
                           reverse,
                           FALSE /*ignore_whitespace*/,
                           TRUE /*remove_tempfiles*/, NULL, NULL,
                           ctx, scratch_pool));

  return SVN_NO_ERROR;
}

/*  */
static svn_error_t *
delete_patch(const char *patch_abspath,
             apr_pool_t *pool)
{
  SVN_ERR(svn_io_remove_file2(patch_abspath, FALSE /*ignore_enoent*/, pool));
  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_shelve(const char *shelf_name,
                  const apr_array_header_t *paths,
                  svn_depth_t depth,
                  const apr_array_header_t *changelists,
                  svn_boolean_t dry_run,
                  svn_client_ctx_t *ctx,
                  apr_pool_t *pool)
{
  const char *local_abspath;
  const char *wc_root_abspath;
  char *patch_abspath;
  svn_error_t *err;

  SVN_ERR(validate_shelf_name(shelf_name, pool));

  /* ### TODO: check all paths are in same WC; for now use first path */
  SVN_ERR(svn_dirent_get_absolute(&local_abspath,
                                  APR_ARRAY_IDX(paths, 0, char *), pool));
  SVN_ERR(svn_client_get_wc_root(&wc_root_abspath,
                                 local_abspath, ctx, pool, pool));
  SVN_ERR(get_patch_abspath(&patch_abspath,
                            wc_root_abspath, shelf_name, ctx, pool, pool));

  err = write_patch(patch_abspath, paths, depth, changelists, ctx, pool);
  if (err && APR_STATUS_IS_EEXIST(err->apr_err))
    {
      return svn_error_quick_wrapf(err,
                                   "Shelved change '%s' already exists",
                                   shelf_name);
    }
  else
    SVN_ERR(err);

  /* Reverse-apply the patch. This should be a safer way to remove those
     changes from the WC than running a 'revert' operation. */
  SVN_ERR(apply_patch(patch_abspath, wc_root_abspath,
                      TRUE /*reverse*/,
                      dry_run,
                      ctx, pool));

  if (dry_run)
    {
      SVN_ERR(delete_patch(patch_abspath, pool));
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_unshelve(const char *shelf_name,
                    const char *local_abspath,
                    svn_boolean_t keep,
                    svn_boolean_t dry_run,
                    svn_client_ctx_t *ctx,
                    apr_pool_t *pool)
{
  const char *wc_root_abspath;
  char *patch_abspath;
  svn_error_t *err;

  SVN_ERR(validate_shelf_name(shelf_name, pool));

  SVN_ERR(svn_client_get_wc_root(&wc_root_abspath,
                                 local_abspath, ctx, pool, pool));
  SVN_ERR(get_patch_abspath(&patch_abspath,
                            local_abspath, shelf_name, ctx, pool, pool));

  /* Apply the patch. */
  err = apply_patch(patch_abspath, wc_root_abspath,
                    FALSE /*reverse*/,
                    dry_run /*dry_run*/,
                    ctx, pool);
  if (err && err->apr_err == SVN_ERR_ILLEGAL_TARGET)
    {
      return svn_error_quick_wrapf(err,
                                   "Shelved change '%s' not found",
                                   shelf_name);
    }
  else
    SVN_ERR(err);

  /* Remove the patch. */
  if (! keep && ! dry_run)
    {
      SVN_ERR(delete_patch(patch_abspath, pool));
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_shelves_delete(const char *shelf_name,
                          const char *local_abspath,
                          svn_boolean_t dry_run,
                          svn_client_ctx_t *ctx,
                          apr_pool_t *pool)
{
  const char *wc_root_abspath;
  char *patch_abspath;

  SVN_ERR(validate_shelf_name(shelf_name, pool));

  SVN_ERR(svn_client_get_wc_root(&wc_root_abspath,
                                 local_abspath, ctx, pool, pool));
  SVN_ERR(get_patch_abspath(&patch_abspath,
                            local_abspath, shelf_name, ctx, pool, pool));

  /* Remove the patch. */
  if (! dry_run)
    {
      svn_error_t *err;

      err = delete_patch(patch_abspath, pool);
      if (err && APR_STATUS_IS_ENOENT(err->apr_err))
        {
          return svn_error_quick_wrapf(err,
                                       "Shelved change '%s' not found",
                                       shelf_name);
        }
      else
        SVN_ERR(err);
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_shelves_list(apr_hash_t **dirents,
                        const char *local_abspath,
                        svn_client_ctx_t *ctx,
                        apr_pool_t *result_pool,
                        apr_pool_t *scratch_pool)
{
  char *shelves_dir;
  apr_hash_index_t *hi;

  SVN_ERR(svn_wc__get_shelves_dir(&shelves_dir, ctx->wc_ctx, local_abspath,
                                  scratch_pool, scratch_pool));
  SVN_ERR(svn_io_get_dirents3(dirents, shelves_dir, TRUE /*only_check_type*/,
                              result_pool, scratch_pool));

  /* Remove non-shelves */
  for (hi = apr_hash_first(scratch_pool, *dirents); hi; hi = apr_hash_next(hi))
    {
      const char *name = apr_hash_this_key(hi);

      if (! strstr(name, ".patch"))
        {
          svn_hash_sets(*dirents, name, NULL);
        }
    }

  return SVN_NO_ERROR;
}

