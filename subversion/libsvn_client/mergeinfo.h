/*
 * mergeinfo.h : Client library-internal mergeinfo APIs.
 *
 * ====================================================================
 *    Licensed to the Subversion Corporation (SVN Corp.) under one
 *    or more contributor license agreements.  See the NOTICE file
 *    distributed with this work for additional information
 *    regarding copyright ownership.  The SVN Corp. licenses this file
 *    to you under the Apache License, Version 2.0 (the
 *    "License"); you may not use this file except in compliance
 *    with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing,
 *    software distributed under the License is distributed on an
 *    "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 *    KIND, either express or implied.  See the License for the
 *    specific language governing permissions and limitations
 *    under the License.
 * ====================================================================
 */

#ifndef SVN_LIBSVN_CLIENT_MERGEINFO_H
#define SVN_LIBSVN_CLIENT_MERGEINFO_H

#include "svn_wc.h"
#include "svn_client.h"


/*** Data Structures ***/


/* Structure to store information about working copy paths that need special
   consideration during a mergeinfo aware merge -- See the
   'THE CHILDREN_WITH_MERGEINFO ARRAY' meta comment and the doc string for the
   function get_mergeinfo_paths() in libsvn_client/merge.c.
*/
typedef struct svn_client__merge_path_t
{
  const char *path;                  /* Working copy path, either absolute or
                                        relative to the current working
                                        directory. */
  svn_boolean_t missing_child;       /* PATH has an immediate child which is
                                        missing. */
  svn_boolean_t switched;            /* PATH is switched. */
  svn_boolean_t has_noninheritable;  /* PATH has svn:mergeinfo set on it which
                                        includes non-inheritable revision
                                        ranges. */
  svn_boolean_t absent;              /* PATH is absent from the WC, probably
                                        due to authz restrictions. */

  svn_boolean_t child_of_noninheritable; /* PATH has no explict mergeinfo
                                            itself but is the child of a
                                            path with noniheritable
                                            mergeinfo. */

  /* The remaining ranges to be merged to PATH.  When describing a forward
     merge this rangelist adheres to the rules for rangelists described in
     svn_mergeinfo.h.  However, when describing reverse merges this
     rangelist can contain reverse merge ranges that are not sorted per
     svn_sort_compare_ranges(), but rather are sorted such that the ranges
     with the youngest start revisions come first.  In both the forward and
     reverse merge cases the ranges should never overlap.  This rangelist
     may be empty but should never be NULL unless ABSENT is true. */
  apr_array_header_t *remaining_ranges;

  svn_mergeinfo_t pre_merge_mergeinfo;  /* Explicit or inherited mergeinfo
                                           on PATH prior to a merge.
                                           May be NULL. */
  svn_mergeinfo_t implicit_mergeinfo;   /* Implicit mergeinfo on PATH prior
                                           to a merge.  May be NULL. */
  svn_boolean_t indirect_mergeinfo;     /* Whether PRE_MERGE_MERGEINFO was
                                           explicit or inherited. */
  svn_boolean_t scheduled_for_deletion; /* PATH is scheduled for deletion. */
} svn_client__merge_path_t;

/* Return a deep copy of the merge-path structure OLD, allocated in POOL. */
svn_client__merge_path_t *
svn_client__merge_path_dup(const svn_client__merge_path_t *old,
                           apr_pool_t *pool);



/*** Functions ***/

/* Find explicit or inherited WC mergeinfo for WCPATH, and return it
   in *MERGEINFO (NULL if no mergeinfo is set).  Set *INHERITED to
   whether the mergeinfo was inherited (TRUE or FALSE).

   This function will search for inherited mergeinfo in the parents of
   WCPATH only if the working revision of WCPATH falls within the range
   of the parent's last committed revision to the parent's working
   revision (inclusive).

   INHERIT indicates whether explicit, explicit or inherited, or only
   inherited mergeinfo for WCPATH is retrieved.

   Don't look for inherited mergeinfo any higher than LIMIT_PATH
   (ignored if NULL) or beyond any switched path.

   Set *WALKED_PATH to the path climbed from WCPATH to find inherited
   mergeinfo, or "" if none was found. (ignored if NULL). */
