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
 * Implementation of the native methods in the Java class SVNReposAccess
 */

#include "../include/org_apache_subversion_javahl_SVNReposAccess.h"
#include "JNIUtil.h"
#include "JNIStackElement.h"
#include "JNIStringHolder.h"
#include "EnumMapper.h"
#include "Revision.h"

#include "svn_version.h"
#include "svn_private_config.h"
#include "version.h"
#include <iostream>

#include "SVNReposAccess.h"

JNIEXPORT jlong JNICALL
Java_org_apache_subversion_javahl_SVNReposAccess_ctNative
(JNIEnv *env, jobject jthis, jstring jurl)
{
  JNIEntry(SVNReposAccess, ctNative);

  JNIStringHolder url(jurl);
  if (JNIUtil::isExceptionThrown())
    return -1;

  SVNReposAccess *obj = new SVNReposAccess(url);
  return obj->getCppAddr();
}

JNIEXPORT void JNICALL
Java_org_apache_subversion_javahl_SVNReposAccess_dispose
(JNIEnv *env, jobject jthis)
{
  JNIEntry(SVNReposAccess, dispose);
  SVNReposAccess *ra = SVNReposAccess::getCppObject(jthis);
  if (ra == NULL)
    {
      JNIUtil::throwError(_("bad C++ this"));
      return;
    }
  ra->dispose(jthis);
}

JNIEXPORT void JNICALL
Java_org_apache_subversion_javahl_SVNReposAccess_finalize
(JNIEnv *env, jobject jthis)
{
  JNIEntry(SVNReposAccess, finalize);
  SVNReposAccess *ra = SVNReposAccess::getCppObject(jthis);
  if (ra != NULL)
    ra->finalize();
}

JNIEXPORT jlong JNICALL
Java_org_apache_subversion_javahl_SVNReposAccess_getDatedRevision
(JNIEnv *env, jobject jthis, jobject jdate)
{
  JNIEntry(SVNReposAccess, getDatedRevision);
  SVNReposAccess *ra = SVNReposAccess::getCppObject(jthis);
  if (ra == NULL)
    {
      JNIUtil::throwError("bad C++ this");
      return -1;
    }

  apr_time_t date = JNIUtil::getDate(jdate);
  if (JNIUtil::isExceptionThrown())
    return -1;

  return ra->getDatedRev(date);
}

JNIEXPORT jobject JNICALL
Java_org_apache_subversion_javahl_SVNReposAccess_getLocks
(JNIEnv *env, jobject jthis, jstring jpath, jobject jdepth)
{
  JNIEntry(SVNReposAccess, getLocks);
  SVNReposAccess *ra = SVNReposAccess::getCppObject(jthis);
  if (ra == NULL)
    {
      JNIUtil::throwError("bad C++ this");
      return NULL;
    }

  JNIStringHolder path(jpath);
  if (JNIUtil::isExceptionThrown())
    return NULL;

  return ra->getLocks(path, EnumMapper::toDepth(jdepth));
}

JNIEXPORT jobject JNICALL
Java_org_apache_subversion_javahl_SVNReposAccess_checkPath
(JNIEnv *env, jobject jthis, jstring jpath, jobject jrevision)
{
  JNIEntry(SVNReposAccess, checkPath);
  SVNReposAccess *ra = SVNReposAccess::getCppObject(jthis);
  if (ra == NULL)
    {
      JNIUtil::throwError("bad C++ this");
      return NULL;
    }

  JNIStringHolder path(jpath);
  if (JNIUtil::isExceptionThrown())
    return NULL;

  Revision revision(jrevision);
  if (JNIUtil::isExceptionThrown())
    return NULL;

  return ra->checkPath(path, revision);
}
