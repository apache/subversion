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
 * @file org_apache_subversion_javahl_util_ConfigLib.cpp
 * @brief Implementation of the native methods in the Java class ConfigLib
 */

#include "../include/org_apache_subversion_javahl_util_ConfigLib.h"

#include "JNIStackElement.h"
#include "JNIStringHolder.h"
#include "JNIUtil.h"
#include "JNICriticalSection.h"
#include "GlobalConfig.h"

namespace {
bool g_ignore_native_credentials = false;
} // anonymous callback

bool GlobalConfig::useNativeCredentialsStore()
{
  JNICriticalSection lock(*JNIUtil::g_configMutex);
  return !g_ignore_native_credentials;
}

JNIEXPORT void JNICALL
Java_org_apache_subversion_javahl_util_ConfigLib_enableNativeCredentialsStore(
    JNIEnv* env, jobject jthis)
{
  JNIEntry(ConfigLib, enableNativeCredentialsStore);

  JNICriticalSection lock(*JNIUtil::g_configMutex);
  g_ignore_native_credentials = false;
}

JNIEXPORT void JNICALL
Java_org_apache_subversion_javahl_util_ConfigLib_disableNativeCredentialsStore(
    JNIEnv* env, jobject jthis)
{
  JNIEntry(ConfigLib, disableNativeCredentialsStore);

  JNICriticalSection lock(*JNIUtil::g_configMutex);
  g_ignore_native_credentials = true;
}

JNIEXPORT jboolean JNICALL
Java_org_apache_subversion_javahl_util_ConfigLib_isNativeCredentialsStoreEnabled(
    JNIEnv* env, jobject jthis)
{
  JNIEntry(ConfigLib, isNativeCredentialsStoreEnabled);

  return jboolean(GlobalConfig::useNativeCredentialsStore());
}
