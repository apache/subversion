/**
 * @copyright
 * ====================================================================
 * Copyright (c) 2000-2003 CollabNet.  All rights reserved.
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
 * @endcopyright
 *
 * @file svn_repos.h
 * @brief tools built on top of the filesystem.
 */


#ifndef SVN_REPOS_H
#define SVN_REPOS_H

#include <apr_pools.h>
#include <apr_hash.h>
#include "svn_fs.h"
#include "svn_delta.h"
#include "svn_types.h"
#include "svn_error.h"


#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* ---------------------------------------------------------------*/


/** The repository object. */
typedef struct svn_repos_t svn_repos_t;

/* Opening and creating repositories. */


/** Set @a *repos_p to a repository object for the repository at @a path.
 *
 * Allocate @a *repos_p in @a pool.
 *
 * Acquires a shared lock on the repository, and attaches a cleanup
 * function to @a pool to remove the lock.  If no lock can be acquired,
 * returns error, with undefined effect on @a *repos_p.  If an exclusive
 * lock is present, this blocks until it's gone.
 */
svn_error_t *svn_repos_open (svn_repos_t **repos_p,
                             const char *path,
                             apr_pool_t *pool);

/** Create a new Subversion repository at @a path.
 *
 * Create a new Subversion repository at @a path, building the necessary
 * directory structure, creating the Berkeley DB filesystem environment, 
 * and so on.  Return the (open) repository object in @a *repos_p,
 * allocated in @a pool.
 *
 * The directory structure will be created based on the @a on_disk_template
 * argument. If NULL, then the default template will be used (or an
 * internal, hard-coded fallback if the default cannot be found). If
 * the argument contains a path separate ('/'), then it is assumed to
 * specify a template directory. Otherwise, it is assumed to be the name
 * of a template from the standard set of installed templates.
 *
 * Once the repository directory has been constructed, and the Berkeley DB
 * filesystem set up, then the @a in_repos_template will be used to
 * specify an initial structure to import into the repository. Hooks will
 * not be run for this import. The argument specifies a template name or
 * a path to a template, similar to @a on_disk_template. If NULL is passed,
 * then nothing will be imported.
 *
 * @a config is a client configuration hash of @c svn_config_t * items
 * keyed on config category names, and may be NULL.
 *
 * @a fs_config is passed to the filesystem, and may be NULL.
 */
svn_error_t *svn_repos_create (svn_repos_t **repos_p, 
                               const char *path,
                               const char *on_disk_template,
                               const char *in_repos_template,
                               apr_hash_t *config,
                               apr_hash_t *fs_config,
                               apr_pool_t *pool);

/** Destroy the Subversion repository found at @a path, using @a pool for any
 * necessary allocations.
 */
svn_error_t *svn_repos_delete (const char *path, apr_pool_t *pool);

/** Return the filesystem associated with repository object @a repos. */
svn_fs_t *svn_repos_fs (svn_repos_t *repos);

/** Run database recovery procedures on the repository at @a path,
 * returning the database to a consistent state.
 *
 * Run database recovery procedures on the repository at @a path,
 * returning the database to a consistent state.  Use @a pool for all
 * allocation.
 *
 * Acquires an @a exclusive lock on the repository, recovers the
 * database, and releases the lock.  If an exclusive lock can't be
 * acquired, returns error.
 */
svn_error_t *svn_repos_recover (const char *path, apr_pool_t *pool);



/* Repository Paths */

/** Return the top-level repository path allocated in @a pool. */
const char *svn_repos_path (svn_repos_t *repos, apr_pool_t *pool);

/** Return the path to @a repos's Berkeley DB environment, allocated in
 * @a pool.
 */
const char *svn_repos_db_env (svn_repos_t *repos, apr_pool_t *pool);

/** Return path to @a repos's lock directory, allocated in @a pool. */
const char *svn_repos_lock_dir (svn_repos_t *repos, apr_pool_t *pool);

/** Return path to @a repos's db lockfile, allocated in @a pool. */
const char *svn_repos_db_lockfile (svn_repos_t *repos, apr_pool_t *pool);

/** Return the path to @a repos's hook directory, allocated in @a pool. */
const char *svn_repos_hook_dir (svn_repos_t *repos, apr_pool_t *pool);

/** Return the path to @a repos's start-commit hook, allocated in @a pool. */
const char *svn_repos_start_commit_hook (svn_repos_t *repos, apr_pool_t *pool);

/** Return the path to @a repos's pre-commit hook, allocated in @a pool. */
const char *svn_repos_pre_commit_hook (svn_repos_t *repos, apr_pool_t *pool);

