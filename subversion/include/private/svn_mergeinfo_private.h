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

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* Take a hash of mergeinfo in MERGEINPUT, and convert it back to
   a text format mergeinfo in OUTPUT.  If INPUT contains no elements,
   return the empty string. */
svn_error_t *
svn_mergeinfo__to_string(svn_string_t **output, apr_hash_t *mergeinput,
                         apr_pool_t *pool);

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
                      apr_hash_t *info1,
                      apr_hash_t *info2,
                      svn_boolean_t consider_inheritance,
                      apr_pool_t *pool);

/* Examine MERGEINFO, a mapping from paths to apr_array_header_t *'s
   of svn_merge_range_t *, removing all paths from the hash which map to
   empty rangelists.  POOL is used only to allocate the apr_hash_index_t
   iterator.  Returns TRUE if any paths were removed and FALSE if none were
   removed or MERGEINFO is NULL. */
svn_boolean_t
svn_mergeinfo__remove_empty_rangelists(apr_hash_t *mergeinfo,
                                       apr_pool_t *pool);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_MERGEINFO_PRIVATE_H */
