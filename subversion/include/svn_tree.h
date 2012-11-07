/*
 * svn_tree.h: reading a generic tree
 *
 * ====================================================================
 *    Licensed to the Apache Software Foundation (ASF) under one
 *    or more contributor license agreements.  See the NOTICE file
 *    distributed with this work for additional information
 *    regarding copyright ownership.  The ASF licenses this file
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

#ifndef SVN_TREE_H
#define SVN_TREE_H

#include <apr_hash.h>
#include "svn_types.h"
#include "svn_io.h"


#ifdef __cplusplus
extern "C" {
#endif


/** Set @a *node_p to the root node of @a tree.
 *
 * Every tree has a root node, but it is possible that an error could be
 * thrown for other reasons.
 */
svn_error_t *
svn_tree_get_root_node(svn_tree_node_t **node_p,
                       svn_tree_t *tree,
                       apr_pool_t *result_pool,
                       apr_pool_t *scratch_pool);

/** Set @a *node_p to the node that has relative path @a relpath within @a tree.
 *
 * Return an error if not found.
 */
svn_error_t *
svn_tree_get_node_by_relpath(svn_tree_node_t **node_p,
                             svn_tree_t *tree,
                             const char *relpath,
                             apr_pool_t *result_pool,
                             apr_pool_t *scratch_pool);

/** A tree-walker callback.
 *
 * This callback presents one tree node object being visited, @a node,
 * and the closure @a walk_baton.
 *
 * ### TODO: Consider re-modelling, more like Python's OS tree walker
 *     that passes a list of subdirs and a list of non-dir children,
 *     and lets the list of subdirs be modified before it recurses into them.
 *
 * @a scratch_pool is available for use within the function until it returns.
 */
typedef svn_error_t *(*svn_tree_walk_func_t)(svn_tree_node_t *node,
                                             void *walk_baton,
                                             apr_pool_t *scratch_pool);

/** Walk the generic tree @a tree.
 *
 * Traverse the tree depth-first, visiting the children in lexically sorted
 * order within a directory.  Recurse to depth @a depth.  Do not recurse
 * into any node for which there is no read authorization.
 *
 * Call @a walk_func for each visited node, passing @a walk_baton and the
 * tree node object.
 *
 * If @a cancel_func is not null, call it with @a cancel_baton to check for
 * cancellation.
 */
svn_error_t *
svn_tree_walk(svn_tree_t *tree,
              svn_depth_t depth,
              svn_tree_walk_func_t walk_func,
              void *walk_baton,
              svn_cancel_func_t cancel_func,
              void *cancel_baton,
              apr_pool_t *scratch_pool);

/** A two-tree-walker callback.
 *
 * This callback presents two tree node objects being visited, @a node1
 * and @a node2, and the closure @a walk_baton.
 *
 * ### TODO: Consider re-modelling, more like Python's OS tree walker
 *     that passes a list of subdirs and a list of non-dir children,
 *     and lets the list of subdirs be modified before it recurses into them.
 *
 * @a scratch_pool is available for use within the function until it returns.
 */
typedef svn_error_t *(*svn_tree_walk_two_func_t)(svn_tree_node_t *node1,
                                                 svn_tree_node_t *node2,
                                                 void *walk_baton,
                                                 apr_pool_t *scratch_pool);

/** Walk the two generic trees @a tree1 and @a tree2, simultaneously,
 * recursing to @a depth.
 *
 * Call @a walk_func for each node, passing @a walk_baton and the tree
 * node object.
 *
 * If @a cancel_func is not null, call it with @a cancel_baton to check for
 * cancellation.
 *
 * TODO: Visit nodes with the same id at the same time, thus tracking moves.
 * TODO: Let the callback determine the order of walking sub-nodes,
 * especially with respect to far-moves (moves into or out of a directory).
 */
svn_error_t *
svn_tree_walk_two(svn_tree_t *tree1,
                  svn_tree_t *tree2,
                  svn_depth_t depth,
                  const svn_tree_walk_two_func_t walk_func,
                  void *walk_baton,
                  svn_cancel_func_t cancel_func,
                  void *cancel_baton,
                  apr_pool_t *scratch_pool);

/* ---------------------------------------------------------------------- */

/** Set @a *relpath_p to the path of @a tree_node, relative to the root of
 * the tree.
 *
 * If @a tree_node is not readable due to lack of authorization,
 * return a #SVN_ERR_AUTHZ_UNREADABLE error.
 */
svn_error_t *
svn_tree_node_get_relpath(svn_tree_node_t *node,
                          const char **relpath_p,
                          apr_pool_t *result_pool,
                          apr_pool_t *scratch_pool);

/** Set @a *kind to the node kind of @a tree_node.
 *
 * The kind will be 'file', 'dir', 'symlink' or 'none'; not 'unknown'.
 *
 * If @a tree_node is not readable due to lack of authorization,
 * return a #SVN_ERR_AUTHZ_UNREADABLE error.
 */
svn_error_t *
svn_tree_node_get_kind(svn_tree_node_t *node,
                       svn_node_kind_t *kind,
                       apr_pool_t *scratch_pool);

/** Fetch the contents and/or properties of the file @a tree_node.
 *
 * If @a stream is non-NULL, set @a *stream to a readable stream yielding
 * the contents of the file.  (### ? The stream
 * handlers for @a stream may not perform any operations on @a tree_node.)
 *
 * ###?  If @a checksum is non-NULL, set @a *checksum to the SHA-1 checksum
 * of the file content.  The particular tree implementation must define
 * whether checksum is guaranteed to be filled in by the time this function
 * call returns or whether it is only guaranteed to happen by the time the
 * caller has read all of the data from @a stream.
 *
 * If @a props is non-NULL, set @a *props to contain the regular
 * versioned properties of the file (not 'wcprops', 'entryprops', etc.).
 * The hash maps (const char *) names to (#svn_string_t *) values.
 *
 * If @a tree_node is not readable due to lack of authorization,
 * return a #SVN_ERR_AUTHZ_UNREADABLE error; otherwise, if it is the wrong
 * kind of node, return a #SVN_ERR_WRONG_KIND error.
 */
svn_error_t *
svn_tree_node_read_file(svn_tree_node_t *node,
                        svn_stream_t **stream,
                        /* svn_checksum_t **checksum, */
                        apr_hash_t **props,
                        apr_pool_t *result_pool,
                        apr_pool_t *scratch_pool);

/** Fetch the entries and/or properties of the directory @a tree_node.
 *
 * If @a dirents is non-NULL, set @a *dirents to contain all the entries
 * of the directory @a tree_node.  The hash maps (const char *) entry
 * basenames to (svn_tree_node_t *) values.
 *
 * If @a props is non-NULL, set @a *props to contain the regular
 * versioned properties of the file (not 'wcprops', 'entryprops', etc.).
 * The hash maps (const char *) names to (#svn_string_t *) values.
 *
 * If @a tree_node is not readable due to lack of authorization,
 * return a #SVN_ERR_AUTHZ_UNREADABLE error; otherwise, if it is the wrong
 * kind of node, return a #SVN_ERR_WRONG_KIND error.
 */
svn_error_t *
svn_tree_node_read_dir(svn_tree_node_t *node,
                       apr_hash_t **dirents,
                       apr_hash_t **props,
                       apr_pool_t *result_pool,
                       apr_pool_t *scratch_pool);


#ifdef __cplusplus
}
#endif

#endif /* SVN_TREE_H */

