/*
 * node_compat.c:  Compatibility shims implementation of svn_fs_node_t
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
#include "fs-loader.h"
#include "node_compat.h"

typedef struct compat_node_data_t
{
  svn_fs_t *fs;
  const char *path;
  svn_node_kind_t node_kind;
  const char *txn_name;
  svn_revnum_t rev;
} compat_node_data_t;

/* Sets *ROOT_P to temporary FS root object referenced by node with
 * node private data FND. */
static svn_error_t *
get_root(svn_fs_root_t **root_p,
         compat_node_data_t *fnd,
         apr_pool_t *pool)
{
  if (fnd->txn_name)
    {
      svn_fs_txn_t *txn;
      SVN_ERR(svn_fs_open_txn(&txn, fnd->fs, fnd->txn_name, pool));
      SVN_ERR(svn_fs_txn_root(root_p, txn, pool));
      return SVN_NO_ERROR;
    }
  else
    {
      SVN_ERR(svn_fs_revision_root(root_p, fnd->fs, fnd->rev, pool));
      return SVN_NO_ERROR;
    }
}

static svn_error_t *
compat_fs_node_kind(svn_node_kind_t *kind_p,
                    svn_fs_node_t *node,
                    apr_pool_t *scratch_pool)
{
  compat_node_data_t *fnd = node->fsap_data;
  *kind_p = fnd->node_kind;
  return SVN_NO_ERROR;
}

static svn_error_t *
compat_fs_node_has_props(svn_boolean_t *has_props,
                         svn_fs_node_t *node,
                         apr_pool_t *scratch_pool)
{
  compat_node_data_t *fnd = node->fsap_data;
  svn_fs_root_t *root;
  SVN_ERR(get_root(&root, fnd, scratch_pool));

  return svn_error_trace(root->vtable->node_has_props(has_props, root,
                                                      fnd->path,
                                                      scratch_pool));
}

static svn_error_t *
compat_fs_node_file_length(svn_filesize_t *length_p,
                           svn_fs_node_t *node,
                           apr_pool_t *pool)
{
  compat_node_data_t *fnd = node->fsap_data;
  svn_fs_root_t *root;

  SVN_ERR(get_root(&root, fnd, pool));

  return svn_error_trace(root->vtable->file_length(length_p, root,
                                                   fnd->path,
                                                   pool));
}

static const node_vtable_t compat_node_vtable = 
{
  compat_fs_node_kind,
  compat_fs_node_has_props,
  compat_fs_node_file_length
};

svn_fs_node_t *
svn_fs__create_node_shim(svn_fs_root_t *root,
                         const char *path,
                         svn_node_kind_t kind,
                         apr_pool_t *result_pool)
{
  compat_node_data_t *fnd = apr_palloc(result_pool,
      sizeof(*fnd));
  svn_fs_node_t *node = apr_palloc(result_pool, sizeof(*node));
  fnd->fs = root->fs;
  if (root->is_txn_root)
    {
      fnd->txn_name = apr_pstrdup(result_pool, root->txn);
      fnd->rev = SVN_INVALID_REVNUM;
    }
  else
    {
      fnd->txn_name = NULL;
      fnd->rev = root->rev;
    }

  fnd->path = apr_pstrdup(result_pool, path);
  fnd->node_kind = kind;

  node->vtable = &compat_node_vtable;
  node->fsap_data = fnd;

  return node;
}
