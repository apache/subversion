/*
 * wc_trees.c: implementation of generic tree access to a WC
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
#include "svn_wc.h"
#include "svn_tree.h"

#include "private/svn_tree_impl.h"
#include "private/svn_wc_private.h"

#include "svn_private_config.h"
#include "wc.h"


/* */
typedef struct wc_tree_baton_t
{
  const char *tree_abspath;
  svn_wc_context_t *wc_ctx;
  svn_boolean_t is_base;  /* true -> base, false -> pristine or "actual" */
  svn_boolean_t is_pristine;  /* true -> pristine, false -> base or "actual" */
    /* "Pristine" means the WC "working" version for a mod/copy/move,
     * but the WC "base" version for a delete/add/replace. */
    /* "Working" means:
     *   - the WC "working" version for a mod/copy/move,
     *   - "nothing" for a delete,
     *   - ??? for an add;
     * /replace. */
} wc_tree_baton_t;

/* */
typedef struct wc_tree_node_baton_t
{
  svn_tree_t *tree;
  const char *relpath;
} wc_tree_node_baton_t;

/* Forward declaration */
static svn_tree_node_t *
wc_tree_node_create(svn_tree_t *tree,
                    const char *relpath,
                    apr_pool_t *result_pool);

/* */
static svn_error_t *
wc_tree_get_node_by_relpath(svn_tree_node_t **node,
                            svn_tree_t *tree,
                            const char *relpath,
                            apr_pool_t *result_pool,
                            apr_pool_t *scratch_pool)
{
  *node = wc_tree_node_create(tree, relpath, result_pool);
  return SVN_NO_ERROR;
}

/* */
static svn_error_t *
wc_treen_get_relpath(svn_tree_node_t *node,
                     const char **relpath_p,
                     apr_pool_t *result_pool,
                     apr_pool_t *scratch_pool)
{
  wc_tree_node_baton_t *nb = node->priv;

  *relpath_p = nb->relpath;  /* ### not duped */
  return SVN_NO_ERROR;
}

/* */
static svn_error_t *
wc_treen_get_kind(svn_tree_node_t *node,
                  svn_node_kind_t *kind,
                  apr_pool_t *scratch_pool)
{
  wc_tree_node_baton_t *nb = node->priv;
  wc_tree_baton_t *tb = nb->tree->priv;
  const char *abspath = svn_dirent_join(tb->tree_abspath, nb->relpath,
                                        scratch_pool);

  if (tb->is_base)
    {
      SVN_DBG(("oops! BASE tree not fully implemented yet: returning WORKING kind"));
      SVN_ERR(/* ### svn_wc_read_base_kind */
              svn_wc_read_kind(kind, tb->wc_ctx, abspath,
                               FALSE /* show_hidden */, scratch_pool));
    }
  else if (tb->is_pristine)
    {
      SVN_ERR(svn_wc_read_kind(kind, tb->wc_ctx, abspath,
                               FALSE /* show_hidden */, scratch_pool));
    }
  else
    {
      SVN_ERR(svn_io_check_path(abspath, kind, scratch_pool));
    }
  return SVN_NO_ERROR;
}

/* */
static svn_error_t *
wc_read_props(apr_hash_t **props,
              wc_tree_baton_t *tb,
              const char *abspath,
              apr_pool_t *result_pool,
              apr_pool_t *scratch_pool)
{
  if (props)
    {
      if (tb->is_base)
        {
          SVN_ERR(svn_wc__db_base_get_props(props, tb->wc_ctx->db, abspath,
                                            result_pool, scratch_pool));
        }
      else if (tb->is_pristine)
        {
          SVN_ERR(svn_wc_get_pristine_props(props, tb->wc_ctx, abspath,
                                            result_pool, scratch_pool));
        }
      else
        {
          SVN_ERR(svn_wc_prop_list2(props, tb->wc_ctx, abspath,
                                    result_pool, scratch_pool));
        }
    }
  return SVN_NO_ERROR;
}

/* */
static svn_error_t *
wc_treen_read_file(svn_tree_node_t *node,
                   svn_stream_t **stream,
                   apr_hash_t **props,
                   apr_pool_t *result_pool,
                   apr_pool_t *scratch_pool)
{
  wc_tree_node_baton_t *nb = node->priv;
  wc_tree_baton_t *tb = nb->tree->priv;
  const char *abspath = svn_dirent_join(tb->tree_abspath, nb->relpath,
                                        scratch_pool);

  if (stream)
    {
      if (tb->is_base)
        {
          const svn_checksum_t *checksum;

          SVN_ERR(svn_wc__db_base_get_info(NULL, NULL, NULL, NULL, NULL, NULL,
                                           NULL, NULL, NULL, NULL, &checksum,
                                           NULL, NULL, NULL, NULL,
                                           tb->wc_ctx->db, abspath,
                                           scratch_pool, scratch_pool));
          if (checksum)
            SVN_ERR(svn_wc__db_pristine_read(stream, NULL /* size */,
                                             tb->wc_ctx->db, abspath,
                                             checksum,
                                             result_pool, scratch_pool));
          else
            *stream = NULL;
        }
      else if (tb->is_pristine)
        {
          SVN_ERR(svn_wc_get_pristine_contents2(stream, tb->wc_ctx, abspath,
                                                result_pool, scratch_pool));
        }
      else
        {
          SVN_ERR(svn_stream_open_readonly(stream, abspath,
                                           result_pool, scratch_pool));
        }
    }

  SVN_ERR(wc_read_props(props, tb, abspath, result_pool, scratch_pool));

  return SVN_NO_ERROR;
}

