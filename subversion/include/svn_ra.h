/**
 * @copyright
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
 * @endcopyright
 *
 * @file svn_ra.h
 * @brief structures related to repository access
 */




#ifndef SVN_RA_H
#define SVN_RA_H

#include <apr_pools.h>
#include <apr_tables.h>

#include "svn_error.h"
#include "svn_delta.h"
#include "svn_auth.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */



/* Misc. declarations */

/**
 * @since New in 1.1.
 *
 * Get libsvn_ra version information.
 */
const svn_version_t *svn_ra_version (void);


/** This is a function type which allows the RA layer to fetch working
 * copy (WC) properties.
 *
 * The @a baton is provided along with the function pointer and should
 * be passed back in. This will be the @a callback_baton or the 
 * @a close_baton as appropriate.
 *
 * @a path is relative to the "root" of the session, defined by the 
 * @a repos_url passed to the @c RA->open() vtable call.
 *
 * @a name is the name of the property to fetch. If the property is present,
 * then it is returned in @a value. Otherwise, @a *value is set to @c NULL.
 */
typedef svn_error_t *(*svn_ra_get_wc_prop_func_t) (void *baton,
                                                   const char *relpath,
                                                   const char *name,
                                                   const svn_string_t **value,
                                                   apr_pool_t *pool);

/** This is a function type which allows the RA layer to store new
 * working copy properties during update-like operations.  See the
 * comments for @c svn_ra_get_wc_prop_func_t for @a baton, @a path, and 
 * @a name. The @a value is the value that will be stored for the property; 
 * a null @a value means the property will be deleted.
 */
typedef svn_error_t *(*svn_ra_set_wc_prop_func_t) (void *baton,
                                                   const char *path,
                                                   const char *name,
                                                   const svn_string_t *value,
                                                   apr_pool_t *pool);

/** This is a function type which allows the RA layer to store new
 * working copy properties as part of a commit.  See the comments for
 * @c svn_ra_get_wc_prop_func_t for @a baton, @a path, and @a name.  
 * The @a value is the value that will be stored for the property; a 
 * @c NULL @a value means the property will be deleted.
 *
 * Note that this might not actually store the new property before
 * returning, but instead schedule it to be changed as part of
 * post-commit processing (in which case a successful commit means the
 * properties got written).  Thus, during the commit, it is possible
 * to invoke this function to set a new value for a wc prop, then read
 * the wc prop back from the working copy and get the *old* value.
 * Callers beware.
 */
typedef svn_error_t *(*svn_ra_push_wc_prop_func_t) (void *baton,
                                                    const char *path,
                                                    const char *name,
                                                    const svn_string_t *value,
                                                    apr_pool_t *pool);

/** This is a function type which allows the RA layer to invalidate
 * (i.e., remove) wcprops.  See the documentation for
 * @c svn_ra_get_wc_prop_func_t for @a baton, @a path, and @a name.
 *
 * Unlike @c svn_ra_push_wc_prop_func_t, this has immediate effect.  If
 * it returns success, the wcprops have been removed.
 */
typedef svn_error_t *(*svn_ra_invalidate_wc_props_func_t) (void *baton,
                                                           const char *path,
                                                           const char *name,
                                                           apr_pool_t *pool);


/** A function type for retrieving the youngest revision from a repos. */
typedef svn_error_t *(*svn_ra_get_latest_revnum_func_t) 
       (void *session_baton,
        svn_revnum_t *latest_revnum);

/**
 * @since New in 1.1.
 *
 * A callback function type for use in @c get_file_revs.
 * @a baton is provided by the caller, @a path is the pathname of the file
 * in revision @a rev and @a rev_props are the revision properties.
 * If @a delta_handler and @a delta_baton are non-NULL, they may be set to a
 * handler/baton which will be called with the delta between the previous
 * revision and this one after the return of this callback.  They may be
 * left as NULL/NULL.
 * @a prop_diffs is an array of svn_prop_t elements indicating the property
 * delta for this and the previous revision.
 * @a pool may be used for temporary allocations, but you can't rely
 * on objects allocated to live outside of this particular call and the
 * immediately following calls to @a *delta_handler, if any. */
typedef svn_error_t *(*svn_ra_file_rev_handler_t)
       (void *baton,
        const char *path,
        svn_revnum_t rev,
        apr_hash_t *rev_props,
        svn_txdelta_window_handler_t *delta_handler,
        void **delta_baton,
        apr_array_header_t *prop_diffs,
        apr_pool_t *pool);


/** The update Reporter.
 *
 * A vtable structure which allows a working copy to describe a subset
 * (or possibly all) of its working-copy to an RA layer, for the
 * purposes of an update, switch, status, or diff operation.
 *
 * Paths for report calls are relative to the target (not the anchor)
 * of the operation.  Report calls must be made in depth-first order:
 * parents before children, all children of a parent before any
 * siblings of the parent.  The first report call must be a set_path
 * with a @a path argument of "" and a valid revision.  (If the target
 * of the operation is locally deleted or missing, use the anchor's
 * revision.)  If the target of the operation is deleted or switched
 * relative to the anchor, follow up the initial set_path call with a
 * link_path or delete_path call with a @a path argument of "" to
 * indicate that.  In no other case may there be two report
 * descriptions for the same path.  If the target of the operation is
 * a locally added file or directory (which previously did not exist),
 * it may be reported as having revision 0 or as having the parent
 * directory's revision.
 */
