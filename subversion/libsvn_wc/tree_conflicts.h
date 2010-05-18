/*
 * tree_conflicts.h: declarations related to tree conflicts
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

#ifndef SVN_LIBSVN_WC_TREE_CONFLICTS_H
#define SVN_LIBSVN_WC_TREE_CONFLICTS_H

#include "wc.h"

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

/* Like svn_wc__del_tree_conflict(), but append to the log accumulator
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
svn_wc__loggy_del_tree_conflict(svn_stringbuf_t **log_accum,
                                const char *victim_path,
                                svn_wc_adm_access_t *adm_access,
                                apr_pool_t *pool);

/*
 * Encode tree conflict descriptions into a single string.
 *
 * Set *CONFLICT_DATA to a string, allocated in POOL, that encodes the tree
 * conflicts in CONFLICTS in a form suitable for storage in a single string
 * field in a WC entry. CONFLICTS is an array of zero or more pointers to
 * svn_wc_conflict_description_t objects. All of the conflict victim paths
 * must be siblings.
 *
 * Do all allocations in POOL.
 *
 * @see svn_wc__read_tree_conflicts()
 *
 * @since New in 1.6.
 */
svn_error_t *
svn_wc__write_tree_conflicts(const char **conflict_data,
                             apr_array_header_t *conflicts,
                             apr_pool_t *pool);

/*
 * Search in CONFLICTS (an array of svn_wc_conflict_description_t tree
 * conflicts) for a conflict with the given VICTIM_BASENAME.
 *
 * This function is used in a unit test in tests/libsvn_wc.
 *
 * @since New in 1.6.
 */
svn_boolean_t
svn_wc__tree_conflict_exists(apr_array_header_t *conflicts,
                             const char *victim_basename,
                             apr_pool_t *pool);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_LIBSVN_WC_TREE_CONFLICTS_H */
