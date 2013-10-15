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
jobject g_config_callback = NULL;
} // anonymous callback

bool GlobalConfig::useNativeCredentialsStore()
{
  JNICriticalSection lock(*JNIUtil::g_configMutex);
  return !g_ignore_native_credentials;
}

jobject GlobalConfig::getConfigCallback()
{
  JNICriticalSection lock(*JNIUtil::g_configMutex);
  return g_config_callback;
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

/*
 * Class:     org_apache_subversion_javahl_util_ConfigLib
 * Method:    isNativeCredentialsStoreEnabled
 * Signature: ()Z
 */
JNIEXPORT jboolean JNICALL
Java_org_apache_subversion_javahl_util_ConfigLib_isNativeCredentialsStoreEnabled(
    JNIEnv* env, jobject jthis)
{
  JNIEntry(ConfigLib, isNativeCredentialsStoreEnabled);

  return jboolean(GlobalConfig::useNativeCredentialsStore());
}

/*
 * Class:     org_apache_subversion_javahl_util_ConfigLib
 * Method:    setConfigEventHandler
 * Signature: (Lorg/apache/subversion/javahl/callback/ConfigEvent;)V
 */
JNIEXPORT void JNICALL
Java_org_apache_subversion_javahl_util_ConfigLib_setConfigEventHandler(
    JNIEnv* env, jobject jthis, jobject jcallback)
{
  JNIEntry(ConfigLib, setConfigEventHandler);

  JNICriticalSection lock(*JNIUtil::g_configMutex);
  if (g_config_callback)
    {
      env->DeleteGlobalRef(g_config_callback);
      g_config_callback = NULL;
    }
  if (jcallback)
    {
      g_config_callback = env->NewGlobalRef(jcallback);
      env->DeleteLocalRef(jcallback);
    }
}

JNIEXPORT jobject JNICALL
Java_org_apache_subversion_javahl_util_ConfigLib_getConfigEventHandler(
    JNIEnv* env, jobject jthis)
{
  JNIEntry(ConfigLib, getConfigEventHandler);

  return GlobalConfig::getConfigCallback();
}
