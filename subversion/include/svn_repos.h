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

/*** Making changes to a filesystem, editor-style.  */

/* Hook function type for commits.  When a filesystem commit happens,
 * one of these should be invoked on the NEW_REVISION that resulted
 * from the commit, and the BATON that was provided with the hook
 * originally.
 *
 * See also svn_repos_get_editor.
 *
 * NOTE: this "hook" is not related to the standard repository hooks
 * run before and after commits, which are configured in the
 * repository's conf/ subdirectory.  When most users say "hook",
 * they're talking about those, not about this function type.
 */
typedef svn_error_t *svn_repos_commit_hook_t (svn_revnum_t new_revision,
                                              void *baton);


/* Return an EDITOR and EDIT_BATON to commit changes to FS, beginning
 * at location `rev:BASE_PATH', where "rev" is the argument given to
 * replace_root().  Store USER as the author of the commit and
 * LOG_MSG as the commit message.
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
svn_error_t *svn_repos_get_editor (svn_delta_edit_fns_t **editor,
                                   void **edit_baton,
                                   svn_fs_t *fs,
                                   svn_stringbuf_t *base_path,
                                   const char *user,
                                   svn_stringbuf_t *log_msg,
                                   svn_repos_commit_hook_t *hook,
                                   void *hook_baton,
                                   apr_pool_t *pool);

/* ---------------------------------------------------------------*/

/*** Reporting the state of a working copy, for updates. */


/* Construct and return a REPORT_BATON to hold context while collecting
   working copy revision state. When the collection of state is completed,
   then the UPDATE_EDITOR will be driven to describe how to change the
   working copy into revision REVNUM of filesystem FS. The description of
   the working copy state will be relative to FS_BASE in the filesystem.

   All allocation for the context and collected state will occur in POOL. */
svn_error_t *
svn_repos_begin_report (void **report_baton,
                        svn_revnum_t revnum,
                        svn_fs_t *fs,
                        svn_stringbuf_t *fs_base,
                        const svn_delta_edit_fns_t *update_editor,
                        void *update_baton,
                        apr_pool_t *pool);


/* Given a REPORT_BATON constructed by svn_repos_begin_report(), this
   routine will build REVISION:PATH into the current transaction.
   This routine is called multiple times to create a transaction that
   is a "mirror" of a working copy.

   The first call of this in a given report usually passes an empty
   PATH; that allows the reporter to set up the correct root revision
   (useful when creating a txn, for example).  */
svn_error_t *
svn_repos_set_path (void *report_baton,
                    svn_stringbuf_t *path,
                    svn_revnum_t revision);


/* Given a REPORT_BATON constructed by svn_repos_begin_report(), this
   routine will remove PATH from the current fs transaction. 

   (This allows the reporter's driver to describe missing pieces of a
   working copy, so that 'svn up' can recreate them.) */   
svn_error_t *svn_repos_delete_path (void *report_baton,
                                    svn_stringbuf_t *path);

/* Make the filesystem compare the transaction to a revision and have
   it drive an update editor (using svn_repos_delta_dirs()).  Then
   abort the transaction. */
svn_error_t *svn_repos_finish_report (void *report_baton);


/* The report-driver is bailing, so abort the fs transaction. */
svn_error_t *svn_repos_abort_report (void *report_baton);


/* ---------------------------------------------------------------*/

/*** The magical dir_delta update routines. */


/* Compute the differences between directories SOURCE_PATH in
   SOURCE_ROOT and TARGET in TARGET_ROOT, and make calls describing
   those differences on EDITOR, using the provided EDIT_BATON.  Due to
   constraints of the editor architecture, the setting of the target
   revision via the editor will only occur if TARGET_ROOT is a
   revision root (which has a single global revision value).  So,
   currently, TARGET_ROOT is required to be a revision root.

   SOURCE_REV_DIFFS is a hash (whose keys are character string paths,
   and whose values are pointers to svn_revnum_t's) which describes
   the base revisions of the items in the SOURCE_PATH tree.  This hash
   need only contain the base revision for the top of the tree, and
   then those paths that have a base revision that differs from that
   of their parent directory.

   Before completing successfully, this function calls
   EDITOR->close_edit(), so the caller should expect its EDIT_BATON to
   be invalid after its use with this function.

   Do any allocation necessary for the delta computation in POOL.
   This function's maximum memory consumption is at most roughly
   proportional to the greatest depth of TARGET_PATH, not the total
   size of the delta.  */
