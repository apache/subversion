/**
 * @copyright
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

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */



/* Misc. declarations */

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
 * working copy properties during update-like operations.
 *
 * This is a function type which allows the RA layer to store new
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
 * working copy properties as part of a commit.
 *
 * This is a function type which allows the RA layer to store new
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
 * (i.e., remove) wcprops.
 *
 * This is a function type which allows the RA layer to invalidate
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


/** A function type for retrieving the youngest revision from a repos.   */
typedef svn_error_t *(*svn_ra_get_latest_revnum_func_t) 
       (void *session_baton,
        svn_revnum_t *latest_revnum);


/** The update Reporter
 *
 * A vtable structure which allows a working copy to describe a
 * subset (or possibly all) of its working-copy to an RA layer.
 */
typedef struct svn_ra_reporter_t
{
  /** Describe a working copy @a path as being at a particular @a revision.
   *
   * This will *override* any previous @c set_path() calls made on parent
   * paths.  @a path is relative to the URL specified in @c open().
   */
  svn_error_t *(*set_path) (void *report_baton,
                            const char *path,
                            svn_revnum_t revision);

  /** Describing a working copy @a path as missing. */
  svn_error_t *(*delete_path) (void *report_baton,
                               const char *path);
    
  /** Like @c set_path(), but differs in that @a path in the working copy
   * (relative to the root of the report driver) isn't a reflection of
   * @a path in the repository (relative to the URL specified when
   * opening the RA layer), but is instead a reflection of a different
   * repository @a url at @a revision.
   */
  svn_error_t *(*link_path) (void *report_baton,
                             const char *path,
                             const char *url,
                             svn_revnum_t revision);

  /** WC calls this when the state report is finished; any directories
   * or files not explicitly `set' above are assumed to be at the
   * baseline revision originally passed into @c do_update().
   */
  svn_error_t *(*finish_report) (void *report_baton);

  /** If an error occurs during a report, this routine should cause the
   * filesystem transaction to be aborted & cleaned up.
   */
  svn_error_t *(*abort_report) (void *report_baton);

} svn_ra_reporter_t;



/** Authentication
 *
 * This is the 2nd draft of client-side authentication; in the first
 * draft, the client presumed to be in control of selecting auth
 * methods and then "pushing" data at the RA layer.  In this draft,
 * the RA layer is presumed to be in control; as server challenges are
 * made, it "pulls" data from the client via callbacks.
 *
 * Authenticators are small "protocol" vtables that are implemented by 
 * libsvn_client, but are driven by the RA layer.  
 *
 * (Because they're related to authentication, we define them here in
 * svn_ra.h)
 *
 * The RA layer is challenged by the server, and then fetches one of
 * these vtables in order to retrieve the necessary authentication
 * info from the client.
 *
 * @defgroup svn_ra_authentication RA client-side authentication.
 * @{
 */

/** List all known authenticator objects (protocols) here. */
enum svn_ra_auth_method
{
  svn_ra_auth_username,
  svn_ra_auth_simple_password
};
/* ### someday add other protocols here: PRIVATE_KEY, CERT, etc. */
  
/** A protocol which only needs a username.
 *
 * A protocol which only needs a username.  (used by ra_local)
 * (matches type @c svn_ra_auth_username above.)
 */
typedef struct svn_ra_username_authenticator_t
{
  /** Set @a *username to a username, allocated in @a pool, and cache it in
   * @a auth_baton.
   *
   * Set @a *username to a username, allocated in @a pool, and cache it in
   * @a auth_baton.  If @a force_prompt is set, then if possible prompt for
   * the username (i.e., do not attempt to get the information any
   * other way, such as from a file cache).  An implementation which
   * is not able to prompt is not required to prompt.
   *
   * This routine always sets @a *username something (possibly just "")
   * or returns error.
   *  
   * If this routine is @c NULL, that means the client is unable, or
   * unwilling, to get the username.
   */
  svn_error_t *(*get_username) (char **username,
                                void *auth_baton,
                                svn_boolean_t force_prompt,
                                apr_pool_t *pool);

  /** Store @a username permanently (e.g., in the working copy).
   *
   * Store @a username permanently (e.g., in the working copy).
   * If this routine is @c NULL, that means the client is unable, or
   * unwilling, to store the username.
   */
  svn_error_t *(*store_username) (const char *username,
                                  void *auth_baton);

} svn_ra_username_authenticator_t;