typedef struct svn_ra_reporter_t
{
  /** Describe a working copy @a path as being at a particular @a revision.  
   *
   * If @a START_EMPTY is set and @a path is a directory, the
   * implementor should assume the directory has no entries or props.
   *
   * This will *override* any previous @c set_path() calls made on parent
   * paths.  @a path is relative to the URL specified in @c open().
   *
   * All temporary allocations are done in @a pool.
   */
  svn_error_t *(*set_path) (void *report_baton,
                            const char *path,
                            svn_revnum_t revision,
                            svn_boolean_t start_empty,
                            apr_pool_t *pool);

  /** Describing a working copy @a path as missing.
   *
   * All temporary allocations are done in @a pool.
   */
  svn_error_t *(*delete_path) (void *report_baton,
                               const char *path,
                               apr_pool_t *pool);
    
  /** Like @c set_path(), but differs in that @a path in the working copy
   * (relative to the root of the report driver) isn't a reflection of
   * @a path in the repository (relative to the URL specified when
   * opening the RA layer), but is instead a reflection of a different
   * repository @a url at @a revision.
   *
   * If @a START_EMPTY is set and @a path is a directory,
   * the implementor should assume the directory has no entries or props.
   *
   * All temporary allocations are done in @a pool.
   */
  svn_error_t *(*link_path) (void *report_baton,
                             const char *path,
                             const char *url,
                             svn_revnum_t revision,
                             svn_boolean_t start_empty,
                             apr_pool_t *pool);

  /** WC calls this when the state report is finished; any directories
   * or files not explicitly `set' above are assumed to be at the
   * baseline revision originally passed into @c do_update().
   */
  svn_error_t *(*finish_report) (void *report_baton,
                                 apr_pool_t *pool);

  /** If an error occurs during a report, this routine should cause the
   * filesystem transaction to be aborted & cleaned up.
   */
  svn_error_t *(*abort_report) (void *report_baton,
                                apr_pool_t *pool);

} svn_ra_reporter_t;



/** A collection of callbacks implemented by libsvn_client which allows
 * an RA layer to "pull" information from the client application, or
 * possibly store information.  libsvn_client passes this vtable to
 * @c RA->open().  
 *
 * Each routine takes a @a callback_baton originally provided with the
 * vtable.
 */
typedef struct svn_ra_callbacks_t
{
  /** Open a unique temporary file for writing in the working copy.
   * This file will be automatically deleted when @a fp is closed.
   */
  svn_error_t *(*open_tmp_file) (apr_file_t **fp,
                                 void *callback_baton,
                                 apr_pool_t *pool);
  
  /** An authentication baton, created by the application, which is
   * capable of retrieving all known types of credentials.
   */
  svn_auth_baton_t *auth_baton;

  /*** The following items may be set to NULL to disallow the RA layer
       to perform the respective operations of the vtable functions.
       Perhaps WC props are not defined or are in invalid for this
       session, or perhaps the commit operation this RA session will
       perform is a server-side only one that shouldn't do post-commit
       processing on a working copy path.  ***/

  /** Fetch working copy properties.
   *
   *<pre> ### we might have a problem if the RA layer ever wants a property
   * ### that corresponds to a different revision of the file than
   * ### what is in the WC. we'll cross that bridge one day...</pre>
   */
  svn_ra_get_wc_prop_func_t get_wc_prop;

  /** Immediately set new values for working copy properties. */
  svn_ra_set_wc_prop_func_t set_wc_prop;

  /** Schedule new values for working copy properties. */
  svn_ra_push_wc_prop_func_t push_wc_prop;

  /** Invalidate working copy properties. */
  svn_ra_invalidate_wc_props_func_t invalidate_wc_props;

} svn_ra_callbacks_t;



/*----------------------------------------------------------------------*/

/* Public Interfaces. */

/**
 * @since New in 1.2.
 *
 * A repository access session.  This object is used to perform requests
 * to a repository, identified by an URL.
 */
typedef struct svn_ra_session_t svn_ra_session_t;

/**
 * @since New in 1.2.
 *
 * Open a repository session to @a repos_url.  Return an opaque object
 * representing this session in @a *session_p, allocated in @a pool.
 *
 * @a callbacks/@a callback_baton is a table of callbacks provided by the
 * client; see @c svn_ra_callbacks_t above.
 *
 * @a config is a hash mapping <tt>const char *</tt> keys to 
 * @c svn_config_t * values.  For example, the @c svn_config_t for the 
 * "~/.subversion/config" file is under the key "config".
 *
 * All RA requests require a session; they will continue to
 * use @a pool for memory allocation.
 */
svn_error_t *svn_ra_open (svn_ra_session_t **session_p,
                          const char *repos_URL,
                          const svn_ra_callbacks_t *callbacks,
                          void *callback_baton,
                          apr_hash_t *config,
                          apr_pool_t *pool);

