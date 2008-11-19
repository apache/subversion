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
 * descriptions.
 *
 * Each tree conflict description contains several fields,
 * separated by the following character:
 */

#define SVN_WC__TREE_CONFLICT_DESC_FIELD_SEPARATOR ':'

/*
 * The fields are:
 *
 *  victim_path:node_kind:operation:action:reason
 *
 * None of these fields are null-terminated.
 * None of these fields can be empty.
 *
 * The victim_path field stores the path of the tree conflict victim,
 * and corresponds to the 'path' field in svn_wc_conflict_description_t.
 *
 * The node_kind field indicates the node kind of the victim, and
 * corresponds to the node_kind field in svn_wc_conflict_description_t.
 *
 * The operation field represents the svn operation that exposed the
 * tree conflict, and corresponds to the operation field in
 * svn_wc_conflict_description_t.
 *
 * The action field describes the action which the operation
 * attempted to carry out on the victim, and corresponds to
 * the same field in svn_wc_conflict_description_t.
 *
 * The reason field describes the local change which contradicts
 * with the action. It corresponds to the same field in
 * svn_wc_conflict_description_t.
 */

/*
 * When multiple tree conflict descriptions are present in an entry,
 * they are separated by the following character:
 */

#define SVN_WC__TREE_CONFLICT_DESC_SEPARATOR '|'

/*
 * Here is an example entry with two tree conflicts:
 *
 *   foo.c:file:update:deleted:edited|bar.h:file:update:edited:deleted
 */

/*
 * If the field separator occurs in the victim_path, it must be escaped
 * with the following character:
 */

#define SVN_WC__TREE_CONFLICT_ESCAPE_CHAR '\\'

/*
 * Likewise, if a description separator character is present in the
 * victim_path, it must also escaped. A literal escape character
 * occurring in the victim_path must also be escaped.
 *
 * The escaping conventions mentioned in subversion/libsvn_wc/README
 * also apply, but are transparent to the tree conflicts code.
 */

/*
 * The other fields have the following mappings to character strings:
 *
 *  node_kind:
 */

#define SVN_WC__NODE_FILE "file"
#define SVN_WC__NODE_DIR "dir"

/* (Contrary to svn_node_kind_t, the node_kind field cannot be "none".) */

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
 * Write tree conflict descriptions to CONFLICT_DATA.
 * Replace the entry's list of tree conflicts with those in CONFLICTS, an
 * array of zero or more pointers to svn_wc_conflict_description_t objects.
 * Do all allocations in POOL.
 *
 * @since New in 1.6.
 */
svn_error_t *
svn_wc__write_tree_conflicts(char **conflict_data,
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

#endif /* SVN_LIBSVN_WC_TREE_CONFLICTS_H */
