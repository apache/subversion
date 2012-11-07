/*
 * svn_tree_impl.h: header for implementors of svn_tree_t
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

#ifndef SVN_TREE_IMPL_H
#define SVN_TREE_IMPL_H

#include "svn_tree.h"

#ifdef __cplusplus
extern "C" {
#endif


/** V-table for #svn_tree_t. */
typedef struct svn_tree__vtable_t
{
  /* See svn_tree_get_node_by_relpath(). */
  svn_error_t *(*get_node_by_relpath)(svn_tree_node_t **node,
                                      svn_tree_t *tree,
                                      const char *relpath,
                                      apr_pool_t *result_pool,
                                      apr_pool_t *scratch_pool);

} svn_tree__vtable_t;

/** The implementation of the typedef #svn_tree_t. */
struct svn_tree_t
{
  const svn_tree__vtable_t *vtable;

  /* Private data for the implementation. */
  void *priv;

  /* Pool used to manage this session.  ### What "session"? */
  apr_pool_t *pool;
};

/** Create a new "tree" object with the given VTABLE and BATON.
 *
 * This is for use by an implementation of the tree class.
 */
svn_tree_t *
svn_tree__create(const svn_tree__vtable_t *vtable,
                 void *baton,
                 apr_pool_t *result_pool);


/* ---------------------------------------------------------------------- */

/** V-table for #svn_tree_node_t. */
typedef struct svn_tree_node__vtable_t
{
  /* See svn_tree_node_get_relpath(). */
  svn_error_t *(*get_relpath)(svn_tree_node_t *node,
                              const char **relpath,
                              apr_pool_t *result_pool,
                              apr_pool_t *scratch_pool);

  /* See svn_tree_node_get_kind(). */
  svn_error_t *(*get_kind)(svn_tree_node_t *node,
                           svn_node_kind_t *kind,
                           apr_pool_t *scratch_pool);

  /* See svn_tree_node_get_file(). */
  svn_error_t *(*get_file)(svn_tree_node_t *node,
                           svn_stream_t **stream,
                           apr_hash_t **props,
                           apr_pool_t *result_pool,
                           apr_pool_t *scratch_pool);

  /* See svn_tree_node_get_dir(). */
  svn_error_t *(*read_dir)(svn_tree_node_t *node,
                          apr_hash_t **children,
                          apr_hash_t **props,
                          apr_pool_t *result_pool,
                          apr_pool_t *scratch_pool);
} svn_tree_node__vtable_t;

/** The implementation of the typedef #svn_tree_t. */
struct svn_tree_node_t
{
  const svn_tree_node__vtable_t *vtable;

  /** Private data for the implementation. */
  void *priv;

  /** Pool for ### what purpose/lifetime? */
  apr_pool_t *pool;
};

/** Create a new "tree node" object with the given VTABLE and BATON.
 *
 * This is for use by an implementation of the tree class.
 */
svn_tree_node_t *
svn_tree_node_create(const svn_tree_node__vtable_t *vtable,
                     void *baton,
                     apr_pool_t *result_pool);


#ifdef __cplusplus
}
#endif

#endif /* SVN_TREE_IMPL_H */