/** Return the path to @a repos's post-commit hook, allocated in @a pool. */
const char *svn_repos_post_commit_hook (svn_repos_t *repos, apr_pool_t *pool);

/** Return the path to @a repos's pre-revprop-change hook, allocated in 
 * @a pool.
 */
const char *svn_repos_pre_revprop_change_hook (svn_repos_t *repos,
                                               apr_pool_t *pool);

/** Return the path to @a repos's post-revprop-change hook, allocated in 
 * @a pool.
 */
const char *svn_repos_post_revprop_change_hook (svn_repos_t *repos,
                                                apr_pool_t *pool);



/* ---------------------------------------------------------------*/

/* Reporting the state of a working copy, for updates. */


/** Construct and return a @a report_baton that will be paired with some
 * @c svn_ra_reporter_t table.
 *
 * Construct and return a @a report_baton that will be paired with some
 * @c svn_ra_reporter_t table.  The table and baton are used to build a
 * transaction in the system;  when the report is finished,
 * @c svn_repos_dir_delta is called on the transaction, driving
 * @a editor/@a edit_baton. 
 *
 * Specifically, the report will create a transaction made by @a username, 
 * relative to @a fs_base in the filesystem.  @a target is a single path 
 * component, used to limit the scope of the report to a single entry of 
 * @a fs_base, or @c NULL if all of @a fs_base itself is the main subject 
 * of the report.
 *
 * @a tgt_path and @a revnum is the fs path/revision pair that is the
 * "target" of @c dir_delta.  In other words, a tree delta will be
 * returned that transforms the transaction into @a tgt_path/@a revnum.
 * @a tgt_path may (indeed, should) be @c NULL when the source and target
 * paths of the report are the same.  That is, @a tgt_path should *only*
 * be specified when specifying that the resultant editor drive be one
 * that transforms the reported hierarchy into a pristine tree of
 * @a tgt_path at revision @a revnum.  Else, a @c NULL value for @a tgt_path 
 * will indicate that the editor should be driven in such a way as to
 * transform the reported hierarchy to revision @a revnum, preserving the
 * reported hierarchy.
 *
 * @a text_deltas instructs the driver of the @a editor to enable to disable
 * the generation of text deltas.
 *
 * @a recurse instructs the driver of the @a editor to send a recursive
 * delta (or not.)
 *
 * All allocation for the context and collected state will occur in
 * @a pool.
 */
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
                        const svn_delta_editor_t *editor,
                        void *edit_baton,
                        apr_pool_t *pool);


/** Given a @a report_baton constructed by @c svn_repos_begin_report(), this
 * routine will build @a revision:@a path into the current transaction.
 *
 * Given a @a report_baton constructed by @c svn_repos_begin_report(), this
 * routine will build @a revision:@a path into the current transaction.
 * This routine is called multiple times to create a transaction that
 * is a "mirror" of a working copy.
 *
 * The first call of this in a given report usually passes an empty
 * @a path; that allows the reporter to set up the correct root revision
 * (useful when creating a txn, for example).
 *
 * All temporary allocations are done in @a pool.
 */
svn_error_t *svn_repos_set_path (void *report_baton,
                                 const char *path,
                                 svn_revnum_t revision,
                                 apr_pool_t *pool);


/** Given a @a report_baton constructed by @c svn_repos_begin_report(), 
 * this routine will build @a revision:@a link_path into the current 
 * transaction at @a path.
 *
 * Given a @a report_baton constructed by @c svn_repos_begin_report(), 
 * this routine will build @a revision:@a link_path into the current 
 * transaction at @a path.  Note that while @a path is relative to the 
 * anchor/target used in the creation of the @a report_baton, @a link_path 
 * is an absolute filesystem path!
 *
 * All temporary allocations are done in @a pool.
 */
svn_error_t *svn_repos_link_path (void *report_baton,
                                  const char *path,
                                  const char *link_path,
                                  svn_revnum_t revision,
                                  apr_pool_t *pool);

/** Given a @a report_baton constructed by @c svn_repos_begin_report(), 
 * this routine will remove @a path from the current fs transaction. 
 *
 * Given a @a report_baton constructed by @c svn_repos_begin_report(), 
 * this routine will remove @a path from the current fs transaction. 
 *
 * (This allows the reporter's driver to describe missing pieces of a
 * working copy, so that 'svn up' can recreate them.)
 *
 * All temporary allocations are done in @a pool.
 */
svn_error_t *svn_repos_delete_path (void *report_baton,
                                    const char *path,
                                    apr_pool_t *pool);

/** Make the filesystem compare the transaction to a revision and have
 * it drive an update editor (using @c svn_repos_delta_dirs()), then
 * abort the transaction.
 */
