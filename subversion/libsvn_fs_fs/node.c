/* node.c : FS node API to DAG filesystem
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
#include "node.h"
#include "svn_hash.h"
#include "../libsvn_fs/fs-loader.h"

typedef struct fs_node_data_t
{
  dag_node_t *dag_node;
} fs_node_data_t;

static svn_error_t *
fs_node_kind(svn_node_kind_t *kind_p,
             svn_fs_node_t *node,
             apr_pool_t *scratch_pool)
{
  fs_node_data_t *fnd = node->fsap_data;
  *kind_p = svn_fs_fs__dag_node_kind(fnd->dag_node);
  return SVN_NO_ERROR;
}

static svn_error_t *
fs_node_has_props(svn_boolean_t *has_props,
                  svn_fs_node_t *node,
                  apr_pool_t *scratch_pool)
{
  fs_node_data_t *fnd = node->fsap_data;

  SVN_ERR(svn_fs_fs__dag_has_props(has_props, fnd->dag_node, scratch_pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
fs_node_file_length(svn_filesize_t *length_p,
                           svn_fs_node_t *node,
                           apr_pool_t *pool)
{
  fs_node_data_t *fnd = node->fsap_data;

  SVN_ERR(svn_fs_fs__dag_file_length(length_p, fnd->dag_node, pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
fs_node_dir_entries(apr_hash_t **entries_p,
                    svn_fs_node_t *node,
                    apr_pool_t *result_pool,
                    apr_pool_t *scratch_pool)
{
  fs_node_data_t *fnd = node->fsap_data;
  apr_array_header_t *entries;
  apr_hash_t *result;
  int i;

  SVN_ERR(svn_fs_fs__dag_dir_entries(&entries, fnd->dag_node, scratch_pool));

  result = apr_hash_make(result_pool);
  for (i = 0; i < entries->nelts; i++)
    {
      svn_fs_dirent_t *dirent_v1 = APR_ARRAY_IDX(entries, i, svn_fs_dirent_t *);
      svn_fs_dirent2_t *dirent_v2 = apr_pcalloc(result_pool,
                                                sizeof(*dirent_v2));
      dag_node_t *dag_node;

      /* TODO: Make partially bound dag_node or something like that. */
      SVN_ERR(svn_fs_fs__dag_get_node(&dag_node,
                                      svn_fs_fs__dag_get_fs(fnd->dag_node),
                                      dirent_v1->id, result_pool));
      dirent_v2->name = apr_pstrdup(result_pool, dirent_v1->name);
      dirent_v2->kind = dirent_v1->kind;
      dirent_v2->node = svn_fs_fs__node_create(dag_node, result_pool);
      svn_hash_sets(result, dirent_v2->name, dirent_v2);
    }

  *entries_p = result;
  return SVN_NO_ERROR;
}

static const node_vtable_t fs_node_vtable = 
{
  fs_node_kind,
  fs_node_has_props,
  fs_node_file_length,
  fs_node_dir_entries
};

svn_fs_node_t *
svn_fs_fs__node_create(dag_node_t *dag_node,
                       apr_pool_t *result_pool)
{
  fs_node_data_t *fnd = apr_palloc(result_pool, sizeof(*fnd));
  svn_fs_node_t *node = apr_palloc(result_pool, sizeof(*node));
  fnd->dag_node = dag_node;
  node->fs = svn_fs_fs__dag_get_fs(dag_node);
  node->vtable = &fs_node_vtable;
  node->fsap_data = fnd;

  return node;
}
