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
 * @file org_tigris_subversion_javahl_SVNClient.cpp
 * @brief Implementation of the native methods in the java class SVNClient
 */
#include "../include/org_tigris_subversion_javahl_SVNClient.h"
#include "../include/org_tigris_subversion_javahl_SVNClientLogLevel.h"
#include "JNIUtil.h"
#include "JNIStackElement.h"
#include "JNIStringHolder.h"
#include "JNIByteArray.h"
#include "SVNClient.h"
#include "Revision.h"
#include "Notify.h"
#include "Notify2.h"
#include "CommitMessage.h"
#include "Prompter.h"
#include "Targets.h"
#include "BlameCallback.h"
#include "svn_version.h"
#include "svn_private_config.h"
#include "version.h"
#include "Outputer.h"
#include <iostream>
/*
 * Class:     org_tigris_subversion_javahl_SVNClient
 * Method:    ctNative
 * Signature: ()J
 */
JNIEXPORT jlong JNICALL Java_org_tigris_subversion_javahl_SVNClient_ctNative
  (JNIEnv* env, jobject jthis)
{
    JNIEntry(SVNClient, ctNative);
    SVNClient *obj = new SVNClient;
    return obj->getCppAddr();
}

/*
 * Class:     org_tigris_subversion_javahl_SVNClient
 * Method:    dispose
 * Signature: ()V
 */
JNIEXPORT void JNICALL Java_org_tigris_subversion_javahl_SVNClient_dispose
  (JNIEnv* env, jobject jthis)
{
    JNIEntry(SVNClient, dispose);
    SVNClient *cl = SVNClient::getCppObject(jthis);
    if(cl == NULL)
    {
        JNIUtil::throwError(_("bad c++ this"));
        return;
    }
    cl->dispose(jthis);
}

/*
 * Class:     org_tigris_subversion_javahl_SVNClient
 * Method:    finalize
 * Signature: ()V
 */
JNIEXPORT void JNICALL Java_org_tigris_subversion_javahl_SVNClient_finalize
  (JNIEnv* env, jobject jthis)
{
    JNIEntry(SVNClient, finalize);
    SVNClient *cl = SVNClient::getCppObject(jthis);
    if(cl != NULL)
    {
        cl->finalize();
    }
}

/*
 * Class:     org_tigris_subversion_javahl_SVNClient
 * Method:    getAdminDirectoryName
 * Signature: ()Ljava/lang/String;
 */
JNIEXPORT jstring JNICALL Java_org_tigris_subversion_javahl_SVNClient_getAdminDirectoryName
  (JNIEnv* env, jobject jthis)
{
    JNIEntry(Client, getAdminDirectoryName);
    SVNClient *cl = SVNClient::getCppObject(jthis);
    if (cl == NULL)
    {
        JNIUtil::throwError(_("bad c++ this"));
        return NULL;
    }
    return cl->getAdminDirectoryName();
}

/*
 * Class:     org_tigris_subversion_javahl_SVNClient
 * Method:    isAdminDirectory
 * Signature: (Ljava/lang/String;)Z
 */
JNIEXPORT jboolean JNICALL Java_org_tigris_subversion_javahl_SVNClient_isAdminDirectory
  (JNIEnv* env, jobject jthis, jstring jname)
{
    JNIEntry(Client, isAdminDirectory);
    SVNClient *cl = SVNClient::getCppObject(jthis);
    if (cl == NULL)
    {
        JNIUtil::throwError(_("bad c++ this"));
        return JNI_FALSE;
    }
    JNIStringHolder name(jname);
    if (JNIUtil::isExceptionThrown())
    {
        return JNI_FALSE;
    }
    return cl->isAdminDirectory(name);
}

/*
 * Class:     org_tigris_subversion_javahl_SVNClient
 * Method:    getLastPath
 * Signature: ()Ljava/lang/String;
 */
JNIEXPORT jstring JNICALL Java_org_tigris_subversion_javahl_SVNClient_getLastPath
  (JNIEnv* env, jobject jthis)
{
    JNIEntry(Client, getLastPath);
    SVNClient *cl = SVNClient::getCppObject(jthis);
    if(cl == NULL)
    {
        JNIUtil::throwError(_("bad c++ this"));
        return NULL;
    }
    const char *ret = cl->getLastPath();
    return JNIUtil::makeJString(ret);
}

/*
 * Class:     org_tigris_subversion_javahl_SVNClient
 * Method:    list
 * Signature: (Ljava/lang/String;Lorg/tigris/subversion/javahl/Revision;
 *             Lorg/tigris/subversion/javahl/Revision;Z)
 *            [Lorg/tigris/subversion/javahl/DirEntry;
 */
JNIEXPORT jobjectArray JNICALL Java_org_tigris_subversion_javahl_SVNClient_list
  (JNIEnv* env, jobject jthis, jstring jurl, jobject jrevision, 
   jobject jpegRevision, jboolean jrecurse)
{
    JNIEntry(SVNClient, list);
    SVNClient *cl = SVNClient::getCppObject(jthis);
    if(cl == NULL)
    {
        return NULL;
    }
    JNIStringHolder url(jurl);
    if(JNIUtil::isExceptionThrown())
    {
        return NULL;
    }
    Revision revision(jrevision);
    if(JNIUtil::isExceptionThrown())
    {
        return NULL;
    }
    Revision pegRevision(jpegRevision);
    if(JNIUtil::isExceptionThrown())
    {
        return NULL;
    }
    return cl->list(url, revision, pegRevision, jrecurse ? true:false);
}
/*
 * Class:     org_tigris_subversion_javahl_SVNClient
 * Method:    status
 * Signature: (Ljava/lang/String;ZZZZZ)[Lorg/tigris/subversion/javahl/Status;
 */
JNIEXPORT jobjectArray JNICALL Java_org_tigris_subversion_javahl_SVNClient_status
  (JNIEnv* env, jobject jthis, jstring jpath, jboolean jrecurse, 
   jboolean jonServer, jboolean jgetAll, jboolean jnoIgnore, 
   jboolean jignoreExternals)
{
    JNIEntry(SVNClient, status);
    SVNClient *cl = SVNClient::getCppObject(jthis);
    if(cl == NULL)
    {
        return NULL;
    }
    JNIStringHolder path(jpath);
    if(JNIUtil::isExceptionThrown())
    {
        return NULL;
    }
    return cl->status(path, jrecurse ? true: false, jonServer ? true:false, 
                      jgetAll ? true:false, jnoIgnore ? true:false,
                      jignoreExternals ? true:false);
}

/*
 * Class:     org_tigris_subversion_javahl_SVNClient
 * Method:    singleStatus
 * Signature: (Ljava/lang/String;ZZ)Lorg/tigris/subversion/javahl/Status;
 */
JNIEXPORT jobject JNICALL Java_org_tigris_subversion_javahl_SVNClient_singleStatus
  (JNIEnv* env, jobject jthis, jstring jpath, jboolean jonServer)
{
    JNIEntry(SVNClient, singleStatus);
    SVNClient *cl = SVNClient::getCppObject(jthis);
    if(cl == NULL)
    {
        return NULL;
    }
    JNIStringHolder path(jpath);
    if(JNIUtil::isExceptionThrown())
    {
        return NULL;
    }
    return cl->singleStatus(path, jonServer ? true:false);
}

/*
 * Class:     org_tigris_subversion_javahl_SVNClient
 * Method:    username
 * Signature: (Ljava/lang/String;)V
 */
JNIEXPORT void JNICALL Java_org_tigris_subversion_javahl_SVNClient_username
  (JNIEnv* env, jobject jthis, jstring jusername)
{
    JNIEntry(SVNClient, username);
    SVNClient *cl = SVNClient::getCppObject(jthis);
    if(cl == NULL)
    {
        JNIUtil::throwError(_("bad c++ this"));
        return;
    }
    if (jusername == NULL)
    {
        JNIUtil::raiseThrowable("java/lang/IllegalArgumentException",
                       _("Provide a username (null is not supported)"));
        return;
    }
    JNIStringHolder username(jusername);
    if(JNIUtil::isExceptionThrown())
    {
        return;
    }
    cl->username(username);
}

