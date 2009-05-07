/**
 * @copyright
 * ====================================================================
 * Copyright (c) 2003-2009 CollabNet.  All rights reserved.
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
 * @brief Implementation of the native methods in the Java class SVNClient
 */

#include "../include/org_tigris_subversion_javahl_SVNClient.h"
#include "../include/org_tigris_subversion_javahl_SVNClientLogLevel.h"
#include "JNIUtil.h"
#include "JNIStackElement.h"
#include "JNIStringHolder.h"
#include "JNIByteArray.h"
#include "SVNClient.h"
#include "Revision.h"
#include "RevisionRange.h"
#include "Notify.h"
#include "Notify2.h"
#include "ConflictResolverCallback.h"
#include "ProgressListener.h"
#include "CommitMessage.h"
#include "Prompter.h"
#include "Targets.h"
#include "CopySources.h"
#include "DiffSummaryReceiver.h"
#include "BlameCallback.h"
#include "ProplistCallback.h"
#include "LogMessageCallback.h"
#include "InfoCallback.h"
#include "StatusCallback.h"
#include "ListCallback.h"
#include "ChangelistCallback.h"
#include "StringArray.h"
#include "RevpropTable.h"
#include "svn_version.h"
#include "svn_private_config.h"
#include "version.h"
#include "Outputer.h"
#include <iostream>

JNIEXPORT jlong JNICALL
Java_org_tigris_subversion_javahl_SVNClient_ctNative
(JNIEnv *env, jobject jthis)
{
  JNIEntry(SVNClient, ctNative);
  SVNClient *obj = new SVNClient;
  return obj->getCppAddr();
}

JNIEXPORT void JNICALL
Java_org_tigris_subversion_javahl_SVNClient_dispose
(JNIEnv *env, jobject jthis)
{
  JNIEntry(SVNClient, dispose);
  SVNClient *cl = SVNClient::getCppObject(jthis);
  if (cl == NULL)
    {
      JNIUtil::throwError(_("bad C++ this"));
      return;
    }
  cl->dispose(jthis);
}

JNIEXPORT void JNICALL
Java_org_tigris_subversion_javahl_SVNClient_finalize
(JNIEnv *env, jobject jthis)
{
  JNIEntry(SVNClient, finalize);
  SVNClient *cl = SVNClient::getCppObject(jthis);
  if (cl != NULL)
    cl->finalize();
}

JNIEXPORT jstring JNICALL
Java_org_tigris_subversion_javahl_SVNClient_getAdminDirectoryName
(JNIEnv *env, jobject jthis)
{
  JNIEntry(SVNClient, getAdminDirectoryName);
  SVNClient *cl = SVNClient::getCppObject(jthis);
  if (cl == NULL)
    {
      JNIUtil::throwError(_("bad C++ this"));
      return NULL;
    }
  return cl->getAdminDirectoryName();
}

JNIEXPORT jboolean JNICALL
Java_org_tigris_subversion_javahl_SVNClient_isAdminDirectory
(JNIEnv *env, jobject jthis, jstring jname)
{
  JNIEntry(SVNClient, isAdminDirectory);
  SVNClient *cl = SVNClient::getCppObject(jthis);
  if (cl == NULL)
    {
      JNIUtil::throwError(_("bad C++ this"));
      return JNI_FALSE;
    }
  JNIStringHolder name(jname);
  if (JNIUtil::isExceptionThrown())
    return JNI_FALSE;

  return cl->isAdminDirectory(name);
}

JNIEXPORT jstring JNICALL
Java_org_tigris_subversion_javahl_SVNClient_getLastPath
(JNIEnv *env, jobject jthis)
{
  JNIEntry(SVNClient, getLastPath);
  SVNClient *cl = SVNClient::getCppObject(jthis);
  if (cl == NULL)
    {
      JNIUtil::throwError(_("bad C++ this"));
      return NULL;
    }
  const char *ret = cl->getLastPath();
  return JNIUtil::makeJString(ret);
}

JNIEXPORT void JNICALL
Java_org_tigris_subversion_javahl_SVNClient_list
(JNIEnv *env, jobject jthis, jstring jurl, jobject jrevision,
 jobject jpegRevision, jint jdepth, jint jdirentFields,
 jboolean jfetchLocks, jobject jcallback)
{
  JNIEntry(SVNClient, list);
  SVNClient *cl = SVNClient::getCppObject(jthis);
  if (cl == NULL)
    return;

  JNIStringHolder url(jurl);
  if (JNIUtil::isExceptionThrown())
    return;

  Revision revision(jrevision);
  if (JNIUtil::isExceptionThrown())
    return;

  Revision pegRevision(jpegRevision);
  if (JNIUtil::isExceptionThrown())
    return;

  ListCallback callback(jcallback);
  cl->list(url, revision, pegRevision, (svn_depth_t)jdepth,
           (int)jdirentFields, jfetchLocks ? true : false, &callback);
}

JNIEXPORT void JNICALL
Java_org_tigris_subversion_javahl_SVNClient_status
(JNIEnv *env, jobject jthis, jstring jpath, jint jdepth,
 jboolean jonServer, jboolean jgetAll, jboolean jnoIgnore,
 jboolean jignoreExternals, jobjectArray jchangelists,
 jobject jstatusCallback)
{
  JNIEntry(SVNClient, status);
  SVNClient *cl = SVNClient::getCppObject(jthis);
  if (cl == NULL)
    return;

  JNIStringHolder path(jpath);
  if (JNIUtil::isExceptionThrown())
    return;

  StringArray changelists(jchangelists);
  if (JNIUtil::isExceptionThrown())
    return;

  StatusCallback callback(jstatusCallback);
  cl->status(path, (svn_depth_t)jdepth,
             jonServer ? true:false,
             jgetAll ? true:false, jnoIgnore ? true:false,
             jignoreExternals ? true:false, changelists, &callback);
}

JNIEXPORT void JNICALL
Java_org_tigris_subversion_javahl_SVNClient_username
(JNIEnv *env, jobject jthis, jstring jusername)
{
  JNIEntry(SVNClient, username);
  SVNClient *cl = SVNClient::getCppObject(jthis);
  if (cl == NULL)
    {
      JNIUtil::throwError(_("bad C++ this"));
      return;
    }
  if (jusername == NULL)
    {
      JNIUtil::raiseThrowable("java/lang/IllegalArgumentException",
                              _("Provide a username (null is not supported)"));
      return;
    }
  JNIStringHolder username(jusername);
  if (JNIUtil::isExceptionThrown())
    return;

  cl->username(username);
}

JNIEXPORT void JNICALL
Java_org_tigris_subversion_javahl_SVNClient_password
(JNIEnv *env, jobject jthis, jstring jpassword)
{
  JNIEntry(SVNClient, password);
  SVNClient *cl = SVNClient::getCppObject(jthis);
  if (cl == NULL)
    {
      JNIUtil::throwError(_("bad C++ this"));
      return;
    }
  if (jpassword == NULL)
    {
      JNIUtil::raiseThrowable("java/lang/IllegalArgumentException",
                              _("Provide a password (null is not supported)"));
      return;
    }
  JNIStringHolder password(jpassword);
  if (JNIUtil::isExceptionThrown())
    return;

  cl->password(password);
}

