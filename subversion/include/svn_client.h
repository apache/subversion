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
#include "svn_string.h"
#include "svn_error.h"





/*** Milestone 1 Interfaces ***/

/* These interfaces are very basic for milestone 1.  They will
   probably be changed significantly soon. */

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
   of diff it wishes. */
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