/*
 * Class:     org_tigris_subversion_javahl_SVNClient
 * Method:    password
 * Signature: (Ljava/lang/String;)V
 */
JNIEXPORT void JNICALL Java_org_tigris_subversion_javahl_SVNClient_password
  (JNIEnv* env, jobject jthis, jstring jpassword)
{
    JNIEntry(SVNClient, password);
    SVNClient *cl = SVNClient::getCppObject(jthis);
    if(cl == NULL)
    {
        JNIUtil::throwError(_("bad c++ this"));
        return;
    }
    if (jpassword == NULL)
    {
        JNIUtil::raiseThrowable("java/lang/IllegalArgumentException",
		       _("Provide a password (null is not supported)"));
        return;
    }
    JNIStringHolder password(jpassword);
    if(JNIUtil::isExceptionThrown())
    {
        return;
    }
    cl->password(password);
}

/*
 * Class:     org_tigris_subversion_javahl_SVNClient
 * Method:    setPrompt
 * Signature: (Lorg/tigris/subversion/javahl/PromptUserPassword;)V
 */
JNIEXPORT void JNICALL Java_org_tigris_subversion_javahl_SVNClient_setPrompt
  (JNIEnv* env, jobject jthis, jobject jprompter)
{
    JNIEntry(SVNClient, setPrompt);
    SVNClient *cl = SVNClient::getCppObject(jthis);
    if(cl == NULL)
    {
        JNIUtil::throwError(_("bad c++ this"));
        return;
    }
    Prompter *prompter = Prompter::makeCPrompter(jprompter);
    if(JNIUtil::isExceptionThrown())
    {
        return;
    }
    cl->setPrompt(prompter);
}

/*
 * Class:     org_tigris_subversion_javahl_SVNClient
 * Method:    logMessages
 * Signature: (Ljava/lang/String;Lorg/tigris/subversion/javahl/Revision;
 *             Lorg/tigris/subversion/javahl/Revision;ZZJ)
 *             [Lorg/tigris/subversion/javahl/LogMessage;
 */
JNIEXPORT jobjectArray JNICALL Java_org_tigris_subversion_javahl_SVNClient_logMessages
  (JNIEnv* env, jobject jthis, jstring jpath, jobject jrevisionStart,
   jobject jrevisionEnd, jboolean jstopOnCopy, jboolean jdisoverPaths,
   jlong jlimit)
{
    JNIEntry(SVNClient, logMessages);
    SVNClient *cl = SVNClient::getCppObject(jthis);
    if(cl == NULL)
    {
        JNIUtil::throwError(_("bad c++ this"));
        return NULL;
    }
    Revision revisionStart(jrevisionStart, false, true);
    if(JNIUtil::isExceptionThrown())
    {
        return NULL;
    }
    Revision revisionEnd(jrevisionEnd, true);
    if(JNIUtil::isExceptionThrown())
    {
        return NULL;
    }
    JNIStringHolder path(jpath);
    if(JNIUtil::isExceptionThrown())
    {
        return NULL;
    }
    return cl->logMessages(path, revisionStart, revisionEnd,
        jstopOnCopy ? true: false, jdisoverPaths ? true : false, jlimit);
}

/*
 * Class:     org_tigris_subversion_javahl_SVNClient
 * Method:    checkout
 * Signature: (Ljava/lang/String;Ljava/lang/String;
 *             Lorg/tigris/subversion/javahl/Revision;
 *             Lorg/tigris/subversion/javahl/Revision;ZZ)J
 */
JNIEXPORT jlong JNICALL Java_org_tigris_subversion_javahl_SVNClient_checkout
  (JNIEnv* env, jobject jthis, jstring jmoduleName, jstring jdestPath, 
   jobject jrevision, jobject jpegRevision, jboolean jrecurse, 
   jboolean jignoreExternals)
{
    JNIEntry(SVNClient, checkout);
    SVNClient *cl = SVNClient::getCppObject(jthis);
    if(cl == NULL)
    {
        JNIUtil::throwError(_("bad c++ this"));
        return -1;
    }
    Revision revision(jrevision, true);
    if(JNIUtil::isExceptionThrown())
    {
        return -1;
    }
    Revision pegRevision(jpegRevision, true);
    if(JNIUtil::isExceptionThrown())
    {
        return -1;
    }
    JNIStringHolder moduleName(jmoduleName);
    if(JNIUtil::isExceptionThrown())
    {
        return -1;
    }
    JNIStringHolder destPath(jdestPath);
    if(JNIUtil::isExceptionThrown())
    {
        return -1;
    }
    return cl->checkout(moduleName, destPath, revision, pegRevision, 
        jrecurse ? true : false, jignoreExternals ? true : false);
}

/*
 * Class:     org_tigris_subversion_javahl_SVNClient
 * Method:    notification
 * Signature: (Lorg/tigris/subversion/javahl/Notify;)V
 */
JNIEXPORT void JNICALL Java_org_tigris_subversion_javahl_SVNClient_notification
  (JNIEnv* env, jobject jthis, jobject jnotify)
{
    JNIEntry(SVNClient, notification);
    SVNClient *cl = SVNClient::getCppObject(jthis);
    if(cl == NULL)
    {
        JNIUtil::throwError(_("bad c++ this"));
        return;
    }
    Notify *notify = Notify::makeCNotify(jnotify);
    if(JNIUtil::isExceptionThrown())
    {
        return;
    }
    cl->notification(notify);
}
/*
 * Class:     org_tigris_subversion_javahl_SVNClient
 * Method:    notification2
 * Signature: (Lorg/tigris/subversion/javahl/Notify2;)V
 */
JNIEXPORT void JNICALL Java_org_tigris_subversion_javahl_SVNClient_notification2
  (JNIEnv* env, jobject jthis, jobject jnotify2)
{
    JNIEntry(SVNClient, notification2);
    SVNClient *cl = SVNClient::getCppObject(jthis);
    if(cl == NULL)
    {
        JNIUtil::throwError(_("bad c++ this"));
        return;
    }
    Notify2 *notify2 = Notify2::makeCNotify(jnotify2);
    if(JNIUtil::isExceptionThrown())
    {
        return;
    }
    cl->notification2(notify2);
}
/*
 * Class:     org_tigris_subversion_javahl_SVNClient
 * Method:    commitMessageHandler
 * Signature: (Lorg/tigris/subversion/javahl/CommitMessage;)V
 */
JNIEXPORT void JNICALL Java_org_tigris_subversion_javahl_SVNClient_commitMessageHandler
  (JNIEnv *env, jobject jthis, jobject jcommitMessage)
{
    JNIEntry(SVNClient, commitMessageHandler);
    SVNClient *cl = SVNClient::getCppObject(jthis);
    if(cl == NULL)
    {
        JNIUtil::throwError("bad c++ this");
        return;
    }
    CommitMessage *commitMessage = 
        CommitMessage::makeCCommitMessage(jcommitMessage);
    if(JNIUtil::isExceptionThrown())
    {
        return;
    }
    cl->commitMessageHandler(commitMessage);
}
/*
 * Class:     org_tigris_subversion_javahl_SVNClient
 * Method:    remove
 * Signature: (Ljava/lang/String;Ljava/lang/String;Z)V
 */
JNIEXPORT void JNICALL Java_org_tigris_subversion_javahl_SVNClient_remove
  (JNIEnv* env, jobject jthis, jobjectArray jtargets, jstring jmessage, 
   jboolean jforce)
{
    JNIEntry(SVNClient, remove);
    SVNClient *cl = SVNClient::getCppObject(jthis);
    if(cl == NULL)
    {
        JNIUtil::throwError(_("bad c++ this"));
        return;
    }
    Targets targets(jtargets);
    JNIStringHolder message(jmessage);
    if(JNIUtil::isExceptionThrown())
    {
        return;
    }
    cl->remove(targets, message, jforce ? true : false);
}

