/**
 * @copyright
 * ====================================================================
 * Copyright (c) 2006-2007 CollabNet.  All rights reserved.
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

/* Suffix for SVN_PROP_MERGE_INFO revision ranges indicating a given
   range is non-inheritable. */
#define SVN_MERGEINFO_NONINHERITABLE_STR "*"

/** Parse the mergeinfo from @a input into @a *mergeinfo, mapping from
 * paths to @c apr_array_header_t *'s of @c svn_merge_range_t *
 * elements.  If no mergeinfo is available, return an empty hash
 * (never @c NULL).  Perform temporary allocations in @a pool.
 *
 * Note: @a *mergeinfo will contain rangelists that are guaranteed to
 * be sorted (ordered by smallest revision ranges to largest).
 * @since New in 1.5.
 */
svn_error_t *
svn_mergeinfo_parse(apr_hash_t **mergeinfo, const char *input,
                    apr_pool_t *pool);

/** Calculate the delta between two hashes of mergeinfo (with
 * rangelists sorted in ascending order), @a mergefrom and @a mergeto
 * (which may be @c NULL), and place the result in @a deleted and @a
 * added (neither output argument will ever be @c NULL), stored as the
 * usual mapping of paths to lists of @c svn_merge_range_t *'s.
 *
 * @a consider_inheritance determines how to account for the inheritability
 * of the rangelists in @a mergefrom and @a mergeto when calculating the
 * diff.
 *
 * @since New in 1.5.
 */
svn_error_t *
svn_mergeinfo_diff(apr_hash_t **deleted, apr_hash_t **added,
                   apr_hash_t *mergefrom, apr_hash_t *mergeto,
                   svn_merge_range_inheritance_t consider_inheritance,
                   apr_pool_t *pool);

/** Merge hash of mergeinfo, @a changes, into existing hash @a
 * *mergeinfo.  @a consider_inheritance determines how to account for
 * the inheritability of the rangelists in @a changes and @a *mergeinfo
 * when merging.
 *
 * Note: @a *mergeinfo and @a changes must have rangelists that are
 * sorted as said by @c svn_sort_compare_ranges().  After the merge @a
 * *mergeinfo will have rangelists that are guaranteed to be in sorted
 * order.
 *
 * @since New in 1.5.
 */
svn_error_t *
svn_mergeinfo_merge(apr_hash_t **mergeinfo, apr_hash_t *changes,
                    svn_merge_range_inheritance_t consider_inheritance,
                    apr_pool_t *pool);

/** Removes @a eraser (the subtrahend) from @a whiteboard (the
 * minuend), and places the resulting difference in @a *mergeinfo.
 *
 * @since New in 1.5.
 */
svn_error_t *
svn_mergeinfo_remove(apr_hash_t **mergeinfo, apr_hash_t *eraser,
                     apr_hash_t *whiteboard, apr_pool_t *pool);

/** Calculate the delta between two rangelists consisting of @c
 * svn_merge_range_t * elements (sorted in ascending order), @a from
 * and @a to, and place the result in @a deleted and @a added (neither
 * output argument will ever be @c NULL).
 *
 * @a consider_inheritance determines how to account for the inheritability
 * of @a to and @a from when calculating the diff.
 *
 * @since New in 1.5.
 */
svn_error_t *
svn_rangelist_diff(apr_array_header_t **deleted, apr_array_header_t **added,
                   apr_array_header_t *from, apr_array_header_t *to,
                   svn_merge_range_inheritance_t consider_inheritance,
                   apr_pool_t *pool);

/** Merge two rangelists consisting of @c svn_merge_range_t *
 * elements, @a *rangelist and @a changes, placing the results in
 * @a *rangelist.
 *
 * @a consider_inheritance determines how to account for the inheritability
 * of @a changes and @a *rangelist when merging.
 *
 * Note: @a *rangelist and @a changes must be sorted as said by @c
 * svn_sort_compare_ranges().  @a *rangelist is guaranteed to remain
 * in sorted order.
 * 
 * @since New in 1.5.
 */
svn_error_t *
svn_rangelist_merge(apr_array_header_t **rangelist,
                    apr_array_header_t *changes,
                    svn_merge_range_inheritance_t consider_inheritance,
                    apr_pool_t *pool);

/** Removes @a eraser (the subtrahend) from @a whiteboard (the
 * minuend), and places the resulting difference in @a output.
 *
 * Note: @a eraser and @a whiteboard must be sorted as said by @c
 * svn_sort_compare_ranges().  @a output is guaranteed to be in sorted
 * order.
 *
 * @a consider_inheritance determines how to account for the inheritability
 * of @a whiteboard and @a *eraser when removing ranges.
 *
 * @since New in 1.5.
 */
svn_error_t *
svn_rangelist_remove(apr_array_header_t **output, apr_array_header_t *eraser,
                     apr_array_header_t *whiteboard,
                     svn_merge_range_inheritance_t consider_inheritance,
                     apr_pool_t *pool);

/** Find the intersection of two rangelists consisting of @c
 * svn_merge_range_t * elements, @a rangelist1 and @a rangelist2, and
 * place the result in @a output.
 *
 * Note: @a rangelist1 and @a rangelist2 must be sorted as said by @c
 * svn_sort_compare_ranges(). @a output is guaranteed to be in sorted
 * order.
 * @since New in 1.5.
 */
svn_error_t *
svn_rangelist_intersect(apr_array_header_t **output,
                        apr_array_header_t *rangelist1,
                        apr_array_header_t *rangelist2,
                        apr_pool_t *pool);

