/*
 * svn_ra.h :  structures related to repository access
 *
 * ====================================================================
 * Copyright (c) 2000 CollabNet.  All rights reserved.
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





/* A function type for "cleaning up" after a commit.  The client layer
   supplies this routine to an RA layer.  RA calls this routine on
   each PATH that was committed, allowing the client to bump revision
   numbers. */
typedef svn_error_t *svn_ra_close_commit_func_t (void *close_baton,
                                                 svn_string_t *path,
                                                 svn_revnum_t new_rev);


/* A function type which allows the RA layer to store WC properties
   after a commit.  */
typedef svn_error_t *svn_ra_set_wc_prop_func_t (void *close_baton,
                                                svn_string_t *path,
                                                svn_string_t *name,
                                                svn_string_t *value);


/* A vtable structure which allows a working copy to describe a
   subset (or possibly all) of its working-copy to an RA layer. */
  
typedef struct svn_ra_reporter_t
{
  /* Describe an entire subtree DIR_PATH as being at a particular
     REVISION; this will *override* any previous set_directory() calls
     made on DIR_PATH's parents.  DIR_PATH is relative to the URL
     specified in open(), and are `svn_path_url_style'. */
  svn_error_t *(*set_directory) (void *report_baton,
                                 svn_string_t *dir_path,
                                 svn_revnum_t revision);
  
  /* Describe a file FILE_PATH as being at a particular REVISION; this
     will *override* any previous set_file() calls made on FILE_PATH's
     parents.  FILE_PATH is relative to the URL specified in open(),
     and are `svn_path_url_style'. */
  svn_error_t *(*set_file) (void *report_baton,
                            svn_string_t *file_path,
                            svn_revnum_t revision);
  
  /* WC calls this when the state report is finished; any directories
     or files not explicitly `set' above are assumed to be at the
     baseline revision.  */
  svn_error_t *(*finish_report) (void *report_baton);

} svn_ra_reporter_t;




/* A vtable structure which encapsulates all the functionality of a
   particular repository-access implementation.

   Note: libsvn_client will keep an array of these objects,
   representing all RA libraries that it has simultaneously loaded
   into memory.  Depending on the situation, the client can look
   through this array and find the appropriate implementation it
   needs. */

typedef struct svn_ra_plugin_t
{
  const char *name;         /* The name of the ra library,
                                 e.g. "ra_dav" or "ra_local" */

  const char *description;  /* Short documentation string */

  /* The vtable hooks */
  
  /* Open a "session" with a repository at URL.  *SESSION_BATON is
     returned and then used (opaquely) for all further interactions
     with the repository. */
  svn_error_t *(*open) (void **session_baton,
                        svn_string_t *repository_URL,
                        apr_pool_t *pool);


  /* Close a repository session.  This frees any memory used by the
     session baton.  (To free the session baton itself, simply free
     the pool it was created in.) */
  svn_error_t *(*close) (void *session_baton);

  /* Get the latest revision number from the repository. This is
     usefule for the `svn status' command.  :) */
  svn_error_t *(*get_latest_revnum) (void *session_baton,
                                     svn_revnum_t *latest_revnum);


  /* Begin a commit against `rev:path' using LOG_MSG.  `rev' is the
     argument passed to replace_root(), and `path' is built into the
     SESSION_BATON's URL.
     
     RA returns an *EDITOR and *EDIT_BATON capable of transmitting a
     commit to the repository, which is then driven by the client.

     RA must guarantee:
     
          1. That it will track each item that is committed
          2. That close_edit() will "finish" the commit by calling
             CLOSE_FUNC (with CLOSE_BATON) on each item that was
             committed.  

     Optionally, the RA layer may also call SET_FUNC to store WC
     properties on committed items.  */
  svn_error_t *(*get_commit_editor) (void *session_baton,
                                     const svn_delta_edit_fns_t **editor,
                                     void **edit_baton,
                                     svn_string_t *log_msg,
                                     svn_ra_close_commit_func_t close_func,
                                     svn_ra_set_wc_prop_func_t set_func,
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
     BASE_REVISION is the revision of this working-copy starting place.

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
                             svn_revnum_t base_revision,
                             svn_revnum_t revision_to_update_to,
                             const svn_delta_edit_fns_t *update_editor,
                             void *update_baton);

} svn_ra_plugin_t;


/* svn_ra_init_func_t :
   
   libsvn_client will be reponsible for loading each RA DSO it needs.
   However, all "ra_FOO" implementations *must* export a function named
   `svn_ra_FOO_init()' of type `svn_ra_init_func_t'.

   When called by libsvn_client, this routine simply returns an
   internal, static plugin structure. In addition, it returns the URI
   scheme handled by this RA module. POOL is a pool for allocating
   configuration / one-time data.

   This type is defined to use the "C Calling Conventions" to ensure that
   abi_version is the first parameter. The RA plugin must check that value
   before accessing the other parameters.

   ### need to force this to be __cdecl on Windows... how??
*/
typedef svn_error_t *svn_ra_init_func_t (int abi_version,
                                         apr_pool_t *pool,
                                         const char **url_scheme,
                                         const svn_ra_plugin_t **plugin);

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

svn_ra_init_func_t svn_ra_dav_init;
svn_ra_init_func_t svn_ra_local_init;




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







#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif  /* SVN_RA_H */


/* ----------------------------------------------------------------
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
