/* node.h : FS node API to DAG filesystem
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

#ifndef SVN_LIBSVN_FS_NODE_H
#define SVN_LIBSVN_FS_NODE_H

#include "svn_fs.h"

#include "dag.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* Returns instance of svn_fs_node_t object allocated in RESULT_POOL
 * and based DAG_NODE. DAG_NODE must have lifetime longer or the
 * the same as RESULT_POOL.
 */
svn_fs_node_t *
svn_fs_fs__node_create(dag_node_t *dag_node,
                       apr_pool_t *result_pool);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_LIBSVN_FS_NODE_H */

