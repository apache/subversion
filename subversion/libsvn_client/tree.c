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

#include "svn_pools.h"
#include "svn_sorts.h"
#include "svn_dirent_uri.h"
#include "svn_path.h"
#include "client.h"
#include "tree.h"
#include "private/svn_wc_private.h"
#include "svn_private_config.h"


/*-----------------------------------------------------------------*/


/* V-table for #svn_tree_t. */
typedef struct svn_tree__vtable_t
{
  /* See svn_tree_get_kind(). */
  svn_error_t *(*get_kind)(svn_tree_t *tree,
                           svn_kind_t *kind,
                           const char *relpath,
                           apr_pool_t *scratch_pool);

  /* See svn_tree_get_file(). */
  svn_error_t *(*get_file)(svn_tree_t *tree,
                           svn_stream_t **stream,
                           apr_hash_t **props,
                           const char *relpath,
                           apr_pool_t *result_pool,
                           apr_pool_t *scratch_pool);

  /* See svn_tree_get_dir(). */
  svn_error_t *(*get_dir)(svn_tree_t *tree,
                          apr_hash_t **dirents,
                          apr_hash_t **props,
                          const char *relpath,
                          apr_pool_t *result_pool,
                          apr_pool_t *scratch_pool);

  /* See svn_tree_get_symlink(). */
  svn_error_t *(*get_symlink)(svn_tree_t *tree,
                              const char **link_target,
                              apr_hash_t **props,
                              const char *relpath,
                              apr_pool_t *result_pool,
                              apr_pool_t *scratch_pool);
} svn_tree__vtable_t;

/* The implementation of the typedef svn_tree_t. */
struct svn_tree_t
{
  const svn_tree__vtable_t *vtable;

  /* Pool used to manage this session. */
  apr_pool_t *pool;

  /* Private data for the tree implementation. */
  void *priv;
};


svn_error_t *
svn_tree_get_kind(svn_tree_t *tree,
                  svn_kind_t *kind,
                  const char *relpath,
                  apr_pool_t *scratch_pool)
{
  return tree->vtable->get_kind(tree, kind, relpath, scratch_pool);
}

svn_error_t *
svn_tree_get_file(svn_tree_t *tree,
                  svn_stream_t **stream,
                  apr_hash_t **props,
                  const char *relpath,
                  apr_pool_t *result_pool,
                  apr_pool_t *scratch_pool)
{
  return tree->vtable->get_file(tree, stream, props, relpath,
                                result_pool, scratch_pool);
}

svn_error_t *
svn_tree_get_dir(svn_tree_t *tree,
                 apr_hash_t **dirents,
                 apr_hash_t **props,
                 const char *relpath,
                 apr_pool_t *result_pool,
                 apr_pool_t *scratch_pool)
{
  return tree->vtable->get_dir(tree, dirents, props, relpath,
                               result_pool, scratch_pool);
}

svn_error_t *
svn_tree_get_symlink(svn_tree_t *tree,
                     const char **link_target,
                     apr_hash_t **props,
                     const char *relpath,
                     apr_pool_t *result_pool,
                     apr_pool_t *scratch_pool)
{
  return tree->vtable->get_symlink(tree, link_target, props, relpath,
                                   result_pool, scratch_pool);
}

/* */
static svn_error_t *
tree_get_kind_or_unknown(svn_tree_t *tree,
                         svn_kind_t *kind,
                         const char *relpath,
                         apr_pool_t *scratch_pool)
{
  svn_error_t *err = svn_tree_get_kind(tree, kind, relpath, scratch_pool);

  if (err && err->apr_err == SVN_ERR_AUTHZ_UNREADABLE)
    {
      /* Can't read this node's kind. That's fine; pass 'unknown'. */
      svn_error_clear(err);
      *kind = svn_kind_unknown;
      return SVN_NO_ERROR;
    }
  return svn_error_trace(err);
}

