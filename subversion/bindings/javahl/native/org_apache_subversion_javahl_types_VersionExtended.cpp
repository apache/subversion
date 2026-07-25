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
 * @file org_apache_subversion_javahl_types_VersionExtended.cpp
 * @brief Implementation of the native methods in the Java class VersionExtended.
 */

#include "../include/org_apache_subversion_javahl_types_VersionExtended.h"
#include "../include/org_apache_subversion_javahl_types_VersionExtended_LinkedLib.h"
#include "../include/org_apache_subversion_javahl_types_VersionExtended_LinkedLibIterator.h"
#include "../include/org_apache_subversion_javahl_types_VersionExtended_LoadedLib.h"
#include "../include/org_apache_subversion_javahl_types_VersionExtended_LoadedLibIterator.h"
#include "VersionExtended.h"
#include "JNIStackElement.h"
#include <string>

#include "svn_private_config.h"

// VersionExtended native methods

JNIEXPORT void JNICALL
Java_org_apache_subversion_javahl_types_VersionExtended_dispose(
    JNIEnv *env, jobject jthis)
{
  JNIEntry(VersionExtended, dispose);
  VersionExtended *const vx = VersionExtended::getCppObject(jthis);
  if (vx == NULL)
    {
      JNIUtil::throwError(_("bad C++ this"));
      return;
    }
  vx->dispose(jthis);
}

JNIEXPORT jstring JNICALL
Java_org_apache_subversion_javahl_types_VersionExtended_getBuildDate(
    JNIEnv *env, jobject jthis)
{
  JNIEntry(VersionExtended, getBuildDate);
  const VersionExtended *const vx = VersionExtended::getCppObject(jthis);
  if (vx)
    return env->NewStringUTF(vx->build_date());
  return 0;
}

JNIEXPORT jstring JNICALL
Java_org_apache_subversion_javahl_types_VersionExtended_getBuildTime(
    JNIEnv *env, jobject jthis)
{
  JNIEntry(VersionExtended, getBuildTime);
  const VersionExtended *const vx = VersionExtended::getCppObject(jthis);
  if (vx)
    return env->NewStringUTF(vx->build_time());
  return 0;
}

JNIEXPORT jstring JNICALL
Java_org_apache_subversion_javahl_types_VersionExtended_getBuildHost(
    JNIEnv *env, jobject jthis)
{
  JNIEntry(VersionExtended, getBuildHost);
  const VersionExtended *const vx = VersionExtended::getCppObject(jthis);
  if (vx)
    return env->NewStringUTF(vx->build_host());
  return 0;
}

JNIEXPORT jstring JNICALL
Java_org_apache_subversion_javahl_types_VersionExtended_getCopyright(
    JNIEnv *env, jobject jthis)
{
  JNIEntry(VersionExtended, getCopyright);
  const VersionExtended *const vx = VersionExtended::getCppObject(jthis);
  if (vx)
    return env->NewStringUTF(vx->copyright());
  return 0;
}

JNIEXPORT jstring JNICALL
Java_org_apache_subversion_javahl_types_VersionExtended_getRuntimeHost(
    JNIEnv *env, jobject jthis)
{
  JNIEntry(VersionExtended, getRuntimeHost);
  const VersionExtended *const vx = VersionExtended::getCppObject(jthis);
  if (vx)
    return env->NewStringUTF(vx->runtime_host());
  return 0;
}

JNIEXPORT jstring JNICALL
Java_org_apache_subversion_javahl_types_VersionExtended_getRuntimeOSName(
    JNIEnv *env, jobject jthis)
{
  JNIEntry(VersionExtended, getRuntimeOSName);
  const VersionExtended *const vx = VersionExtended::getCppObject(jthis);
  if (vx)
    return env->NewStringUTF(vx->runtime_osname());
  return 0;
}


// VersionExtended.LinkedLib native methods

namespace {
static const svn_version_ext_linked_lib_t *
getLinkedLib(JNIEnv *env, jobject jthis)
{
  static volatile jfieldID fid = 0;
  if (!fid)
    {
      fid = env->GetFieldID(env->GetObjectClass(jthis), "index", "I");
      if (JNIUtil::isJavaExceptionThrown())
        return NULL;
    }

  const int index = env->GetIntField(jthis, fid);
  if (JNIUtil::isJavaExceptionThrown())
    return NULL;

  const VersionExtended *const vx =
    VersionExtended::getCppObjectFromLinkedLib(jthis);
  if (vx)
    return vx->get_linked_lib(index);
  return NULL;
}
} // anonymous namespace

