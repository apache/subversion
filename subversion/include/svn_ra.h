/*
 * svn_ra.h :  structures related to repository access
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




#ifndef SVN_RA_H
#define SVN_RA_H

#include <apr_pools.h>
#include <apr_tables.h>

#include "svn_error.h"
#include "svn_delta.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/*----------------------------------------------------------------------*/

/* Misc. declarations */


/* A function type which allows the RA layer to fetch WC properties
   during a commit.  */
typedef svn_error_t *(*svn_ra_get_wc_prop_func_t) (void *close_baton,
                                                   svn_stringbuf_t *path,
                                                   svn_stringbuf_t *name,
                                                   svn_stringbuf_t **value);

/* A function type which allows the RA layer to store WC properties
   after a commit.  */
typedef svn_error_t *(*svn_ra_set_wc_prop_func_t) (void *close_baton,
                                                   svn_stringbuf_t *path,
                                                   svn_stringbuf_t *name,
                                                   svn_stringbuf_t *value);


/* A function type for "cleaning up" after a commit.  The client layer
   supplies this routine to an RA layer.  RA calls this routine on
   each PATH that was committed, allowing the client to bump revision
   numbers, possibly recursively. */
typedef svn_error_t *(*svn_ra_close_commit_func_t) (void *close_baton,
                                                    svn_stringbuf_t *path,
                                                    svn_boolean_t recurse,
                                                    svn_revnum_t new_rev);

/* A function type for retrieving the youngest revision from a repos.   */
typedef svn_error_t *(*svn_ra_get_latest_revnum_func_t) 
       (void *session_baton,
        svn_revnum_t *latest_revnum);



/*----------------------------------------------------------------------*/

/* The update Reporter */

/* A vtable structure which allows a working copy to describe a
   subset (or possibly all) of its working-copy to an RA layer. */
  
typedef struct svn_ra_reporter_t
{
  /* Describe a working copy PATH as being at a particular REVISION;
     this will *override* any previous set_path() calls made on parent
     paths.  PATH is relative to the URL specified in open(), and must
     be given in `svn_path_url_style'. */
  svn_error_t *(*set_path) (void *report_baton,
                            svn_stringbuf_t *path,
                            svn_revnum_t revision);

  /* Describing a working copy PATH as missing. */
  svn_error_t *(*delete_path) (void *report_baton,
                               svn_stringbuf_t *path);
    
  /* WC calls this when the state report is finished; any directories
     or files not explicitly `set' above are assumed to be at the
     baseline revision originally passed into do_update(). */
  svn_error_t *(*finish_report) (void *report_baton);

  /* If an error occurs during a report, this routine should cause the
     filesystem transaction to be aborted & cleaned up. */
  svn_error_t *(*abort_report) (void *report_baton);

} svn_ra_reporter_t;

/*----------------------------------------------------------------------*/

/*** Authentication ***/

/* This is the 2nd draft of client-side authentication; in the first
   draft, the client presumed to be in control of selecting auth
   methods and then "pushing" data at the RA layer.  In this draft,
   the RA layer is presumed to be in control; as server challenges are
   made, it "pulls" data from the client via callbacks. */


/* List all known authenticator objects (protocols) here. */
#define SVN_RA_AUTH_USERNAME                       0x0001
#define SVN_RA_AUTH_SIMPLE_PASSWORD                0x0002
/* ### someday add other protocols here: PRIVATE_KEY, CERT, etc. */
  

/* Authenticators: these are small "protocol" vtables that are
   implemented by libsvn_client, but are driven by the RA layer.  

   (Because they're related to authentication, we define them here in
   svn_ra.h)

   The RA layer is challenged by the server, and then fetches one of
   these vtables in order to retrieve the necessary authentication
   info from the client.  */


/* A protocol which only needs a username.  (used by ra_local)
   (matches type SVN_RA_AUTH_USERNAME above.)  */
typedef struct svn_ra_username_authenticator_t
{
  /* Get a username from the client. */
  svn_error_t *(*get_username) (char **username,
                                void *auth_baton,
                                svn_boolean_t force_prompt,
                                apr_pool_t *pool);

  /* Store a username in the client. */
  svn_error_t *(*store_username) (const char *username,
                                  void *auth_baton);

} svn_ra_username_authenticator_t;



/* A protocol which needs a username and password (used by ra_dav)
   (matches type SVN_RA_AUTH_SIMPLE_PASSWORD above.)  */
