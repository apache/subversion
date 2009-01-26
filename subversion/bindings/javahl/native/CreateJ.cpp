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