JNIEXPORT void JNICALL
Java_org_tigris_subversion_javahl_SVNClient_setPrompt
(JNIEnv *env, jobject jthis, jobject jprompter)
{
  JNIEntry(SVNClient, setPrompt);
  SVNClient *cl = SVNClient::getCppObject(jthis);
  if (cl == NULL)
    {
      JNIUtil::throwError(_("bad C++ this"));
      return;
    }
  Prompter *prompter = Prompter::makeCPrompter(jprompter);
  if (JNIUtil::isExceptionThrown())
    return;

  cl->setPrompt(prompter);
}

JNIEXPORT void JNICALL
Java_org_tigris_subversion_javahl_SVNClient_logMessages
(JNIEnv *env, jobject jthis, jstring jpath, jobject jpegRevision,
 jobjectArray jranges, jboolean jstopOnCopy,
 jboolean jdisoverPaths, jboolean jincludeMergedRevisions,
 jobjectArray jrevProps, jlong jlimit, jobject jlogMessageCallback)
{
  JNIEntry(SVNClient, logMessages);
  SVNClient *cl = SVNClient::getCppObject(jthis);
  if (cl == NULL)
    {
      JNIUtil::throwError(_("bad C++ this"));
      return;
    }
  Revision pegRevision(jpegRevision, true);
  if (JNIUtil::isExceptionThrown())
    return;

  JNIStringHolder path(jpath);
  if (JNIUtil::isExceptionThrown())
    return;

  LogMessageCallback callback(jlogMessageCallback);

  StringArray revProps(jrevProps);
  if (JNIUtil::isExceptionThrown())
    return;

  // Build the revision range vector from the Java array.
  std::vector<RevisionRange> revisionRanges;

  jint arraySize = env->GetArrayLength(jranges);
  if (JNIUtil::isExceptionThrown())
    return;

  if (JNIUtil::isExceptionThrown())
    return;

  for (int i = 0; i < arraySize; ++i)
    {
      jobject elem = env->GetObjectArrayElement(jranges, i);
      if (JNIUtil::isExceptionThrown())
        return;

      RevisionRange revisionRange(elem);
      if (JNIUtil::isExceptionThrown())
        return;

      revisionRanges.push_back(revisionRange);
    }

  cl->logMessages(path, pegRevision, revisionRanges,
                  jstopOnCopy ? true: false, jdisoverPaths ? true : false,
                  jincludeMergedRevisions ? true : false,
                  revProps, jlimit, &callback);
}

JNIEXPORT jlong JNICALL
Java_org_tigris_subversion_javahl_SVNClient_checkout
(JNIEnv *env, jobject jthis, jstring jmoduleName, jstring jdestPath,
 jobject jrevision, jobject jpegRevision, jint jdepth,
 jboolean jignoreExternals, jboolean jallowUnverObstructions)
{
  JNIEntry(SVNClient, checkout);
  SVNClient *cl = SVNClient::getCppObject(jthis);
  if (cl == NULL)
    {
      JNIUtil::throwError(_("bad C++ this"));
      return -1;
    }
  Revision revision(jrevision, true);
  if (JNIUtil::isExceptionThrown())
    return -1;

  Revision pegRevision(jpegRevision, true);
  if (JNIUtil::isExceptionThrown())
    return -1;

  JNIStringHolder moduleName(jmoduleName);
  if (JNIUtil::isExceptionThrown())
    return -1;

  JNIStringHolder destPath(jdestPath);
  if (JNIUtil::isExceptionThrown())
    return -1;

  return cl->checkout(moduleName, destPath, revision, pegRevision,
                      (svn_depth_t)jdepth,
                      jignoreExternals ? true : false,
                      jallowUnverObstructions ? true : false);
}

JNIEXPORT void JNICALL
Java_org_tigris_subversion_javahl_SVNClient_notification
(JNIEnv *env, jobject jthis, jobject jnotify)
{
  JNIEntry(SVNClient, notification);
  SVNClient *cl = SVNClient::getCppObject(jthis);
  if (cl == NULL)
    {
      JNIUtil::throwError(_("bad C++ this"));
      return;
    }
  Notify *notify = Notify::makeCNotify(jnotify);
  if (JNIUtil::isExceptionThrown())
    return;

  cl->notification(notify);
}

JNIEXPORT void JNICALL
Java_org_tigris_subversion_javahl_SVNClient_notification2
(JNIEnv *env, jobject jthis, jobject jnotify2)
{
  JNIEntry(SVNClient, notification2);
  SVNClient *cl = SVNClient::getCppObject(jthis);
  if (cl == NULL)
    {
      JNIUtil::throwError(_("bad C++ this"));
      return;
    }
  Notify2 *notify2 = Notify2::makeCNotify(jnotify2);
  if (JNIUtil::isExceptionThrown())
    return;

  cl->notification2(notify2);
}

JNIEXPORT void JNICALL
Java_org_tigris_subversion_javahl_SVNClient_setConflictResolver
(JNIEnv *env, jobject jthis, jobject jconflictResolver)
{
  JNIEntry(SVNClient, setConflictResolver);
  SVNClient *cl = SVNClient::getCppObject(jthis);
  if (cl == NULL)
    {
      JNIUtil::throwError(_("bad C++ this"));
      return;
    }
  ConflictResolverCallback *listener =
    ConflictResolverCallback::makeCConflictResolverCallback(jconflictResolver);
  if (JNIUtil::isExceptionThrown())
    return;

  cl->setConflictResolver(listener);
}

JNIEXPORT void JNICALL
Java_org_tigris_subversion_javahl_SVNClient_setProgressListener
(JNIEnv *env, jobject jthis, jobject jprogressListener)
{
  JNIEntry(SVNClient, setProgressListener);
  SVNClient *cl = SVNClient::getCppObject(jthis);
  if (cl == NULL)
    {
      JNIUtil::throwError(_("bad C++ this"));
      return;
    }
  ProgressListener *listener =
    ProgressListener::makeCProgressListener(jprogressListener);
  if (JNIUtil::isExceptionThrown())
    return;

  cl->setProgressListener(listener);
}

JNIEXPORT void JNICALL
Java_org_tigris_subversion_javahl_SVNClient_commitMessageHandler
(JNIEnv *env, jobject jthis, jobject jcommitMessage)
{
  JNIEntry(SVNClient, commitMessageHandler);
  SVNClient *cl = SVNClient::getCppObject(jthis);
  if (cl == NULL)
    {
      JNIUtil::throwError("bad C++ this");
      return;
    }
  CommitMessage *commitMessage =
    CommitMessage::makeCCommitMessage(jcommitMessage);
  if (JNIUtil::isExceptionThrown())
    return;

  cl->commitMessageHandler(commitMessage);
}

JNIEXPORT void JNICALL
Java_org_tigris_subversion_javahl_SVNClient_remove
(JNIEnv *env, jobject jthis, jobjectArray jtargets, jstring jmessage,
 jboolean jforce, jboolean keepLocal, jobject jrevpropTable)
{
  JNIEntry(SVNClient, remove);
  SVNClient *cl = SVNClient::getCppObject(jthis);
  if (cl == NULL)
    {
      JNIUtil::throwError(_("bad C++ this"));
      return;
    }
  Targets targets(jtargets);
  JNIStringHolder message(jmessage);
  if (JNIUtil::isExceptionThrown())
    return;

  RevpropTable revprops(jrevpropTable);
  if (JNIUtil::isExceptionThrown())
    return;

  cl->remove(targets, message, jforce ? true : false,
             keepLocal ? true : false, revprops);
}

