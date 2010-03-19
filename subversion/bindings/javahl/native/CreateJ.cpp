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
 * @file ConflictResolverCallback.cpp
 * @brief Implementation of the class ConflictResolverCallback.
 */

#include "svn_error.h"

#include "JNIUtil.h"
#include "JNIStringHolder.h"
#include "EnumMapper.h"
#include "RevisionRange.h"
#include "CreateJ.h"
#include "../include/org_apache_subversion_javahl_Revision.h"

jobject
CreateJ::ConflictDescriptor(const svn_wc_conflict_description_t *desc)
{
  JNIEnv *env = JNIUtil::getEnv();

  if (desc == NULL)
    return NULL;

  // Create a local frame for our references
  env->PushLocalFrame(LOCAL_FRAME_SIZE);
  if (JNIUtil::isJavaExceptionThrown())
    return NULL;

  // Create an instance of the conflict descriptor.
  jclass clazz = env->FindClass(JAVA_PACKAGE "/ConflictDescriptor");
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  static jmethodID ctor = 0;
  if (ctor == 0)
    {
      ctor = env->GetMethodID(clazz, "<init>", "(Ljava/lang/String;"
                              "L"JAVA_PACKAGE"/ConflictDescriptor$Kind;"
                              "L"JAVA_PACKAGE"/NodeKind;"
                              "Ljava/lang/String;ZLjava/lang/String;"
                              "L"JAVA_PACKAGE"/ConflictDescriptor$Action;"
                              "L"JAVA_PACKAGE"/ConflictDescriptor$Reason;I"
                              "Ljava/lang/String;Ljava/lang/String;"
                              "Ljava/lang/String;Ljava/lang/String;"
                              "L"JAVA_PACKAGE"/ConflictVersion;"
                              "L"JAVA_PACKAGE"/ConflictVersion;)V");
      if (JNIUtil::isJavaExceptionThrown() || ctor == 0)
        POP_AND_RETURN_NULL;
    }

  jstring jpath = JNIUtil::makeJString(desc->path);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;
  jstring jpropertyName = JNIUtil::makeJString(desc->property_name);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;
  jstring jmimeType = JNIUtil::makeJString(desc->mime_type);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;
  jstring jbasePath = JNIUtil::makeJString(desc->base_file);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;
  jstring jreposPath = JNIUtil::makeJString(desc->their_file);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;
  jstring juserPath = JNIUtil::makeJString(desc->my_file);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;
  jstring jmergedPath = JNIUtil::makeJString(desc->merged_file);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;
  jobject jsrcLeft = CreateJ::ConflictVersion(desc->src_left_version);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;
  jobject jsrcRight = ConflictVersion(desc->src_right_version);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;
  jobject jnodeKind = EnumMapper::mapNodeKind(desc->node_kind);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;
  jobject jconflictKind = EnumMapper::mapConflictKind(desc->kind);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;
  jobject jconflictAction = EnumMapper::mapConflictAction(desc->action);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;
  jobject jconflictReason = EnumMapper::mapConflictReason(desc->reason);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  // Instantiate the conflict descriptor.
  jobject jdesc = env->NewObject(clazz, ctor, jpath, jconflictKind,
                                 jnodeKind, jpropertyName,
                                 (jboolean) desc->is_binary, jmimeType,
                                 jconflictAction, jconflictReason,
                                 EnumMapper::mapOperation(desc->operation),
                                 jbasePath, jreposPath, juserPath,
                                 jmergedPath, jsrcLeft, jsrcRight);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  return env->PopLocalFrame(jdesc);
}

