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
#include <apr_strings.h>

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

JNIStackElement::JNIStackElement(JNIEnv *env, const char *clazz, const char *method, jobject jthis)
{
	JNIUtil::JNIInit(env);
	if(JNIUtil::getLogLevel() >= JNIUtil::entryLog)
	{
		jclass jlo = env->FindClass("java/lang/Object");
		if(JNIUtil::isJavaExceptionThrown())
		{
			return;
		}
		jmethodID mid = env->GetMethodID(jlo, "toString", "()Ljava/lang/String;");
		if(JNIUtil::isJavaExceptionThrown())
		{
			return;
		}
		jobject oStr = env->CallNonvirtualObjectMethod(jthis, jlo, mid);
		if(JNIUtil::isJavaExceptionThrown())
		{
			return;
		}
		JNIStringHolder name(reinterpret_cast<jstring>(oStr));
		*m_objectID = 0;
		strncat(m_objectID, name, JNIUtil::formatBufferSize -1);
		env->DeleteLocalRef(jlo);
		if(JNIUtil::isJavaExceptionThrown())
		{
			return;
		}
		m_clazz = clazz;
		m_method = method;
		char *buffer = JNIUtil::getFormatBuffer();
		apr_snprintf(buffer, JNIUtil::formatBufferSize, "entry class %s method %s object %s", m_clazz, m_method, m_objectID);
		JNIUtil::logMessage(buffer);
	}
	else
	{
		m_clazz = NULL;
		m_method = NULL;
		*m_objectID = 0;
	}
}

JNIStackElement::~JNIStackElement()
{
	JNIUtil::setEnv(NULL);
	if(m_clazz != NULL)
	{
		char *buffer = JNIUtil::getFormatBuffer();
		apr_snprintf(buffer, JNIUtil::formatBufferSize, "exit class %s method %s object %s", m_clazz, m_method, m_objectID);
		JNIUtil::logMessage(buffer);
	}
}
