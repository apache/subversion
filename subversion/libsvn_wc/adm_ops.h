/*
 * adm_ops.h :  side-effecting wc adm information
 *              (This code doesn't know where any adm information is
 *              located.  The caller always passes in a path obtained
 *              by using the adm_files.h API.)
 *
 *
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
 */


#ifndef SVN_LIBSVN_WC_ADM_OPS_H
#define SVN_LIBSVN_WC_ADM_OPS_H

#include <apr_pools.h>
#include "svn_types.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/* Modify the entry of working copy PATH, presumably after an update
   completes.   If PATH doesn't exist, this routine does nothing.
   ADM_ACCESS must be an access baton for PATH (assuming it existed).

   Set the entry's 'url' and 'working revision' fields to BASE_URL and
   NEW_REVISION.  If BASE_URL is null, the url field is untouched; if
   NEW_REVISION in invalid, the working revision field is untouched.
   The modifications are mutually exclusive.

   If REPOS is non-NULL, set the repository root of the entry to REPOS, but
   only if REPOS is an ancestor of the entries URL (after possibly modifying
   it).  IN addition to that requirement, if the PATH refers to a directory,
   the repository root is only set if REPOS is an ancestor of the URLs all
   file entries which don't already have a repository root set.  This prevents
   the entries file from being corrupted by this operation.

   If PATH is a directory, then, walk entries below PATH according to
   DEPTH thusly:

   If DEPTH is svn_depth_infinity, perform the following actions on
   every entry below PATH; if svn_depth_immediates, svn_depth_files,
   or svn_depth_empty, perform them only on PATH.

   If NEW_REVISION is valid, then tweak every entry to have this new
   working revision (excluding files that are scheduled for addition
   or replacement.)  Likewise, if BASE_URL is non-null, then rewrite
   all urls to be "telescoping" children of the base_url.

   If REMOVE_MISSING_DIRS is TRUE, then delete the entries for any
   missing directories.  If NOTIFY_FUNC is non-null, invoke it with
   NOTIFY_BATON for each missing entry deleted.

   EXCLUDE_PATHS is a hash containing const char * pathnames.  Entries
   for pathnames contained in EXCLUDE_PATHS are not touched by this
   function.
*/
svn_error_t *svn_wc__do_update_cleanup(const char *path,
                                       svn_wc_adm_access_t *adm_access,
                                       svn_depth_t depth,
                                       const char *base_url,
                                       const char *repos,
                                       svn_revnum_t new_revision,
                                       svn_wc_notify_func2_t notify_func,
                                       void *notify_baton,
                                       svn_boolean_t remove_missing_dirs,
                                       apr_hash_t *exclude_paths,
                                       apr_pool_t *pool);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_LIBSVN_WC_ADM_OPS_H */