/** A protocol which needs a username and password.
 *
 * A protocol which needs a username and password (used by ra_dav)
 * (matches type @c svn_ra_auth_simple_password above.)
 */
typedef struct svn_ra_simple_password_authenticator_t
{
  /** Return username and password in @a *username and @a *password, and
   * cache them in @a auth_baton.
   *
   * Return username and password in @a *username and @a *password, and
   * cache them in @a auth_baton.  If @a force_prompt is set, then if
   * possible prompt the user for the information (i.e., do not
   * attempt to get the information any other way, such as from a file
   * cache).  An implementation which is not able to prompt is not
   * required to prompt.
   *
   * This routine always sets @a *username and @a *password to something
   * (possibly just ""), or returns error.
   *
   * If this routine is @c NULL, that means the client is unable, or
   * unwilling, to get the username and password.
   */
  svn_error_t *(*get_user_and_pass) (char **username,
                                     char **password,
                                     void *auth_baton,
                                     svn_boolean_t force_prompt,
                                     apr_pool_t *pool);

  /** If any authentication info is cached in @a auth_baton, store it
   * permanently.
   *
   * If any authentication info is cached in @a auth_baton, store it
   * permanently (e.g., in the working copy).
   * If this routine is @c NULL, that means the client is unable, or
   * unwilling, to store auth data.
   */
  svn_error_t *(*store_user_and_pass) (void *auth_baton);

} svn_ra_simple_password_authenticator_t;

/** @} */

/** A collection of callbacks implemented by libsvn_client which allows
 * an RA layer to "pull" information from the client application, or 
 * possibly store information.
 *
 * A collection of callbacks implemented by libsvn_client which allows
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
   *
   * Open a unique temporary file for writing in the working copy.
   * This file will be automatically deleted when @a fp is closed.
   */
  svn_error_t *(*open_tmp_file) (apr_file_t **fp,
                                 void *callback_baton);
  
  /** Retrieve an @a authenticator/@a auth_baton pair from the client,
   * which represents the protocol @a method.
   */
  svn_error_t *(*get_authenticator) (void **authenticator,
                                     void **auth_baton,
                                     enum svn_ra_auth_method method,
                                     void *callback_baton,
                                     apr_pool_t *pool);

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

/** The RA Library 
 *
 * A vtable structure which encapsulates all the functionality of a
 * particular repository-access implementation.
 *
 * Note: libsvn_client will keep an array of these objects,
 * representing all RA libraries that it has simultaneously loaded
 * into memory.  Depending on the situation, the client can look
 * through this array and find the appropriate implementation it
 * needs.
 */
