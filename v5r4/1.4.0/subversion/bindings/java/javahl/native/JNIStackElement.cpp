/**
 * @copyright
 * ====================================================================
 * Copyright (c) 2003 CollabNet.  All rights reserved.
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
 * @file JNIStackElement.cpp
 * @brief Implementation of the class JNIStackElement
 */

#include "JNIStackElement.h"
#include "JNIUtil.h"
#include "JNIStringHolder.h"
#include "JNIThreadData.h"
#include <apr_strings.h>
/**
 * Create a new object and generate a log message,if requested
 * @param env       the JNI-environment
 * @param clazz     the class name of the method
 * @param method    the name of the method
 * @param jthis     the java object for which the method is call
 */
JNIStackElement::JNIStackElement(JNIEnv *env, const char *clazz, 
                                 const char *method, jobject jthis)
{
    JNIUtil::JNIInit(env);
    // generating a log message is expensive
    if(JNIUtil::getLogLevel() >= JNIUtil::entryLog)
    {
        jclass jlo = env->FindClass("java/lang/Object");
        if(JNIUtil::isJavaExceptionThrown())
        {
            return;
        }
        // the method id will not change during
        // the time this library is loaded, so
        // it can be cached.
        static jmethodID mid = 0;
        if(mid == 0)
        {
            mid = env->GetMethodID(jlo, "toString", "()Ljava/lang/String;");
            if(JNIUtil::isJavaExceptionThrown())
            {
                return;
            }
        }

        // this will call java.lang.Object.toString, even when it is overriden.
        jobject oStr = env->CallNonvirtualObjectMethod(jthis, jlo, mid);
        if(JNIUtil::isJavaExceptionThrown())
        {
            return;
        }

        // copy the result to a buffer
        JNIStringHolder name(reinterpret_cast<jstring>(oStr));
        *m_objectID = 0;
        strncat(m_objectID, name, JNIUtil::formatBufferSize -1);

        // release the java string
        env->DeleteLocalRef(jlo);
        if(JNIUtil::isJavaExceptionThrown())
        {
            return;
        }

        env->DeleteLocalRef(jlo);
        if(JNIUtil::isJavaExceptionThrown())
        {
            return;
        }

        // remember the parameter for the exit of the method
        m_clazz = clazz;
        m_method = method;

        // generate the log message
        char *buffer = JNIUtil::getFormatBuffer();
        apr_snprintf(buffer, JNIUtil::formatBufferSize, 
            "entry class %s method %s object %s", m_clazz, m_method, 
            m_objectID);
        JNIUtil::logMessage(buffer);
    }
    else
    {
        // initialize the object cleanly
        m_clazz = NULL;
        m_method = NULL;
        *m_objectID = 0;
    }
}
/**
 * Destroy an object and create a log message for the exit of the method, if 
 * one was create for the entry
 */
JNIStackElement::~JNIStackElement()
{
    if(m_clazz != NULL)
    {
        // generate a log message
        char *buffer = JNIUtil::getFormatBuffer();
        apr_snprintf(buffer, JNIUtil::formatBufferSize, "exit class %s method %s object %s", m_clazz, m_method, m_objectID);
        JNIUtil::logMessage(buffer);
    }
	JNIThreadData::popThreadData();
}
