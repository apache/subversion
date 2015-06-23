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
 * @file RevisionRange.cpp
 * @brief Implementation of the class RevisionRange.
 */

#include <apr_pools.h>
#include "svn_client.h"
#include "svn_types.h"

#include "JNIUtil.h"
#include "RevisionRange.h"
#include "Revision.h"
#include "Pool.h"

RevisionRange::RevisionRange(jobject jrevisionRange)
{
    m_range = jrevisionRange;
}

RevisionRange::~RevisionRange()
{
    // m_range is assume to be managed externally, and thus is not
    // explicitly destroyed.
}

namespace {
void get_range_info(jobject jrange,
                    svn_opt_revision_t* range_start,
                    svn_opt_revision_t* range_end,
                    svn_boolean_t* range_inheritable)
{
  JNIEnv *env = JNIUtil::getEnv();

  jclass clazz = env->FindClass(JAVAHL_CLASS("/types/RevisionRange"));
  if (JNIUtil::isExceptionThrown())
    return;

  if (range_start)
    {

      static jmethodID fmid = 0;
      if (fmid == 0)
        {
          fmid = env->GetMethodID(clazz, "getFromRevision",
                                  "()" JAVAHL_ARG("/types/Revision;"));
          if (JNIUtil::isJavaExceptionThrown())
            return;
        }

      jobject jstartRevision = env->CallObjectMethod(jrange, fmid);
      if (JNIUtil::isExceptionThrown())
        return;

      Revision startRevision(jstartRevision);
      if (JNIUtil::isExceptionThrown())
        return;

      *range_start = *startRevision.revision();
      if (JNIUtil::isExceptionThrown())
        return;
    }

  if (range_end)
    {
      static jmethodID tmid = 0;
      if (tmid == 0)
        {
          tmid = env->GetMethodID(clazz, "getToRevision",
                                  "()" JAVAHL_ARG("/types/Revision;"));
          if (JNIUtil::isJavaExceptionThrown())
            return;
        }

      jobject jendRevision = env->CallObjectMethod(jrange, tmid);
      if (JNIUtil::isExceptionThrown())
        return;

      Revision endRevision(jendRevision);
      if (JNIUtil::isExceptionThrown())
        return;

      *range_end = *endRevision.revision();
      if (JNIUtil::isExceptionThrown())
        return;
    }

  if (range_inheritable)
    {
      static jmethodID imid = 0;
      if (imid == 0 && range_inheritable)
        {
          imid = env->GetMethodID(clazz, "isInheritable", "()Z");
          if (JNIUtil::isJavaExceptionThrown())
            return;
        }

      jboolean inheritable = env->CallBooleanMethod(jrange, imid);
      if (JNIUtil::isExceptionThrown())
        return;
      *range_inheritable = inheritable;
    }
}
} // anonymous namespace

svn_merge_range_t* RevisionRange::toMergeRange(SVN::Pool &pool) const
{
  svn_opt_revision_t range_start, range_end;
  svn_boolean_t range_inheritable;
  get_range_info(m_range, &range_start, &range_end, &range_inheritable);
  if (JNIUtil::isExceptionThrown())
    return NULL;

  if (range_start.kind != svn_opt_revision_number
      || range_end.kind != svn_opt_revision_number)
    JNIUtil::raiseThrowable("java.lang.InvalidStateException",
                            "Revsision ranges must contain revision numbers");

  svn_merge_range_t* range =
    static_cast<svn_merge_range_t*>
      (apr_palloc(pool.getPool(), sizeof(*range)));

  range->start = range_start.value.number;
  range->end = range_end.value.number;
  range->inheritable = range_inheritable;
  return range;
}

svn_opt_revision_range_t *RevisionRange::toRange(SVN::Pool &pool) const
{
  svn_opt_revision_range_t *range =
    static_cast<svn_opt_revision_range_t *>
      (apr_palloc(pool.getPool(), sizeof(*range)));

  get_range_info(m_range, &range->start, &range->end, NULL);
  if (JNIUtil::isExceptionThrown())
    range = NULL;
  return range;
}

jobject
RevisionRange::makeJRevisionRange(svn_merge_range_t *range)
{
    JNIEnv *env = JNIUtil::getEnv();

    jclass rangeClazz = env->FindClass(JAVAHL_CLASS("/types/RevisionRange"));
    if (JNIUtil::isJavaExceptionThrown())
        return NULL;
    static jmethodID rangeCtor = 0;
    if (rangeCtor == 0)
    {
        rangeCtor = env->GetMethodID(rangeClazz, "<init>", "(JJZ)V");
        if (JNIUtil::isJavaExceptionThrown())
            return NULL;
    }
    jobject jrange = env->NewObject(rangeClazz, rangeCtor,
                                    jlong(range->start),
                                    jlong(range->end),
                                    jboolean(range->inheritable));
    if (JNIUtil::isJavaExceptionThrown())
        return NULL;

    return jrange;
}
