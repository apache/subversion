/*
 * copy.c:  copy/move wrappers around wc 'copy' functionality.
 *
 * ====================================================================
 * Copyright (c) 2000-2006 CollabNet.  All rights reserved.
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

/* ==================================================================== */



/*** Includes. ***/

#include <string.h>
#include <assert.h>
#include "svn_wc.h"
#include "svn_client.h"
#include "svn_error.h"
#include "svn_path.h"
#include "svn_opt.h"
#include "svn_time.h"
#include "svn_pools.h"

#include "client.h"

#include "svn_private_config.h"


/*** Code. ***/

/*
 * for each source/destination pair
 *   if (not exist src_path)
 *     return ERR_BAD_SRC error
 *
 *   if (exist dst_path)
 *     return ERR_OBSTRUCTION error
 *   else
 *     copy src_path into parent_of_dst_path as basename (dst_path)
 *
 *   if (this is a move)
 *     delete src_path
 */


/* Find the longest common ancestor for all the SRCs and DSTs in COPY_PAIRS.
   If SRC_ANCESTOR or DST_ANCESTOR is NULL, nothing will be returned in it.
   COMMON_ANCESTOR will be the common ancestor of both the SRC_ANCESTOR and
   DST_ANCESTOR, and will only be set if it is not NULL.
 */
static svn_error_t *
get_copy_pair_ancestors(const apr_array_header_t *copy_pairs,
                        const char **src_ancestor,
                        const char **dst_ancestor,
                        const char **common_ancestor,
                        apr_pool_t *pool)
{
  apr_pool_t *subpool = svn_pool_create(pool);
  const char *top_dst;
  char *top_src;
  int i;

  top_src = apr_pstrdup(subpool,
              ((svn_client__copy_pair_t **) (copy_pairs->elts))[0]->src);

  /* Because all the destinations are in the same directory, we can easily
     determine their common ancestor. */
  if (copy_pairs->nelts == 1)
    top_dst = apr_pstrdup(subpool,
                ((svn_client__copy_pair_t **) (copy_pairs->elts))[0]->dst);
  else
    top_dst = svn_path_dirname(
                ((svn_client__copy_pair_t **) (copy_pairs->elts))[0]->dst,
                subpool);

  /* We don't need to clear the subpool here for several reasons:
     1)  If we do, we can't use it to allocate the initial versions of 
         top_src and top_dst (above).
     2)  We don't return any errors in the following loop, so we are guanteed
         to destory the subpool at the end of this function.
     3)  The number of iterations is likely to be few, and the loop will be
         through quickly, so memory leakage will not be significant, in time or
         space.  */
  for (i = 1; i < copy_pairs->nelts; i++)
    {
      const svn_client__copy_pair_t *pair =
        ((svn_client__copy_pair_t **) (copy_pairs->elts))[i];

      top_src = svn_path_get_longest_ancestor(top_src, pair->src, subpool);
    }

  if (src_ancestor)
    *src_ancestor = apr_pstrdup(pool, top_src);

  if (dst_ancestor)
    *dst_ancestor = apr_pstrdup(pool, top_dst);

  if (common_ancestor)
    *common_ancestor = svn_path_get_longest_ancestor(top_src, top_dst, pool);

  svn_pool_destroy(subpool);

  return SVN_NO_ERROR;
}


/* Copy SRC_PATH into DST_PATH as DST_BASENAME, deleting SRC_PATH
   afterwards if IS_MOVE is TRUE.  Use POOL for all necessary
   allocations.
*/
static svn_error_t *
wc_to_wc_copy_single(svn_client__copy_pair_t *pair,
                     svn_boolean_t is_move,
                     svn_client_ctx_t *ctx,
                     apr_pool_t *pool)
{
  svn_wc_adm_access_t *adm_access, *src_access;
  svn_error_t *err;

  if (is_move)
    {
      const char *src_parent;

      svn_path_split(pair->src, &src_parent, NULL, pool);

      SVN_ERR(svn_wc_adm_open3(&src_access, NULL, src_parent, TRUE,
                               pair->src_kind == svn_node_dir ? -1 : 0,
                               ctx->cancel_func, ctx->cancel_baton, pool));

      /* Need to avoid attempting to open the same dir twice when source
         and destination overlap. */
      if (strcmp(src_parent, pair->dst_parent) == 0)
        {
          adm_access = src_access;
        }
      else 
        {
          const char *src_parent_abs, *dst_parent_abs;

          SVN_ERR(svn_path_get_absolute(&src_parent_abs, src_parent, pool));
          SVN_ERR(svn_path_get_absolute(&dst_parent_abs, pair->dst_parent,
                                        pool));

          if ((pair->src_kind == svn_node_dir)
              && (svn_path_is_child(src_parent_abs, dst_parent_abs, pool)))
            {
              SVN_ERR(svn_wc_adm_retrieve(&adm_access, src_access,
                                          pair->dst_parent, pool));
            }
          else
            {
              SVN_ERR(svn_wc_adm_open3(&adm_access, NULL, pair->dst_parent,
                                       TRUE, 0, ctx->cancel_func,
                                       ctx->cancel_baton,pool));
            }
        }

    }
  else 
    {
      SVN_ERR(svn_wc_adm_open3(&adm_access, NULL, pair->dst_parent, TRUE, 0,
                               ctx->cancel_func, ctx->cancel_baton, pool));
    }
                              
  /* Perform the copy and (optionally) delete. */

  /* ### If this is not a move, we won't have locked the source, so we
     ### won't detect any outstanding locks. If the source is locked and
     ### requires cleanup should we abort the copy? */

  err = svn_wc_copy2(pair->src, adm_access, pair->base_name,
                     ctx->cancel_func, ctx->cancel_baton,
                     ctx->notify_func2, ctx->notify_baton2, pool);
  svn_sleep_for_timestamps();
  SVN_ERR(err);


  if (is_move)
    {
      SVN_ERR(svn_wc_delete2(pair->src, src_access,
                             ctx->cancel_func, ctx->cancel_baton,
                             ctx->notify_func2, ctx->notify_baton2, pool));

      if (adm_access != src_access)
        SVN_ERR(svn_wc_adm_close(adm_access));
      SVN_ERR(svn_wc_adm_close(src_access));
    }
  else
    {
      SVN_ERR(svn_wc_adm_close(adm_access));
    }

  return SVN_NO_ERROR;
}


