/**
 * @copyright
 * ====================================================================
 *    Licensed to the Subversion Corporation (SVN Corp.) under one
 *    or more contributor license agreements.  See the NOTICE file
 *    distributed with this work for additional information
 *    regarding copyright ownership.  The SVN Corp. licenses this file
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
 * @file ChangelistCallback.cpp
 * @brief Implementation of the class ChangelistCallback
 */

#include "ChangelistCallback.h"
#include "SVNClient.h"
#include "JNIUtil.h"

/**
 * Create a ChangelistCallback object
 * @param jcallback the Java callback object.
 */
ChangelistCallback::ChangelistCallback(jobject jcallback)
{
  m_callback = jcallback;
}

/**
 * Destroy a ChangelistCallback object
 */
ChangelistCallback::~ChangelistCallback()
{
  // the m_callback does not need to be destroyed, because it is the passed
  // in parameter to the Java SVNClient.status method.
}

svn_error_t *
ChangelistCallback::callback(void *baton,
                             const char *path,
                             const char *changelist,
                             apr_pool_t *pool)
{
  if (baton)
    ((ChangelistCallback *)baton)->doChangelist(path, changelist, pool);

  return SVN_NO_ERROR;
}

/**
 * Callback called for a single status item.
 */
void
ChangelistCallback::doChangelist(const char *path, const char *changelist,
                                 apr_pool_t *pool)
{
  JNIEnv *env = JNIUtil::getEnv();

  static jmethodID mid = 0; // the method id will not change during
  // the time this library is loaded, so
  // it can be cached.
  if (mid == 0)
    {
      jclass clazz = env->FindClass(JAVA_PACKAGE"/ChangelistCallback");
      if (JNIUtil::isJavaExceptionThrown())
        return;

      mid = env->GetMethodID(clazz, "doChangelist",
                             "(Ljava/lang/String;Ljava/lang/String;)V");
      if (JNIUtil::isJavaExceptionThrown() || mid == 0)
        return;

      env->DeleteLocalRef(clazz);
      if (JNIUtil::isJavaExceptionThrown())
        return;
    }

  jstring jChangelist = JNIUtil::makeJString(changelist);
  if (JNIUtil::isJavaExceptionThrown())
    return;

  jstring jPath = JNIUtil::makeJString(path);
  if (JNIUtil::isJavaExceptionThrown())
    return;

  env->CallVoidMethod(m_callback, mid, jPath, jChangelist);
  if (JNIUtil::isJavaExceptionThrown())
    return;

  env->DeleteLocalRef(jChangelist);
  // We return here regardless of whether an exception is thrown or not,
  // so we do not need to explicitly check for one.
}
