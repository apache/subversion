/**
 * @copyright
 * ====================================================================
 * Copyright (c) 2003-2004 CollabNet.  All rights reserved.
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
 * @file BlameCall.cpp
 * @brief Implementation of the class BlameCallback
 */

#include "BlameCallback.h"
#include "JNIUtil.h"
#include "svn_time.h"
/**
 * Create a BlameCallback object
 * @param jcallback the java callback object.
 */
BlameCallback::BlameCallback(jobject jcallback)
{
    m_callback = jcallback;
}
/**
 * Destroy a BlameCallback object
 */
BlameCallback::~BlameCallback()
{
    // the m_callback does not need to be destroyed, because it is the passed 
    // in parameter to the java SVNClient.blame method.
}
/**
 * Callback called for a single line in the file, for which the blame 
 * information was requested
 * @param revision  the revision number, when the line was last changed
 *                  or -1, if not changed during the request revision
 *                  interval
 *
 * @param author    the author, who performed the last change of the line
 * @param date      the date of the last change of the line
 * @param line      the content of the line
 * @param pool      memory pool for the use of this function
 */
svn_error_t* BlameCallback::callback(svn_revnum_t revision, const char *author, 
                             const char *date, const char *line, 
                             apr_pool_t *pool)
{
    JNIEnv *env = JNIUtil::getEnv();

    static jmethodID mid = 0; // the method id will not change during
                              // the time this library is loaded, so
                              // it can be cached. 
    if(mid == 0)
    {
        jclass clazz = env->FindClass(JAVA_PACKAGE"/BlameCallback");
        if(JNIUtil::isJavaExceptionThrown())
        {
            return SVN_NO_ERROR;
        }
        mid = env->GetMethodID(clazz, "singleLine", 
            "(Ljava/util/Date;JLjava/lang/String;Ljava/lang/String;)V");
        if(JNIUtil::isJavaExceptionThrown() || mid == 0)
        {
            return SVN_NO_ERROR;
        }
        env->DeleteLocalRef(clazz);
        if(JNIUtil::isJavaExceptionThrown())
        {
            return SVN_NO_ERROR;
        }
    }

    // convert the parameters to their java relatives
    jstring jauthor = JNIUtil::makeJString(author);
    if(JNIUtil::isJavaExceptionThrown())
    {
        return SVN_NO_ERROR;
    }
    jobject jdate = NULL;
    if(date != NULL && *date != '\0')
    {
        apr_time_t timeTemp;
        svn_error_t *err = svn_time_from_cstring (&timeTemp, date, pool);
        if(err != SVN_NO_ERROR)
            return err;

        jdate = JNIUtil::createDate(timeTemp);
        if(JNIUtil::isJavaExceptionThrown())
        {
            return SVN_NO_ERROR;
        }
    }
    jstring jline = JNIUtil::makeJString(line);
    if(JNIUtil::isJavaExceptionThrown())
    {
        return SVN_NO_ERROR;
    }

    // call the java method
    env->CallVoidMethod(m_callback, mid, jdate, (jlong)revision, jauthor, 
        jline);
    if(JNIUtil::isJavaExceptionThrown())
    {
        return SVN_NO_ERROR;
    }

    // cleanup the temporary java objects
    env->DeleteLocalRef(jline);
    if(JNIUtil::isJavaExceptionThrown())
    {
        return SVN_NO_ERROR;
    }
    env->DeleteLocalRef(jauthor);
    if(JNIUtil::isJavaExceptionThrown())
    {
        return SVN_NO_ERROR;
    }
    env->DeleteLocalRef(jdate);
    if(JNIUtil::isJavaExceptionThrown())
    {
        return SVN_NO_ERROR;
    }
    return SVN_NO_ERROR;
}
