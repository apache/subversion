// CommitMessage.cpp: implementation of the CommitMessage class.
//
//////////////////////////////////////////////////////////////////////

#include "CommitMessage.h"
#include "JNIUtil.h"
#include <apr_tables.h>
#include "svn_client.h"
#include "../include/org_tigris_subversion_javahl_CommitItemStateFlags.h"
//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

CommitMessage::CommitMessage(jobject jcommitMessage)
{
    m_jcommitMessage = jcommitMessage;
}

CommitMessage::~CommitMessage()
{
	if(m_jcommitMessage!= NULL)
	{
		JNIEnv *env = JNIUtil::getEnv();
		env->DeleteGlobalRef(m_jcommitMessage);
	}

}

CommitMessage * CommitMessage::makeCCommitMessage(jobject jcommitMessage)
{
	if(jcommitMessage == NULL)
	{
		return NULL;
	}
	JNIEnv *env = JNIUtil::getEnv();
	jclass clazz = env->FindClass(JAVA_PACKAGE"/CommitMessage");
	if(JNIUtil::isJavaExceptionThrown())
	{
		return NULL;
	}
	if(!env->IsInstanceOf(jcommitMessage, clazz))
	{
		env->DeleteLocalRef(clazz);
		return NULL;
	}
	env->DeleteLocalRef(clazz);
	if(JNIUtil::isJavaExceptionThrown())
	{
		return NULL;
	}
	jobject myCommitMessage = env->NewGlobalRef(jcommitMessage);
	if(JNIUtil::isJavaExceptionThrown())
	{
		return NULL;
	}
    return new CommitMessage(myCommitMessage);
}

jstring CommitMessage::getCommitMessage(apr_array_header_t *commit_items)
{
    int count = commit_items->nelts;
    JNIEnv *env = JNIUtil::getEnv();
    jclass clazz = env->FindClass(JAVA_PACKAGE"/CommitItem");
    if(JNIUtil::isExceptionThrown())
    {
        return NULL;
    }
    jobjectArray jitems = env->NewObjectArray(count, clazz, NULL);
    if(JNIUtil::isExceptionThrown())
    {
        return NULL;
    }
    static jmethodID midConstructor = 0;
    if(midConstructor == 0)
    {
        midConstructor = env->GetMethodID(clazz, "<init>", 
            "(Ljava/lang/String;IILjava/lang/String;Ljava/lang/String;J)V");
        if(JNIUtil::isExceptionThrown())
        {
            return NULL;
        }
    }
    static jmethodID midCallback = 0;
    if(midCallback == 0)
    {
	    jclass clazz2 = env->FindClass(JAVA_PACKAGE"/CommitMessage");
	    if(JNIUtil::isJavaExceptionThrown())
	    {
		    return NULL;
	    }
        midCallback = env->GetMethodID(clazz2, "getLogMessage",
            "([L"JAVA_PACKAGE"/CommitItem;)Ljava/lang/String;");
	    if(JNIUtil::isJavaExceptionThrown())
	    {
		    return NULL;
	    }
        env->DeleteLocalRef(clazz2);
	    if(JNIUtil::isJavaExceptionThrown())
	    {
		    return NULL;
	    }
    }
    for(int i = 0; i < count; i++)
    {
        svn_client_commit_item_t *item
            = ((svn_client_commit_item_t **) commit_items->elts)[i];
        jstring jpath = JNIUtil::makeJString(item->path);
        jint jnodeKind = item->kind;
        jint jstateFlags = 0;
        if(item->state_flags & SVN_CLIENT_COMMIT_ITEM_ADD)
            jstateFlags |= 
                org_tigris_subversion_javahl_CommitItemStateFlags_Add;
        if(item->state_flags & SVN_CLIENT_COMMIT_ITEM_DELETE)
            jstateFlags |= 
                org_tigris_subversion_javahl_CommitItemStateFlags_Delete;
        if(item->state_flags & SVN_CLIENT_COMMIT_ITEM_TEXT_MODS)
            jstateFlags |= 
                org_tigris_subversion_javahl_CommitItemStateFlags_TextMods;
        if(item->state_flags & SVN_CLIENT_COMMIT_ITEM_PROP_MODS)
            jstateFlags |= 
                org_tigris_subversion_javahl_CommitItemStateFlags_PropMods;
        if(item->state_flags & SVN_CLIENT_COMMIT_ITEM_IS_COPY)
            jstateFlags |= 
                org_tigris_subversion_javahl_CommitItemStateFlags_IsCopy;
        jstring jurl = JNIUtil::makeJString(item->url);
        jstring jcopyUrl = JNIUtil::makeJString(item->copyfrom_url);
        jlong jcopyRevision = item->revision;

        jobject jitem = env->NewObject(clazz, midConstructor, jpath,
            jnodeKind, jstateFlags, jurl, jcopyUrl, jcopyRevision);
	    if(JNIUtil::isJavaExceptionThrown())
	    {
		    return NULL;
	    }
        env->SetObjectArrayElement(jitems, i, jitem);
	    if(JNIUtil::isJavaExceptionThrown())
	    {
		    return NULL;
	    }
    }
    jstring jmessage = (jstring)env->CallObjectMethod(m_jcommitMessage, 
                                            midCallback, jitems);
	if(JNIUtil::isJavaExceptionThrown())
	{
		return NULL;
	}
    env->DeleteLocalRef(clazz);
	if(JNIUtil::isJavaExceptionThrown())
	{
		return NULL;
	}
    return jmessage;
}