svn_error_t *svn_repos_finish_report (void *report_baton);


/** The report-driver is bailing, so abort the fs transaction. */
svn_error_t *svn_repos_abort_report (void *report_baton);


/* ---------------------------------------------------------------*/

/* The magical dir_delta update routines. */

/** Use @a editor and @a edit_baton to describe the changes necessary for 
 * making a given node (and it's descendants, if it is a directory) under 
 * @a src_root look exactly like @a tgt_path under @a tgt_root.
 *
 * Use the provided @a editor and @a edit_baton to describe the changes
 * necessary for making a given node (and its descendants, if it is a
 * directory) under @a src_root look exactly like @a tgt_path under
 * @a tgt_root.  @a src_entry is the node to update, and is either @c NULL 
 * or a single path component.  If @a src_entry is @c NULL, then compute 
 * the difference between the entire tree anchored at @a src_parent_dir 
 * under @a src_root and @a tgt_path under @a target_root.  Else, describe 
 * the changes needed to update only that entry in @a src_parent_dir.
 * Typically, callers of this function will use a @a tgt_path that is the
 * concatenation of @a src_parent_dir and @a src_entry.
 *
 * @a src_root and @a tgt_root can both be either revision or transaction
 * roots.  If @a tgt_root is a revision, @a editor's @c set_target_revision()
 * will be called with the @a tgt_root's revision number, else it will
 * not be called at all.
 *
 * If @a text_deltas is @c FALSE, send a single @c NULL txdelta window to 
 * the window handler returned by @a editor->apply_textdelta().
 *
 * If @a entry_props is @c TRUE, accompany each opened/added entry with
 * propchange editor calls that relay special "entry props" (this
 * is typically used only for working copy updates).
 *
 * If @a use_copy_history is @c TRUE, then when copy history is present on
 * an added node, pass it through to @a editor, and express differences
 * as against the node referred to by that copy history.  Else if
 * @a use_copy_history is @c FALSE, then never pass copyfrom history, and
 * express differences as full adds (i.e., all directory entries are
 * reported as adds, and text deltas are sent against the empty
 * string).
 *
 * Before completing successfully, this function calls @a editor's
 * @c close_edit(), so the caller should expect its @a edit_baton to be
 * invalid after its use with this function.
 *
 * Do any allocation necessary for the delta computation in @a pool.
 * This function's maximum memory consumption is at most roughly
 * proportional to the greatest depth of the tree under @a tgt_root, not
 * the total size of the delta.
 */
svn_error_t *
svn_repos_dir_delta (svn_fs_root_t *src_root,
                     const char *src_parent_dir,
                     const char *src_entry,
                     svn_fs_root_t *tgt_root,
                     const char *tgt_path,
                     const svn_delta_editor_t *editor,
                     void *edit_baton,
                     svn_boolean_t text_deltas,
                     svn_boolean_t recurse,
                     svn_boolean_t entry_props,
                     svn_boolean_t use_copy_history,
                     apr_pool_t *pool);


/* ---------------------------------------------------------------*/

/* Making commits. */

/** Callback function type for commits.
 *
 * When a filesystem commit succeeds, an instance of this is invoked on 
 * the @a new_revision, @a date, and @a author of the commit, along with 
 * the @a baton closure.
 */
typedef svn_error_t * (*svn_repos_commit_callback_t) (
    svn_revnum_t new_revision,
    const char *date,
    const char *author,
    void *baton);

/** Return an @a editor and @a edit_baton to commit changes to @a session->fs.
 *
 * Return an @a editor and @a edit_baton to commit changes to @a session->fs,
 * beginning at location 'rev:@a base_path', where "rev" is the argument
 * given to @c open_root().  Store @a user as the author of the commit and
 * @a log_msg as the commit message.
 *
 * @a repos is a previously opened repository.  @a repos_url is the decoded
 * URL to the base of the repository, and is used to check copyfrom paths.
 *
 * Calling @a (*editor)->close_edit completes the commit.  Before
 * @c close_edit returns, but after the commit has succeeded, it will
 * invoke @a callback with the new revision number, the commit date (as a
 * <tt>const char *</tt>), commit author (as a <tt>const char *</tt>), and
 * @a callback_baton as arguments.  If @a callback returns an error, that
 * error will be returned from @c close_edit, otherwise @c close_edit will
 * return successfully (unless it encountered an error before invoking
 * @a callback).
 */