JNIEXPORT void JNICALL
Java_org_tigris_subversion_javahl_SVNClient_revert
(JNIEnv *env, jobject jthis, jstring jpath, jint jdepth,
 jobjectArray jchangelists)
{
  JNIEntry(SVNClient, revert);
  SVNClient *cl = SVNClient::getCppObject(jthis);
  if (cl == NULL)
    {
      JNIUtil::throwError(_("bad C++ this"));
      return;
    }
  JNIStringHolder path(jpath);
  if (JNIUtil::isExceptionThrown())
    return;

  StringArray changelists(jchangelists);
  if (JNIUtil::isExceptionThrown())
    return;

  cl->revert(path, (svn_depth_t)jdepth, changelists);
}

JNIEXPORT void JNICALL
Java_org_tigris_subversion_javahl_SVNClient_add
(JNIEnv *env, jobject jthis, jstring jpath, jint jdepth,
 jboolean jforce, jboolean jnoIgnore, jboolean jaddParents)
{
  JNIEntry(SVNClient, add);
  SVNClient *cl = SVNClient::getCppObject(jthis);
  if (cl == NULL)
    {
      JNIUtil::throwError(_("bad C++ this"));
      return;
    }
  JNIStringHolder path(jpath);
  if (JNIUtil::isExceptionThrown())
    return;

  cl->add(path, (svn_depth_t)jdepth, jforce ? true : false,
          jnoIgnore ? true : false, jaddParents ? true : false);
}

JNIEXPORT jlongArray JNICALL
Java_org_tigris_subversion_javahl_SVNClient_update
(JNIEnv *env, jobject jthis, jobjectArray jpath, jobject jrevision,
 jint jdepth, jboolean jdepthIsSticky, jboolean jignoreExternals,
 jboolean jallowUnverObstructions)
{
  JNIEntry(SVNClient, update);
  SVNClient *cl = SVNClient::getCppObject(jthis);
  if (cl == NULL)
    {
      JNIUtil::throwError(_("bad C++ this"));
      return NULL;
    }
  Revision revision(jrevision);
  if (JNIUtil::isExceptionThrown())
    return NULL;

  Targets targets(jpath);
  if (JNIUtil::isExceptionThrown())
    return NULL;

  return cl->update(targets, revision, (svn_depth_t)jdepth,
                    jdepthIsSticky ? true : false,
                    jignoreExternals ? true : false,
                    jallowUnverObstructions ? true : false);
}

JNIEXPORT jlong JNICALL
Java_org_tigris_subversion_javahl_SVNClient_commit
(JNIEnv *env, jobject jthis, jobjectArray jtargets, jstring jmessage,
 jint jdepth, jboolean jnoUnlock, jboolean jkeepChangelist,
 jobjectArray jchangelists, jobject jrevpropTable)
{
  JNIEntry(SVNClient, commit);
  SVNClient *cl = SVNClient::getCppObject(jthis);
  if (cl == NULL)
    {
      JNIUtil::throwError(_("bad C++ this"));
      return -1;
    }
  Targets targets(jtargets);
  JNIStringHolder message(jmessage);
  if (JNIUtil::isExceptionThrown())
    return -1;

  // Build the changelist vector from the Java array.
  StringArray changelists(jchangelists);
  if (JNIUtil::isExceptionThrown())
    return -1;

  RevpropTable revprops(jrevpropTable);
  if (JNIUtil::isExceptionThrown())
    return -1;

  return cl->commit(targets, message, (svn_depth_t)jdepth,
                    jnoUnlock ? true : false, jkeepChangelist ? true : false,
                    changelists, revprops);
}

JNIEXPORT void JNICALL
Java_org_tigris_subversion_javahl_SVNClient_copy
(JNIEnv *env, jobject jthis, jobjectArray jcopySources, jstring jdestPath,
 jstring jmessage, jboolean jcopyAsChild, jboolean jmakeParents,
 jobject jrevpropTable)
{
  JNIEntry(SVNClient, copy);

  SVNClient *cl = SVNClient::getCppObject(jthis);
  if (cl == NULL)
    {
      JNIUtil::throwError(_("bad C++ this"));
      return;
    }
  CopySources copySources(jcopySources);
  if (JNIUtil::isExceptionThrown())
    return;
  JNIStringHolder destPath(jdestPath);
  if (JNIUtil::isExceptionThrown())
    return;
  JNIStringHolder message(jmessage);
  if (JNIUtil::isExceptionThrown())
    return;

  RevpropTable revprops(jrevpropTable);
  if (JNIUtil::isExceptionThrown())
    return;

  cl->copy(copySources, destPath, message, jcopyAsChild ? true : false,
           jmakeParents ? true : false, revprops);
}

JNIEXPORT void JNICALL
Java_org_tigris_subversion_javahl_SVNClient_move
(JNIEnv *env, jobject jthis, jobjectArray jsrcPaths, jstring jdestPath,
 jstring jmessage, jboolean jforce, jboolean jmoveAsChild,
 jboolean jmakeParents, jobject jrevpropTable)
{
  JNIEntry(SVNClient, move);

  SVNClient *cl = SVNClient::getCppObject(jthis);
  if (cl == NULL)
    {
      JNIUtil::throwError(_("bad C++ this"));
      return;
    }
  Targets srcPaths(jsrcPaths);
  if (JNIUtil::isExceptionThrown())
    return;
  JNIStringHolder destPath(jdestPath);
  if (JNIUtil::isExceptionThrown())
    return;
  JNIStringHolder message(jmessage);
  if (JNIUtil::isExceptionThrown())
    return;

  RevpropTable revprops(jrevpropTable);
  if (JNIUtil::isExceptionThrown())
    return;

  cl->move(srcPaths, destPath, message, jforce ? true : false,
           jmoveAsChild ? true : false, jmakeParents ? true : false,
           revprops);
}

JNIEXPORT void JNICALL
Java_org_tigris_subversion_javahl_SVNClient_mkdir
(JNIEnv *env, jobject jthis, jobjectArray jtargets, jstring jmessage,
 jboolean jmakeParents, jobject jrevpropTable)
{
  JNIEntry(SVNClient, mkdir);
  SVNClient *cl = SVNClient::getCppObject(jthis);
  if (cl == NULL)
    {
      JNIUtil::throwError(_("bad C++ this"));
      return;
    }
  Targets targets(jtargets);
  JNIStringHolder message(jmessage);
  if (JNIUtil::isExceptionThrown())
    return;

  RevpropTable revprops(jrevpropTable);
  if (JNIUtil::isExceptionThrown())
    return;

  cl->mkdir(targets, message, jmakeParents ? true : false, revprops);
}

JNIEXPORT void JNICALL
Java_org_tigris_subversion_javahl_SVNClient_cleanup
(JNIEnv *env, jobject jthis, jstring jpath)
{
  JNIEntry(SVNClient, cleanup);
  SVNClient *cl = SVNClient::getCppObject(jthis);
  if (cl == NULL)
    {
      JNIUtil::throwError(_("bad C++ this"));
      return;
    }
  JNIStringHolder path(jpath);
  if (JNIUtil::isExceptionThrown())
    return;

  cl->cleanup(path);
}

JNIEXPORT void JNICALL
Java_org_tigris_subversion_javahl_SVNClient_resolve
(JNIEnv *env, jobject jthis, jstring jpath, jint jdepth, jint jchoice)
{
  JNIEntry(SVNClient, resolve);
  SVNClient *cl = SVNClient::getCppObject(jthis);
  if (cl == NULL)
    {
      JNIUtil::throwError(_("bad C++ this"));
      return;
    }
  JNIStringHolder path(jpath);
  if (JNIUtil::isExceptionThrown())
    return;

  cl->resolve(path, (svn_depth_t) jdepth, (svn_wc_conflict_choice_t) jchoice);
}

