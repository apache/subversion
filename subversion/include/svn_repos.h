/* svn_repos.h :  tools built on top of the filesystem.
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


/* The repository object. */
typedef struct svn_repos_t svn_repos_t;

/* Opening and creating repositories. */


/* Set *REPOS_P to a repository object for the repository at
   PATH.  Allocate *REPOS_P in POOL.

   Acquires a shared lock on the repository, and attaches a cleanup
   function to POOL to remove the lock.  If no lock can be acquired,
   returns error, with undefined effect on *REPOS_P.  If an exclusive
   lock is present, this blocks until it's gone.  */
svn_error_t *svn_repos_open (svn_repos_t **repos_p,
                             const char *path,
                             apr_pool_t *pool);

/* Create a new Subversion repository at PATH, building the necessary
   directory structure, creating the Berkeley DB filesystem
   environment, and so on.  Return the repository object in *REPOS_P,
   allocated in POOL. */
svn_error_t *svn_repos_create (svn_repos_t **repos_p, 
                               const char *path, 
                               apr_pool_t *pool);

/* Close the Subversion repository object REPOS. */
svn_error_t *svn_repos_close (svn_repos_t *repos);

/* Destroy the Subversion repository found at PATH.  Use POOL for any
   necessary allocations. */
svn_error_t *svn_repos_delete (const char *path, apr_pool_t *pool);

/* Return the filesystem associated with repository object REPOS. */
svn_fs_t *svn_repos_fs (svn_repos_t *repos);


/* Repository Paths */

/* Return the top-level repository path allocated in POOL. */
const char *svn_repos_path (svn_repos_t *repos, apr_pool_t *pool);

/* Return the path to REPOS's Berkeley DB environment, allocated in
   POOL. */
const char *svn_repos_db_env (svn_repos_t *repos, apr_pool_t *pool);

/* Return the path to REPOS's configuration directory, allocated in
   POOL. */
const char *svn_repos_conf_dir (svn_repos_t *repos, apr_pool_t *pool);

/* Return path to REPOS's lock directory or db lockfile, respectively,
   allocated in POOL. */
const char *svn_repos_lock_dir (svn_repos_t *repos, apr_pool_t *pool);
const char *svn_repos_db_lockfile (svn_repos_t *repos, apr_pool_t *pool);

/* Return the path to REPOS's hook directory, allocated in POOL. */
const char *svn_repos_hook_dir (svn_repos_t *repos, apr_pool_t *pool);

/* Return the path to REPOS's start-commit hook, pre-commit hook,
   post-commit hook, read sentinel, and write sentinel programs,
   respectively, allocated in POOL. */
const char *svn_repos_start_commit_hook (svn_repos_t *repos, apr_pool_t *pool);
const char *svn_repos_pre_commit_hook (svn_repos_t *repos, apr_pool_t *pool);
const char *svn_repos_post_commit_hook (svn_repos_t *repos, apr_pool_t *pool);
const char *svn_repos_read_sentinel_hook (svn_repos_t *repos, apr_pool_t *pool);
const char *svn_repos_write_sentinel_hook (svn_repos_t *repos, apr_pool_t *pool);



/* ---------------------------------------------------------------*/

/*** Reporting the state of a working copy, for updates. */


/* Construct and return a REPORT_BATON that will be paired with some
   svn_ra_reporter_t table.  The table and baton are used to build a
   transaction in the system;  when the report is finished,
   svn_repos_dir_delta is called on the transaction, driving
   EDITOR/EDIT_BATON. 

   Specifically, the report will create a transaction made by
   USERNAME, relative to FS_BASE in the filesystem.  TARGET is a
   single path component, used to limit the scope of the report to a
   single entry of FS_BASE, or NULL if all of FS_BASE itself is the
   main subject of the report.

   TGT_PATH and REVNUM is the fs path/revision pair that is the
   "target" of dir_delta.  In other words, a tree delta will be
   returned that transforms the transaction into TGT_PATH/REVNUM.
 
   TEXT_DELTAS instructs the driver of the EDITOR to enable to disable
   the generation of text deltas.

   RECURSE instructs the driver of the EDITOR to send a recursive
   delta (or not.)

   All allocation for the context and collected state will occur in
   POOL. */
svn_error_t *
svn_repos_begin_report (void **report_baton,
                        svn_revnum_t revnum,
                        const char *username,
                        svn_repos_t *repos,
                        const char *fs_base,
                        const char *target,
                        const char *tgt_path,
                        svn_boolean_t text_deltas,
                        svn_boolean_t recurse,
                        const svn_delta_edit_fns_t *editor,
                        void *edit_baton,
                        apr_pool_t *pool);