/*
 * Class:     org_tigris_subversion_javahl_SVNClient
 * Method:    revert
 * Signature: (Ljava/lang/String;Z)V
 */
JNIEXPORT void JNICALL Java_org_tigris_subversion_javahl_SVNClient_revert
  (JNIEnv* env, jobject jthis, jstring jpath, jboolean jrecurse)
{
    JNIEntry(SVNClient, revert);
    SVNClient *cl = SVNClient::getCppObject(jthis);
    if(cl == NULL)
    {
        JNIUtil::throwError(_("bad c++ this"));
        return;
    }
    JNIStringHolder path(jpath);
    if(JNIUtil::isExceptionThrown())
    {
        return;
    }
    cl->revert(path, jrecurse ? true : false);
}

/*
 * Class:     org_tigris_subversion_javahl_SVNClient
 * Method:    add
 * Signature: (Ljava/lang/String;ZZ)V
 */
JNIEXPORT void JNICALL Java_org_tigris_subversion_javahl_SVNClient_add
  (JNIEnv* env, jobject jthis, jstring jpath, jboolean jrecurse, 
   jboolean jforce)
{
    JNIEntry(SVNClient, add);
    SVNClient *cl = SVNClient::getCppObject(jthis);
    if(cl == NULL)
    {
        JNIUtil::throwError(_("bad c++ this"));
        return;
    }
    JNIStringHolder path(jpath);
    if(JNIUtil::isExceptionThrown())
    {
        return;
    }
    cl->add(path, jrecurse ? true : false, jforce ? true : false);
}

/*
 * Class:     org_tigris_subversion_javahl_SVNClient
 * Method:    update
 * Signature: ([Ljava/lang/String;Lorg/tigris/subversion/javahl/Revision;ZZ)[J
 */
JNIEXPORT jlongArray JNICALL Java_org_tigris_subversion_javahl_SVNClient_update
  (JNIEnv* env, jobject jthis, jobjectArray jpath, jobject jrevision, 
   jboolean jrecurse, jboolean jignoreExternals)
{
    JNIEntry(SVNClient, update);
    SVNClient *cl = SVNClient::getCppObject(jthis);
    if(cl == NULL)
    {
        JNIUtil::throwError(_("bad c++ this"));
        return NULL;
    }
    Revision revision(jrevision);
    if(JNIUtil::isExceptionThrown())
    {
        return NULL;
    }
    Targets targets(jpath);
    if(JNIUtil::isExceptionThrown())
    {
        return NULL;
    }
    return cl->update(targets, revision, jrecurse ? true : false, 
        jignoreExternals ? true : false);
}

/*
 * Class:     org_tigris_subversion_javahl_SVNClient
 * Method:    commit
 * Signature: ([Ljava/lang/String;Ljava/lang/String;ZZ)J
 */
JNIEXPORT jlong JNICALL Java_org_tigris_subversion_javahl_SVNClient_commit
  (JNIEnv* env, jobject jthis, jobjectArray jtargets, jstring jmessage, 
   jboolean jrecurse, jboolean jnoUnlock)
{
    JNIEntry(SVNClient, commit);
    SVNClient *cl = SVNClient::getCppObject(jthis);
    if(cl == NULL)
    {
        JNIUtil::throwError(_("bad c++ this"));
        return -1;
    }
    Targets targets(jtargets);
    JNIStringHolder message(jmessage);
    if(JNIUtil::isExceptionThrown())
    {
        return -1;
    }
    return cl->commit(targets, message, jrecurse ? true : false, 
        jnoUnlock ? true : false);
}

/*
 * Class:     org_tigris_subversion_javahl_SVNClient
 * Method:    copy
 * Signature: (Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;
 *             Lorg/tigris/subversion/javahl/Revision;)V
 */
JNIEXPORT void JNICALL Java_org_tigris_subversion_javahl_SVNClient_copy
  (JNIEnv* env, jobject jthis, jstring jsrcPath, jstring jdestPath, 
   jstring jmessage, jobject jrevision)
{
    JNIEntry(SVNClient, copy);
    SVNClient *cl = SVNClient::getCppObject(jthis);
    if(cl == NULL)
    {
        JNIUtil::throwError(_("bad c++ this"));
        return;
    }
    JNIStringHolder srcPath(jsrcPath);
    if(JNIUtil::isExceptionThrown())
    {
        return;
    }
    JNIStringHolder destPath(jdestPath);
    if(JNIUtil::isExceptionThrown())
    {
        return;
    }
    JNIStringHolder message(jmessage);
    if(JNIUtil::isExceptionThrown())
    {
        return;
    }
    Revision revision(jrevision);
    if(JNIUtil::isExceptionThrown())
    {
        return;
    }
    cl->copy(srcPath, destPath, message, revision);
}

/*
 * Class:     org_tigris_subversion_javahl_SVNClient
 * Method:    move
 * Signature: (Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Z)V
 */
JNIEXPORT void JNICALL Java_org_tigris_subversion_javahl_SVNClient_move
  (JNIEnv *env, jobject jthis, jstring jsrcPath, jstring jdestPath, 
   jstring jmessage, jboolean jforce)
{
    JNIEntry(SVNClient, move);
    SVNClient *cl = SVNClient::getCppObject(jthis);
    if(cl == NULL)
    {
        JNIUtil::throwError(_("bad c++ this"));
        return;
    }
    JNIStringHolder srcPath(jsrcPath);
    if(JNIUtil::isExceptionThrown())
    {
        return;
    }
    JNIStringHolder destPath(jdestPath);
    if(JNIUtil::isExceptionThrown())
    {
        return;
    }
    JNIStringHolder message(jmessage);
    if(JNIUtil::isExceptionThrown())
    {
        return;
    }
    cl->move(srcPath, destPath, message, jforce ? true:false);
}

/*
 * Class:     org_tigris_subversion_javahl_SVNClient
 * Method:    mkdir
 * Signature: (Ljava/lang/String;Ljava/lang/String;)V
 */
JNIEXPORT void JNICALL Java_org_tigris_subversion_javahl_SVNClient_mkdir
  (JNIEnv* env, jobject jthis, jobjectArray jtargets, jstring jmessage)
{
    JNIEntry(SVNClient, mkdir);
    SVNClient *cl = SVNClient::getCppObject(jthis);
    if(cl == NULL)
    {
        JNIUtil::throwError(_("bad c++ this"));
        return;
    }
    Targets targets(jtargets);
    JNIStringHolder message(jmessage);
    if(JNIUtil::isExceptionThrown())
    {
        return;
    }
    cl->mkdir(targets, message);
}

/*
 * Class:     org_tigris_subversion_javahl_SVNClient
 * Method:    cleanup
 * Signature: (Ljava/lang/String;)V
 */
JNIEXPORT void JNICALL Java_org_tigris_subversion_javahl_SVNClient_cleanup
  (JNIEnv* env, jobject jthis, jstring jpath)
{
    JNIEntry(SVNClient, cleanup);
    SVNClient *cl = SVNClient::getCppObject(jthis);
    if(cl == NULL)
    {
        JNIUtil::throwError(_("bad c++ this"));
        return;
    }
    JNIStringHolder path(jpath);
    if(JNIUtil::isExceptionThrown())
    {
        return;
    }
    cl->cleanup(path);
}

/*
 * Class:     org_tigris_subversion_javahl_SVNClient
 * Method:    resolved
 * Signature: (Ljava/lang/String;Z)V
 */
JNIEXPORT void JNICALL Java_org_tigris_subversion_javahl_SVNClient_resolved
  (JNIEnv* env, jobject jthis, jstring jpath, jboolean jrecurse)
{
    JNIEntry(SVNClient, resolved);
    SVNClient *cl = SVNClient::getCppObject(jthis);
    if(cl == NULL)
    {
        JNIUtil::throwError(_("bad c++ this"));
        return;
    }
    JNIStringHolder path(jpath);
    if(JNIUtil::isExceptionThrown())
    {
        return;
    }
    cl->resolved(path, jrecurse ? true: false);
}