static svn_error_t *
wc_to_wc_copy(const apr_array_header_t *copy_pairs,
              svn_boolean_t is_move,
              svn_client_ctx_t *ctx,
              apr_pool_t *pool)
{
  int i;
  apr_pool_t *subpool = svn_pool_create(pool);

  /* Check that all of our SRCs exist, and all the DSTs don't. */
  for (i = 0; i < copy_pairs->nelts; i++)
    {
      svn_client__copy_pair_t *pair = 
        ((svn_client__copy_pair_t **) (copy_pairs->elts))[i];
      svn_node_kind_t dst_kind, dst_parent_kind;

      svn_pool_clear(subpool);

      /* Verify that SRC_PATH exists. */
      SVN_ERR(svn_io_check_path(pair->src, &pair->src_kind, pool));
      if (pair->src_kind == svn_node_none)
        return svn_error_createf(SVN_ERR_NODE_UNKNOWN_KIND, NULL,
                                 _("Path '%s' does not exist"),
                                 svn_path_local_style(pair->src, pool));

      /* If DST_PATH does not exist, then its basename will become a new
         file or dir added to its parent (possibly an implicit '.').
         Else, just error out. */
      SVN_ERR(svn_io_check_path(pair->dst, &dst_kind, pool));
      if (dst_kind != svn_node_none)
        return svn_error_createf(SVN_ERR_ENTRY_EXISTS, NULL,
                                 _("Path '%s' already exists"),
                                 svn_path_local_style(pair->dst, pool));

      svn_path_split(pair->dst, &pair->dst_parent, &pair->base_name, pool);

      /* Make sure the destination parent is a directory and produce a clear
         error message if it is not. */
      SVN_ERR(svn_io_check_path(pair->dst_parent, &dst_parent_kind, pool));
      if (dst_parent_kind != svn_node_dir)
        return svn_error_createf(SVN_ERR_WC_NOT_DIRECTORY, NULL,
                                 _("Path '%s' is not a directory"),
                                 svn_path_local_style(pair->dst_parent, pool));
    }

  for ( i = 0; i < copy_pairs->nelts; i++)
    {
      svn_client__copy_pair_t *pair = 
        ((svn_client__copy_pair_t **) (copy_pairs->elts))[i];
      svn_pool_clear(subpool);
      SVN_ERR(wc_to_wc_copy_single(pair, is_move, ctx, subpool));
    }

  svn_pool_destroy(subpool);

  return SVN_NO_ERROR;
}


typedef struct {
  const char *src_url;
  const char *src_path;
  const char *dst_path;
  svn_node_kind_t src_kind;
  svn_boolean_t resurrection;
} path_driver_info;


struct path_driver_cb_baton
{
  const svn_delta_editor_t *editor;
  void *edit_baton;
  apr_hash_t *action_hash;
  svn_revnum_t src_revnum;
  svn_boolean_t is_move;
};


static svn_error_t *
path_driver_cb_func(void **dir_baton,
                    void *parent_baton,
                    void *callback_baton,
                    const char *path,
                    apr_pool_t *pool)
{
  struct path_driver_cb_baton *cb_baton = callback_baton;
  svn_boolean_t do_delete = FALSE, do_add = FALSE;
  path_driver_info *path_info = apr_hash_get(cb_baton->action_hash,
                                             path,
                                             APR_HASH_KEY_STRING);

  /* Initialize return value. */
  *dir_baton = NULL;

  /* This function should never get an empty PATH.  We can neither
     create nor delete the empty PATH, so if someone is calling us
     with such, the code is just plain wrong. */
  assert(! svn_path_is_empty(path));

  /* If this is a resurrection, we know the source and dest paths are
     the same, and that our driver will only be calling us once.  */
  if (path_info->resurrection)
    {
      /* If this is a move, we do nothing.  Otherwise, we do the copy.  */
      if (! cb_baton->is_move)
        do_add = TRUE;
    }
  /* Not a resurrection. */
  else 
    {
      /* If this is a move, we check PATH to see if it is the source
         or the destination of the move. */
      if (cb_baton->is_move)
        {
          if (strcmp(path_info->src_path, path) == 0)
            do_delete = TRUE;
          else
            do_add = TRUE;
        }
      /* Not a move?  This must just be the copy addition. */
      else
        {
          do_add = TRUE;
        }
    }

  if (do_delete)
    {
      SVN_ERR(cb_baton->editor->delete_entry(path, SVN_INVALID_REVNUM,
                                             parent_baton, pool));
    }
  if (do_add)
    {
      SVN_ERR(svn_path_check_valid(path, pool));

      if (path_info->src_kind == svn_node_file)
        {
          void *file_baton;
          SVN_ERR(cb_baton->editor->add_file(path, parent_baton, 
                                             path_info->src_url, 
                                             cb_baton->src_revnum, 
                                             pool, &file_baton));
          SVN_ERR(cb_baton->editor->close_file(file_baton, NULL, pool));
        }
      else
        {
          SVN_ERR(cb_baton->editor->add_directory(path, parent_baton,
                                                  path_info->src_url, 
                                                  cb_baton->src_revnum, 
                                                  pool, dir_baton));
        }
    }
  return SVN_NO_ERROR;
}


