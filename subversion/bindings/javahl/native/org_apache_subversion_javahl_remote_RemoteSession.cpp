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
 * @file org_apache_subversion_javahl_remote_RemoteSession.cpp
 * @brief Implementation of the native methods in the Java class RemoteSession
 */

#include "../include/org_apache_subversion_javahl_remote_RemoteSession.h"

#include "JNIStackElement.h"
#include "JNIUtil.h"
#include "Prompter.h"
#include "RemoteSession.h"
#include "Revision.h"
#include "EnumMapper.h"

#include "svn_private_config.h"

JNIEXPORT void JNICALL
Java_org_apache_subversion_javahl_remote_RemoteSession_finalize(JNIEnv *env, jobject jthis)
{
  JNIEntry(RemoteSession, finalize);
  RemoteSession *ras = RemoteSession::getCppObject(jthis);
  if (ras != NULL)
    ras->finalize();
}

JNIEXPORT void JNICALL
Java_org_apache_subversion_javahl_remote_RemoteSession_dispose(JNIEnv *env, jobject jthis)
{
  JNIEntry(RemoteSession, dispose);
  RemoteSession *ras = RemoteSession::getCppObject(jthis);
  if (ras != NULL)
    ras->dispose(jthis);
}

JNIEXPORT jlong JNICALL
Java_org_apache_subversion_javahl_remote_RemoteSession_getLatestRevision(JNIEnv *env,
                                                             jobject jthis)
{
  JNIEntry(RemoteSession, getLatestRevision);
  RemoteSession *ras = RemoteSession::getCppObject(jthis);
  CPPADDR_NULL_PTR(ras, SVN_INVALID_REVNUM);

  return ras->getLatestRevision();
}

JNIEXPORT jstring JNICALL
Java_org_apache_subversion_javahl_remote_RemoteSession_getUUID
(JNIEnv *env, jobject jthis)
{
  JNIEntry(RemoteSession, getUUID);
  RemoteSession *ras = RemoteSession::getCppObject(jthis);
  CPPADDR_NULL_PTR(ras, NULL);

  return ras->getUUID();
}

JNIEXPORT jstring JNICALL
Java_org_apache_subversion_javahl_remote_RemoteSession_getUrl
(JNIEnv *env, jobject jthis)
{
  JNIEntry(RemoteSession, getUrl);
  RemoteSession *ras = RemoteSession::getCppObject(jthis);
  CPPADDR_NULL_PTR(ras, NULL);

  return ras->getUrl();
}

JNIEXPORT jlong JNICALL
Java_org_apache_subversion_javahl_remote_RemoteSession_getDatedRevision
(JNIEnv *env, jobject jthis, jlong timestamp)
{
  JNIEntry(RemoteSession, getDatedRevision);
  RemoteSession *ras = RemoteSession::getCppObject(jthis);
  CPPADDR_NULL_PTR(ras, SVN_INVALID_REVNUM);

  return ras->getDatedRev(timestamp);
}

JNIEXPORT jobject JNICALL
Java_org_apache_subversion_javahl_remote_RemoteSession_getLocks
(JNIEnv *env, jobject jthis, jstring jpath, jobject jdepth)
{
  JNIEntry(RemoteSession, getLocks);
  RemoteSession *ras = RemoteSession::getCppObject(jthis);
  CPPADDR_NULL_PTR(ras, NULL);

  return ras->getLocks(jpath, jdepth);
}

JNIEXPORT jobject JNICALL
Java_org_apache_subversion_javahl_remote_RemoteSession_checkPath
(JNIEnv *env, jobject jthis, jstring jpath, jobject jrevision)
{
  JNIEntry(SVNReposAccess, checkPath);
  RemoteSession *ras = RemoteSession::getCppObject(jthis);
  CPPADDR_NULL_PTR(ras, NULL);

  return ras->checkPath(jpath, jrevision);
}
