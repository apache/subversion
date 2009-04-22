/**
 * @copyright
 * ====================================================================
 * Copyright (c) 2007-2009 CollabNet.  All rights reserved.
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

  // Create an instance of the conflict descriptor.
  static jmethodID ctor = 0;
  jclass clazz = env->FindClass(JAVA_PACKAGE "/ConflictDescriptor");
  if (JNIUtil::isJavaExceptionThrown())
    return NULL;

  if (ctor == 0)
    {
      ctor = env->GetMethodID(clazz, "<init>", "(Ljava/lang/String;II"
                              "Ljava/lang/String;ZLjava/lang/String;III"
                              "Ljava/lang/String;Ljava/lang/String;"
                              "Ljava/lang/String;Ljava/lang/String;"
                              "L"JAVA_PACKAGE"/ConflictVersion;"
                              "L"JAVA_PACKAGE"/ConflictVersion;)V");
      if (JNIUtil::isJavaExceptionThrown() || ctor == 0)
        return NULL;
    }

  jstring jpath = JNIUtil::makeJString(desc->path);
  if (JNIUtil::isJavaExceptionThrown())
    return NULL;
  jstring jpropertyName = JNIUtil::makeJString(desc->property_name);
  if (JNIUtil::isJavaExceptionThrown())
    return NULL;
  jstring jmimeType = JNIUtil::makeJString(desc->mime_type);
  if (JNIUtil::isJavaExceptionThrown())
    return NULL;
  jstring jbasePath = JNIUtil::makeJString(desc->base_file);
  if (JNIUtil::isJavaExceptionThrown())
    return NULL;
  jstring jreposPath = JNIUtil::makeJString(desc->their_file);
  if (JNIUtil::isJavaExceptionThrown())
    return NULL;
  jstring juserPath = JNIUtil::makeJString(desc->my_file);
  if (JNIUtil::isJavaExceptionThrown())
    return NULL;
  jstring jmergedPath = JNIUtil::makeJString(desc->merged_file);
  if (JNIUtil::isJavaExceptionThrown())
    return NULL;
  jobject jsrcLeft = CreateJ::ConflictVersion(desc->src_left_version);
  if (JNIUtil::isJavaExceptionThrown())
    return NULL;
  jobject jsrcRight = ConflictVersion(desc->src_right_version);
  if (JNIUtil::isJavaExceptionThrown())
    return NULL;

  // Instantiate the conflict descriptor.
  jobject jdesc = env->NewObject(clazz, ctor, jpath,
                                 EnumMapper::mapConflictKind(desc->kind),
                                 EnumMapper::mapNodeKind(desc->node_kind),
                                 jpropertyName,
                                 (jboolean) desc->is_binary, jmimeType,
                                 EnumMapper::mapConflictAction(desc->action),
                                 EnumMapper::mapConflictReason(desc->reason),
                                 EnumMapper::mapOperation(desc->operation),
                                 jbasePath, jreposPath, juserPath,
                                 jmergedPath, jsrcLeft, jsrcRight);
  if (JNIUtil::isJavaExceptionThrown())
    return NULL;

  env->DeleteLocalRef(clazz);
  if (JNIUtil::isJavaExceptionThrown())
    return NULL;

  env->DeleteLocalRef(jpath);
  if (JNIUtil::isJavaExceptionThrown())
    return NULL;
  env->DeleteLocalRef(jpropertyName);
  if (JNIUtil::isJavaExceptionThrown())
    return NULL;
  env->DeleteLocalRef(jmimeType);
  if (JNIUtil::isJavaExceptionThrown())
    return NULL;
  env->DeleteLocalRef(jbasePath);
  if (JNIUtil::isJavaExceptionThrown())
    return NULL;
  env->DeleteLocalRef(jreposPath);
  if (JNIUtil::isJavaExceptionThrown())
    return NULL;
  env->DeleteLocalRef(juserPath);
  if (JNIUtil::isJavaExceptionThrown())
    return NULL;
  env->DeleteLocalRef(jmergedPath);
  if (JNIUtil::isJavaExceptionThrown())
    return NULL;
  env->DeleteLocalRef(jsrcRight);
  if (JNIUtil::isJavaExceptionThrown())
    return NULL;
  env->DeleteLocalRef(jsrcLeft);
  if (JNIUtil::isJavaExceptionThrown())
    return NULL;

  return jdesc;
}

jobject
CreateJ::ConflictVersion(const svn_wc_conflict_version_t *version)
{
  JNIEnv *env = JNIUtil::getEnv();

  if (version == NULL)
    return NULL;

  // Create an instance of the conflict version.
  static jmethodID ctor = 0;
  jclass clazz = env->FindClass(JAVA_PACKAGE "/ConflictVersion");
  if (JNIUtil::isJavaExceptionThrown())
    return NULL;

  if (ctor == 0)
    {
      ctor = env->GetMethodID(clazz, "<init>", "(Ljava/lang/String;J"
                                               "Ljava/lang/String;I)V");
      if (JNIUtil::isJavaExceptionThrown() || ctor == 0)
        return NULL;
    }

  jstring jreposURL = JNIUtil::makeJString(version->repos_url);
  if (JNIUtil::isJavaExceptionThrown())
    return NULL;
  jstring jpathInRepos = JNIUtil::makeJString(version->path_in_repos);
  if (JNIUtil::isJavaExceptionThrown())
    return NULL;

  jobject jversion = env->NewObject(clazz, ctor, jreposURL,
                                 (jlong)version->peg_rev, jpathInRepos,
                                 EnumMapper::mapNodeKind(version->node_kind));
  if (JNIUtil::isJavaExceptionThrown())
    return NULL;

  env->DeleteLocalRef(clazz);
  if (JNIUtil::isJavaExceptionThrown())
    return NULL;

  env->DeleteLocalRef(jreposURL);
  if (JNIUtil::isJavaExceptionThrown())
    return NULL;
  env->DeleteLocalRef(jpathInRepos);
  if (JNIUtil::isJavaExceptionThrown())
    return NULL;

  return jversion;
}

jobject
CreateJ::Info(const svn_wc_entry_t *entry)
{
    if (entry == NULL)
        return NULL;

    JNIEnv *env = JNIUtil::getEnv();

    jclass clazz = env->FindClass(JAVA_PACKAGE"/Info");
    if (JNIUtil::isJavaExceptionThrown())
        return NULL;

    static jmethodID mid = 0;
    if (mid == 0)
    {
        mid = env->GetMethodID(clazz, "<init>",
                               "(Ljava/lang/String;Ljava/lang/String;"
                               "Ljava/lang/String;Ljava/lang/String;"
                               "IILjava/lang/String;JJLjava/util/Date;"
                               "Ljava/util/Date;Ljava/util/Date;"
                               "ZZZZJLjava/lang/String;)V");
        if (JNIUtil::isJavaExceptionThrown())
            return NULL;
    }

    jstring jName = JNIUtil::makeJString(entry->name);
    if (JNIUtil::isJavaExceptionThrown())
        return NULL;
    jstring jUrl = JNIUtil::makeJString(entry->url);
    if (JNIUtil::isJavaExceptionThrown())
        return NULL;
    jstring jUuid = JNIUtil::makeJString(entry->uuid);
    if (JNIUtil::isJavaExceptionThrown())
        return NULL;
    jstring jRepository = JNIUtil::makeJString(entry->repos);
    if (JNIUtil::isJavaExceptionThrown())
        return NULL;
    jint jSchedule = EnumMapper::mapScheduleKind(entry->schedule);
    jint jNodeKind = EnumMapper::mapNodeKind(entry->kind);
    jstring jAuthor = JNIUtil::makeJString(entry->cmt_author);
    if (JNIUtil::isJavaExceptionThrown())
        return NULL;
    jlong jRevision = entry->revision;
    jlong jLastChangedRevision = entry->cmt_rev;
    jobject jLastChangedDate = JNIUtil::createDate(entry->cmt_date);
    if (JNIUtil::isJavaExceptionThrown())
        return NULL;
    jobject jLastDateTextUpdate = JNIUtil::createDate(entry->text_time);
    if (JNIUtil::isJavaExceptionThrown())
        return NULL;
    jobject jLastDatePropsUpdate = JNIUtil::createDate(entry->prop_time);
    if (JNIUtil::isJavaExceptionThrown())
        return NULL;
    jboolean jCopied = entry->copied ? JNI_TRUE : JNI_FALSE;
    jboolean jDeleted = entry->deleted ? JNI_TRUE : JNI_FALSE;
    jboolean jAbsent = entry->absent ? JNI_TRUE : JNI_FALSE;
    jboolean jIncomplete = entry->incomplete ? JNI_TRUE : JNI_FALSE;
    jlong jCopyRev = entry->copyfrom_rev;
    jstring jCopyUrl = JNIUtil::makeJString(entry->copyfrom_url);
    if (JNIUtil::isJavaExceptionThrown())
        return NULL;

    jobject jinfo = env->NewObject(clazz, mid, jName, jUrl, jUuid, jRepository,
                                   jSchedule, jNodeKind, jAuthor, jRevision,
                                   jLastChangedRevision, jLastChangedDate,
                                   jLastDateTextUpdate, jLastDatePropsUpdate,
                                   jCopied, jDeleted, jAbsent, jIncomplete,
                                   jCopyRev, jCopyUrl);
    if (JNIUtil::isJavaExceptionThrown())
        return NULL;

    env->DeleteLocalRef(clazz);
    if (JNIUtil::isJavaExceptionThrown())
        return NULL;

    env->DeleteLocalRef(jName);
    if (JNIUtil::isJavaExceptionThrown())
        return NULL;
    env->DeleteLocalRef(jUrl);
    if (JNIUtil::isJavaExceptionThrown())
        return NULL;
    env->DeleteLocalRef(jUuid);
    if (JNIUtil::isJavaExceptionThrown())
        return NULL;
    env->DeleteLocalRef(jRepository);
    if (JNIUtil::isJavaExceptionThrown())
        return NULL;
    env->DeleteLocalRef(jAuthor);
    if (JNIUtil::isJavaExceptionThrown())
        return NULL;
    env->DeleteLocalRef(jLastChangedDate);
    if (JNIUtil::isJavaExceptionThrown())
        return NULL;
    env->DeleteLocalRef(jLastDateTextUpdate);
    if (JNIUtil::isJavaExceptionThrown())
        return NULL;
    env->DeleteLocalRef(jLastDatePropsUpdate);
    if (JNIUtil::isJavaExceptionThrown())
        return NULL;
    env->DeleteLocalRef(jCopyUrl);
    if (JNIUtil::isJavaExceptionThrown())
        return NULL;

    return jinfo;
}


jobject
CreateJ::Lock(const svn_lock_t *lock)
{
    if (lock == NULL)
        return NULL;
    JNIEnv *env = JNIUtil::getEnv();

    jclass clazz = env->FindClass(JAVA_PACKAGE"/Lock");
    if (JNIUtil::isJavaExceptionThrown())
        return NULL;

    static jmethodID mid = 0;
    if (mid == 0)
    {
        mid = env->GetMethodID(clazz, "<init>",
                               "(Ljava/lang/String;Ljava/lang/String;"
                               "Ljava/lang/String;"
                               "Ljava/lang/String;JJ)V");
        if (JNIUtil::isJavaExceptionThrown())
            return NULL;
    }

    jstring jOwner = JNIUtil::makeJString(lock->owner);
    if (JNIUtil::isJavaExceptionThrown())
        return NULL;
    jstring jPath = JNIUtil::makeJString(lock->path);
    if (JNIUtil::isJavaExceptionThrown())
        return NULL;
    jstring jToken = JNIUtil::makeJString(lock->token);
    if (JNIUtil::isJavaExceptionThrown())
        return NULL;
    jstring jComment = JNIUtil::makeJString(lock->comment);
    if (JNIUtil::isJavaExceptionThrown())
        return NULL;

    jlong jCreationDate = lock->creation_date;
    jlong jExpirationDate = lock->expiration_date;
    jobject jlock = env->NewObject(clazz, mid, jOwner, jPath, jToken, jComment,
                                   jCreationDate, jExpirationDate);
    if (JNIUtil::isJavaExceptionThrown())
        return NULL;

    env->DeleteLocalRef(clazz);
    if (JNIUtil::isJavaExceptionThrown())
        return NULL;

    env->DeleteLocalRef(jOwner);
    if (JNIUtil::isJavaExceptionThrown())
        return NULL;
    env->DeleteLocalRef(jPath);
    if (JNIUtil::isJavaExceptionThrown())
        return NULL;
    env->DeleteLocalRef(jToken);
    if (JNIUtil::isJavaExceptionThrown())
        return NULL;
    env->DeleteLocalRef(jComment);
    if (JNIUtil::isJavaExceptionThrown())
        return NULL;

    return jlock;
}

jobject
CreateJ::Property(jobject jthis, const char *path, const char *name,
                  svn_string_t *value)
{
    JNIEnv *env = JNIUtil::getEnv();
    jclass clazz = env->FindClass(JAVA_PACKAGE"/PropertyData");
    if (JNIUtil::isJavaExceptionThrown())
        return NULL;

    static jmethodID mid = 0;
    if (mid == 0)
    {
        mid = env->GetMethodID(clazz, "<init>",
                               "(L"JAVA_PACKAGE"/SVNClient;Ljava/lang/String;"
                               "Ljava/lang/String;Ljava/lang/String;[B)V");
        if (JNIUtil::isJavaExceptionThrown())
            return NULL;
    }

    jstring jPath = JNIUtil::makeJString(path);
    if (JNIUtil::isJavaExceptionThrown())
        return NULL;
    jstring jName = JNIUtil::makeJString(name);
    if (JNIUtil::isJavaExceptionThrown())
        return NULL;
    jstring jValue = JNIUtil::makeJString(value->data);
    if (JNIUtil::isJavaExceptionThrown())
        return NULL;
    jbyteArray jData = JNIUtil::makeJByteArray((const signed char *)value->data,
                                               value->len);
    if (JNIUtil::isJavaExceptionThrown())
        return NULL;

    jobject jprop = env->NewObject(clazz, mid, jthis, jPath, jName, jValue,
                                  jData);
    if (JNIUtil::isJavaExceptionThrown())
        return NULL;

    env->DeleteLocalRef(clazz);
    if (JNIUtil::isJavaExceptionThrown())
        return NULL;
    env->DeleteLocalRef(jPath);
    if (JNIUtil::isJavaExceptionThrown())
        return NULL;
    env->DeleteLocalRef(jName);
    if (JNIUtil::isJavaExceptionThrown())
        return NULL;
    env->DeleteLocalRef(jValue);
    if (JNIUtil::isJavaExceptionThrown())
        return NULL;
    env->DeleteLocalRef(jData);
    if (JNIUtil::isJavaExceptionThrown())
        return NULL;

    return jprop;
}


jobjectArray
CreateJ::RevisionRangeArray(apr_array_header_t *ranges)
{
    JNIEnv *env = JNIUtil::getEnv();

    jclass clazz = env->FindClass(JAVA_PACKAGE "/RevisionRange");
    if (JNIUtil::isJavaExceptionThrown())
        return NULL;

    jobjectArray jranges = env->NewObjectArray(ranges->nelts, clazz, NULL);

    for (int i = 0; i < ranges->nelts; ++i)
    {
        // Convert svn_merge_range_t *'s to Java RevisionRange objects.
        svn_merge_range_t *range =
            APR_ARRAY_IDX(ranges, i, svn_merge_range_t *);
        jobject jrange = RevisionRange::makeJRevisionRange(range);
        if (jrange == NULL)
            return NULL;

        env->SetObjectArrayElement(jranges, i, jrange);
        if (JNIUtil::isJavaExceptionThrown())
            return NULL;

        env->DeleteLocalRef(jrange);
        if (JNIUtil::isJavaExceptionThrown())
            return NULL;
    }

    return jranges;
}
