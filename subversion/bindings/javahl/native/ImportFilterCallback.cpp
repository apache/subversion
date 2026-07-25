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
 * @file ImportFilterCallback.cpp
 * @brief Implementation of the class ImportFilterCallback
 */

#include "ImportFilterCallback.h"
#include "EnumMapper.h"
#include "CreateJ.h"
#include "JNIUtil.h"
#include "svn_time.h"

/**
 * Create a ImportFilterCallback object
 * @param jcallback the Java callback object.
 */
ImportFilterCallback::ImportFilterCallback(jobject jcallback)
{
  m_callback = jcallback;
}

/**
 * Destroy a ImportFilterCallback object
 */
ImportFilterCallback::~ImportFilterCallback()
{
  // The m_callback does not need to be destroyed, because it is the passed
  // in parameter to the Java SVNClient.list method.
}

svn_error_t *
ImportFilterCallback::callback(void *baton,
                               svn_boolean_t *filtered,
                               const char *local_abspath,
                               const svn_io_dirent2_t *dirent,
                               apr_pool_t *pool)
{
  if (baton)
    return static_cast<ImportFilterCallback *>(baton)->doImportFilter(
            filtered, local_abspath, dirent, pool);

  return SVN_NO_ERROR;
}

/**
 * Callback called for each directory entry.
 */
svn_error_t *
ImportFilterCallback::doImportFilter(svn_boolean_t *filtered,
                                     const char *local_abspath,
                                     const svn_io_dirent2_t *dirent,
                                     apr_pool_t *pool)
{
  JNIEnv *env = JNIUtil::getEnv();

  // Create a local frame for our references
  env->PushLocalFrame(LOCAL_FRAME_SIZE);
  if (JNIUtil::isJavaExceptionThrown())
    return SVN_NO_ERROR;

  // The method id will not change during the time this library is
  // loaded, so it can be cached.
  static jmethodID mid = 0;
  if (mid == 0)
    {
      jclass clazz = env->FindClass(JAVAHL_CLASS("/callback/ImportFilterCallback"));
      if (JNIUtil::isJavaExceptionThrown())
        POP_AND_RETURN(SVN_NO_ERROR);

      mid = env->GetMethodID(clazz, "filter",
                             "(Ljava/lang/String;"
                             JAVAHL_ARG("/types/NodeKind;")
                             "Z)Z");
      if (JNIUtil::isJavaExceptionThrown() || mid == 0)
        POP_AND_RETURN(SVN_NO_ERROR);
    }

  // convert the parameters to their Java relatives
  jstring jpath = JNIUtil::makeJString(local_abspath);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN(SVN_NO_ERROR);

  jboolean jspecial = (dirent->special ? JNI_TRUE : JNI_FALSE);

  jobject jkind = EnumMapper::mapNodeKind(dirent->kind);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN(SVN_NO_ERROR);

  // call the Java method
  jboolean jfilter = env->CallBooleanMethod(m_callback, mid, jpath, jkind,
                                            jspecial);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN(SVN_NO_ERROR);

  *filtered = jfilter ? TRUE : FALSE;

  env->PopLocalFrame(NULL);
  return SVN_NO_ERROR;
}
