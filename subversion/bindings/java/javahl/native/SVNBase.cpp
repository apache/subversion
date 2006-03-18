/**
 * @copyright
 * ====================================================================
 * Copyright (c) 2003-2006 CollabNet.  All rights reserved.
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
 * @file SVNBase.cpp
 * @brief Implementation of the class SVNBase
 */

#include "SVNBase.h"
#include "JNIUtil.h"

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

SVNBase::SVNBase()
{

}

SVNBase::~SVNBase()
{

}

jlong SVNBase::getCppAddr()
{
    return reinterpret_cast<jlong>(this);
}

jlong SVNBase::findCppAddrForJObject(jobject jthis, jfieldID *fid,
                                     const char *className)
{
    JNIEnv *env = JNIUtil::getEnv();
    if (*fid == 0)
    {
        jclass clazz = env->FindClass(className);
        if (JNIUtil::isJavaExceptionThrown())
        {
            return 0;
        }

        *fid = env->GetFieldID(clazz, "cppAddr", "J");
        if (JNIUtil::isJavaExceptionThrown())
        {
            return 0;
        }
    }
    jlong cppAddr = env->GetLongField(jthis, *fid);
    return (JNIUtil::isJavaExceptionThrown() ? 0 : cppAddr);
}
