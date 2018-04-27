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
  JNIEntry(RemoteSession, nativeDispose);
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
  JNIEntry(RemoteSession, reparent);
  RemoteSession *ras = RemoteSession::getCppObject(jthis);
  CPPADDR_NULL_PTR(ras, );

  ras->reparent(jurl);
}

JNIEXPORT jstring JNICALL
Java_org_apache_subversion_javahl_remote_RemoteSession_getSessionRelativePath(
    JNIEnv *env, jobject jthis, jstring jurl)
{
  JNIEntry(RemoteSession, getSessionRelativePath);
  RemoteSession *ras = RemoteSession::getCppObject(jthis);
  CPPADDR_NULL_PTR(ras, NULL);

  return ras->getSessionRelativePath(jurl);
}

JNIEXPORT jstring JNICALL
Java_org_apache_subversion_javahl_remote_RemoteSession_getReposRelativePath(
    JNIEnv *env, jobject jthis, jstring jurl)
{
  JNIEntry(RemoteSession, getReposRelativePath);
  RemoteSession *ras = RemoteSession::getCppObject(jthis);
  CPPADDR_NULL_PTR(ras, NULL);

  return ras->getReposRelativePath(jurl);
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
  JNIEntry(RemoteSession, getReposUUID);
  RemoteSession *ras = RemoteSession::getCppObject(jthis);
  CPPADDR_NULL_PTR(ras, NULL);

  return ras->getReposUUID();
}

JNIEXPORT jstring JNICALL
Java_org_apache_subversion_javahl_remote_RemoteSession_getReposRootUrl(
    JNIEnv *env, jobject jthis)
{
  JNIEntry(RemoteSession, getReposRootUrl);
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
  JNIEntry(RemoteSession, getRevisionByTimestamp);
  RemoteSession *ras = RemoteSession::getCppObject(jthis);
  CPPADDR_NULL_PTR(ras, SVN_INVALID_REVNUM);

  return ras->getRevisionByTimestamp(timestamp);
}

JNIEXPORT void JNICALL
Java_org_apache_subversion_javahl_remote_RemoteSession_nativeChangeRevisionProperty(
    JNIEnv *env, jobject jthis, jlong jrevision, jstring jname,
    jbyteArray jold_value, jbyteArray jvalue)
{
  JNIEntry(RemoteSession, nativeChangeRevisionProperty);
  RemoteSession *ras = RemoteSession::getCppObject(jthis);
  CPPADDR_NULL_PTR(ras, );

  return ras->changeRevisionProperty(jrevision, jname, jold_value, jvalue);
}

JNIEXPORT jobject JNICALL
Java_org_apache_subversion_javahl_remote_RemoteSession_getRevisionProperties(
    JNIEnv *env, jobject jthis, jlong jrevision)
{
  JNIEntry(SVNReposAccess, getRevisionProperties);
  RemoteSession *ras = RemoteSession::getCppObject(jthis);
  CPPADDR_NULL_PTR(ras, NULL);

  return ras->getRevisionProperties(jrevision);
}

JNIEXPORT jbyteArray JNICALL
Java_org_apache_subversion_javahl_remote_RemoteSession_getRevisionProperty(
    JNIEnv *env, jobject jthis, jlong jrevision, jstring jname)
{
  JNIEntry(SVNReposAccess, getRevisionProperty);
  RemoteSession *ras = RemoteSession::getCppObject(jthis);
  CPPADDR_NULL_PTR(ras, NULL);

  return ras->getRevisionProperty(jrevision, jname);
}

JNIEXPORT jlong JNICALL
Java_org_apache_subversion_javahl_remote_RemoteSession_nativeGetFile(
    JNIEnv *env, jobject jthis, jlong jrevision, jstring jpath,
    jobject jcontents, jobject jproperties)
{
  JNIEntry(SVNReposAccess, nativeGetFile);
  RemoteSession *ras = RemoteSession::getCppObject(jthis);
  CPPADDR_NULL_PTR(ras, SVN_INVALID_REVNUM);

  return ras->getFile(jrevision, jpath, jcontents, jproperties);
}

JNIEXPORT jlong JNICALL
Java_org_apache_subversion_javahl_remote_RemoteSession_nativeGetDirectory(
    JNIEnv *env, jobject jthis, jlong jrevision, jstring jpath,
    jint jdirent_fields, jobject jdirents, jobject jproperties)
{
  JNIEntry(SVNReposAccess, nativeGetDirectory);
  RemoteSession *ras = RemoteSession::getCppObject(jthis);
  CPPADDR_NULL_PTR(ras, SVN_INVALID_REVNUM);

  return ras->getDirectory(jrevision, jpath,
                           jdirent_fields, jdirents, jproperties);
}

JNIEXPORT jobject JNICALL
Java_org_apache_subversion_javahl_remote_RemoteSession_getMergeinfo(
    JNIEnv *env, jobject jthis, jobject jpaths, jlong jrevision,
    jobject jinherit, jboolean jinclude_descendants)
{
  JNIEntry(SVNReposAccess, getMergeinfo);
  RemoteSession *ras = RemoteSession::getCppObject(jthis);
  CPPADDR_NULL_PTR(ras, NULL);

  return ras->getMergeinfo(jpaths, jrevision, jinherit, jinclude_descendants);
}