svn_error_t *svn_repos_get_commit_editor (const svn_delta_editor_t **editor,
                                          void **edit_baton,
                                          svn_repos_t *repos,
                                          const char *repos_url,
                                          const char *base_path,
                                          const char *user,
                                          const char *log_msg,
                                          svn_repos_commit_callback_t hook,
                                          void *callback_baton,
                                          apr_pool_t *pool);


/* ---------------------------------------------------------------*/

/* Finding particular revisions. */

/** Set @a *revision to the revision number in @a repos's filesystem that was
 * youngest at time @a tm.
 */
svn_error_t *
svn_repos_dated_revision (svn_revnum_t *revision,
                          svn_repos_t *repos,
                          apr_time_t tm,
                          apr_pool_t *pool);
                          

/** Return information about a @a root/@a path combo in a filesystem.
 *
 * Given a @a root/@a path within some filesystem, return three pieces of
 * information allocated in @a pool:
 *
 *    - set @a *committed_rev to the revision in which the object was
 *      last modified.  (In fs parlance, this is the revision in which
 *      the particular node-rev-id was 'created'.)
 *  
 *    - set @a *committed_date to the date of said revision, or @c NULL
 *      if not available.
 *
 *    - set @a *last_author to the author of said revision, or @c NULL
 *      if not available.
 */
svn_error_t *
svn_repos_get_committed_info (svn_revnum_t *committed_rev,
                              const char **committed_date,
                              const char **last_author,
                              svn_fs_root_t *root,
                              const char *path,
                              apr_pool_t *pool);

/* ### other queries we can do someday --

     * fetch the last revision created by <user>
         (once usernames become revision properties!)
     * fetch the last revision where <path> was modified
     
*/


/* ---------------------------------------------------------------*/

/* Making checkouts. */

/** Recursively walk over @a revnum:@a path inside an already-open repository
 * @a fs, and drive a checkout @a editor, allocating all data in @a pool.
 */
svn_error_t *
svn_repos_checkout (svn_fs_t *fs, 
                    svn_revnum_t revnum, 
                    svn_boolean_t recurse,
                    const char *fs_path,
                    const svn_delta_editor_t *editor, 
                    void *edit_baton,
                    apr_pool_t *pool);


/* ---------------------------------------------------------------*/

/* Retrieving log messages. */


/** Invoke @a receiver with @a receiver_baton on each log message from 
 * @a start to @a end in @a repos's filesystem.
 *
 * Invoke @a receiver with @a receiver_baton on each log message from 
 * @a start to @a end in @a repos's filesystem.  @a start may be greater 
 * or less than @a end; this just controls whether the log messages are 
 * processed in descending or ascending revision number order.
 *
 * If @a start or @a end is @c SVN_INVALID_REVNUM, it defaults to youngest.
 *
 * If @a paths is non-null and has one or more elements, then only show
 * revisions in which at least one of @a paths was changed (i.e., if
 * file, text or props changed; if dir, props changed or an entry was
 * added or deleted).  Each path is an <tt>const char *</tt> representing 
 * an absolute path in the repository.
 *
 * ### todo: need to consider whether the above directory behavior is
 * most useful, or if we should actually treat _any_ node change in a
 * directory as a visible change for purposes of log... i.e., show
 * bubble-up.  The reason this might be useful is so that running log
 * on a directory would give a msg for every change under that dir,
 * no matter how far down.  See the thread started on the dev list by
 * Lars Kellogg-Stedman <lars@larsshack.org> with the subject
 * "Single repository, multiple projects?" for more.  We may simple
 * need to offer a few different semantics for @a paths.
 *
 * If @a discover_changed_paths, then each call to @a receiver passes a
 * <tt>const apr_hash_t *</tt> for the receiver's @a changed_paths 
 * argument; the hash's keys are all the paths committed in that revision.
 * Otherwise, each call to @a receiver passes null for @a changed_paths.
 *
 * ### NOTE: @a paths and @a discover_changed_paths are currently ignored,
 * see http://subversion.tigris.org/issues/show_bug.cgi?id=562 for
 * more information.
 *
 * If @a strict_node_history is set, copy history (if any exists) will
 * not be traversed while harvesting revision logs for each path.
 *
 * If any invocation of @a receiver returns error, return that error
 * immediately and without wrapping it.
 *
 * If @a start or @a end is a non-existent revision, return the error
 * @c SVN_ERR_FS_NO_SUCH_REVISION, without ever invoking @a receiver.
 *
 * See also the documentation for @c svn_log_message_receiver_t.
 *
 * Use @a pool for temporary allocations.
 */
