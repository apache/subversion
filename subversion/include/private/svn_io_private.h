/**
 * @copyright
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
 * @endcopyright
 *
 * @file svn_io_private.h
 * @brief Private IO API
 */

#ifndef SVN_IO_PRIVATE_H
#define SVN_IO_PRIVATE_H

#include <apr.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/** Set @a *executable TRUE if @a file_info is executable for the
 * user, FALSE otherwise.
 */
svn_error_t *
svn_io__is_finfo_executable(svn_boolean_t *executable,
                            apr_finfo_t *file_info,
                            apr_pool_t *pool);

/** Set @a *read_only TRUE if @a file_info is read-only for the user,
 * FALSE otherwise.
 */
svn_error_t *
svn_io__is_finfo_read_only(svn_boolean_t *read_only,
                           apr_finfo_t *file_info,
                           apr_pool_t *pool);


#ifdef __cplusplus
}
#endif /* __cplusplus */


#endif /* SVN_IO_PRIVATE_H */