typedef struct svn_ra_plugin_t
{
  /** The proper name of the ra library, (like "ra_dav" or "ra_local") */
  const char *name;         
  
  /** Short doc string printed out by `svn --version` */
  const char *description;

  /* The vtable hooks */
  
  /** Open a repository session to @a repos_url.
   *
   * Open a repository session to @a repos_url.  Return an opaque object
   * representing this session in @a *session_baton, allocated in @a pool.
   *
   * @a callbacks/@a callback_baton is a table of callbacks provided by the
   * client; see @c svn_ra_callbacks_t above.
   *
   * All RA requests require a @a session_baton; they will continue to
   * use @a pool for memory allocation.
   */
  svn_error_t *(*open) (void **session_baton,
                        const char *repos_URL,
                        const svn_ra_callbacks_t *callbacks,
                        void *callback_baton,
                        apr_pool_t *pool);

  /** Close a repository session. 
   *
   * Close a repository session.  This frees any memory used by the
   * session baton.  (To free the session baton itself, simply free
   * the pool it was created in.)
   */
  svn_error_t *(*close) (void *session_baton);

  /** Get the latest revision number from the repository.
   *
   * Get the latest revision number from the repository. This is
   * useful for the `svn status' command.  :)
   */
  svn_error_t *(*get_latest_revnum) (void *session_baton,
                                     svn_revnum_t *latest_revnum);

  /** Get the latest revision number at time @a time. */
  svn_error_t *(*get_dated_revision) (void *session_baton,
                                      svn_revnum_t *revision,
                                      apr_time_t tm);

  /** Set the property @a name to @a value on revision @a rev.
   *
   * Set the property @a name to @a value on revision @a rev.
   * Please note that properties attached to revisions are **unversioned**.
   */
  svn_error_t *(*change_rev_prop) (void *session_baton,
                                   svn_revnum_t rev,
                                   const char *name,
                                   const svn_string_t *value);

  /** Set @a *props to the list of unversioned properties attached to
   * revision @a rev.
   *
   * Set @a *props to the list of unversioned properties attached to
   * revision @a rev.  The hash maps (<tt>const char *</tt>) names to
   * (<tt>@c svn_string_t *</tt>) values.
   */
  svn_error_t *(*rev_proplist) (void *session_baton,
                                svn_revnum_t rev,
                                apr_hash_t **props);

  /** Set @a *value to the value of unversioned property @a name attached to
   * revision @a rev.
   *
   * Set @a *value to the value of unversioned property @a name attached to
   * revision @a rev.  If @a rev has no property by that name, set @a *value 
   * to @c NULL.
   */
  svn_error_t *(*rev_prop) (void *session_baton,
                            svn_revnum_t rev,
                            const char *name,
                            svn_string_t **value);
                                   
  /** Set @a *editor and @a *edit_baton to an editor for committing changes
   * to the repository, using @a log_msg as the log message.
   *
   * Set @a *editor and @a *edit_baton to an editor for committing changes
   * to the repository, using @a log_msg as the log message.  The
   * revisions being committed against are passed to the editor
   * functions, starting with the rev argument to @c open_root.  The path
   * root of the commit is in the @a session_baton's url.
   *
   * These three functions all share @c close_baton:
   *
   *   * @c get_func is used by the RA layer to fetch any WC properties
   *     during the commit.
   *
   *   * @c set_func is used by the RA layer to set any WC properties,
   *     after the commit completes. 
   *
   *   * @c close_func is used by the RA layer to bump the revisions of
   *     each committed item, after the commit completes.  It may be
   *     called multiple times.
   *
   * Any of these functions may be @c NULL.
   *
   * After the editor's close_edit function returns successfully,
   * @a *new_rev holds either the new revision, or @c SVN_INVALID_REVNUM if
   * the commit was a no-op (i.e. nothing was committed);
   * @a *committed_date holds the repository-side date, or NULL if the
   * date is unavailable; @a *committed_author holds the repository-side
   * author (i.e., the one recorded as the author of the commit in the
   * repository), or @c NULL if the author is unavailable.  Allocate the
   * latter two in the session.  Any of @a new_rev, @a committed_date, or
   * @a committed_author may be @c NULL, in which case not touched.
   */
  svn_error_t *(*get_commit_editor) (void *session_baton,
                                     const svn_delta_editor_t **editor,
                                     void **edit_baton,
                                     svn_revnum_t *new_rev,
                                     const char **committed_date,
                                     const char **committed_author,
                                     const char *log_msg);

  /** Fetch the contents and properties of file @a path at @a revision.
   *
   * Fetch the contents and properties of file @a path at @a revision.
   * Interpret @a path relative to the url in @a session_baton.
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
   */
  svn_error_t *(*get_file) (void *session_baton,
                            const char *path,
                            svn_revnum_t revision,
                            svn_stream_t *stream,
                            svn_revnum_t *fetched_rev,
                            apr_hash_t **props);

  /** If @a dirents is non @c NULL, set @a *dirents to contain all the entries
   * of directory @a path at @a revision.
   *
   * If @a dirents is non @c NULL, set @a *dirents to contain all the entries
   * of directory @a path at @a revision.  The keys of @a dirents will be 
   * entry names (<tt>const char *</tt>), and the values dirents 
   * (<tt>@c svn_dirent_t *</tt>).
   *
   * @a path is interpreted relative to the url in @a session_baton.  
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
  svn_error_t *(*get_dir) (void *session_baton,
                           const char *path,
                           svn_revnum_t revision,
                           apr_hash_t **dirents,
                           svn_revnum_t *fetched_rev,
                           apr_hash_t **props);


  /** Check out revision @a revision of the url specified in
   * @a session_baton, using @a editor and @a edit_baton to create the 
   * working copy.
   *
   * Check out revision @a revision of the url specified in
   * @a session_baton, using @a editor and @a edit_baton to create the 
   * working copy.  If @a recurse is non-zero, create the full working tree, 
   * else just its topmost directory. 
   *
   * Do a complete drive of @a editor, ending with a call to @c close_edit().
   */
  svn_error_t *(*do_checkout) (void *session_baton,
                               svn_revnum_t revision,
                               svn_boolean_t recurse,
                               const svn_delta_editor_t *editor,
                               void *edit_baton);


  /** Ask the network layer to update a working copy.
   *
   * Ask the network layer to update a working copy.
   *
   * The client initially provides an @a update_editor/@a baton to the 
   * RA layer; this editor contains knowledge of where the change will
   * begin in the working copy (when @c open_root() is called).
   *
   * In return, the client receives a @a reporter/@a report_baton. The
   * client then describes its working-copy revision numbers by making
   * calls into the @a reporter structure; the RA layer assumes that all
   * paths are relative to the URL used to create @a session_baton.
   *
   * When finished, the client calls @a reporter->finish_report().  The
   * RA layer then does a complete drive of @a update_editor, ending with
   * @c close_edit(), to update the working copy.
   *
   * @a update_target is an optional single path component will restrict
   * the scope of things affected by the update to an entry in the
   * directory represented by the @a session_baton's URL, or @c NULL if the
   * entire directory is meant to be updated.
   *
   * The working copy will be updated to @a revision_to_update_to, or the
   * "latest" revision if this arg is invalid.
   */
  svn_error_t *(*do_update) (void *session_baton,
                             const svn_ra_reporter_t **reporter,
                             void **report_baton,
                             svn_revnum_t revision_to_update_to,
                             const char *update_target,
                             svn_boolean_t recurse,
                             const svn_delta_editor_t *update_editor,
                             void *update_baton);

  /** Ask the network layer to 'switch' a working copy to a new
   * @a switch_url.
   *
   * Ask the network layer to 'switch' a working copy to a new
   * @a switch_url;  it's another form of @c do_update().
   *
   *    [Please note: this function cannot be used to switch a single
   *    file, only a working copy directory.  The main caller of this
   *    routine, @c svn_client_switch, uses @c get_file and
   *    @c svn_wc_install_file to switch a single file.]
   *
   * The client initially provides an @a update_editor/@a baton to the RA
   * layer; this editor contains knowledge of where the change will
   * begin in the working copy (when @c open_root() is called). 
   *
   * In return, the client receives a @a reporter/@a report_baton. The
   * client then describes its working-copy revision numbers by making
   * calls into the @a reporter structure; the RA layer assumes that all
   * paths are relative to the URL used to create @a session_baton.
   *
   * When finished, the client calls @a reporter->finish_report().  The
   * RA layer then does a complete drive of @a update_editor, ending with
   * @c close_edit(), to switch the working copy.
   *
   * @a update_target is an optional single path component will restrict
   * the scope of things affected by the update to an entry in the
   * directory represented by the @a session_baton's URL, or @c NULL if the
   * entire directory is meant to be updated.
   *
   * The working copy will be updated to @a revision_to_update_to, or the
   * "latest" revision if this arg is invalid.
   */
  svn_error_t *(*do_switch) (void *session_baton,
                             const svn_ra_reporter_t **reporter,
                             void **report_baton,
                             svn_revnum_t revision_to_update_to,
                             const char *update_target,
                             svn_boolean_t recurse,
                             const char *switch_url,
                             const svn_delta_editor_t *update_editor,
                             void *update_baton);

  /** Ask the network layer to describe the status of a working copy
   * with respect to the @a head revision of the repository.
   *
   * Ask the network layer to describe the status of a working copy
   * with respect to the @a head revision of the repository.
   *
   * The client initially provides an @a status_editor/@a baton to the RA
   * layer; this editor contains knowledge of where the change will
   * begin in the working copy (when @c open_root() is called).
   *
   * In return, the client receives a @a reporter/@a report_baton. The
   * client then describes its working-copy revision numbers by making
   * calls into the @a reporter structure; the RA layer assumes that all
   * paths are relative to the URL used to create @a session_baton.
   *
   * When finished, the client calls @a reporter->finish_report(). The RA
   * layer then does a complete drive of @a status_editor, ending with
   * @c close_edit(), to report, essentially, what would be modified in
   * the working copy were the client to call @c do_update().
   * @a status_target is an optional single path component will restrict
   * the scope of the status report to an entry in the directory
   * represented by the @a session_baton's URL, or @c NULL if the entire
   * directory is meant to be examined.
   */
  svn_error_t *(*do_status) (void *session_baton,
                             const svn_ra_reporter_t **reporter,
                             void **report_baton,
                             const char *status_target,
                             svn_boolean_t recurse,
                             const svn_delta_editor_t *status_editor,
                             void *status_baton);


  /** Ask the network layer to 'diff' a working copy against @a versus_url.
   *
   * Ask the network layer to 'diff' a working copy against @a versus_url;
   * it's another form of @c do_update().
   *
   *    [Please note: this function cannot be used to diff a single
   *    file, only a working copy directory.  See the do_switch()
   *    function for more details.]
   *
   * The client initially provides a @a diff_editor/@a baton to the RA
   * layer; this editor contains knowledge of where the common diff
   * root is in the working copy (when @c open_root() is called). 
   *
   * In return, the client receives a @a reporter/@a report_baton. The
   * client then describes its working-copy revision numbers by making
   * calls into the @a reporter structure; the RA layer assumes that all
   * paths are relative to the URL used to create @a session_baton.
   *
   * When finished, the client calls @a reporter->finish_report().  The
   * RA layer then does a complete drive of @a diff_editor, ending with
   * @c close_edit(), to transmit the diff.
   *
   * @a diff_target is an optional single path component will restrict
   * the scope of the diff to an entry in the directory represented by
   * the @a session_baton's URL, or @c NULL if the entire directory is 
   * meant to be one of the diff paths.
   *
   * The working copy will be diffed against @a versus_url as it exists
   * in revision @a revision, or as it is in head if @a revision is
   * @c SVN_INVALID_REVNUM.
   */
  svn_error_t *(*do_diff) (void *session_baton,
                           const svn_ra_reporter_t **reporter,
                           void **report_baton,
                           svn_revnum_t revision,
                           const char *diff_target,
                           svn_boolean_t recurse,
                           const char *versus_url,
                           const svn_delta_editor_t *diff_editor,
                           void *diff_baton);

  /** Invoke @a receiver with @a receiver_baton on each log message from
   * @a start to @a end.
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
   * to the session's common parent.
   *
   * If @a discover_changed_paths, then each call to receiver passes a
   * <tt>const apr_hash_t *<tt> for the receiver's @a changed_paths argument;
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
   */
  svn_error_t *(*get_log) (void *session_baton,
                           const apr_array_header_t *paths,
                           svn_revnum_t start,
                           svn_revnum_t end,
                           svn_boolean_t discover_changed_paths,
                           svn_boolean_t strict_node_history,
                           svn_log_message_receiver_t receiver,
                           void *receiver_baton);

  /* Yoshiki Hayashi <yoshiki@xemacs.org> points out that a more
     generic way to support 'discover_changed__paths' in logs would be
     to have these two functions:
    
         svn_error_t *(*get_rev_prop) (void *session_baton,
                                       svn_string_t **value,
                                       svn_string_t *name,
                                       svn_revnum_t revision);
    
         svn_error_t *(get_changed_paths) (void *session_baton,
                                           apr_array_header_t **changed_paths,
                                           svn_revnum_t revision);
    
     Although log requests are common enough to deserve special
     support (to optimize network usage), these two more generic
     functions are still good ideas.  Don't want to implement them
     right now, as am concentrating on the log functionality, but we
     will probably want them eventually, hence this start block.  */


  /** Set @a *kind to node kind associated with @a path at @a revision.
   *
   * Set @a *kind to node kind associated with @a path at @a revision.  
   * If @a path does not exist under @a revision, set @a *kind to 
   * @c svn_node_none.  @a path is relative to the session's parent URL.
   */
  svn_error_t *(*check_path) (svn_node_kind_t *kind,
                              void *session_baton,
                              const char *path,
                              svn_revnum_t revision);


} svn_ra_plugin_t;


