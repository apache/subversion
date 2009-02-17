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
 * @file StatusCallback.cpp
 * @brief Implementation of the class StatusCallback
 */

#include "StatusCallback.h"
#include "CreateJ.h"
#include "EnumMapper.h"
#include "JNIUtil.h"
#include "svn_time.h"
#include "../include/org_tigris_subversion_javahl_NodeKind.h"
#include "../include/org_tigris_subversion_javahl_Revision.h"
#include "../include/org_tigris_subversion_javahl_StatusKind.h"

/**
 * Create a StatusCallback object
 * @param jcallback the Java callback object.
 */
StatusCallback::StatusCallback(jobject jcallback)
{
  m_callback = jcallback;
}

/**
 * Destroy a StatusCallback object
 */
StatusCallback::~StatusCallback()
{
  // the m_callback does not need to be destroyed, because it is the passed
  // in parameter to the Java SVNClient.status method.
}

svn_error_t *
StatusCallback::callback(void *baton,
                         const char *path,
                         svn_wc_status2_t *status,
                         apr_pool_t *pool)
{
  if (baton)
    return ((StatusCallback *)baton)->doStatus(path, status);

  return SVN_NO_ERROR;
}

/**
 * Callback called for a single status item.
 */
svn_error_t *
StatusCallback::doStatus(const char *path, svn_wc_status2_t *status)
{
  JNIEnv *env = JNIUtil::getEnv();

  static jmethodID mid = 0; // the method id will not change during
  // the time this library is loaded, so
  // it can be cached.
  if (mid == 0)
    {
      jclass clazz = env->FindClass(JAVA_PACKAGE"/StatusCallback");
      if (JNIUtil::isJavaExceptionThrown())
        return SVN_NO_ERROR;

      mid = env->GetMethodID(clazz, "doStatus",
                             "(L"JAVA_PACKAGE"/Status;)V");
      if (JNIUtil::isJavaExceptionThrown() || mid == 0)
        return SVN_NO_ERROR;

      env->DeleteLocalRef(clazz);
      if (JNIUtil::isJavaExceptionThrown())
        return SVN_NO_ERROR;
    }

  jobject jStatus = createJavaStatus(path, status);
  if (JNIUtil::isJavaExceptionThrown())
    return SVN_NO_ERROR;

  env->CallVoidMethod(m_callback, mid, jStatus);
  if (JNIUtil::isJavaExceptionThrown())
    return SVN_NO_ERROR;

  env->DeleteLocalRef(jStatus);
  // We return here regardless of whether an exception is thrown or not,
  // so we do not need to explicitly check for one.
  return SVN_NO_ERROR;
}

