/*
 * tree.c: reading a generic tree
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

#include <assert.h>

#include "svn_pools.h"
#include "svn_sorts.h"
#include "svn_dirent_uri.h"
#include "svn_tree.h"

#include "private/svn_tree_impl.h"

#include "svn_private_config.h"


svn_tree_t *
svn_tree__create(const svn_tree__vtable_t *vtable,
                 void *baton,
                 apr_pool_t *result_pool)
{
  svn_tree_t *tree = apr_palloc(result_pool, sizeof(*tree));

  tree->vtable = vtable;
  tree->pool = result_pool;
  tree->priv = baton;
  return tree;
}

svn_error_t *
svn_tree_get_root_node(svn_tree_node_t **node_p,
                       svn_tree_t *tree,
                       apr_pool_t *result_pool,
                       apr_pool_t *scratch_pool)
{
  SVN_ERR(tree->vtable->get_node_by_relpath(node_p, tree, "",
                                            result_pool, scratch_pool));
  return SVN_NO_ERROR;
}

svn_error_t *
svn_tree_get_node_by_relpath(svn_tree_node_t **node_p,
                             svn_tree_t *tree,
                             const char *relpath,
                             apr_pool_t *result_pool,
                             apr_pool_t *scratch_pool)
{
  SVN_ERR(tree->vtable->get_node_by_relpath(node_p, tree, relpath,
                                            result_pool, scratch_pool));
  return SVN_NO_ERROR;
}

/* */
static svn_error_t *
tree_node_get_kind_or_unknown(svn_node_kind_t *kind,
                              svn_tree_node_t *node,
                              apr_pool_t *scratch_pool)
{
  svn_error_t *err = svn_tree_node_get_kind(node, kind, scratch_pool);

  if (err && err->apr_err == SVN_ERR_AUTHZ_UNREADABLE)
    {
      /* Can't read this node's kind. That's fine; pass 'unknown'. */
      svn_error_clear(err);
      *kind = svn_node_unknown;
      return SVN_NO_ERROR;
    }
  return svn_error_trace(err);
}

/* The body of svn_tree_walk(), which see.
 */
static svn_error_t *
walk_tree(svn_tree_node_t *node,
          svn_depth_t depth,
          svn_tree_walk_func_t walk_func,
          void *walk_baton,
          svn_cancel_func_t cancel_func,
          void *cancel_baton,
          apr_pool_t *scratch_pool)
{
  svn_node_kind_t kind;

  if (cancel_func)
    SVN_ERR(cancel_func(cancel_baton));

  SVN_ERR(tree_node_get_kind_or_unknown(&kind, node, scratch_pool));

  SVN_ERR(walk_func(node, walk_baton, scratch_pool));

  /* Recurse */
  if (kind == svn_node_dir && depth >= svn_depth_files)
    {
      apr_hash_t *dirents;
      apr_array_header_t *dirents_sorted;
      apr_pool_t *iterpool = svn_pool_create(scratch_pool);
      int i;

      SVN_ERR(svn_tree_node_read_dir(node, &dirents, NULL /* props */,
                                     scratch_pool, scratch_pool));
      dirents_sorted = svn_sort__hash(dirents,
                                      svn_sort_compare_items_lexically,
                                      scratch_pool);

      for (i = 0; i < dirents_sorted->nelts; i++)
        {
          const svn_sort__item_t *item
            = &APR_ARRAY_IDX(dirents_sorted, i, svn_sort__item_t);
          svn_tree_node_t *child = item->value;
          svn_node_kind_t child_kind;

          svn_pool_clear(iterpool);
          SVN_ERR(tree_node_get_kind_or_unknown(&child_kind, child, iterpool));
          if (depth >= svn_depth_immediates || child_kind == svn_node_file)
            {
              SVN_ERR(walk_tree(child,
                                depth == svn_depth_infinity ? depth
                                       : svn_depth_empty,
                                walk_func, walk_baton,
                                cancel_func, cancel_baton, iterpool));
            }
        }
      svn_pool_destroy(iterpool);
    }
  return SVN_NO_ERROR;
}

svn_error_t *
svn_tree_walk(svn_tree_t *tree,
              svn_depth_t depth,
              svn_tree_walk_func_t walk_func,
              void *walk_baton,
              svn_cancel_func_t cancel_func,
              void *cancel_baton,
              apr_pool_t *scratch_pool)
{
  svn_tree_node_t *node;

  SVN_ERR(svn_tree_get_root_node(&node, tree, scratch_pool, scratch_pool));
  SVN_ERR(walk_tree(node, depth,
                    walk_func, walk_baton,
                    cancel_func, cancel_baton, scratch_pool));
  return SVN_NO_ERROR;
}

/* Walk two trees, rooted at NODE1 and NODE2, simultaneously, driving the
 * CALLBACKS.
 *
 * Currently visits nodes with the same relpath at the same time; see TODO
 * notes on svn_tree_walk_two().
 *
 * TODO: allow recursing into a singleton directory (that is, on one side)?
 */
