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
   numbers. */
typedef svn_error_t *(*svn_ra_close_commit_func_t) (void *close_baton,
                                                    svn_stringbuf_t *path,
                                                    svn_revnum_t new_rev);

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

/* Authentication Methods */

/* Different RA implementations are free to implement whatever
   authentication protocols they choose.  They must define them here,
   however, so that all clients are aware of their existence.  Client
   may choose to support them or not.

   Each protocol requires the definition of an "authenticator" vtable
   here.  This vtable has routines for getting/setting information;
   the client is assumed to know the protocol and the proper use of
   these routines.

   Once information has been exchanged, the client calls
   authenticate().  Every authenticator has a final authenticate()
   routine that is defined to return either a session_baton (a
   repository handle) or another authenticator object (only for those
   protocols that use multi-phase challenges.) */


/* List all known authenticator objects (protocols) here. */
#define SVN_RA_AUTH_USERNAME                       0x0001
#define SVN_RA_AUTH_SIMPLE_PASSWORD                0x0002




/* A protocol which only needs a username.  (like ra_local) */
typedef struct svn_ra_username_authenticator_t
{
  /* Set the username to USERNAME. */
  svn_error_t *(*set_username) (const char *username, void *auth_baton);

  /* Authenticate the set username, passing the AUTH_BATON returned with
     this authenticator structure. If successful, return a valid session
     handle in *SESSION_BATON. If authentication fails,
     SVN_ERR_RA_NOT_AUTHORIZED is returned. */
  svn_error_t *(*authenticate) (void **session_baton, void *auth_baton);

} svn_ra_username_authenticator_t;




/* A protocol which only needs a name and password.  (like ra_dav) */
typedef struct svn_ra_simple_password_authenticator_t
{
  /* Set the username to USERNAME. */
  svn_error_t *(*set_username) (const char *username, void *auth_baton);
  
  /* Set the password to PASSWORD. */
  svn_error_t *(*set_password) (const char *password, void *auth_baton);

  /* Authenticate the set username & password, passing the AUTH_BATON
     returned with this authenticator structure. If successful, return a
     valid session handle in *SESSION_BATON.  If authentication fails,
     SVN_ERR_RA_NOT_AUTHORIZED is returned. */
  svn_error_t *(*authenticate) (void **session_baton, void *auth_baton);

} svn_ra_simple_password_authenticator_t;




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

  /* Flags that describe all supported authentication methods */
  apr_uint64_t auth_methods;

  /* The vtable hooks */
  
  /* Begin an RA session to REPOS_URL, using authentication method
     METHOD.  Return a vtable structure in *AUTHENTICATOR that handles
     the method; its corresponding baton is returned in *AUTH_BATON. If
     authenticator object is driven successfully, the reward will be a
     session_baton. (See previous auth section.)

     POOL will be the place where the authenticator, auth_baton and
     session_baton are allocated, as well as the storage area used by
     further calls to RA routines. */
  svn_error_t *(*get_authenticator) (const void **authenticator,
                                     void **auth_baton,
                                     svn_stringbuf_t *repos_URL,
                                     apr_uint64_t method,
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
     replace_root(), and `path' is built into the SESSION_BATON's URL.
     
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

  /* Ask the network layer to check out a copy of the repository URL
     specified in open(), using EDITOR and EDIT_BATON to create a
     working copy. */
  svn_error_t *(*do_checkout) (void *session_baton,
                               svn_revnum_t revision,
                               const svn_delta_edit_fns_t *editor,
                               void *edit_baton);


  /* Ask the network layer to update a working copy.

     The client initially provides an UPDATE_EDITOR/BATON to the RA
     layer; this editor contains knowledge of where the change will
     begin in the working copy (when replace_root() is called).

     In return, the client receives a REPORTER/REPORT_BATON. The
     client then describes its working-copy revision numbers by making
     calls into the REPORTER structure; the RA layer assumes that all
     paths are relative to the URL used to create SESSION_BATON.

     When finished, the client calls REPORTER->finish_report(). The RA
     layer then drives UPDATE_EDITOR to update the working copy.

     The working copy will be updated to REVISION_TO_UPDATE_TO, or the
     "latest" revision if this arg is invalid. */
  svn_error_t *(*do_update) (void *session_baton,
                             const svn_ra_reporter_t **reporter,
                             void **report_baton,
                             svn_revnum_t revision_to_update_to,
                             const svn_delta_edit_fns_t *update_editor,
                             void *update_baton);

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
