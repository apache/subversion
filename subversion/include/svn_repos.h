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


/* Opening a filesystem. */

/* Set *FS_P to an opened filesystem object for the repository at
   PATH.  Allocate *FS_P in POOL.

   Acquires a shared lock on the repository, and attaches a cleanup
   function to POOL to remove the lock.  If no lock can be acquired,
   returns error, with undefined effect on *FS_P.  If an exclusive
   lock is present, this blocks until it's gone.  */
svn_error_t *svn_repos_open (svn_fs_t **fs_p,
                             const char *path,
                             apr_pool_t *pool);



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
   the working copy state will be relative to FS_BASE in the
   filesystem.  USERNAME will be recorded as the creator of the
   temporary fs txn.

   All allocation for the context and collected state will occur in POOL. */
svn_error_t *
svn_repos_begin_report (void **report_baton,
                        svn_revnum_t revnum,
                        const char *username,
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

/* Use the provided EDITOR and EDIT_BATON to describe the changes
   necessary for making a given node (and its descendants, if it a
   directory) under SRC_ROOT look exactly like TGT_PATH under
   TGT_ROOT.  SRC_ENTRY is the node to update, and is either NULL or a
   single path component.  If SRC_ENTRY is NULL, then compute the
   difference between the entire tree anchored at SRC_PARENT_DIR under
   SRC_ROOT and TGT_PATH under TARGET_ROOT.  Else, describe the
   changes needed to update only that entry in SRC_PARENT_DIR.
   Typically, callers of this function will use a TGT_PATH that is the
   concatenation of SRC_PARENT_DIR and SRC_ENTRY.

   SRC_ROOT and TGT_ROOT can both be either revision or transaction
   roots.  If TGT_ROOT is a revision, EDITOR's set_target_revision()
   will be called with the TGT_ROOT's revision number, else it will
   not be called at all.

   SRC_REVS is a hash whose keys are character string paths, and whose
   values are pointers to svn_revnum_t's, which describes the base
   revisions of the items in the SRC_PARENT tree.  This hash need only
   contain the base revision for the top of that tree, and then those
   paths that have a base revision that differs from that of their
   parent directory.

   Before completing successfully, this function calls EDITOR's
   close_edit(), so the caller should expect its EDIT_BATON to be
   invalid after its use with this function.

   Do any allocation necessary for the delta computation in POOL.
   This function's maximum memory consumption is at most roughly
   proportional to the greatest depth of the tree under TGT_ROOT, not
   the total size of the delta.
*/
svn_error_t *
svn_repos_dir_delta (svn_fs_root_t *src_root,
                     svn_stringbuf_t *src_parent_dir,
                     svn_stringbuf_t *src_entry,
                     apr_hash_t *src_revs,
                     svn_fs_root_t *tgt_root,
                     svn_stringbuf_t *tgt_path,
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

/* ---------------------------------------------------------------*/

/*** Hook-sensitive wrappers for libsvn_fs routines. ***/


/* Like svn_fs_commit_txn(), but invoke the repository's pre- and
 * post-commit hooks around the commit.  Use TXN's pool for temporary
 * allocations.
 *
 * CONFLICT_P, NEW_REV, and TXN are as in svn_fs_commit_txn().
 */
svn_error_t *svn_repos_fs_commit_txn (const char **conflict_p,
                                      svn_revnum_t *new_rev,
                                      svn_fs_txn_t *txn);

/* Like svn_fs_begin_txn(), but use AUTHOR and LOG_MSG to set the
 * corresponding properties on transaction *TXN_P.  FS, REV, *TXN_P,
 * and POOL are as in svn_fs_begin_txn().
 *
 * Before a txn is created, the repository's start-commit hooks are
 * run; if any of them fail, no txn is created, *TXN_P is
 * unaffected, and SVN_ERR_REPOS_HOOK_FAILURE is returned.
 */
svn_error_t *svn_repos_fs_begin_txn_for_commit (svn_fs_txn_t **txn_p,
                                                svn_fs_t *fs,
                                                svn_revnum_t rev,
                                                const char *author,
                                                svn_string_t *log_msg,
                                                apr_pool_t *pool);


/* Like svn_fs_begin_txn(), but use AUTHOR to set the corresponding
 * property on transaction *TXN_P.  FS, REV, *TXN_P, and POOL are as
 * in svn_fs_begin_txn().
 *
 * ### Someday: before a txn is created, some kind of read-hook could
 *              be called here. */
svn_error_t *svn_repos_fs_begin_txn_for_update (svn_fs_txn_t **txn_p,
                                                svn_fs_t *fs,
                                                svn_revnum_t rev,
                                                const char *author,
                                                apr_pool_t *pool);



/* ---------------------------------------------------------------*/

/*** Data structures and editor things for repository inspection. ***/

/* As it turns out, the svn_repos_dir_delta() interface can be
 * extremely useful for examining the repository, or more exactly,
 * changes to the repository.  svn_repos_dir_delta() allows for
 * differences between two trees to be described using an editor.
 *
 * By using the specific editor found below in conjunction with
 * svn_repos_dir_dlta(), the description of how to transform one tree
 * into another can be used to build an in-memory linked-list tree,
 * which each node representing a repository node that was changed as a
 * result of having svn_repos_dir_delta() drive that editor.
 */

typedef struct svn_repos_node_t
{
  /* Node type (file, dir, etc.) */
  enum svn_node_kind kind;

  /* How this node entered the node tree: 'A'dd, 'D'elete, 'R'eplace */
  char action; 

  /* Were there any textual mods? (files only) */
  svn_boolean_t text_mod;

  /* Where there any property mods? */
  svn_boolean_t prop_mod;

  /* The name of this node as it appears in its parent's entries list */
  const char *name;

  /* The repository node ID for this node */
  svn_fs_id_t *id;

  /* Pointer to the next sibling of this node */
  struct svn_repos_node_t *sibling;

  /* Pointer to the first child of this node */
  struct svn_repos_node_t *child;

} svn_repos_node_t;


/* Return an EDITOR that, when driving by svn_repos_dir_delta(),
   builds an in-memory linked-list tree of svn_repos_node_t structures
   representing the delta between the trees found under ROOT and
   BASE_ROOT in FS.  The root node of the linked-list tree lives in
   EDIT_BATON, and can be accessed with svn_repos_node_from_baton(). */
svn_error_t *svn_repos_node_editor (const svn_delta_edit_fns_t **editor,
                                    void **edit_baton,
                                    svn_fs_t *fs,
                                    svn_fs_root_t *root,
                                    svn_fs_root_t *base_root,
                                    apr_pool_t *pool);

/* Return the root node of the linked-list tree generated by driving
   the editor created by svn_repos_node_editor() with
   svn_repos_dir_delta(), which is stored in EDIT_BATON.  This is only
   really useful if used *after* the editor drive is completed. */
svn_repos_node_t *svn_repos_node_from_baton (void *edit_baton);




#endif /* SVN_REPOS_H */

#ifdef __cplusplus
}
#endif /* __cplusplus */


/* ----------------------------------------------------------------
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