static svn_error_t *
walk_two_trees(svn_tree_node_t *node1,
               svn_tree_node_t *node2,
               svn_depth_t depth,
               const svn_tree_walk_two_func_t walk_func,
               void *walk_baton,
               svn_cancel_func_t cancel_func,
               void *cancel_baton,
               apr_pool_t *scratch_pool)
{
  const char *relpath1 = NULL, *relpath2 = NULL;
  svn_node_kind_t kind1 = svn_node_none, kind2 = svn_node_none;

  assert(node1 || node2);

  if (cancel_func)
    SVN_ERR(cancel_func(cancel_baton));

  if (node1)
    {
      SVN_ERR(svn_tree_node_get_relpath(node1, &relpath1,
                                        scratch_pool, scratch_pool));
      SVN_ERR(svn_tree_node_get_kind(node1, &kind1, scratch_pool));
    }
  if (node2)
    {
      SVN_ERR(svn_tree_node_get_relpath(node2, &relpath2,
                                        scratch_pool, scratch_pool));
      SVN_ERR(svn_tree_node_get_kind(node2, &kind2, scratch_pool));
    }
  if (node1 && node2)
    {
      assert(strcmp(relpath1, relpath2) == 0);  /* ### until move/rename support */
      assert(kind1 == kind2);  /* In Subversion a node can't change kind */
    }

  SVN_ERR(walk_func(node1, node2, walk_baton, scratch_pool));

  SVN_DBG(("walk_two_trees: kind %d/%d, '%s'\n",
           kind1, kind2, relpath1 ? relpath1 : relpath2));

  /* Recurse, if it's a directory on BOTH sides */
  if (node1 && node2
      && kind1 == svn_node_dir
      && depth >= svn_depth_files)
    {
      apr_hash_t *children1, *children2;
      apr_hash_t *all_children;
      apr_pool_t *iterpool = svn_pool_create(scratch_pool);
      apr_hash_index_t *hi;

      SVN_ERR(svn_tree_node_read_dir(node1, &children1, NULL,
                                     scratch_pool, scratch_pool));
      SVN_ERR(svn_tree_node_read_dir(node2, &children2, NULL,
                                     scratch_pool, scratch_pool));
      all_children = apr_hash_overlay(scratch_pool, children1, children2);

      SVN_DBG(("Recursing (%d||%d=%d children) in '%s'\n",
               apr_hash_count(children1), apr_hash_count(children2),
               apr_hash_count(all_children),
               relpath1));

      for (hi = apr_hash_first(scratch_pool, all_children); hi;
           hi = apr_hash_next(hi))
        {
          const char *relpath = svn__apr_hash_index_key(hi);
          svn_tree_node_t *child1 = apr_hash_get(children1, relpath,
                                                 APR_HASH_KEY_STRING);
          svn_tree_node_t *child2 = apr_hash_get(children2, relpath,
                                                 APR_HASH_KEY_STRING);
          svn_node_kind_t child_kind;

          svn_pool_clear(iterpool);

          SVN_ERR(tree_node_get_kind_or_unknown(&child_kind,
                                                child1 ? child1 : child2,
                                                iterpool));
          if (depth >= svn_depth_immediates || child_kind == svn_node_file)
            {
              SVN_ERR(walk_two_trees(child1, child2,
                                     depth == svn_depth_infinity ? depth
                                       : svn_depth_empty,
                                     walk_func, walk_baton,
                                     cancel_func, cancel_baton,
                                     iterpool));
            }
        }
      svn_pool_destroy(iterpool);
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_tree_walk_two(svn_tree_t *tree1,
                  svn_tree_t *tree2,
                  svn_depth_t depth,
                  const svn_tree_walk_two_func_t walk_func,
                  void *walk_baton,
                  svn_cancel_func_t cancel_func,
                  void *cancel_baton,
                  apr_pool_t *scratch_pool)
{
  svn_tree_node_t *node1, *node2;

  SVN_ERR(svn_tree_get_root_node(&node1, tree1, scratch_pool, scratch_pool));
  SVN_ERR(svn_tree_get_root_node(&node2, tree2, scratch_pool, scratch_pool));

  SVN_ERR(walk_two_trees(node1, node2,
                         depth,
                         walk_func, walk_baton,
                         cancel_func, cancel_baton,
                         scratch_pool));
  return SVN_NO_ERROR;
}

/* ---------------------------------------------------------------------- */

svn_tree_node_t *
svn_tree_node_create(const svn_tree_node__vtable_t *vtable,
                     void *baton,
                     apr_pool_t *result_pool)
{
  svn_tree_node_t *node = apr_palloc(result_pool, sizeof(*node));

  node->vtable = vtable;
  node->pool = result_pool;
  node->priv = baton;
  return node;
}

svn_error_t *
svn_tree_node_get_relpath(svn_tree_node_t *node,
                          const char **relpath_p,
                          apr_pool_t *result_pool,
                          apr_pool_t *scratch_pool)
{
  SVN_ERR(node->vtable->get_relpath(node, relpath_p,
                                    result_pool, scratch_pool));
  return SVN_NO_ERROR;
}

svn_error_t *
svn_tree_node_get_kind(svn_tree_node_t *node,
                       svn_node_kind_t *kind_p,
                       apr_pool_t *scratch_pool)
{
  return node->vtable->get_kind(node, kind_p, scratch_pool);
}

svn_error_t *
svn_tree_node_read_file(svn_tree_node_t *node,
                        svn_stream_t **stream,
                        /* svn_checksum_t **checksum, */
                        apr_hash_t **props,
                        apr_pool_t *result_pool,
                        apr_pool_t *scratch_pool)
{
  return node->vtable->get_file(node, stream, props,
                                 result_pool, scratch_pool);
}

svn_error_t *
svn_tree_node_read_dir(svn_tree_node_t *node,
                       apr_hash_t **children,
                       apr_hash_t **props,
                       apr_pool_t *result_pool,
                       apr_pool_t *scratch_pool)
{
  return node->vtable->read_dir(node, children, props,
                                result_pool, scratch_pool);
}
