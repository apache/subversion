/* svn_repos.h :  tools built on top of the filesystem.
 *
 * ====================================================================
 * Copyright (c) 2000-2001 CollabNet.  All rights reserved.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution.  The terms
 * are also available at http://subversion.tigris.org/license-1.html.
 * If newer versions of this license are posted there, you may use a
 * newer version instead, at your option.
 * ====================================================================
 */

/* ==================================================================== */


#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#ifndef SVN_REPOS_H
#define SVN_REPOS_H

#include <apr_pools.h>
#include <apr_hash.h>
#include "svn_fs.h"
#include "svn_delta.h"
#include "svn_types.h"
#include "svn_error.h"


/* ---------------------------------------------------------------*/

/* Making changes to a filesystem, editor-style.  */

/* Hook function type for commits.  When a filesystem commit happens,
 * one of these should be invoked on the NEW_REVISION that resulted
 * from the commit, and the BATON that was provided with the hook
 * originally.
 *
 * See svn_repos_get_editor for an example user.
 */
typedef svn_error_t *svn_repos_commit_hook_t (svn_revnum_t new_revision,
                                              void *baton);


/* Return an EDITOR and EDIT_BATON to commit changes to FS, beginning
 * at location `rev:BASE_PATH', where "rev" is the argument given to
 * replace_root().  Store LOG_MSG as the commit message.
 *
 * FS is assumed to be a previously opened file system.
 *
 * Calling (*EDITOR)->close_edit completes the commit.  Before
 * close_edit returns, but after the commit has succeeded, it will
 * invoke HOOK with the new revision number and HOOK_BATON as
 * arguments.  If HOOK returns an error, that error will be returned
 * from close_edit, otherwise close_edit will return successfully
 * (unless it encountered an error before invoking HOOK).  */
svn_error_t *svn_repos_get_editor (svn_delta_edit_fns_t **editor,
                                   void **edit_baton,
                                   svn_fs_t *fs,
                                   svn_string_t *base_path,
                                   svn_string_t *log_msg,
                                   svn_repos_commit_hook_t *hook,
                                   void *hook_baton,
                                   apr_pool_t *pool);

/* ---------------------------------------------------------------*/

/* The `reporter' vtable routines (for updates). */


/* A structure used by the routines within the `reporter' vtable,
   driven by the client as it describes its working copy revisions. */
typedef struct svn_repos_report_baton_t
{
  /* The transaction being built in the repository, a mirror of the
     working copy. */
  svn_fs_t *fs;
  svn_fs_txn_t *txn;
  svn_fs_root_t *txn_root;

  /* The location under which all reporting will happen (in the fs) */
  svn_string_t *base_path;

  /* finish_report() calls svn_fs_dir_delta(), and uses this arg to
     decide which revision to compare the transaction against. */
  svn_revnum_t revnum_to_update_to;

  /* The working copy editor driven by svn_fs_dir_delta(). */
  const svn_delta_edit_fns_t *update_editor;
  void *update_edit_baton;

  /* This hash describes the mixed revisions in the transaction; it
     maps pathnames (char *) to revision numbers (svn_revnum_t). */
  apr_hash_t *path_rev_hash;

  /* Pool from the session baton. */
  apr_pool_t *pool;

} svn_repos_report_baton_t;



/* Given a properly constructed REPORT_BATON, this routine will build
   REVISION:PATH into the current transaction.  This routine is called
   multiple times to create a transaction that is a "mirror" of a
   working copy. */
svn_error_t *
svn_repos_set_path (void *report_baton,
                    svn_string_t *path,
                    svn_revnum_t revision);


/* Make the filesystem compare the transaction to a revision and have
   it drive an update editor (using svn_repos_delta_dirs()).  Then
   abort the transaction. */
svn_error_t *
svn_repos_finish_report (void *report_baton);



#endif /* SVN_REPOS_H */

#ifdef __cplusplus
}
#endif /* __cplusplus */


/* ----------------------------------------------------------------
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