/* The body of svn_tree_walk(), which see.
 *
 * ### The handling of unauthorized-read errors is a bit under-defined.
 * For example, if the get_kind call returns 'dir' but then the 'get_dir'
 * returns unauthorized, the callback only gets an empty dir with no
 * indication that reading the children was unauthorized.
 */
static svn_error_t *
tree_walk(svn_tree_t *tree,
          const char *relpath,
          svn_depth_t depth,
          svn_tree_walk_func_t callback_func,
          void *callback_baton,
          svn_cancel_func_t cancel_func,
          void *cancel_baton,
          apr_pool_t *scratch_pool)
{
  svn_kind_t kind;
  apr_hash_t *dirents;

  if (cancel_func)
    SVN_ERR(cancel_func(cancel_baton));

  SVN_ERR(tree_get_kind_or_unknown(tree, &kind, relpath, scratch_pool));

  /* Fetch the dir's children, if needed, before calling the callback, so
   * that we can pass kind=unknown if fetching the children fails. */
  if (kind == svn_kind_dir && depth > svn_depth_empty)
    {
      svn_error_t *err
        = svn_tree_get_dir(tree, &dirents, NULL /* props */,
                           relpath, scratch_pool, scratch_pool);

      if (err && err->apr_err == SVN_ERR_AUTHZ_UNREADABLE)
        {
          /* Can't read this directory. That's fine; skip it. */
          svn_error_clear(err);
          return SVN_NO_ERROR;
        }
      else
        SVN_ERR(err);
    }

  SVN_ERR(callback_func(tree, relpath, kind, callback_baton, scratch_pool));

  /* Recurse (visiting the children in sorted order). */
  if (kind == svn_kind_dir && depth > svn_depth_empty)
    {
      apr_array_header_t *dirents_sorted
        = svn_sort__hash(dirents, svn_sort_compare_items_lexically,
                         scratch_pool);
      apr_pool_t *iterpool = svn_pool_create(scratch_pool);
      int i;

      for (i = 0; i < dirents_sorted->nelts; i++)
        {
          const svn_sort__item_t *item
            = &APR_ARRAY_IDX(dirents_sorted, i, svn_sort__item_t);
          const char *name = item->key;
          const char *child_relpath;
          svn_kind_t child_kind;

          svn_pool_clear(iterpool);
          child_relpath = svn_relpath_join(relpath, name, iterpool);
          SVN_ERR(tree_get_kind_or_unknown(tree, &child_kind, child_relpath,
                                           scratch_pool));
          if (depth > svn_depth_files || child_kind == svn_kind_file)
            {
              SVN_ERR(svn_tree_walk(tree, child_relpath,
                                    depth == svn_depth_infinity ? depth
                                           : svn_depth_empty,
                                    callback_func, callback_baton,
                                    cancel_func, cancel_baton, iterpool));
            }
        }
      svn_pool_destroy(iterpool);
    }
  return SVN_NO_ERROR;
}

svn_error_t *
svn_tree_walk(svn_tree_t *tree,
              const char *relpath,
              svn_depth_t depth,
              svn_tree_walk_func_t callback_func,
              void *callback_baton,
              svn_cancel_func_t cancel_func,
              void *cancel_baton,
              apr_pool_t *scratch_pool)
{
  SVN_ERR(tree_walk(tree, relpath, depth,
                    callback_func, callback_baton,
                    cancel_func, cancel_baton, scratch_pool));
  return SVN_NO_ERROR;
}


/*-----------------------------------------------------------------*/


/* */
typedef struct disk_tree_baton_t
{
  const char *tree_abspath;
} disk_tree_baton_t;

/* */
static svn_error_t *
disk_tree_get_kind(svn_tree_t *tree,
                   svn_kind_t *kind,
                   const char *relpath,
                   apr_pool_t *scratch_pool)
{
  disk_tree_baton_t *baton = tree->priv;
  const char *abspath = svn_dirent_join(baton->tree_abspath, relpath,
                                        scratch_pool);

  SVN_ERR(svn_io_check_path2(abspath, kind, scratch_pool));
  return SVN_NO_ERROR;
}

