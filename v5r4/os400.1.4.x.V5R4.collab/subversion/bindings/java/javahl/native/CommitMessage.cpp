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
 * @file CommitMessage.cpp
 * @brief Implementation of the class CommitMessage
 */

#include "CommitMessage.h"
#include "JNIUtil.h"
#include <apr_tables.h>
#include "svn_client.h"
#include "../include/org_tigris_subversion_javahl_CommitItemStateFlags.h"
/**
 * Create a commit message object
 * @param jcommitMessage    the java object to receive the callback
 */
CommitMessage::CommitMessage(jobject jcommitMessage)
{
    m_jcommitMessage = jcommitMessage;
}
/**
 * Destroy a commit message object
 */
CommitMessage::~CommitMessage()
{
    // since the m_jcommitMessage is a global reference, it has to be deleted
    // to allow the java garbage collector to reclaim the object.
    if(m_jcommitMessage!= NULL)
    {
        JNIEnv *env = JNIUtil::getEnv();
        env->DeleteGlobalRef(m_jcommitMessage);
    }
}
/**
 * Create a C++ holding object for the java object passed into the native code
 * @param jcommitMessage    local reference to the java object
 */
CommitMessage * CommitMessage::makeCCommitMessage(jobject jcommitMessage)
{
    // if there is no object passed into this method, there is no need for a 
    // C++ holding object
    if(jcommitMessage == NULL)
    {
        return NULL;
    }

    // Sanity check, that the passed java object implements the right interface
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

    // Since the reference is longer needed then the duration of the 
    // SVNClient.commtMessage, the local reference has to be converted to a 
    // global reference
    jobject myCommitMessage = env->NewGlobalRef(jcommitMessage);
    if(JNIUtil::isJavaExceptionThrown())
    {
        return NULL;
    }

    // create & return the holding object
    return new CommitMessage(myCommitMessage);
}
/**
 * Call the java callback method to retrieve the commit message
 * @param commit_items  the array of the items of this commit
 * @returns the commit message
 */
jstring CommitMessage::getCommitMessage(apr_array_header_t *commit_items)
{
    JNIEnv *env = JNIUtil::getEnv();
    // create an java array for the commit items
    jclass clazz = env->FindClass(JAVA_PACKAGE"/CommitItem");
    if(JNIUtil::isExceptionThrown())
    {
        return NULL;
    }
    int count = commit_items->nelts;
    jobjectArray jitems = env->NewObjectArray(count, clazz, NULL);
    if(JNIUtil::isExceptionThrown())
    {
        return NULL;
    }

    // java method ids will not change during the time this library is loaded, 
    // so they can be cached. 

    // get the method id for the CommitItem constructor
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

    // get the method if for the CommitMessage callback method
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

    // create a java CommitItem for each of the passed in commit items
    for(int i = 0; i < count; i++)
    {
        // get the commit item
        svn_client_commit_item_t *item
            = ((svn_client_commit_item_t **) commit_items->elts)[i];

        // convert the commit item members to the match java members
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

        // create the java object
        jobject jitem = env->NewObject(clazz, midConstructor, jpath,
            jnodeKind, jstateFlags, jurl, jcopyUrl, jcopyRevision);
        if(JNIUtil::isJavaExceptionThrown())
        {
            return NULL;
        }

        // release the tempory java objects
        env->DeleteLocalRef(jpath);
        if(JNIUtil::isJavaExceptionThrown())
        {
            return NULL;
        }


        env->DeleteLocalRef(jurl);
        if(JNIUtil::isJavaExceptionThrown())
        {
            return NULL;
        }

        env->DeleteLocalRef(jcopyUrl);
        if(JNIUtil::isJavaExceptionThrown())
        {
            return NULL;
        }

        // store the java object into the array
        env->SetObjectArrayElement(jitems, i, jitem);
        if(JNIUtil::isJavaExceptionThrown())
        {
            return NULL;
        }
    }
    env->DeleteLocalRef(clazz);
    if(JNIUtil::isJavaExceptionThrown())
    {
        return NULL;
    }

    // call the java callback method
    jstring jmessage = (jstring)env->CallObjectMethod(m_jcommitMessage,
                                            midCallback, jitems);
    if(JNIUtil::isJavaExceptionThrown())
    {
        return NULL;
    }

    // release the java object array
    env->DeleteLocalRef(jitems);
    if(JNIUtil::isJavaExceptionThrown())
    {
        return NULL;
    }
    return jmessage;
}
