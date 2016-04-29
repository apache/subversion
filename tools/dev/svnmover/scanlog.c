/*
 * scanlog.c:  scanning the log for moves
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
#include "svn_error.h"
#include "svn_config.h"
#include "svn_time.h"
#include "svn_dirent_uri.h"
#include "svn_path.h"
#include "svn_pools.h"
#include "svn_io.h"

#include "private/svn_client_private.h"
#include "private/svn_wc_private.h"
#include "private/svn_ra_private.h"

#include "svnmover.h"

#include "svn_private_config.h"


/* From moves-scan-log branch */

svn_repos_move_info_t *
svn_repos_move_info_create(const char *moved_from_repos_relpath,
                           const char *moved_to_repos_relpath,
                           svn_revnum_t revision,
                           svn_revnum_t copyfrom_rev,
                           svn_repos_move_info_t *prev,
                           svn_repos_move_info_t *next,
                           apr_pool_t *result_pool)
{
  svn_repos_move_info_t *move = apr_palloc(result_pool, sizeof(*move));

  move->moved_from_repos_relpath = moved_from_repos_relpath;
  move->moved_to_repos_relpath = moved_to_repos_relpath;
  move->revision = revision;
  move->copyfrom_rev = copyfrom_rev;
  move->prev = prev;
  move->next = next;

  return move;
}

const char *
svn_client__format_move_chain_for_display(svn_repos_move_info_t *first_move,
                                          const char *indent,
                                          apr_pool_t *result_pool)
{
  const char *s;
  svn_repos_move_info_t *last_move;

  last_move = first_move;
  while (last_move->next)
    last_move = last_move->next;

  if (last_move != first_move)
    {
      svn_repos_move_info_t *this_move;

      s = apr_psprintf(result_pool,
                        _("Combined move:\n%s  %s@%ld -> %s\n"
                       "%sIndividual moves:\n"),
                       indent, first_move->moved_from_repos_relpath,
                       first_move->copyfrom_rev,
                       last_move->moved_to_repos_relpath, indent);

      this_move = first_move;
      do
        {
          s = apr_pstrcat(result_pool, s,
                          apr_psprintf(
                            result_pool, _("%s  [r%ld] %s@%ld -> %s\n"),
                            indent,
                            this_move->revision,
                            this_move->moved_from_repos_relpath,
                            this_move->copyfrom_rev,
                            this_move->moved_to_repos_relpath),
                          (char *)NULL);
          this_move = this_move->next;
        }
      while (this_move);
    }
  else
    s = apr_psprintf(result_pool, _("  [r%ld] %s@%ld -> %s\n"),
                     first_move->revision,
                     first_move->moved_from_repos_relpath,
                     first_move->copyfrom_rev,
                     first_move->moved_to_repos_relpath);

  return s;
}

typedef struct scan_moves_log_receiver_baton {
  /*const char *anchor_abspath;*/
  svn_client_ctx_t *ctx;
  svn_revnum_t start;
  svn_revnum_t end;
  svn_ra_session_t *ra_session;

  /* The moved nodes hash to be populated.
   * Maps a revision number to an array of svn_repos_move_info_t
   * objects describing moves which happened in the revision.
   *
   * Given a sequence of moves which happened in given revisions, such as:
   *   rA: mv x->z
   *   rA: mv a->b
   *   rB: mv b->c
   *   rC: mv c->d
   * we map each revision number to all moves which happened in the
   * revision, which looks as follows: 
   *   rA : [(rA, x->z), (rA, a->b)]
   *   rB : [(rB, b->c)]
   *   rC : [(rC, c->d)]
   * This allows an update to find relevant moves based on the base
   * revision of a node (during updates the base revision of each node
   * in the working copy is arbitrary so we might not know the nodes 'a'
   * and 'x' under these names).
   * Additionally, all moves pertaining to the same node are chained into a
   * doubly-linked list via 'next' and 'prev' pointers (see definition of
   * svn_repos_move_info_t).
   * This way, an update can look up all moves relevant to a node, forwards
   * or backwards in history, once it has located a relevant move in the chain.
   * This can be visualized as follows:
   *   rA : [(rA, x->z, prev=>NULL, next=>NULL),
   *         (rA, a->b, prev=>NULL, next=>(rB, b->c))]
   *   rB : [(rB, b->c), prev=>(rA, a->b), next=>(rC, c->d)]
   *   rC : [(rC, c->d), prev=>(rB, c->d), next=>NULL]
   */
  apr_hash_t *moves;

  /* Temporary map of move-target paths to repos_move_info_t.
   * Used to link multiple moves of the same node across revisions. */
  apr_hash_t *moves_by_target_path;
} scan_moves_log_receiver_baton;