/**
 * @since New in 1.2.
 *
 * Get the latest revision number from the repository of @a session.
 *
 * Use @a pool for memory allocation.
 */
svn_error_t *svn_ra_get_latest_revnum (svn_ra_session_t *session,
                                       svn_revnum_t *latest_revnum,
                                       apr_pool_t *pool);

/**
 * @since New in 1.2.
 *
 * Get the latest revision number at time @a tm in the repository of
 * @a session.
 *
 * Use @a pool for memory allocation.
 */
svn_error_t *svn_ra_get_dated_revision (svn_ra_session_t *session,
                                        svn_revnum_t *revision,
                                        apr_time_t tm,
                                        apr_pool_t *pool);

/**
 * @since New in 1.2.
 *
 * Set the property @a name to @a value on revision @a rev in the repository
 * of @a session.
 *
 * If @a value is @c NULL, delete the named revision property.
 *
 * Please note that properties attached to revisions are **unversioned**.
 *
 * Use @a pool for memory allocation.
 */
svn_error_t *svn_ra_change_rev_prop (svn_ra_session_t *session,
                                     svn_revnum_t rev,
                                     const char *name,
                                     const svn_string_t *value,
                                     apr_pool_t *pool);

/**
 * @since New in 1.2.
 *
 * Set @a *props to the list of unversioned properties attached to revision
 * @a rev in the repository of @a session.  The hash maps
 * (<tt>const char *</tt>) names to (<tt>@c svn_string_t *</tt>) values.
 *
 * Use @a pool for memory allocation.
 */
svn_error_t *svn_ra_rev_proplist (svn_ra_session_t *session,
                                  svn_revnum_t rev,
                                  apr_hash_t **props,
                                  apr_pool_t *pool);

/**
 * @since New in 1.2.
 *
 * Set @a *value to the value of unversioned property @a name attached to
 * revision @a rev in the repository of @a session.  If @a rev has no
 * property by that name, set @a *value to @c NULL.
 *
 * Use @a pool for memory allocation.
 */
svn_error_t *svn_ra_rev_prop (svn_ra_session_t *session,
                              svn_revnum_t rev,
                              const char *name,
                              svn_string_t **value,
                              apr_pool_t *pool);

/**
 * @since New in 1.2.
 *
 * Set @a *editor and @a *edit_baton to an editor for committing changes
 * to the repository of @a session, using @a log_msg as the log message.  The
 * revisions being committed against are passed to the editor
 * functions, starting with the rev argument to @c open_root.  The path
 * root of the commit is in the @a session's URL.
 *
 * Before @c close_edit returns, but after the commit has succeeded,
 * it will invoke @a callback with the new revision number, the
 * commit date (as a <tt>const char *</tt>), commit author (as a
 * <tt>const char *</tt>), and @a callback_baton as arguments.  If
 * @a callback returns an error, that error will be returned from @c
 * close_edit, otherwise @c close_edit will return successfully
 * (unless it encountered an error before invoking @a callback).
 *
 * The callback will not be called if the commit was a no-op
 * (i.e. nothing was committed);
 *
 * The caller may not perform any RA operations using @a session before
 * finishing the edit.
 * 
 * Use @a pool for memory allocation.
 */
svn_error_t *svn_ra_get_commit_editor (svn_ra_session_t *session,
                                       const svn_delta_editor_t **editor,
                                       void **edit_baton,
                                       const char *log_msg,
                                       svn_commit_callback_t callback,
                                       void *callback_baton,
                                       apr_pool_t *pool);

/**
 * @since New in 1.2.
 *
 * Fetch the contents and properties of file @a path at @a revision.
 * Interpret @a path relative to the URL in @a session.  Use
 * @a pool for all allocations.
 *
 * If @a revision is @c SVN_INVALID_REVNUM (meaning 'head') and
 * @a *fetched_rev is not @c NULL, then this function will set
 * @a *fetched_rev to the actual revision that was retrieved.  (Some
 * callers want to know, and some don't.) 
 *
 * If @a stream is non @c NULL, push the contents of the file at @a stream.
 *
 * If @a props is non @c NULL, set @a *props to contain the properties of 
 * the file.  This means *all* properties: not just ones controlled by
 * the user and stored in the repository fs, but non-tweakable ones
 * generated by the SCM system itself (e.g. 'wcprops', 'entryprops',
 * etc.)  The keys are <tt>const char *</tt>, values are 
 * <tt>@c svn_string_t *</tt>.
 *
 * The stream handlers for @a stream may not perform any RA
 * operations using @a session.
 */
svn_error_t *svn_ra_get_file (svn_ra_session_t *session,
                              const char *path,
                              svn_revnum_t revision,
                              svn_stream_t *stream,
                              svn_revnum_t *fetched_rev,
                              apr_hash_t **props,
                              apr_pool_t *pool);

