/**
 * @copyright
 * ====================================================================
 * Copyright (c) 2003 QintSoft.  All rights reserved.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution.  The terms
 * are also available at http://subversion.tigris.org/license-1.html.
 * If newer versions of this license are posted there, you may use a
 * newer version instead, at your option.
 *
 * This software consists of voluntary contributions made by many
 * individuals.  For exact contribution history, see the revision
 * history and logs, available at http://svnup.tigris.org/.
 * ====================================================================
 * @endcopyright
 *
 * @file org_tigris_subversion_javahl_SVNClient.cpp
 * @brief Implementation of the native methods in the java class SVNClient
 */
#include "org_tigris_subversion_javahl_SVNClient.h"
#include "org_tigris_subversion_javahl_SVNClient_LogLevel.h"
#include "JNIUtil.h"
#include "JNIStackElement.h"
#include "JNIStringHolder.h"
#include "JNIByteArray.h"
#include "SVNClient.h"
#include "Revision.h"
#include "Notify.h"
#include "Prompter.h"
#include "Targets.h"
#include "svn_version.h"
#include "version.h"
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
		JNIUtil::throwError("bad c++ this");
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
		JNIUtil::throwError("bad c++ this");
		return NULL;
	}
	const char *ret = cl->getLastPath();
	return JNIUtil::makeJString(ret);
}

/*
 * Class:     org_tigris_subversion_javahl_SVNClient
 * Method:    list
 * Signature: (Ljava/lang/String;Lorg/tigris/subversion/javahl/Revision;Z)[Lorg/tigris/subversion/javahl/DirEntry;
 */
JNIEXPORT jobjectArray JNICALL Java_org_tigris_subversion_javahl_SVNClient_list
  (JNIEnv* env, jobject jthis, jstring jurl, jobject jrevision, jboolean jrecurse)
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
	return cl->list(url, revision, jrecurse ? true:false);

}
/*
 * Class:     org_tigris_subversion_javahl_SVNClient
 * Method:    status
 * Signature: (Ljava/lang/String;ZZ)[Lorg/tigris/subversion/javahl/Status;
 */
JNIEXPORT jobjectArray JNICALL Java_org_tigris_subversion_javahl_SVNClient_status
  (JNIEnv* env, jobject jthis, jstring jpath, jboolean jrecurse, jboolean jonServer)
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
	return cl->status(path, jrecurse ? true: false, jonServer ? true:false);
}

/*
 * Class:     org_tigris_subversion_javahl_SVNClient
 * Method:    singleStatus
 * Signature: (Ljava/lang/String;Z)Lorg/tigris/subversion/javahl/Status;
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
		JNIUtil::throwError("bad c++ this");
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
		JNIUtil::throwError("bad c++ this");
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
		JNIUtil::throwError("bad c++ this");
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
 * Signature: (Ljava/lang/String;Lorg/tigris/subversion/javahl/Revision;Lorg/tigris/subversion/javahl/Revision;)[Lorg/tigris/subversion/javahl/LogMessage;
 */
JNIEXPORT jobjectArray JNICALL Java_org_tigris_subversion_javahl_SVNClient_logMessages
  (JNIEnv* env, jobject jthis, jstring jpath, jobject jrevisionStart, jobject jrevisionEnd)
{
	JNIEntry(SVNClient, logMessages);
	SVNClient *cl = SVNClient::getCppObject(jthis);
	if(cl == NULL)
	{
		JNIUtil::throwError("bad c++ this");
		return NULL;
	}
	Revision revisionStart(jrevisionStart);
	if(JNIUtil::isExceptionThrown())
	{
		return NULL;
	}
	Revision revisionEnd(jrevisionEnd);
	if(JNIUtil::isExceptionThrown())
	{
		return NULL;
	}
	JNIStringHolder path(jpath);
	if(JNIUtil::isExceptionThrown())
	{
		return NULL;
	}
	return cl->logMessages(path, revisionStart, revisionEnd);
}

