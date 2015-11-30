/* fs_git.h : interface to Subversion filesystem, private to libsvn_fs_git
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

#ifndef SVN_LIBSVN_FS__FS_GIT_H
#define SVN_LIBSVN_FS__FS_GIT_H

typedef struct svn_fs_git_fs_t
{

  svn_error_t *(*svn_fs_open)(svn_fs_t **,
                              const char *,
                              apr_hash_t *,
                              apr_pool_t *,
                              apr_pool_t *);

} svn_fs_git_fs_t;

svn_error_t *
svn_fs_git__initialize_fs_struct(svn_fs_t *fs,
                                 apr_pool_t *scratch_pool);

svn_error_t *
svn_fs_git__create(svn_fs_t *fs,
                   const char *path,
                   apr_pool_t *scratch_pool);

svn_error_t *
svn_fs_git__open(svn_fs_t *fs,
                 const char *path,
                 apr_pool_t *scratch_pool);


#endif