/* Given a REPORT_BATON constructed by svn_repos_begin_report(), this
   routine will build REVISION:PATH into the current transaction.
   This routine is called multiple times to create a transaction that
   is a "mirror" of a working copy.

   The first call of this in a given report usually passes an empty
   PATH; that allows the reporter to set up the correct root revision
   (useful when creating a txn, for example).  */
svn_error_t *svn_repos_set_path (void *report_baton,
                                 const char *path,
                                 svn_revnum_t revision);


/* Given a REPORT_BATON constructed by svn_repos_begin_report(), this
   routine will build REVISION:LINK_PATH into the current transaction
   at PATH. */
svn_error_t *svn_repos_link_path (void *report_baton,
                                  const char *path,
                                  const char *link_path,
                                  svn_revnum_t revision);

/* Given a REPORT_BATON constructed by svn_repos_begin_report(), this
   routine will remove PATH from the current fs transaction. 

   (This allows the reporter's driver to describe missing pieces of a
   working copy, so that 'svn up' can recreate them.) */   
svn_error_t *svn_repos_delete_path (void *report_baton,
                                    const char *path);

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

   If TEXT_DELTAS is FALSE, only a single NULL txdelta window will be
   sent to the window handler returned by EDITOR->apply_textdelta().

   If ENTRY_PROPS is TRUE, each open/added entry will be accompanied
   by propchange editor calls that relay special "entry props" (this
   is typically used only for working copy updates).

   USE_COPYFROM_ARGS determines whether or not the editor's add_file
   and add_directory functions will be called with copyfrom_*
   arguments.  That is to say, if a node that needs to be added can be
   optimized by simply copying another node that already exists in the
   source tree, svn_repos_dir_delta might ask that such a copy take
   place.

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
                     const char *src_parent_dir,
                     const char *src_entry,
                     apr_hash_t *src_revs,
                     svn_fs_root_t *tgt_root,
                     const char *tgt_path,
                     const svn_delta_edit_fns_t *editor,
                     void *edit_baton,
                     svn_boolean_t text_deltas,
                     svn_boolean_t recurse,
                     svn_boolean_t entry_props,
                     svn_boolean_t use_copyfrom_args,
                     apr_pool_t *pool);


/* ---------------------------------------------------------------*/

/*** Finding particular revisions. */

/* Set *REVISION to the revision number in REPOS's filesystem that was
   youngest at time TM. */
svn_error_t *
svn_repos_dated_revision (svn_revnum_t *revision,
                          svn_repos_t *repos,
                          apr_time_t tm,
                          apr_pool_t *pool);
                          

/*  Given a ROOT/PATH within some filesystem, return three pieces of
    information allocated in POOL:

      - set *COMMITTED_REV to the revision in which the object was
        last modified.  (In fs parlance, this is the revision in which
        the particular node-rev-id was 'created'.)
    
      - set *COMMITTED_DATE to the date of said revision.

      - set *LAST_AUTHOR to the author of said revision.    
 */
svn_error_t *
svn_repos_get_committed_info (svn_revnum_t *committed_rev,
                              svn_string_t **committed_date,
                              svn_string_t **last_author,
                              svn_fs_root_t *root,
                              const svn_string_t *path,
                              apr_pool_t *pool);

/* ### other queries we can do someday --

     * fetch the last revision created by <user>
         (once usernames become revision properties!)
     * fetch the last revision where <path> was modified
     
*/


/* ---------------------------------------------------------------*/

/*** Retrieving log messages. */


/* Invoke RECEIVER with RECEIVER_BATON on each log message from START
 * to END in REPOS's fileystem.  START may be greater or less than
 * END; this just controls whether the log messages are processed in
 * descending or ascending revision number order.
 *
 * If START or END is SVN_INVALID_REVNUM, it defaults to youngest.
 *
 * If PATHS is non-null and has one or more elements, then only show
 * revisions in which at least one of PATHS was changed (i.e., if
 * file, text or props changed; if dir, props changed or an entry was
 * added or deleted).  Each path is an (svn_stringbuf_t *)
 * representing an absolute path in the repository.
 *
 * ### todo: need to consider whether the above directory behavior is
 * most useful, or if we should actually treat _any_ node change in a
 * directory as a visible change for purposes of log... i.e., show
 * bubble-up.  The reason this might be useful is so that running log
 * on a directory would give a msg for every change under that dir,
 * no matter how far down.  See the thread started on the dev list by
 * Lars Kellogg-Stedman <lars@larsshack.org> with the subject
 * "Single repository, multiple projects?" for more.  We may simple
 * need to offer a few different semantics for PATHS.
 *
 * If DISCOVER_CHANGED_PATHS, then each call to RECEIVER passes a
 * `const apr_hash_t *' for the receiver's CHANGED_PATHS argument; the
 * hash's keys are all the paths committed in that revision.
 * Otherwise, each call to RECEIVER passes null for CHANGED_PATHS.
 *
 * ### NOTE: PATHS and DISCOVER_CHANGED_PATHS are currently ignored,
 * see http://subversion.tigris.org/issues/show_bug.cgi?id=562 for
 * more information.
 *
 * If any invocation of RECEIVER returns error, return that error
 * immediately and without wrapping it.
 *
 * See also the documentation for `svn_log_message_receiver_t'.
 *
 * Use POOL for temporary allocations.  */
