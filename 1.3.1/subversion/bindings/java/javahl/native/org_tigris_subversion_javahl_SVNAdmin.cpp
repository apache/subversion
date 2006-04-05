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
 * @file org_tigris_subversion_javahl_SVNAdmin.cpp
 * @brief Implementation of the native methods in the java class SVNAdmin
 */
#include "../include/org_tigris_subversion_javahl_SVNAdmin.h"
#include "JNIUtil.h"
#include "JNIStackElement.h"
#include "JNIStringHolder.h"
#include "JNIByteArray.h"
#include "SVNAdmin.h"
#include "Revision.h"
#include "Inputer.h"
#include "Outputer.h"
#include "MessageReceiver.h"
#include "svn_private_config.h"

/*
 * Class:     org_tigris_subversion_javahl_SVNAdmin
 * Method:    ctNative
 * Signature: ()J
 */
JNIEXPORT jlong JNICALL Java_org_tigris_subversion_javahl_SVNAdmin_ctNative
  (JNIEnv *env, jobject jthis)
{
    JNIEntry(SVNAdmin, ctNative);
    SVNAdmin *obj = new SVNAdmin;
    return obj->getCppAddr();
}

/*
 * Class:     org_tigris_subversion_javahl_SVNAdmin
 * Method:    dispose
 * Signature: ()V
 */
JNIEXPORT void JNICALL Java_org_tigris_subversion_javahl_SVNAdmin_dispose
  (JNIEnv *env, jobject jthis)
{
    JNIEntry(SVNAdmin, dispose);
    SVNAdmin *cl = SVNAdmin::getCppObject(jthis);
    if(cl == NULL)
    {
        JNIUtil::throwError(_("bad c++ this"));
        return;
    }
    cl->dispose(jthis);
}

/*
 * Class:     org_tigris_subversion_javahl_SVNAdmin
 * Method:    finalize
 * Signature: ()V
 */
JNIEXPORT void JNICALL Java_org_tigris_subversion_javahl_SVNAdmin_finalize
  (JNIEnv* env, jobject jthis)
{
    JNIEntry(SVNAdmin, finalize);
    SVNAdmin *cl = SVNAdmin::getCppObject(jthis);
    if(cl != NULL)
    {
        cl->finalize();
    }
}

/*
 * Class:     org_tigris_subversion_javahl_SVNAdmin
 * Method:    create
 * Signature: (Ljava/lang/String;ZZLjava/lang/String;Ljava/lang/String;)V
 */
JNIEXPORT void JNICALL Java_org_tigris_subversion_javahl_SVNAdmin_create
  (JNIEnv *env, jobject jthis, jstring jpath, jboolean jdisableFsyncCommit,
  jboolean jkeepLog, jstring jconfigpath, jstring jfstype)
{
    JNIEntry(SVNAdmin, create);
    SVNAdmin *cl = SVNAdmin::getCppObject(jthis);
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

    JNIStringHolder configpath(jconfigpath);
    if(JNIUtil::isExceptionThrown())
    {
        return;
    }

    JNIStringHolder fstype(jfstype);
    if(JNIUtil::isExceptionThrown())
    {
        return;
    }

    cl->create(path, jdisableFsyncCommit? true : false, jkeepLog? true : false,
        configpath, fstype);
}

/*
 * Class:     org_tigris_subversion_javahl_SVNAdmin
 * Method:    deltify
 * Signature: (Ljava/lang/String;Lorg/tigris/subversion/javahl/Revision;
 *             Lorg/tigris/subversion/javahl/Revision;)V
 */
JNIEXPORT void JNICALL Java_org_tigris_subversion_javahl_SVNAdmin_deltify
  (JNIEnv *env, jobject jthis, jstring jpath, jobject jrevisionStart, 
   jobject jrevisionStop)
{
    JNIEntry(SVNAdmin, deltify);
    SVNAdmin *cl = SVNAdmin::getCppObject(jthis);
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

    Revision revisionStart(jrevisionStart);
    if(JNIUtil::isExceptionThrown())
    {
        return;
    }

    Revision revisionEnd(jrevisionStop);
    if(JNIUtil::isExceptionThrown())
    {
        return;
    }
    cl->deltify(path, revisionStart, revisionEnd);
}

/*
 * Class:     org_tigris_subversion_javahl_SVNAdmin
 * Method:    dump
 * Signature: (Ljava/lang/String;Lorg/tigris/subversion/javahl/OutputInterface;
 *             Lorg/tigris/subversion/javahl/OutputInterface;
 *             Lorg/tigris/subversion/javahl/Revision;
 *             Lorg/tigris/subversion/javahl/Revision;Z)V
 */