typedef struct copy_info {
  const char *copyto_path;
  const char *copyfrom_path;
  svn_revnum_t copyfrom_rev;
} copy_info;


/* Set *RELATED to true if the deleted node at repository relpath
 * DELETED_PATH@DELETED_REV is ancestrally related to the node at
 * repository relpath COPYFROM_PATH@COPYFROM_REV, else set it to false.
 *
 * ### JAF:  In practice this attempts to trace back, starting from
 *       DELETED_PATH@(DELETED_REV-1).  What if that does not exist?
 */
static svn_error_t *
check_ancestry(svn_boolean_t *related,
               const char *session_url,
               const char *repos_root_url,
               const char *deleted_path,
               svn_revnum_t deleted_rev,
               const char *copyfrom_path,
               svn_revnum_t copyfrom_rev,
               svn_client_ctx_t *ctx,
               apr_pool_t *scratch_pool)
{
  apr_hash_t *locations;
  const char *old_url;
  const char *old_location;
  const char *relpath;
  svn_ra_session_t *ra_session2;
  apr_array_header_t *location_revisions;

  *related = FALSE;

  location_revisions = apr_array_make(scratch_pool, 1, sizeof(svn_revnum_t));
  APR_ARRAY_PUSH(location_revisions, svn_revnum_t) = copyfrom_rev;
  old_url = svn_uri_canonicalize(apr_pstrcat(scratch_pool,
                                             repos_root_url, "/",
                                             deleted_path, NULL),
                                 scratch_pool);
  relpath = svn_uri_skip_ancestor(session_url, old_url, scratch_pool);
  SVN_ERR(svn_client_open_ra_session2(&ra_session2, session_url, NULL,
                                      ctx, scratch_pool, scratch_pool));
  if (relpath == NULL)
    {
      svn_error_t *err;

      /* The deleted path is outside of the baton's RA session URL.
       * Try to open the new RA session to the repository root. */
      SVN_ERR(svn_ra_reparent(ra_session2, repos_root_url, scratch_pool));
      relpath = svn_uri_skip_ancestor(repos_root_url, old_url, scratch_pool);
      if (relpath == NULL)
        return SVN_NO_ERROR;
      err = svn_ra_get_locations(ra_session2, &locations, relpath,
                                 deleted_rev - 1, location_revisions,
                                 scratch_pool);
      if (err)
        {
          if (err->apr_err == SVN_ERR_RA_NOT_AUTHORIZED ||
              err->apr_err == SVN_ERR_RA_DAV_FORBIDDEN)
            {
              svn_error_clear(err);
              return SVN_NO_ERROR;
            }
          else
            return svn_error_trace(err);
        }
    }
  else
    SVN_ERR(svn_ra_get_locations(ra_session2, &locations, relpath,
                                 deleted_rev - 1, location_revisions,
                                 scratch_pool));

  old_location = apr_hash_get(locations, &copyfrom_rev, sizeof(svn_revnum_t));
  *related = (old_location &&
              strcmp(old_location[0] == '/' ? old_location + 1 : old_location,
                     copyfrom_path) == 0);

  return SVN_NO_ERROR;
}

