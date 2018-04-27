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
 * @file CreateJ.cpp
 * @brief Implementation of the class CreateJ.
 */

#include "svn_error.h"

#include "JNIUtil.h"
#include "JNIStringHolder.h"
#include "EnumMapper.h"
#include "RevisionRange.h"
#include "RevisionRangeList.h"
#include "CreateJ.h"
#include "../include/org_apache_subversion_javahl_types_Revision.h"
#include "../include/org_apache_subversion_javahl_CommitItemStateFlags.h"

#include "svn_path.h"
#include "svn_props.h"
#include "svn_mergeinfo.h"
#include "private/svn_wc_private.h"

jobject
CreateJ::ConflictDescriptor(const svn_wc_conflict_description2_t *desc)
{
  JNIEnv *env = JNIUtil::getEnv();

  if (desc == NULL)
    return NULL;

  // Create a local frame for our references
  env->PushLocalFrame(LOCAL_FRAME_SIZE);
  if (JNIUtil::isJavaExceptionThrown())
    return NULL;

  // Create an instance of the conflict descriptor.
  jclass clazz = env->FindClass(JAVAHL_CLASS("/ConflictDescriptor"));
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  static jmethodID ctor = 0;
  if (ctor == 0)
    {
      ctor = env->GetMethodID(clazz, "<init>", "(Ljava/lang/String;"
                              JAVAHL_ARG("/ConflictDescriptor$Kind;")
                              JAVAHL_ARG("/types/NodeKind;")
                              "Ljava/lang/String;ZLjava/lang/String;"
                              JAVAHL_ARG("/ConflictDescriptor$Action;")
                              JAVAHL_ARG("/ConflictDescriptor$Reason;")
                              JAVAHL_ARG("/ConflictDescriptor$Operation;")
                              "Ljava/lang/String;Ljava/lang/String;"
                              "Ljava/lang/String;Ljava/lang/String;"
                              JAVAHL_ARG("/types/ConflictVersion;")
                              JAVAHL_ARG("/types/ConflictVersion;")
                              "Ljava/lang/String;[B[B[B[B)V");
      if (JNIUtil::isJavaExceptionThrown() || ctor == 0)
        POP_AND_RETURN_NULL;
    }

  jstring jpath = JNIUtil::makeJString(desc->local_abspath);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;
  jstring jpropertyName = JNIUtil::makeJString(desc->property_name);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;
  jstring jmimeType = JNIUtil::makeJString(desc->mime_type);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;
  jstring jbasePath = JNIUtil::makeJString(desc->base_abspath);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;
  jstring jreposPath = JNIUtil::makeJString(desc->their_abspath);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;
  jstring juserPath = JNIUtil::makeJString(desc->my_abspath);
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
  jobject joperation = EnumMapper::mapOperation(desc->operation);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;
  jstring jpropRejectAbspath = JNIUtil::makeJString(desc->prop_reject_abspath);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;
  jbyteArray jpropValueBase = (
      !desc->prop_value_base ? NULL
      :JNIUtil::makeJByteArray(desc->prop_value_base->data,
                               int(desc->prop_value_base->len)));
  if (JNIUtil::isExceptionThrown())
    POP_AND_RETURN_NULL;
  jbyteArray jpropValueWorking = (
      !desc->prop_value_working ? NULL
      :JNIUtil::makeJByteArray(desc->prop_value_working->data,
                               int(desc->prop_value_working->len)));
  if (JNIUtil::isExceptionThrown())
    POP_AND_RETURN_NULL;
  jbyteArray jpropValueIncomingOld = (
      !desc->prop_value_incoming_old ? NULL
      :JNIUtil::makeJByteArray(desc->prop_value_incoming_old->data,
                               int(desc->prop_value_incoming_old->len)));
  if (JNIUtil::isExceptionThrown())
    POP_AND_RETURN_NULL;
  jbyteArray jpropValueIncomingNew = (
      !desc->prop_value_incoming_new ? NULL
      :JNIUtil::makeJByteArray(desc->prop_value_incoming_new->data,
                               int(desc->prop_value_incoming_new->len)));
  if (JNIUtil::isExceptionThrown())
    POP_AND_RETURN_NULL;

  // Instantiate the conflict descriptor.
  jobject jdesc = env->NewObject(clazz, ctor, jpath, jconflictKind,
                                 jnodeKind, jpropertyName,
                                 (jboolean) desc->is_binary, jmimeType,
                                 jconflictAction, jconflictReason, joperation,
                                 jbasePath, jreposPath, juserPath,
                                 jmergedPath, jsrcLeft, jsrcRight,
                                 jpropRejectAbspath, jpropValueBase,
                                 jpropValueWorking, jpropValueIncomingOld,
                                 jpropValueIncomingNew);
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
  jclass clazz = env->FindClass(JAVAHL_CLASS("/types/ConflictVersion"));
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  static jmethodID ctor = 0;
  if (ctor == 0)
    {
      ctor = env->GetMethodID(clazz, "<init>", "(Ljava/lang/String;"
                                               "Ljava/lang/String;J"
                                               "Ljava/lang/String;"
                                               JAVAHL_ARG("/types/NodeKind;")
                                               ")V");
      if (JNIUtil::isJavaExceptionThrown() || ctor == 0)
        POP_AND_RETURN_NULL;
    }

  jstring jreposURL = JNIUtil::makeJString(version->repos_url);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;
  jstring jreposUUID = JNIUtil::makeJString(version->repos_uuid);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;
  jstring jpathInRepos = JNIUtil::makeJString(version->path_in_repos);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;
  jobject jnodeKind = EnumMapper::mapNodeKind(version->node_kind);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  jobject jversion = env->NewObject(clazz, ctor, jreposURL, jreposUUID,
                                    (jlong)version->peg_rev, jpathInRepos,
                                    jnodeKind);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  return env->PopLocalFrame(jversion);
}

