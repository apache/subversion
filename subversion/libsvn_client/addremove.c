/*
 * addremove.c: integrate unversioned structural changes into working copy
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

#include "svn_hash.h"
#include "svn_wc.h"
#include "svn_client.h"
#include "svn_pools.h"
#include "svn_error.h"
#include "svn_dirent_uri.h"
#include "svn_io.h"
#include "client.h"

#include "private/svn_client_private.h"
#include "private/svn_wc_private.h"
#include "private/svn_magic.h"
#include "private/svn_sorts_private.h"

#include "svn_private_config.h"



/*** Code. ***/

struct addremove_status_baton {
  /* Status info for missing paths. */
  apr_hash_t *missing;

  /* Status info for unversioned paths. */
  apr_hash_t *unversioned;
};

/* Implements svn_wc_status_func4_t. */
static svn_error_t *
addremove_status_func(void *baton, const char *local_abspath,
                      const svn_wc_status3_t *status,
                      apr_pool_t *scratch_pool)
{
  struct addremove_status_baton *b = baton;
  apr_hash_t *hash = NULL;

  switch (status->node_status)
    {
      case svn_wc_status_unversioned:
        hash = b->unversioned;
        break;

      case svn_wc_status_missing:
        hash = b->missing;
        break;

      default:
        break;
    }

  if (hash)
    {
      apr_pool_t *result_pool = apr_hash_pool_get(hash);

      svn_hash_sets(hash, apr_pstrdup(result_pool, local_abspath),
                    svn_wc_dup_status3(status, result_pool));
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
suggest_file_moves(apr_hash_t **moves,
                   const char *added_abspath,
                   apr_hash_t *deleted,
                   svn_client_ctx_t *ctx,
                   apr_pool_t *result_pool,
                   apr_pool_t *scratch_pool)
{
  apr_array_header_t *similar_abspaths;
  int i;
  apr_pool_t *iterpool;
  
  SVN_ERR(svn_wc__find_similar_files(&similar_abspaths, ctx->wc_ctx,
                                     added_abspath,
                                     ctx->cancel_func, ctx->cancel_baton,
                                     result_pool, scratch_pool));

  iterpool = svn_pool_create(scratch_pool);
  for (i = 0; i < similar_abspaths->nelts; i++)
    {
      apr_array_header_t *move_targets;
      const char *similar_abspath = APR_ARRAY_IDX(similar_abspaths, i,
                                                  const char *);

      svn_pool_clear(iterpool);

      /* Skip files which were already versioned before addremove. */
      if (svn_hash_gets(deleted, similar_abspath) == NULL)
        continue;

      move_targets = svn_hash_gets(*moves, similar_abspath);
      if (move_targets == NULL)
        {
          move_targets = apr_array_make(result_pool, 1,
                                        sizeof (const char *));
          svn_hash_sets(*moves, similar_abspath, move_targets);
        }

      APR_ARRAY_PUSH(move_targets, const char *) = 
        apr_pstrdup(result_pool, added_abspath);
    }
  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

/* Check whether the directories at DELETED_DIR_ABSPATH and ADDED_DIR_ABSPATH
 * can be considered a match.
 * Requires that all directory entries match up in terms of name
 * and node kind, recursively. */
static svn_error_t *
match_dirs_recursively(svn_boolean_t *found_match,
                       const char *deleted_dir_abspath,
                       const char *added_dir_abspath,
                       svn_client_ctx_t *ctx,
                       apr_pool_t *scratch_pool)
{
  const apr_array_header_t *children1, *children2;
  int i;
  svn_boolean_t match = TRUE;
  apr_pool_t *iterpool;

  *found_match = FALSE;

  SVN_ERR(svn_wc__node_get_children_of_working_node(&children1,
                                                    ctx->wc_ctx,
                                                    deleted_dir_abspath,
                                                    scratch_pool,
                                                    scratch_pool));
  svn_sort__array((apr_array_header_t *)children1, svn_sort_compare_paths);

  SVN_ERR(svn_wc__node_get_children_of_working_node(&children2,
                                                    ctx->wc_ctx,
                                                    added_dir_abspath,
                                                    scratch_pool,
                                                    scratch_pool));
  if (children1->nelts != children2->nelts)
    return SVN_NO_ERROR;

  svn_sort__array((apr_array_header_t *)children2, svn_sort_compare_paths);
  iterpool = svn_pool_create(scratch_pool);
  for (i = 0; i < children1->nelts; i++)
    {
      const char *child1_abspath = APR_ARRAY_IDX(children1, i, const char *);
      const char *child2_abspath = APR_ARRAY_IDX(children2, i, const char *);
      const char *basename1, *basename2;
      svn_node_kind_t kind1, kind2;

      svn_pool_clear(iterpool);

      basename1 = svn_dirent_basename(child1_abspath, iterpool);
      basename2 = svn_dirent_basename(child2_abspath, iterpool);

      /* Verify basename. */
      if (strcmp(basename1, basename2) != 0)
        {
          match = FALSE;
          break;
        }

      /* Verify node kind. */
      SVN_ERR(svn_wc_read_kind2(&kind1, ctx->wc_ctx, child1_abspath,
                                TRUE, FALSE, iterpool));
      SVN_ERR(svn_wc_read_kind2(&kind2, ctx->wc_ctx, child2_abspath,
                                FALSE, FALSE, iterpool));
      if (kind1 != kind2)
        {
          match = FALSE;
          break;
        }

      if (kind1 == svn_node_dir && kind2 == svn_node_dir)
        {
          SVN_ERR(match_dirs_recursively(&match,
                                         child1_abspath, child2_abspath,
                                         ctx, iterpool));
          if (!match)
            break;
        }
    }
  svn_pool_destroy(iterpool);

  *found_match = match;
  return SVN_NO_ERROR;
}

static svn_error_t *
suggest_dir_moves(apr_hash_t **moves,
                  const char *added_abspath,
                  apr_hash_t *deleted,
                  svn_client_ctx_t *ctx,
                  apr_pool_t *result_pool,
                  apr_pool_t *scratch_pool)
{
  apr_hash_index_t *hi; 
  apr_pool_t *iterpool;
  
  iterpool = svn_pool_create(scratch_pool);
  for (hi = apr_hash_first(scratch_pool, deleted); hi;
       hi = apr_hash_next(hi))
    {
      apr_array_header_t *move_targets;
      const char *deleted_abspath = apr_hash_this_key(hi);
      const svn_wc_status3_t *deleted_status = apr_hash_this_val(hi);
      svn_boolean_t match = FALSE;

      if (deleted_status->kind != svn_node_dir)
        continue;

      svn_pool_clear(iterpool);

      SVN_ERR(match_dirs_recursively(&match, deleted_abspath, added_abspath,
                                     ctx, iterpool));
      if (match)
        {
          move_targets = svn_hash_gets(*moves, deleted_abspath);
          if (move_targets == NULL)
            {
              move_targets = apr_array_make(result_pool, 1,
                                            sizeof (const char *));
              svn_hash_sets(*moves, deleted_abspath, move_targets);
            }

          APR_ARRAY_PUSH(move_targets, const char *) = 
            apr_pstrdup(result_pool, added_abspath);
        }
    }
  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

/* Indicate whether MOVES already covers an ADDED_ABSPATH. */
static svn_boolean_t
already_moved(apr_hash_t *moves, const char *added_abspath,
              apr_pool_t *scratch_pool)
{
  apr_hash_index_t *hi;

  for (hi = apr_hash_first(scratch_pool, moves); hi;
       hi = apr_hash_next(hi))
    {
      apr_array_header_t *move_targets = apr_hash_this_val(hi);
      int i;

      for (i = 0; i < move_targets->nelts; i++) 
        {
          const char *dst_abspath = APR_ARRAY_IDX(move_targets, i,
                                                  const char *);
          if (svn_dirent_is_child(dst_abspath, added_abspath, NULL) != NULL)
            return 1;
        }
    }

  return 0;
}

static svn_error_t *
suggest_moves(apr_hash_t **moves,
              apr_hash_t *deleted,
              apr_hash_t *added,
              svn_client_ctx_t *ctx,
              apr_pool_t *result_pool,
              apr_pool_t *scratch_pool)
{
  apr_hash_index_t *hi;
  apr_pool_t *iterpool;

  *moves = apr_hash_make(result_pool);

  iterpool = svn_pool_create(scratch_pool);
  for (hi = apr_hash_first(scratch_pool, added); hi;
       hi = apr_hash_next(hi))
    {
      const char *added_abspath = apr_hash_this_key(hi);
      const svn_wc_status3_t *status = apr_hash_this_val(hi);

      svn_pool_clear(iterpool);

      if (status->actual_kind == svn_node_dir)
        SVN_ERR(suggest_dir_moves(moves, added_abspath, deleted, ctx,
                                  result_pool, iterpool));
    }

  for (hi = apr_hash_first(scratch_pool, added); hi;
       hi = apr_hash_next(hi))
    {
      const char *added_abspath = apr_hash_this_key(hi);
      const svn_wc_status3_t *status = apr_hash_this_val(hi);

      svn_pool_clear(iterpool);

      if (status->actual_kind == svn_node_file &&
          !already_moved(*moves, added_abspath, iterpool))
        SVN_ERR(suggest_file_moves(moves, added_abspath, deleted, ctx,
                                   result_pool, iterpool));
    }

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

static svn_error_t *
match_up_new_deletes_and_adds(const char *local_abspath,
                              apr_hash_t *deleted,
                              apr_hash_t *added,
                              svn_depth_t depth,
                              svn_client_ctx_t *ctx,
                              apr_pool_t *scratch_pool)
{
  apr_hash_t *moves;
  apr_hash_index_t *hi;
  apr_pool_t *iterpool;

  SVN_ERR(suggest_moves(&moves, deleted, added,
                        ctx, scratch_pool, scratch_pool));

  iterpool = svn_pool_create(scratch_pool);
  for (hi = apr_hash_first(scratch_pool, moves); hi;
       hi = apr_hash_next(hi))
    {
      const char *src_abspath = apr_hash_this_key(hi);
      apr_array_header_t *move_targets = apr_hash_this_val(hi);
      svn_boolean_t is_ambiguous_move = (move_targets->nelts > 1);
      int i;

      svn_pool_clear(iterpool);

      for (i = 0; i < move_targets->nelts; i++) 
        {
          const char *dst_abspath = APR_ARRAY_IDX(move_targets, i,
                                                  const char *);
          SVN_ERR(svn_wc__fixup_copyfrom(ctx->wc_ctx, src_abspath, dst_abspath,
                                         !is_ambiguous_move, /* is_move */
                                         ctx->cancel_func, ctx->cancel_baton,
                                         ctx->notify_func2, ctx->notify_baton2,
                                         iterpool));
        }
    }
  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}


static svn_error_t *
addremove(const char *local_abspath, svn_depth_t depth,
          svn_boolean_t no_autoprops, svn_boolean_t no_ignore,
          svn_client_ctx_t *ctx, apr_pool_t *scratch_pool)
{
  svn_magic__cookie_t *magic_cookie;
  struct addremove_status_baton b;
  apr_hash_index_t *hi;
  apr_pool_t *iterpool;

  SVN_ERR(svn_magic__init(&magic_cookie, ctx->config, scratch_pool));

  b.missing = apr_hash_make(scratch_pool);
  b.unversioned = apr_hash_make(scratch_pool);

  SVN_ERR(svn_wc_walk_status(ctx->wc_ctx, local_abspath, depth,
                             TRUE, FALSE, FALSE, NULL,
                             addremove_status_func, &b,
                             ctx->cancel_func, ctx->cancel_baton,
                             scratch_pool));

  iterpool = svn_pool_create(scratch_pool);
  for (hi = apr_hash_first(scratch_pool, b.unversioned); hi;
       hi = apr_hash_next(hi))
    {
      const char *unversioned_abspath = apr_hash_this_key(hi);
      svn_node_kind_t kind_on_disk;

      svn_pool_clear(iterpool);

      SVN_ERR(svn_io_check_path(unversioned_abspath, &kind_on_disk,
                                scratch_pool));

      if (kind_on_disk == svn_node_file)
        {
          SVN_ERR(svn_client__add_file(unversioned_abspath,
                                       magic_cookie,
                                       NULL,
                                       no_autoprops,
                                       ctx, iterpool));
        }
      else if (kind_on_disk == svn_node_dir && depth >= svn_depth_immediates)
        {
          svn_depth_t depth_below_here = depth;

          if (depth == svn_depth_immediates)
            depth_below_here = svn_depth_empty;

          SVN_ERR(svn_client__add_dir_recursive(
                    unversioned_abspath, depth_below_here,
                    FALSE, /* force */
                    no_autoprops,
                    magic_cookie,
                    NULL,
                    !no_ignore,
                    NULL,
                    ctx, iterpool, iterpool));
        }
    }

  for (hi = apr_hash_first(scratch_pool, b.missing); hi;
       hi = apr_hash_next(hi))
    {
      const char *missing_abspath = apr_hash_this_key(hi);

      svn_pool_clear(iterpool);

      SVN_ERR(svn_wc_delete4(ctx->wc_ctx, missing_abspath,
                             FALSE, /* keep_local */
                             FALSE, /* delete_unversioned_target */
                             ctx->cancel_func, ctx->cancel_baton,
                             ctx->notify_func2, ctx->notify_baton2,
                             iterpool));
    }
  svn_pool_destroy(iterpool);

  SVN_ERR(match_up_new_deletes_and_adds(local_abspath,
                                        b.missing, b.unversioned,
                                        depth, ctx, scratch_pool));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_addremove(const char *local_path,
                     svn_depth_t depth,
                     svn_boolean_t no_autoprops,
                     svn_boolean_t no_ignore,
                     svn_client_ctx_t *ctx,
                     apr_pool_t *scratch_pool)
{
  const char *local_abspath;

  SVN_ERR(svn_dirent_get_absolute(&local_abspath, local_path, scratch_pool));

  SVN_WC__CALL_WITH_WRITE_LOCK(
    addremove(local_abspath, depth, no_autoprops, no_ignore, ctx, scratch_pool),
    ctx->wc_ctx, local_abspath, TRUE, scratch_pool);

  return SVN_NO_ERROR;
}
