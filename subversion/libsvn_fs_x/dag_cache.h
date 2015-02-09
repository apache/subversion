/* dag_cache.h : Interface to the DAG walker and node cache.
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

#ifndef SVN_LIBSVN_FS_X_DAG_CACHE_H
#define SVN_LIBSVN_FS_X_DAG_CACHE_H

#include "dag.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* In RESULT_POOL, create an instance of a DAG node cache. */
svn_fs_x__dag_cache_t*
svn_fs_x__create_dag_cache(apr_pool_t *result_pool);

/* Open the node identified by PATH in ROOT.  Set DAG_NODE_P to the
   node we find, allocated in POOL.  Return the error
   SVN_ERR_FS_NOT_FOUND if this node doesn't exist.
 */
static svn_error_t *
svn_fs_x__get_dag_node(dag_node_t **dag_node_p,
                       svn_fs_root_t *root,
                       const char *path,
                       apr_pool_t *pool);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_LIBSVN_FS_X_DAG_CACHE_H */
