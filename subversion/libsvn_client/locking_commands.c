/*
 * locking_commands.c:  Implementation of lock and unlock.
 *
 * ====================================================================
 * Copyright (c) 2000-2004 CollabNet.  All rights reserved.
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

#include "svn_client.h"
#include "client.h"
#include "svn_path.h"
#include "svn_pools.h"

#include "svn_private_config.h"


/*** Code. ***/

#if 0
/* ### This will probably not be necessary. */
/* Check if TARGET is out-of-date, meaning that it was committed to in a
   revision newer than REV. RA_LIB, SESSION is an open RA session pointing to
   TARGET's parent. */
static svn_error_t *check_out_of_date (svn_ra_plugin_t *ra_lib, void *session,
                                       svn_wc_entry_t *entry,
                                       const char *path,
                                       apr_pool_t *pool)
{
  apr_hash_t *dirents;
  svn_dirent_t *dirent;

  /* Get a directory listing of the target's parent. */
  SVN_ERR (ra_lib->get_dir (session, "", SVN_INVALID_REVNUM, &dirents, NULL,
                            NULL, pool));
  dirent = apr_hash_get(dirents, entry->name, APR_HASH_KEY_STRING);
  
  /* Schedule add is handled specially. */
  if (entry->schedule == svn_wc_schedule_add)
    {
      /* If the path exists, it is out-of-date in the WC. */
      if (dirent)
        return svn_error_createf (SVN_ERR_RA_OUT_OF_DATE, NULL,
                                  _("'%s' already exists in the repository"),
                                  svn_path_local_style (path, pool));
      else
        return SVN_NO_ERROR;
    }

  if (! dirent)
    return svn_error_createf (SVN_ERR_RA_OUT_OF_DATE, NULL,
                              _("'%s' does no longer exist in the repository"),
                              svn_path_local_style (path, pool));

  /* Path exists, check that our revision is the latest. */
  if (entry->revision < dirent->created_rev)
    return svn_error_createf (SVN_ERR_RA_OUT_OF_DATE, NULL,
                              _("The repository has a newer revision of '%s' "
                                "than the working copy"),
                              svn_path_local_style (path, pool));

  return SVN_NO_ERROR;
}
#endif

/* For use with store_locks_callback, below. */
struct lock_baton
{
  svn_lock_callback_t nested_callback;
  svn_wc_adm_access_t *adm_access;
  void *nested_baton;
  apr_pool_t *pool;
};


/* This callback is called by the ra_layer with BATON, the PATH being
 * locked, and the LOCK itself.  This function stores the locks in the
 * working copy.
 */
static svn_error_t *
store_locks_callback (void *baton, 
                      const char *path, 
                      const svn_lock_t *lock)
{
  struct lock_baton *lb = baton;
  svn_wc_adm_access_t *adm_access;
  const char *abs_path;
  
  abs_path = svn_path_join (svn_wc_adm_access_path (lb->adm_access), 
                            path, lb->pool);
  
  SVN_ERR (svn_wc_adm_probe_retrieve (&adm_access, lb->adm_access,
                                      abs_path, lb->pool));

  SVN_ERR (svn_wc_add_lock (abs_path, lock, adm_access, lb->pool));

  /* Call our callback, if we've got one. */
  if (lb->nested_callback)
    SVN_ERR (lb->nested_callback (lb->nested_baton, path, lock));

  return SVN_NO_ERROR;
}


/* Determine the nearest common parent of all paths in TARGETS,
 * setting PARENT_ENTRY_P and PARENT_ADM_ACCESS_P to the entry and
 * adm_access of the common parent.  PARENT_ADM_ACCESS_P is associated
 * with all other adm_access's that are locked in the working copy
 * while we lock the path in the repository.
 *
 * Each key in PATH_REVS_P is a path (relative to the common parent),
 * and each value is the corresponding base revision in the working
 * copy.
 */
