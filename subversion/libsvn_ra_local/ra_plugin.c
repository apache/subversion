/*
 * ra_plugin.c : the main RA module for local repository access
 *
 * ====================================================================
 * Copyright (c) 2000-2001 CollabNet.  All rights reserved.
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

#include "ra_local.h"
#include "svn_ra.h"
#include "svn_repos.h"
#include "svn_pools.h"

/*----------------------------------------------------------------*/

/** Callbacks **/

/* This routine is originally passed as a "hook" to the filesystem
   commit editor.  When we get here, the track-editor has already
   stored committed targets inside the baton.
   
   Loop over all committed target paths within BATON, calling the
   clients' close_func() with NEW_REV. */

/* (This is a routine of type svn_repos_commit_hook_t) */
static svn_error_t *
cleanup_commit (svn_revnum_t new_rev, void *baton)
{
  int i;

  /* Recover our hook baton: */
  svn_ra_local__commit_closer_t *closer = 
    (svn_ra_local__commit_closer_t *) baton;

  /* Loop over the closer->targets array, and bump the revision number
     for each. */
  for (i = 0; i < closer->target_array->nelts; i++)
    {
      svn_stringbuf_t *target;
      target = (((svn_stringbuf_t **)(closer->target_array)->elts)[i]);

      if (closer->close_func)
        SVN_ERR (closer->close_func (closer->close_baton, target, new_rev));
    }    

  return SVN_NO_ERROR;
}



/* The reporter vtable needed by do_update() */

static const svn_ra_reporter_t ra_local_reporter = 
{
  svn_repos_set_path,
  svn_repos_delete_path,
  svn_repos_finish_report,
  svn_repos_abort_report
};



/*----------------------------------------------------------------*/

/** The RA plugin routines **/


static svn_error_t *
open (void **session_baton,
      svn_stringbuf_t *repos_URL,
      svn_ra_callbacks_t *callbacks,
      void *callback_baton,
      apr_pool_t *pool)
{
  svn_ra_local__session_baton_t *session;
  void *a, *auth_baton;
  svn_ra_username_authenticator_t *authenticator;

  /* Allocate and stash the session_baton args we have already. */
  session = apr_pcalloc (pool, sizeof(*session));
  session->pool = pool;
  session->repository_URL = repos_URL;
  
  /* Get the username by "pulling" it from the callbacks. */
  SVN_ERR (callbacks->get_authenticator (&a,
                                         &auth_baton, 
                                         SVN_RA_AUTH_USERNAME, 
                                         callback_baton, pool));

  authenticator = (svn_ra_username_authenticator_t *) a;

  SVN_ERR (authenticator->get_username (&(session->username),
                                        auth_baton, pool));

  /* Look through the URL, figure out which part points to the
     repository, and which part is the path *within* the
     repository. */
  SVN_ERR (svn_ra_local__split_URL (&(session->repos_path),
                                    &(session->fs_path),
                                    session->repository_URL,
                                    session->pool));

  /* Open the filesystem at located at environment `repos_path' */
  SVN_ERR (svn_repos_open (&(session->fs),
                           session->repos_path->data,
                           session->pool));

  /* ### ra_local is not going to bother to store the username in the
     working copy.  This means that the username will always be
     fetched from getuid() or from a commandline arg, which is fine.

     The reason for this decision is that in ra_local, authentication
     and authorization are blurred; we'd have to use authorization as
     a *test* to decide if the authentication was valid.  And we
     certainly don't want to track every subsequent svn_fs_* call's
     error, just to decide if it's legitmate to store a username! */

  *session_baton = session;
  return SVN_NO_ERROR;
}



static svn_error_t *
close (void *session_baton)
{
  svn_ra_local__session_baton_t *baton = 
    (svn_ra_local__session_baton_t *) session_baton;

  /* Close the repository filesystem, which will free any memory used
     by it. */
  SVN_ERR (svn_fs_close_fs (baton->fs));

  return SVN_NO_ERROR;
}




static svn_error_t *
get_latest_revnum (void *session_baton,
                   svn_revnum_t *latest_revnum)
{
  svn_ra_local__session_baton_t *baton = 
    (svn_ra_local__session_baton_t *) session_baton;

  SVN_ERR (svn_fs_youngest_rev (latest_revnum, baton->fs, baton->pool));

  return SVN_NO_ERROR;
}



static svn_error_t *
get_dated_revision (void *session_baton,
                    svn_revnum_t *revision,
                    apr_time_t time)
{
  svn_ra_local__session_baton_t *baton = 
    (svn_ra_local__session_baton_t *) session_baton;

  SVN_ERR (svn_repos_dated_revision (revision, baton->fs, time, baton->pool));

  return SVN_NO_ERROR;
}



