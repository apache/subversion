/*
 * ra_local.h : shared internal declarations for ra_local module
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

#include <apr_pools.h>
#include <apr_tables.h>

#include "svn_error.h"
#include "svn_string.h"
#include "svn_path.h"
#include "svn_delta.h"
#include "svn_fs.h"
#include "svn_repos.h"
#include "svn_ra.h"



/** Structures **/

/* A baton which represents a single ra_local session. */
typedef struct svn_ra_local__session_baton_t
{
  /* Each ra_local session does ALL allocation from this pool!  Kind
     of like an Apache transaction, I guess. :) */
  apr_pool_t *pool;
  
  /* A `file://' URL containing a local repository and path. */
  svn_stringbuf_t *repository_URL;

  /* The user accessing the repository. */
  char *username;

  /* The URL above, split into two components. */
  svn_stringbuf_t *repos_path;
  svn_stringbuf_t *fs_path;

  /* A repository object. */
  svn_repos_t *repos;

  /* The filesystem object associated with REPOS above (for
     convenience). */
  svn_fs_t *fs;

} svn_ra_local__session_baton_t;



/* A device to record the targets of commits, and ensuring that proper
   commit closure happens on them (namely, revision setting and wc
   property setting).  This is passed to the `commit hook' routine by
   svn_fs_get_editor.  (   ) */
typedef struct svn_ra_local__commit_closer_t
{
  /* Allocation for this baton, as well as all committed_targets */
  apr_pool_t *pool;

  /* Target paths that are considered committed */
  apr_hash_t *committed_targets;

  /* The filesystem that we just committed to. */
  svn_fs_t *fs;

  /* A function given to RA by the client;  allows RA to bump WC
     revision numbers of targets. */
  svn_ra_close_commit_func_t close_func;
  
  /* A function given to RA by the client;  allows RA to store WC
     properties on targets.  (Wonder if ra_local will ever use this?!?) */
  svn_ra_set_wc_prop_func_t set_func;

  /* The baton to use with above functions */
  void *close_baton;

} svn_ra_local__commit_closer_t;




/*** Making changes to a filesystem, editor-style.  */

/* Hook function type for commits.  When a filesystem commit happens,
 * one of these should be invoked on the NEW_REVISION that resulted
 * from the commit, and the BATON that was provided with the hook
 * originally.
 *
 * See also svn_ra_local__get_editor.
 *
 * NOTE: this "hook" is not related to the standard repository hooks
 * run before and after commits, which are configured in the
 * repository's conf/ subdirectory.  When most users say "hook",
 * they're talking about those, not about this function type.
 */
typedef svn_error_t *svn_ra_local__commit_hook_t (svn_revnum_t new_revision,
                                                  void *baton);




/** Private routines **/

    


/* Given a `file://' URL, figure out which portion specifies a
   repository on local disk, and return in REPOS_PATH; return the
   remainder (the path *within* the repository's filesystem) in
   FS_PATH.  Allocate the return values in POOL.  Currently, we are
   not expecting to handle `file://hostname/'-type URLs; hostname, in
   this case, is expected to be the empty string.  Also, the path
   which follows the */
svn_error_t *
svn_ra_local__split_URL (svn_stringbuf_t **repos_path,
                         svn_stringbuf_t **fs_path,
                         svn_stringbuf_t *URL,
                         apr_pool_t *pool);




/* Recursively walk over REVNUM:PATH inside an already-open repository
   FS, and drive a checkout EDITOR.  URL is the base ancestry that
   will be stored in the working copy.  Allocate all data in POOL. */
svn_error_t *
svn_ra_local__checkout (svn_fs_t *fs, 
                        svn_revnum_t revnum, 
                        svn_boolean_t recurse,
                        svn_stringbuf_t *URL,
                        svn_stringbuf_t *fs_path,
                        const svn_delta_edit_fns_t *editor, 
                        void *edit_baton,
                        apr_pool_t *pool);


/* Return an EDITOR and EDIT_BATON to commit changes to SESSION->fs,
 * beginning at location `rev:SESSION->base_path', where "rev" is the
 * argument given to open_root().  Store SESSION->user as the
 * author of the commit and LOG_MSG as the commit message.
 *
 * FS is a previously opened file system.
 *
 * Calling (*EDITOR)->close_edit completes the commit.  Before
 * close_edit returns, but after the commit has succeeded, it will
 * invoke HOOK with the new revision number and HOOK_BATON as
 * arguments.  If HOOK returns an error, that error will be returned
 * from close_edit, otherwise close_edit will return successfully
 * (unless it encountered an error before invoking HOOK).
 *
 * NOTE: this HOOK is not related to the standard repository hooks
 * run before and after commits, which are configured in the
 * repository's conf/ subdirectory.  When most users say "hook",
 * they're referring to those, not to this HOOK argument.
 */
svn_error_t *svn_ra_local__get_editor (svn_delta_edit_fns_t **editor,
                                       void **edit_baton,
                                       svn_ra_local__session_baton_t *session,
                                       svn_stringbuf_t *log_msg,
                                       svn_ra_local__commit_hook_t *hook,
                                       void *hook_baton,
                                       apr_pool_t *pool);



/* Return an EDITOR and EDIT_BATON which "wrap" around a given
   UPDATE_EDITOR and UPDATE_EDIT_BATON.  SESSION is the currently open
   ra_local session object.

   The editor returned is a customized 'pipe' editor that slightly
   tweaks the way the UPDATE_EDITOR is driven; specifically, extra
   'entry props' are inserted into the stream whenever {open_root,
   open_file, open_dir, add_file, add_dir} are called.
*/
svn_error_t *
svn_ra_local__get_update_pipe_editor (svn_delta_edit_fns_t **editor,
                                      struct svn_pipe_edit_baton **edit_baton,
                                      const svn_delta_edit_fns_t *update_editor,
                                      void *update_edit_baton,
                                      svn_ra_local__session_baton_t *session,
                                      apr_pool_t *pool);






/* ----------------------------------------------------------------
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
