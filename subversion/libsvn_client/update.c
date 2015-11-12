/*
 * update.c:  wrappers around wc update functionality
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
#include "client.h"

#include "svn_private_config.h"
#include "private/svn_wc_private.h"
#include "private/svn_ra_private.h"

/* Implements svn_wc_dirents_func_t for update and switch handling. Assumes
   a struct svn_client__dirent_fetcher_baton_t * baton */
svn_error_t *
svn_client__dirent_fetcher(void *baton,
                           apr_hash_t **dirents,
                           const char *repos_root_url,
                           const char *repos_relpath,
                           apr_pool_t *result_pool,
                           apr_pool_t *scratch_pool)
{
  struct svn_client__dirent_fetcher_baton_t *dfb = baton;
  const char *old_url = NULL;
  const char *session_relpath;
  svn_node_kind_t kind;
  const char *url;

  url = svn_path_url_add_component2(repos_root_url, repos_relpath,
                                    scratch_pool);

  if (!svn_uri__is_ancestor(dfb->anchor_url, url))
    {
      SVN_ERR(svn_client__ensure_ra_session_url(&old_url, dfb->ra_session,
                                                url, scratch_pool));
      session_relpath = "";
    }
  else
    SVN_ERR(svn_ra_get_path_relative_to_session(dfb->ra_session,
                                                &session_relpath, url,
                                                scratch_pool));

  /* Is session_relpath still a directory? */
  SVN_ERR(svn_ra_check_path(dfb->ra_session, session_relpath,
                            dfb->target_revision, &kind, scratch_pool));

  if (kind == svn_node_dir)
    SVN_ERR(svn_ra_get_dir2(dfb->ra_session, dirents, NULL, NULL,
                            session_relpath, dfb->target_revision,
                            SVN_DIRENT_KIND, result_pool));
  else
    *dirents = NULL;

  if (old_url)
    SVN_ERR(svn_ra_reparent(dfb->ra_session, old_url, scratch_pool));

  return SVN_NO_ERROR;
}


/*** Code. ***/

/* Set *CLEAN_CHECKOUT to FALSE only if LOCAL_ABSPATH is a non-empty
   folder. ANCHOR_ABSPATH is the w/c root and LOCAL_ABSPATH will still
   be considered empty, if it is equal to ANCHOR_ABSPATH and only
   contains the admin sub-folder.
   If the w/c folder already exists but cannot be openend, we return
   "unclean" - just in case. Most likely, the caller will have to bail
   out later due to the same error we got here.
 */
static svn_error_t *
is_empty_wc(svn_boolean_t *clean_checkout,
            const char *local_abspath,
            const char *anchor_abspath,
            apr_pool_t *pool)
{
  apr_dir_t *dir;
  apr_finfo_t finfo;
  svn_error_t *err;

  /* "clean" until found dirty */
  *clean_checkout = TRUE;

  /* open directory. If it does not exist, yet, a clean one will
     be created by the caller. */
  err = svn_io_dir_open(&dir, local_abspath, pool);
  if (err)
    {
      if (! APR_STATUS_IS_ENOENT(err->apr_err))
        *clean_checkout = FALSE;

      svn_error_clear(err);
      return SVN_NO_ERROR;
    }

  for (err = svn_io_dir_read(&finfo, APR_FINFO_NAME, dir, pool);
       err == SVN_NO_ERROR;
       err = svn_io_dir_read(&finfo, APR_FINFO_NAME, dir, pool))
    {
      /* Ignore entries for this dir and its parent, robustly.
         (APR promises that they'll come first, so technically
         this guard could be moved outside the loop.  But Ryan Bloom
         says he doesn't believe it, and I believe him. */
      if (! (finfo.name[0] == '.'
             && (finfo.name[1] == '\0'
                 || (finfo.name[1] == '.' && finfo.name[2] == '\0'))))
        {
          if (   ! svn_wc_is_adm_dir(finfo.name, pool)
              || strcmp(local_abspath, anchor_abspath) != 0)
            {
              *clean_checkout = FALSE;
              break;
            }
        }
    }

  if (err)
    {
      if (! APR_STATUS_IS_ENOENT(err->apr_err))
        {
          /* There was some issue reading the folder content.
           * We better disable optimizations in that case. */
          *clean_checkout = FALSE;
        }

      svn_error_clear(err);
    }

  return svn_io_dir_close(dir);
}

