/*
 * add.c:  wrappers around wc add/mkdir functionality.
 *
 * ====================================================================
 * Copyright (c) 2000-2003 CollabNet.  All rights reserved.
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
#include "svn_wc.h"
#include "svn_client.h"
#include "svn_string.h"
#include "svn_pools.h"
#include "svn_error.h"
#include "svn_path.h"
#include "svn_io.h"
#include "client.h"



/*** Code. ***/

static svn_error_t *
add_dir_recursive (const char *dirname,
                   svn_wc_adm_access_t *adm_access,
                   svn_client_ctx_t *ctx,
                   apr_pool_t *pool)
{
  apr_dir_t *dir;
  apr_finfo_t this_entry;
  svn_error_t *err;
  apr_pool_t *subpool;
  apr_int32_t flags = APR_FINFO_TYPE | APR_FINFO_NAME;
  svn_wc_adm_access_t *dir_access;
  apr_array_header_t *ignores;

  /* Check cancellation; note that this catches recursive calls too. */
  if (ctx->cancel_func)
    SVN_ERR (ctx->cancel_func (ctx->cancel_baton));

  /* Add this directory to revision control. */
  SVN_ERR (svn_wc_add (dirname, adm_access,
                       NULL, SVN_INVALID_REVNUM,
                       ctx->cancel_func, ctx->cancel_baton,
                       ctx->notify_func, ctx->notify_baton, pool));

  SVN_ERR (svn_wc_adm_retrieve (&dir_access, adm_access, dirname, pool));

  SVN_ERR (svn_wc_get_default_ignores (&ignores, ctx->config, pool));

  /* Create a subpool for iterative memory control. */
  subpool = svn_pool_create (pool);

  /* Read the directory entries one by one and add those things to
     revision control. */
  SVN_ERR (svn_io_dir_open (&dir, dirname, pool));
  for (err = svn_io_dir_read (&this_entry, flags, dir, subpool);
       err == SVN_NO_ERROR;
       err = svn_io_dir_read (&this_entry, flags, dir, subpool))
    {
      const char *fullpath;

      /* Skip over SVN admin directories. */
      if (strcmp (this_entry.name, SVN_WC_ADM_DIR_NAME) == 0)
        continue;

      /* Skip entries for this dir and its parent.  */
      if (this_entry.name[0] == '.'
          && (this_entry.name[1] == '\0'
              || (this_entry.name[1] == '.' && this_entry.name[2] == '\0')))
        continue;

      if (svn_cstring_match_glob_list (this_entry.name, ignores))
        continue;

      /* Construct the full path of the entry. */
      fullpath = svn_path_join (dirname, this_entry.name, subpool);

      if (this_entry.filetype == APR_DIR)
        /* Recurse. */
        SVN_ERR (add_dir_recursive (fullpath, dir_access, ctx, subpool));

      else if (this_entry.filetype == APR_REG)
        SVN_ERR (svn_wc_add (fullpath, dir_access, NULL, SVN_INVALID_REVNUM,
                             ctx->cancel_func, ctx->cancel_baton,
                             ctx->notify_func, ctx->notify_baton, subpool));

      /* Clean out the per-iteration pool. */
      svn_pool_clear (subpool);
    }

  /* Check that the loop exited cleanly. */
  if (! (APR_STATUS_IS_ENOENT (err->apr_err)))
    {
      return svn_error_createf
        (err->apr_err, err,
         "error during recursive add of `%s'", dirname);
    }
  else  /* Yes, it exited cleanly, so close the dir. */
    {
      apr_status_t apr_err = apr_dir_close (dir);
      if (apr_err)
        return svn_error_createf
          (apr_err, NULL, "error closing dir `%s'", dirname);
    }

  /* Opened by svn_wc_add */
  SVN_ERR (svn_wc_adm_close (dir_access));

  /* Destroy the per-iteration pool. */
  svn_pool_destroy (subpool);

  return SVN_NO_ERROR;
}


