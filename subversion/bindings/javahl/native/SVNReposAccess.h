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
 * @file SVNReposAccess.h
 * @brief Interface of the class SVNReposAccess
 */

#ifndef SVNREPOSACCESS_H
#define SVNREPOSACCESS_H

#include <jni.h>
#include "svn_ra.h"
#include "SVNBase.h"

class Revision;

class SVNReposAccess : public SVNBase
{
 public:
  svn_revnum_t getDatedRev(apr_time_t time);
  jobject getLocks(const char *path, svn_depth_t depth);
  jobject checkPath(const char *path, Revision &revision);

  SVNReposAccess(const char *repos_url);
  virtual ~SVNReposAccess();
  void dispose(jobject jthis);
  static SVNReposAccess *getCppObject(jobject jthis);
 private:
  apr_pool_t *m_sess_pool;
  svn_ra_session_t *m_ra_session;
};

#endif // SVNREPOSACCESS_H
