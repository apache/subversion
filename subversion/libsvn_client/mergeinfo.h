/*
 * mergeinfo.h : Client library-internal mergeinfo APIs.
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

/* Obtain any mergeinfo for the session-relative path REL_PATH from
   the repository, and set it in *TARGET_MERGEINFO.

   INHERIT indicates whether explicit, explicit or inherited, or only
   inherited mergeinfo for REL_PATH is obtained.

   If there is no mergeinfo available for REL_PATH, set
   *TARGET_MERGEINFO to NULL. */
svn_error_t *
svn_client__get_repos_mergeinfo(svn_ra_session_t *ra_session,
                                apr_hash_t **target_mergeinfo,
                                const char *rel_path,
                                svn_revnum_t rev,
                                svn_mergeinfo_inheritance_t inherit,
                                apr_pool_t *pool);

/* Parse any mergeinfo from the WCPATH's ENTRY and store it in
   MERGEINFO.  If no record of any mergeinfo exists, set MERGEINFO to
   NULL.  Does not acount for inherited mergeinfo. */
svn_error_t *
svn_client__parse_mergeinfo(apr_hash_t **mergeinfo,
                            const svn_wc_entry_t *entry,
                            const char *wcpath,
                            svn_wc_adm_access_t *adm_access,
                            svn_client_ctx_t *ctx,
                            apr_pool_t *pool);

/* Write MERGEINFO into the WC for WCPATH.  If MERGEINFO is NULL,
   remove any SVN_PROP_MERGE_INFO for WCPATH.  If MERGEINFO is empty,
   record an empty property value (e.g. ""). */
svn_error_t *
svn_client__record_wc_mergeinfo(const char *wcpath,
                                apr_hash_t *mergeinfo,
                                svn_wc_adm_access_t *adm_access,
                                apr_pool_t *pool);

/* Elide any svn:mergeinfo set on TARGET_PATH to its nearest working
   copy (or possibly repository) ancestor with equivalent mergeinfo.

   If WC_ELISION_LIMIT_PATH is NULL check up to the root of the working copy
   for an elision destination, if none is found check the repository,
   otherwise check as far as WC_ELISION_LIMIT_PATH within the working copy.
   TARGET_PATH and WC_ELISION_LIMIT_PATH, if it exists, must both be absolute
   or relative to the working directory.

   If TARGET_WCPATH's mergeinfo and its nearest ancestor's mergeinfo
   differ by paths existing only in TARGET_PATH's mergeinfo that map to
   empty revision ranges, then the mergeinfo between the two is considered
   equivalent and elision occurs.  If the mergeinfo between the two still
   differs then partial elision occurs: only the paths mapped to empty
   revision ranges in TARGET_WCPATH's mergeinfo elide.

   If TARGET_WCPATH's mergeinfo and its nearest ancestor's mergeinfo
   differ by paths existing only in the ancestor's mergeinfo that map to
   empty revision ranges, then the mergeinfo between the two is considered
   equivalent and elision occurs.

   If TARGET_WCPATH's mergeinfo consists only of paths mapped to empty
   revision ranges and none of these paths exist in TARGET_WCPATH's nearest
   ancestor, then elision occurs.

   If TARGET_WCPATH's mergeinfo consists only of paths mapped to empty
   revision ranges and TARGET_WCPATH has no working copy or repository
   ancestor with mergeinfo (WC_ELISION_LIMIT_PATH must be NULL to ensure the
   repository is checked), then elision occurs.
 */
svn_error_t *
svn_client__elide_mergeinfo(const char *target_wcpath,
                            const char *wc_elision_limit_path,
                            const svn_wc_entry_t *entry,
                            svn_wc_adm_access_t *adm_access,
                            svn_client_ctx_t *ctx,
                            apr_pool_t *pool);

/* A wrapper which calls svn_client__elide_mergeinfo() on each child
   in CHILDREN_WITH_MERGEINFO_HASH in depth-first. */
svn_error_t *
svn_client__elide_mergeinfo_for_tree(apr_hash_t *children_with_mergeinfo,
                                     svn_wc_adm_access_t *adm_access,
                                     svn_client_ctx_t *ctx,
                                     apr_pool_t *pool);

#endif /* SVN_LIBSVN_CLIENT_MERGEINFO_H */