// TODO: update
// TODO: switch

JNIEXPORT void JNICALL
Java_org_apache_subversion_javahl_remote_RemoteSession_nativeStatus(
    JNIEnv *env, jobject jthis, jstring jstatus_target, jlong jrevision,
    jobject jdepth, jobject jstatus_editor, jobject jreporter)
{
  JNIEntry(SVNReposAccess, nativeStatus);
  RemoteSession *ras = RemoteSession::getCppObject(jthis);
  CPPADDR_NULL_PTR(ras,);

  ras->status(jthis, jstatus_target, jrevision, jdepth,
              jstatus_editor, jreporter);
}

// TODO: diff

JNIEXPORT void JNICALL
Java_org_apache_subversion_javahl_remote_RemoteSession_getLog(
    JNIEnv *env, jobject jthis, jobject jpaths,
    jlong jstartrev, jlong jendrev, jint jlimit,
    jboolean jstrict_node_history, jboolean jdiscover_changed_paths,
    jboolean jinclude_merged_revisions,
    jobject jrevprops, jobject jlog_callback)
{
  JNIEntry(SVNReposAccess, getLog);
  RemoteSession *ras = RemoteSession::getCppObject(jthis);
  CPPADDR_NULL_PTR(ras,);

  ras->getLog(jpaths, jstartrev, jendrev, jlimit,
              jstrict_node_history, jdiscover_changed_paths,
              jinclude_merged_revisions,
              jrevprops, jlog_callback);
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
Java_org_apache_subversion_javahl_remote_RemoteSession_stat(
    JNIEnv *env, jobject jthis, jstring jpath, jlong jrevision)
{
  JNIEntry(SVNReposAccess, stat);
  RemoteSession *ras = RemoteSession::getCppObject(jthis);
  CPPADDR_NULL_PTR(ras, NULL);

  return ras->stat(jpath, jrevision);
}

JNIEXPORT jobject JNICALL
Java_org_apache_subversion_javahl_remote_RemoteSession_getLocations(
    JNIEnv *env, jobject jthis, jstring jpath, jlong jpeg_revision,
    jobject jlocation_revisions)
{
  JNIEntry(SVNReposAccess, getLocations);
  RemoteSession *ras = RemoteSession::getCppObject(jthis);
  CPPADDR_NULL_PTR(ras, NULL);

  return ras->getLocations(jpath, jpeg_revision, jlocation_revisions);
}

JNIEXPORT void JNICALL
Java_org_apache_subversion_javahl_remote_RemoteSession_getLocationSegments(
    JNIEnv *env, jobject jthis, jstring jpath, jlong jpeg_revision,
    jlong jstart_revision, jlong jend_revision, jobject jcallback)
{
  JNIEntry(SVNReposAccess, getLocationSegments);
  RemoteSession *ras = RemoteSession::getCppObject(jthis);
  CPPADDR_NULL_PTR(ras, );

  if (jcallback == NULL)
    JNIUtil::throwNullPointerException("handler");
  ras->getLocationSegments(jpath, jpeg_revision,
                           jstart_revision, jend_revision,
                           jcallback);
}

JNIEXPORT void JNICALL
Java_org_apache_subversion_javahl_remote_RemoteSession_getFileRevisions(
    JNIEnv *env, jobject jthis, jstring jpath,
    jlong jstart_revision, jlong jend_revision,
    jboolean jinclude_merged_revisions, jobject jcallback)
{
  JNIEntry(SVNReposAccess, getFileRevisions);
  RemoteSession *ras = RemoteSession::getCppObject(jthis);
  CPPADDR_NULL_PTR(ras, );

  if (jcallback == NULL)
    JNIUtil::throwNullPointerException("handler");
  ras->getFileRevisions(jpath, jstart_revision, jend_revision,
                        jinclude_merged_revisions, jcallback);
}

// TODO: lock
// TODO: unlock
// TODO: getLock

JNIEXPORT jobject JNICALL
Java_org_apache_subversion_javahl_remote_RemoteSession_getLocks(
    JNIEnv *env, jobject jthis, jstring jpath, jobject jdepth)
{
  JNIEntry(RemoteSession, getLocks);
  RemoteSession *ras = RemoteSession::getCppObject(jthis);
  CPPADDR_NULL_PTR(ras, NULL);

  return ras->getLocks(jpath, jdepth);
}

// TODO: replayRange
// TODO: replay
// TODO: getDeletedRevision
// TODO: getInheritedProperties

JNIEXPORT jboolean JNICALL
Java_org_apache_subversion_javahl_remote_RemoteSession_nativeHasCapability(
    JNIEnv *env, jobject jthis, jstring jcapability)
{
  JNIEntry(RemoteSession, nativeHasCapability);
  RemoteSession *ras = RemoteSession::getCppObject(jthis);
  CPPADDR_NULL_PTR(ras, false);

  return ras->hasCapability(jcapability);
}
