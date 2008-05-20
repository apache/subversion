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

/*
 * See the notes/tree-conflicts/ directory for more information.
 *
 * Currently, we only concern ourselves with the signaling of tree
 * conflicts as described in notes/tree-conflicts/use-cases.txt.
 *
 * There is no automatic resolution, the "desired behaviour" in
 * the use case descriptions is far from being implemented here.
 *
 * All we are trying to achieve is making sure that users are made aware
 * of having run into a potentially dangerous tree conflict situation.
 *
 * We do this by trying to recognize known tree conflict use cases
 * and persisting information about them in the "tree-conflicts" field
 * in the this_dir entry in the entries file for the directory containing
 * the tree conflict.
 *
 * A given directory may contain potentially many tree conflicts.
 * Each tree conflict is identified by the filename of the file
 * or directory (both a.k.a node) that it affects.
 * We call this node the "victim" of the tree conflict.
 *
 * For example, a file that is deleted by an update but locally
 * modified by the user is a victim of a tree conflict.
 *
 * For now, tree conflict victims are always direct children of the
 * directory in which the tree conflict is recorded.
 * (This may change once the way Subversion handles adm areas changes.)
 *
 * If a directory has tree conflicts, the "tree-conflict-data" field
 * in the entry for the directory contains one or more tree conflict
 * descriptions.
 *
 * Each description contains several fields, separated by
 * the following character:
 */

#define SVN_WC__TREE_CONFLICT_DESC_FIELD_SEPARATOR ':'

/*
 * The fields are:
 *
 *  victim_path:node_kind:operation:action:reason
 *
 * None of these fields can be empty.
 *
 * The path and victim_path fields correspond to the same fields in
 * svn_wc_conflict_description_t. If there is a colon in the
 * victim_path, it should be escaped with a backslash.
 * A literal backslash in victim_path must also be escaped with a backslash.
 * None of these fields is null-terminated!
 *
 * The escaping conventions mentioned in subversion/libsvn_wc/README also
 * apply, but are transparent to the tree conflicts code.
 *
 * The node_kind of the victim (corresponding to the node_kind field in
 * svn_wc_conflict_description_t) will be useful for tree conflict use cases
 * that involve both directories and files. It can be either "file" or "dir".
 * It cannot be "none".
 *
 * The operation field represents the svn operation that exposed the current
 * tree conflict, and corresponds to the operation field in
 * svn_wc_conflict_description_t.
 *
 * The action and reason fields correspond to the same fields in
 * svn_wc_conflict_description_t.
 *
 * The enum fields have the following mappings to character strings:
 *
 *  node_kind:
 */

#define SVN_WC__NODE_FILE "file"
#define SVN_WC__NODE_DIR "dir"

/*
 *  operation:
 */

#define SVN_WC__OPERATION_UPDATE "update"
#define SVN_WC__OPERATION_SWITCH "switch"
#define SVN_WC__OPERATION_MERGE "merge"

/*
 *  action:
 */

#define SVN_WC__CONFLICT_ACTION_EDITED "edited"
#define SVN_WC__CONFLICT_ACTION_DELETED "deleted"
#define SVN_WC__CONFLICT_ACTION_ADDED "added"

/*
 *  reason:
 */

#define SVN_WC__CONFLICT_REASON_EDITED "edited"
#define SVN_WC__CONFLICT_REASON_DELETED "deleted"
#define SVN_WC__CONFLICT_REASON_ADDED "added"
#define SVN_WC__CONFLICT_REASON_MISSING "missing"
#define SVN_WC__CONFLICT_REASON_OBSTRUCTED "obstructed"

/*
 * Multiple tree conflict descriptions can be present in one entry.
 * They are separated by the following character:
 */

#define SVN_WC__TREE_CONFLICT_DESC_SEPARATOR '|'

/*
 * Example entry with two tree conflicts:
 *
 *   foo.c:file:update:deleted:edited|bar.h:file:update:edited:deleted
 *
 * If the separator characters are needed in the victim_path field,
 * escape them with the following character:
 */

#define SVN_WC__TREE_CONFLICT_ESCAPE_CHAR '\\'

/*
 * The escape character can itself be escaped.
 *
 * From the information in the tree conflict entry, we can generate
 * a human readable description of tree conflicts in a user-visible
 * file inside the conflicted directory in the working copy.
 *
 * The user is presented with an error upon trying to commit without
 * having resolved the tree conflict by running 'svn resolved' on the
 * tree-conflict's victim.
 *
 * We try not to assume renames in any way. Use case 5 described
 * in the paper attached to issue #2282 requires true renames to be
 * detected,  which is impossible given how Subversion currently handles
 * 'svn move' internally. (Note: cmpilato said the "location segments"
 * feature could be used to detect use case 5.)
 *
 */

/* Like svn_wc_add_tree_conflict_data, but also takes a log accumulator
 * LOC_ACCUM, and does not flush the log.
 * This function is meant to be used in the working copy library where
 * log accumulators are usually readily available.
 */
svn_error_t *
svn_wc__loggy_add_tree_conflict_data(svn_stringbuf_t *log_accum,
                                     svn_wc_conflict_description_t *conflict,
                                     svn_wc_adm_access_t *adm_access,
                                     apr_pool_t *pool);

/*
 * Write tree conflicts (svn_wc_conflict_description_t)
 * in CONFLICTS to DIR_ENTRY.
 *
 * This function is used in a unit test in tests/libsvn_wc.
 */
svn_error_t *
svn_wc__write_tree_conflicts_to_entry(apr_array_header_t *conflicts,
                                      svn_wc_entry_t *dir_entry,
                                      apr_pool_t *pool);

/*
 * Search in CONFLICTS for a conflict with the given victim_path.
 *
 * This function is used in a unit test in tests/libsvn_wc.
 */
svn_boolean_t
svn_wc__tree_conflict_exists(apr_array_header_t *conflicts,
                             const char *victim_path);

#endif /* SVN_LIBSVN_WC_TREE_CONFLICTS_H */
