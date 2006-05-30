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
 * @file Notify2.cpp
 * @brief Implementation of the class Notify2
 */

#include "Notify2.h"
#include "JNIUtil.h"
#include "SVNClient.h"
#include "EnumMapper.h"
//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////
/**
 * Create a new object and store the java object
 * @param notify    global reference to the java object
 */
Notify2::Notify2(jobject p_notify)
{
    m_notify = p_notify;
}

/**
 * Destroy the object and delete the global reference to the java object
 */
Notify2::~Notify2()
{
    if(m_notify != NULL)
    {
        JNIEnv *env = JNIUtil::getEnv();
        env->DeleteGlobalRef(m_notify);
    }
}

/**
 * Create a C++ peer object for the java object
 * @param notify    a local reference to the java object
 */
Notify2 * Notify2::makeCNotify(jobject notify)
{
    // if the java object is null -> no C++ peer needed
    if(notify == NULL)
        return NULL;
    JNIEnv *env = JNIUtil::getEnv();

    // sanity check, that the object implements Notify
    jclass clazz = env->FindClass(JAVA_PACKAGE"/Notify2");
    if(JNIUtil::isJavaExceptionThrown())
    {
        return NULL;
    }
    if(!env->IsInstanceOf(notify, clazz))
    {
        env->DeleteLocalRef(clazz);
        return NULL;
    }
    env->DeleteLocalRef(clazz);
    if(JNIUtil::isJavaExceptionThrown())
    {
        return NULL;
    }

    // make a global reference, because the reference is longer needed, than
    // the call
    jobject myNotify = env->NewGlobalRef(notify);
    if(JNIUtil::isJavaExceptionThrown())
    {
        return NULL;
    }

    // create the peer
    return new Notify2(myNotify);
}
  /**
   * notification function passed as svn_wc_notify_func2_t
   * @param baton notification instance is passed using this parameter
   * @param notify all the information about the event
   * @param pool an apr pool to allocated memory
   */

void
Notify2::notify (
    void *baton,
    const svn_wc_notify_t *notify,
    apr_pool_t *pool)
{
    // an Notify object is used as the baton
    Notify2 * that = (Notify2 *) baton;
    if(that) // sanity check
    {
        // call our method
        that->onNotify(notify, pool);
    }
}
  /**
   * Handler for Subversion notifications.
   *
   * @param notify all the information about the event
   * @param pool an apr pool to allocated memory
   */
void
Notify2::onNotify (
    const svn_wc_notify_t *wcNotify,
    apr_pool_t *pool)
{
    JNIEnv *env = JNIUtil::getEnv();
    // java method id will not change during the time this library is loaded,
    // so it can be cached.
    static jmethodID mid = 0;
    if(mid == 0)
    {
        jclass clazz = env->FindClass(JAVA_PACKAGE"/Notify2");
        if(JNIUtil::isJavaExceptionThrown())
        {
            return;
        }
        mid = env->GetMethodID(clazz, "onNotify", 
            "(Lorg/tigris/subversion/javahl/NotifyInformation;)V");
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
    static jmethodID midCT = 0;
    jclass clazz = env->FindClass(JAVA_PACKAGE"/NotifyInformation");
    if(JNIUtil::isJavaExceptionThrown())
    {
        return;
    }
    if(midCT == 0)
    {
        midCT = env->GetMethodID(clazz, "<init>", 
            "(Ljava/lang/String;IILjava/lang/String;"
            "Lorg/tigris/subversion/javahl/Lock;Ljava/lang/String;IIIJ)V");
        if(JNIUtil::isJavaExceptionThrown() || mid == 0)
        {
            return;
        }
    }

    // convert the parameter to their java relatives
    jstring jPath = JNIUtil::makeJString(wcNotify->path);
    if(JNIUtil::isJavaExceptionThrown())
    {
        return;
    }

    jint jAction = EnumMapper::mapNotifyAction(wcNotify->action);
    jint jKind = EnumMapper::mapNodeKind(wcNotify->kind);
    jstring jMimeType = JNIUtil::makeJString(wcNotify->mime_type);
    if(JNIUtil::isJavaExceptionThrown())
    {
        return;
    }
    jobject jLock = SVNClient::createJavaLock(wcNotify->lock);
    if(JNIUtil::isJavaExceptionThrown())
    {
        return;
    }
    jstring jErr = JNIUtil::makeSVNErrorMessage(wcNotify->err);
    if(JNIUtil::isJavaExceptionThrown())
    {
        return;
    }
    jint jContentState = EnumMapper::mapNotifyState(wcNotify->content_state);
    jint jPropState = EnumMapper::mapNotifyState(wcNotify->prop_state);
    jint jLockState = EnumMapper::mapNotifyLockState(wcNotify->lock_state);
    // call the java method
    jobject jInfo = env->NewObject(clazz, midCT, jPath, jAction, jKind, 
        jMimeType, jLock, jErr, jContentState, jPropState, jLockState, 
        (jlong) wcNotify->revision);
    if(JNIUtil::isJavaExceptionThrown())
    {
        return;
    }

    // release all the temporary java objects
    env->DeleteLocalRef(jPath);
    if(JNIUtil::isJavaExceptionThrown())
    {
        return;
    }
    env->DeleteLocalRef(jMimeType);
    if(JNIUtil::isJavaExceptionThrown())
    {
        return;
    }
    env->DeleteLocalRef(jErr);
    if(JNIUtil::isJavaExceptionThrown())
    {
        return;
    }
    env->DeleteLocalRef(jLock);
    if(JNIUtil::isJavaExceptionThrown())
    {
        return;
    }
    env->DeleteLocalRef(clazz);
    if(JNIUtil::isJavaExceptionThrown())
    {
        return;
    }

    env->CallVoidMethod(m_notify, mid, jInfo);
    if(JNIUtil::isJavaExceptionThrown())
    {
        return;
    }

    env->DeleteLocalRef(jInfo);
    if(JNIUtil::isJavaExceptionThrown())
    {
        return;
    }

}