static svn_error_t *
repos_to_repos_copy(svn_commit_info_t **commit_info_p,
                    const apr_array_header_t *copy_pairs, 
                    const svn_opt_revision_t *src_revision, 
                    svn_client_ctx_t *ctx,
                    svn_boolean_t is_move,
                    apr_pool_t *pool)
{
  apr_array_header_t *paths = apr_array_make(pool, 2 * copy_pairs->nelts,
                                             sizeof(const char *));
  apr_hash_t *action_hash = apr_hash_make(pool);
  apr_array_header_t *path_infos;
  const char *top_url, *message, *repos_root;
  svn_revnum_t youngest;
  svn_ra_session_t *ra_session;
  const svn_delta_editor_t *editor;
  void *edit_baton;
  void *commit_baton;
  svn_revnum_t src_revnum;
  struct path_driver_cb_baton cb_baton;
  int i;
  svn_error_t *err;

  /* Create a path_info struct for each src/dst pair, and initialize it. */
  path_infos = apr_array_make(pool, copy_pairs->nelts,
                              sizeof(path_driver_info));
  for (i = 0; i < copy_pairs->nelts; i++)
    {
      svn_client__copy_pair_t *pair =
        ((svn_client__copy_pair_t **) (copy_pairs->elts))[i];
      path_driver_info *info = &(((path_driver_info *) (path_infos->elts))[i]);

      info->resurrection = FALSE;
      info->src_url = pair->src;
    }

  /* We have to open our session to the longest path common to all
     SRC_URLS and DST_URLS in the repository so we can do existence
     checks on all paths, and so we can operate on all paths in the
     case of a move. */
  get_copy_pair_ancestors(copy_pairs, NULL, NULL, &top_url, pool);

  /* Check each src/dst pair for resurrection. */
  for (i = 0; i < copy_pairs->nelts; i++)
    {
      svn_client__copy_pair_t *pair =
        ((svn_client__copy_pair_t **) (copy_pairs->elts))[i];
      path_driver_info *info = &(((path_driver_info *) (path_infos->elts))[i]);

      if (strcmp(pair->src, pair->dst) == 0)
        {
          info->resurrection = TRUE;
          
          /* Special edge-case!  (issue #683)  If you're resurrecting a
             deleted item like this:  'svn cp -rN src_URL dst_URL', then
             it's possible for src_URL == dst_URL == top_url.  In this
             situation, we want to open an RA session to be at least the
             *parent* of all three. */
          if (strcmp(pair->src, top_url) == 0)
            {
              top_url = svn_path_dirname(top_url, pool);
            }
        }
    }

  /* Open an RA session for the URL. Note that we don't have a local
     directory, nor a place to put temp files. */
  err = svn_client__open_ra_session_internal(&ra_session, top_url,
                                             NULL, NULL, NULL, FALSE, TRUE, 
                                             ctx, pool);

  /* If the two URLs appear not to be in the same repository, then
     top_url will be empty and the call to svn_ra_open2()
     above will have failed.  Below we check for that, and propagate a
     descriptive error back to the user.
   
     Ideally, we'd contact the repositories and compare their UUIDs to
     determine whether or not src and dst are in the same repository,
     instead of depending on an essentially textual comparison.
     However, it is simpler to assume that if someone is using the
     same repository, then they will use the same hostname/path to
     refer to it both times.  Conversely, if the repositories are
     different, then they can't share a non-empty prefix, so top_url
     would still be "" and svn_ra_get_library() would still error.
     Thus we can get this check without extra network turnarounds to
     fetch the UUIDs.
   */
  if (err)
    {
      if ((err->apr_err == SVN_ERR_RA_ILLEGAL_URL)
          && ((top_url == NULL) || (top_url[0] == '\0')))
        {
          svn_client__copy_pair_t *first_pair =
            ((svn_client__copy_pair_t **) (copy_pairs->elts))[0];
          return svn_error_createf
            (SVN_ERR_UNSUPPORTED_FEATURE, NULL,
             _("Source and dest appear not to be in the same repository "
               "(src: '%s'; dst: '%s')"),
             first_pair->src, first_pair->dst);
        }
      else
        return err;
    }

  SVN_ERR(svn_ra_get_repos_root(ra_session, &repos_root, pool));

  /* For each src/dst pair, check to see if that SRC_URL is a child of
     the DST_URL (excepting the case where DST_URL is the repo root).
     If it is, and the parent of DST_URL is the current TOP_URL, then we
     need to reparent the session one directory higher, the parent of 
     the DST_URL. */
  for (i = 0; i < copy_pairs->nelts; i++ )
    {
      svn_client__copy_pair_t *pair =
        ((svn_client__copy_pair_t **) (copy_pairs->elts))[i];
      path_driver_info *info = &(((path_driver_info *) (path_infos->elts))[i]);

      if (strcmp(pair->dst, repos_root) != 0
          && svn_path_is_child(pair->dst, pair->src, pool) != NULL)
        {
          info->resurrection = TRUE;
          top_url = svn_path_dirname(top_url, pool);

          SVN_ERR(svn_ra_reparent(ra_session, top_url, pool));
        }
    }

  /* Fetch the youngest revision. */
  SVN_ERR(svn_ra_get_latest_revnum(ra_session, &youngest, pool));

  /* Pass NULL for the path, to ensure error if trying to get a
     revision based on the working copy. */
  SVN_ERR(svn_client__get_revision_number
          (&src_revnum, ra_session, src_revision, NULL, pool));

  /* Use YOUNGEST for copyfrom args if not provided. */
  if (! SVN_IS_VALID_REVNUM(src_revnum))
    src_revnum = youngest;
  
  /* Get the portions of the SRC and DST URLs that are relative to
     TOP_URL, and URI-decode those sections. */
  for (i = 0; i < copy_pairs->nelts; i++)
    {
      svn_client__copy_pair_t *pair =
        ((svn_client__copy_pair_t **) (copy_pairs->elts))[i];
      svn_node_kind_t dst_kind;
      const char *src_rel, *dst_rel;
      path_driver_info *info = &(((path_driver_info *) (path_infos->elts))[i]);

      src_rel = svn_path_is_child(top_url, pair->src, pool);
      if (src_rel)
        src_rel = svn_path_uri_decode(src_rel, pool);
      else
        src_rel = "";

      dst_rel = svn_path_is_child(top_url, pair->dst, pool);
      if (dst_rel)
        dst_rel = svn_path_uri_decode(dst_rel, pool);
      else
        dst_rel = "";

      /* We can't move something into itself, period. */
      if (svn_path_is_empty(src_rel) && is_move)
        return svn_error_createf(SVN_ERR_UNSUPPORTED_FEATURE, NULL,
                                 _("Cannot move URL '%s' into itself"),
                                 pair->src);
  
      /* Verify that SRC_URL exists in the repository. */
      SVN_ERR(svn_ra_check_path(ra_session, src_rel, src_revnum, 
                                &info->src_kind, pool));
      if (info->src_kind == svn_node_none)
        return svn_error_createf 
          (SVN_ERR_FS_NOT_FOUND, NULL,
           _("Path '%s' does not exist in revision %ld"),
           pair->src, src_revnum);

      /* Figure out the basename that will result from this operation. */
      SVN_ERR(svn_ra_check_path(ra_session, dst_rel, youngest, &dst_kind, 
                                pool));
      if (dst_kind != svn_node_none)
        {
          /* We disallow the overwriting of existing paths. */
          return svn_error_createf(SVN_ERR_FS_ALREADY_EXISTS, NULL,
                                   _("Path '%s' already exists"), dst_rel);
        }

      info->src_path = src_rel;
      info->dst_path = dst_rel;
    }

  /* Create a new commit item and add it to the array. */
  if (ctx->log_msg_func || ctx->log_msg_func2)
    {
      svn_client_commit_item2_t *item;
      const char *tmp_file;
      apr_array_header_t *commit_items 
        = apr_array_make(pool, 2 * copy_pairs->nelts, sizeof(item));

      for (i = 0; i < copy_pairs->nelts; i++)
        {
          path_driver_info *info = 
            &(((path_driver_info *) (path_infos->elts))[i]);

          item = apr_pcalloc(pool, sizeof(*item));
          item->url = svn_path_join(top_url, info->dst_path, pool);
          item->state_flags = SVN_CLIENT_COMMIT_ITEM_ADD;
          APR_ARRAY_PUSH(commit_items, svn_client_commit_item2_t *) = item;
          apr_hash_set(action_hash, info->dst_path, APR_HASH_KEY_STRING,
                       info);

          if (is_move && (! info->resurrection))
            {
              item = apr_pcalloc(pool, sizeof(*item));
              item->url = svn_path_join(top_url, info->src_path, pool);
              item->state_flags = SVN_CLIENT_COMMIT_ITEM_DELETE;
              APR_ARRAY_PUSH(commit_items, svn_client_commit_item2_t *) = item;
              apr_hash_set(action_hash, info->src_path, APR_HASH_KEY_STRING,
                           info);
            }
        }

      SVN_ERR(svn_client__get_log_msg(&message, &tmp_file, commit_items,
                                      ctx, pool));
      if (! message)
        return SVN_NO_ERROR;
    }
  else
    message = "";


  /* Fetch RA commit editor. */
  SVN_ERR(svn_client__commit_get_baton(&commit_baton, commit_info_p, pool));
  SVN_ERR(svn_ra_get_commit_editor2(ra_session, &editor, &edit_baton,
                                    message,
                                    svn_client__commit_callback,
                                    commit_baton, 
                                    NULL, TRUE, /* No lock tokens */
                                    pool));

  /* Setup our PATHS for the path-based editor drive. */
  for (i = 0; i < copy_pairs->nelts; i++)
    {
      path_driver_info *info = &(((path_driver_info *) (path_infos->elts))[i]);

      APR_ARRAY_PUSH(paths, const char *) = info->dst_path;
      if (is_move && (! info->resurrection))
        APR_ARRAY_PUSH(paths, const char *) = info->src_path;
    }

  /* Setup the callback baton. */
  cb_baton.editor = editor;
  cb_baton.edit_baton = edit_baton;
  cb_baton.action_hash = action_hash;
  cb_baton.is_move = is_move;
  cb_baton.src_revnum = src_revnum;

  /* Call the path-based editor driver. */
  err = svn_delta_path_driver(editor, edit_baton, youngest, paths,
                              path_driver_cb_func, &cb_baton, pool);
  if (err)
    {
      /* At least try to abort the edit (and fs txn) before throwing err. */
      svn_error_clear(editor->abort_edit(edit_baton, pool));
      return err;
    }

  /* Close the edit. */
  SVN_ERR(editor->close_edit(edit_baton, pool));

  return SVN_NO_ERROR;
}