/** Reverse @a rangelist, and the @c start and @c end fields of each
 * range in @a rangelist, in place.
 * @since New in 1.5.
 */
svn_error_t *
svn_rangelist_reverse(apr_array_header_t *rangelist, apr_pool_t *pool);

/** Take an array of svn_merge_range_t *'s in @a input, and convert it
 * back to a text format rangelist in @a output.  If @a input contains
 * no elements, return the empty string.
 *
 * @since New in 1.5.
 */
svn_error_t *
svn_rangelist_to_stringbuf(svn_stringbuf_t **output,
                           apr_array_header_t *rangeinput,
                           apr_pool_t *pool);

/** Take an array of svn_merge_range_t *'s in @a rangelist, and return the
 * number of distint revisions included in it.
 *
 * @since New in 1.5.
 */
apr_uint64_t
svn_rangelist_count_revs(apr_array_header_t *rangelist);

/** Take an array of @c svn_merge_range_t *'s in @a rangelist, and convert it
 * to an array of @c svn_revnum_t's in @a revs.  If @a rangelist contains
 * no elements, return an empty array.
 *
 * @since New in 1.5.
 */
svn_error_t *
svn_rangelist_to_revs(apr_array_header_t **revs,
                      const apr_array_header_t *rangelist,
                      apr_pool_t *pool);

/** Return a deep copy of @c svn_merge_range_t *'s in @a rangelist excluding
 * all non-inheritable @c svn_merge_range_t.  If @a start and @a end are valid
 * revisions and @start is less than or equal to @end, then exclude only the
 * non-inheritable revision ranges that intersect inclusively with the range
 * defined by @a start and @a end.  If @a rangelist contains no elements, return
 * an empty array.  Allocate the copy in @a pool.
 *
 * @since New in 1.5.
 */
svn_error_t *
svn_rangelist_inheritable(apr_array_header_t **inheritable_rangelist,
                          apr_array_header_t *rangelist,
                          svn_revnum_t start,
                          svn_revnum_t end,
                          apr_pool_t *pool);

/** Remove redundancies between @ *range_1 and @ *range_2.  @ *range_1 and/or
 * @ *range_2 may be additive or subtractive ranges.  The ranges should be
 * sorted such that the minimum of @ *range_1->start and @ *range_1->end is
 * less than or equal to the minimum of @ *range_2->start and
 * @ *range_2->end.
 *
 * If either @ *range_1 or @ *range_2 is NULL, either range contains
 * invalid svn_revnum_t's, or the two ranges do not intersect, then do
 * nothing and return false.
 *
 * If the two ranges can be reduced to one range, set @ *range_1 to represent
 * that range, set @ *range_2 to NULL, and return true.
 *
 * If the two ranges cancel each other out set both @ *range_1 and
 * @ *range_2 to NULL and return true.
 *
 * If the two ranges intersect but cannot be represented by one range (because
 * one range is additive and the other subtractive) then modify @ *range_1 and
 * @ *range_2 to remove the intersecting ranges and return true.
 *
 * The inheritability of @ *range_1 or @ *range_2 is not taken into account.
 *
 * @since New in 1.5.
 */
svn_boolean_t
svn_range_compact(svn_merge_range_t **range_1,
                  svn_merge_range_t **range_2);

/** Return a deep copy of @a mergeinfo, a mapping from paths to
 * @c apr_array_header_t *'s of @c svn_merge_range_t *, excluding all
 * non-inheritable @c svn_merge_range_t.  If @a start and @a end are valid
 * revisions and @start is less than or equal to @end, then exclude only the
 * non-inheritable revisions that intersect inclusively with the range
 * defined by @a start and @a end.  If @a path is not NULL remove
 * non-inheritable ranges only for @a path.  If @a mergeinfo is an empty hash,
 * return an empty hash.  Allocate the copy in @a pool.
 *
 * @since New in 1.5.
 */
svn_error_t *
svn_mergeinfo_inheritable(apr_hash_t **inheritable_mergeinfo,
                          apr_hash_t *mergeinfo,
                          const char *path,
                          svn_revnum_t start,
                          svn_revnum_t end,
                          apr_pool_t *pool);

/** Take a hash of mergeinfo in @a mergeinput, and convert it back to
 * a text format mergeinfo in @a output.  If @a input contains no
 * elements, return the empty string.
 *
 * @since New in 1.5.
 */
svn_error_t *
svn_mergeinfo_to_stringbuf(svn_stringbuf_t **output, apr_hash_t *mergeinput,
                           apr_pool_t *pool);

/** Take a hash of mergeinfo in @a mergeinput, and sort the rangelists
 * associated with each key.
 * Note: This does not sort the hash, only the range lists in the
 * hash.
 * @since New in 1.5
 */
svn_error_t *
svn_mergeinfo_sort(apr_hash_t *mergeinput, apr_pool_t *pool);

/** Return a deep copy of @a mergeinfo, allocated in @a pool.
 *
 * @since New in 1.5.
 */
apr_hash_t *
svn_mergeinfo_dup(apr_hash_t *mergeinfo, apr_pool_t *pool);

/** Return a deep copy of @a rangelist, allocated in @a pool.
 *
 * @since New in 1.5.
 */
apr_array_header_t *
svn_rangelist_dup(apr_array_header_t *rangelist, apr_pool_t *pool);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_MERGEINFO_H */
