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
 * @file ConflictResolverCallback.cpp
 * @brief Implementation of the class ConflictResolverCallback.
 */

#include "svn_error.h"

#include "../include/org_tigris_subversion_javahl_ConflictResult.h"
#include "JNIUtil.h"
#include "JNIStringHolder.h"
#include "EnumMapper.h"
#include "CreateJ.h"
#include "ConflictResolverCallback.h"

ConflictResolverCallback::ConflictResolverCallback(jobject jconflictResolver)
{
  m_conflictResolver = jconflictResolver;
}

ConflictResolverCallback::~ConflictResolverCallback()
{
  if (m_conflictResolver != NULL)
    {
      JNIEnv *env = JNIUtil::getEnv();
      env->DeleteGlobalRef(m_conflictResolver);
    }
}

ConflictResolverCallback *
ConflictResolverCallback::makeCConflictResolverCallback(jobject jconflictResolver)
{
  if (jconflictResolver == NULL)
    return NULL;

  JNIEnv *env = JNIUtil::getEnv();

  // Sanity check that the object implements the ConflictResolverCallback
  // Java interface.
  jclass clazz = env->FindClass(JAVA_PACKAGE "/ConflictResolverCallback");
  if (JNIUtil::isJavaExceptionThrown())
    return NULL;

  if (!env->IsInstanceOf(jconflictResolver, clazz))
    {
      env->DeleteLocalRef(clazz);
      return NULL;
    }
  env->DeleteLocalRef(clazz);
  if (JNIUtil::isJavaExceptionThrown())
    return NULL;

  // Retain a global reference to our Java peer.
  jobject myListener = env->NewGlobalRef(jconflictResolver);
  if (JNIUtil::isJavaExceptionThrown())
    return NULL;

  // Create the peer.
  return new ConflictResolverCallback(myListener);
}

svn_error_t *
ConflictResolverCallback::resolveConflict(svn_wc_conflict_result_t **result,
                                          const svn_wc_conflict_description_t *
                                          desc,
                                          void *baton,
                                          apr_pool_t *pool)
{
  if (baton)
    return ((ConflictResolverCallback *) baton)->resolve(result, desc, pool);
  else
    return SVN_NO_ERROR;
}

svn_error_t *
ConflictResolverCallback::resolve(svn_wc_conflict_result_t **result,
                                  const svn_wc_conflict_description_t *desc,
                                  apr_pool_t *pool)
{
  JNIEnv *env = JNIUtil::getEnv();

  // As Java method IDs will not change during the time this library
  // is loaded, they can be cached.
  static jmethodID mid = 0;
  if (mid == 0)
    {
      // Initialize the callback method ID.
      jclass clazz = env->FindClass(JAVA_PACKAGE "/ConflictResolverCallback");
      if (JNIUtil::isJavaExceptionThrown())
        return SVN_NO_ERROR;

      mid = env->GetMethodID(clazz, "resolve",
                             "(L" JAVA_PACKAGE "/ConflictDescriptor;)"
                             "L" JAVA_PACKAGE "/ConflictResult;");
      if (JNIUtil::isJavaExceptionThrown() || mid == 0)
        return SVN_NO_ERROR;

      env->DeleteLocalRef(clazz);
      if (JNIUtil::isJavaExceptionThrown())
        return SVN_NO_ERROR;
    }

  // Create an instance of the conflict descriptor.
  jobject jdesc = CreateJ::ConflictDescriptor(desc);
  if (JNIUtil::isJavaExceptionThrown())
    return SVN_NO_ERROR;

  // Invoke the Java conflict resolver callback method using the descriptor.
  jobject jresult = env->CallObjectMethod(m_conflictResolver, mid, jdesc);
  if (JNIUtil::isJavaExceptionThrown())
    {
      // If an exception is thrown by our conflict resolver, remove it
      // from the JNI env, and convert it into a Subversion error.
      const char *msg = JNIUtil::thrownExceptionToCString();
      return svn_error_create(SVN_ERR_WC_CONFLICT_RESOLVER_FAILURE, NULL, msg);
    }
  *result = javaResultToC(jresult, pool);
  if (*result == NULL)
    // Unable to convert the result into a C representation.
    return svn_error_create(SVN_ERR_WC_CONFLICT_RESOLVER_FAILURE, NULL, NULL);

  env->DeleteLocalRef(jdesc);
  if (JNIUtil::isJavaExceptionThrown())
    return SVN_NO_ERROR;

  return SVN_NO_ERROR;
}

svn_wc_conflict_result_t *
ConflictResolverCallback::javaResultToC(jobject jresult, apr_pool_t *pool)
{
  JNIEnv *env = JNIUtil::getEnv();
  static jmethodID getChoice = 0;
  static jmethodID getMergedPath = 0;

  jclass clazz = NULL;
  if (getChoice == 0 || getMergedPath == 0)
    {
      clazz = env->FindClass(JAVA_PACKAGE "/ConflictResult");
      if (JNIUtil::isJavaExceptionThrown())
        return NULL;
    }

  if (getChoice == 0)
    {
      getChoice = env->GetMethodID(clazz, "getChoice", "()I");
      if (JNIUtil::isJavaExceptionThrown() || getChoice == 0)
        return NULL;
    }
  if (getMergedPath == 0)
    {
      getMergedPath = env->GetMethodID(clazz, "getMergedPath",
                                       "()Ljava/lang/String;");
      if (JNIUtil::isJavaExceptionThrown() || getMergedPath == 0)
        return NULL;
    }

  if (clazz)
    {
      env->DeleteLocalRef(clazz);
      if (JNIUtil::isJavaExceptionThrown())
        return NULL;
    }

  jint jchoice = env->CallIntMethod(jresult, getChoice);
  if (JNIUtil::isJavaExceptionThrown())
    return NULL;

  jstring jmergedPath =
    (jstring) env->CallObjectMethod(jresult, getMergedPath);
  if (JNIUtil::isJavaExceptionThrown())
    return NULL;
  JNIStringHolder mergedPath(jmergedPath);

  return svn_wc_create_conflict_result(javaChoiceToC(jchoice),
                                       mergedPath.pstrdup(pool),
                                       pool);
}

svn_wc_conflict_choice_t ConflictResolverCallback::javaChoiceToC(jint jchoice)
{
  switch (jchoice)
    {
    case org_tigris_subversion_javahl_ConflictResult_postpone:
    default:
      return svn_wc_conflict_choose_postpone;
    case org_tigris_subversion_javahl_ConflictResult_chooseBase:
      return svn_wc_conflict_choose_base;
    case org_tigris_subversion_javahl_ConflictResult_chooseTheirsFull:
      return svn_wc_conflict_choose_theirs_full;
    case org_tigris_subversion_javahl_ConflictResult_chooseMineFull:
      return svn_wc_conflict_choose_mine_full;
    case org_tigris_subversion_javahl_ConflictResult_chooseTheirsConflict:
      return svn_wc_conflict_choose_theirs_conflict;
    case org_tigris_subversion_javahl_ConflictResult_chooseMineConflict:
      return svn_wc_conflict_choose_mine_conflict;
    case org_tigris_subversion_javahl_ConflictResult_chooseMerged:
      return svn_wc_conflict_choose_merged;
    }
}
