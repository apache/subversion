/*
 * svn_client.h :  public interface for libsvn_client
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



/*** Includes ***/

/* 
 * Requires:  The working copy library.
 * Provides:  Broad wrappers around working copy library functionality.
 * Used By:   Client programs.
 */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#ifndef SVN_CLIENT_H
#define SVN_CLIENT_H

#include <apr_tables.h>
#include "svn_types.h"
#include "svn_wc.h"
#include "svn_ra.h"
#include "svn_string.h"
#include "svn_error.h"



/* ### TODO:  Multiple Targets

    - Up for debate:  an update on multiple targets is *not* atomic.
    Right now, svn_client_update only takes one path.  What's
    debatable is whether this should ever change.  On the one hand,
    it's kind of losing to have the client application loop over
    targets and call svn_client_update() on each one;  each call to
    update initializes a whole new repository session (network
    overhead, etc.)  On the other hand, it's this is a very simple
    implementation, and allows for the possibility that different
    targets may come from different repositories.  */




/*** Authentication framework -- new in M3 ***/


/*  A callback function type defined by the top-level client
    application (the user of libsvn_client.)

    If libsvn_client is unable to retrieve certain authorization
    information, it can use this callback; the application will then
    directly query the user with PROMPT and return the answer in INFO,
    allocated in POOL.  BATON is provided at the same time as the
    callback, and HIDE indicates that the user's answer should not be
    displayed on the screen. */
typedef svn_error_t *(*svn_client_auth_info_callback_t)
       (char **info,
        const char *prompt,
        svn_boolean_t hide,
        void *baton,
        apr_pool_t *pool);


/* Function type possibly returned along with an RA session baton.  If
   returned, the user should call this routine after closing the RA
   session. */
typedef svn_error_t *(*svn_client_auth_storage_callback_t) (void *baton);



/* A structure for holding authentication information.  This is the
   main structure "processed" by svn_client_authenticate and all of
   its protocol helpers.  */
typedef struct svn_client_auth_t
{
  /* These fields are optionally filled in by the application --
     through command-line arguments, dialog boxes, whatever.  They
     will eventually be used by the libsvn_client "authenticator
     drivers" and presented to the RA layer. */
  const char *username;
  const char *password;  /* ### add more fields here for other protocols. */
 
  /* A callback pointer that libsvn_client can use to make the
     application prompt the user for information (see earlier
     typedef.)   This -must- be provided by the calling application. */
  svn_client_auth_info_callback_t prompt_callback;
  void *prompt_baton;

  /* A callback double-pointer for storing auth info in the working
     copy.  After authentication completes, this -might- be filled
     in by the client-side authentication drivers. */
  svn_client_auth_storage_callback_t storage_callback;
  void *storage_baton;

} svn_client_auth_t;




/* Return an authenticated session to REPOS_URL via RA_LIB.  

   This routine will negotiate with an RA library and authenticate
   the user using the data in AUTH_OBJ:

      - RA_LIB's supported authentication methods will be examined,
        and one method selected.

      - a specific "authenticator" vtable will be fetched from RA_LIB,
        and then paired with an appropriate driver routine that
        understands the protocol.

      - the authenticator-driver will provide all necessary
        information to the authenticator using any combination of
        four methods:

           1.  checking if the information is already present within
               AUTH_OBJ   (e.g. was given as a command-line arg.)
           2.  checking if the information is already stored in the
               working copy at PATH.
           3.  checking if the information can be derived automatically 
               (e.g. calling getuid() )
           4.  forcing the application to prompt the user
               (using AUTH_OBJ->PROMPT_CALLBACK)
 
    Assuming no error occurs, *SESSION_BATON will be set to an object
    that represents an "open" session with the repository.  This
    session_baton is necessary for all further interaction with RA
    layer.
 
    This routine might also return an *STORAGE_CALLBACK/BATON within
    AUTH_OBJ for storing the authentication information in the working
    copy at PATH.  If a non-NULL routine is returned, the caller of
    this function -must- remember to call the storage callback after
    closing the RA session.  If the authentication requires no
    storage, *STORAGE_CALLBACK will be set to NULL. */
svn_error_t *
svn_client_authenticate (void **session_baton,
                         svn_ra_plugin_t *ra_lib,
                         svn_stringbuf_t *repos_URL,
                         svn_stringbuf_t *path,
                         svn_client_auth_t *auth_obj,
                         apr_pool_t *pool);


/* Names of files that contain authorization information.  (Notice
   that these also correspond to the fields of svn_client_auth_t.)
   libsvn_client will create these in the working copy administrative
   area, depending on which authentication methods are being used. */