static svn_error_t *
get_commit_editor (void *session_baton,
                   const svn_delta_edit_fns_t **editor,
                   void **edit_baton,
                   svn_stringbuf_t *log_msg,
                   svn_ra_get_wc_prop_func_t get_func,
                   svn_ra_set_wc_prop_func_t set_func,
                   svn_ra_close_commit_func_t close_func,
                   void *close_baton)
{
  svn_delta_edit_fns_t *commit_editor, *tracking_editor;
  const svn_delta_edit_fns_t *composed_editor;
  void *commit_editor_baton, *tracking_editor_baton, *composed_editor_baton;

  svn_ra_local__session_baton_t *sess_baton = 
    (svn_ra_local__session_baton_t *) session_baton;

  /* Construct a Magick commit-hook baton */
  svn_ra_local__commit_closer_t *closer
    = apr_pcalloc (sess_baton->pool, sizeof(*closer));

  closer->pool = sess_baton->pool;
  closer->close_func = close_func;
  closer->set_func = set_func;
  closer->close_baton = close_baton;
  closer->target_array = apr_array_make (sess_baton->pool, 1,
                                         sizeof(svn_stringbuf_t *));
                                         
  /* Get the repos commit-editor */     
  SVN_ERR (svn_repos_get_editor (&commit_editor, &commit_editor_baton,
                                 sess_baton->fs, 
                                 sess_baton->fs_path,
                                 sess_baton->username,
                                 log_msg,
                                 cleanup_commit, closer, /* fs will call
                                                            this when done.*/
                                 sess_baton->pool));

  /* Get the commit `tracking' editor, telling it to store committed
     targets inside our `closer' object, and NOT to bump revisions.
     (The FS editor will do this for us.)  */
  SVN_ERR (svn_delta_get_commit_track_editor (&tracking_editor,
                                              &tracking_editor_baton,
                                              sess_baton->pool,
                                              closer->target_array,
                                              SVN_INVALID_REVNUM,
                                              NULL, NULL));

  /* Set up a pipeline between the editors, creating a composed editor. */
  svn_delta_compose_editors (&composed_editor, &composed_editor_baton,
                             commit_editor, commit_editor_baton,
                             tracking_editor, tracking_editor_baton,
                             sess_baton->pool);

  /* Give the magic composed-editor back to the client */
  *editor = composed_editor;
  *edit_baton = composed_editor_baton;
  return SVN_NO_ERROR;
}



/* todo: the fs_path inside session_baton is currently in
   svn_path_url_style.  To be *formally* correct, this routine needs
   to dup that path and convert it to svn_path_repos_style.  That's
   the style that svn_ra_local__checkout expects in its starting path.
   We punt on this for now, since the two styles are equal at the
   moment. */
static svn_error_t *
do_checkout (void *session_baton,
             svn_revnum_t revision,
             const svn_delta_edit_fns_t *editor,
             void *edit_baton)
{
  svn_revnum_t revnum_to_fetch;
  svn_ra_local__session_baton_t *sbaton = 
    (svn_ra_local__session_baton_t *) session_baton;
  
  if (! SVN_IS_VALID_REVNUM(revision))
    SVN_ERR (get_latest_revnum (sbaton, &revnum_to_fetch));
  else
    revnum_to_fetch = revision;

  SVN_ERR (svn_ra_local__checkout (sbaton->fs,
                                   revnum_to_fetch,
                                   sbaton->repository_URL,
                                   sbaton->fs_path,
                                   editor, edit_baton, sbaton->pool));

  return SVN_NO_ERROR;
}



static svn_error_t *
do_update (void *session_baton,
           const svn_ra_reporter_t **reporter,
           void **report_baton,
           svn_revnum_t update_revision,
           svn_stringbuf_t *update_target,
           const svn_delta_edit_fns_t *update_editor,
           void *update_baton)
{
  svn_revnum_t revnum_to_update_to;
  svn_ra_local__session_baton_t *sbaton = session_baton;
  
  if (! SVN_IS_VALID_REVNUM(update_revision))
    SVN_ERR (get_latest_revnum (sbaton, &revnum_to_update_to));
  else
    revnum_to_update_to = update_revision;

  /* Pass back our reporter */
  *reporter = &ra_local_reporter;

  /* Build a reporter baton. */
  return svn_repos_begin_report (report_baton,
                                 revnum_to_update_to,
                                 sbaton->username,
                                 sbaton->fs, sbaton->fs_path,
                                 update_target, TRUE,
                                 update_editor, update_baton,
                                 sbaton->pool);
}


static svn_error_t *
do_status (void *session_baton,
           const svn_ra_reporter_t **reporter,
           void **report_baton,
           svn_stringbuf_t *status_target,
           const svn_delta_edit_fns_t *status_editor,
           void *status_baton)
{
  svn_revnum_t revnum_to_update_to;
  svn_ra_local__session_baton_t *sbaton = session_baton;
  
  SVN_ERR (get_latest_revnum (sbaton, &revnum_to_update_to));

  /* Pass back our reporter */
  *reporter = &ra_local_reporter;

  /* Build a reporter baton. */
  return svn_repos_begin_report (report_baton,
                                 revnum_to_update_to,
                                 sbaton->username,
                                 sbaton->fs, sbaton->fs_path,
                                 status_target, FALSE,
                                 status_editor, status_baton,
                                 sbaton->pool);
}



/*----------------------------------------------------------------*/

/** The ra_plugin **/

static const svn_ra_plugin_t ra_local_plugin = 
{
  "ra_local",
  "Module for accessing a repository on local disk.",
  open,
  close,
  get_latest_revnum,
  get_dated_revision,
  get_commit_editor,
  do_checkout,
  do_update,
  do_status
};


/*----------------------------------------------------------------*/

/** The One Public Routine, called by libsvn_client **/

svn_error_t *
svn_ra_local_init (int abi_version,
                   apr_pool_t *pool,
                   apr_hash_t *hash)
{
  apr_hash_set (hash, "file", APR_HASH_KEY_STRING, &ra_local_plugin);

  /* ben sez:  todo:  check that abi_version >=1. */

  return SVN_NO_ERROR;
}








/* ----------------------------------------------------------------
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