/*
 * Class:     org_tigris_subversion_javahl_SVNClient
 * Method:    doExport
 * Signature: (Ljava/lang/String;Ljava/lang/String;
 *             Lorg/tigris/subversion/javahl/Revision;
 *             Lorg/tigris/subversion/javahl/Revision;ZZZLjava/lang/String;)J
 */
JNIEXPORT jlong JNICALL Java_org_tigris_subversion_javahl_SVNClient_doExport
  (JNIEnv* env, jobject jthis, jstring jsrcPath, jstring jdestPath, 
   jobject jrevision, jobject jpegRevision, jboolean jforce, 
   jboolean jignoreExternals, jboolean jrecurse, jstring jnativeEOL)
{
    JNIEntry(SVNClient, doExport);
    SVNClient *cl = SVNClient::getCppObject(jthis);
    if(cl == NULL)
    {
        JNIUtil::throwError(_("bad c++ this"));
        return -1;
    }
    Revision revision(jrevision);
    if(JNIUtil::isExceptionThrown())
    {
        return -1;
    }
    Revision pegRevision(jpegRevision);
    if(JNIUtil::isExceptionThrown())
    {
        return -1;
    }
    JNIStringHolder srcPath(jsrcPath);
    if(JNIUtil::isExceptionThrown())
    {
        return -1;
    }
    JNIStringHolder destPath(jdestPath);
    if(JNIUtil::isExceptionThrown())
    {
        return -1;
    }
    JNIStringHolder nativeEOL(jnativeEOL);
    if(JNIUtil::isExceptionThrown())
    {
        return -1;
    }
    return cl->doExport(srcPath, destPath, revision, pegRevision, 
        jforce ? true : false, jignoreExternals ? true : false, 
        jrecurse ? true: false, nativeEOL);
}

/*
 * Class:     org_tigris_subversion_javahl_SVNClient
 * Method:    doSwitch
 * Signature: (Ljava/lang/String;Ljava/lang/String;
 *             Lorg/tigris/subversion/javahl/Revision;Z)J
 */
JNIEXPORT jlong JNICALL Java_org_tigris_subversion_javahl_SVNClient_doSwitch
  (JNIEnv* env, jobject jthis, jstring jpath, jstring jurl, jobject jrevision, 
   jboolean jrecurse)
{
    JNIEntry(SVNClient, doSwitch);
    SVNClient *cl = SVNClient::getCppObject(jthis);
    if(cl == NULL)
    {
        JNIUtil::throwError(_("bad c++ this"));
        return -1;
    }
    Revision revision(jrevision);
    if(JNIUtil::isExceptionThrown())
    {
        return -1;
    }
    JNIStringHolder path(jpath);
    if(JNIUtil::isExceptionThrown())
    {
        return -1;
    }
    JNIStringHolder url(jurl);
    if(JNIUtil::isExceptionThrown())
    {
        return -1;
    }
    return cl->doSwitch(path, url, revision, jrecurse ? true: false);
}

/*
 * Class:     org_tigris_subversion_javahl_SVNClient
 * Method:    doImport
 * Signature: (Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;
 *             Ljava/lang/String;Z)V
 */
JNIEXPORT void JNICALL Java_org_tigris_subversion_javahl_SVNClient_doImport
  (JNIEnv* env, jobject jthis, jstring jpath, jstring jurl, jstring jmessage, 
   jboolean jrecurse)
{
    JNIEntry(SVNClient, doImport);
    SVNClient *cl = SVNClient::getCppObject(jthis);
    if(cl == NULL)
    {
        JNIUtil::throwError(_("bad c++ this"));
        return;
    }
    JNIStringHolder path(jpath);
    if(JNIUtil::isExceptionThrown())
    {
        return;
    }
    JNIStringHolder url(jurl);
    if(JNIUtil::isExceptionThrown())
    {
        return;
    }
    JNIStringHolder message(jmessage);
    if(JNIUtil::isExceptionThrown())
    {
        return;
    }
    cl->doImport(path, url, message, jrecurse ? true : false);
}

/*
 * Class:     org_tigris_subversion_javahl_SVNClient
 * Method:    merge
 * Signature: (Ljava/lang/String;Lorg/tigris/subversion/javahl/Revision;
 *             Ljava/lang/String;Lorg/tigris/subversion/javahl/Revision;
 *             Ljava/lang/String;ZZZZ)V
 */
JNIEXPORT void JNICALL Java_org_tigris_subversion_javahl_SVNClient_merge__Ljava_lang_String_2Lorg_tigris_subversion_javahl_Revision_2Ljava_lang_String_2Lorg_tigris_subversion_javahl_Revision_2Ljava_lang_String_2ZZZZ
  (JNIEnv* env, jobject jthis, jstring jpath1, jobject jrevision1, 
   jstring jpath2, jobject jrevision2, jstring jlocalPath, jboolean jforce, 
   jboolean jrecurse, jboolean jignoreAncestry, jboolean jdryRun)
{
    JNIEntry(SVNClient, merge);
    SVNClient *cl = SVNClient::getCppObject(jthis);
    if(cl == NULL)
    {
        JNIUtil::throwError(_("bad c++ this"));
        return;
    }
    Revision revision1(jrevision1);
    if(JNIUtil::isExceptionThrown())
    {
        return;
    }
    JNIStringHolder path1(jpath1);
    if(JNIUtil::isExceptionThrown())
    {
        return;
    }
    Revision revision2(jrevision2);
    if(JNIUtil::isExceptionThrown())
    {
        return;
    }
    JNIStringHolder path2(jpath2);
    if(JNIUtil::isExceptionThrown())
    {
        return;
    }
    JNIStringHolder localPath(jlocalPath);
    if(JNIUtil::isExceptionThrown())
    {
        return;
    }
    cl->merge(path1, revision1, path2, revision2, localPath, 
        jforce ? true:false, jrecurse ? true:false, 
        jignoreAncestry ? true:false, jdryRun ? true:false);
}
/*
 * Class:     org_tigris_subversion_javahl_SVNClient
 * Method:    merge
 * Signature: (Ljava/lang/String;Lorg/tigris/subversion/javahl/Revision;
 *             Lorg/tigris/subversion/javahl/Revision;
 *             Lorg/tigris/subversion/javahl/Revision;Ljava/lang/String;ZZZZ)V
 */
JNIEXPORT void JNICALL Java_org_tigris_subversion_javahl_SVNClient_merge__Ljava_lang_String_2Lorg_tigris_subversion_javahl_Revision_2Lorg_tigris_subversion_javahl_Revision_2Lorg_tigris_subversion_javahl_Revision_2Ljava_lang_String_2ZZZZ
  (JNIEnv* env, jobject jthis, jstring jpath, jobject jpegRevision, 
   jobject jrevision1, jobject jrevision2, jstring jlocalPath, jboolean jforce,
   jboolean jrecurse, jboolean jignoreAncestry, jboolean jdryRun)
{
    JNIEntry(SVNClient, merge);
    SVNClient *cl = SVNClient::getCppObject(jthis);
    if(cl == NULL)
    {
        JNIUtil::throwError(_("bad c++ this"));
        return;
    }
    Revision revision1(jrevision1);
    if(JNIUtil::isExceptionThrown())
    {
        return;
    }
    JNIStringHolder path(jpath);
    if(JNIUtil::isExceptionThrown())
    {
        return;
    }
    Revision revision2(jrevision2);
    if(JNIUtil::isExceptionThrown())
    {
        return;
    }
    Revision pegRevision(jpegRevision);
    if(JNIUtil::isExceptionThrown())
    {
        return;
    }
    JNIStringHolder localPath(jlocalPath);
    if(JNIUtil::isExceptionThrown())
    {
        return;
    }
    cl->merge(path, pegRevision, revision1, revision2, localPath, 
        jforce ? true:false, jrecurse ? true:false, 
        jignoreAncestry ? true:false, jdryRun ? true:false);
}

