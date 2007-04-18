/**
 * @copyright
 * ====================================================================
 * Copyright (c) 2006 CollabNet.  All rights reserved.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution.  The terms
 * are also available at http://subversion.tigris.org/license-1.html.
 * If newer versions of this license are posted there, you may use a
 * newer version instead, at your option.
 *
 * This software consists of voluntary contributions made by many
 * individuals.  For exact contribution history, see the revision
 * history and logs, available at http://subversion.tigris.org/.
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
  if (JNIUtil::isJavaExceptionThrown())
    return NULL;

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

  // As Java method IDs will not change during the time this library
  // is loaded, they can be cached.
  static jmethodID mid = 0;
  if (mid == 0)
    {
      // Initialize the method ID.
      jclass clazz = env->FindClass(JAVA_PACKAGE"/ProgressListener");
      if (JNIUtil::isJavaExceptionThrown())
        return;

      mid = env->GetMethodID(clazz, "onProgress",
                             "(Lorg/tigris/subversion/javahl/ProgressEvent;)V");
      if (JNIUtil::isJavaExceptionThrown() || mid == 0)
        return;

      env->DeleteLocalRef(clazz);
      if (JNIUtil::isJavaExceptionThrown())
        return;
    }

  static jmethodID midCT = 0;
  jclass clazz = env->FindClass(JAVA_PACKAGE"/ProgressEvent");
  if (JNIUtil::isJavaExceptionThrown())
    return;

  if (midCT == 0)
    {
      midCT = env->GetMethodID(clazz, "<init>", "(JJ)V");
      if (JNIUtil::isJavaExceptionThrown() || midCT == 0)
        return;
    }

  // Call the Java method.
  jobject jevent = env->NewObject(clazz, midCT,
                                  (jlong) progressVal, (jlong) total);
  if (JNIUtil::isJavaExceptionThrown())
    return;

  env->DeleteLocalRef(clazz);
  if (JNIUtil::isJavaExceptionThrown())
    return;

  env->CallVoidMethod(m_progressListener, mid, jevent);
  if (JNIUtil::isJavaExceptionThrown())
    return;

  env->DeleteLocalRef(jevent);
  if (JNIUtil::isJavaExceptionThrown())
    return;
}