svn_error_t *
svn_repos_dir_delta (svn_fs_root_t *source_root,
                     svn_stringbuf_t *source_path,
                     apr_hash_t *source_rev_diffs,
                     svn_fs_root_t *target_root,
                     svn_stringbuf_t *target_path,
                     const svn_delta_edit_fns_t *editor,
                     void *edit_baton,
                     apr_pool_t *pool);


/* Use the provided EDITOR and EDIT_BATON to describe the changes
   necessary for making a given node (and its descendants, if it a
   directory) under SOURCE_ROOT look exactly as it does under
   TARGET_ROOT.  ENTRY is the node to update, and is either NULL or a
   single path component.  If ENTRY is NULL, then compute the
   difference between the entire tree anchored at PARENT_DIR under
   SOURCE_ROOT and TARGET_ROOT.  Else, describe the changes needed to
   update only that entry in PARENT_DIR.  TARGET_ROOT is a revision
   root.

   SOURCE_REV_DIFFS is a hash (whose keys are character string paths,
   and whose values are pointers to svn_revnum_t's) which describes
   the base revisions of the items in the SOURCE_PARENT tree.  This hash
   need only contain the base revision for the top of that tree, and
   then those paths that have a base revision that differs from that
   of their parent directory.

   Before completing successfully, this function calls
   EDITOR->close_edit(), so the caller should expect its EDIT_BATON to
   be invalid after its use with this function.

   Do any allocation necessary for the delta computation in POOL.
   This function's maximum memory consumption is at most roughly
   proportional to the greatest depth of SOURCE_PATH under
   TARGET_ROOT, not the total size of the delta. 

   What's the difference between svn_repos_update and
   svn_repos_dir_delta?

   Say I have a Greek Tree at revision 1 in my working copy.  I type
   `svn up A/mu'.  svn_repos_dir_delta doesn't know what to do with
   files--it only takes directory args.  svn_repos_update, on the
   other hand, can handle this.

   Now, take the dir case.  Let's say that someone has removed the
   directory A/D/G and added a new file A/D/G.  I type `svn up A/D/G.'
   Once again, svn_repos_dir_delta would croak because it isn't
   looking at two directories.

   "So, why don't you just do the delta from one level higher," you
   might be tempted to say.

   "Fine," I reply, "but that means that everthing in A/D gets
   updated...this is NOT what I requested."
   
   So, what I really need is a way to say, "Mr. Update Editor Driver,
   I want you to have full knowledge of the directory A/D, but I need
   you promise to only pay attention to the entry G in that
   directory."
   
   And svn_repos_update complies. 

   TODO:  It is entirely likely that these two functions will become
   one in the near future, at least that is cmpilato's hope.  */
svn_error_t *
svn_repos_update (svn_fs_root_t *target_root,
                  svn_fs_root_t *source_root,
                  svn_stringbuf_t *parent_dir,
                  svn_stringbuf_t *entry,
                  apr_hash_t *source_rev_diffs,
                  const svn_delta_edit_fns_t *editor,
                  void *edit_baton,
                  apr_pool_t *pool);


/* ---------------------------------------------------------------*/

/*** Finding particular revisions. */

/* Set *REVISION to the revision number in FS that was youngest at
   time TM. */
svn_error_t *
svn_repos_dated_revision (svn_revnum_t *revision,
                          svn_fs_t *fs,
                          apr_time_t tm,
                          apr_pool_t *pool);
                          


/* ### other queries we can do someday --

     * fetch the last revision created by <user>
         (once usernames become revision properties!)
     * fetch the last revision where <path> was modified
     
*/


#endif /* SVN_REPOS_H */

#ifdef __cplusplus
}
#endif /* __cplusplus */


/* ----------------------------------------------------------------
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
