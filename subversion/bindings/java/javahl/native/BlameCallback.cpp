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
#include <svn_time.h>
//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

BlameCallback::BlameCallback(jobject jcallback)
{
	m_callback = jcallback;
}

BlameCallback::~BlameCallback()
{

}
void BlameCallback::callback(svn_revnum_t revision, const char *author, const char *date, const char *line, apr_pool_t *pool)
{
	static jmethodID mid = 0;
	JNIEnv *env = JNIUtil::getEnv();
	if(mid == 0)
	{
		jclass clazz = env->FindClass(JAVA_PACKAGE"/BlameCallback");
		if(JNIUtil::isJavaExceptionThrown())
		{
			return;
		}
		mid = env->GetMethodID(clazz, "singleLine", "(Ljava/util/Date;JLjava/lang/String;Ljava/lang/String;)V");
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

	jstring jauthor = JNIUtil::makeJString(author);
	if(JNIUtil::isJavaExceptionThrown())
	{
		return;
	}
    apr_time_t timeTemp;
    svn_time_from_cstring (&timeTemp, date, pool);

	jobject jdate = JNIUtil::createDate(timeTemp);
	if(JNIUtil::isJavaExceptionThrown())
	{
		return;
	}
	jstring jline = JNIUtil::makeJString(line);
	if(JNIUtil::isJavaExceptionThrown())
	{
		return;
	}

	env->CallVoidMethod(m_callback, mid, jdate, (jlong)revision, jauthor, jline);
	if(JNIUtil::isJavaExceptionThrown())
	{
		return;
	}
	env->DeleteLocalRef(jline);
	if(JNIUtil::isJavaExceptionThrown())
	{
		return;
	}
	env->DeleteLocalRef(jauthor);
	if(JNIUtil::isJavaExceptionThrown())
	{
		return;
	}
	env->DeleteLocalRef(jdate);
	if(JNIUtil::isJavaExceptionThrown())
	{
		return;
	}
}