svn_error_t *
svn_repos_get_logs (svn_repos_t *repos,
                    const apr_array_header_t *paths,
                    svn_revnum_t start,
                    svn_revnum_t end,
                    svn_boolean_t discover_changed_paths,
                    svn_log_message_receiver_t receiver,
                    void *receiver_baton,
                    apr_pool_t *pool);


/* ---------------------------------------------------------------*/

/*** Hook-sensitive wrappers for libsvn_fs routines. ***/


/* Like svn_fs_commit_txn(), but invoke the REPOS's pre- and
 * post-commit hooks around the commit.  Use TXN's pool for temporary
 * allocations.
 *
 * CONFLICT_P, NEW_REV, and TXN are as in svn_fs_commit_txn().  */
svn_error_t *svn_repos_fs_commit_txn (const char **conflict_p,
                                      svn_repos_t *repos,
                                      svn_revnum_t *new_rev,
                                      svn_fs_txn_t *txn);

/* Like svn_fs_begin_txn(), but use AUTHOR and LOG_MSG to set the
 * corresponding properties on transaction *TXN_P.  REPOS is the
 * repository object which contains the filesystem.  REV, *TXN_P, and
 * POOL are as in svn_fs_begin_txn().
 *
 * Before a txn is created, the repository's start-commit hooks are
 * run; if any of them fail, no txn is created, *TXN_P is
 * unaffected, and SVN_ERR_REPOS_HOOK_FAILURE is returned.
 *
 * LOG_MSG may be NULL to indicate the message is not (yet) available.
 * The caller will need to attach it to the transaction at a later time.  */
svn_error_t *svn_repos_fs_begin_txn_for_commit (svn_fs_txn_t **txn_p,
                                                svn_repos_t *repos,
                                                svn_revnum_t rev,
                                                const char *author,
                                                svn_string_t *log_msg,
                                                apr_pool_t *pool);


/* Like svn_fs_begin_txn(), but use AUTHOR to set the corresponding
 * property on transaction *TXN_P.  REPOS is the repository object
 * which contains the filesystem.  REV, *TXN_P, and POOL are as in
 * svn_fs_begin_txn().
 *
 * ### Someday: before a txn is created, some kind of read-hook could
 *              be called here. */
svn_error_t *svn_repos_fs_begin_txn_for_update (svn_fs_txn_t **txn_p,
                                                svn_repos_t *repos,
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
 * svn_repos_dir_delta(), the description of how to transform one tree
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

  /* The filesystem revision and path where this was copied from (if any) */
  svn_revnum_t copyfrom_rev;
  const char *copyfrom_path;

  /* Pointer to the next sibling of this node */
  struct svn_repos_node_t *sibling;

  /* Pointer to the first child of this node */
  struct svn_repos_node_t *child;

} svn_repos_node_t;


/* Set *EDITOR and *EDIT_BATON to an editor that, when driven by
   svn_repos_dir_delta(), builds an `svn_repos_node_t *' tree
   representing the delta from BASE_ROOT to ROOT in REPOS's filesystem.
   
   Invoke svn_repos_node_from_baton() on EDIT_BATON to obtain the root
   node afterwards.

   Note that the delta includes "bubbled-up" directories; that is,
   many of the directory nodes will have no prop_mods.

   Allocate the tree and its contents in NODE_POOL; do all other
   allocation in POOL.  */
svn_error_t *svn_repos_node_editor (const svn_delta_edit_fns_t **editor,
                                    void **edit_baton,
                                    svn_repos_t *repos,
                                    svn_fs_root_t *base_root,
                                    svn_fs_root_t *root,
                                    apr_pool_t *node_pool,
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
 * eval: (load-file "../../tools/dev/svn-dev.el")
 * end:
 */
