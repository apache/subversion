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
 * @file ClientNotifyCallback.cpp
 * @brief Implementation of the class ClientNotifyCallback
 */

#include "ClientNotifyCallback.h"
#include "JNIUtil.h"
#include "CreateJ.h"
#include "EnumMapper.h"
#include "RevisionRange.h"

/**
 * Create a new object and store the Java object.
 * @param notify    global reference to the Java object
 */
ClientNotifyCallback::ClientNotifyCallback(jobject p_notify)
{
  m_notify = p_notify;
}

/**
 * Destroy the object and delete the global reference to the Java
 * object.
 */
ClientNotifyCallback::~ClientNotifyCallback()
{
  if (m_notify != NULL)
    {
      JNIEnv *env = JNIUtil::getEnv();
      env->DeleteGlobalRef(m_notify);
    }
}

/**
 * Create a C++ peer object for the Java object.
 * @param notify    a local reference to the Java object
 */
ClientNotifyCallback *ClientNotifyCallback::makeCNotify(jobject notify)
{
  // If the Java object is null -> no C++ peer needed.
  if (notify == NULL)
    return NULL;
  JNIEnv *env = JNIUtil::getEnv();

  // Sanity check, that the object implements Notify.
  jclass clazz = env->FindClass(JAVA_PACKAGE"/callback/ClientNotifyCallback");
  if (JNIUtil::isJavaExceptionThrown())
    return NULL;

  if (!env->IsInstanceOf(notify, clazz))
    {
      env->DeleteLocalRef(clazz);
      return NULL;
    }
  env->DeleteLocalRef(clazz);

  // Make a global reference, because the reference is longer
  // needed, than the call.
  jobject myNotify = env->NewGlobalRef(notify);
  if (JNIUtil::isJavaExceptionThrown())
    return NULL;

  // Create the peer.
  return new ClientNotifyCallback(myNotify);
}

/**
 * Notification function passed as svn_wc_notify_func2_t
 * @param baton notification instance is passed using this parameter
 * @param notify all the information about the event
 * @param pool an apr pool to allocated memory
 */
void
ClientNotifyCallback::notify(void *baton, const svn_wc_notify_t *notify, apr_pool_t *pool)
{
  // A Notify object is used as the baton.
  ClientNotifyCallback *that = (ClientNotifyCallback *) baton;
  if (that) // sanity check
    {
      // Call our method.
      that->onNotify(notify, pool);
    }
}

/**
 * Handler for Subversion notifications.
 *
 * @param notify all the information about the event
 * @param pool an apr pool to allocated memory
 */
void
ClientNotifyCallback::onNotify(const svn_wc_notify_t *wcNotify, apr_pool_t *pool)
{
  JNIEnv *env = JNIUtil::getEnv();

  // Java method id will not change during the time this library is
  // loaded, so it can be cached.
  static jmethodID mid = 0;
  if (mid == 0)
    {
      jclass clazz = env->FindClass(JAVA_PACKAGE"/callback/ClientNotifyCallback");
      if (JNIUtil::isJavaExceptionThrown())
        return;

      mid = env->GetMethodID(clazz, "onNotify",
                             "(L"JAVA_PACKAGE"/ClientNotifyInformation;)V");
      if (JNIUtil::isJavaExceptionThrown() || mid == 0)
        return;

      env->DeleteLocalRef(clazz);
    }

  jobject jInfo = CreateJ::ClientNotifyInformation(wcNotify, pool);
  if (JNIUtil::isJavaExceptionThrown())
    return;

  env->CallVoidMethod(m_notify, mid, jInfo);
  if (JNIUtil::isJavaExceptionThrown())
    return;

  env->DeleteLocalRef(jInfo);
}