/**
 * libsvn_client will be reponsible for loading each RA DSO it needs.
 * However, all "ra_FOO" implementations *must* export a function named
 * @c svn_ra_FOO_init() of type @c svn_ra_init_func_t.
 *
 * When called by libsvn_client, this routine adds an entry (or
 * entries) to the hash table for any URL schemes it handles. The hash
 * value must be of type (<tt>@c svn_ra_plugin_t *</tt>). @a pool is a 
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

/** The current ABI (Application Binary Interface) version for the
 * RA plugin model.
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
 * </pre>
 */
#define SVN_RA_ABI_VERSION      1


/* Public RA implementations: ADD MORE HERE as necessary. */

/** initialize libsvn_ra_dav. */
svn_error_t * svn_ra_dav_init (int abi_version,
                               apr_pool_t *pool,
                               apr_hash_t *hash);

/** initialize libsvn_ra_local. */
svn_error_t * svn_ra_local_init (int abi_version,
                                 apr_pool_t *pool,
                                 apr_hash_t *hash);

/** initialize libsvn_ra_svn. */
svn_error_t * svn_ra_svn_init (int abi_version,
                               apr_pool_t *pool,
                               apr_hash_t *hash);



/* Public Interfaces */

/** Initialize the RA libraries.
 *
 * Every user of the RA layer *must* call this routine and hold on to
 * the @a ra_baton returned.  This baton contains all known methods of
 * accessing a repository, for use within most @c svn_client_* routines.
 */
