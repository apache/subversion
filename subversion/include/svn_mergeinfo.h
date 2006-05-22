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


#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/** Parse the mergeinfo stored @a input, into @a
 * hash mapping from paths to arrays of svn_merge_range_t.
 * Perform temporary allocations in @a pool.
 * @since New in 1.5.
 */
svn_error_t *
svn_parse_mergeinfo(const char *input, apr_hash_t **hash, apr_pool_t *pool);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_MERGEINFO_H */