static svn_error_t *
remove_tmpfiles(apr_hash_t *tempfiles,
                svn_cancel_func_t cancel_func,
                void *cancel_baton,
                apr_pool_t *pool)
{
  apr_hash_index_t *hi;

  /* Split if there's nothing to be done. */
  if (! tempfiles)
    return SVN_NO_ERROR;

  /* Clean up any tempfiles. */
  for (hi = apr_hash_first(pool, tempfiles); hi; hi = apr_hash_next(hi))
    {
      const void *key;
      apr_ssize_t keylen;
      void *val;
      svn_node_kind_t kind;

      if (cancel_func)
        SVN_ERR(cancel_func(cancel_baton));

      apr_hash_this(hi, &key, &keylen, &val);
      SVN_ERR(svn_io_check_path((const char *)key, &kind, pool));
      if (kind == svn_node_file)
        SVN_ERR(svn_io_remove_file((const char *)key, pool));
    }

  return SVN_NO_ERROR;
}



static svn_error_t *
reconcile_errors(svn_error_t *commit_err,
                 svn_error_t *unlock_err,
                 svn_error_t *cleanup_err,
                 apr_pool_t *pool)
{
  svn_error_t *err;

  /* Early release (for good behavior). */
  if (! (commit_err || unlock_err || cleanup_err))
    return SVN_NO_ERROR;

  /* If there was a commit error, start off our error chain with
     that. */
  if (commit_err)
    {
      commit_err = svn_error_quick_wrap 
        (commit_err, _("Commit failed (details follow):"));
      err = commit_err;
    }

  /* Else, create a new "general" error that will lead off the errors
     that follow. */
  else
    err = svn_error_create(SVN_ERR_BASE, NULL,
                           _("Commit succeeded, but other errors follow:"));

  /* If there was an unlock error... */
  if (unlock_err)
    {
      /* Wrap the error with some headers. */
      unlock_err = svn_error_quick_wrap 
        (unlock_err, _("Error unlocking locked dirs (details follow):"));

      /* Append this error to the chain. */
      svn_error_compose(err, unlock_err);
    }

  /* If there was a cleanup error... */
  if (cleanup_err)
    {
      /* Wrap the error with some headers. */
      cleanup_err = svn_error_quick_wrap 
        (cleanup_err, _("Error in post-commit clean-up (details follow):"));

      /* Append this error to the chain. */
      svn_error_compose(err, cleanup_err);
    }

  return err;
}



static svn_error_t *
wc_to_repos_copy(svn_commit_info_t **commit_info_p,
                 const apr_array_header_t *copy_pairs, 
                 svn_client_ctx_t *ctx,
                 apr_pool_t *pool)
{
  const char *message;
  const char *top_src_path, *top_dst_url;
  svn_ra_session_t *ra_session;
  const svn_delta_editor_t *editor;
  void *edit_baton;
  svn_node_kind_t base_kind;
  void *commit_baton;
  apr_hash_t *committables, *tempfiles = NULL;
  svn_wc_adm_access_t *adm_access, *dir_access;
  apr_array_header_t *commit_items;
  svn_error_t *cmt_err = SVN_NO_ERROR;
  svn_error_t *unlock_err = SVN_NO_ERROR;
  svn_error_t *cleanup_err = SVN_NO_ERROR;
  int i;

  /* The commit process uses absolute paths, so we need to open the access
     baton using absolute paths, and so we really need to use absolute
     paths everywhere. */  
  for (i = 0; i < copy_pairs->nelts; i++)
    {
      svn_client__copy_pair_t *pair =
        ((svn_client__copy_pair_t **) (copy_pairs->elts))[i];
      SVN_ERR(svn_path_get_absolute(&pair->src_abs, pair->src, pool));
    }

  /*Find the common root of all the source paths, and probe the wc. */
  get_copy_pair_ancestors(copy_pairs, &top_src_path, NULL, NULL, pool);
  SVN_ERR(svn_wc_adm_probe_open3(&adm_access, NULL, top_src_path,
                                 FALSE, -1, ctx->cancel_func,
                                 ctx->cancel_baton, pool));

  /* Determine the least common ancesor for the destinations, and open an RA
     session to that location. */
  svn_path_split(((svn_client__copy_pair_t **) (copy_pairs->elts))[0]->dst,
                 &top_dst_url,
                 NULL, pool);
  for (i = 1; i < copy_pairs->nelts; i++)
    {
      svn_client__copy_pair_t *pair =
        ((svn_client__copy_pair_t **) (copy_pairs->elts))[i];
      top_dst_url = svn_path_get_longest_ancestor(top_dst_url, pair->dst, pool);
    }
                               
  SVN_ERR(svn_client__open_ra_session_internal(&ra_session, top_dst_url,
                                               svn_wc_adm_access_path
                                               (adm_access),
                                               adm_access, NULL, TRUE, TRUE, 
                                               ctx, pool));

  /* Figure out the basename that will result from each copy and check to make
     sure it doesn't exist already. */
  for (i = 0; i < copy_pairs->nelts; i++)
    {
      svn_node_kind_t dst_kind;
      svn_client__copy_pair_t *pair =
        ((svn_client__copy_pair_t **) (copy_pairs->elts))[i];

      pair->dst_rel = svn_path_is_child(top_dst_url, pair->dst, pool);
      SVN_ERR(svn_ra_check_path(ra_session, 
                                svn_path_uri_decode(pair->dst_rel, pool),
                                SVN_INVALID_REVNUM, &dst_kind, pool));
  
      if (dst_kind != svn_node_none)
        {
          return svn_error_createf(SVN_ERR_FS_ALREADY_EXISTS, NULL,
                                   _("Path '%s' already exists"), pair->dst);
        }
    }

  /* Create a new commit item and add it to the array. */
  if (ctx->log_msg_func || ctx->log_msg_func2)
    {
      svn_client_commit_item2_t *item;
      const char *tmp_file;
      commit_items = apr_array_make(pool, copy_pairs->nelts, sizeof(item));

      for (i = 0; i < copy_pairs->nelts; i++ )
        {
          svn_client__copy_pair_t *pair =
            ((svn_client__copy_pair_t **) (copy_pairs->elts))[i];

          item = apr_pcalloc(pool, sizeof(*item));
          item->url = pair->dst;
          item->state_flags = SVN_CLIENT_COMMIT_ITEM_ADD;
          APR_ARRAY_PUSH(commit_items, svn_client_commit_item2_t *) = item;
        }

      SVN_ERR(svn_client__get_log_msg(&message, &tmp_file, commit_items,
                                      ctx, pool));
      if (! message)
        return SVN_NO_ERROR;
    }
  else
    message = "";

  /* Crawl the working copy for commit items. */
  SVN_ERR(svn_io_check_path(top_src_path, &base_kind, pool));
  if (base_kind == svn_node_dir)
    SVN_ERR(svn_wc_adm_retrieve(&dir_access, adm_access, top_src_path, pool));
  else
    dir_access = adm_access;

  if ((cmt_err = svn_client__get_copy_committables(&committables,
                                                   copy_pairs,
                                                   dir_access,
                                                   ctx,
                                                   pool)))
    goto cleanup;

  /* ### todo: There should be only one hash entry, which currently
     has a hacked name until we have the entries files storing
     canonical repository URLs.  Then, the hacked name can go away and
     be replaced with a entry->repos (or whereever the entry's
     canonical repos URL is stored). */
  if (! ((commit_items = apr_hash_get(committables, 
                                      SVN_CLIENT__SINGLE_REPOS_NAME, 
                                      APR_HASH_KEY_STRING))))
    goto cleanup;

  /* Sort and condense our COMMIT_ITEMS. */
  if ((cmt_err = svn_client__condense_commit_items(&top_dst_url, 
                                                   commit_items, 
                                                   pool)))
    goto cleanup;

  /* Open an RA session to DST_URL. */
  if ((cmt_err = svn_client__open_ra_session_internal(&ra_session, top_dst_url,
                                                      NULL, NULL,
                                                      commit_items,
                                                      FALSE, FALSE,
                                                      ctx, pool)))
    goto cleanup;

  /* Fetch RA commit editor. */
  SVN_ERR(svn_client__commit_get_baton(&commit_baton, commit_info_p, pool));
  if ((cmt_err = svn_ra_get_commit_editor2(ra_session, &editor, &edit_baton, 
                                           message,
                                           svn_client__commit_callback,
                                           commit_baton, 
                                           NULL, TRUE, /* No lock tokens */
                                           pool)))
    goto cleanup;

  /* Perform the commit. */
  cmt_err = svn_client__do_commit(top_dst_url, commit_items, adm_access,
                                  editor, edit_baton, 
                                  0, /* ### any notify_path_offset needed? */
                                  &tempfiles, NULL, ctx, pool);

  /* Sleep to ensure timestamp integrity. */
  svn_sleep_for_timestamps();

 cleanup:

  /* It's only a read lock, so unlocking is harmless. */
  unlock_err = svn_wc_adm_close(adm_access);

  /* Remove any outstanding temporary text-base files. */
  if (tempfiles)
    cleanup_err = remove_tmpfiles(tempfiles,
                                  ctx->cancel_func, ctx->cancel_baton,
                                  pool);

  return reconcile_errors(cmt_err, unlock_err, cleanup_err, pool);
}