svn_error_t *
svn_repos_get_logs (svn_repos_t *repos,
                    const apr_array_header_t *paths,
                    svn_revnum_t start,
                    svn_revnum_t end,
                    svn_boolean_t discover_changed_paths,
                    svn_boolean_t strict_node_history,
                    svn_log_message_receiver_t receiver,
                    void *receiver_baton,
                    apr_pool_t *pool);


/* ---------------------------------------------------------------*/

/**
 * @defgroup svn_repos_hook_wrappers Hook-sensitive wrappers for libsvn_fs 
 * routines.
 * @{
 */

/** Like @c svn_fs_commit_txn(), but invoke the @a repos's pre- and
 * post-commit hooks around the commit.
 *
 * Like @c svn_fs_commit_txn(), but invoke the @a repos's pre- and
 * post-commit hooks around the commit.  Use @a txn's pool for temporary
 * allocations.
 *
 * @a conflict_p, @a new_rev, and @a txn are as in @c svn_fs_commit_txn().
 */
svn_error_t *svn_repos_fs_commit_txn (const char **conflict_p,
                                      svn_repos_t *repos,
                                      svn_revnum_t *new_rev,
                                      svn_fs_txn_t *txn);

/** Like @c svn_fs_begin_txn(), but use @a author and @a log_msg to set the
 * corresponding properties on transaction @a *txn_p.
 *
 * Like @c svn_fs_begin_txn(), but use @a author and @a log_msg to set the
 * corresponding properties on transaction @a *txn_p.  @a repos is the
 * repository object which contains the filesystem.  @a rev, @a *txn_p, and
 * @a pool are as in @c svn_fs_begin_txn().
 *
 * Before a txn is created, the repository's start-commit hooks are
 * run; if any of them fail, no txn is created, @a *txn_p is unaffected, 
 * and @c SVN_ERR_REPOS_HOOK_FAILURE is returned.
 *
 * @a log_msg may be @c NULL to indicate the message is not (yet) available.
 * The caller will need to attach it to the transaction at a later time.
 */
svn_error_t *svn_repos_fs_begin_txn_for_commit (svn_fs_txn_t **txn_p,
                                                svn_repos_t *repos,
                                                svn_revnum_t rev,
                                                const char *author,
                                                const char *log_msg,
                                                apr_pool_t *pool);


/** Like @c svn_fs_begin_txn(), but use @a author to set the corresponding
 * property on transaction @a *txn_p.
 *
 * Like @c svn_fs_begin_txn(), but use @a author to set the corresponding
 * property on transaction @a *txn_p.  @a repos is the repository object
 * which contains the filesystem.  @a rev, @a *txn_p, and @a pool are as in
 * @c svn_fs_begin_txn().
 *
 * ### Someday: before a txn is created, some kind of read-hook could
 *              be called here.
 */
svn_error_t *svn_repos_fs_begin_txn_for_update (svn_fs_txn_t **txn_p,
                                                svn_repos_t *repos,
                                                svn_revnum_t rev,
                                                const char *author,
                                                apr_pool_t *pool);


/** Like @c svn_fs_change_rev_prop(), but invoke the @a repos's pre- and
 * post-revprop-change hooks around the change.
 *
 * Like @c svn_fs_change_rev_prop(), but invoke the @a repos's pre- and
 * post-revprop-change hooks around the change.  Use @a pool for
 * temporary allocations.
 *
 * @a rev is the revision whose property to change, @a name is the
 * name of the property, and @a new_value is the new value of the
 * property.   @a author is the authenticated username of the person
 * changing the property value, or null if not available.
 */
svn_error_t *svn_repos_fs_change_rev_prop (svn_repos_t *repos,
                                           svn_revnum_t rev,
                                           const char *author,
                                           const char *name,
                                           const svn_string_t *new_value,
                                           apr_pool_t *pool);


/* ---------------------------------------------------------------*/

/* Prop-changing wrappers for libsvn_fs routines. */

/* NOTE: svn_repos_fs_change_rev_prop() also exists, but is located
   above with the hook-related functions. */


/** Validating wrapper for @c svn_fs_change_node_prop() (which see for
 * argument descriptions).
 */
svn_error_t *svn_repos_fs_change_node_prop (svn_fs_root_t *root,
                                            const char *path,
                                            const char *name,
                                            const svn_string_t *value,
                                            apr_pool_t *pool);

/** Validating wrapper for @c svn_fs_change_txn_prop() (which see for
 * argument descriptions).
 */
svn_error_t *svn_repos_fs_change_txn_prop (svn_fs_txn_t *txn,
                                           const char *name,
                                           const svn_string_t *value,
                                           apr_pool_t *pool);

/** @} */

/* ---------------------------------------------------------------*/