jobject
CreateJ::Checksum(const svn_checksum_t *checksum)
{
  if (!checksum)
    return NULL;

  JNIEnv *env = JNIUtil::getEnv();

  // Create a local frame for our references
  env->PushLocalFrame(LOCAL_FRAME_SIZE);
  if (JNIUtil::isJavaExceptionThrown())
    return NULL;

  jclass clazz = env->FindClass(JAVAHL_CLASS("/types/Checksum"));
  if (JNIUtil::isExceptionThrown())
    POP_AND_RETURN_NULL;

  // Get the method id for the CommitItem constructor.
  static jmethodID midConstructor = 0;
  if (midConstructor == 0)
    {
      midConstructor = env->GetMethodID(clazz, "<init>",
                                        "([B"
                                        JAVAHL_ARG("/types/Checksum$Kind;")
                                        ")V");
      if (JNIUtil::isExceptionThrown())
        POP_AND_RETURN_NULL;
    }

  jbyteArray jdigest
    = JNIUtil::makeJByteArray(checksum->digest,
                              static_cast<int>(svn_checksum_size(checksum)));
  if (JNIUtil::isExceptionThrown())
    POP_AND_RETURN_NULL;

  jobject jkind = EnumMapper::mapChecksumKind(checksum->kind);
  if (JNIUtil::isExceptionThrown())
    POP_AND_RETURN_NULL;

  // create the Java object
  jobject jchecksum = env->NewObject(clazz, midConstructor, jdigest, jkind);
  if (JNIUtil::isExceptionThrown())
    POP_AND_RETURN_NULL;

  return env->PopLocalFrame(jchecksum);
}

jobject
CreateJ::DirEntry(const char *path, const char *absPath,
                  const svn_dirent_t *dirent)
{
  JNIEnv *env = JNIUtil::getEnv();

  // Create a local frame for our references
  env->PushLocalFrame(LOCAL_FRAME_SIZE);
  if (JNIUtil::isJavaExceptionThrown())
    return SVN_NO_ERROR;

  jclass clazz = env->FindClass(JAVAHL_CLASS("/types/DirEntry"));
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  static jmethodID mid = 0;
  if (mid == 0)
    {
      mid = env->GetMethodID(clazz, "<init>",
                             "(Ljava/lang/String;Ljava/lang/String;"
                             JAVAHL_ARG("/types/NodeKind;")
                             "JZJJLjava/lang/String;)V");
      if (JNIUtil::isJavaExceptionThrown())
        POP_AND_RETURN_NULL;
    }

  jstring jPath = JNIUtil::makeJString(path);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  jstring jAbsPath = JNIUtil::makeJString(absPath);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  jobject jNodeKind = EnumMapper::mapNodeKind(dirent->kind);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  jlong jSize = dirent->size;
  jboolean jHasProps = (dirent->has_props? JNI_TRUE : JNI_FALSE);
  jlong jLastChangedRevision = dirent->created_rev;
  jlong jLastChanged = dirent->time;
  jstring jLastAuthor = JNIUtil::makeJString(dirent->last_author);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  jobject ret = env->NewObject(clazz, mid, jPath, jAbsPath, jNodeKind,
                               jSize, jHasProps, jLastChangedRevision,
                               jLastChanged, jLastAuthor);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  return env->PopLocalFrame(ret);
}

