/*
 * ra_local.h : shared internal declarations for ra_local module
 *
 * ====================================================================
 * Copyright (c) 2000-2002 CollabNet.  All rights reserved.
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

#ifndef SVN_LIBSVN_RA_LOCAL_H
#define SVN_LIBSVN_RA_LOCAL_H

#include <apr_pools.h>
#include <apr_tables.h>

#include "svn_error.h"
#include "svn_string.h"
#include "svn_path.h"
#include "svn_delta.h"
#include "svn_fs.h"
#include "svn_repos.h"
#include "svn_ra.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/** Structures **/

/* A baton which represents a single ra_local session. */
typedef struct svn_ra_local__session_baton_t
{
  /* Each ra_local session does ALL allocation from this pool!  Kind
     of like an Apache transaction, I guess. :) */
  apr_pool_t *pool;
  
  /* A `file://' URL containing a local repository and path. */
  const char *repository_URL;

  /* The user accessing the repository. */
  char *username;

  /* The URL above, split into two components. */
  const char *repos_path;
  const char *fs_path;

  /* A repository object. */
  svn_repos_t *repos;

  /* The filesystem object associated with REPOS above (for
     convenience). */
  svn_fs_t *fs;

  /* Callback stuff. */
  const svn_ra_callbacks_t *callbacks;
  void *callback_baton;

} svn_ra_local__session_baton_t;




/*** Making changes to a filesystem, editor-style.  */

/* Hook function type for commits.  When a filesystem commit succeeds,
 * an instance of this is invoked on the NEW_REVISION, DATE, and
 * AUTHOR of the commit, along with the BATON closure.
 *
 * See also svn_ra_local__get_editor.
 *
 * NOTE: this "hook" is not related to the standard repository hooks
 * run before and after commits, which are configured in the
 * repository's conf/ subdirectory.  When most users say "hook",
 * they're talking about those, not about this function type.
 */
typedef svn_error_t *svn_ra_local__commit_hook_t (svn_revnum_t new_revision,
                                                  const char *date,
                                                  const char *author,
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
svn_ra_local__split_URL (const char **repos_path,
                         const char **fs_path,
                         const char *URL,
                         apr_pool_t *pool);




/* Recursively walk over REVNUM:PATH inside an already-open repository
   FS, and drive a checkout EDITOR.  URL is the base ancestry that
   will be stored in the working copy.  Allocate all data in POOL. */
svn_error_t *
svn_ra_local__checkout (svn_fs_t *fs, 
                        svn_revnum_t revnum, 
                        svn_boolean_t recurse,
                        const char *URL,
                        const char *fs_path,
                        const svn_delta_editor_t *editor, 
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
 * invoke HOOK with the new revision number, the commit date (as a
 * const char *), commit author (as a const char *), and HOOK_BATON
 * as arguments.  If HOOK returns an error, that error will be
 * returned from close_edit, otherwise close_edit will return
 * successfully (unless it encountered an error before invoking HOOK).
 *
 * NOTE: this HOOK is not related to the standard repository hooks
 * run before and after commits, which are configured in the
 * repository's conf/ subdirectory.  When most users say "hook",
 * they're referring to those, not to this HOOK argument.
 */
svn_error_t *svn_ra_local__get_editor (const svn_delta_editor_t **editor,
                                       void **edit_baton,
                                       svn_ra_local__session_baton_t *session,
                                       const char *log_msg,
                                       svn_ra_local__commit_hook_t *hook,
                                       void *hook_baton,
                                       apr_pool_t *pool);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_LIBSVN_RA_LOCAL_H */


/* ----------------------------------------------------------------
 * local variables:
 * eval: (load-file "../../tools/dev/svn-dev.el")
 * end:
 */