/**
 * @defgroup svn_repos_inspection Data structures and editor things for 
 * repository inspection.
 * @{
 *
 * As it turns out, the @c svn_repos_dir_delta() interface can be
 * extremely useful for examining the repository, or more exactly,
 * changes to the repository.  @c svn_repos_dir_delta() allows for
 * differences between two trees to be described using an editor.
 *
 * By using the specific editor found below in conjunction with
 * @c svn_repos_dir_delta(), the description of how to transform one tree
 * into another can be used to build an in-memory linked-list tree,
 * which each node representing a repository node that was changed as a
 * result of having @c svn_repos_dir_delta() drive that editor.
 */

/** A node in the repository. */
typedef struct svn_repos_node_t
{
  /** Node type (file, dir, etc.) */
  svn_node_kind_t kind;

  /** How this node entered the node tree: 'A'dd, 'D'elete, 'R'eplace */
  char action; 

  /** Were there any textual mods? (files only) */
  svn_boolean_t text_mod;

  /** Where there any property mods? */
  svn_boolean_t prop_mod;

  /** The name of this node as it appears in its parent's entries list */
  const char *name;

  /** The filesystem revision where this was copied from (if any) */
  svn_revnum_t copyfrom_rev;

  /** The filesystem path where this was copied from (if any) */
  const char *copyfrom_path;

  /** Pointer to the next sibling of this node */
  struct svn_repos_node_t *sibling;

  /** Pointer to the first child of this node */
  struct svn_repos_node_t *child;

} svn_repos_node_t;


/** Get an @a editor/@a edit_baton pair that can be driven by 
 * @c svn_repos_dir_delta to build a tree of @c svn_repos_node_t's 
 * representing the delta from @a base_root to @a root.
 *
 * Set @a *editor and @a *edit_baton to an editor that, when driven by
 * @c svn_repos_dir_delta(), builds an <tt>svn_repos_node_t *</tt> tree
 * representing the delta from @a base_root to @a root in @a repos's 
 * filesystem.
 *  
 * Invoke @c svn_repos_node_from_baton() on @a edit_baton to obtain the root
 * node afterwards.
 *
 * Note that the delta includes "bubbled-up" directories; that is,
 * many of the directory nodes will have no prop_mods.
 *
 * Allocate the tree and its contents in @a node_pool; do all other
 * allocation in @a pool.
 */
svn_error_t *svn_repos_node_editor (const svn_delta_editor_t **editor,
                                    void **edit_baton,
                                    svn_repos_t *repos,
                                    svn_fs_root_t *base_root,
                                    svn_fs_root_t *root,
                                    apr_pool_t *node_pool,
                                    apr_pool_t *pool);

/** Return the root node of the linked-list tree generated by using an 
 * editor from @c svn_repos_node_editor with @c svn_repos_dir_delta.
 *
 * Return the root node of the linked-list tree generated by driving
 * the editor created by @c svn_repos_node_editor() with
 * @c svn_repos_dir_delta(), which is stored in @a edit_baton.  This is 
 * only really useful if used *after* the editor drive is completed.
 */
svn_repos_node_t *svn_repos_node_from_baton (void *edit_baton);

/** @} */

/* ---------------------------------------------------------------*/

/**
 * @defgroup svn_repos_dump_load Dumping and loading filesystem data
 * @{
 *
 * The filesystem 'dump' format contains nothing but the abstract
 * structure of the filesystem -- independent of any internal node-id
 * schema or database back-end.  All of the data in the dumpfile is
 * acquired by public function calls into svn_fs.h.  Similarly, the
 * parser which reads the dumpfile is able to reconstruct the
 * filesystem using only public svn_fs.h routines.
 *
 * Thus the dump/load feature's main purpose is for *migrating* data
 * from one svn filesystem to another -- presumably two filesystems
 * which have different internal implementations.
 *
 * If you simply want to backup your filesystem, you're probably
 * better off using the built-in facilities of the DB backend (using
 * Berkeley DB's hot-backup feature, for example.)
 * 
 * For a description of the dumpfile format, see
 * /trunk/notes/fs_dumprestore.txt.
 */

/* The RFC822-style headers in our dumpfile format. */
#define SVN_REPOS_DUMPFILE_MAGIC_HEADER            "SVN-fs-dump-format-version"
#define SVN_REPOS_DUMPFILE_FORMAT_VERSION           2
#define SVN_REPOS_DUMPFILE_UUID                      "UUID"
#define SVN_REPOS_DUMPFILE_CONTENT_LENGTH            "Content-length"

#define SVN_REPOS_DUMPFILE_REVISION_NUMBER           "Revision-number"