JNIEXPORT void JNICALL Java_org_tigris_subversion_javahl_SVNAdmin_dump
  (JNIEnv *env, jobject jthis, jstring jpath, jobject jdataout, 
   jobject jmessageout, jobject jrevisionStart,
   jobject jrevisionEnd, jboolean jincremental)
{
    JNIEntry(SVNAdmin, dump);
    SVNAdmin *cl = SVNAdmin::getCppObject(jthis);
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
    Outputer dataOut(jdataout);
    if(JNIUtil::isExceptionThrown())
    {
        return;
    }

    Outputer messageOut(jmessageout);
    if(JNIUtil::isExceptionThrown())
    {
        return;
    }

    Revision revisionStart(jrevisionStart);
    if(JNIUtil::isExceptionThrown())
    {
        return;
    }

    Revision revisionEnd(jrevisionEnd);
    if(JNIUtil::isExceptionThrown())
    {
        return;
    }

    cl->dump(path, dataOut, messageOut, revisionStart, revisionEnd, 
             jincremental ? true : false);
}
/*
 * Class:     org_tigris_subversion_javahl_SVNAdmin
 * Method:    hotcopy
 * Signature: (Ljava/lang/String;Ljava/lang/String;Z)V
 */
JNIEXPORT void JNICALL Java_org_tigris_subversion_javahl_SVNAdmin_hotcopy
  (JNIEnv *env, jobject jthis, jstring jpath, jstring jtargetPath, 
   jboolean jcleanLogs)
{
    JNIEntry(SVNAdmin, hotcopy);
    SVNAdmin *cl = SVNAdmin::getCppObject(jthis);
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

    JNIStringHolder targetPath(jtargetPath);
    if(JNIUtil::isExceptionThrown())
    {
        return;
    }

    cl->hotcopy(path, targetPath, jcleanLogs ? true : false);
}
/*
 * Class:     org_tigris_subversion_javahl_SVNAdmin
 * Method:    listDBLogs
 * Signature: (Ljava/lang/String;
 *             Lorg/tigris/subversion/javahl/SVNAdmin$MessageReceiver;)V
 */
JNIEXPORT void JNICALL Java_org_tigris_subversion_javahl_SVNAdmin_listDBLogs
  (JNIEnv *env, jobject jthis, jstring jpath, jobject jreceiver)
{
    JNIEntry(SVNAdmin, listDBLogs);
    SVNAdmin *cl = SVNAdmin::getCppObject(jthis);
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

    MessageReceiver mr(jreceiver);
    if(JNIUtil::isExceptionThrown())
    {
        return;
    }

    cl->listDBLogs(path, mr);
}

/*
 * Class:     org_tigris_subversion_javahl_SVNAdmin
 * Method:    listUnusedDBLogs
 * Signature: (Ljava/lang/String;
 *             Lorg/tigris/subversion/javahl/SVNAdmin$MessageReceiver;)V
 */
JNIEXPORT void JNICALL Java_org_tigris_subversion_javahl_SVNAdmin_listUnusedDBLogs
  (JNIEnv *env, jobject jthis, jstring jpath, jobject jreceiver)
{
    JNIEntry(SVNAdmin, listUnusedDBLogs);
    SVNAdmin *cl = SVNAdmin::getCppObject(jthis);
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

    MessageReceiver mr(jreceiver);
    if(JNIUtil::isExceptionThrown())
    {
        return;
    }

    cl->listUnusedDBLogs(path, mr);
}

/*
 * Class:     org_tigris_subversion_javahl_SVNAdmin
 * Method:    load
 * Signature: (Ljava/lang/String;Lorg/tigris/subversion/javahl/InputInterface;
 *             Lorg/tigris/subversion/javahl/OutputInterface;ZZ
 *             Ljava/lang/String;)V
 */
JNIEXPORT void JNICALL Java_org_tigris_subversion_javahl_SVNAdmin_load
  (JNIEnv *env, jobject jthis, jstring jpath, jobject jinputData, 
   jobject joutputMsg, jboolean jignoreUUID, jboolean jforceUUID, 
   jstring jrelativePath)
{
    JNIEntry(SVNAdmin, load);
    SVNAdmin *cl = SVNAdmin::getCppObject(jthis);
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

    Inputer inputData(jinputData);
    if(JNIUtil::isExceptionThrown())
    {
        return;
    }

    Outputer outputMsg(joutputMsg);
    if(JNIUtil::isExceptionThrown())
    {
        return;
    }

    JNIStringHolder relativePath(jrelativePath);
    if(JNIUtil::isExceptionThrown())
    {
        return;
    }

    cl->load(path, inputData, outputMsg, jignoreUUID ? true : false, 
             jforceUUID ? true : false, relativePath);
}

/*
 * Class:     org_tigris_subversion_javahl_SVNAdmin
 * Method:    lstxns
 * Signature: (Ljava/lang/String;
 *             Lorg/tigris/subversion/javahl/SVNAdmin$MessageReceiver;)V
 */
JNIEXPORT void JNICALL Java_org_tigris_subversion_javahl_SVNAdmin_lstxns
  (JNIEnv *env, jobject jthis, jstring jpath, jobject jmessageReceiver)
{
    JNIEntry(SVNAdmin, lstxns);
    SVNAdmin *cl = SVNAdmin::getCppObject(jthis);
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

    MessageReceiver mr(jmessageReceiver);
    if(JNIUtil::isExceptionThrown())
    {
        return;
    }

    cl->lstxns(path, mr);
}
/*
 * Class:     org_tigris_subversion_javahl_SVNAdmin
 * Method:    recover
 * Signature: (Ljava/lang/String;)V
 */
