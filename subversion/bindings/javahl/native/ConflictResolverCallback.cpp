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
 * @file ConflictResolverCallback.cpp
 * @brief Implementation of the class ConflictResolverCallback.
 */

#include "svn_error.h"

#include "JNIUtil.h"
#include "JNIStringHolder.h"
#include "EnumMapper.h"
#include "CreateJ.h"
#include "ConflictResolverCallback.h"

ConflictResolverCallback::ConflictResolverCallback(jobject jconflictResolver)
{
  m_conflictResolver = jconflictResolver;
}

ConflictResolverCallback::~ConflictResolverCallback()
{
  if (m_conflictResolver != NULL)
    {
      JNIEnv *env = JNIUtil::getEnv();
      env->DeleteGlobalRef(m_conflictResolver);
    }
}

ConflictResolverCallback *
ConflictResolverCallback::makeCConflictResolverCallback(jobject jconflictResolver)
{
  if (jconflictResolver == NULL)
    return NULL;

  JNIEnv *env = JNIUtil::getEnv();

  // Sanity check that the object implements the ConflictResolverCallback
  // Java interface.
  jclass clazz = env->FindClass(JAVA_PACKAGE "/callback/ConflictResolverCallback");
  if (JNIUtil::isJavaExceptionThrown())
    return NULL;

  if (!env->IsInstanceOf(jconflictResolver, clazz))
    {
      env->DeleteLocalRef(clazz);
      return NULL;
    }
  env->DeleteLocalRef(clazz);

  // Retain a global reference to our Java peer.
  jobject myListener = env->NewGlobalRef(jconflictResolver);
  if (JNIUtil::isJavaExceptionThrown())
    return NULL;

  // Create the peer.
  return new ConflictResolverCallback(myListener);
}

svn_error_t *
ConflictResolverCallback::resolveConflict(svn_wc_conflict_result_t **result,
                                          const svn_wc_conflict_description_t *
                                          desc,
                                          void *baton,
                                          apr_pool_t *pool)
{
  if (baton)
    return ((ConflictResolverCallback *) baton)->resolve(result, desc, pool);
  else
    return SVN_NO_ERROR;
}

svn_error_t *
ConflictResolverCallback::resolve(svn_wc_conflict_result_t **result,
                                  const svn_wc_conflict_description_t *desc,
                                  apr_pool_t *pool)
{
  JNIEnv *env = JNIUtil::getEnv();

  // Create a local frame for our references
  env->PushLocalFrame(LOCAL_FRAME_SIZE);
  if (JNIUtil::isJavaExceptionThrown())
    return SVN_NO_ERROR;

  // As Java method IDs will not change during the time this library
  // is loaded, they can be cached.
  static jmethodID mid = 0;
  if (mid == 0)
    {
      // Initialize the callback method ID.
      jclass clazz = env->FindClass(JAVA_PACKAGE "/callback/ConflictResolverCallback");
      if (JNIUtil::isJavaExceptionThrown())
        POP_AND_RETURN(SVN_NO_ERROR);

      mid = env->GetMethodID(clazz, "resolve",
                             "(L" JAVA_PACKAGE "/ConflictDescriptor;)"
                             "L" JAVA_PACKAGE "/ConflictResult;");
      if (JNIUtil::isJavaExceptionThrown() || mid == 0)
        POP_AND_RETURN(SVN_NO_ERROR);
    }

  // Create an instance of the conflict descriptor.
  jobject jdesc = CreateJ::ConflictDescriptor(desc);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN(SVN_NO_ERROR);

  // Invoke the Java conflict resolver callback method using the descriptor.
  jobject jresult = env->CallObjectMethod(m_conflictResolver, mid, jdesc);
  if (JNIUtil::isJavaExceptionThrown())
    {
      // If an exception is thrown by our conflict resolver, remove it
      // from the JNI env, and convert it into a Subversion error.
      const char *msg = JNIUtil::thrownExceptionToCString();
      svn_error_t *err = svn_error_create(SVN_ERR_WC_CONFLICT_RESOLVER_FAILURE,
                                          NULL, msg);
      env->PopLocalFrame(NULL);
      return err;
    }
  *result = javaResultToC(jresult, pool);
  if (*result == NULL)
    {
      // Unable to convert the result into a C representation.
      env->PopLocalFrame(NULL);
      return svn_error_create(SVN_ERR_WC_CONFLICT_RESOLVER_FAILURE, NULL, NULL);
    }

  env->PopLocalFrame(NULL);
  return SVN_NO_ERROR;
}

svn_wc_conflict_result_t *
ConflictResolverCallback::javaResultToC(jobject jresult, apr_pool_t *pool)
{
  JNIEnv *env = JNIUtil::getEnv();

  // Create a local frame for our references
  env->PushLocalFrame(LOCAL_FRAME_SIZE);
  if (JNIUtil::isJavaExceptionThrown())
    return SVN_NO_ERROR;

  static jmethodID getChoice = 0;
  static jmethodID getMergedPath = 0;

  jclass clazz = NULL;
  if (getChoice == 0 || getMergedPath == 0)
    {
      clazz = env->FindClass(JAVA_PACKAGE "/ConflictResult");
      if (JNIUtil::isJavaExceptionThrown())
        POP_AND_RETURN_NULL;
    }

  if (getChoice == 0)
    {
      getChoice = env->GetMethodID(clazz, "getChoice",
                                   "()L"JAVA_PACKAGE"/ConflictResult$Choice;");
      if (JNIUtil::isJavaExceptionThrown() || getChoice == 0)
        POP_AND_RETURN_NULL;
    }
  if (getMergedPath == 0)
    {
      getMergedPath = env->GetMethodID(clazz, "getMergedPath",
                                       "()Ljava/lang/String;");
      if (JNIUtil::isJavaExceptionThrown() || getMergedPath == 0)
        POP_AND_RETURN_NULL;
    }

  jobject jchoice = env->CallObjectMethod(jresult, getChoice);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  jstring jmergedPath = (jstring) env->CallObjectMethod(jresult, getMergedPath);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  JNIStringHolder mergedPath(jmergedPath);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  svn_wc_conflict_result_t *result =
         svn_wc_create_conflict_result(EnumMapper::toConflictChoice(jchoice),
                                       mergedPath.pstrdup(pool),
                                       pool);

  env->PopLocalFrame(NULL);
  return result;
}