#define SVN_REPOS_DUMPFILE_NODE_PATH                 "Node-path"
#define SVN_REPOS_DUMPFILE_NODE_KIND                 "Node-kind"
#define SVN_REPOS_DUMPFILE_NODE_ACTION               "Node-action"
#define SVN_REPOS_DUMPFILE_NODE_COPYFROM_PATH        "Node-copyfrom-path"
#define SVN_REPOS_DUMPFILE_NODE_COPYFROM_REV         "Node-copyfrom-rev"
#define SVN_REPOS_DUMPFILE_TEXT_COPY_SOURCE_CHECKSUM "Text-copy-source-md5"
#define SVN_REPOS_DUMPFILE_TEXT_CONTENT_CHECKSUM     "Text-content-md5"

#define SVN_REPOS_DUMPFILE_PROP_CONTENT_LENGTH       "Prop-content-length"
#define SVN_REPOS_DUMPFILE_TEXT_CONTENT_LENGTH       "Text-content-length"

/** The different "actions" attached to nodes in the dumpfile. */
enum svn_node_action
{
  svn_node_action_change,
  svn_node_action_add,
  svn_node_action_delete,
  svn_node_action_replace
};

/** The different policies for processing the UUID in the dumpfile. */
enum svn_repos_load_uuid
{
  svn_repos_load_uuid_default,
  svn_repos_load_uuid_ignore,
  svn_repos_load_uuid_force,
};

/** Dump the contents of the filesystem within already-open @a repos into
 * writable @a dumpstream. 
 *
 * Dump the contents of the filesystem within already-open @a repos into
 * writable @a dumpstream.  Begin at revision @a start_rev, and dump every
 * revision up through @a end_rev.  Use @a pool for all allocation.  If
 * non-@c NULL, send feedback to @a feedback_stream.
 *
 * If @a start_rev is @c SVN_INVALID_REVNUM, then start dumping at revision 
 * 0.  If @a end_rev is @c SVN_INVALID_REVNUM, then dump through the @c HEAD 
 * revision.
 *
 * If @a incremental is @c TRUE, the first revision dumped will be a diff
 * against the previous revision (usually it looks like a full dump of
 * the tree).
 */
svn_error_t *svn_repos_dump_fs (svn_repos_t *repos,
                                svn_stream_t *dumpstream,
                                svn_stream_t *feedback_stream,
                                svn_revnum_t start_rev,
                                svn_revnum_t end_rev,
                                svn_boolean_t incremental,
                                apr_pool_t *pool);


/* Read and parse dumpfile-formatted @a dumpstream, reconstructing
 * filesystem revisions in already-open @a repos, handling uuids
 * in accordance with @a uuid_action.
 *
 * Read and parse dumpfile-formatted @a dumpstream, reconstructing
 * filesystem revisions in already-open @a repos.  Use @a pool for all
 * allocation.  If non-@c NULL, send feedback to @a feedback_stream.
 *
 * If the dumpstream contains copy history that is unavailable in the
 * repository, an error will be thrown.
 *
 * The repository's UUID will be updated iff
 *   the dumpstream contains a UUID and
 *   @a uuid_action is not equal to @c svn_repos_load_uuid_ignore and
 *   either the repository contains no revisions or
 *          @a uuid_action is equal to @c svn_repos_load_uuid_force.
 *
 * If the dumpstream contains no UUID, then @a uuid_action is
 * ignored and the repository UUID is not touched.
 */

svn_error_t *svn_repos_load_fs (svn_repos_t *repos,
                                svn_stream_t *dumpstream,
                                svn_stream_t *feedback_stream,
                                enum svn_repos_load_uuid uuid_action,
                                const char *parent_dir,
                                apr_pool_t *pool);