JNIEXPORT jlong JNICALL Java_org_tigris_subversion_javahl_SVNAdmin_recover
  (JNIEnv *env, jobject jthis, jstring jpath)
{
    JNIEntry(SVNAdmin, recover);
    SVNAdmin *cl = SVNAdmin::getCppObject(jthis);
    if(cl == NULL)
    {
        JNIUtil::throwError(_("bad c++ this"));
        return -1;
    }

    JNIStringHolder path(jpath);
    if(JNIUtil::isExceptionThrown())
    {
        return -1;
    }

    return cl->recover(path);
}

/*
 * Class:     org_tigris_subversion_javahl_SVNAdmin
 * Method:    rmtxns
 * Signature: (Ljava/lang/String;[Ljava/lang/String;)V
 */
JNIEXPORT void JNICALL Java_org_tigris_subversion_javahl_SVNAdmin_rmtxns
  (JNIEnv *env, jobject jthis, jstring jpath, jobjectArray jtransactions)
{
    JNIEntry(SVNAdmin, rmtxns);
    SVNAdmin *cl = SVNAdmin::getCppObject(jthis);
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

    Targets transactions(jtransactions);
    if(JNIUtil::isExceptionThrown())
    {
        return;
    }
    transactions.setDoesNotContainsPath();
    cl->rmtxns(path, transactions);
}

/*
 * Class:     org_tigris_subversion_javahl_SVNAdmin
 * Method:    setLog
 * Signature: (Ljava/lang/String;Lorg/tigris/subversion/javahl/Revision;
 *             Ljava/lang/String;Z)V
 */
JNIEXPORT void JNICALL Java_org_tigris_subversion_javahl_SVNAdmin_setLog
  (JNIEnv *env, jobject jthis, jstring jpath, jobject jrevision, 
   jstring jmessage, jboolean jbypassHooks)
{
    JNIEntry(SVNAdmin, setLog);
    SVNAdmin *cl = SVNAdmin::getCppObject(jthis);
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

    Revision revision(jrevision);
    if(JNIUtil::isExceptionThrown())
    {
        return;
    }

    JNIStringHolder message(jmessage);
    if(JNIUtil::isExceptionThrown())
    {
        return;
    }

    cl->setLog(path, revision, message, jbypassHooks ? true : false);
}
/*
 * Class:     org_tigris_subversion_javahl_SVNAdmin
 * Method:    verify
 * Signature: (Ljava/lang/String;Lorg/tigris/subversion/javahl/OutputInterface;
 *             Lorg/tigris/subversion/javahl/Revision;
 *             Lorg/tigris/subversion/javahl/Revision;)V
 */
JNIEXPORT void JNICALL Java_org_tigris_subversion_javahl_SVNAdmin_verify
  (JNIEnv *env, jobject jthis, jstring jpath, jobject jmessageout, 
   jobject jrevisionStart, jobject jrevisionEnd)
{
    JNIEntry(SVNAdmin, dump);
    SVNAdmin *cl = SVNAdmin::getCppObject(jthis);
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

    Outputer messageOut(jmessageout);
    if(JNIUtil::isExceptionThrown())
    {
        return;
    }

    Revision revisionStart(jrevisionStart);
    if(JNIUtil::isExceptionThrown())
    {
        return;
    }

    Revision revisionEnd(jrevisionEnd);
    if(JNIUtil::isExceptionThrown())
    {
        return;
    }

    cl->verify(path, messageOut, revisionStart, revisionEnd);
}
/*
 * Class:     org_tigris_subversion_javahl_SVNAdmin
 * Method:    lslocks
 * Signature: (Ljava/lang/String;)[Lorg/tigris/subversion/javahl/Lock;
 */
JNIEXPORT jobjectArray JNICALL Java_org_tigris_subversion_javahl_SVNAdmin_lslocks
  (JNIEnv *env, jobject jthis, jstring jpath)
{
    JNIEntry(SVNAdmin, lstxns);
    SVNAdmin *cl = SVNAdmin::getCppObject(jthis);
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
    return cl->lslocks(path);
}
/*
 * Class:     org_tigris_subversion_javahl_SVNAdmin
 * Method:    rmlocks
 * Signature: (Ljava/lang/String;[Ljava/lang/String;)V
 */
JNIEXPORT void JNICALL Java_org_tigris_subversion_javahl_SVNAdmin_rmlocks
  (JNIEnv *env, jobject jthis, jstring jpath, jobjectArray jlocks)
{
    JNIEntry(SVNAdmin, rmlocks);
    SVNAdmin *cl = SVNAdmin::getCppObject(jthis);
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

    Targets locks(jlocks);
    if(JNIUtil::isExceptionThrown())
    {
        return;
    }
    locks.setDoesNotContainsPath();
    cl->rmlocks(path, locks);
}