/* */
static svn_error_t *
disk_tree_get_file(svn_tree_t *tree,
                   svn_stream_t **stream,
                   apr_hash_t **props,
                   const char *relpath,
                   apr_pool_t *result_pool,
                   apr_pool_t *scratch_pool)
{
  disk_tree_baton_t *baton = tree->priv;
  const char *abspath = svn_dirent_join(baton->tree_abspath, relpath,
                                        scratch_pool);

  if (stream)
    SVN_ERR(svn_stream_open_readonly(stream, abspath,
                                     result_pool, scratch_pool));
  if (props)
    *props = apr_hash_make(result_pool);

  return SVN_NO_ERROR;
}

/* */
static svn_error_t *
disk_tree_get_dir(svn_tree_t *tree,
                  apr_hash_t **dirents,
                  apr_hash_t **props,
                  const char *relpath,
                  apr_pool_t *result_pool,
                  apr_pool_t *scratch_pool)
{
  disk_tree_baton_t *baton = tree->priv;
  const char *abspath = svn_dirent_join(baton->tree_abspath, relpath,
                                        scratch_pool);

  if (dirents)
    {
      SVN_ERR(svn_io_get_dirents3(dirents, abspath, FALSE,
                                  result_pool, scratch_pool));
    }
  if (props)
    *props = apr_hash_make(result_pool);

  return SVN_NO_ERROR;
}

/* */
static svn_error_t *
disk_tree_get_symlink(svn_tree_t *tree,
                      const char **link_target,
                      apr_hash_t **props,
                      const char *relpath,
                      apr_pool_t *result_pool,
                      apr_pool_t *scratch_pool)
{
  disk_tree_baton_t *baton = tree->priv;
  const char *abspath = svn_dirent_join(baton->tree_abspath, relpath,
                                        scratch_pool);

  if (link_target)
    {
      svn_string_t *dest;
      SVN_ERR(svn_io_read_link(&dest, abspath, result_pool));
      *link_target = dest->data;
    }
  if (props)
    *props = apr_hash_make(result_pool);

  return SVN_NO_ERROR;
}

/* */
static const svn_tree__vtable_t disk_tree_vtable =
{
  disk_tree_get_kind,
  disk_tree_get_file,
  disk_tree_get_dir,
  disk_tree_get_symlink
};

svn_error_t *
svn_client__disk_tree(svn_tree_t **tree_p,
                      const char *abspath,
                      apr_pool_t *result_pool)
{
  svn_tree_t *tree = apr_palloc(result_pool, sizeof(*tree));
  disk_tree_baton_t *baton = apr_palloc(result_pool, sizeof(*baton));

  baton->tree_abspath = abspath;

  tree->vtable = &disk_tree_vtable;
  tree->pool = result_pool;
  tree->priv = baton;

  *tree_p = tree;
  return SVN_NO_ERROR;
}

/*-----------------------------------------------------------------*/


/* */
typedef struct wc_tree_baton_t
{
  const char *tree_abspath;
  svn_wc_context_t *wc_ctx;
  svn_boolean_t is_base;  /* true -> base, false -> working */
} wc_tree_baton_t;

/* */
static svn_error_t *
wc_tree_get_kind(svn_tree_t *tree,
                 svn_kind_t *kind,
                 const char *relpath,
                 apr_pool_t *scratch_pool)
{
  wc_tree_baton_t *baton = tree->priv;
  const char *abspath = svn_dirent_join(baton->tree_abspath, relpath,
                                        scratch_pool);

  if (baton->is_base)
    {
      SVN_ERR(svn_wc_read_base_kind(kind, baton->wc_ctx, abspath,
                                    FALSE /* show_hidden */, scratch_pool));
    }
  else
    SVN_ERR(svn_wc_read_kind2(kind, baton->wc_ctx, abspath,
                              FALSE /* show_hidden */, scratch_pool));
  return SVN_NO_ERROR;
}