/**
 * @since New in 1.2.
 *
 * If @a dirents is non @c NULL, set @a *dirents to contain all the entries
 * of directory @a path at @a revision.  The keys of @a dirents will be 
 * entry names (<tt>const char *</tt>), and the values dirents 
 * (<tt>@c svn_dirent_t *</tt>).  Use @a pool for all allocations.
 *
 * @a path is interpreted relative to the URL in @a session.
 *
 * If @a revision is @c SVN_INVALID_REVNUM (meaning 'head') and
 * @a *fetched_rev is not @c NULL, then this function will set
 * @a *fetched_rev to the actual revision that was retrieved.  (Some
 * callers want to know, and some don't.) 
 *
 * If @a props is non @c NULL, set @a *props to contain the properties of 
 * the directory.  This means *all* properties: not just ones controlled by
 * the user and stored in the repository fs, but non-tweakable ones
 * generated by the SCM system itself (e.g. 'wcprops', 'entryprops',
 * etc.)  The keys are <tt>const char *</tt>, values are 
 * <tt>@c svn_string_t *</tt>.
 */
svn_error_t *svn_ra_get_dir (svn_ra_session_t *session,
                             const char *path,
                             svn_revnum_t revision,
                             apr_hash_t **dirents,
                             svn_revnum_t *fetched_rev,
                             apr_hash_t **props,
                             apr_pool_t *pool);

/**
 * @since New in 1.2.
 *
 * Ask the RA layer to update a working copy.
 *
 * The client initially provides an @a update_editor/@a baton to the 
 * RA layer; this editor contains knowledge of where the change will
 * begin in the working copy (when @c open_root() is called).
 *
 * In return, the client receives a @a reporter/@a report_baton.  The
 * client then describes its working-copy revision numbers by making
 * calls into the @a reporter structure; the RA layer assumes that all
 * paths are relative to the URL used to open @a session.
 *
 * When finished, the client calls @a reporter->finish_report().  The
 * RA layer then does a complete drive of @a update_editor, ending with
 * @c close_edit(), to update the working copy.
 *
 * @a update_target is an optional single path component will restrict
 * the scope of things affected by the update to an entry in the
 * directory represented by the @a session's URL, or empty if the
 * entire directory is meant to be updated.
 *
 * The working copy will be updated to @a revision_to_update_to, or the
 * "latest" revision if this arg is invalid.
 *
 * The caller may not perform any RA operations using @a session before
 * finishing the report, and may not perform any RA operations using
 * @a session from within the editing operations of @a update_editor.
 *
 * Use @a pool for memory allocation.
 */
svn_error_t *svn_ra_do_update (svn_ra_session_t *session,
                               const svn_ra_reporter_t **reporter,
                               void **report_baton,
                               svn_revnum_t revision_to_update_to,
                               const char *update_target,
                               svn_boolean_t recurse,
                               const svn_delta_editor_t *update_editor,
                               void *update_baton,
                               apr_pool_t *pool);

/**
 * @since New in 1.2.
 *
 * Ask the RA layer to 'switch' a working copy to a new
 * @a switch_url;  it's another form of @c svn_ra_do_update().
 *
 * The client initially provides an @a switch_editor/@a baton to the RA
 * layer; this editor contains knowledge of where the change will
 * begin in the working copy (when @c open_root() is called). 
 *
 * In return, the client receives a @a reporter/@a report_baton.  The
 * client then describes its working-copy revision numbers by making
 * calls into the @a reporter structure; the RA layer assumes that all
 * paths are relative to the URL used to create @a session_baton.
 *
 * When finished, the client calls @a reporter->finish_report().  The
 * RA layer then does a complete drive of @a switch_editor, ending with
 * @c close_edit(), to switch the working copy.
 *
 * @a switch_target is an optional single path component will restrict
 * the scope of things affected by the switch to an entry in the
 * directory represented by the @a session's URL, or empty if the
 * entire directory is meant to be switched.
 *
 * The working copy will be switched to @a revision_to_switch_to, or the
 * "latest" revision if this arg is invalid.
 *
 * The caller may not perform any RA operations using
 * @a session before finishing the report, and may not perform
 * any RA operations using @a session_baton from within the editing
 * operations of @a switch_editor.
 *
 * Use @a pool for memory allocation.
 */
svn_error_t *svn_ra_do_switch (svn_ra_session_t *session,
                               const svn_ra_reporter_t **reporter,
                               void **report_baton,
                               svn_revnum_t revision_to_switch_to,
                               const char *switch_target,
                               svn_boolean_t recurse,
                               const char *switch_url,
                               const svn_delta_editor_t *switch_editor,
                               void *switch_baton,
                               apr_pool_t *pool);

/**
 * @since New in 1.2.
 *
 * Ask the RA layer to describe the status of a working copy with respect
 * to @a revision of the repository (or HEAD, if @a revision is invalid).
 *
 * The client initially provides an @a status_editor/@a baton to the RA
 * layer; this editor contains knowledge of where the change will
 * begin in the working copy (when @c open_root() is called).
 *
 * In return, the client receives a @a reporter/@a report_baton. The
 * client then describes its working-copy revision numbers by making
 * calls into the @a reporter structure; the RA layer assumes that all
 * paths are relative to the URL used to open @a session.
 *
 * When finished, the client calls @a reporter->finish_report(). The RA
 * layer then does a complete drive of @a status_editor, ending with
 * @c close_edit(), to report, essentially, what would be modified in
 * the working copy were the client to call @c do_update().
 * @a status_target is an optional single path component will restrict
 * the scope of the status report to an entry in the directory
 * represented by the @a session_baton's URL, or empty if the entire
 * directory is meant to be examined.
 *
 * The caller may not perform any RA operations using @a session
 * before finishing the report, and may not perform any RA operations
 * using @a session from within the editing operations of @a status_editor.
 *
 * Use @a pool for memory allocation.
 */
