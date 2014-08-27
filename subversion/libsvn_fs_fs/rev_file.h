/* rev_file.h --- revision file and index access data structure
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

#ifndef SVN_LIBSVN_FS__REV_FILE_H
#define SVN_LIBSVN_FS__REV_FILE_H

#include "private/svn_fs_fs_private.h"

/* Open the proto-rev file of transaction TXN_ID in FS and return it in *FILE.
 * Allocate *FILE in RESULT_POOL use and SCRATCH_POOL for temporaries.. */
svn_error_t *
svn_fs_fs__open_proto_rev_file(svn_fs_fs__revision_file_t **file,
                               svn_fs_t *fs,
                               const svn_fs_fs__id_part_t *txn_id,
                               apr_pool_t* result_pool,
                               apr_pool_t *scratch_pool);

#endif