/* The main logic of the public svn_client_add;  the only difference
   is that this function uses an existing access baton.
   (svn_client_add just generates an access baton and calls this func.) */
static svn_error_t *
add (const char *path, 
     svn_boolean_t recursive,
     svn_wc_adm_access_t *adm_access,
     svn_client_ctx_t *ctx,
     apr_pool_t *pool)
{
  svn_node_kind_t kind;
  svn_error_t *err;

  SVN_ERR (svn_io_check_path (path, &kind, pool));
  if ((kind == svn_node_dir) && recursive)
    err = add_dir_recursive (path, adm_access, ctx, pool);
  else
    err = svn_wc_add (path, adm_access, NULL, SVN_INVALID_REVNUM,
                      ctx->cancel_func, ctx->cancel_baton,
                      ctx->notify_func, ctx->notify_baton, pool);

  return err;
}



svn_error_t *
svn_client_add (const char *path, 
                svn_boolean_t recursive,
                svn_client_ctx_t *ctx,
                apr_pool_t *pool)
{
  svn_error_t *err, *err2;
  svn_wc_adm_access_t *adm_access;
  const char *parent_path = svn_path_dirname (path, pool);

  SVN_ERR (svn_wc_adm_open (&adm_access, NULL, parent_path,
                            TRUE, FALSE, pool));

  err = add (path, recursive, adm_access, ctx, pool);
  
  err2 = svn_wc_adm_close (adm_access);
  if (err2)
    {
      if (err)
        svn_error_clear (err2);
      else
        err = err2;
    }

  return err;
}


static svn_error_t *
path_driver_cb_func (void **dir_baton,
                     void *parent_baton,
                     void *callback_baton,
                     const char *path,
                     apr_pool_t *pool)
{
  const svn_delta_editor_t *editor = callback_baton;
  return editor->add_directory (path, parent_baton, NULL,
                                SVN_INVALID_REVNUM, pool, dir_baton);
}


