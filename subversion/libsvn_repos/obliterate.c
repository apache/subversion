/*
 * obliterate.c: permanently delete history from the repository
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


#include "svn_error.h"
#include "svn_error_codes.h"
#include "svn_fs.h"
#include "svn_repos.h"
#include "repos.h"
#include "private/svn_repos_private.h"
#include "private/svn_fs_private.h"
#include "svn_private_config.h"



svn_error_t *
svn_repos__obliterate_path_rev(svn_repos_t *repos,
                               svn_revnum_t revision,
                               const char *path,
                               apr_pool_t *pool)
{
  svn_fs_t *fs = svn_repos_fs(repos);

  /* SVN_ERR(svn_repos_pre_obliterate_hook()); */

  SVN_ERR(svn_fs_obliterate(fs, path, revision, pool));

  /* SVN_ERR(svn_repos_post_obliterate_hook()); */

  return SVN_NO_ERROR;
}


