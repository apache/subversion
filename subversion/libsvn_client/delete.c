/*
 * delete.c:  wrappers around wc delete functionality.
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

#include <apr_file_io.h>
#include "svn_types.h"
#include "svn_pools.h"
#include "svn_wc.h"
#include "svn_client.h"
#include "svn_error.h"
#include "svn_dirent_uri.h"
#include "svn_path.h"
#include "client.h"

#include "private/svn_client_private.h"
#include "private/svn_wc_private.h"

#include "svn_private_config.h"


/*** Code. ***/


/* An svn_client_status_func_t callback function for finding
   status structures which are not safely deletable. */
static svn_error_t *
find_undeletables(void *baton,
                  const char *path,
                  const svn_client_status_t *status,
                  apr_pool_t *pool)
{
  /* Check for error-ful states. */
  if (status->node_status == svn_wc_status_obstructed)
    return svn_error_createf(SVN_ERR_NODE_UNEXPECTED_KIND, NULL,
                             _("'%s' is in the way of the resource "
                               "actually under version control"),
                             svn_dirent_local_style(path, pool));
  else if (! status->versioned)
    return svn_error_createf(SVN_ERR_UNVERSIONED_RESOURCE, NULL,
                             _("'%s' is not under version control"),
                             svn_dirent_local_style(path, pool));

  else if ((status->node_status != svn_wc_status_normal
            && status->node_status != svn_wc_status_deleted
            && status->node_status != svn_wc_status_missing)
           ||
           (status->prop_status != svn_wc_status_none
            && status->prop_status != svn_wc_status_normal))
    return svn_error_createf(SVN_ERR_CLIENT_MODIFIED, NULL,
                             _("'%s' has local modifications -- commit or "
                               "revert them first"),
                             svn_dirent_local_style(path, pool));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_client__can_delete(const char *path,
                       svn_client_ctx_t *ctx,
                       apr_pool_t *scratch_pool)
{
  svn_opt_revision_t revision;
  svn_node_kind_t external_kind;
  const char *defining_abspath;
  const char* local_abspath;

  revision.kind = svn_opt_revision_unspecified;

  SVN_ERR(svn_dirent_get_absolute(&local_abspath, path, scratch_pool));

  /* A file external should not be deleted since the file external is
     implemented as a switched file and it would delete the file the
     file external is switched to, which is not the behavior the user
     would probably want. */
  SVN_ERR(svn_wc__read_external_info(&external_kind, &defining_abspath, NULL,
                                     NULL, NULL,
                                     ctx->wc_ctx, local_abspath,
                                     local_abspath, TRUE,
                                     scratch_pool, scratch_pool));

  if (external_kind != svn_node_none)
    return svn_error_createf(SVN_ERR_WC_CANNOT_DELETE_FILE_EXTERNAL, NULL,
                             _("Cannot remove the external at '%s'; "
                               "please edit or delete the svn:externals "
                               "property on '%s'"),
                             svn_dirent_local_style(local_abspath,
                                                    scratch_pool),
                             svn_dirent_local_style(defining_abspath,
                                                    scratch_pool));


  /* Use an infinite-depth status check to see if there's anything in
     or under PATH which would make it unsafe for deletion.  The
     status callback function find_undeletables() makes the
     determination, returning an error if it finds anything that shouldn't
     be deleted. */
  return svn_error_return(svn_client_status5(NULL, ctx, path, &revision,
                                             svn_depth_infinity, FALSE,
                                             FALSE, FALSE, FALSE, FALSE,
                                             NULL,
                                             find_undeletables, NULL,
                                             scratch_pool));
}


static svn_error_t *
path_driver_cb_func(void **dir_baton,
                    void *parent_baton,
                    void *callback_baton,
                    const char *path,
                    apr_pool_t *pool)
{
  const svn_delta_editor_t *editor = callback_baton;
  *dir_baton = NULL;
  return editor->delete_entry(path, SVN_INVALID_REVNUM, parent_baton, pool);
}

static svn_error_t *
single_repos_delete(svn_ra_session_t *ra_session,
                    const char *repos_root,
                    const apr_array_header_t *relpaths,
                    const apr_hash_t *revprop_table,
                    svn_commit_callback2_t commit_callback,
                    void *commit_baton,
                    svn_client_ctx_t *ctx,
                    apr_pool_t *pool)
{
  const svn_delta_editor_t *editor;
  apr_hash_t *commit_revprops;
  void *edit_baton;
  const char *log_msg;
  int i;
  svn_error_t *err;

  /* Create new commit items and add them to the array. */
  if (SVN_CLIENT__HAS_LOG_MSG_FUNC(ctx))
    {
      svn_client_commit_item3_t *item;
      const char *tmp_file;
      apr_array_header_t *commit_items
        = apr_array_make(pool, relpaths->nelts, sizeof(item));

      for (i = 0; i < relpaths->nelts; i++)
        {
          const char *relpath = APR_ARRAY_IDX(relpaths, i, const char *);

          item = svn_client_commit_item3_create(pool);
          item->url = svn_path_url_add_component2(repos_root, relpath, pool);
          item->state_flags = SVN_CLIENT_COMMIT_ITEM_DELETE;
          APR_ARRAY_PUSH(commit_items, svn_client_commit_item3_t *) = item;
        }
      SVN_ERR(svn_client__get_log_msg(&log_msg, &tmp_file, commit_items,
                                      ctx, pool));
      if (! log_msg)
        return SVN_NO_ERROR;
    }
  else
    log_msg = "";

  SVN_ERR(svn_client__ensure_revprop_table(&commit_revprops, revprop_table,
                                           log_msg, ctx, pool));

  /* Fetch RA commit editor */
  SVN_ERR(svn_ra_get_commit_editor3(ra_session, &editor, &edit_baton,
                                    commit_revprops,
                                    commit_callback,
                                    commit_baton,
                                    NULL, TRUE, /* No lock tokens */
                                    pool));

  /* Call the path-based editor driver. */
  err = svn_delta_path_driver(editor, edit_baton, SVN_INVALID_REVNUM,
                              relpaths, path_driver_cb_func,
                              (void *)editor, pool);

  if (err)
    {
      return svn_error_return(
               svn_error_compose_create(err,
                                        editor->abort_edit(edit_baton, pool)));
    }

  /* Close the edit. */
  return svn_error_return(editor->close_edit(edit_baton, pool));
}

static svn_error_t *
delete_urls_multi_repos(const apr_array_header_t *uris,
                        const apr_hash_t *revprop_table,
                        svn_commit_callback2_t commit_callback,
                        void *commit_baton,
                        svn_client_ctx_t *ctx,
                        apr_pool_t *pool)
{
  apr_hash_t *sessions = apr_hash_make(pool);
  apr_hash_t *relpaths = apr_hash_make(pool);
  apr_hash_index_t *hi;
  int i;

  /* Create a hash of repos_root -> ra_session maps and repos_root -> relpaths
     maps, used to group the various targets. */
  for (i = 0; i < uris->nelts; i++)
    {
      const char *uri = APR_ARRAY_IDX(uris, i, const char *);
      svn_ra_session_t *ra_session = NULL;
      const char *repos_root = NULL;
      const char *repos_relpath = NULL;
      apr_array_header_t *relpaths_list;
      svn_node_kind_t kind;

      for (hi = apr_hash_first(pool, sessions); hi; hi = apr_hash_next(hi))
        {
          repos_root = svn__apr_hash_index_key(hi);
          repos_relpath = svn_uri_is_child(repos_root, uri, pool);

          if (repos_relpath)
            {
              /* Great!  We've found another uri underneath this session,
                 store it and move on. */
              ra_session = svn__apr_hash_index_val(hi);
              relpaths_list = apr_hash_get(relpaths, repos_root,
                                           APR_HASH_KEY_STRING);

              APR_ARRAY_PUSH(relpaths_list, const char *) = repos_relpath;
              break;
            }
        }

      if (!ra_session)
        {
          /* If we haven't found a session yet, we need to open one up.
             Note that we don't have a local directory, nor a place
             to put temp files. */
          SVN_ERR(svn_client__open_ra_session_internal(&ra_session, NULL, uri,
                                                       NULL, NULL, FALSE,
                                                       TRUE, ctx, pool));
          SVN_ERR(svn_ra_get_repos_root2(ra_session, &repos_root, pool));
          SVN_ERR(svn_ra_reparent(ra_session, repos_root, pool));

          apr_hash_set(sessions, repos_root, APR_HASH_KEY_STRING, ra_session);
          repos_relpath = svn_uri_is_child(repos_root, uri, pool);

          relpaths_list = apr_array_make(pool, 1, sizeof(const char *));
          apr_hash_set(relpaths, repos_root, APR_HASH_KEY_STRING,
                       relpaths_list);
          APR_ARRAY_PUSH(relpaths_list, const char *) = repos_relpath;
        }

      /* Now, test to see if the thing actually exists. */
      SVN_ERR(svn_ra_check_path(ra_session, repos_relpath, SVN_INVALID_REVNUM,
                                &kind, pool));
      if (kind == svn_node_none)
        return svn_error_createf(SVN_ERR_FS_NOT_FOUND, NULL,
                                 "URL '%s' does not exist", uri);
    }

  /* At this point, we should have two hashs:
      SESSIONS maps repos_roots to ra_sessions.
      RELPATHS maps repos_roots to a list of decoded relpaths for that root.
      
     Now we iterate over the collection of sessions and do a commit for each
     one with the collected relpaths. */
  for (hi = apr_hash_first(pool, sessions); hi; hi = apr_hash_next(hi))
    {
      const char *repos_root = svn__apr_hash_index_key(hi);
      svn_ra_session_t *ra_session = svn__apr_hash_index_val(hi);
      const apr_array_header_t *relpaths_list = apr_hash_get(relpaths,
                                                         repos_root,
                                                         APR_HASH_KEY_STRING);

      SVN_ERR(single_repos_delete(ra_session, repos_root, relpaths_list,
                                  revprop_table, commit_callback,
                                  commit_baton, ctx, pool));
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_client__wc_delete(const char *path,
                      svn_boolean_t force,
                      svn_boolean_t dry_run,
                      svn_boolean_t keep_local,
                      svn_wc_notify_func2_t notify_func,
                      void *notify_baton,
                      svn_client_ctx_t *ctx,
                      apr_pool_t *pool)
{
  const char *local_abspath;

  SVN_ERR(svn_dirent_get_absolute(&local_abspath, path, pool));

  if (!force && !keep_local)
    /* Verify that there are no "awkward" files */
    SVN_ERR(svn_client__can_delete(local_abspath, ctx, pool));

  if (!dry_run)
    /* Mark the entry for commit deletion and perform wc deletion */
    return svn_error_return(svn_wc_delete4(ctx->wc_ctx, local_abspath,
                                           keep_local, TRUE,
                                           ctx->cancel_func, ctx->cancel_baton,
                                           notify_func, notify_baton, pool));

  return SVN_NO_ERROR;
}

/* Callback baton for delete_with_write_lock_baton. */
struct delete_with_write_lock_baton
{
  const char *path;
  svn_boolean_t force;
  svn_boolean_t keep_local;
  svn_client_ctx_t *ctx;
};

/* Implements svn_wc__with_write_lock_func_t. */
static svn_error_t *
delete_with_write_lock_func(void *baton,
                            apr_pool_t *result_pool,
                            apr_pool_t *scratch_pool)
{
  struct delete_with_write_lock_baton *args = baton;

  /* Let the working copy library handle the PATH. */
  return svn_client__wc_delete(args->path, args->force,
                               FALSE, args->keep_local,
                               args->ctx->notify_func2,
                               args->ctx->notify_baton2,
                               args->ctx, scratch_pool);
}

svn_error_t *
svn_client_delete4(const apr_array_header_t *paths,
                   svn_boolean_t force,
                   svn_boolean_t keep_local,
                   const apr_hash_t *revprop_table,
                   svn_commit_callback2_t commit_callback,
                   void *commit_baton,
                   svn_client_ctx_t *ctx,
                   apr_pool_t *pool)
{
  svn_boolean_t is_url;
  int i;

  if (! paths->nelts)
    return SVN_NO_ERROR;

  SVN_ERR(svn_client__assert_homogeneous_target_type(paths));
  is_url = svn_path_is_url(APR_ARRAY_IDX(paths, 0, const char *));

  if (is_url)
    {
      SVN_ERR(delete_urls_multi_repos(paths, revprop_table, commit_callback,
                                      commit_baton, ctx, pool));
    }
  else
    {
      apr_pool_t *subpool = svn_pool_create(pool);

      for (i = 0; i < paths->nelts; i++)
        {
          struct delete_with_write_lock_baton dwwlb;
          const char *path = APR_ARRAY_IDX(paths, i, const char *);
          const char *local_abspath;

          svn_pool_clear(subpool);

          /* See if the user wants us to stop. */
          if (ctx->cancel_func)
            SVN_ERR(ctx->cancel_func(ctx->cancel_baton));

          SVN_ERR(svn_dirent_get_absolute(&local_abspath, path, subpool));
          dwwlb.path = path;
          dwwlb.force = force;
          dwwlb.keep_local = keep_local;
          dwwlb.ctx = ctx;
          SVN_ERR(svn_wc__call_with_write_lock(delete_with_write_lock_func,
                                               &dwwlb, ctx->wc_ctx,
                                               local_abspath, TRUE,
                                               pool, subpool));
        }
      svn_pool_destroy(subpool);
    }

  return SVN_NO_ERROR;
}