/* A conflict callback that simply records the conflicted path in BATON.

   Implements svn_wc_conflict_resolver_func2_t.
*/
static svn_error_t *
record_conflict(svn_wc_conflict_result_t **result,
                const svn_wc_conflict_description2_t *description,
                void *baton,
                apr_pool_t *result_pool,
                apr_pool_t *scratch_pool)
{
  apr_hash_t *conflicted_paths = baton;

  svn_hash_sets(conflicted_paths,
                apr_pstrdup(apr_hash_pool_get(conflicted_paths),
                            description->local_abspath), "");
  *result = svn_wc_create_conflict_result(svn_wc_conflict_choose_postpone,
                                          NULL, result_pool);
  return SVN_NO_ERROR;
}


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

struct scan_moves_log_receiver_baton {
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

struct copy_info {
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
  SVN_ERR(svn_client__open_ra_session_internal(&ra_session2, NULL,
                                               session_url, NULL,
                                               NULL, FALSE, FALSE,
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

/* This is a helper for svn_client__update_internal(), which see for
   an explanation of most of these parameters.  Some stuff that's
   unique is as follows:

   ANCHOR_ABSPATH is the local absolute path of the update anchor.
   This is typically either the same as LOCAL_ABSPATH, or the
   immediate parent of LOCAL_ABSPATH.

   If NOTIFY_SUMMARY is set (and there's a notification handler in
   CTX), transmit the final update summary upon successful
   completion of the update.

   Add the paths of any conflict victims to CONFLICTED_PATHS, if that
   is not null.

   Use RA_SESSION_P to run the update if it is not NULL.  If it is then
   open a new ra session and place it in RA_SESSION_P.  This allows
   repeated calls to update_internal to reuse the same session.
*/
static svn_error_t *
update_internal(svn_revnum_t *result_rev,
                svn_boolean_t *timestamp_sleep,
                apr_hash_t *conflicted_paths,
                svn_ra_session_t **ra_session_p,
                const char *local_abspath,
                const char *anchor_abspath,
                const svn_opt_revision_t *revision,
                svn_depth_t depth,
                svn_boolean_t depth_is_sticky,
                svn_boolean_t ignore_externals,
                svn_boolean_t allow_unver_obstructions,
                svn_boolean_t adds_as_modification,
                svn_boolean_t notify_summary,
                svn_client_ctx_t *ctx,
                apr_pool_t *result_pool,
                apr_pool_t *scratch_pool)
{
#if 0
  svn_branch__compat_update_editor3_t *update_editor;
#else
  const svn_delta_editor_t *update_editor;
  void *update_edit_baton;
#endif
  const svn_ra_reporter3_t *reporter;
  void *report_baton;
  const char *corrected_url;
  const char *target;
  const char *repos_root_url;
  const char *repos_relpath;
  const char *repos_uuid;
  const char *anchor_url;
  svn_revnum_t revnum;
  svn_boolean_t use_commit_times;
  svn_boolean_t clean_checkout = FALSE;
  const char *diff3_cmd;
  apr_hash_t *wcroot_iprops;
  svn_opt_revision_t opt_rev;
  svn_ra_session_t *ra_session = *ra_session_p;
  const char *preserved_exts_str;
  apr_array_header_t *preserved_exts;
  struct svn_client__dirent_fetcher_baton_t dfb;
  svn_boolean_t server_supports_depth;
  svn_boolean_t cropping_target;
  svn_boolean_t target_conflicted = FALSE;
  svn_config_t *cfg = ctx->config
                      ? svn_hash_gets(ctx->config, SVN_CONFIG_CATEGORY_CONFIG)
                      : NULL;

  if (result_rev)
    *result_rev = SVN_INVALID_REVNUM;

  /* An unknown depth can't be sticky. */
  if (depth == svn_depth_unknown)
    depth_is_sticky = FALSE;

  if (strcmp(local_abspath, anchor_abspath))
    target = svn_dirent_basename(local_abspath, scratch_pool);
  else
    target = "";

  /* Check if our anchor exists in BASE. If it doesn't we can't update. */
  SVN_ERR(svn_wc__node_get_base(NULL, NULL, &repos_relpath, &repos_root_url,
                                &repos_uuid, NULL,
                                ctx->wc_ctx, anchor_abspath,
                                TRUE /* ignore_enoent */,
                                scratch_pool, scratch_pool));

  /* It does not make sense to update conflict victims. */
  if (repos_relpath)
    {
      svn_error_t *err;
      svn_boolean_t text_conflicted, prop_conflicted;

      anchor_url = svn_path_url_add_component2(repos_root_url, repos_relpath,
                                               scratch_pool);

      err = svn_wc_conflicted_p3(&text_conflicted, &prop_conflicted,
                                 NULL,
                                 ctx->wc_ctx, local_abspath, scratch_pool);

      if (err && err->apr_err != SVN_ERR_WC_PATH_NOT_FOUND)
        return svn_error_trace(err);
      svn_error_clear(err);

      /* tree-conflicts are handled by the update editor */
      if (!err && (text_conflicted || prop_conflicted))
        target_conflicted = TRUE;
    }
  else
    anchor_url = NULL;

  if (! anchor_url || target_conflicted)
    {
      if (ctx->notify_func2)
        {
          svn_wc_notify_t *nt;

          nt = svn_wc_create_notify(local_abspath,
                                    target_conflicted
                                      ? svn_wc_notify_skip_conflicted
                                      : svn_wc_notify_update_skip_working_only,
                                    scratch_pool);

          ctx->notify_func2(ctx->notify_baton2, nt, scratch_pool);
        }
      return SVN_NO_ERROR;
    }

  /* We may need to crop the tree if the depth is sticky */
  cropping_target = (depth_is_sticky && depth < svn_depth_infinity);
  if (cropping_target)
    {
      svn_node_kind_t target_kind;

      if (depth == svn_depth_exclude)
        {
          SVN_ERR(svn_wc_exclude(ctx->wc_ctx,
                                 local_abspath,
                                 ctx->cancel_func, ctx->cancel_baton,
                                 ctx->notify_func2, ctx->notify_baton2,
                                 scratch_pool));

          /* Target excluded, we are done now */
          return SVN_NO_ERROR;
        }

      SVN_ERR(svn_wc_read_kind2(&target_kind, ctx->wc_ctx, local_abspath,
                                TRUE, TRUE, scratch_pool));
      if (target_kind == svn_node_dir)
        {
          SVN_ERR(svn_wc_crop_tree2(ctx->wc_ctx, local_abspath, depth,
                                    ctx->cancel_func, ctx->cancel_baton,
                                    ctx->notify_func2, ctx->notify_baton2,
                                    scratch_pool));
        }
    }

  /* check whether the "clean c/o" optimization is applicable */
  SVN_ERR(is_empty_wc(&clean_checkout, local_abspath, anchor_abspath,
                      scratch_pool));

  /* Get the external diff3, if any. */
  svn_config_get(cfg, &diff3_cmd, SVN_CONFIG_SECTION_HELPERS,
                 SVN_CONFIG_OPTION_DIFF3_CMD, NULL);

  if (diff3_cmd != NULL)
    SVN_ERR(svn_path_cstring_to_utf8(&diff3_cmd, diff3_cmd, scratch_pool));

  /* See if the user wants last-commit timestamps instead of current ones. */
  SVN_ERR(svn_config_get_bool(cfg, &use_commit_times,
                              SVN_CONFIG_SECTION_MISCELLANY,
                              SVN_CONFIG_OPTION_USE_COMMIT_TIMES, FALSE));

  /* See which files the user wants to preserve the extension of when
     conflict files are made. */
  svn_config_get(cfg, &preserved_exts_str, SVN_CONFIG_SECTION_MISCELLANY,
                 SVN_CONFIG_OPTION_PRESERVED_CF_EXTS, "");
  preserved_exts = *preserved_exts_str
    ? svn_cstring_split(preserved_exts_str, "\n\r\t\v ", FALSE, scratch_pool)
    : NULL;

  /* Let everyone know we're starting a real update (unless we're
     asked not to). */
  if (ctx->notify_func2 && notify_summary)
    {
      svn_wc_notify_t *notify
        = svn_wc_create_notify(local_abspath, svn_wc_notify_update_started,
                               scratch_pool);
      notify->kind = svn_node_none;
      notify->content_state = notify->prop_state
        = svn_wc_notify_state_inapplicable;
      notify->lock_state = svn_wc_notify_lock_state_inapplicable;
      ctx->notify_func2(ctx->notify_baton2, notify, scratch_pool);
    }

  /* Try to reuse the RA session by reparenting it to the anchor_url.
   * This code is probably overly cautious since we only use this
   * currently when parents are missing and so all the anchor_urls
   * have to be in the same repo. */
  if (ra_session)
    {
      svn_error_t *err = svn_ra_reparent(ra_session, anchor_url, scratch_pool);
      if (err)
        {
          if (err->apr_err == SVN_ERR_RA_ILLEGAL_URL)
            {
            /* session changed repos, can't reuse it */
              svn_error_clear(err);
              ra_session = NULL;
            }
          else
            {
              return svn_error_trace(err);
            }
        }
      else
        {
          corrected_url = NULL;
        }
    }

  /* Open an RA session for the URL if one isn't already available */
  if (!ra_session)
    {
      SVN_ERR(svn_client__open_ra_session_internal(&ra_session, &corrected_url,
                                                   anchor_url,
                                                   anchor_abspath, NULL,
                                                   TRUE /* write_dav_props */,
                                                   TRUE /* read_dav_props */,
                                                   ctx,
                                                   result_pool, scratch_pool));
      *ra_session_p = ra_session;
    }

  /* If we got a corrected URL from the RA subsystem, we'll need to
     relocate our working copy first. */
  if (corrected_url)
    {
      const char *new_repos_root_url;

      /* To relocate everything inside our repository we need the old and new
         repos root. */
      SVN_ERR(svn_ra_get_repos_root2(ra_session, &new_repos_root_url,
                                     scratch_pool));

      /* svn_client_relocate2() will check the uuid */
      SVN_ERR(svn_client_relocate2(anchor_abspath, repos_root_url,
                                   new_repos_root_url, ignore_externals,
                                   ctx, scratch_pool));

      /* Store updated repository root for externals */
      repos_root_url = new_repos_root_url;
      /* ### We should update anchor_loc->repos_uuid too, although currently
       * we don't use it. */
      anchor_url = corrected_url;
    }

  /* Resolve unspecified REVISION now, because we need to retrieve the
     correct inherited props prior to the editor drive and we need to
     use the same value of HEAD for both. */
  opt_rev.kind = revision->kind;
  opt_rev.value = revision->value;
  if (opt_rev.kind == svn_opt_revision_unspecified)
    opt_rev.kind = svn_opt_revision_head;

  /* ### todo: shouldn't svn_client__get_revision_number be able
     to take a URL as easily as a local path?  */
  SVN_ERR(svn_client__get_revision_number(&revnum, NULL, ctx->wc_ctx,
                                          local_abspath, ra_session, &opt_rev,
                                          scratch_pool));

  SVN_ERR(svn_ra_has_capability(ra_session, &server_supports_depth,
                                SVN_RA_CAPABILITY_DEPTH, scratch_pool));

  dfb.ra_session = ra_session;
  dfb.target_revision = revnum;
  dfb.anchor_url = anchor_url;

  SVN_ERR(svn_client__get_inheritable_props(&wcroot_iprops, local_abspath,
                                            revnum, depth, ra_session,
                                            ctx, scratch_pool, scratch_pool));

#if 0
  /* Fetch the update editor.  If REVISION is invalid, that's okay;
     the RA driver will call editor->set_target_revision later on. */
  SVN_ERR(svn_wc__get_update_editor_ev3(&update_editor,
                                    &revnum, ctx->wc_ctx, anchor_abspath,
                                    target, wcroot_iprops, use_commit_times,
                                    depth, depth_is_sticky,
                                    allow_unver_obstructions,
                                    adds_as_modification,
                                    server_supports_depth,
                                    clean_checkout,
                                    diff3_cmd, preserved_exts,
                                    svn_client__dirent_fetcher, &dfb,
                                    conflicted_paths ? record_conflict : NULL,
                                    conflicted_paths,
                                    NULL, NULL,
                                    ctx->cancel_func, ctx->cancel_baton,
                                    ctx->notify_func2, ctx->notify_baton2,
                                    pool, pool));
  SVN_ERR(svn_editor3__get_debug_editor(&update_editor->editor,
                                        update_editor->editor,
                                        pool));

  /* Tell RA to do an update of URL+TARGET to REVISION; if we pass an
     invalid revnum, that means RA will use the latest revision.  */
  SVN_ERR(svn_ra_do_update4(ra_session, &reporter, &report_baton,
                            revnum, target,
                            (!server_supports_depth || depth_is_sticky
                             ? depth
                             : svn_depth_unknown),
                            FALSE /* send_copyfrom_args */,
                            FALSE /* ignore_ancestry */,
                            update_editor, pool, pool));
#else
  /* Fetch the update editor.  If REVISION is invalid, that's okay;
     the RA driver will call editor->set_target_revision later on. */
  SVN_ERR(svn_wc__get_update_editor(&update_editor, &update_edit_baton,
                                    &revnum, ctx->wc_ctx, anchor_abspath,
                                    target, wcroot_iprops, use_commit_times,
                                    depth, depth_is_sticky,
                                    allow_unver_obstructions,
                                    adds_as_modification,
                                    server_supports_depth,
                                    clean_checkout,
                                    diff3_cmd, preserved_exts,
                                    svn_client__dirent_fetcher, &dfb,
                                    conflicted_paths ? record_conflict : NULL,
                                    conflicted_paths,
                                    NULL, NULL,
                                    ctx->cancel_func, ctx->cancel_baton,
                                    ctx->notify_func2, ctx->notify_baton2,
                                    scratch_pool, scratch_pool));

  /* Tell RA to do an update of URL+TARGET to REVISION; if we pass an
     invalid revnum, that means RA will use the latest revision.  */
  SVN_ERR(svn_ra_do_update3(ra_session, &reporter, &report_baton,
                            revnum, target,
                            (!server_supports_depth || depth_is_sticky
                             ? depth
                             : svn_depth_unknown),
                            FALSE /* send_copyfrom_args */,
                            FALSE /* ignore_ancestry */,
                            update_editor, update_edit_baton,
                            scratch_pool, scratch_pool));
#endif

  /* Past this point, we assume the WC is going to be modified so we will
   * need to sleep for timestamps. */
  *timestamp_sleep = TRUE;

  /* Drive the reporter structure, describing the revisions within
     LOCAL_ABSPATH.  When this calls reporter->finish_report, the
     reporter will drive the update_editor. */
  SVN_ERR(svn_wc_crawl_revisions5(ctx->wc_ctx, local_abspath, reporter,
                                  report_baton, TRUE,
                                  depth, (! depth_is_sticky),
                                  (! server_supports_depth),
                                  use_commit_times,
                                  ctx->cancel_func, ctx->cancel_baton,
                                  ctx->notify_func2, ctx->notify_baton2,
                                  scratch_pool));

  /* We handle externals after the update is complete, so that
     handling external items (and any errors therefrom) doesn't delay
     the primary operation.  */
  if ((SVN_DEPTH_IS_RECURSIVE(depth) || cropping_target)
      && (! ignore_externals))
    {
      apr_hash_t *new_externals;
      apr_hash_t *new_depths;
      SVN_ERR(svn_wc__externals_gather_definitions(&new_externals,
                                                   &new_depths,
                                                   ctx->wc_ctx, local_abspath,
                                                   depth,
                                                   scratch_pool, scratch_pool));

      SVN_ERR(svn_client__handle_externals(new_externals,
                                           new_depths,
                                           repos_root_url, local_abspath,
                                           depth, timestamp_sleep, ra_session,
                                           ctx, scratch_pool));
    }

  /* Let everyone know we're finished here (unless we're asked not to). */
  if (ctx->notify_func2 && notify_summary)
    {
      svn_wc_notify_t *notify
        = svn_wc_create_notify(local_abspath, svn_wc_notify_update_completed,
                               scratch_pool);
      notify->kind = svn_node_none;
      notify->content_state = notify->prop_state
        = svn_wc_notify_state_inapplicable;
      notify->lock_state = svn_wc_notify_lock_state_inapplicable;
      notify->revision = revnum;
      ctx->notify_func2(ctx->notify_baton2, notify, scratch_pool);
    }

  /* If the caller wants the result revision, give it to them. */
  if (result_rev)
    *result_rev = revnum;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_client__update_internal(svn_revnum_t *result_rev,
                            svn_boolean_t *timestamp_sleep,
                            const char *local_abspath,
                            const svn_opt_revision_t *revision,
                            svn_depth_t depth,
                            svn_boolean_t depth_is_sticky,
                            svn_boolean_t ignore_externals,
                            svn_boolean_t allow_unver_obstructions,
                            svn_boolean_t adds_as_modification,
                            svn_boolean_t make_parents,
                            svn_boolean_t innerupdate,
                            svn_ra_session_t *ra_session,
                            svn_client_ctx_t *ctx,
                            apr_pool_t *pool)
{
  const char *anchor_abspath, *lockroot_abspath;
  svn_error_t *err;
  svn_opt_revision_t peg_revision = *revision;
  apr_hash_t *conflicted_paths
    = ctx->conflict_func2 ? apr_hash_make(pool) : NULL;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));
  SVN_ERR_ASSERT(! (innerupdate && make_parents));

  if (make_parents)
    {
      int i;
      const char *parent_abspath = local_abspath;
      apr_array_header_t *missing_parents =
        apr_array_make(pool, 4, sizeof(const char *));
      apr_pool_t *iterpool;

      iterpool = svn_pool_create(pool);

      while (1)
        {
          svn_pool_clear(iterpool);

          /* Try to lock.  If we can't lock because our target (or its
             parent) isn't a working copy, we'll try to walk up the
             tree to find a working copy, remembering this path's
             parent as one we need to flesh out.  */
          err = svn_wc__acquire_write_lock(&lockroot_abspath, ctx->wc_ctx,
                                           parent_abspath, !innerupdate,
                                           pool, iterpool);
          if (!err)
            break;
          if ((err->apr_err != SVN_ERR_WC_NOT_WORKING_COPY)
              || svn_dirent_is_root(parent_abspath, strlen(parent_abspath)))
            return err;
          svn_error_clear(err);

          /* Remember the parent of our update target as a missing
             parent. */
          parent_abspath = svn_dirent_dirname(parent_abspath, pool);
          APR_ARRAY_PUSH(missing_parents, const char *) = parent_abspath;
        }

      /* Run 'svn up --depth=empty' (effectively) on the missing
         parents, if any. */
      anchor_abspath = lockroot_abspath;
      for (i = missing_parents->nelts - 1; i >= 0; i--)
        {
          const char *missing_parent =
            APR_ARRAY_IDX(missing_parents, i, const char *);

          svn_pool_clear(iterpool);

          err = update_internal(result_rev, timestamp_sleep, conflicted_paths,
                                &ra_session, missing_parent,
                                anchor_abspath, &peg_revision, svn_depth_empty,
                                FALSE, ignore_externals,
                                allow_unver_obstructions, adds_as_modification,
                                FALSE, ctx, pool, iterpool);
          if (err)
            goto cleanup;
          anchor_abspath = missing_parent;

          /* If we successfully updated a missing parent, let's re-use
             the returned revision number for future updates for the
             sake of consistency. */
          peg_revision.kind = svn_opt_revision_number;
          peg_revision.value.number = *result_rev;
        }

      svn_pool_destroy(iterpool);
    }
  else
    {
      SVN_ERR(svn_wc__acquire_write_lock(&lockroot_abspath, ctx->wc_ctx,
                                         local_abspath, !innerupdate,
                                         pool, pool));
      anchor_abspath = lockroot_abspath;
    }

  err = update_internal(result_rev, timestamp_sleep, conflicted_paths,
                        &ra_session,
                        local_abspath, anchor_abspath,
                        &peg_revision, depth, depth_is_sticky,
                        ignore_externals, allow_unver_obstructions,
                        adds_as_modification,
                        TRUE, ctx, pool, pool);

  /* Give the conflict resolver callback the opportunity to
   * resolve any conflicts that were raised. */
  if (! err && ctx->conflict_func2 && apr_hash_count(conflicted_paths))
    {
      err = svn_client__resolve_conflicts(NULL, conflicted_paths, ctx, pool);
    }

 cleanup:
  err = svn_error_compose_create(
            err,
            svn_wc__release_write_lock(ctx->wc_ctx, lockroot_abspath, pool));

  return svn_error_trace(err);
}


svn_error_t *
svn_client_update4(apr_array_header_t **result_revs,
                   const apr_array_header_t *paths,
                   const svn_opt_revision_t *revision,
                   svn_depth_t depth,
                   svn_boolean_t depth_is_sticky,
                   svn_boolean_t ignore_externals,
                   svn_boolean_t allow_unver_obstructions,
                   svn_boolean_t adds_as_modification,
                   svn_boolean_t make_parents,
                   svn_client_ctx_t *ctx,
                   apr_pool_t *pool)
{
  int i;
  apr_pool_t *iterpool = svn_pool_create(pool);
  const char *path = NULL;
  svn_boolean_t sleep = FALSE;
  svn_error_t *err = SVN_NO_ERROR;
  svn_boolean_t found_valid_target = FALSE;

  if (result_revs)
    *result_revs = apr_array_make(pool, paths->nelts, sizeof(svn_revnum_t));

  for (i = 0; i < paths->nelts; ++i)
    {
      path = APR_ARRAY_IDX(paths, i, const char *);

      if (svn_path_is_url(path))
        return svn_error_createf(SVN_ERR_ILLEGAL_TARGET, NULL,
                                 _("'%s' is not a local path"), path);
    }

  for (i = 0; i < paths->nelts; ++i)
    {
      svn_revnum_t result_rev;
      const char *local_abspath;
      path = APR_ARRAY_IDX(paths, i, const char *);

      svn_pool_clear(iterpool);

      if (ctx->cancel_func)
        {
          err = ctx->cancel_func(ctx->cancel_baton);
          if (err)
            goto cleanup;
        }

      err = svn_dirent_get_absolute(&local_abspath, path, iterpool);
      if (err)
        goto cleanup;
      err = svn_client__update_internal(&result_rev, &sleep, local_abspath,
                                        revision, depth, depth_is_sticky,
                                        ignore_externals,
                                        allow_unver_obstructions,
                                        adds_as_modification,
                                        make_parents,
                                        FALSE, NULL, ctx,
                                        iterpool);

      if (err)
        {
          if (err->apr_err != SVN_ERR_WC_NOT_WORKING_COPY)
            goto cleanup;

          svn_error_clear(err);
          err = SVN_NO_ERROR;

          /* SVN_ERR_WC_NOT_WORKING_COPY: it's not versioned */

          result_rev = SVN_INVALID_REVNUM;
          if (ctx->notify_func2)
            {
              svn_wc_notify_t *notify;
              notify = svn_wc_create_notify(path,
                                            svn_wc_notify_skip,
                                            iterpool);
              ctx->notify_func2(ctx->notify_baton2, notify, iterpool);
            }
        }
      else
        found_valid_target = TRUE;

      if (result_revs)
        APR_ARRAY_PUSH(*result_revs, svn_revnum_t) = result_rev;
    }
  svn_pool_destroy(iterpool);

 cleanup:
  if (!err && !found_valid_target)
    return svn_error_create(SVN_ERR_WC_NOT_WORKING_COPY, NULL,
                            _("None of the targets are working copies"));
  if (sleep)
    {
      const char *wcroot_abspath;

      if (paths->nelts == 1)
        {
          const char *abspath;

          /* PATH iteslf may have been removed by the update. */
          SVN_ERR(svn_dirent_get_absolute(&abspath, path, pool));
          SVN_ERR(svn_wc__get_wcroot(&wcroot_abspath, ctx->wc_ctx, abspath,
                                     pool, pool));
        }
      else
        wcroot_abspath = NULL;

      svn_io_sleep_for_timestamps(wcroot_abspath, pool);
    }

  return svn_error_trace(err);
}
