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
 * @file Outputer.cpp
 * @brief Implementation of the class Outputer
 */

#include "Outputer.h"
#include "JNIUtil.h"
#include "JNIByteArray.h"

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

Outputer::Outputer(jobject jthis)
{
	m_jthis = jthis;
}

Outputer::~Outputer()
{

}

svn_stream_t *Outputer::getStream(const Pool & pool)
{
	svn_stream_t *ret = svn_stream_create(this, pool.pool());
	svn_stream_set_write(ret, Outputer::write);
	svn_stream_set_close(ret, Outputer::close);
	return ret;
}

svn_error_t *Outputer::write(void *baton, const char *buffer, apr_size_t *len)
{
	JNIEnv *env = JNIUtil::getEnv();
	Outputer *that = (Outputer*)baton;
	static jmethodID mid = 0;
	if(mid == 0)
	{
		jclass clazz = env->FindClass(JAVA_PACKAGE"/OutputInterface");
		if(JNIUtil::isJavaExceptionThrown())
		{
			return SVN_NO_ERROR;
		}
		mid = env->GetMethodID(clazz, "write", "([B)I");
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
	jbyteArray data = JNIUtil::makeJByteArray((const signed char*)buffer, *len);
	if(JNIUtil::isJavaExceptionThrown())
	{
		return SVN_NO_ERROR;
	}

	jint written = env->CallIntMethod(that->m_jthis, mid, data);
	if(JNIUtil::isJavaExceptionThrown())
	{
		return SVN_NO_ERROR;
	}

	*len = written;

	return SVN_NO_ERROR;
}
svn_error_t *Outputer::close(void *baton)
{
	JNIEnv *env = JNIUtil::getEnv();
	Outputer *that = (Outputer*)baton;
	static jmethodID mid = 0;
	if(mid == 0)
	{
		jclass clazz = env->FindClass(JAVA_PACKAGE"/OutputInterface");
		if(JNIUtil::isJavaExceptionThrown())
		{
			return SVN_NO_ERROR;
		}
		mid = env->GetMethodID(clazz, "close", "()V");
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
	env->CallVoidMethod(that->m_jthis, mid);
	if(JNIUtil::isJavaExceptionThrown())
	{
		return SVN_NO_ERROR;
	}

	return SVN_NO_ERROR;
}
