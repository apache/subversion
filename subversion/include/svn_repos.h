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



#endif /* SVN_REPOS_H */

#ifdef __cplusplus
}
#endif /* __cplusplus */


/* ----------------------------------------------------------------
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
