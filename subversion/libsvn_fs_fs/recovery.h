/* recovery.h : interface to the FSFS recovery functionality
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

#ifndef SVN_LIBSVN_FS__RECOVERY_H
#define SVN_LIBSVN_FS__RECOVERY_H

#include "fs.h"

/* Find the "largest / max" node IDs in FS with the given YOUNGEST revision.
   Return the result in the pre-allocated MAX_NODE_ID and MAX_COPY_ID data
   buffer, respectively.   Use POOL for allocations.  */
svn_error_t *
svn_fs_fs__find_max_ids(svn_fs_t *fs,
                        svn_revnum_t youngest,
                        char *max_node_id,
                        char *max_copy_id,
                        apr_pool_t *pool);

/* Recover the fsfs associated with filesystem FS.
   Use optional CANCEL_FUNC/CANCEL_BATON for cancellation support.
   Use POOL for temporary allocations. */
svn_error_t *svn_fs_fs__recover(svn_fs_t *fs,
                                svn_cancel_func_t cancel_func,
                                void *cancel_baton,
                                apr_pool_t *pool);

#endif
