// Inputer.cpp: implementation of the Inputer class.
//
//////////////////////////////////////////////////////////////////////

#include "Inputer.h"
#include "JNIUtil.h"
#include "JNIByteArray.h"
//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

Inputer::Inputer(jobject jthis)
{
	m_jthis = jthis;
}

Inputer::~Inputer()
{

}

svn_stream_t *Inputer::getStream(const Pool & pool)
{
	svn_stream_t *ret = svn_stream_create(this, pool.pool());
	svn_stream_set_read(ret, Inputer::read);
	svn_stream_set_close(ret, Inputer::close);
	return ret;
}

svn_error_t *Inputer::read(void *baton, char *buffer, apr_size_t *len)
{
	JNIEnv *env = JNIUtil::getEnv();
	Inputer *that = (Inputer*)baton;
	static jmethodID mid = 0;
	if(mid == 0)
	{
		jclass clazz = env->FindClass(JAVA_PACKAGE"/InputInterface");
		if(JNIUtil::isJavaExceptionThrown())
		{
			return SVN_NO_ERROR;
		}
		mid = env->GetMethodID(clazz, "read", "([B)I");
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

	jint jread = env->CallIntMethod(that->m_jthis, mid, data);
	if(JNIUtil::isJavaExceptionThrown())
	{
		return SVN_NO_ERROR;
	}

	JNIByteArray outdata(data);
	if(JNIUtil::isJavaExceptionThrown())
	{
		return SVN_NO_ERROR;
	}
	if(jread > 0)
		memcpy(buffer, outdata.getBytes(), jread);
	*len = jread;

	return SVN_NO_ERROR;
}
svn_error_t *Inputer::close(void *baton)
{
	JNIEnv *env = JNIUtil::getEnv();
	Inputer *that = (Inputer*)baton;
	static jmethodID mid = 0;
	if(mid == 0)
	{
		jclass clazz = env->FindClass(JAVA_PACKAGE"/InputInterface");
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
