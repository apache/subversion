/*
 * svn_client.h :  public interface for libsvn_client
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




/*** Repository Access (RA) interface for client library ***/

/* Every user of the client library *must* call this routine and hold
   on to the RA_BATON returned.  This baton contains all known methods
   of accessing a repository, and will be required by most
   svn_client_* routines below. */
svn_error_t *
svn_client_init_ra_libs (void **ra_baton,
                         apr_pool_t *pool);


/* Return an ra vtable-LIBRARY (already within RA_BATON) which can
   handle URL.  A number of svn_client_* routines will call this
   internally, but client apps might use it too. */
svn_error_t *
svn_client_get_ra_library (svn_ra_plugin_t **library,
                           void *ra_baton,
                           const char *URL,
                           apr_pool_t *pool);



/*** Milestone 1 Interfaces ***/

/* These interfaces are very basic for milestone 1.  They will
   probably be changed significantly soon. */

/* Perform a checkout, providing pre- and post-checkout hook editors
   and batons (BEFORE_EDITOR, BEFORE_EDIT_BATON / AFTER_EDITOR,
   AFTER_EDIT_BATON), the target PATH that will be the root directory
   of your checked out working copy, an XML_SRC file to use instead of
   an honest-to-goodness fs repository (hey, we're working on it!),
   the ANCESTOR_PATH which describes what is being checked out
   (relative to the repository), and the ANCESTOR_REVISION that you
   would like to check out.  This operation will use the provided
   memory POOL. */
svn_error_t *
svn_client_checkout (const svn_delta_edit_fns_t *before_editor,
                     void *before_edit_baton,
                     const svn_delta_edit_fns_t *after_editor,
                     void *after_edit_baton,
                     svn_string_t *path,
                     svn_string_t *xml_src,
                     svn_string_t *ancestor_path,
                     svn_revnum_t ancestor_revision,
                     apr_pool_t *pool);


/* Perform an update, providing pre- and post-checkout hook editors
   and batons (BEFORE_EDITOR, BEFORE_EDIT_BATON / AFTER_EDITOR,
   AFTER_EDIT_BATON), the target PATH that will be the root directory
   of your updated working copy, an XML_SRC file to use instead of an
   fs repository, and the ANCESTOR_REVISION to which you would like to
   update.  This operation will use the provided memory POOL. */
svn_error_t *
svn_client_update (const svn_delta_edit_fns_t *before_editor,
                   void *before_edit_baton,
                   const svn_delta_edit_fns_t *after_editor,
                   void *after_edit_baton,
                   svn_string_t *path,
                   svn_string_t *xml_src,
                   svn_revnum_t ancestor_revision,
                   apr_pool_t *pool);


svn_error_t *
svn_client_add (svn_string_t *file,
                apr_pool_t *pool);


svn_error_t *
svn_client_delete (svn_string_t *file,
                   svn_boolean_t force,
                   apr_pool_t *pool);


svn_error_t *
svn_client_commit (const svn_delta_edit_fns_t *before_editor,
                   void *before_edit_baton,
                   const svn_delta_edit_fns_t *after_editor,
                   void *after_edit_baton,                   
                   svn_string_t *path,
                   svn_string_t *xml_dst,
                   svn_revnum_t revision,  /* this param is temporary */
                   apr_pool_t *pool);


svn_error_t *
svn_client_status (apr_hash_t **statushash,
                   svn_string_t *path,
                   svn_boolean_t descend,
                   apr_pool_t *pool);


/* Given a PATH to a working copy file, return a path to a temporary
   copy of the PRISTINE version of the file.  The client can then
   compare this to the working copy of the file and execute any kind
   of diff it wishes. 
   
   TODO:  Someday this function will need to return a "cleanup"
   routine to remove the pristine file, in case the pristine file is
   fetched and dumped somewhere by the RA layer. */
svn_error_t *
svn_client_file_diff (svn_string_t *path,
                      svn_string_t **pristine_copy_path,
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