svn_error_t *svn_ra_do_status (svn_ra_session_t *session,
                               const svn_ra_reporter_t **reporter,
                               void **report_baton,
                               const char *status_target,
                               svn_revnum_t revision,
                               svn_boolean_t recurse,
                               const svn_delta_editor_t *status_editor,
                               void *status_baton,
                               apr_pool_t *pool);

/**
 * @since New in 1.2.
 *
 * Ask the RA layer to 'diff' a working copy against @a versus_url;
 * it's another form of @c svn_ra_do_update().
 *
 *    [Please note: this function cannot be used to diff a single
 *    file, only a working copy directory.  See the @c svn_ra_do_switch()
 *    function for more details.]
 *
 * The client initially provides a @a diff_editor/@a baton to the RA
 * layer; this editor contains knowledge of where the common diff
 * root is in the working copy (when @c open_root() is called). 
 *
 * In return, the client receives a @a reporter/@a report_baton. The
 * client then describes its working-copy revision numbers by making
 * calls into the @a reporter structure; the RA layer assumes that all
 * paths are relative to the URL used to open @a session.
 *
 * When finished, the client calls @a reporter->finish_report().  The
 * RA layer then does a complete drive of @a diff_editor, ending with
 * @c close_edit(), to transmit the diff.
 *
 * @a diff_target is an optional single path component will restrict
 * the scope of the diff to an entry in the directory represented by
 * the @a session's URL, or empty if the entire directory is meant to be
 * one of the diff paths.
 *
 * The working copy will be diffed against @a versus_url as it exists
 * in revision @a revision, or as it is in head if @a revision is
 * @c SVN_INVALID_REVNUM.
 *
 * Use @a ignore_ancestry to control whether or not items being
 * diffed will be checked for relatedness first.  Unrelated items
 * are typically transmitted to the editor as a deletion of one thing
 * and the addition of another, but if this flag is @c TRUE,
 * unrelated items will be diffed as if they were related.
 *
 * The caller may not perform any RA operations using @a session before
 * finishing the report, and may not perform any RA operations using
 * @a session from within the editing operations of @a diff_editor.
 *
 * Use @a pool for memory allocation.
 */
svn_error_t *svn_ra_do_diff (svn_ra_session_t *session,
                             const svn_ra_reporter_t **reporter,
                             void **report_baton,
                             svn_revnum_t revision,
                             const char *diff_target,
                             svn_boolean_t recurse,
                             svn_boolean_t ignore_ancestry,
                             const char *versus_url,
                             const svn_delta_editor_t *diff_editor,
                             void *diff_baton,
                             apr_pool_t *pool);

/**
 * @since New in 1.2.
 *
 * Invoke @a receiver with @a receiver_baton on each log message from
 * @a start to @a end.  @a start may be greater or less than @a end; 
 * this just controls whether the log messages are processed in descending 
 * or ascending revision number order.
 *
 * If @a start or @a end is @c SVN_INVALID_REVNUM, it defaults to youngest.
 *
 * If @a paths is non-null and has one or more elements, then only show
 * revisions in which at least one of @a paths was changed (i.e., if
 * file, text or props changed; if dir, props changed or an entry
 * was added or deleted).  Each path is an <tt>const char *</tt>, relative 
 * to the @a session's common parent.
 *
 * If @a limit is non-zero only invoke @a receiver on the first @a limit
 * logs.
 *
 * If @a discover_changed_paths, then each call to receiver passes a
 * <tt>const apr_hash_t *</tt> for the receiver's @a changed_paths argument;
 * the hash's keys are all the paths committed in that revision.
 * Otherwise, each call to receiver passes null for @a changed_paths.
 *
 * If @a strict_node_history is set, copy history will not be traversed
 * (if any exists) when harvesting the revision logs for each path.
 *
 * If any invocation of @a receiver returns error, return that error
 * immediately and without wrapping it.
 *
 * If @a start or @a end is a non-existent revision, return the error
 * @c SVN_ERR_FS_NO_SUCH_REVISION, without ever invoking @a receiver.
 *
 * See also the documentation for @c svn_log_message_receiver_t.
 *
 * The caller may not invoke any RA operations using @a session from
 * within @a receiver.
 *
 * Use @a pool for memory allocation.
 */
svn_error_t *svn_ra_get_log (svn_ra_session_t *session,
                             const apr_array_header_t *paths,
                             svn_revnum_t start,
                             svn_revnum_t end,
                             int limit,
                             svn_boolean_t discover_changed_paths,
                             svn_boolean_t strict_node_history,
                             svn_log_message_receiver_t receiver,
                             void *receiver_baton,
                             apr_pool_t *pool);

