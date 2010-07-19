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
 * @file ProgressListener.cpp
 * @brief Implementation of the class ProgressListener.
 */

#include "ProgressListener.h"
#include "JNIUtil.h"

ProgressListener::ProgressListener(jobject jprogressListener)
{
  m_progressListener = jprogressListener;
}

ProgressListener::~ProgressListener()
{
  if (m_progressListener != NULL)
    {
      JNIEnv *env = JNIUtil::getEnv();
      env->DeleteGlobalRef(m_progressListener);
    }
}

ProgressListener *
ProgressListener::makeCProgressListener(jobject jprogressListener)
{
  if (jprogressListener == NULL)
    return NULL;

  JNIEnv *env = JNIUtil::getEnv();

  // Sanity check that the object implements the ProgressListener
  // Java interface.
  jclass clazz = env->FindClass(JAVA_PACKAGE"/ProgressListener");
  if (JNIUtil::isJavaExceptionThrown())
    return NULL;

  if (!env->IsInstanceOf(jprogressListener, clazz))
    {
      env->DeleteLocalRef(clazz);
      return NULL;
    }
  env->DeleteLocalRef(clazz);

  // Retain a global reference to our Java peer.
  jobject myListener = env->NewGlobalRef(jprogressListener);
  if (JNIUtil::isJavaExceptionThrown())
    return NULL;

  // Create the peer.
  return new ProgressListener(myListener);
}

void
ProgressListener::progress(apr_off_t nbrBytes, apr_off_t total, void *baton,
                           apr_pool_t *pool)
{
  if (baton)
    ((ProgressListener *) baton)->onProgress(nbrBytes, total, pool);
}

void
ProgressListener::onProgress(apr_off_t progressVal, apr_off_t total,
                             apr_pool_t *pool)
{
  JNIEnv *env = JNIUtil::getEnv();

  // Create a local frame for our references
  env->PushLocalFrame(LOCAL_FRAME_SIZE);
  if (JNIUtil::isJavaExceptionThrown())
    return;

  // As Java method IDs will not change during the time this library
  // is loaded, they can be cached.
  static jmethodID mid = 0;
  if (mid == 0)
    {
      // Initialize the method ID.
      jclass clazz = env->FindClass(JAVA_PACKAGE"/ProgressListener");
      if (JNIUtil::isJavaExceptionThrown())
        POP_AND_RETURN_NOTHING();

      mid = env->GetMethodID(clazz, "onProgress",
                             "(L"JAVA_PACKAGE"/ProgressEvent;)V");
      if (JNIUtil::isJavaExceptionThrown() || mid == 0)
        POP_AND_RETURN_NOTHING();
    }

  static jmethodID midCT = 0;
  jclass clazz = env->FindClass(JAVA_PACKAGE"/ProgressEvent");
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NOTHING();

  if (midCT == 0)
    {
      midCT = env->GetMethodID(clazz, "<init>", "(JJ)V");
      if (JNIUtil::isJavaExceptionThrown() || midCT == 0)
        POP_AND_RETURN_NOTHING();
    }

  // Call the Java method.
  jobject jevent = env->NewObject(clazz, midCT,
                                  (jlong) progressVal, (jlong) total);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NOTHING();

  env->CallVoidMethod(m_progressListener, mid, jevent);
  
  POP_AND_RETURN_NOTHING();
}