static svn_error_t *
open_lock_targets (const svn_wc_entry_t **parent_entry_p,
                   svn_wc_adm_access_t **parent_adm_access_p,
                   apr_hash_t **path_revs_p,
                   apr_array_header_t *targets,
                   svn_client_ctx_t *ctx,
                   apr_pool_t *pool)
{
  int i;
  const char *common_parent;
  apr_array_header_t *paths = apr_array_make(pool, 1, sizeof(const char *));
  apr_hash_t *path_revs = apr_hash_make(pool);

  /* Get the common parent and all relative paths */
  SVN_ERR (svn_path_condense_targets (&common_parent, &paths, targets, 
                                      FALSE, pool));

  /* svn_path_condense_targets leaves PATHS empty if TARGETS only had
     1 member, so we special case that. */
  if (apr_is_empty_array (paths))
    {
      char *basename = svn_path_basename (common_parent, pool);
      common_parent = svn_path_dirname (common_parent, pool);

      APR_ARRAY_PUSH(paths, char *) = basename;
    }

  /* Open the common parent. */
  SVN_ERR (svn_wc_adm_probe_open3 (parent_adm_access_p, NULL, common_parent, 
                                   TRUE, 0, ctx->cancel_func, 
                                   ctx->cancel_baton, pool));  

  SVN_ERR (svn_wc_entry (parent_entry_p, common_parent, 
                         *parent_adm_access_p, FALSE, pool));

  /* Verify all paths. */
  for (i = 0; i < paths->nelts; i++)
    {
      svn_wc_adm_access_t *adm_access;
      svn_revnum_t *revnum;
      const svn_wc_entry_t *entry;
      const char *target = ((const char **) (targets->elts))[i];
      const char *abs_path;

      if (svn_path_is_url (target))
        return svn_error_createf (SVN_ERR_UNSUPPORTED_FEATURE, NULL,
                                  _("Locking non-local target '%s' "
                                    "not supported"),
                                  target);

      abs_path = svn_path_join (svn_wc_adm_access_path (*parent_adm_access_p), 
                                target, pool);

      SVN_ERR (svn_wc_adm_probe_try3 (&adm_access, *parent_adm_access_p,
                                      abs_path, TRUE, 0, ctx->cancel_func,
                                      ctx->cancel_baton, pool));

      SVN_ERR (svn_wc_entry (&entry, abs_path, adm_access, FALSE, pool));

      if (! entry)
        return svn_error_createf (SVN_ERR_UNVERSIONED_RESOURCE, NULL,
                                  _("'%s' is not under version control"), 
                                  svn_path_local_style (target, pool));
      if (! entry->url)
        return svn_error_createf (SVN_ERR_ENTRY_MISSING_URL, NULL,
                                  _("'%s' has no URL"),
                                  svn_path_local_style (target, pool));

      revnum = apr_palloc (pool, sizeof (* revnum));
      *revnum = entry->revision;

      apr_hash_set (path_revs, apr_pstrdup (pool, target),
                    APR_HASH_KEY_STRING, revnum);
    }

  *path_revs_p = path_revs;

  return SVN_NO_ERROR;
}


svn_error_t *
svn_client_lock (apr_array_header_t **locks_p, apr_array_header_t *targets,
                 const char *comment, svn_boolean_t force,
                 svn_lock_callback_t lock_func, void *lock_baton,
                 svn_client_ctx_t *ctx, apr_pool_t *pool)
{
  svn_wc_adm_access_t *adm_access;
  const svn_wc_entry_t *entry;
  svn_ra_session_t *ra_session;
  apr_array_header_t *locks;
  apr_hash_t *path_revs;
  struct lock_baton cb;

  SVN_ERR (open_lock_targets (&entry, &adm_access, &path_revs, 
                              targets, ctx, pool));

  /* Open an RA session to the common parent of TARGETS. */
  SVN_ERR (svn_client__open_ra_session (&ra_session, entry->url,
                                        svn_wc_adm_access_path (adm_access),
                                        NULL, NULL, FALSE, FALSE,
                                        ctx, pool));

  cb.pool = pool;
  cb.nested_callback = lock_func;
  cb.nested_baton = lock_baton;
  cb.adm_access = adm_access;

  /* Lock the paths. */
  SVN_ERR (svn_ra_lock (ra_session, &locks, path_revs, comment, 
                        force, store_locks_callback, &cb, pool));

  /* Unlock the wc. */
  svn_wc_adm_close (adm_access);

 /* ###TODO Since we've got callbacks all over the place, is there
    still any point in providing the list of locks back to our
    caller? */
  *locks_p = locks;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_unlock (const char *path, svn_boolean_t force,
                   svn_client_ctx_t *ctx, apr_pool_t *pool)
{
  svn_wc_adm_access_t *adm_access;
  const svn_wc_entry_t *entry;
  const char *lock_token;
  svn_ra_session_t *ra_session;

  /* ### TODO Support unlock on URL with --force. */
  if (svn_path_is_url (path))
    return svn_error_createf (SVN_ERR_UNSUPPORTED_FEATURE, NULL,
                              _("Unlocking non-local target '%s' not "
                                "supported"), path);
  SVN_ERR (svn_wc_adm_probe_open2 (&adm_access, NULL, path, TRUE, 0, pool));
  SVN_ERR (svn_wc_entry (&entry, path, adm_access, FALSE, pool));
  if (! entry)
    return svn_error_createf (SVN_ERR_UNVERSIONED_RESOURCE, NULL,
                              _("'%s' is not under version control"), 
                              svn_path_local_style (path, pool));
  if (! entry->url)
    return svn_error_createf (SVN_ERR_ENTRY_MISSING_URL, NULL,
                              _("'%s' has no URL"),
                              svn_path_local_style (path, pool));
  /* If not force, get the lock token from the WC entry. */
  if (! force)
    {
      if (! entry->lock_token)
        return svn_error_createf (SVN_ERR_CLIENT_MISSING_LOCK_TOKEN, NULL,
                                  _("'%s' is not locked in this working copy"),
                                  path);
      lock_token = entry->lock_token;
    }
  else
    {
      /* If breaking a lock, we shouldn't pass any lock token. */
      lock_token = NULL;
    }

  /* Open an RA session. */
  SVN_ERR (svn_client__open_ra_session (&ra_session, entry->url,
                                        svn_wc_adm_access_path (adm_access),
                                        adm_access, NULL, FALSE, FALSE,
                                        ctx, pool));

  /* Unlock the path. */
  SVN_ERR (svn_ra_unlock (ra_session, "", lock_token, force, pool));

  /* Remove any lock token from the WC. */
  SVN_ERR (svn_wc_remove_lock (path, adm_access, pool));

  return SVN_NO_ERROR;
}
