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

RevisionRange::RevisionRange(jobject jrevisionRange)
{
    m_range = jrevisionRange;
}

RevisionRange::~RevisionRange()
{
    // m_range is assume to be managed externally, and thus is not
    // explicitly destroyed.
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
