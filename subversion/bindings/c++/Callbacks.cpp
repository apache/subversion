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
 */

#include "Pool.h"
#include "Core.h"
#include "Types.h"
#include "Common.h"
#include "Callback.h"

#include "svn_error.h"
#include "svn_pools.h"

#include <iostream>

namespace SVN {

namespace Callback {

svn_error_t *
Commit::callback(const svn_commit_info_t *commit_info,
                 void *baton, apr_pool_t *pool)
{
  if (baton)
    try
      {
        reinterpret_cast<Commit *>(baton)->sendInfo(CommitInfo(commit_info));
      }
    catch (Exception ex)
      {
        return ex.c_err();
      }

  return SVN_NO_ERROR;
}

}
}
