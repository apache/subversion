/**
 * @copyright
 * ====================================================================
 * Copyright (c) 2007 CollabNet.  All rights reserved.
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
