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
    findCppAddrFieldID(fid, className, env);
    if (*fid == 0)
    {
        return 0;
    }
    else
    {
        jlong cppAddr = env->GetLongField(jthis, *fid);
        return (JNIUtil::isJavaExceptionThrown() ? 0 : cppAddr);
    }
}

void SVNBase::finalize()
{
    // This object should've already been disposed of!
    if (JNIUtil::getLogLevel() >= JNIUtil::errorLog)
    {
        JNIUtil::logMessage("An SVNBase object escaped disposal");
    }
    JNIUtil::enqueueForDeletion(this);
}

void SVNBase::dispose(jobject jthis, jfieldID *fid, const char *className)
{
    delete this;
    JNIEnv *env = JNIUtil::getEnv();
    SVNBase::findCppAddrFieldID(fid, className, env);
    if (*fid == 0)
    {
	return;
    }
    env->SetLongField(jthis, *fid, 0);
    if (JNIUtil::isJavaExceptionThrown())
    {
        return;
    }
}

inline void SVNBase::findCppAddrFieldID(jfieldID *fid, const char *className,
                                        JNIEnv *env)
{
    if (*fid == 0)
    {
        jclass clazz = env->FindClass(className);
        if (!JNIUtil::isJavaExceptionThrown())
        {
            *fid = env->GetFieldID(clazz, "cppAddr", "J");
            if (JNIUtil::isJavaExceptionThrown())
            {
                *fid = 0;
            }
        }
    }
}
