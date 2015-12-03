/* git-history.c --- svn history, delivered from git
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
#include "svn_fs.h"

#include "fs_git.h"
#include "../libsvn_fs/fs-loader.h"

static svn_fs_history_t *
history_make(const history_vtable_t *vtable,
             void *fsap_data,
             apr_pool_t *result_pool)
{
  svn_fs_history_t *h = apr_palloc(result_pool, sizeof(*h));
  h->vtable = vtable;
  h->fsap_data = fsap_data;

  return h;
}

/* ------------------------------------------------------- */
typedef struct fs_git_simple_history_t
{
  const char *next_path;
  svn_revnum_t rev;
  svn_revnum_t last_rev;
  svn_boolean_t initial_item;
} fs_git_simple_history_t;

static svn_error_t *
fs_git_simple_history_prev(svn_fs_history_t **prev_history_p,
                           svn_fs_history_t *history,
                           svn_boolean_t cross_copies,
                           apr_pool_t *result_pool,
                           apr_pool_t *scratch_pool)
{
  fs_git_simple_history_t *sht = history->fsap_data;

  if (!sht->initial_item
      && sht->rev == sht->last_rev)
    {
      *prev_history_p = NULL;
      return SVN_NO_ERROR;
    }

  sht = apr_pmemdup(result_pool, sht, sizeof(*sht));
  sht->next_path = apr_pstrdup(result_pool, sht->next_path);

  if (sht->initial_item)
    sht->initial_item = FALSE;
  else
    {
      sht->rev--;
    }

  *prev_history_p = history_make(history->vtable,
                                 sht, result_pool);
  return SVN_NO_ERROR;
}

static svn_error_t *
fs_git_simple_history_location(const char **path,
                               svn_revnum_t *revision,
                               svn_fs_history_t *history,
                               apr_pool_t *pool)
{
  fs_git_simple_history_t *sht = history->fsap_data;

  *path = apr_pstrdup(pool, sht->next_path);
  *revision = sht->rev;
  return SVN_NO_ERROR;
}

static const history_vtable_t fs_git_simple_history_vtable =
{
  fs_git_simple_history_prev,
  fs_git_simple_history_location
};

svn_error_t *
svn_fs_git__make_history_simple(svn_fs_history_t **history_p,
                                svn_fs_root_t *root,
                                svn_revnum_t rev_start,
                                svn_revnum_t rev_end,
                                const char *path,
                                apr_pool_t *result_pool,
                                apr_pool_t *scratch_pool)
{
  fs_git_simple_history_t *sht = apr_palloc(result_pool, sizeof(*sht));
  sht->next_path = apr_pstrdup(result_pool, path ? path : "/"); 
  sht->rev = rev_start;
  sht->last_rev = rev_end;
  sht->initial_item = TRUE;

  *history_p = history_make(&fs_git_simple_history_vtable,
                              sht, result_pool);
  return SVN_NO_ERROR;
}

/* ------------------------------------------------------- */
svn_error_t *
svn_fs_git__make_history_commit(svn_fs_history_t **history_p,
                                svn_fs_root_t *root,
                                const git_commit *commit,
                                apr_pool_t *result_pool,
                                apr_pool_t *scratch_pool)
{
  return svn_error_create(APR_ENOTIMPL, NULL, NULL);
}

/* ------------------------------------------------------- */
svn_error_t *
svn_fs_git__make_history_node(svn_fs_history_t **history_p,
                              svn_fs_root_t *root,
                              const git_commit *commit,
                              const char *relpath,
                              apr_pool_t *result_pool,
                              apr_pool_t *scratch_pool)
{
  return svn_error_create(APR_ENOTIMPL, NULL, NULL);
}
