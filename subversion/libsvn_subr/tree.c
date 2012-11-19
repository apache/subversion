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

/* The body of svn_tree_walk_dirs(), which see.
 */
static svn_error_t *
walk_dirs(svn_tree_node_t *dir_node,
          svn_depth_t depth,
          svn_tree_dir_visit_func_t walk_func,
          void *walk_baton,
          svn_cancel_func_t cancel_func,
          void *cancel_baton,
          apr_pool_t *scratch_pool)
{
  apr_array_header_t *dirs
    = apr_array_make(scratch_pool, 1, sizeof(svn_tree_node_t *));
  apr_array_header_t *files
    = apr_array_make(scratch_pool, 1, sizeof(svn_tree_node_t *));
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);
  int i;

#ifdef SVN_DEBUG
  {
    svn_node_kind_t kind;

    SVN_ERR(tree_node_get_kind_or_unknown(&kind, dir_node, scratch_pool));
    assert(kind == svn_node_dir);
  }
#endif

  if (cancel_func)
    SVN_ERR(cancel_func(cancel_baton));

  if (depth >= svn_depth_files)
    {
      apr_hash_t *children;
      apr_array_header_t *children_sorted;

      SVN_ERR(svn_tree_node_read_dir(dir_node, &children, NULL /* props */,
                                     scratch_pool, scratch_pool));
      children_sorted = svn_sort__hash(children,
                                       svn_sort_compare_items_lexically,
                                       scratch_pool);

      /* Categorize the children into dirs and non-dirs */
      for (i = 0; i < children_sorted->nelts; i++)
        {
          const svn_sort__item_t *item
            = &APR_ARRAY_IDX(children_sorted, i, svn_sort__item_t);
          svn_tree_node_t *child = item->value;
          svn_node_kind_t child_kind;

          svn_pool_clear(iterpool);
          SVN_ERR(tree_node_get_kind_or_unknown(&child_kind, child, iterpool));
          if (child_kind == svn_node_dir)
            {
              APR_ARRAY_PUSH(dirs, svn_tree_node_t *) = child;
            }
          else
            {
              if (depth >= svn_depth_immediates)
                APR_ARRAY_PUSH(files, svn_tree_node_t *) = child;
            }
        }
    }

  /* Call the visitor callback */
  SVN_ERR(walk_func(dir_node, dirs, files, walk_baton, scratch_pool));

  /* Recurse */
  for (i = 0; i < dirs->nelts; i++)
    {
      const svn_sort__item_t *item
        = &APR_ARRAY_IDX(dirs, i, svn_sort__item_t);
      svn_tree_node_t *child = item->value;

      svn_pool_clear(iterpool);
      SVN_ERR(walk_dirs(child,
                        depth == svn_depth_infinity ? depth
                               : svn_depth_empty,
                        walk_func, walk_baton,
                        cancel_func, cancel_baton, iterpool));
    }
  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_tree_walk_dirs(svn_tree_node_t *root_dir_node,
              svn_depth_t depth,
              svn_tree_dir_visit_func_t dir_visit_func,
              void *dir_visit_baton,
              svn_cancel_func_t cancel_func,
              void *cancel_baton,
              apr_pool_t *scratch_pool)
{
  SVN_ERR(walk_dirs(root_dir_node, depth, dir_visit_func, dir_visit_baton,
                    cancel_func, cancel_baton, scratch_pool));

  return SVN_NO_ERROR;
}

/* Baton for per_dir_to_per_node_cb() */
typedef struct per_dir_to_per_node_baton_t
{
  svn_tree_walk_func_t node_walk_func;
  void *node_walk_baton;
} per_dir_to_per_node_baton_t;

/* A dir-walk callback that calls a per-node callback. */
static svn_error_t *
per_dir_to_per_node_cb(svn_tree_node_t *dir_node,
                       apr_array_header_t *subdirs,
                       apr_array_header_t *files,
                       void *dir_visit_baton,
                       apr_pool_t *scratch_pool)
{
  per_dir_to_per_node_baton_t *b = dir_visit_baton;
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);
  int i;

  /* Visit this dir */
  SVN_ERR(b->node_walk_func(dir_node, b->node_walk_baton, scratch_pool));

  /* Visit the non-directory children */
  for (i = 0; i < files->nelts; i++)
    {
      svn_tree_node_t *child_node = APR_ARRAY_IDX(files, i, svn_tree_node_t *);

      svn_pool_clear(iterpool);
      SVN_ERR(b->node_walk_func(child_node, b->node_walk_baton, iterpool));
    }
  svn_pool_destroy(iterpool);

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
  svn_node_kind_t kind;

  SVN_ERR(svn_tree_get_root_node(&node, tree, scratch_pool, scratch_pool));
  SVN_ERR(svn_tree_node_get_kind(node, &kind, scratch_pool));

  if (kind == svn_node_dir)
    {
      per_dir_to_per_node_baton_t b;

      b.node_walk_func = walk_func;
      b.node_walk_baton = walk_baton;
      SVN_ERR(walk_dirs(node, depth, per_dir_to_per_node_cb, &b,
                        cancel_func, cancel_baton, scratch_pool));
    }
  else
    {
      SVN_ERR(walk_func(node, walk_baton, scratch_pool));
    }

  return SVN_NO_ERROR;
}