svn_error_t *
svn_client__get_wc_mergeinfo(svn_mergeinfo_t *mergeinfo,
                             svn_boolean_t *inherited,
                             svn_mergeinfo_inheritance_t inherit,
                             const svn_wc_entry_t *entry,
                             const char *wcpath,
                             const char *limit_path,
                             const char **walked_path,
                             svn_wc_adm_access_t *adm_access,
                             svn_client_ctx_t *ctx,
                             apr_pool_t *pool);

/* Obtain any mergeinfo for the root-relative repository filesystem path
   REL_PATH from the repository, and set it in *TARGET_MERGEINFO.
   RA_SESSION should be an open RA session pointing at the URL that REL_PATH
   is relative to, or NULL, in which case this function will open its own
   temporary session.

   INHERIT indicates whether explicit, explicit or inherited, or only
   inherited mergeinfo for REL_PATH is obtained.

   If REL_PATH does not exist at REV, SVN_ERR_FS_NOT_FOUND or
   SVN_ERR_RA_DAV_REQUEST_FAILED is returned and *TARGET_MERGEINFO
   is untouched.

   If there is no mergeinfo available for REL_PATH, or if the server
   doesn't support a mergeinfo capability and SQUELCH_INCAPABLE is
   TRUE, set *TARGET_MERGEINFO to NULL. */
svn_error_t *
svn_client__get_repos_mergeinfo(svn_ra_session_t *ra_session,
                                svn_mergeinfo_t *target_mergeinfo,
                                const char *rel_path,
                                svn_revnum_t rev,
                                svn_mergeinfo_inheritance_t inherit,
                                svn_boolean_t squelch_incapable,
                                apr_pool_t *pool);

/* Retrieve the direct mergeinfo for the TARGET_WCPATH from the WC's
   mergeinfo prop, or that inherited from its nearest ancestor if the
   target has no info of its own.

   If no mergeinfo can be obtained from the WC or REPOS_ONLY is TRUE,
   get it from the repository.  RA_SESSION should be an open RA session
   pointing at ENTRY->URL, or NULL, in which case this function will open
   its own temporary session.

   (opening a new RA session if RA_SESSION
   is NULL).  Store any mergeinfo obtained for TARGET_WCPATH -- which
   is reflected by ENTRY -- in *TARGET_MERGEINFO, if no mergeinfo is
   found *TARGET_MERGEINFO is NULL.

   Like svn_client__get_wc_mergeinfo, this function considers no inherited
   mergeinfo to be found in the WC when trying to crawl into a parent path
   with a different working revision.

   INHERIT indicates whether explicit, explicit or inherited, or only
   inherited mergeinfo for TARGET_WCPATH is retrieved.

   If TARGET_WCPATH inherited its mergeinfo from a working copy ancestor
   or if it was obtained from the repository, set *INDIRECT to TRUE, set it
   to FALSE *otherwise. */
svn_error_t *
svn_client__get_wc_or_repos_mergeinfo(svn_mergeinfo_t *target_mergeinfo,
                                      const svn_wc_entry_t *entry,
                                      svn_boolean_t *indirect,
                                      svn_boolean_t repos_only,
                                      svn_mergeinfo_inheritance_t inherit,
                                      svn_ra_session_t *ra_session,
                                      const char *target_wcpath,
                                      svn_wc_adm_access_t *adm_access,
                                      svn_client_ctx_t *ctx,
                                      apr_pool_t *pool);

/* Set *MERGEINFO_P to a mergeinfo constructed solely from the
   natural history of PATH_OR_URL@PEG_REVISION.  RA_SESSION is an RA
   session whose session URL maps to PATH_OR_URL's URL, or NULL.
   If RANGE_YOUNGEST and RANGE_OLDEST are valid, use them to bound the
   revision ranges of returned mergeinfo.  See svn_ra_get_location_segments()
   for the rules governing PEG_REVISION, START_REVISION, and END_REVISION.*/
svn_error_t *
svn_client__get_history_as_mergeinfo(svn_mergeinfo_t *mergeinfo_p,
                                     const char *path_or_url,
                                     const svn_opt_revision_t *peg_revision,
                                     svn_revnum_t range_youngest,
                                     svn_revnum_t range_oldest,
                                     svn_ra_session_t *ra_session,
                                     svn_client_ctx_t *ctx,
                                     apr_pool_t *pool);

/* Translates an array SEGMENTS (of svn_location_t *), like the one
   returned from svn_client__repos_location_segments, into a mergeinfo
   *MERGEINFO_P, allocated in POOL. */