svn_error_t * svn_ra_init_ra_libs (void **ra_baton, apr_pool_t *pool);


/** Return an ra vtable-@a library (already within @a ra_baton) which can
 * handle URL.
 *
 * Return an ra vtable-@a library (already within @a ra_baton) which can
 * handle URL.  A number of svn_client_* routines will call this
 * internally, but client apps might use it too.  
 *
 * For reference, note that according to W3 RFC 1738, a valid URL is
 * of the following form:
 *
 *   scheme://\<user\>:\<password\>\@\<host\>:\<port\>/\<url-path\> 
 *
 * Common URLs are as follows:
 *
 *   http://subversion.tigris.org/index.html
 *   file:///home/joeuser/documents/resume.txt
 *
 * Of interest is the file URL schema, which takes the form
 * file://\<host\>/\<path\>, where \<host\> and \<path\> are optional.  The 
 * `/' between \<host\> and \<path\> is NOT part of path, yet the RFC doesn't
 * specify how \<path\> should be formatted.  SVN will count on the
 * portability layer to be able to handle the specific formatting of
 * the \<path\> on a per-system basis.
 */
svn_error_t *svn_ra_get_ra_library (svn_ra_plugin_t **library,
                                    void *ra_baton,
                                    const char *url,
                                    apr_pool_t *pool);

/** Return a @a *descriptions string (allocated in @a pool) that is a textual
 * list of all available RA libraries.
 */
svn_error_t *svn_ra_print_ra_libraries (svn_stringbuf_t **descriptions,
                                        void *ra_baton,
                                        apr_pool_t *pool);





#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif  /* SVN_RA_H */