jobject
CreateJ::ConflictVersion(const svn_wc_conflict_version_t *version)
{
  JNIEnv *env = JNIUtil::getEnv();

  if (version == NULL)
    return NULL;

  // Create a local frame for our references
  env->PushLocalFrame(LOCAL_FRAME_SIZE);
  if (JNIUtil::isJavaExceptionThrown())
    return NULL;

  // Create an instance of the conflict version.
  jclass clazz = env->FindClass(JAVA_PACKAGE "/ConflictVersion");
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  static jmethodID ctor = 0;
  if (ctor == 0)
    {
      ctor = env->GetMethodID(clazz, "<init>", "(Ljava/lang/String;J"
                                               "Ljava/lang/String;"
                                               "L"JAVA_PACKAGE"/NodeKind;)V");
      if (JNIUtil::isJavaExceptionThrown() || ctor == 0)
        POP_AND_RETURN_NULL;
    }

  jstring jreposURL = JNIUtil::makeJString(version->repos_url);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;
  jstring jpathInRepos = JNIUtil::makeJString(version->path_in_repos);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;
  jobject jnodeKind = EnumMapper::mapNodeKind(version->node_kind);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  jobject jversion = env->NewObject(clazz, ctor, jreposURL,
                                    (jlong)version->peg_rev, jpathInRepos,
                                    jnodeKind);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  return env->PopLocalFrame(jversion);
}

jobject
CreateJ::Info(const svn_wc_entry_t *entry)
{
  if (entry == NULL)
    return NULL;

  JNIEnv *env = JNIUtil::getEnv();

  // Create a local frame for our references
  env->PushLocalFrame(LOCAL_FRAME_SIZE);
  if (JNIUtil::isJavaExceptionThrown())
    return NULL;

  jclass clazz = env->FindClass(JAVA_PACKAGE"/Info");
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  static jmethodID mid = 0;
  if (mid == 0)
    {
        mid = env->GetMethodID(clazz, "<init>",
                               "(Ljava/lang/String;Ljava/lang/String;"
                               "Ljava/lang/String;Ljava/lang/String;I"
                               "L"JAVA_PACKAGE"/NodeKind;"
                               "Ljava/lang/String;JJLjava/util/Date;"
                               "Ljava/util/Date;Ljava/util/Date;"
                               "ZZZZJLjava/lang/String;)V");
        if (JNIUtil::isJavaExceptionThrown())
          POP_AND_RETURN_NULL;
    }

  jstring jName = JNIUtil::makeJString(entry->name);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;
  jstring jUrl = JNIUtil::makeJString(entry->url);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;
  jstring jUuid = JNIUtil::makeJString(entry->uuid);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;
  jstring jRepository = JNIUtil::makeJString(entry->repos);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;
  jint jSchedule = EnumMapper::mapScheduleKind(entry->schedule);
  jobject jNodeKind = EnumMapper::mapNodeKind(entry->kind);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;
  jstring jAuthor = JNIUtil::makeJString(entry->cmt_author);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;
  jlong jRevision = entry->revision;
  jlong jLastChangedRevision = entry->cmt_rev;
  jobject jLastChangedDate = JNIUtil::createDate(entry->cmt_date);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;
  jobject jLastDateTextUpdate = JNIUtil::createDate(entry->text_time);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;
  jobject jLastDatePropsUpdate = JNIUtil::createDate(entry->prop_time);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;
  jboolean jCopied = entry->copied ? JNI_TRUE : JNI_FALSE;
  jboolean jDeleted = entry->deleted ? JNI_TRUE : JNI_FALSE;
  jboolean jAbsent = entry->absent ? JNI_TRUE : JNI_FALSE;
  jboolean jIncomplete = entry->incomplete ? JNI_TRUE : JNI_FALSE;
  jlong jCopyRev = entry->copyfrom_rev;
  jstring jCopyUrl = JNIUtil::makeJString(entry->copyfrom_url);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  jobject jinfo = env->NewObject(clazz, mid, jName, jUrl, jUuid, jRepository,
                                 jSchedule, jNodeKind, jAuthor, jRevision,
                                 jLastChangedRevision, jLastChangedDate,
                                 jLastDateTextUpdate, jLastDatePropsUpdate,
                                 jCopied, jDeleted, jAbsent, jIncomplete,
                                 jCopyRev, jCopyUrl);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  return env->PopLocalFrame(jinfo);
}

