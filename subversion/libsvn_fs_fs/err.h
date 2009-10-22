/*
 * err.h : interface to routines for returning Berkeley DB errors
 *
 * ====================================================================
 *    Licensed to the Subversion Corporation (SVN Corp.) under one
 *    or more contributor license agreements.  See the NOTICE file
 *    distributed with this work for additional information
 *    regarding copyright ownership.  The SVN Corp. licenses this file
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



#ifndef SVN_LIBSVN_FS_ERR_H
#define SVN_LIBSVN_FS_ERR_H

#include <apr_pools.h>

#include "svn_error.h"
#include "svn_fs.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */



/* Building common error objects.  */


/* SVN_ERR_FS_ID_NOT_FOUND: something in FS refers to node revision
   ID, but that node revision doesn't exist.  */
svn_error_t *svn_fs_fs__err_dangling_id(svn_fs_t *fs,
                                        const svn_fs_id_t *id);

/* SVN_ERR_FS_CORRUPT: the lockfile for PATH in FS is corrupt.  */
svn_error_t *svn_fs_fs__err_corrupt_lockfile(svn_fs_t *fs,
                                             const char *path);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_LIBSVN_FS_ERR_H */