/*
 * Class:     org_tigris_subversion_javahl_SVNClient
 * Method:    checkout
 * Signature: (Ljava/lang/String;Ljava/lang/String;Lorg/tigris/subversion/javahl/Revision;Z)V
 */
JNIEXPORT void JNICALL Java_org_tigris_subversion_javahl_SVNClient_checkout
  (JNIEnv* env, jobject jthis, jstring jmoduleName, jstring jdestPath, jobject jrevision, jboolean jrecurse)
{
	JNIEntry(SVNClient, checkout);
	SVNClient *cl = SVNClient::getCppObject(jthis);
	if(cl == NULL)
	{
		JNIUtil::throwError("bad c++ this");
		return;
	}
	Revision revision(jrevision);
	if(JNIUtil::isExceptionThrown())
	{
		return;
	}
	JNIStringHolder moduleName(jmoduleName);
	if(JNIUtil::isExceptionThrown())
	{
		return;
	}
	JNIStringHolder destPath(jdestPath);
	if(JNIUtil::isExceptionThrown())
	{
		return;
	}
	cl->checkout(moduleName, destPath, revision, jrecurse ? true:false);
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
		JNIUtil::throwError("bad c++ this");
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
 * Method:    remove
 * Signature: (Ljava/lang/String;Ljava/lang/String;Z)V
 */
JNIEXPORT void JNICALL Java_org_tigris_subversion_javahl_SVNClient_remove
  (JNIEnv *env, jobject jthis, jstring jpath, jstring jmessage, jboolean jforce)
{
	JNIEntry(SVNClient, remove);
	SVNClient *cl = SVNClient::getCppObject(jthis);
	if(cl == NULL)
	{
		JNIUtil::throwError("bad c++ this");
		return;
	}
	JNIStringHolder path(jpath);
	if(JNIUtil::isExceptionThrown())
	{
		return;
	}
	JNIStringHolder message(jmessage);
	if(JNIUtil::isExceptionThrown())
	{
		return;
	}
	cl->remove(path, message, jforce ? true : false);
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
		JNIUtil::throwError("bad c++ this");
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
 * Signature: (Ljava/lang/String;Z)V
 */
JNIEXPORT void JNICALL Java_org_tigris_subversion_javahl_SVNClient_add
  (JNIEnv* env, jobject jthis, jstring jpath, jboolean jrecurse)
{
	JNIEntry(SVNClient, add);
	SVNClient *cl = SVNClient::getCppObject(jthis);
	if(cl == NULL)
	{
		JNIUtil::throwError("bad c++ this");
		return;
	}
	JNIStringHolder path(jpath);
	if(JNIUtil::isExceptionThrown())
	{
		return;
	}
	cl->add(path, jrecurse ? true : false);
}

/*
 * Class:     org_tigris_subversion_javahl_SVNClient
 * Method:    update
 * Signature: (Ljava/lang/String;Lorg/tigris/subversion/javahl/Revision;Z)V
 */
JNIEXPORT void JNICALL Java_org_tigris_subversion_javahl_SVNClient_update
  (JNIEnv* env, jobject jthis, jstring jpath, jobject jrevision, jboolean jrecurse)
{
	JNIEntry(SVNClient, update);
	SVNClient *cl = SVNClient::getCppObject(jthis);
	if(cl == NULL)
	{
		JNIUtil::throwError("bad c++ this");
		return;
	}
	Revision revision(jrevision);
	if(JNIUtil::isExceptionThrown())
	{
		return;
	}
	JNIStringHolder path(jpath);
	if(JNIUtil::isExceptionThrown())
	{
		return;
	}
	cl->update(path, revision, jrecurse ? true:false);
}

/*
 * Class:     org_tigris_subversion_javahl_SVNClient
 * Method:    commit
 * Signature: ([Ljava/lang/String;Ljava/lang/String;Z)J
 */
JNIEXPORT jlong JNICALL Java_org_tigris_subversion_javahl_SVNClient_commit
  (JNIEnv* env, jobject jthis, jobjectArray jtargets, jstring jmessage, jboolean jrecurse)
{
	JNIEntry(SVNClient, commit);
	SVNClient *cl = SVNClient::getCppObject(jthis);
	if(cl == NULL)
	{
		JNIUtil::throwError("bad c++ this");
		return -1;
	}
	Targets targets(jtargets);
	JNIStringHolder message(jmessage);
	if(JNIUtil::isExceptionThrown())
	{
		return -1;
	}
	return cl->commit(targets, message, jrecurse ? true : false);
}

/*
 * Class:     org_tigris_subversion_javahl_SVNClient
 * Method:    copy
 * Signature: (Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Lorg/tigris/subversion/javahl/Revision;)V
 */
JNIEXPORT void JNICALL Java_org_tigris_subversion_javahl_SVNClient_copy
  (JNIEnv* env, jobject jthis, jstring jsrcPath, jstring jdestPath, jstring jmessage, jobject jrevision)
{
	JNIEntry(SVNClient, copy);
	SVNClient *cl = SVNClient::getCppObject(jthis);
	if(cl == NULL)
	{
		JNIUtil::throwError("bad c++ this");
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
 * Signature: (Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Lorg/tigris/subversion/javahl/Revision;Z)V
 */
JNIEXPORT void JNICALL Java_org_tigris_subversion_javahl_SVNClient_move
  (JNIEnv *env, jobject jthis, jstring jsrcPath, jstring jdestPath, jstring jmessage, jobject jrevision, jboolean jforce)
{
	JNIEntry(SVNClient, move);
	SVNClient *cl = SVNClient::getCppObject(jthis);
	if(cl == NULL)
	{
		JNIUtil::throwError("bad c++ this");
		return;
	}
	Revision revision(jrevision);
	if(JNIUtil::isExceptionThrown())
	{
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
	cl->move(srcPath, destPath, message, revision, jforce ? true:false);
}

/*
 * Class:     org_tigris_subversion_javahl_SVNClient
 * Method:    mkdir
 * Signature: (Ljava/lang/String;Ljava/lang/String;)V
 */
JNIEXPORT void JNICALL Java_org_tigris_subversion_javahl_SVNClient_mkdir
  (JNIEnv* env, jobject jthis, jstring jpath, jstring jmessage)
{
	JNIEntry(SVNClient, mkdir);
	SVNClient *cl = SVNClient::getCppObject(jthis);
	if(cl == NULL)
	{
		JNIUtil::throwError("bad c++ this");
		return;
	}
	JNIStringHolder path(jpath);
	if(JNIUtil::isExceptionThrown())
	{
		return;
	}
	JNIStringHolder message(jmessage);
	if(JNIUtil::isExceptionThrown())
	{
		return;
	}
	cl->mkdir(path, message);
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
		JNIUtil::throwError("bad c++ this");
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
 * Method:    resolve
 * Signature: (Ljava/lang/String;Z)V
 */
JNIEXPORT void JNICALL Java_org_tigris_subversion_javahl_SVNClient_resolve
  (JNIEnv* env, jobject jthis, jstring jpath, jboolean jrecurse)
{
	JNIEntry(SVNClient, resolve);
	SVNClient *cl = SVNClient::getCppObject(jthis);
	if(cl == NULL)
	{
		JNIUtil::throwError("bad c++ this");
		return;
	}
	JNIStringHolder path(jpath);
	if(JNIUtil::isExceptionThrown())
	{
		return;
	}
	cl->resolve(path, jrecurse ? true: false);
}

/*
 * Class:     org_tigris_subversion_javahl_SVNClient
 * Method:    doExport
 * Signature: (Ljava/lang/String;Ljava/lang/String;Lorg/tigris/subversion/javahl/Revision;)V
 */
JNIEXPORT void JNICALL Java_org_tigris_subversion_javahl_SVNClient_doExport
  (JNIEnv* env, jobject jthis, jstring jsrcPath, jstring jdestPath, jobject jrevision)
{
	JNIEntry(SVNClient, doExport);
	SVNClient *cl = SVNClient::getCppObject(jthis);
	if(cl == NULL)
	{
		JNIUtil::throwError("bad c++ this");
		return;
	}
	Revision revision(jrevision);
	if(JNIUtil::isExceptionThrown())
	{
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
	cl->doExport(srcPath, destPath, revision);
}

/*
 * Class:     org_tigris_subversion_javahl_SVNClient
 * Method:    doSwitch
 * Signature: (Ljava/lang/String;Ljava/lang/String;Lorg/tigris/subversion/javahl/Revision;Z)V
 */
JNIEXPORT void JNICALL Java_org_tigris_subversion_javahl_SVNClient_doSwitch
  (JNIEnv* env, jobject jthis, jstring jpath, jstring jurl, jobject jrevision, jboolean jrecurse)
{
	JNIEntry(SVNClient, doSwitch);
	SVNClient *cl = SVNClient::getCppObject(jthis);
	if(cl == NULL)
	{
		JNIUtil::throwError("bad c++ this");
		return;
	}
	Revision revision(jrevision);
	if(JNIUtil::isExceptionThrown())
	{
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
	cl->doSwitch(path, url, revision, jrecurse ? true: false);
}

/*
 * Class:     org_tigris_subversion_javahl_SVNClient
 * Method:    doImport
 * Signature: (Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Z)V
 */
JNIEXPORT void JNICALL Java_org_tigris_subversion_javahl_SVNClient_doImport
  (JNIEnv* env, jobject jthis, jstring jpath, jstring jurl, jstring jnewEntry, jstring jmessage, jboolean jrecurse)
{
	JNIEntry(SVNClient, doImport);
	SVNClient *cl = SVNClient::getCppObject(jthis);
	if(cl == NULL)
	{
		JNIUtil::throwError("bad c++ this");
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
	JNIStringHolder newEntry(jnewEntry);
	if(JNIUtil::isExceptionThrown())
	{
		return;
	}
	JNIStringHolder message(jmessage);
	if(JNIUtil::isExceptionThrown())
	{
		return;
	}
	cl->doImport(path, url, newEntry, message, jrecurse ? true : false);
}

/*
 * Class:     org_tigris_subversion_javahl_SVNClient
 * Method:    merge
 * Signature: (Ljava/lang/String;Lorg/tigris/subversion/javahl/Revision;Ljava/lang/String;Lorg/tigris/subversion/javahl/Revision;Ljava/lang/String;ZZ)V
 */
JNIEXPORT void JNICALL Java_org_tigris_subversion_javahl_SVNClient_merge
  (JNIEnv* env, jobject jthis, jstring jpath1, jobject jrevision1, jstring jpath2, jobject jrevision2, jstring jlocalPath, jboolean jforce, jboolean jrecurse)
{
	JNIEntry(SVNClient, merge);
	SVNClient *cl = SVNClient::getCppObject(jthis);
	if(cl == NULL)
	{
		JNIUtil::throwError("bad c++ this");
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
	cl->merge(path1, revision1, path2, revision2, localPath, jforce ? true:false, jrecurse ? true:false);
}

/*
 * Class:     org_tigris_subversion_javahl_SVNClient
 * Method:    properties
 * Signature: (Ljava/lang/String;)[Lorg/tigris/subversion/javahl/PropertyData;
 */
JNIEXPORT jobjectArray JNICALL Java_org_tigris_subversion_javahl_SVNClient_properties
  (JNIEnv* env, jobject jthis, jstring jpath)
{
	JNIEntry(SVNClient, properties);
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
	return cl->properties(jthis, path);
}

/*
 * Class:     org_tigris_subversion_javahl_SVNClient
 * Method:    propertySet
 * Signature: (Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Z)V
 */
JNIEXPORT void JNICALL Java_org_tigris_subversion_javahl_SVNClient_propertySet__Ljava_lang_String_2Ljava_lang_String_2Ljava_lang_String_2Z
  (JNIEnv* env, jobject jthis, jstring jpath, jstring jname, jstring jvalue, jboolean jrecurse)
{
	JNIEntry(SVNClient, propertySet);
	SVNClient *cl = SVNClient::getCppObject(jthis);
	if(cl == NULL)
	{
		JNIUtil::throwError("bad c++ this");
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
	cl->propertySet(path, name, value, jrecurse ? true:false);
}

/*
 * Class:     org_tigris_subversion_javahl_SVNClient
 * Method:    propertySet
 * Signature: (Ljava/lang/String;Ljava/lang/String;[BZ)V
 */
JNIEXPORT void JNICALL Java_org_tigris_subversion_javahl_SVNClient_propertySet__Ljava_lang_String_2Ljava_lang_String_2_3BZ
  (JNIEnv* env, jobject jthis, jstring jpath, jstring jname, jbyteArray jvalue, jboolean jrecurse)
{
	JNIEntry(SVNClient, propertySet);
	SVNClient *cl = SVNClient::getCppObject(jthis);
	if(cl == NULL)
	{
		JNIUtil::throwError("bad c++ this");
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
	cl->propertySet(path, name, value, jrecurse ? true:false);
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
		JNIUtil::throwError("bad c++ this");
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
 * Signature: (Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Z)V
 */
JNIEXPORT void JNICALL Java_org_tigris_subversion_javahl_SVNClient_propertyCreate__Ljava_lang_String_2Ljava_lang_String_2Ljava_lang_String_2Z
  (JNIEnv* env, jobject jthis, jstring jpath, jstring jname, jstring jvalue, jboolean jrecurse)
{
	JNIEntry(SVNClient, propertyCreate);
	SVNClient *cl = SVNClient::getCppObject(jthis);
	if(cl == NULL)
	{
		JNIUtil::throwError("bad c++ this");
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
	cl->propertyCreate(path, name, value, jrecurse ? true:false);
}

/*
 * Class:     org_tigris_subversion_javahl_SVNClient
 * Method:    propertyCreate
 * Signature: (Ljava/lang/String;Ljava/lang/String;[BZ)V
 */
JNIEXPORT void JNICALL Java_org_tigris_subversion_javahl_SVNClient_propertyCreate__Ljava_lang_String_2Ljava_lang_String_2_3BZ
  (JNIEnv* env, jobject jthis, jstring jpath, jstring jname, jbyteArray jvalue, jboolean jrecurse)
{
	JNIEntry(SVNClient, propertyCreate);
	SVNClient *cl = SVNClient::getCppObject(jthis);
	if(cl == NULL)
	{
		JNIUtil::throwError("bad c++ this");
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
	cl->propertyCreate(path, name, value, jrecurse ? true:false);
}
/*
 * Class:     org_tigris_subversion_javahl_SVNClient
 * Method:    revProperty
 * Signature: (Ljava/lang/String;Ljava/lang/String;Lorg/tigris/subversion/javahl/Revision;)Lorg/tigris/subversion/javahl/PropertyData;
 */
JNIEXPORT jobject JNICALL Java_org_tigris_subversion_javahl_SVNClient_revProperty
  (JNIEnv *env, jobject jthis, jstring jpath, jstring jname, jobject jrevision)
{
	JNIEntry(SVNClient, revProperty);
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
 * Method:    fileContent
 * Signature: (Ljava/lang/String;Lorg/tigris/subversion/javahl/Revision;)[B
 */
JNIEXPORT jbyteArray JNICALL Java_org_tigris_subversion_javahl_SVNClient_fileContent
  (JNIEnv *env, jobject jthis, jstring jpath, jobject jrevision)
{
	JNIEntry(SVNClient, propertyCreate);
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
	return cl->fileContent(path, revision);
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
	case org_tigris_subversion_javahl_SVNClient_LogLevel_NoLog:
		cLevel = JNIUtil::noLog;
		break;
	case org_tigris_subversion_javahl_SVNClient_LogLevel_ErrorLog:
		cLevel = JNIUtil::errorLog;
		break;
	case org_tigris_subversion_javahl_SVNClient_LogLevel_ExceptionLog:
		cLevel = JNIUtil::exceptionLog;
		break;
	case org_tigris_subversion_javahl_SVNClient_LogLevel_EntryLog:
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