svn_error_t *
svn_client__mergeinfo_from_segments(svn_mergeinfo_t *mergeinfo_p,
                                    apr_array_header_t *segments,
                                    apr_pool_t *pool);

/* Parse any mergeinfo from the LOCAL_ABSPATH's ENTRY and store it in
   MERGEINFO.  If no record of any mergeinfo exists, set MERGEINFO to NULL.
   Does not acount for inherited mergeinfo. */
svn_error_t *
svn_client__parse_mergeinfo(svn_mergeinfo_t *mergeinfo,
                            svn_wc_context_t *wc_ctx,
                            const char *local_abspath,
                            apr_pool_t *result_pool,
                            apr_pool_t *scratch_pool);

/* Write MERGEINFO into the WC for LOCAL_ABSPATH.  If MERGEINFO is NULL,
   remove any SVN_PROP_MERGEINFO for LOCAL_ABSPATH.  If MERGEINFO is empty,
   record an empty property value (e.g. "").  If CTX->NOTIFY_FUNC2 is
   not null call it with notification type svn_wc_notify_merge_record_info.
   
   Use WC_CTX to access the working copy, and SCRATCH_POOL for any temporary
   allocations. */
svn_error_t *
svn_client__record_wc_mergeinfo(const char *local_abspath,
                                svn_mergeinfo_t mergeinfo,
                                svn_client_ctx_t *ctx,
                                apr_pool_t *scratch_pool);

/* Elide any svn:mergeinfo set on TARGET_PATH to its nearest working
   copy (or possibly repository) ancestor with equivalent mergeinfo.

   If WC_ELISION_LIMIT_PATH is NULL check up to the root of the working copy
   or the nearest switched parent for an elision destination, if none is found
   check the repository, otherwise check as far as WC_ELISION_LIMIT_PATH
   within the working copy.  TARGET_PATH and WC_ELISION_LIMIT_PATH, if it
   exists, must both be absolute or relative to the working directory.

   Elision occurs if:

     A) WCPATH has empty mergeinfo and no parent path with explicit mergeinfo
        can be found in either the WC or the repository (WC_ELISION_LIMIT_PATH
        must be NULL for this to occur).

     B) WCPATH has empty mergeinfo and its nearest parent also has empty
        mergeinfo.

     C) WCPATH has the same mergeinfo as its nearest parent when that parent's
        mergeinfo is adjusted for the path difference between the two, e.g.:

                                WCPATH's                          Parent's
                    WCPATH's    Nearest    Parent's   Path        Adjusted
        WCPATH      mergeinfo   parent     Mergeinfo  Difference  Mergeinfo
        -------     ---------   ---------  ---------  ----------  ---------
        A_COPY/D/H  '/A/D/H:3'  A_COPY     '/A:3'     'D/H'       '/A/D/H:3'

   If Elision occurs remove the svn:mergeinfo property from TARGET_WCPATH. */
svn_error_t *
svn_client__elide_mergeinfo(const char *target_wcpath,
                            const char *wc_elision_limit_path,
                            const svn_wc_entry_t *entry,
                            svn_wc_adm_access_t *adm_access,
                            svn_client_ctx_t *ctx,
                            apr_pool_t *pool);

/* A wrapper which calls svn_client__elide_mergeinfo() on each child
   in CHILDREN_WITH_MERGEINFO in depth-first. */
svn_error_t *
svn_client__elide_mergeinfo_for_tree(apr_hash_t *children_with_mergeinfo,
                                     svn_wc_adm_access_t *adm_access,
                                     svn_client_ctx_t *ctx,
                                     apr_pool_t *pool);

/* TODO(reint): Document. */
svn_error_t *
svn_client__elide_mergeinfo_catalog(svn_mergeinfo_t mergeinfo_catalog,
                                    apr_pool_t *pool);

/* For each source path : rangelist pair in MERGEINFO, append REL_PATH to
   the source path and add the new source path : rangelist pair to
   ADJUSTED_MERGEINFO.  The new source path and rangelist are both deep
   copies allocated in POOL.  Neither ADJUSTED_MERGEINFO
   nor MERGEINFO should be NULL. */
svn_error_t *
svn_client__adjust_mergeinfo_source_paths(svn_mergeinfo_t adjusted_mergeinfo,
                                          const char *rel_path,
                                          svn_mergeinfo_t mergeinfo,
                                          apr_pool_t *pool);

#endif /* SVN_LIBSVN_CLIENT_MERGEINFO_H */