/* */
static svn_error_t *
wc_tree_get_file(svn_tree_t *tree,
                 svn_stream_t **stream,
                 apr_hash_t **props,
                 const char *relpath,
                 apr_pool_t *result_pool,
                 apr_pool_t *scratch_pool)
{
  wc_tree_baton_t *baton = tree->priv;
  const char *abspath = svn_dirent_join(baton->tree_abspath, relpath,
                                        scratch_pool);

  if (stream)
    {
      if (baton->is_base)
        SVN_ERR(svn_wc_get_pristine_contents2(stream, baton->wc_ctx, abspath,
                                              result_pool, scratch_pool));
      else
        SVN_ERR(svn_stream_open_readonly(stream, abspath,
                                         result_pool, scratch_pool));
    }
  if (props)
    {
      if (baton->is_base)
        SVN_ERR(svn_wc_get_pristine_props(props, baton->wc_ctx, abspath,
                                          result_pool, scratch_pool));
      else
        SVN_ERR(svn_wc_prop_list2(props, baton->wc_ctx, abspath,
                                  result_pool, scratch_pool));
    }

  return SVN_NO_ERROR;
}

/* */
static svn_error_t *
wc_tree_get_dir(svn_tree_t *tree,
                apr_hash_t **dirents,
                apr_hash_t **props,
                const char *relpath,
                apr_pool_t *result_pool,
                apr_pool_t *scratch_pool)
{
  wc_tree_baton_t *baton = tree->priv;
  const char *abspath = svn_dirent_join(baton->tree_abspath, relpath,
                                        scratch_pool);

  if (dirents)
    {
      const apr_array_header_t *children;
      int i;

      *dirents = apr_hash_make(result_pool);

      if (baton->is_base)
        SVN_ERR(svn_wc__base_get_children(
                  &children, baton->wc_ctx, abspath, FALSE /* show_hidden */,
                  result_pool, scratch_pool));
      else
        SVN_ERR(svn_wc__node_get_children_of_working_node(
                  &children, baton->wc_ctx, abspath, FALSE /* show_hidden */,
                  result_pool, scratch_pool));
      for (i = 0; i < children->nelts; i++)
        {
          const char *child_abspath = APR_ARRAY_IDX(children, i, const char *);
          const char *name = svn_dirent_basename(child_abspath, result_pool);

          apr_hash_set(*dirents, name, APR_HASH_KEY_STRING, name);
        }
    }
  if (props)
    {
      if (baton->is_base)
        SVN_ERR(svn_wc_get_pristine_props(props, baton->wc_ctx, abspath,
                                          result_pool, scratch_pool));
      else
        SVN_ERR(svn_wc_prop_list2(props, baton->wc_ctx, abspath,
                                  result_pool, scratch_pool));
    }

  return SVN_NO_ERROR;
}

/* */
static svn_error_t *
wc_tree_get_symlink(svn_tree_t *tree,
                    const char **link_target,
                    apr_hash_t **props,
                    const char *relpath,
                    apr_pool_t *result_pool,
                    apr_pool_t *scratch_pool)
{
  wc_tree_baton_t *baton = tree->priv;
  const char *abspath = svn_dirent_join(baton->tree_abspath, relpath,
                                        scratch_pool);

  if (link_target)
    {
      if (baton->is_base)
        *link_target = "";  /* ### */
      else
        {
          svn_string_t *dest;
          SVN_ERR(svn_io_read_link(&dest, abspath, result_pool));
          *link_target = dest->data;
        }
    }
  if (props)
    {
      if (baton->is_base)
        SVN_ERR(svn_wc_get_pristine_props(props, baton->wc_ctx, abspath,
                                          result_pool, scratch_pool));
      else
        SVN_ERR(svn_wc_prop_list2(props, baton->wc_ctx, abspath,
                                  result_pool, scratch_pool));
    }

  return SVN_NO_ERROR;
}

/* */
static const svn_tree__vtable_t wc_tree_vtable =
{
  wc_tree_get_kind,
  wc_tree_get_file,
  wc_tree_get_dir,
  wc_tree_get_symlink
};

