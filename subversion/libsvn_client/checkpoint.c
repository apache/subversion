/*
 * checkpoint.c:  implementation of the 'checkpoint' commands
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
#include "private/svn_sorts_private.h"
#include "svn_private_config.h"

/*  */
static svn_error_t *
read_current(int *current,
             const char *local_abspath,
             svn_client_ctx_t *ctx,
             apr_pool_t *scratch_pool)
{
  char *dir;
  const char *current_abspath;

  SVN_ERR(svn_wc__get_shelves_dir(&dir, ctx->wc_ctx, local_abspath,
                                  scratch_pool, scratch_pool));
  current_abspath = svn_dirent_join(dir, "current", scratch_pool);

  {
    FILE *fp = fopen(current_abspath, "r");

    if (! fp)
      {
        *current = 0;
        return SVN_NO_ERROR;
      }
    fscanf(fp, "%d", current);
    fclose(fp);
  }

  return SVN_NO_ERROR;
}

/*  */
static svn_error_t *
write_current(int current,
              const char *local_abspath,
              svn_client_ctx_t *ctx,
              apr_pool_t *scratch_pool)
{
  char *dir;
  const char *current_abspath;

  SVN_ERR(svn_wc__get_shelves_dir(&dir, ctx->wc_ctx, local_abspath,
                                  scratch_pool, scratch_pool));
  current_abspath = svn_dirent_join(dir, "current", scratch_pool);

  {
    FILE *fp = fopen(current_abspath, "w");
    fprintf(fp, "%d", current);
    fclose(fp);
  }

  return SVN_NO_ERROR;
}

/*  */
static char *
format_checkpoint_name(int checkpoint_number,
                       apr_pool_t *result_pool)
{
  return apr_psprintf(result_pool, "checkpoint-%03d",
                      checkpoint_number);
}

/* Write a checkpoint patch of the whole WC. */
static svn_error_t *
write_checkpoint(int checkpoint_number,
                 /*const apr_array_header_t *paths,
                   svn_depth_t depth,
                   const apr_array_header_t *changelists,*/
                 const char *local_abspath,
                 svn_client_ctx_t *ctx,
                 apr_pool_t *scratch_pool)
{
  const char *wc_root_abspath;
  char *shelf_name = format_checkpoint_name(checkpoint_number, scratch_pool);

  apr_array_header_t *paths = apr_array_make(scratch_pool, 1, sizeof(char *));

  SVN_ERR(svn_client_get_wc_root(&wc_root_abspath, local_abspath,
                                 ctx, scratch_pool, scratch_pool));
  APR_ARRAY_PUSH(paths, const char *) = wc_root_abspath;
  SVN_ERR(svn_client_shelf_write_patch(
            shelf_name, "" /*message*/, wc_root_abspath,
            TRUE /*overwrite_existing*/,
            paths, svn_depth_infinity, NULL /*changelists*/,
            ctx, scratch_pool));
  return SVN_NO_ERROR;
}

/*  */
static svn_error_t *
apply_checkpoint(int checkpoint_number,
                 const char *wc_root_abspath,
                 svn_boolean_t reverse,
                 svn_boolean_t dry_run,
                 svn_client_ctx_t *ctx,
                 apr_pool_t *scratch_pool)
{
  char *shelf_name = format_checkpoint_name(checkpoint_number, scratch_pool);

  SVN_ERR(svn_client_shelf_apply_patch(shelf_name, wc_root_abspath,
                                       reverse, dry_run,
                                       ctx, scratch_pool));

  return SVN_NO_ERROR;
}

