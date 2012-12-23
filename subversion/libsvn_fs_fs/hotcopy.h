/* hotcopy.h : interface to the native filesystem layer
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

#ifndef SVN_LIBSVN_FS__HOTCOPY_H
#define SVN_LIBSVN_FS__HOTCOPY_H

#include "fs.h"

/* Copy the fsfs filesystem SRC_FS at SRC_PATH into a new copy DST_FS at
 * DST_PATH. If INCREMENTAL is TRUE, do not re-copy data which already
 * exists in DST_FS. Use POOL for temporary allocations. */
svn_error_t * svn_fs_fs__hotcopy(svn_fs_t *src_fs,
                                 svn_fs_t *dst_fs,
                                 const char *src_path,
                                 const char *dst_path,
                                 svn_boolean_t incremental,
                                 svn_cancel_func_t cancel_func,
                                 void *cancel_baton,
                                 apr_pool_t *pool);

#endif