static svn_error_t *
mkdir_urls (svn_client_commit_info_t **commit_info,
            const apr_array_header_t *paths,
            svn_client_ctx_t *ctx,
            apr_pool_t *pool)
{
  void *ra_baton, *session;
  svn_ra_plugin_t *ra_lib;
  const svn_delta_editor_t *editor;
  void *edit_baton;
  void *commit_baton;
  const char *log_msg;
  const char *auth_dir;
  apr_array_header_t *targets;
  const char *common;
  int i;

  /* Condense our list of mkdir targets. */
  SVN_ERR (svn_path_condense_targets (&common, &targets, paths, FALSE, pool));
  if (! targets->nelts)
    {
      const char *bname;
      svn_path_split (common, &common, &bname, pool);
      APR_ARRAY_PUSH (targets, const char *) = bname;
    }
  else
    {
      svn_boolean_t resplit = FALSE;

      /* We can't "mkdir" the root of an editor drive, so if one of
         our targets is the empty string, we need to back everything
         up by a path component. */
      for (i = 0; i < targets->nelts; i++)
        {
          const char *path = APR_ARRAY_IDX (targets, i, const char *);
          if (! *path)
            {
              resplit = TRUE;
              break;
            }
        }
      if (resplit)
        {
          const char *bname;
          svn_path_split (common, &common, &bname, pool);
          for (i = 0; i < targets->nelts; i++)
            {
              const char *path = APR_ARRAY_IDX (targets, i, const char *);
              path = svn_path_join (bname, path, pool);
              APR_ARRAY_IDX (targets, i, const char *) = path;
            }
        }
    }

  /* Create new commit items and add them to the array. */
  if (ctx->log_msg_func)
    {
      svn_client_commit_item_t *item;
      const char *tmp_file;
      apr_array_header_t *commit_items 
        = apr_array_make (pool, targets->nelts, sizeof (item));
          
      for (i = 0; i < targets->nelts; i++)
        {
          const char *path = APR_ARRAY_IDX (targets, i, const char *);
          item = apr_pcalloc (pool, sizeof (*item));
          item->url = svn_path_join (common, path, pool);
          item->state_flags = SVN_CLIENT_COMMIT_ITEM_ADD;
          APR_ARRAY_PUSH (commit_items, svn_client_commit_item_t *) = item;
        }
      SVN_ERR ((*ctx->log_msg_func) (&log_msg, &tmp_file, commit_items, 
                                     ctx->log_msg_baton, pool));
      if (! log_msg)
        return SVN_NO_ERROR;
    }
  else
    log_msg = "";

  /* Get the RA vtable that matches URL. */
  SVN_ERR (svn_ra_init_ra_libs (&ra_baton, pool));
  SVN_ERR (svn_ra_get_ra_library (&ra_lib, ra_baton, common, pool));

  /* Open an RA session for the URL. Note that we don't have a local
     directory, nor a place to put temp files or store the auth
     data, although we'll try to retrieve auth data from the
     current directory. */
  SVN_ERR (svn_client__dir_if_wc (&auth_dir, "", pool));
  SVN_ERR (svn_client__open_ra_session (&session, ra_lib, common, auth_dir,
                                        NULL, NULL, FALSE, TRUE,
                                        ctx, pool));

  /* URI-decode each target. */
  for (i = 0; i < targets->nelts; i++)
    {
      const char *path = APR_ARRAY_IDX (targets, i, const char *);
      path = svn_path_uri_decode (path, pool);
      APR_ARRAY_IDX (targets, i, const char *) = path;
    }

  /* Fetch RA commit editor */
  SVN_ERR (svn_client__commit_get_baton (&commit_baton, commit_info, pool));
  SVN_ERR (ra_lib->get_commit_editor (session, &editor, &edit_baton,
                                      log_msg, svn_client__commit_callback,
                                      commit_baton, pool));

  /* Call the path-based editor driver. */
  SVN_ERR (svn_delta_path_driver (editor, edit_baton, SVN_INVALID_REVNUM, 
                                  targets, path_driver_cb_func, 
                                  (void *)editor, pool));

  /* Close the edit. */
  SVN_ERR (editor->close_edit (edit_baton, pool));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_client_mkdir (svn_client_commit_info_t **commit_info,
                  const apr_array_header_t *paths,
                  svn_client_ctx_t *ctx,
                  apr_pool_t *pool)
{
  if (! paths->nelts)
    return SVN_NO_ERROR;
  
  if (svn_path_is_url (APR_ARRAY_IDX (paths, 0, const char *)))
    {
      SVN_ERR (mkdir_urls (commit_info, paths, ctx, pool));
    }
  else
    {
      /* This is a regular "mkdir" + "svn add" */
      apr_pool_t *subpool = svn_pool_create (pool);
      svn_error_t *err;
      int i;

      for (i = 0; i < paths->nelts; i++)
        {
          const char *path = APR_ARRAY_IDX (paths, i, const char *);

          SVN_ERR (svn_io_dir_make (path, APR_OS_DEFAULT, pool));
          err = svn_client_add (path, FALSE, ctx, pool);

          /* Trying to add a directory with the same name as a file that is
             scheduled for deletion is not supported.  Leaving an unversioned
             directory makes the working copy hard to use.  */
          if (err && err->apr_err == SVN_ERR_WC_NODE_KIND_CHANGE)
            {
              svn_error_t *err2 = svn_io_remove_dir (path, pool);
              if (err2)
                svn_error_clear (err2);
            }
          SVN_ERR (err);

          /* See if the user wants us to stop. */
          if (ctx->cancel_func)
            SVN_ERR (ctx->cancel_func (ctx->cancel_baton));
          svn_pool_clear (subpool);

        }
      svn_pool_destroy (subpool);
    }

  return SVN_NO_ERROR;
}