/*  */
static svn_error_t *
delete_checkpoint(int checkpoint_number,
                  const char *wc_root_abspath,
                  svn_client_ctx_t *ctx,
                  apr_pool_t *scratch_pool)
{
  char *shelf_name = format_checkpoint_name(checkpoint_number, scratch_pool);

  SVN_ERR(svn_client_shelf_delete_patch(shelf_name, wc_root_abspath,
                                        ctx, scratch_pool));
  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_checkpoint_get_current(int *checkpoint_number_p,
                                  const char *local_abspath,
                                  svn_client_ctx_t *ctx,
                                  apr_pool_t *scratch_pool)
{
  SVN_ERR(read_current(checkpoint_number_p, local_abspath, ctx, scratch_pool));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_checkpoint_save(int *checkpoint_number,
                           const char *local_abspath,
                           svn_client_ctx_t *ctx,
                           apr_pool_t *scratch_pool)
{
  int current;

  SVN_ERR(read_current(&current, local_abspath, ctx, scratch_pool));
  current++;

  SVN_ERR(write_checkpoint(current, local_abspath, ctx, scratch_pool));
  SVN_ERR(write_current(current, local_abspath, ctx, scratch_pool));

  *checkpoint_number = current;
  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_checkpoint_restore(int checkpoint_number,
                              const char *local_abspath,
                              svn_boolean_t dry_run,
                              svn_client_ctx_t *ctx,
                              apr_pool_t *scratch_pool)
{
  const char *wc_root_abspath;

  SVN_ERR(svn_client_get_wc_root(&wc_root_abspath, local_abspath,
                                 ctx, scratch_pool, scratch_pool));

  /* Save and revert the current state (of whole WC) */
  /* (Even with dry_run, we write and use and delete a temp checkpoint) */
  {
    /* Write a temp checkpoint */
    SVN_ERR(write_checkpoint(-1, local_abspath, ctx, scratch_pool));

    /* Revert it */
    SVN_ERR(apply_checkpoint(-1, wc_root_abspath,
                             TRUE /*reverse*/, dry_run,
                             ctx, scratch_pool));

    /* Delete it */
    SVN_ERR(delete_checkpoint(-1, wc_root_abspath,
                              ctx, scratch_pool));
  }

  /* Restore the requested checkpoint (if > 0) */
  if (checkpoint_number > 0)
    {
      SVN_ERR(apply_checkpoint(checkpoint_number, wc_root_abspath,
                               FALSE /*reverse*/, dry_run,
                               ctx, scratch_pool));
    }

  SVN_ERR(write_current(checkpoint_number, local_abspath, ctx, scratch_pool));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_checkpoint_delete(int checkpoint_number,
                             const char *local_abspath,
                             svn_boolean_t dry_run,
                             svn_client_ctx_t *ctx,
                             apr_pool_t *scratch_pool)
{
  if (! dry_run)
    {
      SVN_ERR(delete_checkpoint(checkpoint_number, local_abspath,
                                ctx, scratch_pool));
    }
  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_checkpoint_list(apr_array_header_t **checkpoints,
                           const char *local_abspath,
                           svn_client_ctx_t *ctx,
                           apr_pool_t *result_pool,
                           apr_pool_t *scratch_pool)
{
  char *checkpoints_dir;
  apr_hash_t *dirents;
  apr_hash_index_t *hi;

  SVN_ERR(svn_wc__get_shelves_dir(&checkpoints_dir, ctx->wc_ctx, local_abspath,
                                  scratch_pool, scratch_pool));
  SVN_ERR(svn_io_get_dirents3(&dirents, checkpoints_dir,
                              FALSE /*only_check_type*/,
                              scratch_pool, scratch_pool));

  /* Remove non-checkpoint entries */
  for (hi = apr_hash_first(scratch_pool, dirents); hi; hi = apr_hash_next(hi))
    {
      const char *name = apr_hash_this_key(hi);

      if (strncmp(name, "checkpoint-", 11) != 0)
        {
          svn_hash_sets(dirents, name, NULL);
        }
    }
  *checkpoints = svn_sort__hash(dirents, svn_sort_compare_items_lexically,
                                result_pool);

  return SVN_NO_ERROR;
}

