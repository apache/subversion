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
#include "svn_ra.h"
#include "svn_private_config.h"

SVNReposAccess::SVNReposAccess()
{
}

SVNReposAccess::~SVNReposAccess()
{
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
