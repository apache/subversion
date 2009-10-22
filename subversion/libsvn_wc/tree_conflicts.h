/*
 * tree_conflicts.h: declarations related to tree conflicts
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

#ifndef SVN_LIBSVN_WC_TREE_CONFLICTS_H
#define SVN_LIBSVN_WC_TREE_CONFLICTS_H

#include <apr_pools.h>
#include <apr_tables.h>

#include "svn_string.h"
#include "svn_wc.h"

#include "wc_db.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/*
 * See the notes/tree-conflicts/ directory for more information
 * about tree conflicts in general.
 *
 * A given directory may contain potentially many tree conflicts.
 * Each tree conflict is identified by the path of the file
 * or directory (both a.k.a node) that it affects.
 * We call this file or directory the "victim" of the tree conflict.
 *
 * For example, a file that is deleted by an update but locally
 * modified by the user is a victim of a tree conflict.
 *
 * For now, tree conflict victims are always direct children of the
 * directory in which the tree conflict is recorded.
 * This may change once the way Subversion handles adm areas changes.
 *
 * If a directory has tree conflicts, the "tree-conflict-data" field
 * in the entry for the directory contains one or more tree conflict
 * descriptions stored using the "skel" format.
 */



/* Like svn_wc__add_tree_conflict(), but append to the log accumulator
 * LOG_ACCUM a command to rewrite the entry field, and do not flush the log.
 * This function is meant to be used in the working copy library where
 * log accumulators are usually readily available.
 *
 * If *LOG_ACCUM is NULL then set *LOG_ACCUM to a new stringbug allocated in
 * POOL, else append to the existing stringbuf there.
 *
 * @since New in 1.6.
 */
svn_error_t *
svn_wc__loggy_add_tree_conflict(svn_stringbuf_t **log_accum,
                                const svn_wc_conflict_description_t *conflict,
                                svn_wc_adm_access_t *adm_access,
                                apr_pool_t *pool);

/*
 * Encode tree conflict descriptions into a single string.
 *
 * Set *CONFLICT_DATA to a string, allocated in POOL, that encodes the tree
 * conflicts in CONFLICTS in a form suitable for storage in a single string
 * field in a WC entry. CONFLICTS is a hash of zero or more pointers to
 * svn_wc_conflict_description_t objects, index by their basenames. All of the
 * conflict victim paths must be siblings.
 *
 * Do all allocations in POOL.
 *
 * @see svn_wc__read_tree_conflicts()
 *
 * @since New in 1.6.
 */
svn_error_t *
svn_wc__write_tree_conflicts(const char **conflict_data,
                             apr_hash_t *conflicts,
                             apr_pool_t *pool);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_LIBSVN_WC_TREE_CONFLICTS_H */