#define SVN_CLIENT_AUTH_USERNAME             "username"
#define SVN_CLIENT_AUTH_PASSWORD             "password"






/*** Milestone 3 Interfaces ***/


/* Perform a checkout from URL, providing pre- and post-checkout hook
   editors and batons (BEFORE_EDITOR, BEFORE_EDIT_BATON /
   AFTER_EDITOR, AFTER_EDIT_BATON).  These editors are purely optional
   and exist only for extensibility;  pass four NULLs here if you
   don't need them.

   PATH will be the root directory of your checked out working copy.

   If XML_SRC is NULL, then the checkout will come from the repository
   and subdir specified by URL.  An invalid REVISION will cause the
   "latest" tree to be fetched, while a valid REVISION will fetch a
   specific tree.  Alternatively, a time TM can be used to implicitly
   select a revision.  TM cannot be used at the same time as REVISION.

   If XML_SRC is non-NULL, it is an xml file to check out from; in
   this case, the working copy will record the URL as artificial
   ancestry information.  An invalid REVISION implies that the
   revision *must* be present in the <delta-pkg> tag, while a valid
   REVISION will be simply be stored in the wc. (Note:  a <delta-pkg>
   revision will *always* override the one passed in.)

   This operation will use the provided memory POOL. */
svn_error_t *
svn_client_checkout (const svn_delta_edit_fns_t *before_editor,
                     void *before_edit_baton,
                     const svn_delta_edit_fns_t *after_editor,
                     void *after_edit_baton,
                     svn_client_auth_t *auth_obj,
                     svn_stringbuf_t *URL,
                     svn_stringbuf_t *path,
                     svn_revnum_t revision,
                     apr_time_t tm,
                     svn_stringbuf_t *xml_src,
                     apr_pool_t *pool);


/* Perform an update of PATH (part of a working copy), providing pre-
   and post-checkout hook editors and batons (BEFORE_EDITOR,
   BEFORE_EDIT_BATON / AFTER_EDITOR, AFTER_EDIT_BATON).  These editors
   are purely optional and exist only for extensibility; pass four
   NULLs here if you don't need them.

   If XML_SRC is NULL, then the update will come from the repository
   that PATH was originally checked-out from.  An invalid REVISION
   will cause the PATH to be updated to the "latest" revision, while a
   valid REVISION will update to a specific tree.  Alternatively, a
   time TM can be used to implicitly select a revision.  TM cannot be
   used at the same time as REVISION.

   If XML_SRC is non-NULL, it is an xml file to update from.  An
   invalid REVISION implies that the revision *must* be present in the
   <delta-pkg> tag, while a valid REVISION will be simply be stored in
   the wc. (Note: a <delta-pkg> revision will *always* override the
   one passed in.)

   This operation will use the provided memory POOL. */
svn_error_t *
svn_client_update (const svn_delta_edit_fns_t *before_editor,
                   void *before_edit_baton,
                   const svn_delta_edit_fns_t *after_editor,
                   void *after_edit_baton,
                   svn_client_auth_t *auth_obj,
                   svn_stringbuf_t *path,
                   svn_stringbuf_t *xml_src,
                   svn_revnum_t revision,
                   apr_time_t tm,
                   apr_pool_t *pool);


svn_error_t *
svn_client_add (svn_stringbuf_t *path,
                svn_boolean_t recursive,
                apr_pool_t *pool);


svn_error_t *
svn_client_unadd (svn_stringbuf_t *path,
                  apr_pool_t *pool);


svn_error_t *
svn_client_delete (svn_stringbuf_t *path,
                   svn_boolean_t force,
                   apr_pool_t *pool);


svn_error_t *
svn_client_undelete (svn_stringbuf_t *path,
                     svn_boolean_t recursive,
                     apr_pool_t *pool);