JNIEXPORT jlong JNICALL
Java_org_tigris_subversion_javahl_SVNClient_doExport
(JNIEnv *env, jobject jthis, jstring jsrcPath, jstring jdestPath,
 jobject jrevision, jobject jpegRevision, jboolean jforce,
 jboolean jignoreExternals, jint jdepth, jstring jnativeEOL)
{
  JNIEntry(SVNClient, doExport);
  SVNClient *cl = SVNClient::getCppObject(jthis);
  if (cl == NULL)
    {
      JNIUtil::throwError(_("bad C++ this"));
      return -1;
    }
  Revision revision(jrevision);
  if (JNIUtil::isExceptionThrown())
    return -1;

  Revision pegRevision(jpegRevision);
  if (JNIUtil::isExceptionThrown())
    return -1;

  JNIStringHolder srcPath(jsrcPath);
  if (JNIUtil::isExceptionThrown())
    return -1;

  JNIStringHolder destPath(jdestPath);
  if (JNIUtil::isExceptionThrown())
    return -1;

  JNIStringHolder nativeEOL(jnativeEOL);
  if (JNIUtil::isExceptionThrown())
    return -1;

  return cl->doExport(srcPath, destPath, revision, pegRevision,
                      jforce ? true : false, jignoreExternals ? true : false,
                      (svn_depth_t)jdepth, nativeEOL);
}

JNIEXPORT jlong JNICALL
Java_org_tigris_subversion_javahl_SVNClient_doSwitch
(JNIEnv *env, jobject jthis, jstring jpath, jstring jurl, jobject jrevision,
 jobject jPegRevision, jint jdepth, jboolean jdepthIsSticky,
 jboolean jignoreExternals, jboolean jallowUnverObstructions)
{
  JNIEntry(SVNClient, doSwitch);
  SVNClient *cl = SVNClient::getCppObject(jthis);
  if (cl == NULL)
    {
      JNIUtil::throwError(_("bad C++ this"));
      return -1;
    }
  Revision revision(jrevision);
  if (JNIUtil::isExceptionThrown())
    return -1;

  Revision pegRevision(jPegRevision);
  if (JNIUtil::isExceptionThrown())
    return -1;

  JNIStringHolder path(jpath);
  if (JNIUtil::isExceptionThrown())
    return -1;

  JNIStringHolder url(jurl);
  if (JNIUtil::isExceptionThrown())
    return -1;

  return cl->doSwitch(path, url, revision, pegRevision, (svn_depth_t) jdepth,
                      jdepthIsSticky ? true : false,
                      jignoreExternals ? true : false,
                      jallowUnverObstructions ? true : false);
}

JNIEXPORT void JNICALL
Java_org_tigris_subversion_javahl_SVNClient_doImport
(JNIEnv *env, jobject jthis, jstring jpath, jstring jurl, jstring jmessage,
 jint jdepth, jboolean jnoIgnore, jboolean jignoreUnknownNodeTypes,
 jobject jrevpropTable)
{
  JNIEntry(SVNClient, doImport);
  SVNClient *cl = SVNClient::getCppObject(jthis);
  if (cl == NULL)
    {
      JNIUtil::throwError(_("bad C++ this"));
      return;
    }
  JNIStringHolder path(jpath);
  if (JNIUtil::isExceptionThrown())
    return;

  JNIStringHolder url(jurl);
  if (JNIUtil::isExceptionThrown())
    return;

  JNIStringHolder message(jmessage);
  if (JNIUtil::isExceptionThrown())
    return;

  RevpropTable revprops(jrevpropTable);
  if (JNIUtil::isExceptionThrown())
    return;

  cl->doImport(path, url, message, (svn_depth_t)jdepth,
               jnoIgnore ? true : false,
               jignoreUnknownNodeTypes ? true : false, revprops);
}

JNIEXPORT jobjectArray JNICALL
Java_org_tigris_subversion_javahl_SVNClient_suggestMergeSources
(JNIEnv *env, jobject jthis, jstring jpath, jobject jpegRevision)
{
  JNIEntry(SVNClient, suggestMergeSources);
  SVNClient *cl = SVNClient::getCppObject(jthis);
  if (cl == NULL)
    {
      JNIUtil::throwError(_("bad C++ this"));
      return NULL;
    }

  JNIStringHolder path(jpath);
  if (JNIUtil::isExceptionThrown())
    return NULL;

  Revision pegRevision(jpegRevision);
  if (JNIUtil::isExceptionThrown())
    return NULL;

  return cl->suggestMergeSources(path, pegRevision);
}

JNIEXPORT void JNICALL
Java_org_tigris_subversion_javahl_SVNClient_merge__Ljava_lang_String_2Lorg_tigris_subversion_javahl_Revision_2Ljava_lang_String_2Lorg_tigris_subversion_javahl_Revision_2Ljava_lang_String_2ZIZZZ
(JNIEnv *env, jobject jthis, jstring jpath1, jobject jrevision1,
 jstring jpath2, jobject jrevision2, jstring jlocalPath, jboolean jforce,
 jint jdepth, jboolean jignoreAncestry, jboolean jdryRun, jboolean jrecordOnly)
{
  JNIEntry(SVNClient, merge);
  SVNClient *cl = SVNClient::getCppObject(jthis);
  if (cl == NULL)
    {
      JNIUtil::throwError(_("bad C++ this"));
      return;
    }
  Revision revision1(jrevision1);
  if (JNIUtil::isExceptionThrown())
    return;

  JNIStringHolder path1(jpath1);
  if (JNIUtil::isExceptionThrown())
    return;

  Revision revision2(jrevision2);
  if (JNIUtil::isExceptionThrown())
    return;

  JNIStringHolder path2(jpath2);
  if (JNIUtil::isExceptionThrown())
    return;

  JNIStringHolder localPath(jlocalPath);
  if (JNIUtil::isExceptionThrown())
    return;

  cl->merge(path1, revision1, path2, revision2, localPath,
            jforce ? true:false, (svn_depth_t)jdepth,
            jignoreAncestry ? true:false, jdryRun ? true:false,
            jrecordOnly ? true:false);
}

JNIEXPORT void JNICALL
Java_org_tigris_subversion_javahl_SVNClient_merge__Ljava_lang_String_2Lorg_tigris_subversion_javahl_Revision_2_3Lorg_tigris_subversion_javahl_RevisionRange_2Ljava_lang_String_2ZIZZZ
(JNIEnv *env, jobject jthis, jstring jpath, jobject jpegRevision,
 jobjectArray jranges, jstring jlocalPath, jboolean jforce,
 jint jdepth, jboolean jignoreAncestry, jboolean jdryRun, jboolean jrecordOnly)
{
  JNIEntry(SVNClient, merge);
  SVNClient *cl = SVNClient::getCppObject(jthis);
  if (cl == NULL)
    {
      JNIUtil::throwError(_("bad C++ this"));
      return;
    }

  JNIStringHolder path(jpath);
  if (JNIUtil::isExceptionThrown())
    return;

  Revision pegRevision(jpegRevision);
  if (JNIUtil::isExceptionThrown())
    return;

  JNIStringHolder localPath(jlocalPath);
  if (JNIUtil::isExceptionThrown())
    return;

  // Build the revision range vector from the Java array.
  std::vector<RevisionRange> revisionRanges;

  jint arraySize = env->GetArrayLength(jranges);
  if (JNIUtil::isExceptionThrown())
    return;

  if (JNIUtil::isExceptionThrown())
    return;

  for (int i = 0; i < arraySize; ++i)
    {
      jobject elem = env->GetObjectArrayElement(jranges, i);
      if (JNIUtil::isExceptionThrown())
        return;

      RevisionRange revisionRange(elem);
      if (JNIUtil::isExceptionThrown())
        return;

      revisionRanges.push_back(revisionRange);
    }

  cl->merge(path, pegRevision, revisionRanges, localPath,
            jforce ? true:false, (svn_depth_t)jdepth,
            jignoreAncestry ? true:false, jdryRun ? true:false,
            jrecordOnly ? true:false);
}