/*
 * Class:     org_tigris_subversion_javahl_SVNClient
 * Method:    properties
 * Signature: (Ljava/lang/String;Lorg/tigris/subversion/javahl/Revision;
 *             Lorg/tigris/subversion/javahl/Revision;)
 *            [Lorg/tigris/subversion/javahl/PropertyData;
 */
JNIEXPORT jobjectArray JNICALL Java_org_tigris_subversion_javahl_SVNClient_properties
  (JNIEnv* env, jobject jthis, jstring jpath, jobject jrevision, 
   jobject jpegRevision)
{
    JNIEntry(SVNClient, properties);
    SVNClient *cl = SVNClient::getCppObject(jthis);
    if(cl == NULL)
    {
        JNIUtil::throwError(_("bad c++ this"));
        return NULL;
    }
    JNIStringHolder path(jpath);
    if(JNIUtil::isExceptionThrown())
    {
        return NULL;
    }
    Revision revision(jrevision);
    if(JNIUtil::isExceptionThrown())
    {
        return NULL;
    }
    Revision pegRevision(jpegRevision);
    if(JNIUtil::isExceptionThrown())
    {
        return NULL;
    }
    return cl->properties(jthis, path, revision, pegRevision);
}

/*
 * Class:     org_tigris_subversion_javahl_SVNClient
 * Method:    propertySet
 * Signature: (Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;ZZ)V
 */
JNIEXPORT void JNICALL Java_org_tigris_subversion_javahl_SVNClient_propertySet__Ljava_lang_String_2Ljava_lang_String_2Ljava_lang_String_2ZZ
  (JNIEnv* env, jobject jthis, jstring jpath, jstring jname, jstring jvalue, 
   jboolean jrecurse, jboolean jforce)
{
    JNIEntry(SVNClient, propertySet);
    SVNClient *cl = SVNClient::getCppObject(jthis);
    if(cl == NULL)
    {
        JNIUtil::throwError(_("bad c++ this"));
        return;
    }
    JNIStringHolder path(jpath);
    if(JNIUtil::isExceptionThrown())
    {
        return;
    }
    JNIStringHolder name(jname);
    if(JNIUtil::isExceptionThrown())
    {
        return;
    }
    JNIStringHolder value(jvalue);
    if(JNIUtil::isExceptionThrown())
    {
        return;
    }
    cl->propertySet(path, name, value, jrecurse ? true:false, 
        jforce ? true:false);
}

/*
 * Class:     org_tigris_subversion_javahl_SVNClient
 * Method:    propertySet
 * Signature: (Ljava/lang/String;Ljava/lang/String;[BZZ)V
 */
JNIEXPORT void JNICALL Java_org_tigris_subversion_javahl_SVNClient_propertySet__Ljava_lang_String_2Ljava_lang_String_2_3BZZ
  (JNIEnv* env, jobject jthis, jstring jpath, jstring jname, 
   jbyteArray jvalue, jboolean jrecurse, jboolean jforce )
{
    JNIEntry(SVNClient, propertySet);
    SVNClient *cl = SVNClient::getCppObject(jthis);
    if(cl == NULL)
    {
        JNIUtil::throwError(_("bad c++ this"));
        return;
    }
    JNIStringHolder path(jpath);
    if(JNIUtil::isExceptionThrown())
    {
        return;
    }
    JNIStringHolder name(jname);
    if(JNIUtil::isExceptionThrown())
    {
        return;
    }
    JNIByteArray value(jvalue);
    if(JNIUtil::isExceptionThrown())
    {
        return;
    }
    cl->propertySet(path, name, value, jrecurse ? true:false,
        jforce ? true:false);
}

/*
 * Class:     org_tigris_subversion_javahl_SVNClient
 * Method:    propertyRemove
 * Signature: (Ljava/lang/String;Ljava/lang/String;Z)V
 */
JNIEXPORT void JNICALL Java_org_tigris_subversion_javahl_SVNClient_propertyRemove
  (JNIEnv* env, jobject jthis, jstring jpath, jstring jname, jboolean jrecurse)
{
    JNIEntry(SVNClient, propertyRemove);
    SVNClient *cl = SVNClient::getCppObject(jthis);
    if(cl == NULL)
    {
        JNIUtil::throwError(_("bad c++ this"));
        return;
    }
    JNIStringHolder path(jpath);
    if(JNIUtil::isExceptionThrown())
    {
        return;
    }
    JNIStringHolder name(jname);
    if(JNIUtil::isExceptionThrown())
    {
        return;
    }
    cl->propertyRemove(path, name, jrecurse ? true:false);
}

/*
 * Class:     org_tigris_subversion_javahl_SVNClient
 * Method:    propertyCreate
 * Signature: (Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;ZZ)V
 */
JNIEXPORT void JNICALL Java_org_tigris_subversion_javahl_SVNClient_propertyCreate__Ljava_lang_String_2Ljava_lang_String_2Ljava_lang_String_2ZZ
  (JNIEnv* env, jobject jthis, jstring jpath, jstring jname, jstring jvalue, 
   jboolean jrecurse, jboolean jforce)
{
    JNIEntry(SVNClient, propertyCreate);
    SVNClient *cl = SVNClient::getCppObject(jthis);
    if(cl == NULL)
    {
        JNIUtil::throwError(_("bad c++ this"));
        return;
    }
    JNIStringHolder path(jpath);
    if(JNIUtil::isExceptionThrown())
    {
        return;
    }
    JNIStringHolder name(jname);
    if(JNIUtil::isExceptionThrown())
    {
        return;
    }
    JNIStringHolder value(jvalue);
    if(JNIUtil::isExceptionThrown())
    {
        return;
    }
    cl->propertyCreate(path, name, value, jrecurse ? true:false, 
        jforce ? true:false);
}


/*
 * Class:     org_tigris_subversion_javahl_SVNClient
 * Method:    propertyCreate
 * Signature: (Ljava/lang/String;Ljava/lang/String;[BZZ)V
 */
JNIEXPORT void JNICALL Java_org_tigris_subversion_javahl_SVNClient_propertyCreate__Ljava_lang_String_2Ljava_lang_String_2_3BZZ
  (JNIEnv* env, jobject jthis, jstring jpath, jstring jname, jbyteArray jvalue, 
   jboolean jrecurse, jboolean jforce)
{
    JNIEntry(SVNClient, propertyCreate);
    SVNClient *cl = SVNClient::getCppObject(jthis);
    if(cl == NULL)
    {
        JNIUtil::throwError(_("bad c++ this"));
        return;
    }
    JNIStringHolder path(jpath);
    if(JNIUtil::isExceptionThrown())
    {
        return;
    }
    JNIStringHolder name(jname);
    if(JNIUtil::isExceptionThrown())
    {
        return;
    }
    JNIByteArray value(jvalue);
    if(JNIUtil::isExceptionThrown())
    {
        return;
    }
    cl->propertyCreate(path, name, value, jrecurse ? true:false, 
        jforce ? true:false);
}
/*
 * Class:     org_tigris_subversion_javahl_SVNClient
 * Method:    revProperty
 * Signature: (Ljava/lang/String;Ljava/lang/String;
 *             Lorg/tigris/subversion/javahl/Revision;)
 *            Lorg/tigris/subversion/javahl/PropertyData;
 */
JNIEXPORT jobject JNICALL Java_org_tigris_subversion_javahl_SVNClient_revProperty
  (JNIEnv *env, jobject jthis, jstring jpath, jstring jname, jobject jrevision)
{
    JNIEntry(SVNClient, revProperty);
    SVNClient *cl = SVNClient::getCppObject(jthis);
    if(cl == NULL)
    {
        JNIUtil::throwError(_("bad c++ this"));
        return NULL;
    }
    JNIStringHolder path(jpath);
    if(JNIUtil::isExceptionThrown())
    {
        return NULL;
    }
    JNIStringHolder name(jname);
    if(JNIUtil::isExceptionThrown())
    {
        return NULL;
    }
    Revision revision(jrevision);
    if(JNIUtil::isExceptionThrown())
    {
        return NULL;
    }
    return cl->revProperty(jthis, path, name, revision);
}
/*
 * Class:     org_tigris_subversion_javahl_SVNClient
 * Method:    revProperties
 * Signature: (Ljava/lang/String;Lorg/tigris/subversion/javahl/Revision;)
 *            [Lorg/tigris/subversion/javahl/PropertyData;
 */
