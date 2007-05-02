/*
 * mergeinfo.h : Client library-internal merge info APIs.
 *
 * ====================================================================
 * Copyright (c) 2007 CollabNet.  All rights reserved.
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

#ifndef SVN_LIBSVN_CLIENT_MERGEINFO_H
#define SVN_LIBSVN_CLIENT_MERGEINFO_H

/* Obtain any inherited/direct merge info for the session-relative
   path REL_PATH from the repository, and set it in *TARGET_MERGEINFO.
   If there is no merge info available for REL_PATH, set
   *TARGET_MERGEINFO to NULL. */
svn_error_t *
svn_client__get_repos_merge_info(svn_ra_session_t *ra_session,
                                 apr_hash_t **target_mergeinfo,
                                 const char *rel_path,
                                 svn_revnum_t rev,
                                 apr_pool_t *pool);

/* Parse any merge info from WCPATH's ENTRY and store it in MERGEINFO.
   If no merge info is available, set MERGEINFO to an empty hash. */
svn_error_t *
svn_client__parse_merge_info(apr_hash_t **mergeinfo,
                             const svn_wc_entry_t *entry,
                             const char *wcpath,
                             svn_wc_adm_access_t *adm_access,
                             svn_client_ctx_t *ctx,
                             apr_pool_t *pool);

/* Write MERGEINFO into the WC for WCPATH.  If MERGEINFO is NULL,
   remove any SVN_PROP_MERGE_INFO for WCPATH.  If MERGEINFO is empty,
   record an empty property value (e.g. ""). */
svn_error_t *
svn_client__record_wc_merge_info(const char *wcpath,
                                 apr_hash_t *mergeinfo,
                                 svn_wc_adm_access_t *adm_access,
                                 apr_pool_t *pool);

/* Elide any svn:mergeinfo set on TARGET_PATH to its nearest working
   copy ancestor with equivalent mergeinfo.  If ELISION_LIMIT_PATH is NULL
   check up to the root of the working copy for elidable mergeinfo,
   otherwise check as far as ELISION_LIMIT_PATH.  TARGET_PATH and
   ELISION_LIMIT_PATH, if it exists, must both be absolute or relative to
   the working directory. */
svn_error_t *
svn_client__elide_mergeinfo(const char *target_wcpath,
                            const char *elision_limit_path,
                            const svn_wc_entry_t *entry,
                            svn_wc_adm_access_t *adm_access,
                            svn_client_ctx_t *ctx,
                            apr_pool_t *pool);

#endif /* SVN_LIBSVN_CLIENT_MERGEINFO_H */
