/**
 * @copyright
 * ====================================================================
 * Copyright (c) 2006 CollabNet.  All rights reserved.
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
 * @file svn_mergeinfo.h
 * @brief mergeinfo handling and processing
 */


#ifndef SVN_MERGEINFO_H
#define SVN_MERGEINFO_H

#include <apr_pools.h>
#include <apr_tables.h>         /* for apr_array_header_t */
#include <apr_hash.h>

#include "svn_error.h"


#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/** Parse the mergeinfo from @a input into @a hash, mapping from paths
 * to arrays of @c svn_merge_range_t.  Perform temporary allocations
 * in @a pool.
 *
 * @since New in 1.5.
 */
svn_error_t *
svn_mergeinfo_parse(const char *input, apr_hash_t **hash, apr_pool_t *pool);

/** Calculate the delta between two hashes of merge info, @a from and
 * @a to, and place the result in @a deleted and @a added (neither
 * output argument will ever be @c NULL), stored as the usual mapping
 * of paths to arrays of @c svn_merge_range_t.
 *
 * @since New in 1.5.
 */
svn_error_t *
svn_mergeinfo_diff(apr_hash_t **deleted, apr_hash_t **added,
                   apr_hash_t *from, apr_hash_t *to, apr_pool_t *pool);

/** Merge two hashes of merge info, @a in1 and @a in2,
 * and place the result in @a output.  
 *
 * @since New in 1.5.
 */
svn_error_t *
svn_mergeinfo_merge(apr_hash_t **output, apr_hash_t *in1, apr_hash_t *in2,
                    apr_pool_t *pool);

/** Removes @a eraser (the subtrahend) from @a whiteboard (the
 * minuend), and places the resulting difference in @a output.
 *
 * @since New in 1.5.
 */
svn_error_t *
svn_mergeinfo_remove(apr_hash_t **output, apr_hash_t *eraser,
                     apr_hash_t *whiteboard, apr_pool_t *pool);

/** Calculate the delta between two rangelists consisting of @c
 * svn_merge_range_t * elements, @a from and @a to, and place the
 * result in @a deleted and @a added (neither output argument will
 * ever be @c NULL).
 *
 * @since New in 1.5.
 */
svn_error_t *
svn_rangelist_diff(apr_array_header_t **deleted, apr_array_header_t **added,
                   apr_array_header_t *from, apr_array_header_t *to,
                   apr_pool_t *pool);

/** Merge two rangelists consisting of @c svn_merge_range_t *
 * elements, @a in1 and @a in2, and place the result in @a output.
 *
 * Note: @a in1 and @a in2 must be sorted as said by
 * svn_sort_compare_ranges. @a output is guaranteed to be in sorted
 * order.
 * @since New in 1.5.
 */
svn_error_t *
svn_rangelist_merge(apr_array_header_t **output, apr_array_header_t *in1,
                     apr_array_header_t *in2, apr_pool_t *pool);

/** Removes @a eraser (the subtrahend) from @a whiteboard (the
 * minuend), and places the resulting difference in @a output.
 *
 * Note: @a in1 and @a in2 must be sorted as said by
 * svn_sort_compare_ranges.  @a output is guaranteed to be in sorted
 * order.
 * @since New in 1.5.
 */
svn_error_t *
svn_rangelist_remove(apr_array_header_t **output, apr_array_header_t *eraser,
                     apr_array_header_t *whiteboard, apr_pool_t *pool);

/** Take an array of svn_merge_range_t *'s in @a input, and convert it
 * back to a text format rangelist in @a output.
 *
 * @since New in 1.5.
 */
svn_error_t *
svn_rangelist_to_string(svn_stringbuf_t **output, apr_array_header_t *input,
                         apr_pool_t *pool);

/** Take a hash of mergeinfo in @a input, and convert it
 * back to a text format mergeinfo in @a output.
 *
 * @since New in 1.5.
 */
svn_error_t *
svn_mergeinfo_to_string(svn_stringbuf_t **output, apr_hash_t *input,
                        apr_pool_t *pool);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_MERGEINFO_H */
