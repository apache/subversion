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
 * @file org_apache_subversion_javahl_types_RuntimeVersion.cpp
 * @brief Implementation of the native methods in the Java class RuntimeVersion.
 */

#include "../include/org_apache_subversion_javahl_types_RuntimeVersion.h"
#include "svn_client.h"
#include "svn_version.h"

#include "jniwrapper/jni_stack.hpp"
#include "jniwrapper/jni_string.hpp"

JNIEXPORT jint JNICALL
Java_org_apache_subversion_javahl_types_RuntimeVersion_getMajor(
    JNIEnv* jenv, jobject jthis)
{
  const svn_version_t* const version = svn_client_version();
  return jint(version->major);
}

JNIEXPORT jint JNICALL
Java_org_apache_subversion_javahl_types_RuntimeVersion_getMinor(
    JNIEnv* jenv, jobject jthis)
{
  const svn_version_t* const version = svn_client_version();
  return jint(version->minor);
}

JNIEXPORT jint JNICALL
Java_org_apache_subversion_javahl_types_RuntimeVersion_getPatch(
    JNIEnv* jenv, jobject jthis)
{
  const svn_version_t* const version = svn_client_version();
  return jint(version->patch);
}

JNIEXPORT jstring JNICALL
Java_org_apache_subversion_javahl_types_RuntimeVersion_getNumberTag(
    JNIEnv* jenv, jobject jthis)
{
  SVN_JAVAHL_JNI_TRY(RuntimeVersion, getNumberTag)
    {
      const svn_version_t* const version = svn_client_version();
      return Java::String(Java::Env(jenv), version->tag).get();
    }
  SVN_JAVAHL_JNI_CATCH;
  return NULL;
}