typedef struct svn_ra_simple_password_authenticator_t
{
  /* Get a username and password from the client.  If FORCE_PROMPT is
     set, then a prompt will be displayed to the user automatically
     (rather than looking for cached info from command-line or file.)
     
     *USERNAME and *PASSWORD will not only be returned to the RA
     layer, but libsvn_client will also cache them in the AUTH_BATON.  */
  svn_error_t *(*get_user_and_pass) (char **username,
                                     char **password,
                                     void *auth_baton,
                                     svn_boolean_t force_prompt,
                                     apr_pool_t *pool);

  /* If any authentication info has been cached in AUTH_BATON (as a
     result of calling get_user_and_pass), ask the client to store it
     in the working copy.

     If this routine is NULL, that means the client is unable (or
     unwilling) to store auth data. */
  svn_error_t *(*store_user_and_pass) (void *auth_baton);

} svn_ra_simple_password_authenticator_t;



/* A collection of callbacks implemented by libsvn_client which allows
   an RA layer to "pull" information from the client application, or
   possibly store information.  libsvn_client passes this vtable to
   RA->open().  

   Each routine takes a CALLBACK_BATON originally provided with the
   vtable. */
typedef struct svn_ra_callbacks_t
{
  /* Open a unique temporary file for writing in the working copy.
     This file will be automatically deleted when FP is closed. */
  svn_error_t *(*open_tmp_file) (apr_file_t **fp,
                                 void *callback_baton);
  
  /* Retrieve an AUTHENTICATOR/AUTH_BATON pair from the client,
     which represents the protocol METHOD.  */
  svn_error_t *(*get_authenticator) (void **authenticator,
                                     void **auth_baton,
                                     apr_uint64_t method,
                                     void *callback_baton,
                                     apr_pool_t *pool);

} svn_ra_callbacks_t;

/* ### will svn_ra_callbacks_t need its own baton?  probably .*/


/*----------------------------------------------------------------------*/

/* The RA Library */


/* A vtable structure which encapsulates all the functionality of a
   particular repository-access implementation.

   Note: libsvn_client will keep an array of these objects,
   representing all RA libraries that it has simultaneously loaded
   into memory.  Depending on the situation, the client can look
   through this array and find the appropriate implementation it
   needs. */

