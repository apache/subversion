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