/* */
static svn_error_t *
wc_treen_read_dir(svn_tree_node_t *node,
                  apr_hash_t **children_p,
                  apr_hash_t **props,
                  apr_pool_t *result_pool,
                  apr_pool_t *scratch_pool)
{
  wc_tree_node_baton_t *nb = node->priv;
  wc_tree_baton_t *tb = nb->tree->priv;
  const char *abspath = svn_dirent_join(tb->tree_abspath, nb->relpath,
                                        scratch_pool);

  if (children_p)
    {
      apr_pool_t *iterpool = svn_pool_create(scratch_pool);
      const apr_array_header_t *wc_children;
      int i;
      apr_hash_t *tree_children = apr_hash_make(result_pool);

      if (tb->is_base)
        {
          SVN_ERR(svn_wc__db_base_get_children(
                    &wc_children, tb->wc_ctx->db, abspath,
                    result_pool, scratch_pool));
        }
      else if (tb->is_pristine)
        {
          SVN_ERR(svn_wc__node_get_children_of_working_node(
                    &wc_children, tb->wc_ctx, abspath, FALSE /* show_hidden */,
                    result_pool, scratch_pool));
        }
      else
        {
          SVN_ERR(svn_wc__node_get_children(
                    &wc_children, tb->wc_ctx, abspath, FALSE /* show_hidden */,
                    result_pool, scratch_pool));
        }

      for (i = 0; i < wc_children->nelts; i++)
        {
          const char *child_abspath = APR_ARRAY_IDX(wc_children, i, const char *);
          const char *name, *relpath;
          svn_tree_node_t *child;

          svn_pool_clear(iterpool);
          name = svn_dirent_basename(child_abspath, iterpool);
          relpath = svn_relpath_join(nb->relpath, name, result_pool);
          child = wc_tree_node_create(nb->tree, relpath, result_pool);
          apr_hash_set(tree_children, name, APR_HASH_KEY_STRING, child);
        }
      svn_pool_destroy(iterpool);

      *children_p = tree_children;
    }

  SVN_ERR(wc_read_props(props, tb, abspath, result_pool, scratch_pool));

  return SVN_NO_ERROR;
}

/* */
static const svn_tree__vtable_t wc_tree_vtable =
{
  wc_tree_get_node_by_relpath
};

/* */
static const svn_tree_node__vtable_t wc_tree_node_vtable =
{
  wc_treen_get_relpath,
  wc_treen_get_kind,
  wc_treen_read_file,
  wc_treen_read_dir
};

/* */
static svn_tree_node_t *
wc_tree_node_create(svn_tree_t *tree,
                    const char *relpath,
                    apr_pool_t *result_pool)
{
  wc_tree_node_baton_t *nb = apr_palloc(result_pool, sizeof(*nb));

  nb->tree = tree;
  nb->relpath = relpath;
  return svn_tree_node_create(&wc_tree_node_vtable, nb, result_pool);
}

svn_error_t *
svn_wc__open_base_tree(svn_tree_t **tree_p,
                       const char *abspath,
                       svn_wc_context_t *wc_ctx,
                       apr_pool_t *result_pool)
{
  wc_tree_baton_t *tb = apr_palloc(result_pool, sizeof(*tb));

  tb->tree_abspath = abspath;
  tb->wc_ctx = wc_ctx;
  tb->is_base = TRUE;
  tb->is_pristine = FALSE;
  *tree_p = svn_tree__create(&wc_tree_vtable, tb, result_pool);
  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__open_pristine_tree(svn_tree_t **tree_p,
                           const char *abspath,
                           svn_wc_context_t *wc_ctx,
                           apr_pool_t *result_pool)
{
  wc_tree_baton_t *tb = apr_palloc(result_pool, sizeof(*tb));

  tb->tree_abspath = abspath;
  tb->wc_ctx = wc_ctx;
  tb->is_base = FALSE;
  tb->is_pristine = TRUE;
  *tree_p = svn_tree__create(&wc_tree_vtable, tb, result_pool);
  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__open_actual_tree(svn_tree_t **tree_p,
                         const char *abspath,
                         svn_wc_context_t *wc_ctx,
                         apr_pool_t *result_pool)
{
  wc_tree_baton_t *tb = apr_palloc(result_pool, sizeof(*tb));

  tb->tree_abspath = abspath;
  tb->wc_ctx = wc_ctx;
  tb->is_base = FALSE;
  tb->is_pristine = FALSE;
  *tree_p = svn_tree__create(&wc_tree_vtable, tb, result_pool);
  return SVN_NO_ERROR;
}

