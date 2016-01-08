/*
 * node_compat.h:  Compatibility shims implementation of svn_fs_node_t
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
#include <svn_fs.h>

/* Creates svn_fs_node_t object of the node identified by ROOT + PATH
 * of type KIND.
 * Allocates result in RESULT_POOL.
 */
svn_fs_node_t *
svn_fs__create_node_shim(svn_fs_root_t *root,
                         const char *path,
                         svn_node_kind_t kind,
                         apr_pool_t *result_pool);
