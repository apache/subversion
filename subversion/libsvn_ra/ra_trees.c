/*
 * ra_trees.c: generic tree implementation of an in-repository subtree
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
#include "svn_tree.h"
#include "svn_ra.h"

#include "private/svn_tree_impl.h"
#include "private/svn_ra_private.h"

#include "svn_private_config.h"


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

/* ---------------------------------------------------------------------- */

/* */
typedef struct ra_tree_baton_t
{
  svn_ra_session_t *ra_session;
  svn_revnum_t revnum;
} ra_tree_baton_t;

/* */
typedef struct ra_tree_node_baton_t
{
  svn_tree_t *tree;
  ra_tree_baton_t *tb;
  const char *relpath;
  svn_dirent_t *dirent;  /* null until fetched/known */
  apr_hash_t *children;  /* null until fetched/known */
  apr_hash_t *props;  /* null until fetched/known */
  apr_pool_t *pool;
} ra_tree_node_baton_t;

/* Forward declaration */
static svn_tree_node_t *
ra_tree_node_create(svn_tree_t *tree,
                    const char *relpath,
                    apr_pool_t *result_pool);

/* */
static svn_error_t *
ra_tree_get_node_by_relpath(svn_tree_node_t **node,
                            svn_tree_t *tree,
                            const char *relpath,
                            apr_pool_t *result_pool,
                            apr_pool_t *scratch_pool)
{
  *node = ra_tree_node_create(tree, apr_pstrdup(result_pool, relpath),
                              result_pool);
  return SVN_NO_ERROR;
}

/* */
static svn_error_t *
fetch_dirent(svn_tree_node_t *node,
             apr_pool_t *scratch_pool)
{
  ra_tree_node_baton_t *nb = node->priv;

  if (nb->dirent == NULL)
    {
      SVN_ERR(ra_unauthz_err(svn_ra_stat(nb->tb->ra_session, nb->relpath,
                                         nb->tb->revnum, &nb->dirent,
                                         nb->pool)));
    }

  return SVN_NO_ERROR;
}

/* */
static svn_error_t *
ra_treen_get_relpath(svn_tree_node_t *node,
                const char **relpath_p,
                apr_pool_t *result_pool,
                apr_pool_t *scratch_pool)
{
  ra_tree_node_baton_t *nb = node->priv;

  *relpath_p = nb->relpath;
  return SVN_NO_ERROR;
}

/* */
static svn_error_t *
ra_treen_get_kind(svn_tree_node_t *node,
                  svn_node_kind_t *kind,
                  apr_pool_t *scratch_pool)
{
  ra_tree_node_baton_t *nb = node->priv;

  if (kind)
    {
      SVN_ERR(fetch_dirent(node, scratch_pool));
      *kind = nb->dirent->kind;
    }
  return SVN_NO_ERROR;
}

/* */
static svn_error_t *
ra_treen_read_file(svn_tree_node_t *node,
                   svn_stream_t **stream,
                   apr_hash_t **props,
                   apr_pool_t *result_pool,
                   apr_pool_t *scratch_pool)
{
  ra_tree_node_baton_t *nb = node->priv;

  if (stream)
    {
      /* Spool the content into a temp file, rewind, and return that stream
       * for the caller to read. */
      SVN_ERR(svn_stream_open_unique(stream, NULL, NULL,
                                     svn_io_file_del_on_close,
                                     result_pool, scratch_pool));
      SVN_ERR(ra_unauthz_err(svn_ra_get_file(nb->tb->ra_session, nb->relpath,
                                             nb->tb->revnum, *stream,
                                             NULL,
                                             nb->props ? NULL : &nb->props,
                                             nb->pool)));
      SVN_ERR(svn_stream_reset(*stream));
    }
  else if (props)
    {
      /* Just get the properties. */
      if (! nb->props)
        {
          SVN_ERR(ra_unauthz_err(svn_ra_get_file(nb->tb->ra_session, nb->relpath,
                                                 nb->tb->revnum, NULL,
                                                 NULL, &nb->props, nb->pool)));
        }
    }
  if (props)
    *props = nb->props;
  return SVN_NO_ERROR;
}