/**
 * @since New in 1.2.
 *
 * Set @a *kind to the node kind associated with @a path at @a revision.  
 * If @a path does not exist under @a revision, set @a *kind to 
 * @c svn_node_none.  @a path is relative to the @a session's parent URL.
 *
 * Use @a pool for memory allocation.
 */
svn_error_t *svn_ra_check_path (svn_ra_session_t *session,
                                const char *path,
                                svn_revnum_t revision,
                                svn_node_kind_t *kind,
                                apr_pool_t *pool);

/** @since New in 1.2.
 *
 * Set @a *uuid to the repository's UUID.
 *
 * NOTE: the UUID has the same lifetime as the @a session.
 *
 * Use @a pool for temporary memory allocation.
 */
svn_error_t *svn_ra_get_uuid (svn_ra_session_t *session,
                              const char **uuid,
                              apr_pool_t *pool);

/**
 * @since New in 1.2.
 *
 * Set @a *url to the repository's root URL.  The value will not include
 * a trailing '/'.  The returned URL is guaranteed to be a prefix of the
 * @a session's URL.
 *
 * NOTE: the URL has the same lifetime as the @a session.
 *
 * Use @a pool for temporary memory allocation.
 */
svn_error_t *svn_ra_get_repos_root (svn_ra_session_t *session,
                                    const char **url,
                                    apr_pool_t *pool);

/**
 * @since New in 1.2.
 *
 * Set @a *locations to the locations at the repository revisions
 * @a location_revisions of the file @a path present at the repository in
 * revision @a peg_revision.  @a path is relative to the URL to which
 * @a session was opened.  @a location_revisions is an array of
 * @c svn_revnum_t's.  @a *locations will be a mapping from the revisions to
 * their appropriate absolute paths.  If the file doesn't exist in a
 * location_revision, that revision will be ignored.
 *
 * Use @a pool for all allocations.
 *
 * NOTE: This functionality is not available in pre-1.1 servers.  If the
 * server doesn't implement it, an @c SVN_ERR_RA_NOT_IMPLEMENTED error is
 * returned.
 */
svn_error_t *svn_ra_get_locations (svn_ra_session_t *session,
                                   apr_hash_t **locations,
                                   const char *path,
                                   svn_revnum_t peg_revision,
                                   apr_array_header_t *location_revisions,
                                   apr_pool_t *pool);

/**
 * @since New in 1.2.
 *
 * Retrieve a subset of the interesting revisions of a file @a path
 * as seen in revision @a end.  Invoke @a handler with @a handler_baton
 * as its first argument for each such revision.  @a sesson is
 * an open RA session.  @a pool is used for all allocations.  See
 * @c svn_fs_history_prev for a discussion of interesting revisions.
 *
 * If there is an interesting revision of the file that is less than or
 * equal to start, the iteration will start at that revision.  Else, the
 * iteration will start at the first revision of the file in the repository,
 * whic has to be less than or equal to end.  Note that if the function
 * succeeds, @a handler will have been called at least once.
 *
 * In a series of calls, the file contents for the first interesting revision
 * will be provided as a text delta against the empty file.  In the following
 * calls, the delta will be against the contents for the previous call.
 *
 * NOTE: This functionality is not available in pre-1.1 servers.  If the
 * server doesn't implement it, an @c SVN_ERR_RA_NOT_IMPLEMENTED error is
 * returned.
 */
svn_error_t *svn_ra_get_file_revs (svn_ra_session_t *session,
                                   const char *path,
                                   svn_revnum_t start,
                                   svn_revnum_t end,
                                   svn_ra_file_rev_handler_t handler,
                                   void *handler_baton,
                                   apr_pool_t *pool);

/** Return a @a *descriptions string (allocated in @a pool) that is a textual
 * list of all available RA libraries.
 *
 * @a unused is provided for backwards compatibility with the 1.1 API and is
 * ignored.
 */
svn_error_t *svn_ra_print_ra_libraries (svn_stringbuf_t **descriptions,
                                        void *unused,
                                        apr_pool_t *pool);



/** @deprecated Provided for backwards compatibility with the 1.1. API.
 *
 * Using this callback struct is similar to calling the public interface above.
 */
