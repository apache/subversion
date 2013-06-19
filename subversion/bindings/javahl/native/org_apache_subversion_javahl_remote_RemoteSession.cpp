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
Java_org_apache_subversion_javahl_remote_RemoteSession_finalize(
    JNIEnv *env, jobject jthis)
{
  JNIEntry(RemoteSession, finalize);
  RemoteSession *ras = RemoteSession::getCppObject(jthis);
  if (ras != NULL)
    ras->finalize();
}

JNIEXPORT void JNICALL
Java_org_apache_subversion_javahl_remote_RemoteSession_nativeDispose(
    JNIEnv *env, jobject jthis)
{
  JNIEntry(RemoteSession, dispose);
  RemoteSession *ras = RemoteSession::getCppObject(jthis);
  if (ras != NULL)
    ras->dispose(jthis);
}

JNIEXPORT void JNICALL
Java_org_apache_subversion_javahl_remote_RemoteSession_cancelOperation(
    JNIEnv *env, jobject jthis)
{
  JNIEntry(RemoteSession, cancelOperation);
  RemoteSession *ras = RemoteSession::getCppObject(jthis);
  CPPADDR_NULL_PTR(ras, );

  ras->cancelOperation();
}

JNIEXPORT void JNICALL
Java_org_apache_subversion_javahl_remote_RemoteSession_reparent(
    JNIEnv *env, jobject jthis, jstring jurl)
{
  JNIEntry(RemoteSession, getSessionUrl);
  RemoteSession *ras = RemoteSession::getCppObject(jthis);
  CPPADDR_NULL_PTR(ras, );

  JNIStringHolder url(jurl);
  if (!JNIUtil::isJavaExceptionThrown())
    ras->reparent(url);
}

JNIEXPORT jstring JNICALL
Java_org_apache_subversion_javahl_remote_RemoteSession_getSessionRelativePath(
    JNIEnv *env, jobject jthis, jstring jurl)
{
  JNIEntry(RemoteSession, getSessionUrl);
  RemoteSession *ras = RemoteSession::getCppObject(jthis);
  CPPADDR_NULL_PTR(ras, NULL);

  JNIStringHolder url(jurl);
  if (JNIUtil::isJavaExceptionThrown())
    return NULL;
  return ras->getSessionRelativePath(url);
}

JNIEXPORT jstring JNICALL
Java_org_apache_subversion_javahl_remote_RemoteSession_getReposRelativePath(
    JNIEnv *env, jobject jthis, jstring jurl)
{
  JNIEntry(RemoteSession, getSessionUrl);
  RemoteSession *ras = RemoteSession::getCppObject(jthis);
  CPPADDR_NULL_PTR(ras, NULL);

  JNIStringHolder url(jurl);
  if (JNIUtil::isJavaExceptionThrown())
    return NULL;
  return ras->getReposRelativePath(url);
}

JNIEXPORT jstring JNICALL
Java_org_apache_subversion_javahl_remote_RemoteSession_getSessionUrl(
    JNIEnv *env, jobject jthis)
{
  JNIEntry(RemoteSession, getSessionUrl);
  RemoteSession *ras = RemoteSession::getCppObject(jthis);
  CPPADDR_NULL_PTR(ras, NULL);

  return ras->getSessionUrl();
}

JNIEXPORT jstring JNICALL
Java_org_apache_subversion_javahl_remote_RemoteSession_getReposUUID(
    JNIEnv *env, jobject jthis)
{
  JNIEntry(RemoteSession, geRepostUUID);
  RemoteSession *ras = RemoteSession::getCppObject(jthis);
  CPPADDR_NULL_PTR(ras, NULL);

  return ras->getReposUUID();
}

JNIEXPORT jstring JNICALL
Java_org_apache_subversion_javahl_remote_RemoteSession_getReposRootUrl(
    JNIEnv *env, jobject jthis)
{
  JNIEntry(RemoteSession, geRepostUUID);
  RemoteSession *ras = RemoteSession::getCppObject(jthis);
  CPPADDR_NULL_PTR(ras, NULL);

  return ras->getReposRootUrl();
}

JNIEXPORT jlong JNICALL
Java_org_apache_subversion_javahl_remote_RemoteSession_getLatestRevision(
    JNIEnv *env, jobject jthis)
{
  JNIEntry(RemoteSession, getLatestRevision);
  RemoteSession *ras = RemoteSession::getCppObject(jthis);
  CPPADDR_NULL_PTR(ras, SVN_INVALID_REVNUM);

  return ras->getLatestRevision();
}

JNIEXPORT jlong JNICALL
Java_org_apache_subversion_javahl_remote_RemoteSession_getRevisionByTimestamp(
    JNIEnv *env, jobject jthis, jlong timestamp)
{
  JNIEntry(RemoteSession, getDatedRevision);
  RemoteSession *ras = RemoteSession::getCppObject(jthis);
  CPPADDR_NULL_PTR(ras, SVN_INVALID_REVNUM);

  return ras->getRevisionByTimestamp(timestamp);
}

JNIEXPORT jobject JNICALL
Java_org_apache_subversion_javahl_remote_RemoteSession_checkPath(
    JNIEnv *env, jobject jthis, jstring jpath, jlong jrevision)
{
  JNIEntry(SVNReposAccess, checkPath);
  RemoteSession *ras = RemoteSession::getCppObject(jthis);
  CPPADDR_NULL_PTR(ras, NULL);

  return ras->checkPath(jpath, jrevision);
}

JNIEXPORT jobject JNICALL
Java_org_apache_subversion_javahl_remote_RemoteSession_getLocks(
    JNIEnv *env, jobject jthis, jstring jpath, jobject jdepth)
{
  JNIEntry(RemoteSession, getLocks);
  RemoteSession *ras = RemoteSession::getCppObject(jthis);
  CPPADDR_NULL_PTR(ras, NULL);

  return ras->getLocks(jpath, jdepth);
}

JNIEXPORT jboolean JNICALL
Java_org_apache_subversion_javahl_remote_RemoteSession_nativeHasCapability(
    JNIEnv *env, jobject jthis, jstring jcapability)
{
  JNIEntry(RemoteSession, nativeHasCapability);
  RemoteSession *ras = RemoteSession::getCppObject(jthis);
  CPPADDR_NULL_PTR(ras, false);

  return ras->hasCapability(jcapability);
}
