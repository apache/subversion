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
 * @file RevisionRanges.cpp
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

const svn_opt_revision_range_t *RevisionRange::toRange(Pool &pool) const
{
  JNIEnv *env = JNIUtil::getEnv();

  jclass clazz = env->FindClass(JAVA_PACKAGE"/RevisionRange");
  if (JNIUtil::isExceptionThrown())
    return NULL;

  static jmethodID fmid = 0;
  if (fmid == 0)
    {
      fmid = env->GetMethodID(clazz, "getFromRevision",
                              "()L"JAVA_PACKAGE"/Revision;");
      if (JNIUtil::isJavaExceptionThrown())
        return NULL;
    }

  static jmethodID tmid = 0;
  if (tmid == 0)
    {
      tmid = env->GetMethodID(clazz, "getToRevision",
                              "()L"JAVA_PACKAGE"/Revision;");
      if (JNIUtil::isJavaExceptionThrown())
        return NULL;
    }

  jobject jstartRevision = env->CallObjectMethod(m_range, fmid);
  if (JNIUtil::isExceptionThrown())
    return NULL;

  Revision startRevision(jstartRevision);
  if (JNIUtil::isExceptionThrown())
    return NULL;

  jobject jendRevision = env->CallObjectMethod(m_range, tmid);
  if (JNIUtil::isExceptionThrown())
    return NULL;

  Revision endRevision(jendRevision);
  if (JNIUtil::isExceptionThrown())
    return NULL;

  svn_opt_revision_range_t *range =
    (svn_opt_revision_range_t *) apr_palloc(pool.pool(), sizeof(*range));

  range->start = *startRevision.revision();
  if (JNIUtil::isExceptionThrown())
    return NULL;

  range->end = *endRevision.revision();
  if (JNIUtil::isExceptionThrown())
    return NULL;

  return range;
}

jobject
RevisionRange::makeJRevisionRange(svn_merge_range_t *range)
{
    JNIEnv *env = JNIUtil::getEnv();

    jclass rangeClazz = env->FindClass(JAVA_PACKAGE "/RevisionRange");
    if (JNIUtil::isJavaExceptionThrown())
        return NULL;
    static jmethodID rangeCtor = 0;
    if (rangeCtor == 0)
    {
        rangeCtor = env->GetMethodID(rangeClazz, "<init>", "(JJ)V");
        if (JNIUtil::isJavaExceptionThrown())
            return NULL;
    }
    jobject jrange = env->NewObject(rangeClazz, rangeCtor,
                                    (jlong) range->start,
                                    (jlong) range->end);
    if (JNIUtil::isJavaExceptionThrown())
        return NULL;

    return jrange;
}