/** A vtable that is driven by @c svn_repos_parse_dumpstream. */
typedef struct svn_repos_parse_fns_t
{
  /** The parser has discovered a new revision record within the
   * parsing session represented by @a parse_baton.
   *
   * The parser has discovered a new revision record within the
   * parsing session represented by @a parse_baton.  All the headers are
   * placed in @a headers (allocated in @a pool), which maps <tt>const 
   * char *</tt> header-name ==> <tt>const char *</tt> header-value.  
   * The @a revision_baton received back (also allocated in @a pool) 
   * represents the revision.
   */
  svn_error_t *(*new_revision_record) (void **revision_baton,
                                       apr_hash_t *headers,
                                       void *parse_baton,
                                       apr_pool_t *pool);

  /** The parser has discovered a new uuid record within the parsing
   * session represented by @a parse_baton.
   *
   * The parser has discovered a new uuid record within the parsing
   * session represented by @a parse_baton.  The uuid's value is
   * @a uuid, and it is allocated in @a pool.
   */
  svn_error_t *(*uuid_record) (const char *uuid,
                               void *parse_baton,
                               apr_pool_t *pool);

  /** The parser has discovered a new node record within the current
   * revision represented by @a revision_baton.
   *
   * The parser has discovered a new node record within the current
   * revision represented by @a revision_baton.  All the headers are
   * placed in @a headers as above, allocated in @a pool.  The 
   * @a node_baton received back is allocated in @a pool and represents 
   * the node.
   */
  svn_error_t *(*new_node_record) (void **node_baton,
                                   apr_hash_t *headers,
                                   void *revision_baton,
                                   apr_pool_t *pool);

  /** For a given @a revision_baton, set a property @a name to @a value. */
  svn_error_t *(*set_revision_property) (void *revision_baton,
                                         const char *name,
                                         const svn_string_t *value);

  /** For a given @a node_baton, set a property @a name to @a value. */
  svn_error_t *(*set_node_property) (void *node_baton,
                                     const char *name,
                                     const svn_string_t *value);

  /** For a given @a node_baton, remove all properties. */
  svn_error_t *(*remove_node_props) (void *node_baton);

  /** For a given @a node_baton, receive a writable @a stream capable of
   * receiving the node's fulltext.
   *
   * For a given @a node_baton, receive a writable @a stream capable of
   * receiving the node's fulltext.  After writing the fulltext, call
   * the stream's @c close() function.
   *
   * If a @c NULL is returned instead of a stream, the vtable is
   * indicating that no text is desired, and the parser will not
   * attempt to send it.
   */
  svn_error_t *(*set_fulltext) (svn_stream_t **stream,
                                void *node_baton);

  /** The parser has reached the end of the current node represented by
   * @a node_baton, it can be freed.
   */
  svn_error_t *(*close_node) (void *node_baton);

  /** The parser has reached the end of the current revision
   * represented by @a revision_baton.
   *
   * The parser has reached the end of the current revision
   * represented by @a revision_baton.  In other words, there are no more
   * changed nodes within the revision.  The baton can be freed.
   */
  svn_error_t *(*close_revision) (void *revision_baton);

} svn_repos_parser_fns_t;



/** Read and parse dumpfile-formatted @a stream, calling callbacks in
 * @a parse_fns/@a parse_baton, and using @a pool for allocations.
 *
 * Read and parse dumpfile-formatted @a stream, calling callbacks in
 * @a parse_fns/@a parse_baton, and using @a pool for allocations.
 *
 * This parser has built-in knowledge of the dumpfile format, but only
 * in a general sense:
 *
 *    * it recognizes revision and node records by looking for either
 *      a REVISION_NUMBER or NODE_PATH headers.
 *
 *    * it recognizes the CONTENT-LENGTH headers, so it knows if and
 *      how to suck up the content body.
 *
 *    * it knows how to parse a content body into two parts:  props
 *      and text, and pass the pieces to the vtable.
 *
 * This is enough knowledge to make it easy on vtable implementors,
 * but still allow expansion of the format:  most headers are ignored.
 */
svn_error_t *
svn_repos_parse_dumpstream (svn_stream_t *stream,
                            const svn_repos_parser_fns_t *parse_fns,
                            void *parse_baton,
                            apr_pool_t *pool);


/** Set @a *parser and @a *parse_baton to a vtable parser which commits new
 * revisions to the fs in @a repos.  The constructed parser will treat
 * UUID records in a manner consistent with @a uuid_action.  Use @a pool
 * to operate on the fs.
 *
 * Set @a *parser and @a *parse_baton to a vtable parser which commits new
 * revisions to the fs in @a repos.  The constructed parser will treat
 * UUID records in a manner consistent with @a uuid_action.  Use @a pool
 * to operate on the fs.
 *
 * If @a use_history is set, then the parser will require relative
 * 'copyfrom' history to exist in the repository when it encounters
 * nodes that are added-with-history.
 *
 * If @a parent_dir is not null, then the parser will reparent all the
 * loaded nodes, from root to @a parent_dir.  The directory @a parent_dir
 * must be an existing directory in the repository.
 *
 * Print all parsing feedback to @a outstream (if non-@c NULL).
 *
 */
svn_error_t *
svn_repos_get_fs_build_parser (const svn_repos_parser_fns_t **parser,
                               void **parse_baton,
                               svn_repos_t *repos,
                               svn_boolean_t use_history,
                               enum svn_repos_load_uuid uuid_action,
                               svn_stream_t *outstream,
                               const char *parent_dir,
                               apr_pool_t *pool);
/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_REPOS_H */