jobject
CreateJ::Info(const char *path, const svn_client_info2_t *info)
{
  JNIEnv *env = JNIUtil::getEnv();

  // Create a local frame for our references
  env->PushLocalFrame(LOCAL_FRAME_SIZE);
  if (JNIUtil::isJavaExceptionThrown())
    return NULL;

  jclass clazz = env->FindClass(JAVAHL_CLASS("/types/Info"));
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  static jmethodID mid = 0;
  if (mid == 0)
    {
      mid = env->GetMethodID(clazz, "<init>",
                             "(Ljava/lang/String;Ljava/lang/String;"
                             "Ljava/lang/String;J"
                             JAVAHL_ARG("/types/NodeKind;")
                             "Ljava/lang/String;Ljava/lang/String;"
                             "JJLjava/lang/String;"
                             JAVAHL_ARG("/types/Lock;Z")
                             JAVAHL_ARG("/types/Info$ScheduleKind;")
                             "Ljava/lang/String;JJ"
                             JAVAHL_ARG("/types/Checksum;")
                             "Ljava/lang/String;JJ"
                             JAVAHL_ARG("/types/Depth;Ljava/util/Set;")
                             ")V");
      if (mid == 0 || JNIUtil::isJavaExceptionThrown())
        POP_AND_RETURN_NULL;
    }

  jstring jpath = JNIUtil::makeJString(path);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  jstring jwcroot = NULL;
  jstring jcopyFromUrl = NULL;
  jobject jchecksum = NULL;
  jstring jchangelist = NULL;
  jobject jconflicts = NULL;
  jobject jscheduleKind = NULL;
  jobject jdepth = NULL;
  jlong jworkingSize = -1;
  jlong jcopyfrom_rev = -1;
  jlong jtext_time = -1;
  if (info->wc_info)
    {
      jwcroot = JNIUtil::makeJString(info->wc_info->wcroot_abspath);
      if (JNIUtil::isJavaExceptionThrown())
        POP_AND_RETURN_NULL;

      jcopyFromUrl = JNIUtil::makeJString(info->wc_info->copyfrom_url);
      if (JNIUtil::isJavaExceptionThrown())
        POP_AND_RETURN_NULL;

      jchecksum = Checksum(info->wc_info->checksum);
      if (JNIUtil::isJavaExceptionThrown())
        POP_AND_RETURN_NULL;

      jchangelist = JNIUtil::makeJString(info->wc_info->changelist);
      if (JNIUtil::isJavaExceptionThrown())
        POP_AND_RETURN_NULL;

      jscheduleKind = EnumMapper::mapScheduleKind(info->wc_info->schedule);
      if (JNIUtil::isJavaExceptionThrown())
        POP_AND_RETURN_NULL;

      jdepth = EnumMapper::mapDepth(info->wc_info->depth);
      if (JNIUtil::isJavaExceptionThrown())
        POP_AND_RETURN_NULL;

      /* ### Maybe rename the java fields while we can */
      jworkingSize = info->wc_info->recorded_size;
      jtext_time = info->wc_info->recorded_time;

      jcopyfrom_rev = info->wc_info->copyfrom_rev;

      if (info->wc_info->conflicts && info->wc_info->conflicts->nelts > 0)
        {
          std::vector<jobject> jconflict_vec;

          for (int i = 0; i < info->wc_info->conflicts->nelts; i++)
            {
              const svn_wc_conflict_description2_t *conflict = APR_ARRAY_IDX(
                                info->wc_info->conflicts, i,
                                const svn_wc_conflict_description2_t *);

              jobject jconflict = ConflictDescriptor(conflict);
              if (JNIUtil::isJavaExceptionThrown())
                POP_AND_RETURN_NULL;

              jconflict_vec.push_back(jconflict);
            }

          jconflicts = Set(jconflict_vec);
          if (JNIUtil::isJavaExceptionThrown())
            POP_AND_RETURN_NULL;
        }
    }

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

  jobject jnodeKind = EnumMapper::mapNodeKind(info->kind);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  jobject jinfo2 = env->NewObject(clazz, mid, jpath, jwcroot, jurl,
                                  (jlong) info->rev,
                                  jnodeKind, jreposRootUrl, jreportUUID,
                                  (jlong) info->last_changed_rev,
                                  (jlong) info->last_changed_date,
                                  jlastChangedAuthor, jlock,
                                  info->wc_info ? JNI_TRUE : JNI_FALSE,
                                  jscheduleKind, jcopyFromUrl,
                                  jcopyfrom_rev, jtext_time, jchecksum,
                                  jchangelist, jworkingSize,
                                  (jlong) info->size, jdepth, jconflicts);

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

  jclass clazz = env->FindClass(JAVAHL_CLASS("/types/Lock"));
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
CreateJ::LockMap(const apr_hash_t *locks, apr_pool_t *pool)
{
  JNIEnv *env = JNIUtil::getEnv();

  if (locks == NULL)
    return NULL;

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
  for (hi = apr_hash_first(pool, (apr_hash_t *) locks); hi;
        hi = apr_hash_next(hi), ++i)
    {
      const char *key = (const char *) apr_hash_this_key(hi);
      const svn_lock_t *lock = (const svn_lock_t *) apr_hash_this_val(hi);

      jstring jpath = JNIUtil::makeJString(key);
      if (JNIUtil::isJavaExceptionThrown())
        POP_AND_RETURN_NULL;

      jobject jlock = Lock(lock);
      if (JNIUtil::isJavaExceptionThrown())
        POP_AND_RETURN_NULL;

      env->CallObjectMethod(map, put_mid, jpath, jlock);
      if (JNIUtil::isJavaExceptionThrown())
        POP_AND_RETURN_NULL;

      env->DeleteLocalRef(jpath);
      env->DeleteLocalRef(jlock);
    }

  return env->PopLocalFrame(map);
}

jobject
CreateJ::ChangedPath(const char *path, svn_log_changed_path2_t *log_item)
{
  JNIEnv *env = JNIUtil::getEnv();

  // Create a local frame for our references
  env->PushLocalFrame(LOCAL_FRAME_SIZE);
  if (JNIUtil::isJavaExceptionThrown())
    return NULL;

  jclass clazzCP = env->FindClass(JAVAHL_CLASS("/types/ChangePath"));
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  static jmethodID midCP = 0;
  if (midCP == 0)
    {
      midCP = env->GetMethodID(clazzCP,
                               "<init>",
                               "(Ljava/lang/String;JLjava/lang/String;"
                               JAVAHL_ARG("/types/ChangePath$Action;")
                               JAVAHL_ARG("/types/NodeKind;")
                               JAVAHL_ARG("/types/Tristate;")
                               JAVAHL_ARG("/types/Tristate;")
                               ")V");
      if (JNIUtil::isJavaExceptionThrown())
        POP_AND_RETURN_NULL;
    }

  jstring jpath = JNIUtil::makeJString(path);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  jstring jcopyFromPath = JNIUtil::makeJString(log_item->copyfrom_path);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  jobject jaction = EnumMapper::mapChangePathAction(log_item->action);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  jobject jnodeKind = EnumMapper::mapNodeKind(log_item->node_kind);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  jlong jcopyFromRev = log_item->copyfrom_rev;

  jobject jcp = env->NewObject(clazzCP, midCP, jpath, jcopyFromRev,
                      jcopyFromPath, jaction, jnodeKind,
                      EnumMapper::mapTristate(log_item->text_modified),
                      EnumMapper::mapTristate(log_item->props_modified));
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  return env->PopLocalFrame(jcp);
}

jobject
CreateJ::Status(svn_wc_context_t *wc_ctx,
                const svn_client_status_t *status,
                apr_pool_t *pool)
{
  if (status == NULL)
    return NULL;

  JNIEnv *env = JNIUtil::getEnv();

  // Create a local frame for our references
  env->PushLocalFrame(LOCAL_FRAME_SIZE);
  if (JNIUtil::isJavaExceptionThrown())
    return NULL;

  jclass clazz = env->FindClass(JAVAHL_CLASS("/types/Status"));
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  static jmethodID mid = 0;
  if (mid == 0)
    {
      mid = env->GetMethodID(clazz, "<init>",
                             "(Ljava/lang/String;Ljava/lang/String;"
                             JAVAHL_ARG("/types/NodeKind;")
                             "JJJLjava/lang/String;"
                             JAVAHL_ARG("/types/Status$Kind;")
                             JAVAHL_ARG("/types/Status$Kind;")
                             JAVAHL_ARG("/types/Status$Kind;")
                             JAVAHL_ARG("/types/Status$Kind;")
                             JAVAHL_ARG("/types/Status$Kind;")
                             JAVAHL_ARG("/types/Status$Kind;")
                             "ZZ" JAVAHL_ARG("/types/Depth;")
                             "ZZZ" JAVAHL_ARG("/types/Lock;")
                             JAVAHL_ARG("/types/Lock;")
                             "JJ" JAVAHL_ARG("/types/NodeKind;")
                             "Ljava/lang/String;Ljava/lang/String;"
                             "Ljava/lang/String;Ljava/lang/String;)V");
      if (JNIUtil::isJavaExceptionThrown())
        POP_AND_RETURN_NULL;
    }

  /* ### Calculate the old style text_status value to make
     ### the tests pass. It is probably better to do this in
     ### the tigris package and then switch the apache package
     ### to three statuses. */
  jstring jUrl = NULL;
  jobject jNodeKind = NULL;
  jlong jRevision =
    org_apache_subversion_javahl_types_Revision_SVN_INVALID_REVNUM;
  jlong jLastChangedRevision =
    org_apache_subversion_javahl_types_Revision_SVN_INVALID_REVNUM;
  jlong jLastChangedDate = 0;
  jstring jLastCommitAuthor = NULL;
  jobject jLocalLock = NULL;
  jstring jChangelist = NULL;
  jstring jMovedFromAbspath = NULL;
  jstring jMovedToAbspath = NULL;

  jobject jNodeType = EnumMapper::mapStatusKind(status->node_status);
  jobject jTextType = EnumMapper::mapStatusKind(status->text_status);
  jobject jPropType = EnumMapper::mapStatusKind(status->prop_status);
  jobject jRpNodeType = EnumMapper::mapStatusKind(status->repos_node_status);
  jobject jRpTextType = EnumMapper::mapStatusKind(status->repos_text_status);
  jobject jRpPropType = EnumMapper::mapStatusKind(status->repos_prop_status);
  jobject jDepth = EnumMapper::mapDepth(status->depth);
  jboolean jIsLocked = (status->wc_is_locked == 1) ? JNI_TRUE: JNI_FALSE;
  jboolean jIsCopied = (status->copied == 1) ? JNI_TRUE: JNI_FALSE;
  jboolean jIsConflicted = (status->conflicted == 1) ? JNI_TRUE : JNI_FALSE;
  jboolean jIsSwitched = (status->switched == 1) ? JNI_TRUE: JNI_FALSE;
  jboolean jIsFileExternal = (status->file_external == 1) ? JNI_TRUE
                                                          : JNI_FALSE;

  jstring jPath = JNIUtil::makeJString(status->local_abspath);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  jobject jReposLock = CreateJ::Lock(status->repos_lock);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  if (status->repos_root_url)
    {
      jUrl = JNIUtil::makeJString(svn_path_url_add_component2(
                                    status->repos_root_url,
                                    status->repos_relpath,
                                    pool));
      if (JNIUtil::isJavaExceptionThrown())
        POP_AND_RETURN_NULL;
    }

  jlong jOODLastCmtRevision = status->ood_changed_rev;
  jlong jOODLastCmtDate = status->ood_changed_date;
  jobject jOODKind = EnumMapper::mapNodeKind(status->ood_kind);
  jstring jOODLastCmtAuthor = JNIUtil::makeJString(status->ood_changed_author);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  if (status->versioned)
    {
      jNodeKind = EnumMapper::mapNodeKind(status->kind);
      jRevision = status->revision;
      jLastChangedRevision = status->changed_rev;
      jLastChangedDate = status->changed_date;
      jLastCommitAuthor = JNIUtil::makeJString(status->changed_author);

      if (JNIUtil::isJavaExceptionThrown())
        POP_AND_RETURN_NULL;

      jLocalLock = Lock(status->lock);
      if (JNIUtil::isJavaExceptionThrown())
        POP_AND_RETURN_NULL;

      jChangelist = JNIUtil::makeJString(status->changelist);
      if (JNIUtil::isJavaExceptionThrown())
        POP_AND_RETURN_NULL;
    }

  if (status->moved_from_abspath)
    {
      jMovedFromAbspath = JNIUtil::makeJString(status->moved_from_abspath);
      if (JNIUtil::isJavaExceptionThrown())
        POP_AND_RETURN_NULL;
    }

  if (status->moved_to_abspath)
    {
      jMovedToAbspath = JNIUtil::makeJString(status->moved_to_abspath);
      if (JNIUtil::isJavaExceptionThrown())
        POP_AND_RETURN_NULL;
    }

  jobject ret = env->NewObject(clazz, mid, jPath, jUrl, jNodeKind, jRevision,
                               jLastChangedRevision, jLastChangedDate,
                               jLastCommitAuthor,
                               jNodeType, jTextType, jPropType,
                               jRpNodeType, jRpTextType, jRpPropType,
                               jIsLocked, jIsCopied, jDepth, jIsConflicted,
                               jIsSwitched, jIsFileExternal, jLocalLock,
                               jReposLock,
                               jOODLastCmtRevision, jOODLastCmtDate,
                               jOODKind, jOODLastCmtAuthor, jChangelist,
                               jMovedFromAbspath, jMovedToAbspath);

  return env->PopLocalFrame(ret);
}

jobject
CreateJ::ClientNotifyInformation(const svn_wc_notify_t *wcNotify)
{
  JNIEnv *env = JNIUtil::getEnv();

  // Create a local frame for our references
  env->PushLocalFrame(LOCAL_FRAME_SIZE);
  if (JNIUtil::isJavaExceptionThrown())
    return NULL;

  static jmethodID midCT = 0;
  jclass clazz = env->FindClass(JAVAHL_CLASS("/ClientNotifyInformation"));
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  if (midCT == 0)
    {
      midCT = env->GetMethodID(clazz, "<init>",
                               "(Ljava/lang/String;"
                               JAVAHL_ARG("/ClientNotifyInformation$Action;")
                               JAVAHL_ARG("/types/NodeKind;")
                               "Ljava/lang/String;"
                               JAVAHL_ARG("/types/Lock;")
                               "Ljava/lang/String;Ljava/util/List;"
                               JAVAHL_ARG("/ClientNotifyInformation$Status;")
                               JAVAHL_ARG("/ClientNotifyInformation$Status;")
                               JAVAHL_ARG("/ClientNotifyInformation$LockStatus;")
                               "JLjava/lang/String;"
                               JAVAHL_ARG("/types/RevisionRange;")
                               "Ljava/lang/String;"
                               "Ljava/lang/String;Ljava/lang/String;"
                               "Ljava/util/Map;JJJJJJI)V");
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

  jstring jErr;
  jobject jErrStack;
  JNIUtil::makeSVNErrorMessage(wcNotify->err, &jErr, &jErrStack);
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

  jstring jUrl = JNIUtil::makeJString(wcNotify->url);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  jstring jpathPrefix = JNIUtil::makeJString(wcNotify->path_prefix);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  jstring jpropName = JNIUtil::makeJString(wcNotify->prop_name);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  jobject jrevProps = CreateJ::PropertyMap(wcNotify->rev_props);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  jlong joldRevision = wcNotify->old_revision;
  jlong jhunkOriginalStart = wcNotify->hunk_original_start;
  jlong jhunkOriginalLength = wcNotify->hunk_original_length;
  jlong jhunkModifiedStart = wcNotify->hunk_modified_start;
  jlong jhunkModifiedLength = wcNotify->hunk_modified_length;
  jlong jhunkMatchedLine = wcNotify->hunk_matched_line;
  jint jhunkFuzz = static_cast<jint>(wcNotify->hunk_fuzz);
  if (jhunkFuzz < 0 || jhunkFuzz != wcNotify->hunk_fuzz)
    {
      env->ThrowNew(env->FindClass("java.lang.ArithmeticException"),
                    "Overflow converting C svn_linenum_t to Java int");
      POP_AND_RETURN_NULL;
    }

  // call the Java method
  jobject jInfo = env->NewObject(clazz, midCT, jPath, jAction,
                                 jKind, jMimeType, jLock, jErr, jErrStack,
                                 jContentState, jPropState, jLockState,
                                 (jlong) wcNotify->revision, jChangelistName,
                                 jMergeRange, jUrl, jpathPrefix, jpropName,
                                 jrevProps, joldRevision,
                                 jhunkOriginalStart, jhunkOriginalLength,
                                 jhunkModifiedStart, jhunkModifiedLength,
                                 jhunkMatchedLine, jhunkFuzz);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  return env->PopLocalFrame(jInfo);
}

jobject
CreateJ::ReposNotifyInformation(const svn_repos_notify_t *reposNotify)
{
  JNIEnv *env = JNIUtil::getEnv();

  // Create a local frame for our references
  env->PushLocalFrame(LOCAL_FRAME_SIZE);
  if (JNIUtil::isJavaExceptionThrown())
    return NULL;

  static jmethodID midCT = 0;
  jclass clazz = env->FindClass(JAVAHL_CLASS("/ReposNotifyInformation"));
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  if (midCT == 0)
    {
      midCT = env->GetMethodID(clazz, "<init>",
                               "(" JAVAHL_ARG("/ReposNotifyInformation$Action;")
                               "JLjava/lang/String;JJJ"
                               JAVAHL_ARG("/ReposNotifyInformation$NodeAction;")
                               "Ljava/lang/String;)V");
      if (JNIUtil::isJavaExceptionThrown() || midCT == 0)
        POP_AND_RETURN_NULL;
    }

  // convert the parameters to their Java relatives
  jobject jAction = EnumMapper::mapReposNotifyAction(reposNotify->action);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  jstring jWarning = JNIUtil::makeJString(reposNotify->warning_str);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  jlong jRevision = (jlong)reposNotify->revision;
  jlong jShard = (jlong)reposNotify->shard;
  jlong jnewRevision = (jlong)reposNotify->new_revision;
  jlong joldRevision = (jlong)reposNotify->old_revision;

  jobject jnodeAction = EnumMapper::mapReposNotifyNodeAction(
                                                    reposNotify->node_action);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  jstring jpath = JNIUtil::makeJString(reposNotify->path);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  // call the Java method
  jobject jInfo = env->NewObject(clazz, midCT, jAction, jRevision, jWarning,
                                 jShard, jnewRevision, joldRevision,
                                 jnodeAction, jpath);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  return env->PopLocalFrame(jInfo);
}

jobject
CreateJ::CommitItem(svn_client_commit_item3_t *item)
{
  JNIEnv *env = JNIUtil::getEnv();

  // Create a local frame for our references
  env->PushLocalFrame(LOCAL_FRAME_SIZE);
  if (JNIUtil::isJavaExceptionThrown())
    return NULL;

  jclass clazz = env->FindClass(JAVAHL_CLASS("/CommitItem"));
  if (JNIUtil::isExceptionThrown())
    POP_AND_RETURN_NULL;

  // Get the method id for the CommitItem constructor.
  static jmethodID midConstructor = 0;
  if (midConstructor == 0)
    {
      midConstructor = env->GetMethodID(clazz, "<init>",
                                        "(Ljava/lang/String;"
                                        JAVAHL_ARG("/types/NodeKind;")
                                        "ILjava/lang/String;"
                                        "Ljava/lang/String;J"
                                        "Ljava/lang/String;)V");
      if (JNIUtil::isExceptionThrown())
        POP_AND_RETURN_NULL;
    }

  jstring jpath = JNIUtil::makeJString(item->path);

  jobject jnodeKind = EnumMapper::mapNodeKind(item->kind);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  jint jstateFlags = 0;
  if (item->state_flags & SVN_CLIENT_COMMIT_ITEM_ADD)
    jstateFlags |=
      org_apache_subversion_javahl_CommitItemStateFlags_Add;
  if (item->state_flags & SVN_CLIENT_COMMIT_ITEM_DELETE)
    jstateFlags |=
      org_apache_subversion_javahl_CommitItemStateFlags_Delete;
  if (item->state_flags & SVN_CLIENT_COMMIT_ITEM_TEXT_MODS)
    jstateFlags |=
      org_apache_subversion_javahl_CommitItemStateFlags_TextMods;
  if (item->state_flags & SVN_CLIENT_COMMIT_ITEM_PROP_MODS)
    jstateFlags |=
      org_apache_subversion_javahl_CommitItemStateFlags_PropMods;
  if (item->state_flags & SVN_CLIENT_COMMIT_ITEM_IS_COPY)
    jstateFlags |=
      org_apache_subversion_javahl_CommitItemStateFlags_IsCopy;
  if (item->state_flags & SVN_CLIENT_COMMIT_ITEM_LOCK_TOKEN)
    jstateFlags |=
      org_apache_subversion_javahl_CommitItemStateFlags_LockToken;
  if (item->state_flags & SVN_CLIENT_COMMIT_ITEM_MOVED_HERE)
    jstateFlags |=
      org_apache_subversion_javahl_CommitItemStateFlags_MovedHere;

  jstring jurl = JNIUtil::makeJString(item->url);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  jstring jcopyUrl = JNIUtil::makeJString(item->copyfrom_url);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  jstring jmovedFromPath = JNIUtil::makeJString(item->moved_from_abspath);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  jlong jcopyRevision = item->revision;

  // create the Java object
  jobject jitem = env->NewObject(clazz, midConstructor, jpath,
                                 jnodeKind, jstateFlags, jurl,
                                 jcopyUrl, jcopyRevision, jmovedFromPath);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  return env->PopLocalFrame(jitem);
}

jobject
CreateJ::CommitInfo(const svn_commit_info_t *commit_info)
{
  JNIEnv *env = JNIUtil::getEnv();

  // Create a local frame for our references
  env->PushLocalFrame(LOCAL_FRAME_SIZE);
  if (JNIUtil::isJavaExceptionThrown())
    return NULL;

  static jmethodID midCT = 0;
  jclass clazz = env->FindClass(JAVAHL_CLASS("/CommitInfo"));
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  if (midCT == 0)
    {
      midCT = env->GetMethodID(clazz, "<init>",
                               "(JLjava/lang/String;Ljava/lang/String;"
                               "Ljava/lang/String;Ljava/lang/String;)V");
      if (JNIUtil::isJavaExceptionThrown() || midCT == 0)
        POP_AND_RETURN_NULL;
    }

  jstring jAuthor = JNIUtil::makeJString(commit_info->author);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  jstring jDate = JNIUtil::makeJString(commit_info->date);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  jlong jRevision = commit_info->revision;

  jstring jPostCommitError = JNIUtil::makeJString(
                                            commit_info->post_commit_err);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  jstring jReposRoot = JNIUtil::makeJString(commit_info->repos_root);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  // call the Java method
  jobject jInfo = env->NewObject(clazz, midCT, jRevision, jDate, jAuthor,
                                 jPostCommitError, jReposRoot);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  return env->PopLocalFrame(jInfo);
}

jobject
CreateJ::StringSet(const apr_array_header_t *strings)
{
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

namespace {
void fill_property_map(jobject map,
                       apr_hash_t* prop_hash, apr_array_header_t* prop_diffs,
                       apr_pool_t* scratch_pool, jmethodID put_mid)
{
  SVN_ERR_ASSERT_NO_RETURN(!(prop_hash && prop_diffs));

  if (!map || (prop_hash == NULL && prop_diffs == NULL))
    return;

  JNIEnv *env = JNIUtil::getEnv();

  // Create a local frame for our references
  env->PushLocalFrame(LOCAL_FRAME_SIZE);
  if (JNIUtil::isJavaExceptionThrown())
    return;

  // The caller may not know the concrete class of the map, so
  // determine the "put" method identifier here.
  if (put_mid == 0)
    {
      put_mid = env->GetMethodID(env->GetObjectClass(map), "put",
                                 "(Ljava/lang/Object;Ljava/lang/Object;)"
                                 "Ljava/lang/Object;");
      if (JNIUtil::isJavaExceptionThrown())
        POP_AND_RETURN_NOTHING();
    }

  struct body
  {
    void operator()(const char* key, const svn_string_t* val)
      {
        jstring jpropName = JNIUtil::makeJString(key);
        if (JNIUtil::isJavaExceptionThrown())
          return;

        jbyteArray jpropVal = (val ? JNIUtil::makeJByteArray(val) : NULL);
        if (JNIUtil::isJavaExceptionThrown())
          return;

        jobject ret = m_env->CallObjectMethod(m_map, m_put_mid,
                                              jpropName, jpropVal);
        if (JNIUtil::isJavaExceptionThrown())
          return;

        m_env->DeleteLocalRef(ret);
        m_env->DeleteLocalRef(jpropVal);
        m_env->DeleteLocalRef(jpropName);
      }

    JNIEnv*& m_env;
    jmethodID& m_put_mid;
    jobject& m_map;

    body(JNIEnv*& xenv, jmethodID& xput_mid, jobject& xmap)
      : m_env(xenv), m_put_mid(xput_mid), m_map(xmap)
      {}
  } loop_body(env, put_mid, map);

  if (prop_hash)
    {
      if (!scratch_pool)
        scratch_pool = apr_hash_pool_get(prop_hash);

      apr_hash_index_t *hi;
      for (hi = apr_hash_first(scratch_pool, prop_hash);
           hi; hi = apr_hash_next(hi))
        {
          const char* key;
          svn_string_t* val;

          const void* v_key;
          void* v_val;

          apr_hash_this(hi, &v_key, NULL, &v_val);
          key = static_cast<const char*>(v_key);
          val = static_cast<svn_string_t*>(v_val);

          loop_body(key, val);
          if (JNIUtil::isJavaExceptionThrown())
            POP_AND_RETURN_NOTHING();
        }
    }
  else
    {
      for (int i = 0; i < prop_diffs->nelts; ++i)
        {
          svn_prop_t* prop = &APR_ARRAY_IDX(prop_diffs, i, svn_prop_t);
          loop_body(prop->name, prop->value);
          if (JNIUtil::isJavaExceptionThrown())
            POP_AND_RETURN_NOTHING();
        }
    }
  POP_AND_RETURN_NOTHING();
}

jobject property_map(apr_hash_t *prop_hash, apr_array_header_t* prop_diffs,
                     apr_pool_t* scratch_pool)
{
  SVN_ERR_ASSERT_NO_RETURN(!(prop_hash && prop_diffs));

  if (prop_hash == NULL && prop_diffs == NULL)
    return NULL;

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

  fill_property_map(map, prop_hash, prop_diffs, scratch_pool, put_mid);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  return env->PopLocalFrame(map);
}
} // anonymous namespace

jobject CreateJ::PropertyMap(apr_hash_t *prop_hash, apr_pool_t* scratch_pool)
{
  return property_map(prop_hash, NULL, scratch_pool);
}

jobject CreateJ::PropertyMap(apr_array_header_t* prop_diffs,
                             apr_pool_t* scratch_pool)
{
  return property_map(NULL, prop_diffs, scratch_pool);
}

void CreateJ::FillPropertyMap(jobject map, apr_hash_t* prop_hash,
                              apr_pool_t* scratch_pool, jmethodID put_mid)
{
  fill_property_map(map, prop_hash, NULL, scratch_pool, put_mid);
}

void CreateJ::FillPropertyMap(jobject map, apr_array_header_t* prop_diffs,
                              apr_pool_t* scratch_pool, jmethodID put_mid)
{
  fill_property_map(map, NULL, prop_diffs, scratch_pool, put_mid);
}

jobject CreateJ::InheritedProps(apr_array_header_t *iprops)
{
  JNIEnv *env = JNIUtil::getEnv();

  if (iprops == NULL)
    return NULL;

  // Create a local frame for our references
  env->PushLocalFrame(LOCAL_FRAME_SIZE);
  if (JNIUtil::isJavaExceptionThrown())
    return NULL;

  jclass list_cls = env->FindClass("java/util/ArrayList");
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  static volatile jmethodID init_mid = 0;
  if (init_mid == 0)
    {
      init_mid = env->GetMethodID(list_cls, "<init>", "(I)V");
      if (JNIUtil::isJavaExceptionThrown())
        POP_AND_RETURN_NULL;
    }

  static volatile jmethodID add_mid = 0;
  if (add_mid == 0)
    {
      add_mid = env->GetMethodID(list_cls, "add",
                                 "(Ljava/lang/Object;)Z");
      if (JNIUtil::isJavaExceptionThrown())
        POP_AND_RETURN_NULL;
    }

  jclass item_cls = env->FindClass(
      JAVAHL_CLASS("/callback/InheritedProplistCallback$InheritedItem"));
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  static volatile jmethodID ctor_mid = 0;
  if (ctor_mid == 0)
    {
      ctor_mid = env->GetMethodID(item_cls, "<init>",
                                  "(Ljava/lang/String;Ljava/util/Map;)V");
      if (JNIUtil::isJavaExceptionThrown())
        POP_AND_RETURN_NULL;
    }

  jobject array = env->NewObject(list_cls, init_mid, iprops->nelts);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  for (int i = 0; i < iprops->nelts; ++i)
    {
      svn_prop_inherited_item_t *iprop =
        APR_ARRAY_IDX(iprops, i, svn_prop_inherited_item_t*);

      jstring path_or_url = JNIUtil::makeJString(iprop->path_or_url);
      if (JNIUtil::isJavaExceptionThrown())
        POP_AND_RETURN_NULL;

      jobject props = PropertyMap(iprop->prop_hash);
      if (JNIUtil::isJavaExceptionThrown())
        POP_AND_RETURN_NULL;

      jobject item = env->NewObject(item_cls, ctor_mid, path_or_url, props);
      if (JNIUtil::isJavaExceptionThrown())
        POP_AND_RETURN_NULL;

      env->CallBooleanMethod(array, add_mid, item);
      if (JNIUtil::isJavaExceptionThrown())
        POP_AND_RETURN_NULL;

      env->DeleteLocalRef(item);
      env->DeleteLocalRef(props);
      env->DeleteLocalRef(path_or_url);
    }

  return env->PopLocalFrame(array);
}

jobject CreateJ::Mergeinfo(svn_mergeinfo_t mergeinfo, apr_pool_t* scratch_pool)
{
  if (mergeinfo == NULL)
    return NULL;

  // Transform mergeinfo into Java Mergeinfo object.
  JNIEnv *env = JNIUtil::getEnv();
  jclass clazz = env->FindClass(JAVAHL_CLASS("/types/Mergeinfo"));
  if (JNIUtil::isJavaExceptionThrown())
    return NULL;

  static jmethodID ctor = 0;
  if (ctor == 0)
    {
      ctor = env->GetMethodID(clazz, "<init>", "()V");
      if (JNIUtil::isJavaExceptionThrown())
        return NULL;
    }

  static jmethodID addRevisions = 0;
  if (addRevisions == 0)
    {
      addRevisions = env->GetMethodID(clazz, "addRevisions",
                                      "(Ljava/lang/String;"
                                      "Ljava/util/List;)V");
      if (JNIUtil::isJavaExceptionThrown())
        return NULL;
    }

  jobject jmergeinfo = env->NewObject(clazz, ctor);
  if (JNIUtil::isJavaExceptionThrown())
    return NULL;

  apr_hash_index_t *hi;
  for (hi = apr_hash_first(scratch_pool, mergeinfo);
       hi;
       hi = apr_hash_next(hi))
    {
      const void *path;
      void *val;
      apr_hash_this(hi, &path, NULL, &val);

      jstring jpath =
        JNIUtil::makeJString(static_cast<const char*>(path));
      jobject jranges =
        RevisionRangeList(static_cast<svn_rangelist_t*>(val)).toList();

      env->CallVoidMethod(jmergeinfo, addRevisions, jpath, jranges);
      if (JNIUtil::isJavaExceptionThrown())
        return NULL;

      env->DeleteLocalRef(jranges);
      env->DeleteLocalRef(jpath);
    }

  env->DeleteLocalRef(clazz);

  return jmergeinfo;
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

      env->CallBooleanMethod(set, add_mid, jthing);
      if (JNIUtil::isJavaExceptionThrown())
        POP_AND_RETURN_NULL;

      env->DeleteLocalRef(jthing);
    }

  return env->PopLocalFrame(set);
}