JNIEXPORT void JNICALL
Java_org_tigris_subversion_javahl_SVNClient_mergeReintegrate
(JNIEnv *env, jobject jthis, jstring jpath, jobject jpegRevision,
 jstring jlocalPath, jboolean jdryRun)
{
  JNIEntry(SVNClient, mergeReintegrate);
  SVNClient *cl = SVNClient::getCppObject(jthis);
  if (cl == NULL)
    {
      JNIUtil::throwError(_("bad C++ this"));
      return;
    }

  JNIStringHolder path(jpath);
  if (JNIUtil::isExceptionThrown())
    return;

  Revision pegRevision(jpegRevision);
  if (JNIUtil::isExceptionThrown())
    return;

  JNIStringHolder localPath(jlocalPath);
  if (JNIUtil::isExceptionThrown())
    return;

  cl->mergeReintegrate(path, pegRevision, localPath,
                       jdryRun ? true:false);
}

JNIEXPORT void JNICALL
Java_org_tigris_subversion_javahl_SVNClient_properties
(JNIEnv *env, jobject jthis, jstring jpath, jobject jrevision,
 jobject jpegRevision, jint jdepth, jobjectArray jchangelists,
 jobject jproplistCallback)
{
  JNIEntry(SVNClient, properties);
  SVNClient *cl = SVNClient::getCppObject(jthis);
  if (cl == NULL)
    {
      JNIUtil::throwError(_("bad C++ this"));
      return;
    }
  JNIStringHolder path(jpath);
  if (JNIUtil::isExceptionThrown())
    return;

  Revision revision(jrevision);
  if (JNIUtil::isExceptionThrown())
    return;

  Revision pegRevision(jpegRevision);
  if (JNIUtil::isExceptionThrown())
    return;

  StringArray changelists(jchangelists);
  if (JNIUtil::isExceptionThrown())
    return;

  ProplistCallback callback(jproplistCallback);
  cl->properties(path, revision, pegRevision, (svn_depth_t)jdepth,
                 changelists, &callback);
}

JNIEXPORT void JNICALL
Java_org_tigris_subversion_javahl_SVNClient_propertySet
(JNIEnv *env, jobject jthis, jstring jpath, jstring jname, jstring jvalue,
 jint jdepth, jobjectArray jchangelists, jboolean jforce,
 jobject jrevpropTable)
{
  JNIEntry(SVNClient, propertySet);
  SVNClient *cl = SVNClient::getCppObject(jthis);
  if (cl == NULL)
    {
      JNIUtil::throwError(_("bad C++ this"));
      return;
    }
  JNIStringHolder path(jpath);
  if (JNIUtil::isExceptionThrown())
    return;

  JNIStringHolder name(jname);
  if (JNIUtil::isExceptionThrown())
    return;

  JNIStringHolder value(jvalue);
  if (JNIUtil::isExceptionThrown())
    return;

  StringArray changelists(jchangelists);
  if (JNIUtil::isExceptionThrown())
    return;

  RevpropTable revprops(jrevpropTable);
  if (JNIUtil::isExceptionThrown())
    return;

  cl->propertySet(path, name, value, (svn_depth_t)jdepth, changelists,
                  jforce ? true:false, revprops);
}

JNIEXPORT jobject JNICALL
Java_org_tigris_subversion_javahl_SVNClient_revProperty
(JNIEnv *env, jobject jthis, jstring jpath, jstring jname, jobject jrevision)
{
  JNIEntry(SVNClient, revProperty);
  SVNClient *cl = SVNClient::getCppObject(jthis);
  if (cl == NULL)
    {
      JNIUtil::throwError(_("bad C++ this"));
      return NULL;
    }
  JNIStringHolder path(jpath);
  if (JNIUtil::isExceptionThrown())
    return NULL;

  JNIStringHolder name(jname);
  if (JNIUtil::isExceptionThrown())
    return NULL;

  Revision revision(jrevision);
  if (JNIUtil::isExceptionThrown())
    return NULL;

  return cl->revProperty(jthis, path, name, revision);
}

JNIEXPORT jobjectArray JNICALL
Java_org_tigris_subversion_javahl_SVNClient_revProperties
(JNIEnv *env, jobject jthis, jstring jpath, jobject jrevision)
{
  JNIEntry(SVNClient, revProperty);
  SVNClient *cl = SVNClient::getCppObject(jthis);
  if (cl == NULL)
    {
      JNIUtil::throwError(_("bad C++ this"));
      return NULL;
    }
  JNIStringHolder path(jpath);
  if (JNIUtil::isExceptionThrown())
    return NULL;

  Revision revision(jrevision);
  if (JNIUtil::isExceptionThrown())
    return NULL;

  return cl->revProperties(jthis, path, revision);
}

JNIEXPORT void JNICALL
Java_org_tigris_subversion_javahl_SVNClient_setRevProperty
(JNIEnv *env, jobject jthis, jstring jpath, jstring jname, jobject jrevision,
 jstring jvalue, jstring joriginalValue, jboolean jforce)
{
  JNIEntry(SVNClient, setRevProperty);
  SVNClient *cl = SVNClient::getCppObject(jthis);
  if (cl == NULL)
    {
      JNIUtil::throwError(_("bad C++ this"));
      return;
    }
  JNIStringHolder path(jpath);
  if (JNIUtil::isExceptionThrown())
    return;

  JNIStringHolder name(jname);
  if (JNIUtil::isExceptionThrown())
    return;

  Revision revision(jrevision);
  if (JNIUtil::isExceptionThrown())
    return;

  JNIStringHolder value(jvalue);
  if (JNIUtil::isExceptionThrown())
    return;

  JNIStringHolder original_value(joriginalValue);
  if (JNIUtil::isExceptionThrown())
    return;

  cl->setRevProperty(jthis, path, name, revision, value, original_value,
                     jforce ? true: false);
}

JNIEXPORT jobject JNICALL
Java_org_tigris_subversion_javahl_SVNClient_propertyGet
(JNIEnv *env, jobject jthis, jstring jpath, jstring jname, jobject jrevision,
 jobject jpegRevision)
{
  JNIEntry(SVNClient, propertyGet);
  SVNClient *cl = SVNClient::getCppObject(jthis);
  if (cl == NULL)
    {
      JNIUtil::throwError(_("bad C++ this"));
      return NULL;
    }
  JNIStringHolder path(jpath);
  if (JNIUtil::isExceptionThrown())
    return NULL;

  JNIStringHolder name(jname);
  if (JNIUtil::isExceptionThrown())
    return NULL;

  Revision revision(jrevision);
  if (JNIUtil::isExceptionThrown())
    return NULL;

  Revision pegRevision(jpegRevision);
  if (JNIUtil::isExceptionThrown())
    return NULL;

  return cl->propertyGet(jthis, path, name, revision, pegRevision);
}