/* Get the child node named NAME from CHILDREN, if it falls within the
 * requested DEPTH.
 *
 * CHILDREN maps (const char *) names to (svn_tree_node_t *) child nodes.
 * DEPTH is relative to the parent node of CHILDREN and is at least 'files'.
 *
 * If NAME is not present in CHILDREN, or DEPTH is 'files' and the child
 * node is not a file, then set *CHILD to NULL.
 */
static svn_error_t *
get_child(svn_tree_node_t **child,
          apr_hash_t *children,
          const char *name,
          svn_depth_t depth,
          apr_pool_t *scratch_pool)
{
  /* We shouldn't be called for depth less than 'files'. */
  assert(depth >= svn_depth_files);

  *child = apr_hash_get(children, name, APR_HASH_KEY_STRING);

  /* If we want to omit directory children, do so now. */
  if (*child && depth == svn_depth_files)
    {
      svn_node_kind_t kind;

      SVN_ERR(tree_node_get_kind_or_unknown(&kind, *child, scratch_pool));
      if (kind == svn_node_dir)
        *child = NULL;
    }
  return SVN_NO_ERROR;
}

/* Walk two trees, rooted at NODE1 and NODE2, in parallel, visiting nodes
 * with the same relpath at the same time.
 *
 * Call the WALK_FUNC with WALK_BATON for each visited pair of nodes.
 * Recurse as far as DEPTH.  When a directory appears at a given path only
 * in one of the trees, recurse into it only if WALK_SINGLETON_DIRS is true.
 *
 * Use CANCEL_FUNC with CANCEL_BATON for cancellation.
 */
static svn_error_t *
walk_two_trees(svn_tree_node_t *node1,
               svn_tree_node_t *node2,
               svn_depth_t depth,
               svn_boolean_t walk_singleton_dirs,
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
    }

  SVN_ERR(walk_func(node1, node2, walk_baton, scratch_pool));

  /* Recurse, if it's a directory on BOTH sides or if we're walking
   * singleton directories. */
  if (depth >= svn_depth_files
      && ((kind1 == svn_node_dir && kind2 == svn_node_dir)
          || ((kind1 == svn_node_dir || kind2 == svn_node_dir)
              && walk_singleton_dirs)))
    {
      apr_hash_t *children1, *children2;
      apr_hash_t *all_children;
      apr_pool_t *iterpool = svn_pool_create(scratch_pool);
      apr_hash_index_t *hi;

      if (kind1 == svn_node_dir)
        SVN_ERR(svn_tree_node_read_dir(node1, &children1, NULL,
                                       scratch_pool, scratch_pool));
      else
        children1 = apr_hash_make(scratch_pool);
      if (kind2 == svn_node_dir)
        SVN_ERR(svn_tree_node_read_dir(node2, &children2, NULL,
                                       scratch_pool, scratch_pool));
      else
        children2 = apr_hash_make(scratch_pool);
      all_children = apr_hash_overlay(scratch_pool, children1, children2);

      SVN_DBG(("Recursing (%d||%d=%d children) in '%s'\n",
               apr_hash_count(children1), apr_hash_count(children2),
               apr_hash_count(all_children),
               relpath1));

      /* Iterate through the children in parallel, pairing them up by name. */
      for (hi = apr_hash_first(scratch_pool, all_children); hi;
           hi = apr_hash_next(hi))
        {
          const char *name = svn__apr_hash_index_key(hi);
          svn_tree_node_t *child1, *child2;

          svn_pool_clear(iterpool);

          SVN_ERR(get_child(&child1, children1, name, depth, iterpool));
          SVN_ERR(get_child(&child2, children2, name, depth, iterpool));

          if (child1 || child2)
            {
              SVN_ERR(walk_two_trees(child1, child2,
                                     depth == svn_depth_infinity ? depth
                                       : svn_depth_empty, walk_singleton_dirs,
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
                  svn_boolean_t walk_singleton_dirs,
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
                         depth, walk_singleton_dirs,
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