typedef struct svn_ra_plugin_t
{
  /** The proper name of the RA library, (like "ra_dav" or "ra_local") */
  const char *name;         
  
  /** Short doc string printed out by `svn --version` */
  const char *description;

  /* The vtable hooks */

  /** Call @c svn_ra_open and set @a session_baton to an object representing
   * the new session.  All other arguments are passed to @c svn_ra_open.
   */
  svn_error_t *(*open) (void **session_baton,
                        const char *repos_URL,
                        const svn_ra_callbacks_t *callbacks,
                        void *callback_baton,
                        apr_hash_t *config,
                        apr_pool_t *pool);

  /** Call @c svn_ra_get_lastest_revnum with the session associated with
   * @a session_baton and all other arguments.
   */
  svn_error_t *(*get_latest_revnum) (void *session_baton,
                                     svn_revnum_t *latest_revnum,
                                     apr_pool_t *pool);

  /** Call @c svn_ra_get_dated_revision with the session associated with
   * @a session_baton and all other arguments.
   */
  svn_error_t *(*get_dated_revision) (void *session_baton,
                                      svn_revnum_t *revision,
                                      apr_time_t tm,
                                      apr_pool_t *pool);

  /** Call @c svn_ra_change_rev_prop with the session associated with
   * @a session_baton and all other arguments.
   */
  svn_error_t *(*change_rev_prop) (void *session_baton,
                                   svn_revnum_t rev,
                                   const char *name,
                                   const svn_string_t *value,
                                   apr_pool_t *pool);

  /** Call @c svn_ra_rev_proplist with the session associated with
   * @a session_baton and all other arguments.
   */
  svn_error_t *(*rev_proplist) (void *session_baton,
                                svn_revnum_t rev,
                                apr_hash_t **props,
                                apr_pool_t *pool);

  /** Call @c svn_ra_rev_prop with the session associated with
   * @a session_baton and all other arguments.
   */
  svn_error_t *(*rev_prop) (void *session_baton,
                            svn_revnum_t rev,
                            const char *name,
                            svn_string_t **value,
                            apr_pool_t *pool);
                                   
  /** Call @c svn_ra_get_commit_editor with the session associated with
   * @a session_baton and all other arguments.
   */
  svn_error_t *(*get_commit_editor) (void *session_baton,
                                     const svn_delta_editor_t **editor,
                                     void **edit_baton,
                                     const char *log_msg,
                                     svn_commit_callback_t callback,
                                     void *callback_baton,
                                     apr_pool_t *pool);

  /** Call @c svn_ra_get_file with the session associated with
   * @a session_baton and all other arguments.
   */
  svn_error_t *(*get_file) (void *session_baton,
                            const char *path,
                            svn_revnum_t revision,
                            svn_stream_t *stream,
                            svn_revnum_t *fetched_rev,
                            apr_hash_t **props,
                            apr_pool_t *pool);

  /** Call @c svn_ra_get_dir with the session associated with
   * @a session_baton and all other arguments.
   */
  svn_error_t *(*get_dir) (void *session_baton,
                           const char *path,
                           svn_revnum_t revision,
                           apr_hash_t **dirents,
                           svn_revnum_t *fetched_rev,
                           apr_hash_t **props,
                           apr_pool_t *pool);

  /** Call @c svn_ra_do_update with the session associated with
   * @a session_baton and all other arguments.
   */
  svn_error_t *(*do_update) (void *session_baton,
                             const svn_ra_reporter_t **reporter,
                             void **report_baton,
                             svn_revnum_t revision_to_update_to,
                             const char *update_target,
                             svn_boolean_t recurse,
                             const svn_delta_editor_t *update_editor,
                             void *update_baton,
                             apr_pool_t *pool);

  /** Call @c svn_ra_do_switch with the session associated with
   * @a session_baton and all other arguments.
   */
  svn_error_t *(*do_switch) (void *session_baton,
                             const svn_ra_reporter_t **reporter,
                             void **report_baton,
                             svn_revnum_t revision_to_switch_to,
                             const char *switch_target,
                             svn_boolean_t recurse,
                             const char *switch_url,
                             const svn_delta_editor_t *switch_editor,
                             void *switch_baton,
                             apr_pool_t *pool);

  /** Call @c svn_ra_do_status with the session associated with
   * @a session_baton and all other arguments.
   */
  svn_error_t *(*do_status) (void *session_baton,
                             const svn_ra_reporter_t **reporter,
                             void **report_baton,
                             const char *status_target,
                             svn_revnum_t revision,
                             svn_boolean_t recurse,
                             const svn_delta_editor_t *status_editor,
                             void *status_baton,
                             apr_pool_t *pool);

  /** Call @c svn_ra_do_diff with the session associated with
   * @a session_baton and all other arguments.
   */
  svn_error_t *(*do_diff) (void *session_baton,
                           const svn_ra_reporter_t **reporter,
                           void **report_baton,
                           svn_revnum_t revision,
                           const char *diff_target,
                           svn_boolean_t recurse,
                           svn_boolean_t ignore_ancestry,
                           const char *versus_url,
                           const svn_delta_editor_t *diff_editor,
                           void *diff_baton,
                           apr_pool_t *pool);

  /** Call @c svn_ra_get_log with the session associated with
   * @a session_baton and all other arguments.  @a limit is set to 0.
   */
  svn_error_t *(*get_log) (void *session_baton,
                           const apr_array_header_t *paths,
                           svn_revnum_t start,
                           svn_revnum_t end,
                           svn_boolean_t discover_changed_paths,
                           svn_boolean_t strict_node_history,
                           svn_log_message_receiver_t receiver,
                           void *receiver_baton,
                           apr_pool_t *pool);

  /** Call @c svn_ra_check_path with the session associated with
   * @a session_baton and all other arguments.
   */
  svn_error_t *(*check_path) (void *session_baton,
                              const char *path,
                              svn_revnum_t revision,
                              svn_node_kind_t *kind,
                              apr_pool_t *pool);

  /** Call @c svn_ra_get_uuid with the session associated with
   * @a session_baton and all other arguments.
   */
  svn_error_t *(*get_uuid) (void *session_baton,
                            const char **uuid,
                            apr_pool_t *pool);

  /** Call @c svn_ra_get_repos_root with the session associated with
   * @a session_baton and all other arguments.
   */
  svn_error_t *(*get_repos_root) (void *session_baton,
                                  const char **url,
                                  apr_pool_t *pool);

  /** @since New in 1.1.
   *
   * Call @c svn_ra_get_locations with the session associated with
   * @a session_baton and all other arguments.
   */
  svn_error_t *(*get_locations) (void *session_baton,
                                 apr_hash_t **locations,
                                 const char *path,
                                 svn_revnum_t peg_revision,
                                 apr_array_header_t *location_revisions,
                                 apr_pool_t *pool);

  /**
   * @since New in 1.1.
   *
   * Call @c svn_ra_get_file_revs with the session associated with
   * @a session_baton and all other arguments.
   */
  svn_error_t *(*get_file_revs) (void *session_baton,
                                 const char *path,
                                 svn_revnum_t start,
                                 svn_revnum_t end,
                                 svn_ra_file_rev_handler_t handler,
                                 void *handler_baton,
                                 apr_pool_t *pool);

  /** Call @c svn_ra_get_dated_revision with the session associated with
   * @a session_baton and all other arguments.
   */
  /**
   * @since New in 1.1.
   *
   * Return the plugin's version information.
   */
  const svn_version_t *(*get_version) (void);
} svn_ra_plugin_t;

