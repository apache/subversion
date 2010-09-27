/**
 * @copyright
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
 * @endcopyright
 *
 * @file svn_mergeinfo_private.h
 * @brief Subversion-internal mergeinfo APIs.
 */

#ifndef SVN_MERGEINFO_PRIVATE_H
#define SVN_MERGEINFO_PRIVATE_H

#include <apr_pools.h>

#include "svn_types.h"
#include "svn_error.h"
#include "svn_mergeinfo.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/* Set inheritability of all ranges in RANGELIST to INHERITABLE.
   If RANGELIST is NULL do nothing. */
void
svn_rangelist__set_inheritance(apr_array_header_t *rangelist,
                               svn_boolean_t inheritable);

/* Return whether INFO1 and INFO2 are equal in *IS_EQUAL.

   CONSIDER_INERITANCE determines how the rangelists in the two
   hashes are compared for equality.  If CONSIDER_INERITANCE is FALSE,
   then the start and end revisions of the svn_merge_range_t's being
   compared are the only factors considered when determining equality.

     e.g. '/trunk: 1,3-4*,5' == '/trunk: 1,3-5'

   If CONSIDER_INERITANCE is TRUE, then the inheritability of the
   svn_merge_range_t's is also considered and must be the same for two
   otherwise identical ranges to be judged equal.

     e.g. '/trunk: 1,3-4*,5' != '/trunk: 1,3-5'
          '/trunk: 1,3-4*,5' == '/trunk: 1,3-4*,5'
          '/trunk: 1,3-4,5'  == '/trunk: 1,3-4,5'

   Use POOL for temporary allocations. */
svn_error_t *
svn_mergeinfo__equals(svn_boolean_t *is_equal,
                      svn_mergeinfo_t info1,
                      svn_mergeinfo_t info2,
                      svn_boolean_t consider_inheritance,
                      apr_pool_t *pool);

/* Examine MERGEINFO, removing all paths from the hash which map to
   empty rangelists.  POOL is used only to allocate the apr_hash_index_t
   iterator.  Returns TRUE if any paths were removed and FALSE if none were
   removed or MERGEINFO is NULL. */
svn_boolean_t
svn_mergeinfo__remove_empty_rangelists(svn_mergeinfo_t mergeinfo,
                                       apr_pool_t *pool);

/* Makes a shallow (ie, mergeinfos are not duped, or altered at all;
   keys share storage) copy of IN_CATALOG in *OUT_CATALOG.  PREFIX is
   removed from the beginning of each key in the catalog; it is
   illegal for any key to not start with PREFIX.  The new hash and
   temporary values are allocated in POOL.  (This is useful for making
   the return value from svn_ra_get_mergeinfo relative to the session
   root, say.) */
svn_error_t *
svn_mergeinfo__remove_prefix_from_catalog(svn_mergeinfo_catalog_t *out_catalog,
                                          svn_mergeinfo_catalog_t in_catalog,
                                          const char *prefix,
                                          apr_pool_t *pool);

/* Make a shallow (ie, mergeinfos are not duped, or altered at all;
   though keys are reallocated) copy of IN_CATALOG in *OUT_CATALOG,
   adding PREFIX_PATH to the beginning of each key in the catalog.

   The new hash keys are allocated in RESULT_POOL.  SCRATCH_POOL
   is used for any temporary allocations.*/
svn_error_t *
svn_mergeinfo__add_prefix_to_catalog(svn_mergeinfo_catalog_t *out_catalog,
                                     svn_mergeinfo_catalog_t in_catalog,
                                     const char *prefix_path,
                                     apr_pool_t *result_pool,
                                     apr_pool_t *scratch_pool);

/* Create a string representation of CATALOG in *OUTPUT, allocated in POOL.
   The hash keys of CATALOG and the merge source paths of each key's mergeinfo
   are represented in sorted order as per svn_sort_compare_items_as_paths.
   If CATALOG is empty or NULL then *OUTPUT->DATA is set to "\n".  If SVN_DEBUG
   is true, then a NULL or empty CATALOG causes *OUTPUT to be set to an
   appropriate newline terminated string.  If KEY_PREFIX is not NULL then
   prepend KEY_PREFIX to each key (path) in *OUTPUT.  if VAL_PREFIX is not
   NULL then prepend VAL_PREFIX to each merge source:rangelist line in
   *OUTPUT.

   Any relative merge source paths in the mergeinfo in CATALOG are converted
   to absolute paths in *OUTPUT. */
svn_error_t *
svn_mergeinfo__catalog_to_formatted_string(svn_string_t **output,
                                           svn_mergeinfo_catalog_t catalog,
                                           const char *key_prefix,
                                           const char *val_prefix,
                                           apr_pool_t *pool);

