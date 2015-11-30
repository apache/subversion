/* util.c --- git filesystem utilities
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

#include "svn_fs.h"
#include "svn_version.h"
#include "svn_pools.h"

#include "svn_private_config.h"
#include "fs_git.h"

svn_error_t *
svn_fs_git__wrap_git_error(int err)
{
  git_error git_err;

  if (giterr_detach(&git_err) == -1)
    SVN_ERR_MALFUNCTION();

  /* ### TODO: map error code */
  return svn_error_createf(SVN_ERR_FS_GIT_LIBGIT2_ERROR, NULL,
                           _("git: %s"), git_err.message);
}