/* */
static svn_error_t *
ra_treen_read_dir(svn_tree_node_t *node,
                  apr_hash_t **children_p,
                  apr_hash_t **props,
                  apr_pool_t *result_pool,
                  apr_pool_t *scratch_pool)
{
  ra_tree_node_baton_t *nb = node->priv;

  if ((children_p && ! nb->children)
      || (props && ! nb->props))
    {
      apr_hash_t *dirents = NULL;

      SVN_ERR(ra_unauthz_err(svn_ra_get_dir2(nb->tb->ra_session,
                                             nb->children ? NULL : &dirents,
                                             NULL, &nb->props,
                                             nb->relpath, nb->tb->revnum,
                                             SVN_DIRENT_ALL,
                                             nb->pool)));

      /* Convert RA dirents to tree children */
      if (! nb->children)
        {
          apr_hash_index_t *hi;

          nb->children = apr_hash_make(nb->pool);
          for (hi = apr_hash_first(scratch_pool, dirents); hi;
               hi = apr_hash_next(hi))
            {
              const char *name = apr_hash_this_key(hi);
              svn_dirent_t *dirent = apr_hash_this_val(hi);
              const char *relpath = svn_relpath_join(nb->relpath, name, nb->pool);
              svn_tree_node_t *child;
              ra_tree_node_baton_t *cb;

              child = ra_tree_node_create(nb->tree, relpath, nb->pool);
              cb = child->priv;
              cb->dirent = dirent;
              apr_hash_set(nb->children, name, APR_HASH_KEY_STRING, child);
            }
        }
    }
  if (children_p)
    *children_p = nb->children;
  if (props)
    *props = nb->props;

  return SVN_NO_ERROR;
}

/* */
static svn_error_t *
ra_treen_get_dirent(svn_tree_node_t *node,
                    svn_dirent_t **dirent,
                    apr_pool_t *result_pool,
                    apr_pool_t *scratch_pool)
{
  ra_tree_node_baton_t *nb = node->priv;

  if (dirent)
    {
      SVN_ERR(fetch_dirent(node, scratch_pool));
      *dirent = nb->dirent;
    }
  return SVN_NO_ERROR;
}

/* */
static const svn_tree__vtable_t ra_tree_vtable =
{
  ra_tree_get_node_by_relpath
};

/* */
static const svn_tree_node__vtable_t ra_tree_node_vtable =
{
  ra_treen_get_relpath,
  ra_treen_get_kind,
  ra_treen_read_file,
  ra_treen_read_dir,
  ra_treen_get_dirent
};

/* Return a new node in RESULT_POOL.
 * RELPATH must be already allocated in RESULT_POOL.
 */
static svn_tree_node_t *
ra_tree_node_create(svn_tree_t *tree,
                    const char *relpath,
                    apr_pool_t *result_pool)
{
  ra_tree_node_baton_t *nb = apr_palloc(result_pool, sizeof(*nb));

  nb->tree = tree;
  nb->tb = tree->priv;
  nb->relpath = relpath;
  nb->dirent = NULL;
  nb->children = NULL;
  nb->props = NULL;
  nb->pool = result_pool;
  return svn_tree_node_create(&ra_tree_node_vtable, nb, result_pool);
}

svn_error_t *
svn_ra__open_tree(svn_tree_t **tree_p,
                  svn_ra_session_t *ra_session,
                  svn_revnum_t revnum,
                  apr_pool_t *result_pool)
{
  ra_tree_baton_t *tb = apr_palloc(result_pool, sizeof(*tb));

  if (! SVN_IS_VALID_REVNUM(revnum))
    SVN_ERR(svn_ra_get_latest_revnum(ra_session, &revnum, result_pool));

  tb->ra_session = ra_session;
  tb->revnum = revnum;

  *tree_p = svn_tree__create(&ra_tree_vtable, tb, result_pool);
  return SVN_NO_ERROR;
}
