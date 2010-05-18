/**
 * @copyright
 * ====================================================================
 * Copyright (c) 2007-2008 CollabNet.  All rights reserved.
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
 * @file LogMessageCallback.cpp
 * @brief Implementation of the class LogMessageCallback
 */

#include "LogMessageCallback.h"
#include "ProplistCallback.h"
#include "EnumMapper.h"
#include "JNIUtil.h"
#include "svn_time.h"
#include "svn_sorts.h"
#include "svn_compat.h"

/**
 * Create a LogMessageCallback object
 * @param jcallback the Java callback object.
 */
LogMessageCallback::LogMessageCallback(jobject jcallback)
{
  m_callback = jcallback;
}

/**
 * Destroy a LogMessageCallback object
 */
LogMessageCallback::~LogMessageCallback()
{
  // The m_callback does not need to be destroyed because it is the
  // passed in parameter to the Java SVNClientInterface.logMessages
  // method.
}

svn_error_t *
LogMessageCallback::callback(void *baton,
                             svn_log_entry_t *log_entry,
                             apr_pool_t *pool)
{
  if (baton)
    return ((LogMessageCallback *)baton)->singleMessage(log_entry, pool);

  return SVN_NO_ERROR;
}

/**
 * Callback called for a single log message
 */
svn_error_t *
LogMessageCallback::singleMessage(svn_log_entry_t *log_entry, apr_pool_t *pool)
{
  JNIEnv *env = JNIUtil::getEnv();

  // The method id will not change during the time this library is
  // loaded, so it can be cached.
  static jmethodID sm_mid = 0;
  if (sm_mid == 0)
    {
      jclass clazz = env->FindClass(JAVA_PACKAGE"/LogMessageCallback");
      if (JNIUtil::isJavaExceptionThrown())
        return SVN_NO_ERROR;

      sm_mid = env->GetMethodID(clazz,
                                "singleMessage",
                                "([L"JAVA_PACKAGE"/ChangePath;"
                                "JLjava/util/Map;Z)V");
      if (JNIUtil::isJavaExceptionThrown())
        return SVN_NO_ERROR;

      env->DeleteLocalRef(clazz);
      if (JNIUtil::isJavaExceptionThrown())
        return SVN_NO_ERROR;
    }

  jclass clazzCP = env->FindClass(JAVA_PACKAGE"/ChangePath");
  if (JNIUtil::isJavaExceptionThrown())
    return SVN_NO_ERROR;

  static jmethodID midCP = 0;
  if (midCP == 0)
    {
      midCP = env->GetMethodID(clazzCP,
                               "<init>",
                               "(Ljava/lang/String;JLjava/lang/String;CI)V");
      if (JNIUtil::isJavaExceptionThrown())
        return SVN_NO_ERROR;
    }

  jobjectArray jChangedPaths = NULL;
  if (log_entry->changed_paths)
    {
      apr_array_header_t *sorted_paths;
      int i;

      /* Get an array of sorted hash keys. */
      sorted_paths = svn_sort__hash(log_entry->changed_paths,
                                    svn_sort_compare_items_as_paths,
                                    pool);

      jChangedPaths = env->NewObjectArray(sorted_paths->nelts,
                                          clazzCP,
                                          NULL);

      for (i = 0; i < sorted_paths->nelts; ++i)
        {
          svn_sort__item_t *item = &(APR_ARRAY_IDX(sorted_paths, i,
                                                   svn_sort__item_t));
          const char *path = (const char *)item->key;
          svn_log_changed_path2_t *log_item
            = (svn_log_changed_path2_t *)
            apr_hash_get(log_entry->changed_paths, item->key, item->klen);

          jstring jpath = JNIUtil::makeJString(path);
          if (JNIUtil::isJavaExceptionThrown())
            return SVN_NO_ERROR;

          jstring jcopyFromPath =
            JNIUtil::makeJString(log_item->copyfrom_path);
          if (JNIUtil::isJavaExceptionThrown())
            return SVN_NO_ERROR;

          jlong jcopyFromRev = log_item->copyfrom_rev;
          jchar jaction = log_item->action;

          jobject cp = env->NewObject(clazzCP, midCP, jpath, jcopyFromRev,
                                  jcopyFromPath, jaction,
                                  EnumMapper::mapNodeKind(log_item->node_kind));
          if (JNIUtil::isJavaExceptionThrown())
            return SVN_NO_ERROR;

          env->SetObjectArrayElement(jChangedPaths, i, cp);
          if (JNIUtil::isJavaExceptionThrown())
            return SVN_NO_ERROR;

          env->DeleteLocalRef(cp);
          if (JNIUtil::isJavaExceptionThrown())
            return SVN_NO_ERROR;

          env->DeleteLocalRef(jpath);
          if (JNIUtil::isJavaExceptionThrown())
            return SVN_NO_ERROR;

          env->DeleteLocalRef(jcopyFromPath);
          if (JNIUtil::isJavaExceptionThrown())
            return SVN_NO_ERROR;
        }
    }

  jobject jrevprops = NULL;
  if (log_entry->revprops != NULL && apr_hash_count(log_entry->revprops) > 0)
    jrevprops = ProplistCallback::makeMapFromHash(log_entry->revprops, pool);

  env->CallVoidMethod(m_callback,
                      sm_mid,
                      jChangedPaths,
                      (jlong)log_entry->revision,
                      jrevprops,
                      (jboolean)log_entry->has_children);
  if (JNIUtil::isJavaExceptionThrown())
    return SVN_NO_ERROR;

  env->DeleteLocalRef(jChangedPaths);
  if (JNIUtil::isJavaExceptionThrown())
    return SVN_NO_ERROR;

  env->DeleteLocalRef(jrevprops);
  // No need to check for an exception here, because we return anyway.

  return SVN_NO_ERROR;
}
