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
 * @file deprecated.cpp
 * @brief: Implementation of methods that intentionally use deprecated
 *         Subversion APIs.
 */

/* We define this here to remove any further warnings about the usage of
   deprecated functions in this file. */
#define SVN_DEPRECATED

#include "SVNClient.h"
#include "JNIUtil.h"
#include "Path.h"
#include "Revision.h"

#include "svn_client.h"

void SVNClient::mergeReintegrate(const char *path, Revision &pegRevision,
                                 const char *localPath, bool dryRun)
{
    SVN::Pool subPool(pool);
    SVN_JNI_NULL_PTR_EX(path, "path", );
    SVN_JNI_NULL_PTR_EX(localPath, "localPath", );
    Path intLocalPath(localPath, subPool);
    SVN_JNI_ERR(intLocalPath.error_occurred(), );

    Path srcPath(path, subPool);
    SVN_JNI_ERR(srcPath.error_occurred(), );

    svn_client_ctx_t *ctx = context.getContext(NULL, subPool);
    if (ctx == NULL)
        return;

    SVN_JNI_ERR(svn_client_merge_reintegrate(srcPath.c_str(),
                                             pegRevision.revision(),
                                             intLocalPath.c_str(),
                                             dryRun, NULL, ctx,
                                             subPool.getPool()), );
}
