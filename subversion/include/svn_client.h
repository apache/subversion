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






/*** Milestone 2 Interfaces ***/


/* Perform a checkout from URL, providing pre- and post-checkout hook
   editors and batons (BEFORE_EDITOR, BEFORE_EDIT_BATON /
   AFTER_EDITOR, AFTER_EDIT_BATON).

   PATH will be the root directory of your checked out working copy.

   If XML_SRC is NULL, then the checkout will come from the repository
   and subdir specified by URL.  An invalid REVISION will cause the
   "latest" tree to be fetched, while a valid REVISION will fetch a
   specific tree.

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
                     svn_string_t *URL,
                     svn_string_t *path,
                     svn_revnum_t revision,
                     svn_string_t *xml_src,
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


/* Perform an commit, providing pre- and post-commit hook editors and
   batons (BEFORE_EDITOR, BEFORE_EDIT_BATON / AFTER_EDITOR,
   AFTER_EDIT_BATON).  Store LOG_MSG as the log for the commit.

   PATH is where the commit will begin in the working copy.

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
                   svn_string_t *path,
                   svn_string_t *log_msg,
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