JNIEXPORT jobject JNICALL
Java_org_tigris_subversion_javahl_SVNClient_getMergeinfo
(JNIEnv *env, jobject jthis, jstring jtarget, jobject jpegRevision)
{
  JNIEntry(SVNClient, getMergeinfo);
  SVNClient *cl = SVNClient::getCppObject(jthis);
  if (cl == NULL)
    {
      JNIUtil::throwError(_("bad C++ this"));
      return NULL;
    }
  JNIStringHolder target(jtarget);
  if (JNIUtil::isExceptionThrown())
    return NULL;
  Revision pegRevision(jpegRevision);
  if (JNIUtil::isExceptionThrown())
    return NULL;
  return cl->getMergeinfo(target, pegRevision);
}

JNIEXPORT void JNICALL Java_org_tigris_subversion_javahl_SVNClient_getMergeinfoLog
(JNIEnv *env, jobject jthis, jint jkind, jstring jpathOrUrl,
 jobject jpegRevision, jstring jmergeSourceUrl, jobject jsrcPegRevision,
 jboolean jdiscoverChangedPaths, jobjectArray jrevProps,
 jobject jlogMessageCallback)
{
  JNIEntry(SVNClient, getMergeinfoLog);
  SVNClient *cl = SVNClient::getCppObject(jthis);
  if (cl == NULL)
    {
      JNIUtil::throwError(_("bad C++ this"));
      return;
    }

  Revision pegRevision(jpegRevision, true);
  if (JNIUtil::isExceptionThrown())
    return;

  Revision srcPegRevision(jsrcPegRevision, true);
  if (JNIUtil::isExceptionThrown())
    return;

  JNIStringHolder pathOrUrl(jpathOrUrl);
  if (JNIUtil::isExceptionThrown())
    return;

  JNIStringHolder mergeSourceUrl(jmergeSourceUrl);
  if (JNIUtil::isExceptionThrown())
    return;

  StringArray revProps(jrevProps);
  if (JNIUtil::isExceptionThrown())
    return;

  LogMessageCallback callback(jlogMessageCallback);

  cl->getMergeinfoLog((int)jkind, pathOrUrl, pegRevision, mergeSourceUrl,
                      srcPegRevision, jdiscoverChangedPaths ? true : false,
                      revProps, &callback);
}

JNIEXPORT void JNICALL
Java_org_tigris_subversion_javahl_SVNClient_diff__Ljava_lang_String_2Lorg_tigris_subversion_javahl_Revision_2Ljava_lang_String_2Lorg_tigris_subversion_javahl_Revision_2Ljava_lang_String_2Ljava_lang_String_2I_3Ljava_lang_String_2ZZZ
(JNIEnv *env, jobject jthis, jstring jtarget1, jobject jrevision1,
 jstring jtarget2, jobject jrevision2, jstring jrelativeToDir,
 jstring joutfileName, jint jdepth, jobjectArray jchangelists,
 jboolean jignoreAncestry, jboolean jnoDiffDeleted, jboolean jforce)
{
  JNIEntry(SVNClient, diff);
  SVNClient *cl = SVNClient::getCppObject(jthis);
  if (cl == NULL)
    {
      JNIUtil::throwError(_("bad C++ this"));
      return;
    }
  JNIStringHolder target1(jtarget1);
  if (JNIUtil::isExceptionThrown())
    return;

  Revision revision1(jrevision1);
  if (JNIUtil::isExceptionThrown())
    return;

  JNIStringHolder target2(jtarget2);
  if (JNIUtil::isExceptionThrown())
    return;

  Revision revision2(jrevision2);
  if (JNIUtil::isExceptionThrown())
    return;

  JNIStringHolder relativeToDir(jrelativeToDir);
  if (JNIUtil::isExceptionThrown())
    return;

  JNIStringHolder outfileName(joutfileName);
  if (JNIUtil::isExceptionThrown())
    return;

  StringArray changelists(jchangelists);
  if (JNIUtil::isExceptionThrown())
    return;

  cl->diff(target1, revision1, target2, revision2, relativeToDir, outfileName,
           (svn_depth_t)jdepth, changelists,
           jignoreAncestry ? true:false,
           jnoDiffDeleted ? true:false, jforce ? true:false);
}

JNIEXPORT void JNICALL
Java_org_tigris_subversion_javahl_SVNClient_diff__Ljava_lang_String_2Lorg_tigris_subversion_javahl_Revision_2Lorg_tigris_subversion_javahl_Revision_2Lorg_tigris_subversion_javahl_Revision_2Ljava_lang_String_2Ljava_lang_String_2I_3Ljava_lang_String_2ZZZ
(JNIEnv *env, jobject jthis, jstring jtarget, jobject jpegRevision,
 jobject jstartRevision, jobject jendRevision, jstring jrelativeToDir,
 jstring joutfileName, jint jdepth, jobjectArray jchangelists,
 jboolean jignoreAncestry, jboolean jnoDiffDeleted, jboolean jforce)
{
  JNIEntry(SVNClient, diff);
  SVNClient *cl = SVNClient::getCppObject(jthis);
  if (cl == NULL)
    {
      JNIUtil::throwError(_("bad C++ this"));
      return;
    }
  JNIStringHolder target(jtarget);
  if (JNIUtil::isExceptionThrown())
    return;

  Revision pegRevision(jpegRevision);
  if (JNIUtil::isExceptionThrown())
    return;

  Revision startRevision(jstartRevision);
  if (JNIUtil::isExceptionThrown())
    return;

  Revision endRevision(jendRevision);
  if (JNIUtil::isExceptionThrown())
    return;

  JNIStringHolder outfileName(joutfileName);
  if (JNIUtil::isExceptionThrown())
    return;

  JNIStringHolder relativeToDir(jrelativeToDir);
  if (JNIUtil::isExceptionThrown())
    return;

  StringArray changelists(jchangelists);
  if (JNIUtil::isExceptionThrown())
    return;

  cl->diff(target, pegRevision, startRevision, endRevision, relativeToDir,
           outfileName, (svn_depth_t) jdepth, changelists,
           jignoreAncestry ? true:false,
           jnoDiffDeleted ? true:false, jforce ? true:false);
}

JNIEXPORT void JNICALL
Java_org_tigris_subversion_javahl_SVNClient_diffSummarize__Ljava_lang_String_2Lorg_tigris_subversion_javahl_Revision_2Ljava_lang_String_2Lorg_tigris_subversion_javahl_Revision_2I_3Ljava_lang_String_2ZLorg_tigris_subversion_javahl_DiffSummaryReceiver_2
(JNIEnv *env, jobject jthis, jstring jtarget1, jobject jrevision1,
 jstring jtarget2, jobject jrevision2, jint jdepth, jobjectArray jchangelists,
 jboolean jignoreAncestry, jobject jdiffSummaryReceiver)
{
  JNIEntry(SVNClient, diffSummarize);

  SVNClient *cl = SVNClient::getCppObject(jthis);
  if (cl == NULL)
    {
      JNIUtil::throwError(_("bad C++ this"));
      return;
    }
  JNIStringHolder target1(jtarget1);
  if (JNIUtil::isExceptionThrown())
    return;

  Revision revision1(jrevision1);
  if (JNIUtil::isExceptionThrown())
    return;

  JNIStringHolder target2(jtarget2);
  if (JNIUtil::isExceptionThrown())
    return;

  Revision revision2(jrevision2);
  if (JNIUtil::isExceptionThrown())
    return;

  DiffSummaryReceiver receiver(jdiffSummaryReceiver);
  if (JNIUtil::isExceptionThrown())
    return;

  StringArray changelists(jchangelists);
  if (JNIUtil::isExceptionThrown())
    return;

  cl->diffSummarize(target1, revision1, target2, revision2,
                    (svn_depth_t)jdepth, changelists,
                    jignoreAncestry ? true: false, receiver);
}