static svn_error_t *
scan_moves_log_receiver(void *baton,
                        svn_log_entry_t *log_entry,
                        apr_pool_t *scratch_pool)
{
  apr_hash_index_t *hi;
  apr_hash_t *copies;
  apr_array_header_t *deleted_paths;
  struct scan_moves_log_receiver_baton *b = baton;
  apr_pool_t *result_pool = apr_hash_pool_get(b->moves);
  apr_pool_t *iterpool;
  int i;
  const char *session_url;
  const char *repos_root_url;
  apr_array_header_t *moves;

  if (b->ctx->notify_func2)
    {
#if 0
      svn_wc_notify_t *notify;
      notify = svn_wc_create_notify(b->anchor_abspath,
                                    svn_wc_notify_moves_scan_log_in_progress,
                                    scratch_pool);
      notify->moves_scan_log_start_rev = b->start;
      notify->moves_scan_log_end_rev = b->end;
      notify->moves_scan_log_current_rev = log_entry->revision;
      b->ctx->notify_func2(b->ctx->notify_baton2, notify, scratch_pool);
#endif
    }

  if (log_entry->changed_paths2 == NULL)
    return SVN_NO_ERROR;

  copies = apr_hash_make(scratch_pool);
  deleted_paths = apr_array_make(scratch_pool, 0, sizeof(const char *));

  /* Scan for copied and deleted nodes in this revision. */
  for (hi = apr_hash_first(scratch_pool, log_entry->changed_paths2);
       hi; hi = apr_hash_next(hi))
    {
      const char *path = apr_hash_this_key(hi);
      svn_log_changed_path2_t *data = apr_hash_this_val(hi);

      if ((data->action == 'A' || data->action == 'R') && data->copyfrom_path)
        {
          struct copy_info *copy;
          apr_array_header_t *copies_with_same_source_path;

          SVN_ERR_ASSERT(path[0] == '/');

          if (data->copyfrom_path[0] == '/')
            data->copyfrom_path++;

          copy = apr_palloc(scratch_pool, sizeof(*copy));
          copy->copyto_path = path + 1; /* Strip leading '/' */
          copy->copyfrom_path = data->copyfrom_path;
          copy->copyfrom_rev = data->copyfrom_rev;
          copies_with_same_source_path = apr_hash_get(copies,
                                                      data->copyfrom_path,
                                                      APR_HASH_KEY_STRING);
          if (copies_with_same_source_path == NULL)
            {
              copies_with_same_source_path = apr_array_make(
                                               result_pool, 1,
                                               sizeof(struct copy_info *));
              apr_hash_set(copies, copy->copyfrom_path, APR_HASH_KEY_STRING,
                           copies_with_same_source_path);
            }
          APR_ARRAY_PUSH(copies_with_same_source_path,
                         struct copy_info *) = copy;
        }

      if (data->action == 'D' || data->action == 'R')
        {
          const char *parent_path;

          /* ### Is this true?  What does the API guarantee?  Is it
             ### true that copyfrom_path is a relpath? */
          SVN_ERR_ASSERT(path[0] == '/');

          /* When a delete is within a copy the deleted path in the
             changed_paths2 hash is the copied path, but for the purposes
             of move detection we want the pre-copy path.

             ### Not sure if this is the correct thing to do.  Yes, it
             ### allows us to detect moves in copies/moves but will it
             ### lead to false positives?  Does it matter that the
             ### adjusted path may not have been committed?  Does it
             ### matter that the adjusted path may be the same as
             ### another committed path? */
          parent_path = svn_dirent_dirname(path, scratch_pool);
          while(strcmp(parent_path, "/"))
            {
              svn_log_changed_path2_t *data2
                = apr_hash_get(log_entry->changed_paths2, parent_path,
                               APR_HASH_KEY_STRING);

              if (data2 && data2->action == 'A')
                {
                  const char *relpath = svn_dirent_skip_ancestor(parent_path,
                                                                 path);
                  path = svn_dirent_join_many(scratch_pool, "/",
                                              data2->copyfrom_path, relpath,
                                              NULL);
                  break;
                }
              else
                parent_path = svn_dirent_dirname(parent_path, scratch_pool);
            }
          APR_ARRAY_PUSH(deleted_paths, const char *) = path + 1;
        }
    }

  /* If a node was deleted at one location and copied from the deleted
   * location to a new location within the same revision, put the node
   * on the moved-nodes list. */
  SVN_ERR(svn_ra_get_session_url(b->ra_session, &session_url, scratch_pool));
  SVN_ERR(svn_ra_get_repos_root2(b->ra_session, &repos_root_url, scratch_pool));
  iterpool = svn_pool_create(scratch_pool);
  for (i = 0; i < deleted_paths->nelts; i++)
    {
      const char *deleted_path;
      apr_array_header_t *copies_with_same_source_path;
      svn_repos_move_info_t *new_move;
      svn_repos_move_info_t *prior_move;
      svn_boolean_t related;
      int j;

      deleted_path = APR_ARRAY_IDX(deleted_paths, i, const char *);
      copies_with_same_source_path = apr_hash_get(copies, deleted_path,
                                                  APR_HASH_KEY_STRING);
      if (copies_with_same_source_path == NULL)
        continue;

      svn_pool_clear(iterpool);

      for (j = 0; j < copies_with_same_source_path->nelts; j++)
        {
          struct copy_info *copy;

          copy = APR_ARRAY_IDX(copies_with_same_source_path, j,
                               struct copy_info *);

          /* We found a deleted node which matches the copyfrom path of
           * a copied node. Verify that the deleted node is an ancestor
           * of the copied node. Tracing back history of the deleted node
           * from revision log_entry->revision-1 to the copyfrom-revision
           * we must end up at the copyfrom-path. */
          SVN_ERR(check_ancestry(&related, session_url, repos_root_url,
                                 deleted_path, log_entry->revision,
                                 copy->copyfrom_path,
                                 copy->copyfrom_rev,
                                 b->ctx, iterpool));
          if (!related)
            continue;

          /* ### TODO:
           * If the node was not copied from the most recent last-changed
           * revision of the deleted node, this is not a move but a
           * "copy from the past + delete". */

          /* Remember details of this move. */
          new_move = svn_repos_move_info_create(
                       apr_pstrdup(result_pool, deleted_path),
                       apr_pstrdup(result_pool, copy->copyto_path),
                       log_entry->revision, copy->copyfrom_rev,
                       NULL, NULL, result_pool);

          /* Link together multiple moves of the same node. */
          prior_move = apr_hash_get(b->moves_by_target_path,
                                    new_move->moved_from_repos_relpath,
                                    APR_HASH_KEY_STRING);
          if (prior_move)
            {
              /* Tracing back history of the delete-half of the new move
               * to the copyfrom-revision of the prior move we must end up
               * at the delete-half of the prior move. */
              SVN_ERR(check_ancestry(&related, session_url, repos_root_url,
                                     new_move->moved_from_repos_relpath,
                                     new_move->revision,
                                     prior_move->moved_from_repos_relpath,
                                     prior_move->copyfrom_rev,
                                     b->ctx, iterpool));
              if (related)
                {
                  prior_move->next = new_move;
                  new_move->prev = prior_move;
                }
            }
          apr_hash_set(b->moves_by_target_path,
                       new_move->moved_to_repos_relpath,
                       APR_HASH_KEY_STRING, new_move);

          /* Add this move to the list of moves in this revision. */
          moves = apr_hash_get(b->moves, &new_move->revision,
                               sizeof(svn_revnum_t));
          if (moves == NULL)
            {
              moves = apr_array_make(result_pool,  1,
                                     sizeof(svn_repos_move_info_t *));
              APR_ARRAY_PUSH(moves, svn_repos_move_info_t *) = new_move;
              apr_hash_set(b->moves, &new_move->revision, sizeof(svn_revnum_t),
                           moves);
            }
          else
            APR_ARRAY_PUSH(moves, svn_repos_move_info_t *) = new_move;
        }
    }
  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_client__get_repos_moves(apr_hash_t **moves,
                            const char *anchor_abspath,
                            svn_ra_session_t *ra_session,
                            svn_revnum_t start,
                            svn_revnum_t end,
                            svn_client_ctx_t *ctx,
                            apr_pool_t *result_pool,
                            apr_pool_t *scratch_pool)
{
  struct scan_moves_log_receiver_baton lrb;

  /*lrb.anchor_abspath = anchor_abspath;*/
  lrb.ctx = ctx;
  lrb.moves = apr_hash_make(result_pool);
  lrb.start = start;
  lrb.end = end;
  lrb.ra_session = ra_session;
  lrb.moves_by_target_path = apr_hash_make(scratch_pool);

  if (ctx->notify_func2)
    {
#if 0
      svn_wc_notify_t *notify;
      notify = svn_wc_create_notify(b->anchor_abspath,
                                    svn_wc_notify_moves_scan_log_start,
                                    scratch_pool);
      notify->moves_scan_log_start_rev = start;
      notify->moves_scan_log_end_rev = end;
      notify->moves_scan_log_current_rev = start;
      ctx->notify_func2(b->ctx->notify_baton2, notify, scratch_pool);
#endif
    }

  SVN_ERR(svn_ra_get_log2(ra_session, NULL, start, end, 0, TRUE, FALSE,
                          FALSE, apr_array_make(scratch_pool, 0,
                                                sizeof(const char *)),
                          scan_moves_log_receiver, &lrb, scratch_pool));

  if (ctx->notify_func2)
    {
#if 0
      svn_wc_notify_t *notify;
      notify = svn_wc_create_notify(b->anchor_abspath,
                                    svn_wc_notify_moves_scan_log_done,
                                    scratch_pool);
      notify->moves_scan_log_start_rev = start;
      notify->moves_scan_log_end_rev = end;
      notify->moves_scan_log_current_rev = end;
      b->ctx->notify_func2(b->ctx->notify_baton2, notify, scratch_pool);
#endif
    }

  if (moves)
    *moves = lrb.moves;

  return SVN_NO_ERROR;
}

