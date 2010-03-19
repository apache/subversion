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