/* Import a tree, using optional pre- and post-commit hook editors
 * (BEFORE_EDITOR, BEFORE_EDIT_BATON / AFTER_EDITOR,
 * AFTER_EDIT_BATON).  These editors are purely optional and exist
 * only for extensibility; pass four NULLs here if you don't need
 * them.
 *
 * Store USER as the author of the commit, LOG_MSG as its log.
 * 
 * PATH is the path to local tree being imported.  PATH can be a file
 * or directory.
 *
 * URL is the repository directory where the imported data is placed.
 *
 * NEW_ENTRY is the new entry created in the repository directory
 * identified by URL.
 *
 * If PATH is a file, that file is imported as NEW_ENTRY.  If PATH is
 * a directory, the contents of that directory are imported, under a
 * new directory the NEW_ENTRY in the repository.  Note and the
 * directory itself is not imported; that is, the basename of PATH is
 * not part of the import.
 *
 * If PATH is a directory and NEW_ENTRY is null, then the contents of
 * PATH are imported directly into the repository directory identified
 * by URL.  NEW_ENTRY may not be the empty string.
 *
 * If NEW_ENTRY already exists in the youngest revision, return error.
 * 
 * If XML_DST is non-NULL, it is a file in which to store the xml
 * result of the commit, and REVISION is used as the revision.
 * 
 * Use POOL for all allocation.
 * 
 * ### kff todo: This import is similar to cvs import, in that it does
 * not change the source tree into a working copy.  However, this
 * behavior confuses most people, and I think eventually svn _should_
 * turn the tree into a working copy, or at least should offer the
 * option. However, doing so is a bit involved, and we don't need it
 * right now.  */
svn_error_t *svn_client_import (const svn_delta_edit_fns_t *before_editor,
                                void *before_edit_baton,
                                const svn_delta_edit_fns_t *after_editor,
                                void *after_edit_baton, 
                                svn_client_auth_t *auth_obj,   
                                svn_stringbuf_t *path,
                                svn_stringbuf_t *url,
                                svn_stringbuf_t *new_entry,
                                const char *user,
                                svn_stringbuf_t *log_msg,
                                svn_stringbuf_t *xml_dst,
                                svn_revnum_t revision,
                                apr_pool_t *pool);


/* Perform an commit, providing pre- and post-commit hook editors and
   batons (BEFORE_EDITOR, BEFORE_EDIT_BATON / AFTER_EDITOR,
   AFTER_EDIT_BATON).  These editors are purely optional and exist
   only for extensibility; pass four NULLs here if you don't need
   them.

   Store USER as the author of the commit, LOG_MSG as its log.

   TARGETS is an array of svn_stringbuf_t * paths to commit.  They need
   not be canonicalized nor condensed; this function will take care of
   that.

   If XML_DST is NULL, then the commit will write to a repository, and
   the REVISION argument is ignored.

   If XML_DST is non-NULL, it is a file path to commit to.  In this
   case, if REVISION is valid, the working copy's revision numbers
   will be updated appropriately.  If REVISION is invalid, the working
   copy remains unchanged.

   This operation will use the provided memory POOL. */
svn_error_t *
svn_client_commit (const svn_delta_edit_fns_t *before_editor,
                   void *before_edit_baton,
                   const svn_delta_edit_fns_t *after_editor,
                   void *after_edit_baton,
                   svn_client_auth_t *auth_obj,
                   const apr_array_header_t *targets,
                   svn_stringbuf_t *log_msg,
                   svn_stringbuf_t *xml_dst,
                   svn_revnum_t revision,  /* this param is temporary */
                   apr_pool_t *pool);


/* Given PATH to a working copy directory or file, allocate and return
   a STATUSHASH structure containing the stati of all entries.  If
   DESCEND is non-zero, recurse fully, else do only immediate
   children.  (See svn_wc.h:svn_wc_statuses() for more verbiage on
   this). */
svn_error_t *
svn_client_status (apr_hash_t **statushash,
                   svn_stringbuf_t *path,
                   svn_boolean_t descend,
                   svn_client_auth_t *auth_obj,
                   apr_pool_t *pool);


/* Given a PATH to a working copy file, return a path to a temporary
   copy of the PRISTINE version of the file.  The client can then
   compare this to the working copy of the file and execute any kind
   of diff it wishes. 
   
   TODO:  Someday this function will need to return a "cleanup"
   routine to remove the pristine file, in case the pristine file is
   fetched and dumped somewhere by the RA layer. */
svn_error_t *
svn_client_file_diff (svn_stringbuf_t *path,
                      svn_stringbuf_t **pristine_copy_path,
                      apr_pool_t *pool);


/* Recursively cleanup a working copy directory DIR, finishing any
   incomplete operations, removing lockfiles, etc. */
svn_error_t *
svn_client_cleanup (svn_stringbuf_t *dir,
                    apr_pool_t *pool);


/* Restore the pristine version of a working copy PATH, effectively
   undoing any local mods.  ### cmpilato todo: What about directories?
   What about properties?  Eh?  */
svn_error_t *
svn_client_revert (svn_stringbuf_t *path,
                   apr_pool_t *pool);


#endif  /* SVN_CLIENT_H */

#ifdef __cplusplus
}
#endif /* __cplusplus */


/* --------------------------------------------------------------
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end: 
 */