typedef struct svn_ra_plugin_t
{
  /* The proper name of the ra library, (e.g. "ra_dav" or "ra_local") */
  const char *name;         
  
  /* Short doc string printed out by `svn -v` */
  const char *description;

  /* The vtable hooks */
  
  /* Open a repository session to REPOS_URL.  Return an opaque object
     representing this session in *SESSION_BATON, allocated in POOL.

     CALLBACKS/CALLBACK_BATON is a table of callbacks provided by the
     client; see svn_ra_callbacks_t above.

     All RA requests require a session_baton; they will continue to
     use POOL for memory allocation. */
  svn_error_t *(*open) (void **session_baton,
                        svn_stringbuf_t *repos_URL,
                        svn_ra_callbacks_t *callbacks,
                        void *callback_baton,
                        apr_pool_t *pool);

  /* Close a repository session.  This frees any memory used by the
     session baton.  (To free the session baton itself, simply free
     the pool it was created in.) */
  svn_error_t *(*close) (void *session_baton);

  /* Get the latest revision number from the repository. This is
     useful for the `svn status' command.  :) */
  svn_error_t *(*get_latest_revnum) (void *session_baton,
                                     svn_revnum_t *latest_revnum);

  /* Get the latest revision number at time TIME. */
  svn_error_t *(*get_dated_revision) (void *session_baton,
                                      svn_revnum_t *revision,
                                      apr_time_t tm);

  /* Begin a commit against `rev:path' using LOG_MSG as the log
     message.  `rev' is the argument that will be passed to
     open_root(), and `path' is built into the SESSION_BATON's URL.
     
     RA returns an *EDITOR and *EDIT_BATON capable of transmitting a
     commit to the repository, which is then driven by the client.

     The client may supply three functions to the RA layer, each of
     which requires the CLOSE_BATON:

       * The GET_FUNC will be used by the RA layer to fetch any WC
         properties during the commit.

       * The SET_FUNC will be used by the RA layer to set any WC
         properties, after the commit completes.

       * The CLOSE_FUNC will be used by the RA layer to bump the
         revisions of each committed item, after the commit completes.

     Any of these functions may be null.
          
  */
  svn_error_t *(*get_commit_editor) (void *session_baton,
                                     const svn_delta_edit_fns_t **editor,
                                     void **edit_baton,
                                     svn_stringbuf_t *log_msg,
                                     svn_ra_get_wc_prop_func_t get_func,
                                     svn_ra_set_wc_prop_func_t set_func,
                                     svn_ra_close_commit_func_t close_func,
                                     void *close_baton);

  /* Feed the contents of URL at REVISION into STREAM as part of
     session SESSION_BATON. */
  svn_error_t *(*get_file) (void *session_baton,
                            svn_stringbuf_t *url,
                            svn_revnum_t revision,
                            svn_stream_t *stream);


  /* Check out revision REVISION of the url specified in
     SESSION_BATON, using EDITOR and EDIT_BATON to create the working
     copy.  If RECURSE is non-zero, create the full working tree, else
     just its topmost directory. */
  svn_error_t *(*do_checkout) (void *session_baton,
                               svn_revnum_t revision,
                               svn_boolean_t recurse,
                               const svn_delta_edit_fns_t *editor,
                               void *edit_baton);


  /* Ask the network layer to update a working copy.

     The client initially provides an UPDATE_EDITOR/BATON to the RA
     layer; this editor contains knowledge of where the change will
     begin in the working copy (when open_root() is called).

     In return, the client receives a REPORTER/REPORT_BATON. The
     client then describes its working-copy revision numbers by making
     calls into the REPORTER structure; the RA layer assumes that all
     paths are relative to the URL used to create SESSION_BATON.

     When finished, the client calls REPORTER->finish_report(). The RA
     layer then drives UPDATE_EDITOR to update the working copy.
     UPDATE_TARGET is an optional single path component will restrict
     the scope of things affected by the update to an entry in the
     directory represented by the SESSION_BATON's URL, or NULL if the
     entire directory is meant to be updated.

     The working copy will be updated to REVISION_TO_UPDATE_TO, or the
     "latest" revision if this arg is invalid. */
  svn_error_t *(*do_update) (void *session_baton,
                             const svn_ra_reporter_t **reporter,
                             void **report_baton,
                             svn_revnum_t revision_to_update_to,
                             svn_stringbuf_t *update_target,
                             svn_boolean_t recurse,
                             const svn_delta_edit_fns_t *update_editor,
                             void *update_baton);

  /* Ask the network layer to describe the status of a working copy
     with respect to the HEAD revision of the repository.

     The client initially provides an STATUS_EDITOR/BATON to the RA
     layer; this editor contains knowledge of where the change will
     begin in the working copy (when open_root() is called).

     In return, the client receives a REPORTER/REPORT_BATON. The
     client then describes its working-copy revision numbers by making
     calls into the REPORTER structure; the RA layer assumes that all
     paths are relative to the URL used to create SESSION_BATON.

     When finished, the client calls REPORTER->finish_report(). The RA
     layer then drives STATUS_EDITOR to report, essentially, what
     would would be modified in the working copy were the client to
     call do_update().  STATUS_TARGET is an optional single path
     component will restrict the scope of the status report to an
     entry in the directory represented by the SESSION_BATON's URL, or
     NULL if the entire directory is meant to be examined. */
  svn_error_t *(*do_status) (void *session_baton,
                             const svn_ra_reporter_t **reporter,
                             void **report_baton,
                             svn_stringbuf_t *status_target,
                             svn_boolean_t recurse,
                             const svn_delta_edit_fns_t *status_editor,
                             void *status_baton);

  /* Invoke RECEIVER with RECEIVER_BATON on each log message from
   * START to END.  START may be greater or less than END; this just
   * controls whether the log messages are processed in descending or
   * ascending revision number order.
   *
   * If START or END is SVN_INVALID_REVNUM, it defaults to youngest.
   *
   * If PATHS is non-null and has one or more elements, then only show
   * revisions in which at least one of PATHS was changed (i.e., if
   * file, text or props changed; if dir, props changed or an entry
   * was added or deleted).  Each path is an svn_stringbuf_t *,
   * relative to the session's common parent.
   *
   * If DISCOVER_CHANGED_PATHS, then each call to receiver passes a
   * `const apr_hash_t *' for the receiver's CHANGED_PATHS argument;
   * the hash's keys are all the paths committed in that revision.
   * Otherwise, each call to receiver passes null for CHANGED_PATHS.
   *
   * If any invocation of RECEIVER returns error, return that error
   * immediately and without wrapping it.
   *
   * See also the documentation for `svn_log_message_receiver_t'.
   */
  svn_error_t *(*get_log) (void *session_baton,
                           const apr_array_header_t *paths,
                           svn_revnum_t start,
                           svn_revnum_t end,
                           svn_boolean_t discover_changed_paths,
                           svn_log_message_receiver_t receiver,
                           void *receiver_baton);

  /* Set *KIND to node kind associated with PATH at REVISION.  If PATH
   * does not exist under REVISION, set *KIND to svn_node_none.  PATH
   * is relative to the session's parent URL.
   */
  svn_error_t *(*check_path) (svn_node_kind_t *kind,
                              void *session_baton,
                              const char *path,
                              svn_revnum_t revision);

  /* Yoshiki Hayashi <yoshiki@xemacs.org> points out that a more
   * generic way to support the above would be to have these two
   * functions:
   *
   *     svn_error_t *(*get_rev_prop) (void *session_baton,
   *                                   svn_string_t **value,
   *                                   svn_string_t *name,
   *                                   svn_revnum_t revision);
   *
   *     svn_error_t *(get_changed_paths) (void *session_baton,
   *                                       apr_array_header_t **changed_paths,
   *                                       svn_revnum_t revision);
   *
   * Although log requests are common enough to deserve special
   * support (to optimize network usage), these two more generic
   * functions are still good ideas.  Don't want to implement them
   * right now, as am concentrating on the log functionality, but
   * we will probably want them eventually, hence this start block.
   */

} svn_ra_plugin_t;