static svn_error_t *
repos_to_wc_copy_single(svn_client__copy_pair_t *pair,
                        svn_revnum_t src_revnum,
                        svn_boolean_t same_repositories,
                        svn_opt_revision_t *revision,
                        svn_ra_session_t *ra_session,
                        svn_wc_adm_access_t *adm_access,
                        svn_client_ctx_t *ctx,
                        apr_pool_t *pool)
{
  if (pair->src_kind == svn_node_dir)
    {
      SVN_ERR(svn_client__checkout_internal
              (NULL, pair->src, pair->dst, revision, revision,
               TRUE, FALSE, FALSE, NULL, ctx, pool));

      if ((revision->kind == svn_opt_revision_head) && same_repositories)
        {
          /* If we just checked out from the "head" revision, that's fine,
             but we don't want to pass a '-1' as a copyfrom_rev to
             svn_wc_add().  That function will dump it right into the
             entry, and when we try to commit later on, the
             'add-dir-with-history' step will be -very- unhappy; it only
             accepts specific revisions.
             
             On the other hand, we *could* say that -1 is a legitimate
             copyfrom_rev, but I think that's bogus.  Somebody made a copy
             from a particular revision;  if they wait a long time to
             commit, it would be terrible if the copied happened from a
             newer revision!! */
          
          /* We just did a checkout; whatever revision we just got, that
             should be the copyfrom_revision when we commit later. */
          const svn_wc_entry_t *d_entry;
          svn_wc_adm_access_t *dst_access;
          SVN_ERR(svn_wc_adm_open3(&dst_access, adm_access, pair->dst,
                                   TRUE, -1, ctx->cancel_func,
                                   ctx->cancel_baton, pool));
          SVN_ERR(svn_wc_entry(&d_entry, pair->dst, dst_access, FALSE, pool));
          src_revnum = d_entry->revision;
        }

      /* Rewrite URLs recursively, remove wcprops, and mark everything
         as 'copied' -- assuming that the src and dst are from the
         same repository.  (It's kind of weird that svn_wc_add() is the
         way to do this; see its doc for more about the controversy.) */
      if (same_repositories)
        {
          /* Schedule dst_path for addition in parent, with copy history.
             (This function also recursively puts a 'copied' flag on every
             entry). */
          SVN_ERR(svn_wc_add2(pair->dst, adm_access, pair->src, src_revnum,
                              ctx->cancel_func, ctx->cancel_baton, 
                              ctx->notify_func2, ctx->notify_baton2, pool));
        }
      else  /* different repositories */
        {
          /* ### Someday, we would just call svn_wc_add(), as above,
             but with no copyfrom args.  I.e. in the
             directory-foreign-UUID case, we still want everything
             scheduled for addition, URLs rewritten, and wcprop cache
             deleted, but WITHOUT any copied flags or copyfrom urls.
             Unfortunately, svn_wc_add() is such a mess that it chokes
             at the moment when we pass a NULL copyfromurl. */
          
          return svn_error_createf
            (SVN_ERR_UNSUPPORTED_FEATURE, NULL,
             _("Source URL '%s' is from foreign repository; "
               "leaving it as a disjoint WC"), pair->src);
        }
    } /* end directory case */

  else if (pair->src_kind == svn_node_file)
    {
      apr_file_t *fp;
      svn_stream_t *fstream;
      svn_revnum_t real_rev;
      const char *new_text_path;
      apr_hash_t *new_props;
      svn_error_t *err;

      SVN_ERR(svn_io_open_unique_file2
              (&fp, &new_text_path, pair->dst, ".tmp",
               svn_io_file_del_none, pool));

      fstream = svn_stream_from_aprfile2(fp, FALSE, pool);
      SVN_ERR(svn_ra_get_file(ra_session, pair->src_rel, src_revnum, fstream,
                              &real_rev, &new_props, pool));
      SVN_ERR(svn_stream_close(fstream));

      /* If SRC_REVNUM is invalid (HEAD), then REAL_REV is now the
         revision that was actually retrieved.  This is the value we
         want to use as 'copyfrom_rev' below. */
      if (! SVN_IS_VALID_REVNUM(src_revnum))
        src_revnum = real_rev;

      err = svn_wc_add_repos_file2
        (pair->dst, adm_access,
         new_text_path, NULL, new_props, NULL,
         same_repositories ? pair->src : NULL,
         same_repositories ? src_revnum : SVN_INVALID_REVNUM,
         pool);

      /* Ideally, svn_wc_add_repos_file() would take a notify function
         and baton, and we wouldn't have to make this call here.
         However, the situation is... complicated.  See issue #1552
         for the full story. */
      if (!err && ctx->notify_func2)
        {
          svn_wc_notify_t *notify = svn_wc_create_notify(pair->dst,
                                                         svn_wc_notify_add,
                                                         pool);
          notify->kind = pair->src_kind;
          (*ctx->notify_func2)(ctx->notify_baton2, notify, pool);
        }

      svn_sleep_for_timestamps();
      SVN_ERR(err);
    }
  
    return SVN_NO_ERROR;
}

