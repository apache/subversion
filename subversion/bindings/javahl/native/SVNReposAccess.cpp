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
 * @file SVNReposAccess.cpp
 * @brief Implementation of the class SVNReposAccess
 */

#include "SVNReposAccess.h"
#include "JNIUtil.h"
#include "JNICriticalSection.h"
#include "CreateJ.h"
#include "EnumMapper.h"
#include "Revision.h"

#include "svn_ra.h"
#include "svn_private_config.h"

SVNReposAccess::SVNReposAccess(const char *repos_url)
{
  m_sess_pool = svn_pool_create(JNIUtil::getPool());

  svn_ra_callbacks2_t *cbtable =
            (svn_ra_callbacks2_t *) apr_pcalloc(m_sess_pool, sizeof(*cbtable));

  SVN_JNI_ERR(svn_ra_open4(&m_ra_session, NULL, repos_url,
                           NULL, cbtable, NULL, NULL,
                           m_sess_pool), );
}

SVNReposAccess::~SVNReposAccess()
{
  // This will close the ra session
  svn_pool_destroy(m_sess_pool);
}

SVNReposAccess *SVNReposAccess::getCppObject(jobject jthis)
{
  static jfieldID fid = 0;
  jlong cppAddr = SVNBase::findCppAddrForJObject(jthis, &fid,
                                                 JAVA_PACKAGE"/SVNReposAccess");
  return (cppAddr == 0 ? NULL : reinterpret_cast<SVNReposAccess *>(cppAddr));
}

void SVNReposAccess::dispose()
{
  static jfieldID fid = 0;
  SVNBase::dispose(&fid, JAVA_PACKAGE"/SVNReposAccess");
}

svn_revnum_t
SVNReposAccess::getDatedRev(apr_time_t tm)
{
  SVN::Pool requestPool;
  svn_revnum_t rev;

  SVN_JNI_ERR(svn_ra_get_dated_revision(m_ra_session, &rev, tm,
                                        requestPool.getPool()),
              SVN_INVALID_REVNUM);

  return rev;
}

jobject
SVNReposAccess::getLocks(const char *path, svn_depth_t depth)
{
  SVN::Pool requestPool;
  apr_hash_t *locks;

  SVN_JNI_ERR(svn_ra_get_locks2(m_ra_session, &locks, path, depth,
                                requestPool.getPool()),
              NULL);

  return CreateJ::LockMap(locks, requestPool.getPool());
}

jobject
SVNReposAccess::checkPath(const char *path, Revision &revision)
{
  SVN::Pool requestPool;
  svn_node_kind_t kind;

  SVN_JNI_ERR(svn_ra_check_path(m_ra_session, path,
                                revision.revision()->value.number,
                                &kind, requestPool.getPool()),
              NULL);

  return EnumMapper::mapNodeKind(kind);
}
