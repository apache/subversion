// MessageReceiver.cpp: implementation of the MessageReceiver class.
//
//////////////////////////////////////////////////////////////////////

#include "MessageReceiver.h"
#include "JNIUtil.h"
//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

MessageReceiver::MessageReceiver(jobject jthis)
{
	m_jthis = jthis;
}

MessageReceiver::~MessageReceiver()
{

}
void MessageReceiver::receiveMessage(const char *message)
{
	JNIEnv *env = JNIUtil::getEnv();
	static jmethodID mid = 0;
	if(mid == 0)
	{
		jclass clazz = env->FindClass(JAVA_PACKAGE"/SVNAdmin$MessageReceiver");
		if(JNIUtil::isJavaExceptionThrown())
		{
			return;
		}
		mid = env->GetMethodID(clazz, "receiveMessageLine", "(Ljava/lang/String;)V");
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
	jstring jmsg = JNIUtil::makeJString(message);
	if(JNIUtil::isJavaExceptionThrown())
	{
		return;
	}

	env->CallVoidMethod(m_jthis, mid);
	if(JNIUtil::isJavaExceptionThrown())
	{
		return;
	}

	env->DeleteLocalRef(jmsg);
	if(JNIUtil::isJavaExceptionThrown())
	{
		return;
	}
}
