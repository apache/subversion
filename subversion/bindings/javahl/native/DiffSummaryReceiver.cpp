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
 * @file DiffSummaryReceiver.cpp
 * @brief Implementation of the class DiffSummaryReceiver
 */

#include "JNIUtil.h"
#include "EnumMapper.h"
#include "DiffSummaryReceiver.h"

DiffSummaryReceiver::DiffSummaryReceiver(jobject jreceiver)
{
  m_receiver = jreceiver;
}

DiffSummaryReceiver::~DiffSummaryReceiver()
{
  // m_receiver does not need to be destroyed, because it is a
  // parameter to the Java SVNClient.diffSummarize() method, and
  // thus not explicitly managed.
}

svn_error_t *
DiffSummaryReceiver::summarize(const svn_client_diff_summarize_t *diff,
                               void *baton,
                               apr_pool_t *pool)
{
  if (baton)
    return ((DiffSummaryReceiver *) baton)->onSummary(diff, pool);

  return SVN_NO_ERROR;
}

svn_error_t *
DiffSummaryReceiver::onSummary(const svn_client_diff_summarize_t *diff,
                               apr_pool_t *pool)
{
  JNIEnv *env = JNIUtil::getEnv();
  jclass clazz;

  // As Java method IDs will not change during the time this library
  // is loaded, they can be cached.
  static jmethodID callback = 0;
  if (callback == 0)
    {
      // Initialize the method ID.
      clazz = env->FindClass(JAVA_PACKAGE "/DiffSummaryReceiver");
      if (JNIUtil::isJavaExceptionThrown())
        return SVN_NO_ERROR;

      callback = env->GetMethodID(clazz, "onSummary",
                                  "(Lorg/tigris/subversion/javahl/DiffSummary;)V");
      if (JNIUtil::isJavaExceptionThrown() || callback == 0)
        return SVN_NO_ERROR;

      env->DeleteLocalRef(clazz);
      if (JNIUtil::isJavaExceptionThrown())
        return SVN_NO_ERROR;
    }

  // Do some prep work for tranforming the DIFF parameter into a
  // Java equivalent.
  static jmethodID ctor = 0;
  clazz = env->FindClass(JAVA_PACKAGE "/DiffSummary");
  if (JNIUtil::isJavaExceptionThrown())
    return SVN_NO_ERROR;

  if (ctor == 0)
    {
      ctor = env->GetMethodID(clazz, "<init>",
                              "(Ljava/lang/String;IZI)V");
      if (JNIUtil::isJavaExceptionThrown() || ctor == 0)
        return SVN_NO_ERROR;
    }
  // Convert the arguments into their Java equivalent,
  jstring jPath = JNIUtil::makeJString(diff->path);
  if (JNIUtil::isJavaExceptionThrown())
    return SVN_NO_ERROR;

  jint jNodeKind = EnumMapper::mapNodeKind(diff->node_kind);
  // Actually tranform the DIFF parameter into a Java equivalent.
  jobject jDiffSummary = env->NewObject(clazz, ctor, jPath,
                                        (jint) diff->summarize_kind,
                                        (jboolean) diff->prop_changed,
                                        jNodeKind);
  if (JNIUtil::isJavaExceptionThrown() || jDiffSummary == NULL)
    return SVN_NO_ERROR;

  env->DeleteLocalRef(jPath);
  if (JNIUtil::isJavaExceptionThrown())
    return SVN_NO_ERROR;

  env->DeleteLocalRef(clazz);
  if (JNIUtil::isJavaExceptionThrown())
    return SVN_NO_ERROR;

  // Invoke the Java DiffSummaryReceiver callback.
  env->CallVoidMethod(m_receiver, callback, jDiffSummary);
  if (JNIUtil::isJavaExceptionThrown())
    return SVN_NO_ERROR;

  env->DeleteLocalRef(jDiffSummary);
  // We return whether an exception was thrown or not.

  return SVN_NO_ERROR;
}