JNIEXPORT jobjectArray JNICALL Java_org_tigris_subversion_javahl_SVNClient_revProperties
  (JNIEnv *env, jobject jthis, jstring jpath, jobject jrevision)
{
    JNIEntry(SVNClient, revProperty);
    SVNClient *cl = SVNClient::getCppObject(jthis);
    if(cl == NULL)
    {
        JNIUtil::throwError(_("bad c++ this"));
        return NULL;
    }
    JNIStringHolder path(jpath);
    if(JNIUtil::isExceptionThrown())
    {
        return NULL;
    }
    Revision revision(jrevision);
    if(JNIUtil::isExceptionThrown())
    {
        return NULL;
    }
    return cl->revProperties(jthis, path, revision);
}
/*
 * Class:     org_tigris_subversion_javahl_SVNClient
 * Method:    setRevProperty
 * Signature: (Ljava/lang/String;Ljava/lang/String;
 *             Lorg/tigris/subversion/javahl/Revision;Ljava/lang/String;Z)V
 */
JNIEXPORT void JNICALL Java_org_tigris_subversion_javahl_SVNClient_setRevProperty
  (JNIEnv *env, jobject jthis, jstring jpath, jstring jname, jobject jrevision, 
   jstring jvalue, jboolean jforce)
{
    JNIEntry(SVNClient, setRevProperty);
    SVNClient *cl = SVNClient::getCppObject(jthis);
    if(cl == NULL)
    {
        JNIUtil::throwError(_("bad c++ this"));
        return;
    }
    JNIStringHolder path(jpath);
    if(JNIUtil::isExceptionThrown())
    {
        return;
    }
    JNIStringHolder name(jname);
    if(JNIUtil::isExceptionThrown())
    {
        return;
    }
    Revision revision(jrevision);
    if(JNIUtil::isExceptionThrown())
    {
        return;
    }
    JNIStringHolder value(jvalue);
    if(JNIUtil::isExceptionThrown())
    {
        return;
    }
    cl->setRevProperty(jthis, path, name, revision, value, 
        jforce ? true: false);
}
/*
 * Class:     org_tigris_subversion_javahl_SVNClient
 * Method:    propertyGet
 * Signature: (Ljava/lang/String;Ljava/lang/String;
 *             Lorg/tigris/subversion/javahl/Revision;
 *             Lorg/tigris/subversion/javahl/Revision;)
 *            Lorg/tigris/subversion/javahl/PropertyData;
 */
JNIEXPORT jobject JNICALL Java_org_tigris_subversion_javahl_SVNClient_propertyGet
  (JNIEnv *env, jobject jthis, jstring jpath, jstring jname, jobject jrevision,
   jobject jpegRevision)
{
    JNIEntry(SVNClient, propertyGet);
    SVNClient *cl = SVNClient::getCppObject(jthis);
    if(cl == NULL)
    {
        JNIUtil::throwError(_("bad c++ this"));
        return NULL;
    }
    JNIStringHolder path(jpath);
    if(JNIUtil::isExceptionThrown())
    {
        return NULL;
    }
    JNIStringHolder name(jname);
    if(JNIUtil::isExceptionThrown())
    {
        return NULL;
    }
    Revision revision(jrevision);
    if(JNIUtil::isExceptionThrown())
    {
        return NULL;
    }
    Revision pegRevision(jpegRevision);
    if(JNIUtil::isExceptionThrown())
    {
        return NULL;
    }
    return cl->propertyGet(jthis, path, name, revision, pegRevision);
}


/*
 * Class:     org_tigris_subversion_javahl_SVNClient
 * Method:    diff
 * Signature: (Ljava/lang/String;Lorg/tigris/subversion/javahl/Revision;
 *             Ljava/lang/String;Lorg/tigris/subversion/javahl/Revision;
 *             Ljava/lang/String;ZZZZ)V
 */
JNIEXPORT void JNICALL Java_org_tigris_subversion_javahl_SVNClient_diff__Ljava_lang_String_2Lorg_tigris_subversion_javahl_Revision_2Ljava_lang_String_2Lorg_tigris_subversion_javahl_Revision_2Ljava_lang_String_2ZZZZ
  (JNIEnv *env, jobject jthis, jstring jtarget1, jobject jrevision1, 
   jstring jtarget2, jobject jrevision2, jstring joutfileName,
   jboolean jrecurse, jboolean jignoreAncestry, jboolean jnoDiffDeleted, 
   jboolean jforce)
{
    JNIEntry(SVNClient, diff);
    SVNClient *cl = SVNClient::getCppObject(jthis);
    if(cl == NULL)
    {
        JNIUtil::throwError(_("bad c++ this"));
        return;
    }
    JNIStringHolder target1(jtarget1);
    if(JNIUtil::isExceptionThrown())
    {
        return;
    }
    Revision revision1(jrevision1);
    if(JNIUtil::isExceptionThrown())
    {
        return;
    }
    JNIStringHolder target2(jtarget2);
    if(JNIUtil::isExceptionThrown())
    {
        return;
    }
    Revision revision2(jrevision2);
    if(JNIUtil::isExceptionThrown())
    {
        return;
    }
    JNIStringHolder outfileName(joutfileName);
    if(JNIUtil::isExceptionThrown())
    {
        return;
    }
    cl->diff(target1, revision1, target2, revision2, outfileName,
        jrecurse ? true:false, jignoreAncestry ? true:false,
        jnoDiffDeleted ? true:false, jforce ? true:false);
}
/*
 * Class:     org_tigris_subversion_javahl_SVNClient
 * Method:    diff
 * Signature: (Ljava/lang/String;Lorg/tigris/subversion/javahl/Revision;
 *             Lorg/tigris/subversion/javahl/Revision;
 *             Lorg/tigris/subversion/javahl/Revision;Ljava/lang/String;ZZZZ)V
 */
JNIEXPORT void JNICALL Java_org_tigris_subversion_javahl_SVNClient_diff__Ljava_lang_String_2Lorg_tigris_subversion_javahl_Revision_2Lorg_tigris_subversion_javahl_Revision_2Lorg_tigris_subversion_javahl_Revision_2Ljava_lang_String_2ZZZZ
  (JNIEnv *env, jobject jthis, jstring jtarget, jobject jpegRevision, 
   jobject jstartRevision, jobject jendRevision, jstring joutfileName, 
   jboolean jrecurse, jboolean jignoreAncestry, jboolean jnoDiffDeleted, 
   jboolean jforce)
{
    JNIEntry(SVNClient, diff);
    SVNClient *cl = SVNClient::getCppObject(jthis);
    if(cl == NULL)
    {
        JNIUtil::throwError(_("bad c++ this"));
        return;
    }
    JNIStringHolder target(jtarget);
    if(JNIUtil::isExceptionThrown())
    {
        return;
    }
    Revision pegRevision(jpegRevision);
    if(JNIUtil::isExceptionThrown())
    {
        return;
    }
    Revision startRevision(jstartRevision);
    if(JNIUtil::isExceptionThrown())
    {
        return;
    }
    Revision endRevision(jendRevision);
    if(JNIUtil::isExceptionThrown())
    {
        return;
    }
    JNIStringHolder outfileName(joutfileName);
    if(JNIUtil::isExceptionThrown())
    {
        return;
    }
    cl->diff(target, pegRevision, startRevision, endRevision, outfileName,
        jrecurse ? true:false, jignoreAncestry ? true:false,
        jnoDiffDeleted ? true:false, jforce ? true:false);
}


/*
 * Class:     org_tigris_subversion_javahl_SVNClient
 * Method:    fileContent
 * Signature: (Ljava/lang/String;Lorg/tigris/subversion/javahl/Revision;
 *             Lorg/tigris/subversion/javahl/Revision;)[B
 */
