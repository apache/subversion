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
 * @file org_apache_subversion_javahl_remote_StateReporter.cpp
 * @brief Implementation of the native methods in the Java class StateReporter
 */

#include "../include/org_apache_subversion_javahl_remote_StateReporter.h"

#include <jni.h>
#include "JNIStackElement.h"
#include "JNIUtil.h"
#include "StateReporter.h"

#include "svn_private_config.h"

JNIEXPORT void JNICALL
Java_org_apache_subversion_javahl_remote_StateReporter_finalize(
    JNIEnv *env, jobject jthis)
{
  JNIEntry(StateReporter, finalize);
  StateReporter *reporter = StateReporter::getCppObject(jthis);
  if (reporter != NULL)
    reporter->finalize();
}

JNIEXPORT void JNICALL
Java_org_apache_subversion_javahl_remote_StateReporter_nativeDispose(
    JNIEnv* env, jobject jthis)
{
  JNIEntry(StateReporter, nativeDispose);
  StateReporter* reporter = StateReporter::getCppObject(jthis);
  CPPADDR_NULL_PTR(reporter,);
  reporter->dispose(jthis);
}

JNIEXPORT void JNICALL
Java_org_apache_subversion_javahl_remote_StateReporter_setPath(
    JNIEnv* env, jobject jthis,
    jstring jpath, jlong jrevision, jobject jdepth,
    jboolean jstart_empty, jstring jlock_token)
{
  JNIEntry(StateReporter, setPath);
  StateReporter* reporter = StateReporter::getCppObject(jthis);
  CPPADDR_NULL_PTR(reporter,);
  reporter->setPath(jpath, jrevision, jdepth, jstart_empty, jlock_token);
}

JNIEXPORT void JNICALL
Java_org_apache_subversion_javahl_remote_StateReporter_deletePath(
    JNIEnv* env, jobject jthis, jstring jpath)
{
  JNIEntry(StateReporter, deletePath);
  StateReporter* reporter = StateReporter::getCppObject(jthis);
  CPPADDR_NULL_PTR(reporter,);
  reporter->deletePath(jpath);
}

JNIEXPORT void JNICALL
Java_org_apache_subversion_javahl_remote_StateReporter_linkPath(
    JNIEnv* env, jobject jthis,
    jstring jurl, jstring jpath, jlong jrevision, jobject jdepth,
    jboolean jstart_empty, jstring jlock_token)
{
  JNIEntry(StateReporter, linkPath);
  StateReporter* reporter = StateReporter::getCppObject(jthis);
  CPPADDR_NULL_PTR(reporter,);
  reporter->linkPath(jurl, jpath, jrevision, jdepth, jstart_empty, jlock_token);
}

JNIEXPORT jlong JNICALL
Java_org_apache_subversion_javahl_remote_StateReporter_finishReport(
    JNIEnv* env, jobject jthis)
{
  JNIEntry(StateReporter, finishReport);
  StateReporter* reporter = StateReporter::getCppObject(jthis);
  CPPADDR_NULL_PTR(reporter, SVN_INVALID_REVNUM);
  return reporter->finishReport();
}

JNIEXPORT void JNICALL
Java_org_apache_subversion_javahl_remote_StateReporter_abortReport(
    JNIEnv* env, jobject jthis)
{
  JNIEntry(StateReporter, abortReport);
  StateReporter* reporter = StateReporter::getCppObject(jthis);
  CPPADDR_NULL_PTR(reporter,);
  reporter->abortReport();
}

JNIEXPORT jlong JNICALL
Java_org_apache_subversion_javahl_remote_StateReporter_nativeCreateInstance(
    JNIEnv* env, jclass clazz)
{
  jobject jthis = NULL;
  JNIEntry(StateReporter, nativeCreateInstance);
  return reinterpret_cast<jlong>(new StateReporter);
}
