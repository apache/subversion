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
 * to arrays of svn_merge_range_t.  Perform temporary allocations in
 * @a pool.
 *
 * @since New in 1.5.
 */
svn_error_t *
svn_mergeinfo_parse(const char *input, apr_hash_t **hash, apr_pool_t *pool);

/** Merge two hashes of merge info, @a in1 and @a in2,
 * and place the result in @a output.  
 * @since New in 1.5.
 */
svn_error_t *
svn_mergeinfo_merge(apr_hash_t **output, apr_hash_t *in1, apr_hash_t *in2,
                    apr_pool_t *pool);

/** Merge two rangelists consisting of @c svn_merge_range_t *
 *  elements, @a in1 and @a in2, and place the result in @a output.
 *  @since New in 1.5
 */
svn_error_t *
svn_rangelists_merge(apr_array_header_t **output, apr_array_header_t *in1,
                     apr_array_header_t *in2, apr_pool_t *pool);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_MERGEINFO_H */