/* Create a string representation of MERGEINFO in *OUTPUT, allocated in POOL.
   Unlike svn_mergeinfo_to_string(), NULL MERGEINFO is tolerated and results
   in *OUTPUT set to "\n".  If SVN_DEBUG is true, then NULL or empty MERGEINFO
   causes *OUTPUT to be set to an appropriate newline terminated string.  If
   PREFIX is not NULL then prepend PREFIX to each line in *OUTPUT.

   Any relative merge source paths in MERGEINFO are converted to absolute
   paths in *OUTPUT.*/
svn_error_t *
svn_mergeinfo__to_formatted_string(svn_string_t **output,
                                   svn_mergeinfo_t mergeinfo,
                                   const char *prefix,
                                   apr_pool_t *pool);

/* Set *YOUNGEST_REV and *OLDEST_REV to the youngest and oldest revisions
   found in the rangelists within MERGEINFO.  If MERGEINFO is NULL or empty
   set *YOUNGEST_REV and *OLDEST_REV to SVN_INVALID_REVNUM. */
svn_error_t *
svn_mergeinfo__get_range_endpoints(svn_revnum_t *youngest_rev,
                                   svn_revnum_t *oldest_rev,
                                   svn_mergeinfo_t mergeinfo,
                                   apr_pool_t *pool);

/* Set *FILTERED_MERGEINFO to a deep copy of MERGEINFO, allocated in POOL, less
   any rangelists that fall outside of the range OLDEST_REV:YOUGEST_REV
   (inclusive).  If all the rangelists mapped to a given path are filtered
   then filter that path as well.  If all paths are filtered or MERGEINFO is
   empty or NULL then *FILTERED_MERGEINFO is set to an empty hash. */
svn_error_t *
svn_mergeinfo__filter_mergeinfo_by_ranges(svn_mergeinfo_t *filtered_mergeinfo,
                                          svn_mergeinfo_t mergeinfo,
                                          svn_revnum_t youngest_rev,
                                          svn_revnum_t oldest_rev,
                                          apr_pool_t *pool);

/* Filter each mergeinfo in CATALOG as per
   svn_mergeinfo__filter_mergefino_by_ranges and put a deep copy of the
   result in *FILTERED_CATALOG.  If any mergeinfo is filtered to an empty
   hash then filter that path/mergeinfo as well.  If all mergeinfo is filtered
   or CATALOG is NULL then set *FILTERED_CATALOG to an empty hash. */
svn_error_t*
svn_mergeinfo__filter_catalog_by_ranges(
  svn_mergeinfo_catalog_t *filtered_catalog,
  svn_mergeinfo_catalog_t catalog,
  svn_revnum_t youngest_rev,
  svn_revnum_t oldest_rev,
  apr_pool_t *pool);

/* Combine one mergeinfo catalog, CHANGES_CATALOG, into another mergeinfo
  catalog MERGEINFO_CATALOG.  If both catalogs have mergeinfo for the same
  key, use svn_mergeinfo_merge() to combine the mergeinfos.
 
  Additions to MERGEINFO_CATALOG are deep copies allocated in
  RESULT_POOL.  Temporary allocations are made in SCRATCH_POOL. */
svn_error_t *
svn_mergeinfo__catalog_merge(svn_mergeinfo_catalog_t mergeinfo_catalog,
                             svn_mergeinfo_catalog_t changes_catalog,
                             apr_pool_t *result_pool,
                             apr_pool_t *scratch_pool);
                            
/* Removes ERASER (the subtrahend) from WHITEBOARD (the
   minuend), and places the resulting difference in *MERGEINFO.
   Allocates *MERGEINFO in RESULT_POOL.  Temporary allocations
   will be performed in SCRATCH_POOL.

   CONSIDER_INHERITANCE determines how to account for the inheritability
   of the two mergeinfo's ranges when calculating the range equivalence,
   as described for svn_mergeinfo_diff().*/
svn_error_t *
svn_mergeinfo__remove2(svn_mergeinfo_t *mergeinfo,
                       svn_mergeinfo_t eraser,
                       svn_mergeinfo_t whiteboard,
                       svn_boolean_t consider_inheritance,
                       apr_pool_t *result_pool,
                       apr_pool_t *scratch_pool);

/* Find the intersection of two mergeinfos, MERGEINFO1 and 
   MERGEINFO2, and place the result in *MERGEINFO, which is (deeply)
   allocated in RESULT_POOL.  Temporary allocations will be performed
   in SCRATCH_POOL.

   CONSIDER_INHERITANCE determines how to account for the inheritability
   of the two mergeinfo's ranges when calculating the range equivalence,
   as described for svn_mergeinfo_diff(). */
svn_error_t *
svn_mergeinfo__intersect2(svn_mergeinfo_t *mergeinfo,
                          svn_mergeinfo_t mergeinfo1,
                          svn_mergeinfo_t mergeinfo2,
                          svn_boolean_t consider_inheritance,
                          apr_pool_t *result_pool,
                          apr_pool_t *scratch_pool);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_MERGEINFO_PRIVATE_H */