static svn_error_t *
repos_to_wc_copy(const apr_array_header_t *copy_pairs,
                 const svn_opt_revision_t *src_revision,
                 svn_client_ctx_t *ctx,
                 apr_pool_t *pool)
{
  svn_ra_session_t *ra_session;
  svn_revnum_t src_revnum;
  svn_wc_adm_access_t *adm_access;
  const char *top_src_url, *top_dst_path;
  const char *src_uuid = NULL, *dst_uuid = NULL;
  svn_boolean_t same_repositories;
  svn_opt_revision_t revision;
  int i;

  get_copy_pair_ancestors(copy_pairs, &top_src_url, &top_dst_path, NULL, pool);

  /* Open a repository session to the longest common src ancestor.  We do not
     (yet) have a working copy, so we don't have a corresponding path and
     tempfiles cannot go into the admin area. */
  SVN_ERR(svn_client__open_ra_session_internal(&ra_session, top_src_url, NULL,
                                               NULL, NULL, FALSE, TRUE, 
                                               ctx, pool));
  
  /* Pass null for the path, to ensure error if trying to get a
     revision based on the working copy.  And additionally, we can't
     pass an 'unspecified' revnum to the update reporter;  assume HEAD
     if not specified. */
  revision.kind = src_revision->kind;
  revision.value = src_revision->value;
  if (revision.kind == svn_opt_revision_unspecified)
    revision.kind = svn_opt_revision_head;

  SVN_ERR(svn_client__get_revision_number
          (&src_revnum, ra_session, &revision, NULL, pool));

  /* Verify that each SRC_URL exists in the repository, and that we aren't
     overwriting an existing path. */
  for (i = 0; i < copy_pairs->nelts; i++)
    {
      svn_client__copy_pair_t *pair =
        ((svn_client__copy_pair_t **) (copy_pairs->elts))[i];
      svn_node_kind_t dst_parent_kind, dst_kind;
      const char *dst_parent;

      pair->src_rel = svn_path_is_child(top_src_url, pair->src, pool);

      SVN_ERR(svn_ra_check_path(ra_session, pair->src_rel, src_revnum, 
                                &pair->src_kind,
                                pool));
      if (pair->src_kind == svn_node_none)
        {
          if (SVN_IS_VALID_REVNUM(src_revnum))
            return svn_error_createf
              (SVN_ERR_FS_NOT_FOUND, NULL,
               _("Path '%s' not found in revision %ld"),
               pair->src, src_revnum);
          else
            return svn_error_createf
              (SVN_ERR_FS_NOT_FOUND, NULL,
               _("Path '%s' not found in head revision"), pair->src);
        }

      /* Figure out about dst. */
      SVN_ERR(svn_io_check_path(pair->dst, &dst_kind, pool));
      if (dst_kind != svn_node_none)
        {
          return svn_error_createf(SVN_ERR_ENTRY_EXISTS, NULL,
                                   _("Path '%s' already exists"),
                                   svn_path_local_style(pair->dst, pool));
        }

      /* Make sure the destination parent is a directory and produce a clear
         error message if it is not. */
      dst_parent = svn_path_dirname(pair->dst, pool);
      SVN_ERR(svn_io_check_path(dst_parent, &dst_parent_kind, pool));
      if (dst_parent_kind != svn_node_dir)
        return svn_error_createf(SVN_ERR_WC_NOT_DIRECTORY, NULL,
                                 _("Path '%s' is not a directory"),
                                 svn_path_local_style(dst_parent, pool));
    }

  /* Probe the wc at the longest common dst ancestor. */
  SVN_ERR(svn_wc_adm_probe_open3(&adm_access, NULL, top_dst_path, TRUE,
                                 0, ctx->cancel_func, ctx->cancel_baton,
                                 pool));

  /* We've already checked for physical obstruction by a working file.
     But there could also be logical obstruction by an entry whose
     working file happens to be missing.*/ 
  for (i = 0; i < copy_pairs->nelts; i++)
    {
      svn_client__copy_pair_t *pair =
        ((svn_client__copy_pair_t **) (copy_pairs->elts))[i];
      const svn_wc_entry_t *ent;

      SVN_ERR(svn_wc_entry(&ent, pair->dst, adm_access, FALSE, pool));
      if (ent && (ent->kind != svn_node_dir) && 
          (ent->schedule != svn_wc_schedule_delete))
        return svn_error_createf
          (SVN_ERR_WC_OBSTRUCTED_UPDATE, NULL,
           _("Entry for '%s' exists (though the working file is missing)"),
           svn_path_local_style(pair->dst, pool));
    }

  /* Decide whether the two repositories are the same or not. */
  { 
    svn_error_t *src_err, *dst_err;
    const char *parent;
   
    /* Get the repository uuid of SRC_URL */
    src_err = svn_ra_get_uuid(ra_session, &src_uuid, pool);
    if (src_err && src_err->apr_err != SVN_ERR_RA_NO_REPOS_UUID)
      return src_err;

    /* Get repository uuid of dst's parent directory, since dst may
       not exist.  ### TODO:  we should probably walk up the wc here,
       in case the parent dir has an imaginary URL.  */
    if (copy_pairs->nelts == 1)
      svn_path_split(top_dst_path, &parent, NULL, pool);
    else
      parent = top_dst_path;
    dst_err = svn_client_uuid_from_path(&dst_uuid, parent, adm_access,
                                        ctx, pool);
    if (dst_err && dst_err->apr_err != SVN_ERR_RA_NO_REPOS_UUID)
      return dst_err;
    
    /* If either of the UUIDs are nonexistent, then at least one of
       the repositories must be very old.  Rather than punish the
       user, just assume the repositories are different, so no
       copy-history is attempted. */
    if (src_err || dst_err || (! src_uuid) || (! dst_uuid))
      same_repositories = FALSE;
        
    else
      same_repositories = (strcmp(src_uuid, dst_uuid) == 0) ? TRUE : FALSE; 
  }

  /* Perform the move for each of the copy_pairs. */
  for (i = 0; i < copy_pairs->nelts; i++)
    {
      /* Check for cancellation */
      if (ctx->cancel_func)
        SVN_ERR(ctx->cancel_func(ctx->cancel_baton));

      SVN_ERR(repos_to_wc_copy_single(((svn_client__copy_pair_t **)
                                        (copy_pairs->elts))[i], 
                                      src_revnum, same_repositories, &revision, 
                                      ra_session, adm_access,
                                      ctx, pool));
    }

  SVN_ERR(svn_wc_adm_close(adm_access));

  return SVN_NO_ERROR;
}