jobject
CreateJ::Info2(const char *path, const svn_info_t *info)
{
  JNIEnv *env = JNIUtil::getEnv();

  // Create a local frame for our references
  env->PushLocalFrame(LOCAL_FRAME_SIZE);
  if (JNIUtil::isJavaExceptionThrown())
    return NULL;

  jclass clazz = env->FindClass(JAVA_PACKAGE "/Info2");
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  static jmethodID mid = 0;
  if (mid == 0)
    {
      mid = env->GetMethodID(clazz, "<init>",
                             "(Ljava/lang/String;Ljava/lang/String;J"
                             "L"JAVA_PACKAGE"/NodeKind;"
                             "Ljava/lang/String;Ljava/lang/String;"
                             "JJLjava/lang/String;"
                             "L"JAVA_PACKAGE"/Lock;"
                             "ZILjava/lang/String;JJJ"
                             "Ljava/lang/String;Ljava/lang/String;"
                             "Ljava/lang/String;Ljava/lang/String;"
                             "Ljava/lang/String;Ljava/lang/String;JJ"
                             "L"JAVA_PACKAGE"/Depth;"
                             "L"JAVA_PACKAGE"/ConflictDescriptor;)V");
      if (mid == 0 || JNIUtil::isJavaExceptionThrown())
        POP_AND_RETURN_NULL;
    }

  jstring jpath = JNIUtil::makeJString(path);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  jstring jurl = JNIUtil::makeJString(info->URL);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  jstring jreposRootUrl = JNIUtil::makeJString(info->repos_root_URL);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  jstring jreportUUID = JNIUtil::makeJString(info->repos_UUID);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  jstring jlastChangedAuthor =
    JNIUtil::makeJString(info->last_changed_author);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  jobject jlock = CreateJ::Lock(info->lock);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  jstring jcopyFromUrl = JNIUtil::makeJString(info->copyfrom_url);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  jstring jchecksum = JNIUtil::makeJString(info->checksum);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  jstring jconflictOld = JNIUtil::makeJString(info->conflict_old);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  jstring jconflictNew = JNIUtil::makeJString(info->conflict_new);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  jstring jconflictWrk = JNIUtil::makeJString(info->conflict_wrk);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  jstring jprejfile = JNIUtil::makeJString(info->prejfile);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  jstring jchangelist = JNIUtil::makeJString(info->changelist);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  jobject jdesc = CreateJ::ConflictDescriptor(info->tree_conflict);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  jobject jnodeKind = EnumMapper::mapNodeKind(info->kind);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  jlong jworkingSize = info->working_size == SVN_INFO_SIZE_UNKNOWN
    ? -1 : (jlong) info->working_size;
  jlong jreposSize = info->size == SVN_INFO_SIZE_UNKNOWN
    ? -1 : (jlong) info->size;

  jobject jinfo2 = env->NewObject(clazz, mid, jpath, jurl, (jlong) info->rev,
                                  jnodeKind, jreposRootUrl, jreportUUID,
                                  (jlong) info->last_changed_rev,
                                  (jlong) info->last_changed_date,
                                  jlastChangedAuthor, jlock,
                                  info->has_wc_info ? JNI_TRUE : JNI_FALSE,
                                  EnumMapper::mapScheduleKind(info->schedule),
                                  jcopyFromUrl, (jlong) info->copyfrom_rev,
                                  (jlong) info->text_time,
                                  (jlong) info->prop_time, jchecksum,
                                  jconflictOld, jconflictNew, jconflictWrk,
                                  jprejfile, jchangelist,
                                  jworkingSize, jreposSize,
                                  EnumMapper::mapDepth(info->depth), jdesc);

  return env->PopLocalFrame(jinfo2);
}