JNIEXPORT jbyteArray JNICALL Java_org_tigris_subversion_javahl_SVNClient_fileContent
  (JNIEnv *env, jobject jthis, jstring jpath, jobject jrevision, 
   jobject jpegRevision)
{
    JNIEntry(SVNClient, fileContent);
    SVNClient *cl = SVNClient::getCppObject(jthis);
    if(cl == NULL)
    {
        JNIUtil::throwError(_("bad c++ this"));
        return NULL;
    }
    JNIStringHolder path(jpath);
    if(JNIUtil::isExceptionThrown())
    {
        return NULL;
    }
    Revision revision(jrevision);
    if(JNIUtil::isExceptionThrown())
    {
        return NULL;
    }
    Revision pegRevision(jpegRevision);
    if(JNIUtil::isExceptionThrown())
    {
        return NULL;
    }
    return cl->fileContent(path, revision, pegRevision);
}

JNIEXPORT void JNICALL Java_org_tigris_subversion_javahl_SVNClient_streamFileContent
  (JNIEnv *env, jobject jthis, jstring jpath, jobject jrevision,
   jobject jpegRevision, jint bufSize, jobject jstream)
{
    JNIEntry(SVNClient, streamFileContent);
    SVNClient *cl = SVNClient::getCppObject(jthis);
    if (cl == NULL)
    {
        JNIUtil::throwError(_("bad c++ this"));
        return;
    }
    JNIStringHolder path(jpath);
    if (JNIUtil::isExceptionThrown())
    {
        return;
    }
    Revision revision(jrevision);
    if (JNIUtil::isExceptionThrown())
    {
        return;
    }
    Revision pegRevision(jpegRevision);
    if (JNIUtil::isExceptionThrown())
    {
        return;
    }
    cl->streamFileContent(path, revision, pegRevision, jstream, bufSize);
}

/*
 * Class:     org_tigris_subversion_javahl_SVNClient
 * Method:    getVersionInfo
 * Signature: (Ljava/lang/String;Ljava/lang/String;Z)Ljava/lang/String;
 */
JNIEXPORT jstring JNICALL Java_org_tigris_subversion_javahl_SVNClient_getVersionInfo
  (JNIEnv *env, jobject jthis, jstring jpath, jstring jtrailUrl, 
   jboolean jlastChanged)
{
    JNIEntry(SVNClient, getVersionInfo);
    SVNClient *cl = SVNClient::getCppObject(jthis);
    if(cl == NULL)
    {
        JNIUtil::throwError(_("bad c++ this"));
        return NULL;
    }
    JNIStringHolder path(jpath);
    if(JNIUtil::isExceptionThrown())
    {
        return NULL;
    }
    JNIStringHolder trailUrl(jtrailUrl);
    return cl->getVersionInfo(path, trailUrl, jlastChanged ? true:false);
}

/*
 * Class:     org_tigris_subversion_javahl_SVNClient
 * Method:    enableLogging
 * Signature: (ILjava/lang/String;)V
 */
JNIEXPORT void JNICALL Java_org_tigris_subversion_javahl_SVNClient_enableLogging
  (JNIEnv* env, jclass jclazz, jint jlogLevel, jstring jpath)
{
    JNIEntryStatic(SVNClient, enableLogging);
    int cLevel = JNIUtil::noLog;
    switch(jlogLevel)
    {
    case org_tigris_subversion_javahl_SVNClientLogLevel_NoLog:
        cLevel = JNIUtil::noLog;
        break;
    case org_tigris_subversion_javahl_SVNClientLogLevel_ErrorLog:
        cLevel = JNIUtil::errorLog;
        break;
    case org_tigris_subversion_javahl_SVNClientLogLevel_ExceptionLog:
        cLevel = JNIUtil::exceptionLog;
        break;
    case org_tigris_subversion_javahl_SVNClientLogLevel_EntryLog:
        cLevel = JNIUtil::entryLog;
        break;
    }
    JNIUtil::initLogFile(cLevel, jpath);

}
/*
 * Class:     org_tigris_subversion_javahl_SVNClient
 * Method:    version
 * Signature: ()Ljava/lang/String;
 */
JNIEXPORT jstring JNICALL Java_org_tigris_subversion_javahl_SVNClient_version
  (JNIEnv *env, jclass jclazz)
{
    JNIEntryStatic(SVNClient, version);
    const char *version = "svn:" SVN_VERSION "\njni:" JNI_VERSION;
    return JNIUtil::makeJString(version);
}
/*
 * Class:     org_tigris_subversion_javahl_SVNClient
 * Method:    versionMajor
 * Signature: ()I
 */
JNIEXPORT jint JNICALL Java_org_tigris_subversion_javahl_SVNClient_versionMajor
  (JNIEnv *env, jclass jclazz)
{
    JNIEntryStatic(SVNClient, versionMajor);
    return JNI_VER_MAJOR;
}

/*
 * Class:     org_tigris_subversion_javahl_SVNClient
 * Method:    versionMinor
 * Signature: ()I
 */
JNIEXPORT jint JNICALL Java_org_tigris_subversion_javahl_SVNClient_versionMinor
  (JNIEnv *env, jclass jclazz)
{
    JNIEntryStatic(SVNClient, versionMinor);
    return JNI_VER_MINOR;
}

/*
 * Class:     org_tigris_subversion_javahl_SVNClient
 * Method:    versionMicro
 * Signature: ()I
 */
JNIEXPORT jint JNICALL Java_org_tigris_subversion_javahl_SVNClient_versionMicro
  (JNIEnv *env, jclass jclazz)
{
    JNIEntryStatic(SVNClient, versionMicro);
    return JNI_VER_MICRO;
}
/*
 * Class:     org_tigris_subversion_javahl_SVNClient
 * Method:    relocate
 * Signature: (Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;
 *             Lorg/tigris/subversion/javahl/Revision;Z)V
 */
JNIEXPORT void JNICALL Java_org_tigris_subversion_javahl_SVNClient_relocate
  (JNIEnv *env, jobject jthis, jstring jfrom, jstring jto, jstring jpath, 
   jboolean jrecurse)
{
    JNIEntry(SVNClient, relocate);
    SVNClient *cl = SVNClient::getCppObject(jthis);
    if(cl == NULL)
    {
        JNIUtil::throwError(_("bad c++ this"));
        return;
    }
    JNIStringHolder from(jfrom);
    if(JNIUtil::isExceptionThrown())
    {
        return;
    }
    JNIStringHolder to(jto);
    if(JNIUtil::isExceptionThrown())
    {
        return;
    }
    JNIStringHolder path(jpath);
    if(JNIUtil::isExceptionThrown())
    {
        return;
    }
    cl->relocate(from, to, path, jrecurse ? true: false);
    return;
}

/*
 * Class:     org_tigris_subversion_javahl_SVNClient
 * Method:    blame
 * Signature: (Ljava/lang/String;Lorg/tigris/subversion/javahl/Revision;
 *             Lorg/tigris/subversion/javahl/Revision;)[B
 */
JNIEXPORT jbyteArray JNICALL Java_org_tigris_subversion_javahl_SVNClient_blame__Ljava_lang_String_2Lorg_tigris_subversion_javahl_Revision_2Lorg_tigris_subversion_javahl_Revision_2
  (JNIEnv *env, jobject jthis, jstring jpath, jobject jrevisionStart, 
   jobject jrevisionEnd)
{
    JNIEntry(SVNClient, blame);
    SVNClient *cl = SVNClient::getCppObject(jthis);
    if(cl == NULL)
    {
        JNIUtil::throwError(_("bad c++ this"));
        return NULL;
    }
    JNIStringHolder path(jpath);
    if(JNIUtil::isExceptionThrown())
    {
        return NULL;
    }
    Revision revisionStart(jrevisionStart, false, true);
    if(JNIUtil::isExceptionThrown())
    {
        return NULL;
    }
    Revision revisionEnd(jrevisionEnd, true);
    if(JNIUtil::isExceptionThrown())
    {
        return NULL;
    }
    return cl->blame(path, revisionStart, revisionEnd);
}
/*
 * Class:     org_tigris_subversion_javahl_SVNClient
 * Method:    blame
 * Signature: (Ljava/lang/String;Lorg/tigris/subversion/javahl/Revision;
 *             Lorg/tigris/subversion/javahl/Revision;
 *             Lorg/tigris/subversion/javahl/Revision;
 *             Lorg/tigris/subversion/javahl/BlameCallback;)V
 */
