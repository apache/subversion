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
 * @file ReposVerifyCallback.cpp
 * @brief Implementation of the class ReposVerifyCallback
 */

#include "ReposVerifyCallback.h"
#include "JNIUtil.h"

ReposVerifyCallback::ReposVerifyCallback(jobject jverify_cb)
  : m_jverify_cb(jverify_cb)
{}

ReposVerifyCallback::~ReposVerifyCallback()
{
  // Don't need to destroy the reference, since it was given us by Java
}

svn_error_t *
ReposVerifyCallback::callback(void *baton,
                              svn_revnum_t revision,
                              svn_error_t *verify_err,
                              apr_pool_t *scratch_pool)
{
  if (!baton)
    return SVN_NO_ERROR;

  static_cast<ReposVerifyCallback*>(baton)
    ->onVerifyError(revision, verify_err, scratch_pool);
  if (JNIUtil::isJavaExceptionThrown())
    return JNIUtil::wrapJavaException();
  return SVN_NO_ERROR;
}

void
ReposVerifyCallback::onVerifyError(svn_revnum_t revision,
                                   svn_error_t *verify_err,
                                   apr_pool_t *scratch_pool)
{
  JNIEnv *env = JNIUtil::getEnv();

  // Java method id will not change during the time this library is
  // loaded, so it can be cached.
  static jmethodID mid = 0;
  if (mid == 0)
    {
      jclass clazz = env->FindClass(JAVAHL_CLASS("/callback/ReposVerifyCallback"));
      if (JNIUtil::isJavaExceptionThrown())
        return;

      mid = env->GetMethodID(clazz, "onVerifyError",
                             "(J" JAVAHL_ARG("/ClientException;") ")V");
      if (JNIUtil::isJavaExceptionThrown() || mid == 0)
        return;

      env->DeleteLocalRef(clazz);
    }

  jthrowable jverify_err = NULL;
  if (verify_err)
    jverify_err = JNIUtil::createClientException(svn_error_dup(verify_err), NULL);
  if (JNIUtil::isJavaExceptionThrown())
    return;

  env->CallVoidMethod(m_jverify_cb, mid, jlong(revision), jverify_err);
  if (verify_err)
    env->DeleteLocalRef(jverify_err);
}
