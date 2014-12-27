/* fs_id.h : FSX's implementation of svn_fs_id_t
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

#ifndef SVN_LIBSVN_FS_X_FS_ID_H
#define SVN_LIBSVN_FS_X_FS_ID_H

#include "id.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* Create a permanent ID based on NODE_ID and NODEREV_ID, allocated in
   POOL.  Return NULL, if the NODEREV_ID is "unused". */
svn_fs_id_t *svn_fs_x__id_create(const svn_fs_x__id_part_t *node_id,
                                 const svn_fs_x__id_part_t *noderev_id,
                                 apr_pool_t *pool);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_LIBSVN_FS_X_FS_ID_H */