JNIEXPORT void JNICALL Java_org_tigris_subversion_javahl_SVNClient_blame__Ljava_lang_String_2Lorg_tigris_subversion_javahl_Revision_2Lorg_tigris_subversion_javahl_Revision_2Lorg_tigris_subversion_javahl_Revision_2Lorg_tigris_subversion_javahl_BlameCallback_2
  (JNIEnv *env, jobject jthis, jstring jpath, jobject jpegRevision, 
   jobject jrevisionStart, jobject jrevisionEnd, jobject jblameCallback)
{
    JNIEntry(SVNClient, blame);
    SVNClient *cl = SVNClient::getCppObject(jthis);
    if(cl == NULL)
    {
        JNIUtil::throwError(_("bad c++ this"));
        return;
    }
    JNIStringHolder path(jpath);
    if(JNIUtil::isExceptionThrown())
    {
        return;
    }
    Revision pegRevision(jpegRevision, false, true);
    if(JNIUtil::isExceptionThrown())
    {
        return;
    }
    Revision revisionStart(jrevisionStart, false, true);
    if(JNIUtil::isExceptionThrown())
    {
        return;
    }
    Revision revisionEnd(jrevisionEnd, true);
    if(JNIUtil::isExceptionThrown())
    {
        return;
    }
    BlameCallback callback(jblameCallback);
    cl->blame(path, pegRevision, revisionStart, revisionEnd, &callback);
}
/*
 * Class:     org_tigris_subversion_javahl_SVNClient
 * Method:    setConfigDirectory
 * Signature: (Ljava/lang/String;)V
 */
JNIEXPORT void JNICALL Java_org_tigris_subversion_javahl_SVNClient_setConfigDirectory
  (JNIEnv *env, jobject jthis, jstring jconfigDir)
{
    JNIEntry(SVNClient, setConfigDirectory);
    SVNClient *cl = SVNClient::getCppObject(jthis);
    if(cl == NULL)
    {
        JNIUtil::throwError(_("bad c++ this"));
        return;
    }

    JNIStringHolder configDir(jconfigDir);
    if(JNIUtil::isExceptionThrown())
    {
        return;
    }

    cl->setConfigDirectory(configDir);
}
/*
 * Class:     org_tigris_subversion_javahl_SVNClient
 * Method:    getConfigDirectory
 * Signature: ()Ljava/lang/String;
 */
JNIEXPORT jstring JNICALL Java_org_tigris_subversion_javahl_SVNClient_getConfigDirectory
  (JNIEnv *env, jobject jthis)
{
    JNIEntry(SVNClient, getConfigDirectory);
    SVNClient *cl = SVNClient::getCppObject(jthis);
    if(cl == NULL)
    {
        JNIUtil::throwError(_("bad c++ this"));
        return NULL;
    }

    const char *configDir = cl->getConfigDirectory();
    return JNIUtil::makeJString(configDir);
}
/*
 * Class:     org_tigris_subversion_javahl_SVNClient
 * Method:    cancelOperation
 * Signature: ()V
 */
JNIEXPORT void JNICALL Java_org_tigris_subversion_javahl_SVNClient_cancelOperation
  (JNIEnv *env, jobject jthis)
{
    JNIEntry(SVNClient, cancelOperation);
    SVNClient *cl = SVNClient::getCppObject(jthis);
    if(cl == NULL)
    {
        JNIUtil::throwError("bad c++ this");
        return;
    }
    cl->cancelOperation();
}
/*
 * Class:     org_tigris_subversion_javahl_SVNClient
 * Method:    info
 * Signature: (Ljava/lang/String;)Lorg/tigris/subversion/javahl/Info;
 */
JNIEXPORT jobject JNICALL Java_org_tigris_subversion_javahl_SVNClient_info
  (JNIEnv *env, jobject jthis, jstring jpath)
{
    JNIEntry(SVNClient, info);
    SVNClient *cl = SVNClient::getCppObject(jthis);
    if(cl == NULL)
    {
        JNIUtil::throwError("bad c++ this");
        return NULL;
    }
    
    JNIStringHolder path(jpath);
    if(JNIUtil::isExceptionThrown())
    {
        return NULL;
    }
    return cl->info(path);
}
/*
 * Class:     org_tigris_subversion_javahl_SVNClient
 * Method:    lock
 * Signature: ([Ljava/lang/String;Ljava/lang/String;Z)V
 */
JNIEXPORT void JNICALL Java_org_tigris_subversion_javahl_SVNClient_lock
  (JNIEnv *env, jobject jthis, jobjectArray jtargets, jstring jcomment, 
   jboolean jforce)
{
    JNIEntry(SVNClient, lock);
    SVNClient *cl = SVNClient::getCppObject(jthis);
    if(cl == NULL)
    {
        JNIUtil::throwError("bad c++ this");
        return;
    }
    Targets targets(jtargets);    
    if(JNIUtil::isExceptionThrown())
    {
        return;
    }
    JNIStringHolder comment(jcomment);
    if(JNIUtil::isExceptionThrown())
    {
        return;
    }
    cl->lock(targets, comment, jforce ? true : false);
}

/*
 * Class:     org_tigris_subversion_javahl_SVNClient
 * Method:    unlock
 * Signature: ([Ljava/lang/String;Z)V
 */
JNIEXPORT void JNICALL Java_org_tigris_subversion_javahl_SVNClient_unlock
  (JNIEnv *env, jobject jthis, jobjectArray jtargets, jboolean jforce)
{
    JNIEntry(SVNClient, unlock);
    SVNClient *cl = SVNClient::getCppObject(jthis);
    if(cl == NULL)
    {
        JNIUtil::throwError("bad c++ this");
        return;
    }
    
    Targets targets(jtargets);
    if(JNIUtil::isExceptionThrown())
    {
        return;
    }

    cl->unlock(targets, jforce ? true : false);
}
/*
 * Class:     org_tigris_subversion_javahl_SVNClient
 * Method:    info2
 * Signature: (Ljava/lang/String;Lorg/tigris/subversion/javahl/Revision;
 *             Lorg/tigris/subversion/javahl/Revision;Z)
 *            [Lorg/tigris/subversion/javahl/Info2;
 */
JNIEXPORT jobjectArray JNICALL Java_org_tigris_subversion_javahl_SVNClient_info2
  (JNIEnv *env, jobject jthis, jstring jpath, jobject jrevision, 
   jobject jpegRevision, jboolean jrecurse)
{
    JNIEntry(SVNClient, unlock);
    SVNClient *cl = SVNClient::getCppObject(jthis);
    if(cl == NULL)
    {
        JNIUtil::throwError("bad c++ this");
        return NULL;
    }
    JNIStringHolder path(jpath);
    if(JNIUtil::isExceptionThrown())
    {
        return NULL;
    }
    Revision revision(jrevision);
    if(JNIUtil::isExceptionThrown())
    {
        return NULL;
    }
    Revision pegRevision(jpegRevision);
    if(JNIUtil::isExceptionThrown())
    {
        return NULL;
    }
    return cl->info2(path, revision, pegRevision, jrecurse ? true : false);
}
/*
 * Class:     org_tigris_subversion_javahl_SVNClient
 * Method:    initNative
 * Signature: ()V
 */
JNIEXPORT void JNICALL Java_org_tigris_subversion_javahl_SVNClient_initNative
  (JNIEnv *env, jclass jclazz)
{
	// No standard JNIEntry here, because this call initializes everthing
	JNIUtil::JNIGlobalInit(env);
}