JNIEXPORT void JNICALL
Java_org_tigris_subversion_javahl_SVNClient_diffSummarize__Ljava_lang_String_2Lorg_tigris_subversion_javahl_Revision_2Lorg_tigris_subversion_javahl_Revision_2Lorg_tigris_subversion_javahl_Revision_2I_3Ljava_lang_String_2ZLorg_tigris_subversion_javahl_DiffSummaryReceiver_2
(JNIEnv *env, jobject jthis, jstring jtarget, jobject jPegRevision,
 jobject jStartRevision, jobject jEndRevision, jint jdepth,
 jobjectArray jchangelists, jboolean jignoreAncestry,
 jobject jdiffSummaryReceiver)
{
  JNIEntry(SVNClient, diffSummarize);

  SVNClient *cl = SVNClient::getCppObject(jthis);
  if (cl == NULL)
    {
      JNIUtil::throwError(_("bad C++ this"));
      return;
    }
  JNIStringHolder target(jtarget);
  if (JNIUtil::isExceptionThrown())
    return;
  Revision pegRevision(jPegRevision);
  if (JNIUtil::isExceptionThrown())
    return;
  Revision startRevision(jStartRevision);
  if (JNIUtil::isExceptionThrown())
    return;
  Revision endRevision(jEndRevision);
  if (JNIUtil::isExceptionThrown())
    return;
  DiffSummaryReceiver receiver(jdiffSummaryReceiver);
  if (JNIUtil::isExceptionThrown())
    return;

  StringArray changelists(jchangelists);
  if (JNIUtil::isExceptionThrown())
    return;

  cl->diffSummarize(target, pegRevision, startRevision, endRevision,
                    (svn_depth_t)jdepth, changelists,
                    jignoreAncestry ? true : false, receiver);
}

JNIEXPORT jbyteArray JNICALL
Java_org_tigris_subversion_javahl_SVNClient_fileContent
(JNIEnv *env, jobject jthis, jstring jpath, jobject jrevision,
 jobject jpegRevision)
{
  JNIEntry(SVNClient, fileContent);
  SVNClient *cl = SVNClient::getCppObject(jthis);
  if (cl == NULL)
    {
      JNIUtil::throwError(_("bad C++ this"));
      return NULL;
    }
  JNIStringHolder path(jpath);
  if (JNIUtil::isExceptionThrown())
    return NULL;

  Revision revision(jrevision);
  if (JNIUtil::isExceptionThrown())
    return NULL;

  Revision pegRevision(jpegRevision);
  if (JNIUtil::isExceptionThrown())
    return NULL;

  return cl->fileContent(path, revision, pegRevision);
}

JNIEXPORT void JNICALL
Java_org_tigris_subversion_javahl_SVNClient_streamFileContent
(JNIEnv *env, jobject jthis, jstring jpath, jobject jrevision,
 jobject jpegRevision, jint bufSize, jobject jstream)
{
  JNIEntry(SVNClient, streamFileContent);
  SVNClient *cl = SVNClient::getCppObject(jthis);
  if (cl == NULL)
    {
      JNIUtil::throwError(_("bad C++ this"));
      return;
    }
  JNIStringHolder path(jpath);
  if (JNIUtil::isExceptionThrown())
    return;

  Revision revision(jrevision);
  if (JNIUtil::isExceptionThrown())
    return;

  Revision pegRevision(jpegRevision);
  if (JNIUtil::isExceptionThrown())
    return;

  cl->streamFileContent(path, revision, pegRevision, jstream, bufSize);
}

JNIEXPORT jstring JNICALL
Java_org_tigris_subversion_javahl_SVNClient_getVersionInfo
(JNIEnv *env, jobject jthis, jstring jpath, jstring jtrailUrl,
 jboolean jlastChanged)
{
  JNIEntry(SVNClient, getVersionInfo);
  SVNClient *cl = SVNClient::getCppObject(jthis);
  if (cl == NULL)
    {
      JNIUtil::throwError(_("bad C++ this"));
      return NULL;
    }
  JNIStringHolder path(jpath);
  if (JNIUtil::isExceptionThrown())
    return NULL;

  JNIStringHolder trailUrl(jtrailUrl);
  return cl->getVersionInfo(path, trailUrl, jlastChanged ? true:false);
}

JNIEXPORT void JNICALL
Java_org_tigris_subversion_javahl_SVNClient_enableLogging
(JNIEnv *env, jclass jclazz, jint jlogLevel, jstring jpath)
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

JNIEXPORT jstring JNICALL
Java_org_tigris_subversion_javahl_SVNClient_version
(JNIEnv *env, jclass jclazz)
{
  JNIEntryStatic(SVNClient, version);
  const char *version = "svn:" SVN_VERSION "\njni:" JNI_VERSION;
  return JNIUtil::makeJString(version);
}

JNIEXPORT jint JNICALL
Java_org_tigris_subversion_javahl_SVNClient_versionMajor
(JNIEnv *env, jclass jclazz)
{
  JNIEntryStatic(SVNClient, versionMajor);
  return JNI_VER_MAJOR;
}

JNIEXPORT jint JNICALL
Java_org_tigris_subversion_javahl_SVNClient_versionMinor
(JNIEnv *env, jclass jclazz)
{
  JNIEntryStatic(SVNClient, versionMinor);
  return JNI_VER_MINOR;
}

JNIEXPORT jint JNICALL
Java_org_tigris_subversion_javahl_SVNClient_versionMicro
(JNIEnv *env, jclass jclazz)
{
  JNIEntryStatic(SVNClient, versionMicro);
  return JNI_VER_MICRO;
}

JNIEXPORT void JNICALL
Java_org_tigris_subversion_javahl_SVNClient_relocate
(JNIEnv *env, jobject jthis, jstring jfrom, jstring jto, jstring jpath,
 jboolean jrecurse)
{
  JNIEntry(SVNClient, relocate);
  SVNClient *cl = SVNClient::getCppObject(jthis);
  if (cl == NULL)
    {
      JNIUtil::throwError(_("bad C++ this"));
      return;
    }
  JNIStringHolder from(jfrom);
  if (JNIUtil::isExceptionThrown())
    return;

  JNIStringHolder to(jto);
  if (JNIUtil::isExceptionThrown())
    return;

  JNIStringHolder path(jpath);
  if (JNIUtil::isExceptionThrown())
    return;

  cl->relocate(from, to, path, jrecurse ? true: false);
  return;
}

JNIEXPORT void JNICALL
Java_org_tigris_subversion_javahl_SVNClient_blame
(JNIEnv *env, jobject jthis, jstring jpath, jobject jpegRevision,
 jobject jrevisionStart, jobject jrevisionEnd, jboolean jignoreMimeType,
 jboolean jincludeMergedRevisions, jobject jblameCallback)
{
  JNIEntry(SVNClient, blame);
  SVNClient *cl = SVNClient::getCppObject(jthis);
  if (cl == NULL)
    {
      JNIUtil::throwError(_("bad C++ this"));
      return;
    }
  JNIStringHolder path(jpath);
  if (JNIUtil::isExceptionThrown())
    return;

  Revision pegRevision(jpegRevision, false, true);
  if (JNIUtil::isExceptionThrown())
    return;

  Revision revisionStart(jrevisionStart, false, true);
  if (JNIUtil::isExceptionThrown())
    return;

  Revision revisionEnd(jrevisionEnd, true);
  if (JNIUtil::isExceptionThrown())
    return;

  BlameCallback callback(jblameCallback);
  cl->blame(path, pegRevision, revisionStart, revisionEnd,
            jignoreMimeType ? true : false,
            jincludeMergedRevisions ? true : false, &callback);
}