svn_error_t *
svn_client__wc_base_tree(svn_tree_t **tree_p,
                         const char *abspath,
                         svn_client_ctx_t *ctx,
                         apr_pool_t *result_pool)
{
  svn_tree_t *tree = apr_pcalloc(result_pool, sizeof(*tree));
  wc_tree_baton_t *baton = apr_palloc(result_pool, sizeof(*baton));

  baton->tree_abspath = abspath;
  baton->wc_ctx = ctx->wc_ctx;
  baton->is_base = TRUE;

  tree->vtable = &wc_tree_vtable;
  tree->pool = result_pool;
  tree->priv = baton;

  *tree_p = tree;
  return SVN_NO_ERROR;
}

svn_error_t *
svn_client__wc_working_tree(svn_tree_t **tree_p,
                            const char *abspath,
                            svn_client_ctx_t *ctx,
                            apr_pool_t *result_pool)
{
  svn_tree_t *tree = apr_pcalloc(result_pool, sizeof(*tree));
  wc_tree_baton_t *baton = apr_palloc(result_pool, sizeof(*baton));

  baton->tree_abspath = abspath;
  baton->wc_ctx = ctx->wc_ctx;
  baton->is_base = FALSE;

  tree->vtable = &wc_tree_vtable;
  tree->pool = result_pool;
  tree->priv = baton;

  *tree_p = tree;
  return SVN_NO_ERROR;
}

/*-----------------------------------------------------------------*/


/* */
typedef struct ra_tree_baton_t
{
  svn_ra_session_t *ra_session;
  svn_revnum_t revnum;
} ra_tree_baton_t;

/* Wrap any RA-layer 'unauthorized read' error in an
 * SVN_ERR_AUTHZ_UNREADABLE error. */
static svn_error_t *
ra_unauthz_err(svn_error_t *err)
{
  if (err && ((err->apr_err == SVN_ERR_RA_NOT_AUTHORIZED) ||
              (err->apr_err == SVN_ERR_RA_DAV_FORBIDDEN)))
    {
      err = svn_error_createf(SVN_ERR_AUTHZ_UNREADABLE, err, NULL);
    }
  return err;
}

/* */
static svn_error_t *
ra_tree_get_kind(svn_tree_t *tree,
                 svn_kind_t *kind,
                 const char *relpath,
                 apr_pool_t *scratch_pool)
{
  ra_tree_baton_t *baton = tree->priv;

  SVN_ERR(ra_unauthz_err(svn_ra_check_path2(baton->ra_session, relpath,
                                            baton->revnum, kind,
                                            scratch_pool)));
  return SVN_NO_ERROR;
}

/* */
static svn_error_t *
ra_tree_get_file(svn_tree_t *tree,
                 svn_stream_t **stream,
                 apr_hash_t **props,
                 const char *relpath,
                 apr_pool_t *result_pool,
                 apr_pool_t *scratch_pool)
{
  ra_tree_baton_t *baton = tree->priv;
  svn_stream_t *holding_stream;
  
  SVN_ERR(svn_stream_open_unique(&holding_stream, NULL, NULL,
                                 svn_io_file_del_on_close,
                                 scratch_pool, scratch_pool));
  SVN_ERR(ra_unauthz_err(svn_ra_get_file(baton->ra_session, relpath,
                                         baton->revnum, holding_stream,
                                         NULL, props, result_pool)));
  SVN_ERR(svn_stream_reset(holding_stream));
  *stream = holding_stream;
  return SVN_NO_ERROR;
}

/* */
static svn_error_t *
ra_tree_get_dir(svn_tree_t *tree,
                apr_hash_t **dirents,
                apr_hash_t **props,
                const char *relpath,
                apr_pool_t *result_pool,
                apr_pool_t *scratch_pool)
{
  ra_tree_baton_t *baton = tree->priv;

  SVN_ERR(ra_unauthz_err(svn_ra_get_dir2(baton->ra_session,
                                         dirents, NULL, props,
                                         relpath, baton->revnum,
                                         0 /* dirent_fields */,
                                         result_pool)));
  return SVN_NO_ERROR;
}