static svn_error_t *
setup_copy(svn_commit_info_t **commit_info_p,
           const apr_array_header_t *src_paths,
           const svn_opt_revision_t *src_revision,
           const char *dst_path_in,
           svn_boolean_t is_move,
           svn_boolean_t force,
           svn_client_ctx_t *ctx,
           apr_pool_t *pool)
{
  apr_pool_t *subpool;
  apr_array_header_t *copy_pairs = apr_array_make(pool, src_paths->nelts,
                                                  sizeof(struct copy_pair *));
  svn_boolean_t srcs_are_urls, dst_is_url;
  int i;

  /* Are either of our paths URLs?
   * Just check the first src_path.  If there are more than one, we'll check
   * for homogeneity amoung them down below. */
  srcs_are_urls = svn_path_is_url(((const char **) (src_paths->elts))[0]);
  dst_is_url = svn_path_is_url(dst_path_in);

  /* If we have multiple source paths, it implies the dst_path is a directory
   * we are moving or copying into.  Populate the dst_paths array to contain
   * a destination path for each of the source paths. */
  if (src_paths->nelts > 1)
    {
      subpool = svn_pool_create(pool);

      for ( i = 0; i < src_paths->nelts; i++)
        {
          const char *src_path = ((const char **) (src_paths->elts))[i];
          const char *src_basename;
          svn_client__copy_pair_t *pair = apr_palloc(pool, sizeof(*pair));

          svn_pool_clear(subpool);
          src_basename = svn_path_basename(src_path, subpool);

          /* Check to see if all the sources are urls or all working copy 
           * paths. */
          if (svn_path_is_url(src_path) != srcs_are_urls)
            return svn_error_create
              (SVN_ERR_UNSUPPORTED_FEATURE, NULL,
               _("Cannot mix repository and working copy paths in source list"));

          pair->src = src_path;
          pair->dst = svn_path_join(dst_path_in, src_basename, pool);
          APR_ARRAY_PUSH(copy_pairs, svn_client__copy_pair_t *) = pair;
        }

      svn_pool_destroy(subpool);
    }
  else
    {
      svn_client__copy_pair_t *pair = apr_palloc(pool, sizeof(*pair));

      pair->src = ((const char **) (src_paths->elts))[0];
      pair->dst = dst_path_in;
      APR_ARRAY_PUSH(copy_pairs, svn_client__copy_pair_t *) = pair;
    }

  if (!srcs_are_urls && !dst_is_url)
    {
      subpool = svn_pool_create(pool);

      for ( i = 0; i < copy_pairs->nelts; i++ )
        {
          svn_client__copy_pair_t *pair =
            ((svn_client__copy_pair_t **) (copy_pairs->elts))[i];

          svn_pool_clear(subpool);
          
          if (svn_path_is_child(pair->src, pair->dst, subpool))
            return svn_error_createf
              (SVN_ERR_UNSUPPORTED_FEATURE, NULL,
               _("Cannot copy path '%s' into its own child '%s'"),
               svn_path_local_style(pair->src, pool),
               svn_path_local_style(pair->dst, pool));
        }

      svn_pool_destroy(subpool);
    }

  if (is_move)
    {
      if (srcs_are_urls == dst_is_url)
        {
          for ( i = 0; i < copy_pairs->nelts; i++)
            {
              svn_client__copy_pair_t *pair =
                ((svn_client__copy_pair_t **) (copy_pairs->elts))[i];

              if (strcmp(pair->src, pair->dst) == 0)
                return svn_error_createf
                  (SVN_ERR_UNSUPPORTED_FEATURE, NULL,
                   _("Cannot move path '%s' into itself"),
                   svn_path_local_style(pair->src, pool));
            }
        }
      else
        {
          /* Disallow moves between the working copy and the repository. */
          return svn_error_create 
            (SVN_ERR_UNSUPPORTED_FEATURE, NULL,
             _("No support for repos <--> working copy moves"));
        }
    }
  else
    {
      if (!srcs_are_urls)
        {
          if (src_revision->kind != svn_opt_revision_unspecified
              && src_revision->kind != svn_opt_revision_working)
            {
              for ( i = 0; i < copy_pairs->nelts; i++)
                {
                  svn_client__copy_pair_t *pair =
                    ((svn_client__copy_pair_t **) (copy_pairs->elts))[i];

                  /* We can convert the working copy path to a URL based on the
                     entries file. */
                  svn_wc_adm_access_t *adm_access;  /* ### FIXME local */
                  const svn_wc_entry_t *entry;
                  SVN_ERR(svn_wc_adm_probe_open3(&adm_access, NULL,
                                                 pair->src, FALSE, 0,
                                                 ctx->cancel_func,
                                                 ctx->cancel_baton, 
                                                 pool));
                  SVN_ERR(svn_wc_entry(&entry, pair->src, adm_access, FALSE,
                                       pool));
                  SVN_ERR(svn_wc_adm_close(adm_access));

                  if (! entry)
                    return svn_error_createf
                      (SVN_ERR_UNVERSIONED_RESOURCE, NULL,
                       _("'%s' is not under version control"),
                       svn_path_local_style(pair->src, pool));

                  if (! entry->url)
                    return svn_error_createf
                      (SVN_ERR_ENTRY_MISSING_URL, NULL,
                       _("'%s' does not seem to have a URL associated with it"),
                       svn_path_local_style(pair->src, pool));

                  ((svn_client__copy_pair_t **) (copy_pairs->elts))[i]->src =
                    entry->url;
                }

              srcs_are_urls = TRUE;
            }
        }
    }

  /* Now, call the right handler for the operation. */
  if ((! srcs_are_urls) && (! dst_is_url))
    {
      SVN_ERR(wc_to_wc_copy(copy_pairs, is_move,
                            ctx, pool));
    }
  else if ((! srcs_are_urls) && (dst_is_url))
    {
      SVN_ERR(wc_to_repos_copy(commit_info_p, copy_pairs,
                               ctx, pool));
    }
  else if ((srcs_are_urls) && (! dst_is_url))
    {
      SVN_ERR(repos_to_wc_copy(copy_pairs, src_revision,
                               ctx, pool));
    }
  else
    {
      SVN_ERR(repos_to_repos_copy(commit_info_p, copy_pairs, src_revision,
                                  ctx, is_move, pool));
    }

  return SVN_NO_ERROR;
}



/* Public Interfaces */

svn_error_t *
svn_client_copy3(svn_commit_info_t **commit_info_p,
                 const char *src_path,
                 const svn_opt_revision_t *src_revision,
                 const char *dst_path,
                 svn_client_ctx_t *ctx,
                 apr_pool_t *pool)
{
  apr_array_header_t *src_paths = apr_array_make(pool, 1, sizeof(const char *));
  APR_ARRAY_PUSH(src_paths, const char *) = src_path;

  return setup_copy(commit_info_p,
                    src_paths, src_revision, dst_path,
                    FALSE /* is_move */,
                    TRUE /* force, set to avoid deletion check */,
                    ctx,
                    pool);
}


