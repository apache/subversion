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
 * @file ProplistCallback.cpp
 * @brief Implementation of the class ProplistCallback
 */

#include "ProplistCallback.h"
#include "JNIUtil.h"
#include "svn_time.h"

/**
 * Create a ProplistCallback object
 * @param jcallback the Java callback object.
 */
ProplistCallback::ProplistCallback(jobject jcallback)
{
  m_callback = jcallback;
}

/**
 * Destroy a ProplistCallback object
 */
ProplistCallback::~ProplistCallback()
{
  // the m_callback does not need to be destroyed, because it is the passed
  // in parameter to the Java SVNClient.properties method.
}

svn_error_t *
ProplistCallback::callback(void *baton,
                           const char *path,
                           apr_hash_t *prop_hash,
                           apr_pool_t *pool)
{
  if (baton)
    return ((ProplistCallback *)baton)->singlePath(path, prop_hash, pool);

  return SVN_NO_ERROR;
}

/**
 * Callback called for a single path
 * @param path      the path name
 * @param prop_hash the hash of properties on this path
 * @param pool      memory pool for the use of this function
 */
svn_error_t *ProplistCallback::singlePath(const char *path,
                                          apr_hash_t *prop_hash,
                                          apr_pool_t *pool)
{
  JNIEnv *env = JNIUtil::getEnv();

  // The method id will not change during the time this library is
  // loaded, so it can be cached.
  static jmethodID mid = 0;
  if (mid == 0)
    {
      jclass clazz = env->FindClass(JAVA_PACKAGE"/ProplistCallback");
      if (JNIUtil::isJavaExceptionThrown())
        return SVN_NO_ERROR;

      mid = env->GetMethodID(clazz, "singlePath",
                             "(Ljava/lang/String;Ljava/util/Map;)V");
      if (JNIUtil::isJavaExceptionThrown() || mid == 0)
        return SVN_NO_ERROR;

      env->DeleteLocalRef(clazz);
      if (JNIUtil::isJavaExceptionThrown())
        return SVN_NO_ERROR;
    }

  // convert the parameters to their Java relatives
  jstring jpath = JNIUtil::makeJString(path);
  if (JNIUtil::isJavaExceptionThrown())
    return SVN_NO_ERROR;

  jobject jmap = NULL;
  jmap = makeMapFromHash(prop_hash, pool);
  if (JNIUtil::isJavaExceptionThrown())
    return SVN_NO_ERROR;

  // call the Java method
  env->CallVoidMethod(m_callback, mid, jpath, jmap);
  if (JNIUtil::isJavaExceptionThrown())
    return SVN_NO_ERROR;

  // cleanup the temporary Java objects
  env->DeleteLocalRef(jpath);
  if (JNIUtil::isJavaExceptionThrown())
    return SVN_NO_ERROR;

  env->DeleteLocalRef(jmap);
  // We return whether an exception was thrown or not.

  return SVN_NO_ERROR;
}

jobject ProplistCallback::makeMapFromHash(apr_hash_t *prop_hash,
                                          apr_pool_t *pool)
{
  JNIEnv *env = JNIUtil::getEnv();
  jclass clazz = env->FindClass("java/util/HashMap");
  if (JNIUtil::isJavaExceptionThrown())
    return NULL;

  static jmethodID init_mid = 0;
  if (init_mid == 0)
    {
      init_mid = env->GetMethodID(clazz, "<init>", "()V");
      if (JNIUtil::isJavaExceptionThrown())
        return NULL;
    }

  static jmethodID put_mid = 0;
  if (put_mid == 0)
    {
      put_mid = env->GetMethodID(clazz, "put",
                                 "(Ljava/lang/Object;Ljava/lang/Object;)"
                                 "Ljava/lang/Object;");
      if (JNIUtil::isJavaExceptionThrown())
        return NULL;
    }

  jobject map = env->NewObject(clazz, init_mid);
  if (JNIUtil::isJavaExceptionThrown())
    return NULL;

  apr_hash_index_t *hi;
  int i = 0;
  for (hi = apr_hash_first(pool, prop_hash); hi; hi = apr_hash_next(hi), ++i)
    {
      const char *key;
      svn_string_t *val;

      apr_hash_this(hi, (const void **)&key, NULL, (void **)&val);

      jstring jpropName = JNIUtil::makeJString(key);
      if (JNIUtil::isJavaExceptionThrown())
        return NULL;

      jstring jpropVal = JNIUtil::makeJString(val->data);
      if (JNIUtil::isJavaExceptionThrown())
        return NULL;

      env->CallObjectMethod(map, put_mid, jpropName, jpropVal);
      if (JNIUtil::isJavaExceptionThrown())
        return NULL;

      env->DeleteLocalRef(jpropName);
      if (JNIUtil::isJavaExceptionThrown())
        return NULL;

      env->DeleteLocalRef(jpropVal);
      if (JNIUtil::isJavaExceptionThrown())
        return NULL;
    }

  env->DeleteLocalRef(clazz);
  if (JNIUtil::isJavaExceptionThrown())
    return NULL;

  return map;
}