JNIEXPORT void JNICALL
Java_org_tigris_subversion_javahl_SVNClient_setConfigDirectory
(JNIEnv *env, jobject jthis, jstring jconfigDir)
{
  JNIEntry(SVNClient, setConfigDirectory);
  SVNClient *cl = SVNClient::getCppObject(jthis);
  if (cl == NULL)
    {
      JNIUtil::throwError(_("bad C++ this"));
      return;
    }

  JNIStringHolder configDir(jconfigDir);
  if (JNIUtil::isExceptionThrown())
    return;

  cl->setConfigDirectory(configDir);
}

JNIEXPORT jstring JNICALL
Java_org_tigris_subversion_javahl_SVNClient_getConfigDirectory
(JNIEnv *env, jobject jthis)
{
  JNIEntry(SVNClient, getConfigDirectory);
  SVNClient *cl = SVNClient::getCppObject(jthis);
  if (cl == NULL)
    {
      JNIUtil::throwError(_("bad C++ this"));
      return NULL;
    }

  const char *configDir = cl->getConfigDirectory();
  return JNIUtil::makeJString(configDir);
}

JNIEXPORT void JNICALL
Java_org_tigris_subversion_javahl_SVNClient_cancelOperation
(JNIEnv *env, jobject jthis)
{
  JNIEntry(SVNClient, cancelOperation);
  SVNClient *cl = SVNClient::getCppObject(jthis);
  if (cl == NULL)
    {
      JNIUtil::throwError("bad C++ this");
      return;
    }
  cl->cancelOperation();
}

JNIEXPORT jobject JNICALL
Java_org_tigris_subversion_javahl_SVNClient_info
(JNIEnv *env, jobject jthis, jstring jpath)
{
  JNIEntry(SVNClient, info);
  SVNClient *cl = SVNClient::getCppObject(jthis);
  if (cl == NULL)
    {
      JNIUtil::throwError("bad C++ this");
      return NULL;
    }

  JNIStringHolder path(jpath);
  if (JNIUtil::isExceptionThrown())
    return NULL;

  return cl->info(path);
}

JNIEXPORT void JNICALL
Java_org_tigris_subversion_javahl_SVNClient_addToChangelist
(JNIEnv *env, jobject jthis, jobjectArray jtargets, jstring jchangelist,
 jint jdepth, jobjectArray jchangelists)
{
  JNIEntry(SVNClient, addToChangelist);
  SVNClient *cl = SVNClient::getCppObject(jthis);
  if (cl == NULL)
    {
      JNIUtil::throwError("bad C++ this");
      return;
    }
  Targets targets(jtargets);
  if (JNIUtil::isExceptionThrown())
    return;

  JNIStringHolder changelist_name(jchangelist);
  if (JNIUtil::isExceptionThrown())
    return;

  StringArray changelists(jchangelists);
  if (JNIUtil::isExceptionThrown())
    return;

  cl->addToChangelist(targets, changelist_name, (svn_depth_t) jdepth,
                      changelists);
}

JNIEXPORT void JNICALL
Java_org_tigris_subversion_javahl_SVNClient_removeFromChangelists
(JNIEnv *env, jobject jthis, jobjectArray jtargets, jint jdepth,
 jobjectArray jchangelists)
{
  JNIEntry(SVNClient, removeFromChangelist);
  SVNClient *cl = SVNClient::getCppObject(jthis);
  if (cl == NULL)
    {
      JNIUtil::throwError("bad C++ this");
      return;
    }
  Targets targets(jtargets);
  if (JNIUtil::isExceptionThrown())
    return;

  StringArray changelists(jchangelists);
  if (JNIUtil::isExceptionThrown())
    return;

  cl->removeFromChangelists(targets, (svn_depth_t)jdepth, changelists);
}

JNIEXPORT void JNICALL
Java_org_tigris_subversion_javahl_SVNClient_getChangelists
(JNIEnv *env, jobject jthis, jstring jroot_path, jobjectArray jchangelists,
 jint jdepth, jobject jchangelistCallback)
{
  JNIEntry(SVNClient, getChangelist);
  SVNClient *cl = SVNClient::getCppObject(jthis);
  if (cl == NULL)
    {
      JNIUtil::throwError("bad C++ this");
      return;
    }

  JNIStringHolder root_path(jroot_path);
  if (JNIUtil::isExceptionThrown())
    return;

  StringArray changelists(jchangelists);
  if (JNIUtil::isExceptionThrown())
    return;

  ChangelistCallback callback(jchangelistCallback);
  cl->getChangelists(root_path, changelists, (svn_depth_t) jdepth, &callback);
}

JNIEXPORT void JNICALL
Java_org_tigris_subversion_javahl_SVNClient_lock
(JNIEnv *env, jobject jthis, jobjectArray jtargets, jstring jcomment,
 jboolean jforce)
{
  JNIEntry(SVNClient, lock);
  SVNClient *cl = SVNClient::getCppObject(jthis);
  if (cl == NULL)
    {
      JNIUtil::throwError("bad C++ this");
      return;
    }
  Targets targets(jtargets);
  if (JNIUtil::isExceptionThrown())
    return;

  JNIStringHolder comment(jcomment);
  if (JNIUtil::isExceptionThrown())
    return;

  cl->lock(targets, comment, jforce ? true : false);
}

JNIEXPORT void JNICALL
Java_org_tigris_subversion_javahl_SVNClient_unlock
(JNIEnv *env, jobject jthis, jobjectArray jtargets, jboolean jforce)
{
  JNIEntry(SVNClient, unlock);
  SVNClient *cl = SVNClient::getCppObject(jthis);
  if (cl == NULL)
    {
      JNIUtil::throwError("bad C++ this");
      return;
    }

  Targets targets(jtargets);
  if (JNIUtil::isExceptionThrown())
    return;


  cl->unlock(targets, jforce ? true : false);
}

JNIEXPORT void JNICALL
Java_org_tigris_subversion_javahl_SVNClient_info2
(JNIEnv *env, jobject jthis, jstring jpath, jobject jrevision,
 jobject jpegRevision, jint jdepth, jobjectArray jchangelists,
 jobject jinfoCallback)
{
  JNIEntry(SVNClient, info2);
  SVNClient *cl = SVNClient::getCppObject(jthis);
  if (cl == NULL)
    {
      JNIUtil::throwError("bad C++ this");
      return;
    }
  JNIStringHolder path(jpath);
  if (JNIUtil::isExceptionThrown())
    return;

  Revision revision(jrevision);
  if (JNIUtil::isExceptionThrown())
    return;

  Revision pegRevision(jpegRevision);
  if (JNIUtil::isExceptionThrown())
    return;

  StringArray changelists(jchangelists);
  if (JNIUtil::isExceptionThrown())
    return;

  InfoCallback callback(jinfoCallback);
  cl->info2(path, revision, pegRevision, (svn_depth_t)jdepth, changelists,
            &callback);
}
