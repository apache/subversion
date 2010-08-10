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
 * @file CommitMessage.cpp
 * @brief Implementation of the class CommitMessage
 */

#include "CommitMessage.h"
#include "CreateJ.h"
#include "EnumMapper.h"
#include "JNIUtil.h"
#include <apr_tables.h>
#include "svn_client.h"
#include "../include/org_apache_subversion_javahl_CommitItemStateFlags.h"

CommitMessage::CommitMessage(jobject jcommitMessage)
{
  m_jcommitMessage = jcommitMessage;
}

CommitMessage::~CommitMessage()
{
  // Since the m_jcommitMessage is a global reference, it has to be
  // deleted to allow the Java garbage collector to reclaim the
  // object.
  if (m_jcommitMessage!= NULL)
    {
      JNIEnv *env = JNIUtil::getEnv();
      env->DeleteGlobalRef(m_jcommitMessage);
    }
}

CommitMessage *CommitMessage::makeCCommitMessage(jobject jcommitMessage)
{
  // If there is no object passed into this method, there is no need
  // for a C++ holding object.
  if (jcommitMessage == NULL)
    return NULL;

  // Sanity check, that the passed Java object implements the right
  // interface.
  JNIEnv *env = JNIUtil::getEnv();
  jclass clazz = env->FindClass(JAVA_PACKAGE"/CommitMessage");
  if (JNIUtil::isJavaExceptionThrown())
    return NULL;

  if (!env->IsInstanceOf(jcommitMessage, clazz))
    {
      env->DeleteLocalRef(clazz);
      return NULL;
    }
  env->DeleteLocalRef(clazz);

  // Since the reference is longer needed then the duration of the
  // SVNClient.commtMessage, the local reference has to be converted
  // to a global reference.
  jobject myCommitMessage = env->NewGlobalRef(jcommitMessage);
  if (JNIUtil::isJavaExceptionThrown())
    return NULL;

  // create & return the holding object
  return new CommitMessage(myCommitMessage);
}

/**
 * Call the Java callback method to retrieve the commit message
 * @param commit_items  the array of the items of this commit
 * @returns the commit message
 */
jstring
CommitMessage::getCommitMessage(const apr_array_header_t *commit_items)
{
  JNIEnv *env = JNIUtil::getEnv();
  // create an Java array for the commit items
  jclass clazz = env->FindClass(JAVA_PACKAGE"/CommitItem");
  if (JNIUtil::isExceptionThrown())
    return NULL;

  // Java method ids will not change during the time this library is
  // loaded, so they can be cached.

  // Get the method id for the CommitItem constructor.
  static jmethodID midConstructor = 0;
  if (midConstructor == 0)
    {
      midConstructor = env->GetMethodID(clazz, "<init>",
                                        "(Ljava/lang/String;"
                                        "L"JAVA_PACKAGE"/NodeKind;"
                                        "ILjava/lang/String;"
                                        "Ljava/lang/String;J)V");
      if (JNIUtil::isExceptionThrown())
        return NULL;
    }

  // get the method if for the CommitMessage callback method
  static jmethodID midCallback = 0;
  if (midCallback == 0)
    {
      jclass clazz2 = env->FindClass(JAVA_PACKAGE"/CommitMessage");
      if (JNIUtil::isJavaExceptionThrown())
        return NULL;

      midCallback = env->GetMethodID(clazz2, "getLogMessage",
                                     "(Ljava/util/Set;)Ljava/lang/String;");
      if (JNIUtil::isJavaExceptionThrown())
        return NULL;

      env->DeleteLocalRef(clazz2);
    }

  // create a Java CommitItem for each of the passed in commit items
  std::vector<jobject> jitems;
  for (int i = 0; i < commit_items->nelts; ++i)
    {
      svn_client_commit_item3_t *item =
        APR_ARRAY_IDX(commit_items, i, svn_client_commit_item3_t *);

      // convert the commit item members to the match Java members
      jstring jpath = JNIUtil::makeJString(item->path);

      jobject jnodeKind = EnumMapper::mapNodeKind(item->kind);
      if (JNIUtil::isJavaExceptionThrown())
        return NULL;

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

      jstring jurl = JNIUtil::makeJString(item->url);
      if (JNIUtil::isJavaExceptionThrown())
        return NULL;

      jstring jcopyUrl = JNIUtil::makeJString(item->copyfrom_url);
      if (JNIUtil::isJavaExceptionThrown())
        return NULL;

      jlong jcopyRevision = item->revision;

      // create the Java object
      jobject jitem = env->NewObject(clazz, midConstructor, jpath,
                                     jnodeKind, jstateFlags, jurl,
                                     jcopyUrl, jcopyRevision);
      if (JNIUtil::isJavaExceptionThrown())
        return NULL;

      // release the tempory Java objects
      env->DeleteLocalRef(jpath);
      env->DeleteLocalRef(jnodeKind);
      env->DeleteLocalRef(jurl);
      env->DeleteLocalRef(jcopyUrl);

      // store the Java object into the array
      jitems.push_back(jitem);
    }

  env->DeleteLocalRef(clazz);

  // call the Java callback method
  jstring jmessage = (jstring)env->CallObjectMethod(m_jcommitMessage,
                                                    midCallback,
                                                    CreateJ::Set(jitems));
  if (JNIUtil::isJavaExceptionThrown())
    return NULL;

  return jmessage;
}