jobject
StatusCallback::createJavaStatus(const char *path,
                                 svn_wc_status2_t *status)
{
  JNIEnv *env = JNIUtil::getEnv();
  jclass clazz = env->FindClass(JAVA_PACKAGE"/Status");
  if (JNIUtil::isJavaExceptionThrown())
    return NULL;

  static jmethodID mid = 0;
  if (mid == 0)
    {
      mid = env->GetMethodID(clazz, "<init>",
                             "(Ljava/lang/String;Ljava/lang/String;"
                             "IJJJLjava/lang/String;IIIIZZZ"
                             "L"JAVA_PACKAGE"/ConflictDescriptor;"
                             "Ljava/lang/String;Ljava/lang/String;"
                             "Ljava/lang/String;Ljava/lang/String;"
                             "JZZLjava/lang/String;Ljava/lang/String;"
                             "Ljava/lang/String;"
                             "JLorg/tigris/subversion/javahl/Lock;"
                             "JJILjava/lang/String;Ljava/lang/String;)V");
      if (JNIUtil::isJavaExceptionThrown())
        return NULL;
    }
  jstring jPath = JNIUtil::makeJString(path);
  if (JNIUtil::isJavaExceptionThrown())
    return NULL;

  jstring jUrl = NULL;
  jint jNodeKind = org_tigris_subversion_javahl_NodeKind_unknown;
  jlong jRevision = org_tigris_subversion_javahl_Revision_SVN_INVALID_REVNUM;
  jlong jLastChangedRevision =
    org_tigris_subversion_javahl_Revision_SVN_INVALID_REVNUM;
  jlong jLastChangedDate = 0;
  jstring jLastCommitAuthor = NULL;
  jint jTextType = org_tigris_subversion_javahl_StatusKind_none;
  jint jPropType = org_tigris_subversion_javahl_StatusKind_none;
  jint jRepositoryTextType = org_tigris_subversion_javahl_StatusKind_none;
  jint jRepositoryPropType = org_tigris_subversion_javahl_StatusKind_none;
  jboolean jIsLocked = JNI_FALSE;
  jboolean jIsCopied = JNI_FALSE;
  jboolean jIsSwitched = JNI_FALSE;
  jboolean jIsFileExternal = JNI_FALSE;
  jboolean jIsTreeConflicted = JNI_FALSE;
  jobject jConflictDescription = NULL;
  jstring jConflictOld = NULL;
  jstring jConflictNew = NULL;
  jstring jConflictWorking = NULL;
  jstring jURLCopiedFrom = NULL;
  jlong jRevisionCopiedFrom =
    org_tigris_subversion_javahl_Revision_SVN_INVALID_REVNUM;
  jstring jLockToken = NULL;
  jstring jLockComment = NULL;
  jstring jLockOwner = NULL;
  jlong jLockCreationDate = 0;
  jobject jLock = NULL;
  jlong jOODLastCmtRevision =
    org_tigris_subversion_javahl_Revision_SVN_INVALID_REVNUM;
  jlong jOODLastCmtDate = 0;
  jint jOODKind = org_tigris_subversion_javahl_NodeKind_none;
  jstring jOODLastCmtAuthor = NULL;
  jstring jChangelist = NULL;
  if (status != NULL)
    {
      jTextType = EnumMapper::mapStatusKind(status->text_status);
      jPropType = EnumMapper::mapStatusKind(status->prop_status);
      jRepositoryTextType = EnumMapper::mapStatusKind(
                                                      status->repos_text_status);
      jRepositoryPropType = EnumMapper::mapStatusKind(
                                                      status->repos_prop_status);
      jIsCopied = (status->copied == 1) ? JNI_TRUE: JNI_FALSE;
      jIsLocked = (status->locked == 1) ? JNI_TRUE: JNI_FALSE;
      jIsSwitched = (status->switched == 1) ? JNI_TRUE: JNI_FALSE;
      jIsFileExternal = (status->file_external == 1) ? JNI_TRUE: JNI_FALSE;
      jConflictDescription = CreateJ::ConflictDescriptor(status->tree_conflict);
      if (JNIUtil::isJavaExceptionThrown())
        return NULL;

      jIsTreeConflicted = (status->tree_conflict != NULL)
                             ? JNI_TRUE: JNI_FALSE;
      jLock = CreateJ::Lock(status->repos_lock);
      if (JNIUtil::isJavaExceptionThrown())
        return NULL;

      jUrl = JNIUtil::makeJString(status->url);
      if (JNIUtil::isJavaExceptionThrown())
        return NULL;

      jOODLastCmtRevision = status->ood_last_cmt_rev;
      jOODLastCmtDate = status->ood_last_cmt_date;
      jOODKind = EnumMapper::mapNodeKind(status->ood_kind);
      jOODLastCmtAuthor = JNIUtil::makeJString(status->ood_last_cmt_author);
      if (JNIUtil::isJavaExceptionThrown())
        return NULL;

      svn_wc_entry_t *entry = status->entry;
      if (entry != NULL)
        {
          jNodeKind = EnumMapper::mapNodeKind(entry->kind);
          jRevision = entry->revision;
          jLastChangedRevision = entry->cmt_rev;
          jLastChangedDate = entry->cmt_date;
          jLastCommitAuthor = JNIUtil::makeJString(entry->cmt_author);
          if (JNIUtil::isJavaExceptionThrown())
            return NULL;

          jConflictNew = JNIUtil::makeJString(entry->conflict_new);
          if (JNIUtil::isJavaExceptionThrown())
            return NULL;

          jConflictOld = JNIUtil::makeJString(entry->conflict_old);
          if (JNIUtil::isJavaExceptionThrown())
            return NULL;

          jConflictWorking= JNIUtil::makeJString(entry->conflict_wrk);
          if (JNIUtil::isJavaExceptionThrown())
            return NULL;

          jURLCopiedFrom = JNIUtil::makeJString(entry->copyfrom_url);
          if (JNIUtil::isJavaExceptionThrown())
            return NULL;

          jRevisionCopiedFrom = entry->copyfrom_rev;
          jLockToken = JNIUtil::makeJString(entry->lock_token);
          if (JNIUtil::isJavaExceptionThrown())
            return NULL;

          jLockComment = JNIUtil::makeJString(entry->lock_comment);
          if (JNIUtil::isJavaExceptionThrown())
            return NULL;

          jLockOwner = JNIUtil::makeJString(entry->lock_owner);
          if (JNIUtil::isJavaExceptionThrown())
            return NULL;

          jLockCreationDate = entry->lock_creation_date;

          jChangelist = JNIUtil::makeJString(entry->changelist);
          if (JNIUtil::isJavaExceptionThrown())
            return NULL;
        }
    }

  jobject ret = env->NewObject(clazz, mid, jPath, jUrl, jNodeKind, jRevision,
                               jLastChangedRevision, jLastChangedDate,
                               jLastCommitAuthor, jTextType, jPropType,
                               jRepositoryTextType, jRepositoryPropType,
                               jIsLocked, jIsCopied, jIsTreeConflicted,
                               jConflictDescription, jConflictOld, jConflictNew,
                               jConflictWorking, jURLCopiedFrom,
                               jRevisionCopiedFrom, jIsSwitched, jIsFileExternal,
                               jLockToken, jLockOwner,
                               jLockComment, jLockCreationDate, jLock,
                               jOODLastCmtRevision, jOODLastCmtDate,
                               jOODKind, jOODLastCmtAuthor, jChangelist);
  if (JNIUtil::isJavaExceptionThrown())
    return NULL;

  env->DeleteLocalRef(clazz);
  if (JNIUtil::isJavaExceptionThrown())
    return NULL;

  env->DeleteLocalRef(jPath);
  if (JNIUtil::isJavaExceptionThrown())
    return NULL;

  env->DeleteLocalRef(jUrl);
  if (JNIUtil::isJavaExceptionThrown())
    return NULL;

  env->DeleteLocalRef(jLastCommitAuthor);
  if (JNIUtil::isJavaExceptionThrown())
    return NULL;

  env->DeleteLocalRef(jConflictNew);
  if (JNIUtil::isJavaExceptionThrown())
    return NULL;

  env->DeleteLocalRef(jConflictOld);
  if (JNIUtil::isJavaExceptionThrown())
    return NULL;

  env->DeleteLocalRef(jConflictWorking);
  if (JNIUtil::isJavaExceptionThrown())
    return NULL;

  env->DeleteLocalRef(jURLCopiedFrom);
  if (JNIUtil::isJavaExceptionThrown())
    return NULL;

  env->DeleteLocalRef(jLockComment);
  if (JNIUtil::isJavaExceptionThrown())
    return NULL;

  env->DeleteLocalRef(jLockOwner);
  if (JNIUtil::isJavaExceptionThrown())
    return NULL;

  env->DeleteLocalRef(jLockToken);
  if (JNIUtil::isJavaExceptionThrown())
    return NULL;

  env->DeleteLocalRef(jLock);
  if (JNIUtil::isJavaExceptionThrown())
    return NULL;

  env->DeleteLocalRef(jOODLastCmtAuthor);
  if (JNIUtil::isJavaExceptionThrown())
    return NULL;

  env->DeleteLocalRef(jChangelist);
  if (JNIUtil::isJavaExceptionThrown())
    return NULL;

  return ret;
}