/* svn_ra_init_func_t :
   
   libsvn_client will be reponsible for loading each RA DSO it needs.
   However, all "ra_FOO" implementations *must* export a function named
   `svn_ra_FOO_init()' of type `svn_ra_init_func_t'.

   When called by libsvn_client, this routine adds an entry (or
   entries) to the hash table for any URL schemes it handles. The hash
   value must be of type (svn_ra_plugin_t *). POOL is a pool for
   allocating configuration / one-time data.

   This type is defined to use the "C Calling Conventions" to ensure that
   abi_version is the first parameter. The RA plugin must check that value
   before accessing the other parameters.

   ### need to force this to be __cdecl on Windows... how??
*/
typedef svn_error_t *(*svn_ra_init_func_t) (int abi_version,
                                            apr_pool_t *pool,
                                            apr_hash_t *hash);

/* The current ABI (Application Binary Interface) version for the
   RA plugin model. This version number will change when the ABI
   between the SVN core (e.g. libsvn_client) and the RA plugin changes.

   An RA plugin should verify that the passed version number is acceptable
   before accessing the rest of the parameters, and before returning any
   information.

   It is entirely acceptable for an RA plugin to accept multiple ABI
   versions. It can simply interpret the parameters based on the version,
   and it can return different plugin structures.


   VSN  DATE        REASON FOR CHANGE
   ---  ----------  ------------------------------------------------
     1  2001-02-17  Initial revision.
*/
#define SVN_RA_ABI_VERSION      1


/** Public RA implementations: ADD MORE HERE as necessary. **/

svn_error_t * svn_ra_dav_init (int abi_version,
                               apr_pool_t *pool,
                               apr_hash_t *hash);
svn_error_t * svn_ra_local_init (int abi_version,
                                 apr_pool_t *pool,
                                 apr_hash_t *hash);


/*----------------------------------------------------------------------*/

/*** Public Interfaces ***/

/* Every user of the RA layer *must* call this routine and hold on to
   the RA_BATON returned.  This baton contains all known methods of
   accessing a repository, for use within most svn_client_* routines. */
svn_error_t * svn_ra_init_ra_libs (void **ra_baton, apr_pool_t *pool);


/* Return an ra vtable-LIBRARY (already within RA_BATON) which can
   handle URL.  A number of svn_client_* routines will call this
   internally, but client apps might use it too.  

   For reference, note that according to W3 RFC 1738, a valid URL is
   of the following form:

     scheme://<user>:<password>@<host>:<port>/<url-path> 

   Common URLs are as follows:

     http://subversion.tigris.org/index.html
     file:///home/joeuser/documents/resume.txt

   Of interest is the file URL schema, which takes the form
   file://<host>/<path>, where <host> and <path> are optional.  The `/'
   between <host> and <path> is NOT part of path, yet the RFC doesn't
   specify how <path> should be formatted.  SVN will count on the
   portability layer to be able to handle the specific formatting of
   the <path> on a per-system basis. */
svn_error_t *svn_ra_get_ra_library (svn_ra_plugin_t **library,
                                    void *ra_baton,
                                    const char *URL,
                                    apr_pool_t *pool);

/* Return a *DESCRIPTIONS string (allocated in POOL) that is a textual
   list of all available RA libraries. */
svn_error_t *svn_ra_print_ra_libraries (svn_stringbuf_t **descriptions,
                                        void *ra_baton,
                                        apr_pool_t *pool);





#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif  /* SVN_RA_H */


/* ----------------------------------------------------------------
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