/* */
static svn_error_t *
ra_tree_get_symlink(svn_tree_t *tree,
                    const char **link_target,
                    apr_hash_t **props,
                    const char *relpath,
                    apr_pool_t *result_pool,
                    apr_pool_t *scratch_pool)
{
  ra_tree_baton_t *baton = tree->priv;

  SVN_ERR(ra_unauthz_err(svn_ra_get_symlink(baton->ra_session, relpath,
                                            baton->revnum, link_target,
                                            NULL, props, result_pool)));
  return SVN_NO_ERROR;
}

/* */
static const svn_tree__vtable_t ra_tree_vtable =
{
  ra_tree_get_kind,
  ra_tree_get_file,
  ra_tree_get_dir,
  ra_tree_get_symlink
};

/* */
static svn_error_t *
read_ra_tree(svn_tree_t **tree_p,
             svn_ra_session_t *ra_session,
             svn_revnum_t revnum,
             apr_pool_t *result_pool)
{
  svn_tree_t *tree = apr_pcalloc(result_pool, sizeof(*tree));
  ra_tree_baton_t *baton = apr_palloc(result_pool, sizeof(*baton));

  baton->ra_session = ra_session;
  baton->revnum = revnum;

  tree->vtable = &ra_tree_vtable;
  tree->pool = result_pool;
  tree->priv = baton;

  *tree_p = tree;
  return SVN_NO_ERROR;
}

svn_error_t *
svn_client__repository_tree(svn_tree_t **tree_p,
                            const char *path_or_url,
                            const svn_opt_revision_t *peg_revision,
                            const svn_opt_revision_t *revision,
                            svn_client_ctx_t *ctx,
                            apr_pool_t *result_pool)
{
  svn_ra_session_t *ra_session;
  svn_revnum_t revnum;
  const char *url;

  /* Get the RA connection. */
  SVN_ERR(svn_client__ra_session_from_path(&ra_session, &revnum,
                                           &url, path_or_url, NULL,
                                           peg_revision, revision,
                                           ctx, result_pool));

  SVN_ERR(read_ra_tree(tree_p, ra_session, revnum, result_pool));

  return SVN_NO_ERROR;
}

/*-----------------------------------------------------------------*/

svn_error_t *
svn_client__open_tree(svn_tree_t **tree,
                      const char *path,
                      const svn_opt_revision_t *revision,
                      const svn_opt_revision_t *peg_revision,
                      svn_client_ctx_t *ctx,
                      apr_pool_t *result_pool,
                      apr_pool_t *scratch_pool)
{
  SVN_ERR_ASSERT(revision->kind != svn_opt_revision_unspecified);

  if (svn_path_is_url(path)
      || ! SVN_CLIENT__REVKIND_IS_LOCAL_TO_WC(revision->kind))
    {
      SVN_ERR(svn_client__repository_tree(tree, path, peg_revision, revision,
                                          ctx, result_pool));
    }
  else
    {
      const char *abspath;
      svn_error_t *err;
      svn_node_kind_t kind;

      /* Read the working node kind just to find out whether it is in fact
       * a versioned node. */
      SVN_ERR(svn_dirent_get_absolute(&abspath, path, scratch_pool));
      err = svn_wc_read_kind(&kind, ctx->wc_ctx, abspath,
                             TRUE /* show_hidden */, scratch_pool);
      if (! err)
        {
          if (revision->kind == svn_opt_revision_working)
            SVN_ERR(svn_client__wc_working_tree(tree, abspath, ctx,
                                                result_pool));
          else
            SVN_ERR(svn_client__wc_base_tree(tree, abspath, ctx, result_pool));
        }
      else if (err->apr_err == SVN_ERR_WC_PATH_NOT_FOUND
               || err->apr_err == SVN_ERR_WC_NOT_WORKING_COPY)
        {
          svn_error_clear(err);
          SVN_ERR(svn_client__disk_tree(tree, abspath, result_pool));
        }
      else
        {
          SVN_ERR(err);
        }
    }

  return SVN_NO_ERROR;
}