/**
 * @deprecated Provided for backwards compatibility with the 1.1 API.
 *
 * All "ra_FOO" implementations *must* export a function named
 * @c svn_ra_FOO_init() of type @c svn_ra_init_func_t.
 *
 * When called by libsvn_client, this routine adds an entry (or
 * entries) to the hash table for any URL schemes it handles.  The hash
 * value must be of type (<tt>@c svn_ra_plugin_t *</tt>).  @a pool is a 
 * pool for allocating configuration / one-time data.
 *
 * This type is defined to use the "C Calling Conventions" to ensure that
 * abi_version is the first parameter. The RA plugin must check that value
 * before accessing the other parameters.
 *
 * ### need to force this to be __cdecl on Windows... how??
 */
typedef svn_error_t *(*svn_ra_init_func_t) (int abi_version,
                                            apr_pool_t *pool,
                                            apr_hash_t *hash);

/** 
 * @deprecated Provided for backward compatibility with the 1.0 API.
 *
 * The current ABI (Application Binary Interface) version for the
 * RA plugin model. This version number will change when the ABI
 * between the SVN core (e.g. libsvn_client) and the RA plugin changes.
 *
 * An RA plugin should verify that the passed version number is acceptable
 * before accessing the rest of the parameters, and before returning any
 * information.
 *
 * It is entirely acceptable for an RA plugin to accept multiple ABI
 * versions. It can simply interpret the parameters based on the version,
 * and it can return different plugin structures.
 *
 *
 * <pre>
 * VSN  DATE        REASON FOR CHANGE
 * ---  ----------  ------------------------------------------------
 *   1  2001-02-17  Initial revision.
 *   2  2004-06-29  Preparing for svn 1.1, which adds new RA vtable funcs.
 *      2005-01-19  Rework the plugin interface and don't provide the vtable
 *                  to the client.  Separate ABI versions are no longer used.
 * </pre>
 */
#define SVN_RA_ABI_VERSION      2

/* Public RA implementations. */

/** @deprecated Provided for backwards compatibility with the 1.1 API.
 * Initialize libsvn_ra_dav. */
svn_error_t * svn_ra_dav_init (int abi_version,
                               apr_pool_t *pool,
                               apr_hash_t *hash);

/** @deprecated Provided for backwards compatibility with the 1.1 API.
 * Initialize libsvn_ra_local. */
svn_error_t * svn_ra_local_init (int abi_version,
                                 apr_pool_t *pool,
                                 apr_hash_t *hash);

/** @deprecated Provided for backwards compatibility with the 1.1 API.
 * Initialize libsvn_ra_svn. */
svn_error_t * svn_ra_svn_init (int abi_version,
                               apr_pool_t *pool,
                               apr_hash_t *hash);



/** @deprecated Provide for backwards compability with the 1.1 API.
 *
 * Initialize the compatibility wrapper, using @a pool for any allocations.
 * The caller must hold on to @a ra_baton as long as the RA library is used.
 */
svn_error_t *svn_ra_init_ra_libs (void **ra_baton, apr_pool_t *pool);

/** @deprecated Provided for backwards compatibility with the 1.1 API.
 *
 * Return an RA vtable-@a library which can handle URL.  A number of
 * svn_client_* routines will call this internally, but client apps might
 * use it too.  $a ra_baton is a baton obtained by a call to
 * @c svn_ra_init_ra_libs().
 */
svn_error_t *svn_ra_get_ra_library (svn_ra_plugin_t **library,
                                    void *ra_baton,
                                    const char *url,
                                    apr_pool_t *pool);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif  /* SVN_RA_H */