svn_error_t *
svn_client_copy2(svn_commit_info_t **commit_info_p,
                 const char *src_path,
                 const svn_opt_revision_t *src_revision,
                 const char *dst_path,
                 svn_client_ctx_t *ctx,
                 apr_pool_t *pool)
{
  svn_error_t *err;

  err = svn_client_copy3(commit_info_p, src_path, src_revision,
                         dst_path, ctx, pool);

  /* If the target exists, try to copy the source as a child of the target.
     This will obviously fail if target is not a directory, but that's exactly
     what we want. */
  if (err && (err->apr_err == SVN_ERR_ENTRY_EXISTS
              || err->apr_err == SVN_ERR_FS_ALREADY_EXISTS))
    {
      const char *src_basename = svn_path_basename(src_path, pool);

      svn_error_clear(err);
      
      return svn_client_copy3(commit_info_p, src_path, src_revision,
                              svn_path_join(dst_path, src_basename, pool),
                              ctx, pool);
    }

  return err;
}

svn_error_t *
svn_client_copy(svn_client_commit_info_t **commit_info_p,
                const char *src_path,
                const svn_opt_revision_t *src_revision,
                const char *dst_path,
                svn_client_ctx_t *ctx,
                apr_pool_t *pool)
{
  svn_commit_info_t *commit_info = NULL;
  svn_error_t *err;

  err = svn_client_copy2(&commit_info, src_path, src_revision, dst_path,
                         ctx, pool);
  /* These structs have the same layout for the common fields. */
  *commit_info_p = (svn_client_commit_info_t *) commit_info;
  return err;
}

svn_error_t *
svn_client_copy_into(svn_commit_info_t **commit_info_p,
                     const apr_array_header_t *src_paths,
                     const svn_opt_revision_t *src_revision,
                     const char *dst_dir,
                     svn_client_ctx_t *ctx,
                     apr_pool_t *pool)
{
  const char *dst_path;

  /* If we have only one source, then setup_copy is going to think the
   * destination is explicit, so we had better make sure that it is. */
  if (src_paths->nelts == 1)
    {
      const char *src_path = ((const char **) (src_paths->elts))[0];
      const char *src_basename = svn_path_basename(src_path, pool);
      dst_path = svn_path_join(dst_dir, src_basename, pool);
    }
  else
    {
      dst_path = dst_dir;
    }

  return setup_copy(commit_info_p,
                    src_paths, src_revision, dst_path,
                    FALSE /* is_move */,
                    TRUE /* force, set to avoid deletion check */,
                    ctx,
                    pool);
}

svn_error_t *
svn_client_move4(svn_commit_info_t **commit_info_p,
                 const char *src_path,
                 const char *dst_path,
                 svn_boolean_t force,
                 svn_client_ctx_t *ctx,
                 apr_pool_t *pool)
{
  const svn_opt_revision_t src_revision
    = { svn_opt_revision_unspecified, { 0 } };
  apr_array_header_t *src_paths = apr_array_make(pool, 1, sizeof(const char *));
  APR_ARRAY_PUSH(src_paths, const char *) = src_path;

  return setup_copy(commit_info_p,
                    src_paths, &src_revision, dst_path,
                    TRUE /* is_move */,
                    force,
                    ctx,
                    pool);
}

svn_error_t *
svn_client_move3(svn_commit_info_t **commit_info_p,
                 const char *src_path,
                 const char *dst_path,
                 svn_boolean_t force,
                 svn_client_ctx_t *ctx,
                 apr_pool_t *pool)
{
  svn_error_t *err;

  err = svn_client_move4(commit_info_p, src_path, dst_path, force, ctx, pool);

  /* If the target exists, try to move the source as a child of the target.
     This will obviously fail if target is not a directory, but that's exactly
     what we want. */
  if (err && (err->apr_err == SVN_ERR_ENTRY_EXISTS
              || err->apr_err == SVN_ERR_FS_ALREADY_EXISTS))
    {
      const char *src_basename = svn_path_basename(src_path, pool);

      svn_error_clear(err);
      
      return svn_client_move4(commit_info_p, src_path,
                              svn_path_join(dst_path, src_basename, pool),
                              force, ctx, pool);
    }

  return err;
}

svn_error_t *
svn_client_move2(svn_client_commit_info_t **commit_info_p,
                 const char *src_path,
                 const char *dst_path,
                 svn_boolean_t force,
                 svn_client_ctx_t *ctx,
                 apr_pool_t *pool)
{
  svn_commit_info_t *commit_info = NULL;
  svn_error_t *err;

  err = svn_client_move3(&commit_info, src_path, dst_path, force, ctx, pool);
  /* These structs have the same layout for the common fields. */
  *commit_info_p = (svn_client_commit_info_t *) commit_info;
  return err;
}


svn_error_t *
svn_client_move(svn_client_commit_info_t **commit_info_p,
                const char *src_path,
                const svn_opt_revision_t *src_revision,
                const char *dst_path,
                svn_boolean_t force,
                svn_client_ctx_t *ctx,
                apr_pool_t *pool)
{
  svn_commit_info_t *commit_info = NULL;
  svn_error_t *err;
  apr_array_header_t *src_paths = apr_array_make(pool, 1, sizeof(const char *));

  /* It doesn't make sense to specify revisions in a move. */

  /* ### todo: this check could fail wrongly.  For example,
     someone could pass in an svn_opt_revision_number that just
     happens to be the HEAD.  It's fair enough to punt then, IMHO,
     and just demand that the user not specify a revision at all;
     beats mucking up this function with RA calls and such. */ 
  if (src_revision->kind != svn_opt_revision_unspecified
      && src_revision->kind != svn_opt_revision_head)
    {
      return svn_error_create
        (SVN_ERR_UNSUPPORTED_FEATURE, NULL,
         _("Cannot specify revisions (except HEAD) with move operations"));
    }
 
  APR_ARRAY_PUSH(src_paths, const char *) = src_path;

  err = setup_copy(&commit_info,
                   src_paths, src_revision, dst_path,
                   TRUE /* is_move */,
                   force,
                   ctx,
                   pool);
  /* These structs have the same layout for the common fields. */
  *commit_info_p = (svn_client_commit_info_t *) commit_info;
  return err;
}

svn_error_t *
svn_client_move_into(svn_commit_info_t **commit_info_p,
                     apr_array_header_t *src_paths,
                     const char *dst_dir,
                     svn_boolean_t force,
                     svn_client_ctx_t *ctx,
                     apr_pool_t *pool)
{
  const svn_opt_revision_t src_revision
    = { svn_opt_revision_unspecified, { 0 } };
  const char *dst_path;

  /* If we have only one source, then setup_copy is going to think the
   * destination is explicit, so we had better make sure that it is. */
  if (src_paths->nelts == 1)
    {
      const char *src_path = ((const char **) (src_paths->elts))[0];
      const char *src_basename = svn_path_basename(src_path, pool);
      dst_path = svn_path_join(dst_dir, src_basename, pool);
    }
  else
    {
      dst_path = dst_dir;
    }

  return setup_copy(commit_info_p, src_paths, &src_revision, dst_path,
                    TRUE /* is_move */,
                    force,
                    ctx,
                    pool);
}