jobject
CreateJ::Lock(const svn_lock_t *lock)
{
  if (lock == NULL)
    return NULL;

  JNIEnv *env = JNIUtil::getEnv();

  // Create a local frame for our references
  env->PushLocalFrame(LOCAL_FRAME_SIZE);
  if (JNIUtil::isJavaExceptionThrown())
    return NULL;

  jclass clazz = env->FindClass(JAVA_PACKAGE"/Lock");
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  static jmethodID mid = 0;
  if (mid == 0)
    {
      mid = env->GetMethodID(clazz, "<init>",
                             "(Ljava/lang/String;Ljava/lang/String;"
                             "Ljava/lang/String;"
                             "Ljava/lang/String;JJ)V");
      if (JNIUtil::isJavaExceptionThrown())
        POP_AND_RETURN_NULL;
    }

  jstring jOwner = JNIUtil::makeJString(lock->owner);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;
  jstring jPath = JNIUtil::makeJString(lock->path);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;
  jstring jToken = JNIUtil::makeJString(lock->token);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;
  jstring jComment = JNIUtil::makeJString(lock->comment);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  jlong jCreationDate = lock->creation_date;
  jlong jExpirationDate = lock->expiration_date;
  jobject jlock = env->NewObject(clazz, mid, jOwner, jPath, jToken, jComment,
                                 jCreationDate, jExpirationDate);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  return env->PopLocalFrame(jlock);
}

