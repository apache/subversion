/**
 * @copyright
 * ====================================================================
 * Copyright (c) 2003-2005 CollabNet.  All rights reserved.
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
 * @file LockCallback.cpp
 * @brief Implementation of the class LockCallback
 */

#include "LockCallback.h"
#include "JNIUtil.h"
/**
 * Create a LockCallback object
 * @param jcallback the java callback object.
 */
LockCallback::LockCallback(jobject jcallback)
{
    m_callback = jcallback;
}
/**
 * Destroy a LockCallback object
 */
LockCallback::~LockCallback()
{
    // the m_callback does not need to be destroyed, because it is the passed 
    // in parameter to the java SVNClient.lock or SVNClient.unlock method.
}
/**
 * Callback called for a each lock or unlock operation
 * @param path      the path of the item handled
 *
 * @param doLock    flag if the operation is lock (true) or unlock (false)
 * @param jLock     the java lock describing object
 */
void LockCallback::callback(const char *path, bool doLock, jobject jLock)
{
    // if no java callback object is used -> do nothing 
    if(m_callback == NULL)
        return;
    JNIEnv *env = JNIUtil::getEnv();

    static jmethodID mid = 0; // the method id will not change during
                              // the time this library is loaded, so
                              // it can be cached. 
    if(mid == 0)
    {
        jclass clazz = env->FindClass(JAVA_PACKAGE"/LockCallback");
        if(JNIUtil::isJavaExceptionThrown())
        {
            return;
        }
        mid = env->GetMethodID(clazz, "lockNotify", 
            "(Ljava/lang/String;Z"JAVA_PACKAGE"/Lock;)V");
        if(JNIUtil::isJavaExceptionThrown() || mid == 0)
        {
            return;
        }
        env->DeleteLocalRef(clazz);
        if(JNIUtil::isJavaExceptionThrown())
        {
            return;
        }
    }

    // convert the parameters to their java relatives
    jstring jpath = JNIUtil::makeJString(path);
    if(JNIUtil::isJavaExceptionThrown())
    {
        return;
    }

    // call the java method
    env->CallVoidMethod(m_callback, mid, jpath, doLock ? JNI_TRUE:JNI_FALSE, 
        jLock);
    if(JNIUtil::isJavaExceptionThrown())
    {
        return;
    }

    // cleanup the temporary java objects
    env->DeleteLocalRef(jpath);
    if(JNIUtil::isJavaExceptionThrown())
    {
        return;
    }
}
