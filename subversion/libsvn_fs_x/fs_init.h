/*
* libsvn_fs_x/fs_init.h:  Exported function of libsvn_fs_x
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

#ifndef SVN_LIBSVN_FS_X_INIT
#define SVN_LIBSVN_FS_X_INIT

#include "../libsvn_fs/fs-loader.h"

svn_error_t *svn_fs_x__init(const svn_version_t *loader_version,
                            fs_library_vtable_t **vtable,
                            apr_pool_t *common_pool);

#endif