jobject
CreateJ::Status(const char *local_abspath, const svn_wc_status2_t *status)
{
  JNIEnv *env = JNIUtil::getEnv();

  // Create a local frame for our references
  env->PushLocalFrame(LOCAL_FRAME_SIZE);
  if (JNIUtil::isJavaExceptionThrown())
    return NULL;

  jclass clazz = env->FindClass(JAVA_PACKAGE"/Status");
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  static jmethodID mid = 0;
  if (mid == 0)
    {
      mid = env->GetMethodID(clazz, "<init>",
                             "(Ljava/lang/String;Ljava/lang/String;"
                             "L"JAVA_PACKAGE"/NodeKind;"
                             "JJJLjava/lang/String;"
                             "L"JAVA_PACKAGE"/Status$Kind;"
                             "L"JAVA_PACKAGE"/Status$Kind;"
                             "L"JAVA_PACKAGE"/Status$Kind;"
                             "L"JAVA_PACKAGE"/Status$Kind;"
                             "ZZZL"JAVA_PACKAGE"/ConflictDescriptor;"
                             "Ljava/lang/String;Ljava/lang/String;"
                             "Ljava/lang/String;Ljava/lang/String;"
                             "JZZLjava/lang/String;Ljava/lang/String;"
                             "Ljava/lang/String;"
                             "JL"JAVA_PACKAGE"/Lock;"
                             "JJL"JAVA_PACKAGE"/NodeKind;"
                             "Ljava/lang/String;Ljava/lang/String;)V");
      if (JNIUtil::isJavaExceptionThrown())
        POP_AND_RETURN_NULL;
    }
  jstring jPath = JNIUtil::makeJString(local_abspath);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  jstring jUrl = NULL;
  jobject jNodeKind = NULL;
  jlong jRevision = org_apache_subversion_javahl_Revision_SVN_INVALID_REVNUM;
  jlong jLastChangedRevision =
    org_apache_subversion_javahl_Revision_SVN_INVALID_REVNUM;
  jlong jLastChangedDate = 0;
  jstring jLastCommitAuthor = NULL;
  jobject jTextType = NULL;
  jobject jPropType = NULL;
  jobject jRepositoryTextType = NULL;
  jobject jRepositoryPropType = NULL;
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
    org_apache_subversion_javahl_Revision_SVN_INVALID_REVNUM;
  jstring jLockToken = NULL;
  jstring jLockComment = NULL;
  jstring jLockOwner = NULL;
  jlong jLockCreationDate = 0;
  jobject jLock = NULL;
  jlong jOODLastCmtRevision =
    org_apache_subversion_javahl_Revision_SVN_INVALID_REVNUM;
  jlong jOODLastCmtDate = 0;
  jobject jOODKind = NULL;
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
        POP_AND_RETURN_NULL;

      jIsTreeConflicted = (status->tree_conflict != NULL)
                             ? JNI_TRUE: JNI_FALSE;
      jLock = CreateJ::Lock(status->repos_lock);
      if (JNIUtil::isJavaExceptionThrown())
        POP_AND_RETURN_NULL;

      jUrl = JNIUtil::makeJString(status->url);
      if (JNIUtil::isJavaExceptionThrown())
        POP_AND_RETURN_NULL;

      jOODLastCmtRevision = status->ood_last_cmt_rev;
      jOODLastCmtDate = status->ood_last_cmt_date;
      jOODKind = EnumMapper::mapNodeKind(status->ood_kind);
      jOODLastCmtAuthor = JNIUtil::makeJString(status->ood_last_cmt_author);
      if (JNIUtil::isJavaExceptionThrown())
        POP_AND_RETURN_NULL;

      const svn_wc_entry_t *entry = status->entry;
      if (entry != NULL)
        {
          jNodeKind = EnumMapper::mapNodeKind(entry->kind);
          jRevision = entry->revision;
          jLastChangedRevision = entry->cmt_rev;
          jLastChangedDate = entry->cmt_date;
          jLastCommitAuthor = JNIUtil::makeJString(entry->cmt_author);
          if (JNIUtil::isJavaExceptionThrown())
            POP_AND_RETURN_NULL;

          jConflictNew = JNIUtil::makeJString(entry->conflict_new);
          if (JNIUtil::isJavaExceptionThrown())
            POP_AND_RETURN_NULL;

          jConflictOld = JNIUtil::makeJString(entry->conflict_old);
          if (JNIUtil::isJavaExceptionThrown())
            POP_AND_RETURN_NULL;

          jConflictWorking= JNIUtil::makeJString(entry->conflict_wrk);
          if (JNIUtil::isJavaExceptionThrown())
            POP_AND_RETURN_NULL;

          jURLCopiedFrom = JNIUtil::makeJString(entry->copyfrom_url);
          if (JNIUtil::isJavaExceptionThrown())
            POP_AND_RETURN_NULL;

          jRevisionCopiedFrom = entry->copyfrom_rev;
          jLockToken = JNIUtil::makeJString(entry->lock_token);
          if (JNIUtil::isJavaExceptionThrown())
            POP_AND_RETURN_NULL;

          jLockComment = JNIUtil::makeJString(entry->lock_comment);
          if (JNIUtil::isJavaExceptionThrown())
            POP_AND_RETURN_NULL;

          jLockOwner = JNIUtil::makeJString(entry->lock_owner);
          if (JNIUtil::isJavaExceptionThrown())
            POP_AND_RETURN_NULL;

          jLockCreationDate = entry->lock_creation_date;

          jChangelist = JNIUtil::makeJString(entry->changelist);
          if (JNIUtil::isJavaExceptionThrown())
            POP_AND_RETURN_NULL;
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

  return env->PopLocalFrame(ret);
}

jobject
CreateJ::NotifyInformation(const svn_wc_notify_t *wcNotify)
{
  JNIEnv *env = JNIUtil::getEnv();

  // Create a local frame for our references
  env->PushLocalFrame(LOCAL_FRAME_SIZE);
  if (JNIUtil::isJavaExceptionThrown())
    return NULL;

  static jmethodID midCT = 0;
  jclass clazz = env->FindClass(JAVA_PACKAGE"/NotifyInformation");
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  if (midCT == 0)
    {
      midCT = env->GetMethodID(clazz, "<init>",
                               "(Ljava/lang/String;"
                               "L"JAVA_PACKAGE"/NotifyInformation$Action;"
                               "L"JAVA_PACKAGE"/NodeKind;Ljava/lang/String;"
                               "L"JAVA_PACKAGE"/Lock;"
                               "Ljava/lang/String;"
                               "L"JAVA_PACKAGE"/NotifyInformation$Status;"
                               "L"JAVA_PACKAGE"/NotifyInformation$Status;"
                               "L"JAVA_PACKAGE"/NotifyInformation$LockStatus;"
                               "JLjava/lang/String;"
                               "L"JAVA_PACKAGE"/RevisionRange;"
                               "Ljava/lang/String;)V");
      if (JNIUtil::isJavaExceptionThrown() || midCT == 0)
        POP_AND_RETURN_NULL;
    }

  // convert the parameter to their Java relatives
  jstring jPath = JNIUtil::makeJString(wcNotify->path);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  jobject jAction = EnumMapper::mapNotifyAction(wcNotify->action);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  jobject jKind = EnumMapper::mapNodeKind(wcNotify->kind);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  jstring jMimeType = JNIUtil::makeJString(wcNotify->mime_type);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  jobject jLock = CreateJ::Lock(wcNotify->lock);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  jstring jErr = JNIUtil::makeSVNErrorMessage(wcNotify->err);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  jobject jContentState = EnumMapper::mapNotifyState(wcNotify->content_state);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  jobject jPropState = EnumMapper::mapNotifyState(wcNotify->prop_state);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  jobject jLockState = EnumMapper::mapNotifyLockState(wcNotify->lock_state);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  jstring jChangelistName = JNIUtil::makeJString(wcNotify->changelist_name);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  jobject jMergeRange = NULL;
  if (wcNotify->merge_range)
    {
      jMergeRange = RevisionRange::makeJRevisionRange(wcNotify->merge_range);
      if (jMergeRange == NULL)
        POP_AND_RETURN_NULL;
    }

  jstring jpathPrefix = JNIUtil::makeJString(wcNotify->path_prefix);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  // call the Java method
  jobject jInfo = env->NewObject(clazz, midCT, jPath, jAction,
                                 jKind, jMimeType, jLock, jErr,
                                 jContentState, jPropState, jLockState,
                                 (jlong) wcNotify->revision, jChangelistName,
                                 jMergeRange, jpathPrefix);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  return env->PopLocalFrame(jInfo);
}

jobject
CreateJ::RevisionRangeList(apr_array_header_t *ranges)
{
  JNIEnv *env = JNIUtil::getEnv();

  // Create a local frame for our references
  env->PushLocalFrame(LOCAL_FRAME_SIZE);
  if (JNIUtil::isJavaExceptionThrown())
    return NULL;

  jclass clazz = env->FindClass("java/util/ArrayList");
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  static jmethodID init_mid = 0;
  if (init_mid == 0)
    {
      init_mid = env->GetMethodID(clazz, "<init>", "()V");
      if (JNIUtil::isJavaExceptionThrown())
        POP_AND_RETURN_NULL;
    }

  static jmethodID add_mid = 0;
  if (add_mid == 0)
    {
      add_mid = env->GetMethodID(clazz, "add", "(Ljava/lang/Object;)Z");
      if (JNIUtil::isJavaExceptionThrown())
        POP_AND_RETURN_NULL;
    }

  jobject jranges = env->NewObject(clazz, init_mid);

  for (int i = 0; i < ranges->nelts; ++i)
    {
      // Convert svn_merge_range_t *'s to Java RevisionRange objects.
      svn_merge_range_t *range =
          APR_ARRAY_IDX(ranges, i, svn_merge_range_t *);

      jobject jrange = RevisionRange::makeJRevisionRange(range);
      if (JNIUtil::isJavaExceptionThrown())
        POP_AND_RETURN_NULL;

      env->CallObjectMethod(jranges, add_mid, jrange);
      if (JNIUtil::isJavaExceptionThrown())
        POP_AND_RETURN_NULL;

      env->DeleteLocalRef(jrange);
      if (JNIUtil::isJavaExceptionThrown())
        POP_AND_RETURN_NULL;
    }

  return env->PopLocalFrame(jranges);
}

jobject
CreateJ::StringSet(apr_array_header_t *strings)
{
  JNIEnv *env = JNIUtil::getEnv();
  std::vector<jobject> jstrs;

  for (int i = 0; i < strings->nelts; ++i)
    {
      const char *str = APR_ARRAY_IDX(strings, i, const char *);
      jstring jstr = JNIUtil::makeJString(str);
      if (JNIUtil::isJavaExceptionThrown())
        return NULL;

      jstrs.push_back(jstr);
    }

  return CreateJ::Set(jstrs);
}

jobject CreateJ::PropertyMap(apr_hash_t *prop_hash, apr_pool_t *pool)
{
  JNIEnv *env = JNIUtil::getEnv();

  // Create a local frame for our references
  env->PushLocalFrame(LOCAL_FRAME_SIZE);
  if (JNIUtil::isJavaExceptionThrown())
    return NULL;

  jclass clazz = env->FindClass("java/util/HashMap");
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  static jmethodID init_mid = 0;
  if (init_mid == 0)
    {
      init_mid = env->GetMethodID(clazz, "<init>", "()V");
      if (JNIUtil::isJavaExceptionThrown())
        POP_AND_RETURN_NULL;
    }

  static jmethodID put_mid = 0;
  if (put_mid == 0)
    {
      put_mid = env->GetMethodID(clazz, "put",
                                 "(Ljava/lang/Object;Ljava/lang/Object;)"
                                 "Ljava/lang/Object;");
      if (JNIUtil::isJavaExceptionThrown())
        POP_AND_RETURN_NULL;
    }

  jobject map = env->NewObject(clazz, init_mid);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  apr_hash_index_t *hi;
  int i = 0;
  for (hi = apr_hash_first(pool, prop_hash); hi; hi = apr_hash_next(hi), ++i)
    {
      const char *key;
      svn_string_t *val;

      apr_hash_this(hi, (const void **)&key, NULL, (void **)&val);

      jstring jpropName = JNIUtil::makeJString(key);
      if (JNIUtil::isJavaExceptionThrown())
        POP_AND_RETURN_NULL;

      jbyteArray jpropVal = JNIUtil::makeJByteArray(
                                    (const signed char *)val->data, val->len);
      if (JNIUtil::isJavaExceptionThrown())
        POP_AND_RETURN_NULL;

      env->CallObjectMethod(map, put_mid, jpropName, jpropVal);
      if (JNIUtil::isJavaExceptionThrown())
        POP_AND_RETURN_NULL;

      env->DeleteLocalRef(jpropName);
      if (JNIUtil::isJavaExceptionThrown())
        POP_AND_RETURN_NULL;

      env->DeleteLocalRef(jpropVal);
      if (JNIUtil::isJavaExceptionThrown())
        POP_AND_RETURN_NULL;
    }

  return env->PopLocalFrame(map);
}

jobject CreateJ::Set(std::vector<jobject> &objects)
{
  JNIEnv *env = JNIUtil::getEnv();

  // Create a local frame for our references
  env->PushLocalFrame(LOCAL_FRAME_SIZE);
  if (JNIUtil::isJavaExceptionThrown())
    return NULL;

  jclass clazz = env->FindClass("java/util/HashSet");
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  static jmethodID init_mid = 0;
  if (init_mid == 0)
    {
      init_mid = env->GetMethodID(clazz, "<init>", "()V");
      if (JNIUtil::isJavaExceptionThrown())
        POP_AND_RETURN_NULL;
    }

  static jmethodID add_mid = 0;
  if (add_mid == 0)
    {
      add_mid = env->GetMethodID(clazz, "add", "(Ljava/lang/Object;)Z");
      if (JNIUtil::isJavaExceptionThrown())
        POP_AND_RETURN_NULL;
    }

  jobject set = env->NewObject(clazz, init_mid);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  std::vector<jobject>::const_iterator it;
  for (it = objects.begin(); it < objects.end(); ++it)
    {
      jobject jthing = *it;

      env->CallObjectMethod(set, add_mid, jthing);
      if (JNIUtil::isJavaExceptionThrown())
        POP_AND_RETURN_NULL;

      env->DeleteLocalRef(jthing);
      if (JNIUtil::isJavaExceptionThrown())
        POP_AND_RETURN_NULL;
    }

  return env->PopLocalFrame(set);
}
