/*
 * disk_trees.c: generic tree implementation of unversioned disk trees
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

#include "svn_types.h"
#include "svn_pools.h"
#include "svn_dirent_uri.h"
#include "svn_path.h"
#include "svn_tree.h"

#include "private/svn_tree_impl.h"
#include "private/svn_subr_private.h"

#include "svn_private_config.h"


/* */
typedef struct disk_tree_baton_t
{
  const char *tree_abspath;
} disk_tree_baton_t;

/* */
typedef struct disk_tree_node_baton_t
{
  svn_tree_t *tree;
  disk_tree_baton_t *tb;
  const char *relpath;
} disk_tree_node_baton_t;

/* Forward declaration */
static svn_tree_node_t *
disk_tree_node_create(svn_tree_t *tree,
                      const char *relpath,
                      apr_pool_t *result_pool);

/* */
static svn_error_t *
disk_tree_get_node_by_relpath(svn_tree_node_t **node,
                              svn_tree_t *tree,
                              const char *relpath,
                              apr_pool_t *result_pool,
                              apr_pool_t *scratch_pool)
{
  *node = disk_tree_node_create(tree, relpath, result_pool);
  return SVN_NO_ERROR;
}

/* */
static svn_error_t *
disk_treen_get_relpath(svn_tree_node_t *node,
                       const char **relpath_p,
                       apr_pool_t *result_pool,
                       apr_pool_t *scratch_pool)
{
  disk_tree_node_baton_t *nb = node->priv;

  *relpath_p = nb->relpath;
  return SVN_NO_ERROR;
}

/* */
static svn_error_t *
disk_treen_get_kind(svn_tree_node_t *node,
                    svn_node_kind_t *kind,
                    apr_pool_t *scratch_pool)
{
  disk_tree_node_baton_t *nb = node->priv;
  const char *abspath = svn_dirent_join(nb->tb->tree_abspath,
                                        nb->relpath, scratch_pool);

  SVN_ERR(svn_io_check_path(abspath, kind, scratch_pool));
  return SVN_NO_ERROR;
}

/* */
static svn_error_t *
disk_treen_read_file(svn_tree_node_t *node,
                     svn_stream_t **stream,
                     apr_hash_t **props,
                     apr_pool_t *result_pool,
                     apr_pool_t *scratch_pool)
{
  disk_tree_node_baton_t *nb = node->priv;
  const char *abspath = svn_dirent_join(nb->tb->tree_abspath,
                                        nb->relpath, scratch_pool);

  if (stream)
    SVN_ERR(svn_stream_open_readonly(stream, abspath,
                                     result_pool, scratch_pool));
  if (props)
    *props = apr_hash_make(result_pool);

  return SVN_NO_ERROR;
}

/* Read a directory from disk.
 * It's an unversioned tree on disk, so report no properties.
 * TODO: Consider adding the ability to report svn:executable,
 * auto-props, etc. like "svn add" does.
 */
static svn_error_t *
disk_treen_read_dir(svn_tree_node_t *node,
                    apr_hash_t **children_p,
                    apr_hash_t **props,
                    apr_pool_t *result_pool,
                    apr_pool_t *scratch_pool)
{
  disk_tree_node_baton_t *nb = node->priv;
  const char *abspath = svn_dirent_join(nb->tb->tree_abspath,
                                        nb->relpath, scratch_pool);

  if (children_p)
    {
      apr_hash_t *dirents;
      apr_hash_index_t *hi;
      apr_hash_t *children = apr_hash_make(result_pool);

      SVN_ERR(svn_io_get_dirents3(&dirents, abspath, FALSE,
                                  result_pool, scratch_pool));

      /* Convert RA dirents to tree children */
      for (hi = apr_hash_first(scratch_pool, dirents); hi;
           hi = apr_hash_next(hi))
        {
          const char *name = svn__apr_hash_index_key(hi);
          const char *relpath = svn_relpath_join(nb->relpath, name, result_pool);
          svn_tree_node_t *child;

          child = disk_tree_node_create(nb->tree, relpath, result_pool);
          apr_hash_set(children, name, APR_HASH_KEY_STRING, child);
        }
      *children_p = children;
    }

  /* It's an unversioned tree on disk, so report no properties. */
  if (props)
    *props = apr_hash_make(result_pool);

  return SVN_NO_ERROR;
}

/* */
static const svn_tree__vtable_t disk_tree_vtable =
{
  disk_tree_get_node_by_relpath
};

/* */
static const svn_tree_node__vtable_t disk_tree_node_vtable =
{
  disk_treen_get_relpath,
  disk_treen_get_kind,
  disk_treen_read_file,
  disk_treen_read_dir
};

/* */
static svn_tree_node_t *
disk_tree_node_create(svn_tree_t *tree,
                      const char *relpath,
                      apr_pool_t *result_pool)
{
  disk_tree_node_baton_t *nb = apr_palloc(result_pool, sizeof(*nb));

  nb->tree = tree;
  nb->tb = tree->priv;
  nb->relpath = relpath;
  return svn_tree_node_create(&disk_tree_node_vtable, nb, result_pool);
}

svn_error_t *
svn_io__open_tree(svn_tree_t **tree_p,
                  const char *abspath,
                  apr_pool_t *result_pool)
{
  disk_tree_baton_t *tb = apr_palloc(result_pool, sizeof(*tb));

  tb->tree_abspath = abspath;

  *tree_p = svn_tree__create(&disk_tree_vtable, tb, result_pool);
  return SVN_NO_ERROR;
}
