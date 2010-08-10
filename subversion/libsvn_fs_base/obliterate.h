/* obliterate.h : operations related to obliteration
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

#ifndef SVN_LIBSVN_FS_OBLITERATE_H
#define SVN_LIBSVN_FS_OBLITERATE_H

#include <apr_pools.h>

#include "svn_fs.h"
#include "trail.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/* Create a new representation that is a duplicate of the one keyed by KEY,
 * but make the duplicate refer to NEW_TXN_ID.
 * Set *NEW_KEY to the key of the new representation, allocated in
 * TRAIL->pool.
 * Work within TRAIL.
 */
svn_error_t *
svn_fs_base__rep_dup(const char **new_key,
                     const char *new_txn_id,
                     const char *key,
                     trail_t *trail,
                     apr_pool_t *scratch_pool);

/* If the node_rev identified by OLD_ID was not created in transaction
 * OLD_TXN_ID, then set *NEW_ID to a copy of OLD_ID, allocated in
 * TRAIL->pool, and return. Otherwise:
 *
 * Make a deep copy of node OLD_ID, with any references to OLD_TXN_ID
 * replaced by NEW_TXN_ID (### and more differences?) The new node-rev-id is
 * OLD_ID except with the txn-id field changed to NEW_TXN_ID.
 * Set *NEW_ID to the new node-rev-id, allocated in TRAIL->pool.
 * Work within TRAIL.
 */
svn_error_t *
svn_fs_base__node_rev_dup(const svn_fs_id_t **new_id,
                          const svn_fs_id_t *old_id,
                          const char *new_txn_id,
                          const char *old_txn_id,
                          trail_t *trail,
                          apr_pool_t *scratch_pool);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_LIBSVN_FS_OBLITERATE_H */
