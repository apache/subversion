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

#include "jniwrapper/jni_stack.hpp"

#include "AuthnCallback.hpp"
#include "GlobalConfig.h"

#include "JNIUtil.h"
#include "JNICriticalSection.h"

#include "svn_config.h"

#include "svn_private_config.h"

namespace {
bool g_ignore_native_credentials = false;
} // anonymous namespace

bool GlobalConfig::useNativeCredentialsStore()
{
  JNICriticalSection lock(*JNIUtil::g_configMutex);
  return !g_ignore_native_credentials;
}

JNIEXPORT void JNICALL
Java_org_apache_subversion_javahl_util_ConfigLib_enableNativeCredentialsStore(
    JNIEnv* jenv, jobject jthis)
{
  SVN_JAVAHL_JNI_TRY(ConfigLib, enableNativeCredentialsStore)
    {
      JNICriticalSection lock(*JNIUtil::g_configMutex);
      g_ignore_native_credentials = false;
    }
  SVN_JAVAHL_JNI_CATCH;
}

JNIEXPORT void JNICALL
Java_org_apache_subversion_javahl_util_ConfigLib_disableNativeCredentialsStore(
    JNIEnv* jenv, jobject jthis)
{
  SVN_JAVAHL_JNI_TRY(ConfigLib, disableNativeCredentialsStore)
    {
      JNICriticalSection lock(*JNIUtil::g_configMutex);
      g_ignore_native_credentials = true;
    }
  SVN_JAVAHL_JNI_CATCH;
}

JNIEXPORT jboolean JNICALL
Java_org_apache_subversion_javahl_util_ConfigLib_isNativeCredentialsStoreEnabled(
    JNIEnv* jenv, jobject jthis)
{
  SVN_JAVAHL_JNI_TRY(ConfigLib, isNativeCredentialsStoreEnabled)
    {
      return jboolean(GlobalConfig::useNativeCredentialsStore());
    }
  SVN_JAVAHL_JNI_CATCH;
  return JNI_FALSE;
}

JNIEXPORT jobject JNICALL
Java_org_apache_subversion_javahl_util_ConfigLib_nativeGetCredential(
    JNIEnv* jenv, jobject jthis,
    jstring jconfig_dir, jstring jcred_kind, jstring jrealm)
{
  SVN_JAVAHL_JNI_TRY(ConfigLib, nativeGetCredential)
    {
      if (!GlobalConfig::useNativeCredentialsStore())
        return NULL;

      const Java::Env env(jenv);
    }
  SVN_JAVAHL_JNI_CATCH;
  return NULL;
}

JNIEXPORT jobject JNICALL
Java_org_apache_subversion_javahl_util_ConfigLib_nativeRemoveCredential(
    JNIEnv* jenv, jobject jthis,
    jstring jconfig_dir, jstring jcred_kind, jstring jrealm)
{
  SVN_JAVAHL_JNI_TRY(ConfigLib, nativeRemoveCredential)
    {
      if (!GlobalConfig::useNativeCredentialsStore())
        return NULL;

      const Java::Env env(jenv);
    }
  SVN_JAVAHL_JNI_CATCH;
  return NULL;
}

JNIEXPORT jobject JNICALL
Java_org_apache_subversion_javahl_util_ConfigLib_nativeAddCredential(
    JNIEnv* jenv, jobject jthis,
    jstring jconfig_dir, jstring jcred_kind, jstring jrealm,
    jstring jusername, jstring jpassword,
    jstring jserver_cert_hostname,
    jstring jserver_cert_fingerprint,
    jstring jserver_cert_valid_from,
    jstring jserver_cert_valid_until,
    jstring jserver_cert_issuer,
    jstring jserver_cert_der,
    jint jserver_cert_failures,
    jstring jclient_cert_passphrase)
{
  SVN_JAVAHL_JNI_TRY(ConfigLib, nativeAddCredential)
    {
      if (!GlobalConfig::useNativeCredentialsStore())
        return NULL;

      const Java::Env env(jenv);
    }
  SVN_JAVAHL_JNI_CATCH;
  return NULL;
}


JNIEXPORT jobject JNICALL
Java_org_apache_subversion_javahl_util_ConfigLib_iterateCredentials(
    JNIEnv* jenv, jobject jthis,
    jboolean jdelete_matching,
    jstring jconfig_dir, jstring jcred_kind,
    jstring jrealm_pattern, jstring jusername_pattern,
    jstring jhostname_pattern, jstring jtext_pattern)
{
  SVN_JAVAHL_JNI_TRY(ConfigLib, iterateCredentials)
    {
      if (!GlobalConfig::useNativeCredentialsStore())
        return NULL;

      const Java::Env env(jenv);
    }
  SVN_JAVAHL_JNI_CATCH;
  return NULL;
}
