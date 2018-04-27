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
 * @file RevisionRangeList.cpp
 * @brief Implementat of the class RevisionRangeList
 */

#include "JNIUtil.h"
#include "Iterator.h"
#include "RevisionRange.h"
#include "RevisionRangeList.h"

RevisionRangeList::RevisionRangeList(jobject jrangelist, SVN::Pool &pool)
  : m_rangelist(NULL)
{
  if (!jrangelist)
    return;

  Iterator iter(jrangelist);
  if (JNIUtil::isJavaExceptionThrown())
    return;

  m_rangelist = apr_array_make(pool.getPool(), 0, sizeof(svn_merge_range_t*));
  while (iter.hasNext())
    {
      jobject next = iter.next();
      if (JNIUtil::isExceptionThrown())
        return;
      svn_merge_range_t* range = RevisionRange(next).toMergeRange(pool);
      if (JNIUtil::isExceptionThrown())
        return;
      APR_ARRAY_PUSH(m_rangelist, svn_merge_range_t*) = range;
    }
}

RevisionRangeList RevisionRangeList::create(jobject jthis, SVN::Pool &pool)
{
  jobject jrangelist = NULL;

  if (jthis)
    {
      JNIEnv *env = JNIUtil::getEnv();

      jmethodID mid = 0;
      if (mid == 0)
        {
          jclass cls = env->FindClass(JAVAHL_CLASS("/types/RevisionRangeList"));
          if (JNIUtil::isJavaExceptionThrown())
            return RevisionRangeList(NULL, pool);

          mid = env->GetMethodID(cls, "getRanges", "()Ljava/util/List;");
          if (JNIUtil::isJavaExceptionThrown())
            return RevisionRangeList(NULL, pool);
        }

      jrangelist = env->CallObjectMethod(jthis, mid);
      if (JNIUtil::isJavaExceptionThrown())
        return RevisionRangeList(NULL, pool);
    }
  return RevisionRangeList(jrangelist, pool);
}

jobject RevisionRangeList::toList() const
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

  for (int i = 0; i < m_rangelist->nelts; ++i)
    {
      // Convert svn_merge_range_t *'s to Java RevisionRange objects.
      svn_merge_range_t *range =
        APR_ARRAY_IDX(m_rangelist, i, svn_merge_range_t *);

      jobject jrange = RevisionRange::makeJRevisionRange(range);
      if (JNIUtil::isJavaExceptionThrown())
        POP_AND_RETURN_NULL;

      env->CallBooleanMethod(jranges, add_mid, jrange);
      if (JNIUtil::isJavaExceptionThrown())
        POP_AND_RETURN_NULL;

      env->DeleteLocalRef(jrange);
    }

  return env->PopLocalFrame(jranges);
}