JNIEXPORT jstring JNICALL
Java_org_apache_subversion_javahl_types_VersionExtended_00024LinkedLib_getName(
    JNIEnv *env, jobject jthis)
{
  JNIEntry(VersionExtended$LinkedLib, getName);
  const svn_version_ext_linked_lib_t *const lib = getLinkedLib(env, jthis);
  if (lib)
    return env->NewStringUTF(lib->name);
  return 0;
}

JNIEXPORT jstring JNICALL
Java_org_apache_subversion_javahl_types_VersionExtended_00024LinkedLib_getCompiledVersion(
    JNIEnv *env, jobject jthis)
{
  JNIEntry(VersionExtended$LinkedLib, getCompiledVersion);
  const svn_version_ext_linked_lib_t *const lib = getLinkedLib(env, jthis);
  if (lib)
    return env->NewStringUTF(lib->compiled_version);
  return 0;
}

JNIEXPORT jstring JNICALL
Java_org_apache_subversion_javahl_types_VersionExtended_00024LinkedLib_getRuntimeVersion(
    JNIEnv *env, jobject jthis)
{
  JNIEntry(VersionExtended$LinkedLib, getRuntimeVersion);
  const svn_version_ext_linked_lib_t *const lib = getLinkedLib(env, jthis);
  if (lib)
    return env->NewStringUTF(lib->runtime_version);
  return 0;
}


// VersionExtended.LoadedLib native methods

namespace {
static const svn_version_ext_loaded_lib_t *
getLoadedLib(JNIEnv *env, jobject jthis)
{
  static volatile jfieldID fid = 0;
  if (!fid)
    {
      fid = env->GetFieldID(env->GetObjectClass(jthis), "index", "I");
      if (JNIUtil::isJavaExceptionThrown())
        return NULL;
    }

  const int index = env->GetIntField(jthis, fid);
  if (JNIUtil::isJavaExceptionThrown())
    return NULL;

  const VersionExtended *const vx =
    VersionExtended::getCppObjectFromLoadedLib(jthis);
  if (vx)
    return vx->get_loaded_lib(index);
  return NULL;
}
} // anonymous namespace

JNIEXPORT jstring JNICALL
Java_org_apache_subversion_javahl_types_VersionExtended_00024LoadedLib_getName(
    JNIEnv *env, jobject jthis)
{
  JNIEntry(VersionExtended$LoadedLib, getName);
  const svn_version_ext_loaded_lib_t *const lib = getLoadedLib(env, jthis);
  if (lib)
    return env->NewStringUTF(lib->name);
  return 0;
}

JNIEXPORT jstring JNICALL
Java_org_apache_subversion_javahl_types_VersionExtended_00024LoadedLib_getVersion(
    JNIEnv *env, jobject jthis)
{
  JNIEntry(VersionExtended$LoadedLib, getVersion);
  const svn_version_ext_loaded_lib_t *const lib = getLoadedLib(env, jthis);
  if (lib)
    return env->NewStringUTF(lib->version);
  return 0;
}


// VersionExtended.LinkedLibIterator and .LoadedLibIterator native methods

JNIEXPORT jboolean JNICALL
Java_org_apache_subversion_javahl_types_VersionExtended_00024LinkedLibIterator_hasNext(
    JNIEnv *env, jobject jthis)
{
  JNIEntry(VersionExtended$LinkedLibIterator, hasNext);

  static volatile jfieldID fid = 0;
  if (!fid)
    {
      fid = env->GetFieldID(env->GetObjectClass(jthis), "index", "I");
      if (JNIUtil::isJavaExceptionThrown())
        return false;
    }

  const int index = env->GetIntField(jthis, fid);
  if (JNIUtil::isJavaExceptionThrown())
    return false;

  const VersionExtended *const vx =
    VersionExtended::getCppObjectFromLinkedLibIterator(jthis);
  if (vx)
    return !!vx->get_linked_lib(1 + index);
  return false;
}

JNIEXPORT jboolean JNICALL
Java_org_apache_subversion_javahl_types_VersionExtended_00024LoadedLibIterator_hasNext(
    JNIEnv *env, jobject jthis)
{
  JNIEntry(VersionExtended$LoadedLibIterator, hasNext);

  static volatile jfieldID fid = 0;
  if (!fid)
    {
      fid = env->GetFieldID(env->GetObjectClass(jthis), "index", "I");
      if (JNIUtil::isJavaExceptionThrown())
        return false;
    }

  const int index = env->GetIntField(jthis, fid);
  if (JNIUtil::isJavaExceptionThrown())
    return false;

  const VersionExtended *const vx =
    VersionExtended::getCppObjectFromLoadedLibIterator(jthis);
  if (vx)
    return !!vx->get_loaded_lib(1 + index);
  return false;
}
