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


svn_error_t *
svn_client_lock (const svn_lock_t **lock_p, const char *path,
                 const char *comment, svn_boolean_t force,
                 svn_client_ctx_t *ctx, apr_pool_t *pool)
{
  svn_wc_adm_access_t *adm_access;
  const svn_wc_entry_t *entry;
  svn_ra_plugin_t *ra_lib;
  void *ra_baton, *session;
  svn_lock_t *lock;

  if (svn_path_is_url (path))
    return svn_error_createf (SVN_ERR_UNSUPPORTED_FEATURE, NULL,
                              _("Locking non-local target '%s' not supported"),
                              path);

  SVN_ERR (svn_wc_adm_probe_open2 (&adm_access, NULL, path, TRUE, 0, pool));
  SVN_ERR (svn_wc_entry (&entry, path, adm_access, FALSE, pool));
  if (! entry)
    return svn_error_createf (SVN_ERR_UNVERSIONED_RESOURCE, NULL,
                              _("'%s' is not under version control"), 
                              svn_path_local_style (path, pool));
  if (! entry->url)
    return svn_error_createf (SVN_ERR_ENTRY_MISSING_URL, NULL,
                              "'%s' has no URL");

  /* Open an RA session. */
  SVN_ERR (svn_ra_init_ra_libs (&ra_baton, pool));
  SVN_ERR (svn_ra_get_ra_library (&ra_lib, ra_baton, entry->url, pool));
  SVN_ERR (svn_client__open_ra_session (&session, ra_lib, entry->url,
                                        svn_wc_adm_access_path (adm_access),
                                        adm_access, NULL, FALSE, FALSE,
                                        ctx, pool));

  /* Lock the path. */
  /* ### TO-DO: Need to pass our revision of the path here when the API is
     changed. */
  SVN_ERR (ra_lib->lock (session, &lock, "", comment, force, pool));

  /* Store the lock token in the entry and optionally make file writeable. */
  SVN_ERR (svn_wc_add_lock (path, lock, adm_access, pool));

  *lock_p = lock;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_unlock (const char *path, svn_boolean_t force,
                   svn_client_ctx_t *ctx, apr_pool_t *pool)
{
  svn_wc_adm_access_t *adm_access;
  const svn_wc_entry_t *entry;
  const char *lock_token;
  svn_lock_t *lock;
  svn_ra_plugin_t *ra_lib;
  void *ra_baton, *session;

  /* ### Support unlock on URL with --force? */
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
                              "'%s' has no URL");
  /* If not force, get the lock token from the WC entry. */
  if (! force)
    {
      if (! entry->lock_token)
        return svn_error_createf (SVN_ERR_CLIENT_MISSING_LOCK_TOKEN, NULL,
                                  _("'%s' is not locked in this working copy"),
                                  path);
      lock_token = entry->lock_token;
    }

  /* Open an RA session. */
  SVN_ERR (svn_ra_init_ra_libs (&ra_baton, pool));
  SVN_ERR (svn_ra_get_ra_library (&ra_lib, ra_baton, entry->url, pool));
  SVN_ERR (svn_client__open_ra_session (&session, ra_lib, entry->url,
                                        svn_wc_adm_access_path (adm_access),
                                        adm_access, NULL, FALSE, FALSE,
                                        ctx, pool));

  /* If force, get the lock token from the repository. */
  if (force)
    {
      SVN_ERR (ra_lib->get_lock (session, &lock, "", pool));
      if (lock)
        lock_token = lock->token;
      else
        lock_token = NULL;
    }

  /* Unlock the path. */
  /* We don't have a lock token if force is specified, but the path wasn't
     locked in the repository. */
  if (lock_token)
    SVN_ERR (ra_lib->unlock (session, lock_token, force, pool));

  /* Remove any lock token from the WC. */
  SVN_ERR (svn_wc_remove_lock (path, adm_access, pool));

  return SVN_NO_ERROR;
}
