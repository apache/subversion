/*
 * repos_pool.c :  pool of svn_repos_t objects
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



#include "svn_error.h"
#include "svn_pools.h"

#include "private/svn_dep_compat.h"
#include "private/svn_object_pool.h"
#include "private/svn_repos_private.h"

/* Data structure simply adding a configuration to the basic object pool.
 */
struct svn_repos__repos_pool_t
{
  /* repository object storage */
  svn_object_pool__t *object_pool;

  /* FS configuration to be used with all repository instances */
  apr_hash_t *fs_config;
};

/* Return the path REPOS_ROOT as a memory buffer allocated in POOL.
 */
static svn_membuf_t *
construct_key(const char *repos_root,
              apr_pool_t *pool)
{
  apr_size_t len = strlen(repos_root);
  svn_membuf_t *result = apr_pcalloc(pool, sizeof(*result));

  svn_membuf__create(result, len, pool);
  memcpy(result->data, repos_root, len);
  result->size = len;

  return result;
}

/* API implementation */

svn_error_t *
svn_repos__repos_pool_create(svn_repos__repos_pool_t **repos_pool,
                             apr_hash_t *fs_config,
                             svn_boolean_t thread_safe,
                             apr_pool_t *pool)
{
  svn_object_pool__t *object_pool;
  apr_pool_t *root_pool;
  svn_repos__repos_pool_t *result;

  /* no getter nor setter is required but we also can't share repos
   * instances */
  SVN_ERR(svn_object_pool__create(&object_pool, NULL, NULL,
                                  4, APR_UINT32_MAX, FALSE, thread_safe,
                                  pool));

  root_pool = svn_object_pool__pool(object_pool);
  result = apr_pcalloc(root_pool, sizeof(*result));
  result->object_pool = object_pool;
  result->fs_config = fs_config ? apr_hash_copy(root_pool, fs_config) : NULL;

  *repos_pool = result;
  return SVN_NO_ERROR;
}

svn_error_t *
svn_repos__repos_pool_get(svn_repos_t **repos_p,
                          svn_repos__repos_pool_t *repos_pool,
                          const char *repos_root,
                          apr_pool_t *pool)
{
  svn_repos_t *repos;
  apr_pool_t *wrapper_pool;
  svn_membuf_t *key = construct_key(repos_root, pool);

  /* already in pool? */
  SVN_ERR(svn_object_pool__lookup((void **)repos_p, repos_pool->object_pool,
                                  key, NULL, pool));
  if (*repos_p)
    return SVN_NO_ERROR;

  /* open repos in its private pool */
  wrapper_pool
    = svn_pool_create(svn_object_pool__pool(repos_pool->object_pool));
  SVN_ERR(svn_repos_open2(&repos, repos_root, repos_pool->fs_config,
                          wrapper_pool));

  /* add */
  SVN_ERR(svn_object_pool__insert((void **)repos_p, repos_pool->object_pool,
                                  key, repos, NULL, wrapper_pool, pool));

  return SVN_NO_ERROR;
}
