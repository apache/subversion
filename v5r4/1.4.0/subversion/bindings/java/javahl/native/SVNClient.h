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
 */
// SVNClient.h: interface for the SVNClient class.
//
//////////////////////////////////////////////////////////////////////

#if !defined(AFX_SVNCLIENT_H__B5A135CD_3D7C_4ABC_8D75_643B14507979__INCLUDED_)
#define AFX_SVNCLIENT_H__B5A135CD_3D7C_4ABC_8D75_643B14507979__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000
#include <jni.h>
#include "Path.h"

class Revision;
class Notify;
class Notify2;
class Targets;
class JNIByteArray;
class Prompter;
class BlameCallback;
class CommitMessage;
#include "svn_client.h"
#include "SVNBase.h"

class SVNClient :public SVNBase
{
public:
    jobjectArray info2(const char *path, Revision &revision, 
        Revision &pegRevision, bool recurse);
	void unlock(Targets &targets, bool force);
	void lock(Targets &targets, const char *comment, bool force);
	jobjectArray revProperties(jobject jthis, const char *path, 
                                Revision &revision);
    void cancelOperation();
    void commitMessageHandler(CommitMessage *commitMessage);
    const char * getConfigDirectory();

    /**
     * Set the configuration directory, taking the usual steps to
     * ensure that Subversion's config file templates exist in the
     * specified location.
     */
    void setConfigDirectory(const char *configDir);

    jbyteArray blame(const char *path, Revision& revisionStart,
                         Revision &revisionEnd);
    void blame(const char *path, Revision &pegRevision, 
                   Revision &revisionStart,
                   Revision &revisionEnd, BlameCallback *callback);
    void relocate(const char *from, const char *to, const char *path,
                      bool recurse);
    jbyteArray fileContent(const char *path, Revision &revision,  
                           Revision &pegRevision);
    void streamFileContent(const char *path, Revision &revision,
                           Revision &pegRevision, jobject outputStream,
                           size_t bufSize);
    void propertyCreate(const char *path, const char *name,
                            JNIByteArray &value, bool recurse, bool force);
    void propertyCreate(const char *path, const char *name,
                            const char *value, bool recurse, bool force);
    void propertyRemove(const char *path, const char *name, bool recurse);
    void propertySet(const char *path, const char *name,
                         JNIByteArray &value, bool recurse, bool force);
    void propertySet(const char *path, const char *name, const char *value,
                         bool recurse, bool force);
    jobjectArray properties(jobject jthis, const char *path,
                            Revision &revision, Revision &pegRevision);
    void merge(const char *path1, Revision &revision1, const char *path2,
                   Revision &revision2, const char *localPath, bool force,
                   bool recurse, bool ignoreAncestry, bool dryRun);
    void merge(const char *path, Revision &pegRevision, Revision &revision1,
                   Revision &revision2, const char *localPath, bool force,
                   bool recurse, bool ignoreAncestry, bool dryRun);
    void doImport(const char *path, const char *url, const char *message,
                      bool recurse);
    jlong doSwitch(const char *path, const char *url, Revision &revision,
                       bool recurse);
    jlong doExport(const char *srcPath, const char *destPath, 
                       Revision &revision, Revision &pegRevision, bool force,
                       bool ignoreExternals, bool recurse, 
                       const char *nativeEOL);
    void resolved(const char *path, bool recurse);
    void cleanup(const char *path);
    void mkdir(Targets &targets, const char *message);
    void move(const char *srcPath, const char *destPath,
                  const char *message, bool force);
    void copy(const char *srcPath, const char *destPath,
                  const char *message, Revision &revision);
    jlong commit(Targets &targets, const char *message, bool recurse, 
                  bool noUnlock);
    jlongArray update(Targets &targets, Revision &revision, bool recurse,
        bool ignoreExternals);
    void add(const char *path, bool recurse, bool force);
    void revert(const char *path, bool recurse);
    void remove(Targets &targets, const char *message,bool force);
    void notification(Notify *notify);
    void notification2(Notify2 *notify2);
    jlong checkout(const char *moduleName, const char *destPath,
                       Revision &revision, Revision &pegRevsion, bool recurse,
                       bool ignoreExternals);
    jobjectArray logMessages(const char *path, Revision &revisionStart,
                                 Revision &revisionEnd, bool stopOnCopy,
                                 bool discoverPaths, long limit);
    void setPrompt(Prompter *prompter);
    void password(const char *pi_password);
    void username(const char *pi_username);
    jstring getAdminDirectoryName();
    jboolean isAdminDirectory(const char *name);
    jobject info(const char *path);
    jobject singleStatus(const char *path, bool onServer);
    jobjectArray status(const char *path, bool descend, bool onServer,
                            bool getAll, bool noIgnore, bool ignoreExternals);
    jobjectArray list(const char *url, Revision &revision, 
                      Revision &pegRevision, bool recurse);
    jobject revProperty(jobject jthis, const char *path, const char *name,
                            Revision &rev);
    void setRevProperty(jobject jthis, const char *path, const char *name,
                            Revision &rev, const char *value, bool force);
    jstring getVersionInfo(const char *path, const char *trailUrl,
                            bool lastChanged);
    jobject propertyGet(jobject jthis, const char *path, const char *name,
                            Revision &revision, Revision &pegRevision);
    void diff(const char *target1, Revision &revision1,
                  const char *target2, Revision &revision2,
                  const char *outfileName,bool recurse, bool ignoreAncestry,
                  bool noDiffDelete, bool force);
    void diff(const char *target, Revision &pegevision,
                  Revision &startRevision, Revision &endRevision,
                  const char *outfileName,bool recurse, bool ignoreAncestry,
                  bool noDiffDelete, bool force);

    const char * getLastPath();
    void dispose(jobject jthis);
    static SVNClient * getCppObject(jobject jthis);
    SVNClient();
    virtual ~SVNClient();
    static jobject createJavaLock(const svn_lock_t *lock);
private:
    static svn_error_t * checkCancel(void *cancelBaton);
    void propertySet(const char *path, const char *name,
                         svn_string_t *value, bool recurse, bool force);
    jobject createJavaProperty(jobject jthis, const char *path,
                                   const char *name, svn_string_t *value);
    jobject createJavaDirEntry(const char *path, svn_dirent_t *dirent);
    jobject createJavaInfo(const svn_wc_entry_t *entry);
    svn_client_ctx_t * getContext(const char *message);
    svn_stream_t * createReadStream(apr_pool_t* pool, const char *path,
                                    Revision &revision, Revision &pegRevision,
                                    size_t& size);
    Notify *m_notify;
    Notify2 *m_notify2;
    Prompter *m_prompter;
    Path m_lastPath;
    bool m_cancelOperation;
    CommitMessage *m_commitMessage;
    void *getCommitMessageBaton(const char *message);
    static svn_error_t *getCommitMessage(const char **log_msg,
                                             const char **tmp_file,
                                             apr_array_header_t *commit_items,
                                             void *baton,
                                             apr_pool_t *pool);
    std::string m_userName;
    std::string m_passWord;
    std::string m_configDir;
    static jobject createJavaStatus(const char *path,
                                        svn_wc_status2_t *status);
    static svn_error_t *messageReceiver(void *baton,
                                            apr_hash_t * changed_paths,
                                            svn_revnum_t rev,
                                            const char *author,
                                            const char *date,
                                            const char *msg,
                                            apr_pool_t * pool);
    static void statusReceiver(void *baton,
                                   const char *path, svn_wc_status2_t *status);
    static svn_error_t *infoReceiver(void *baton, 
                                     const char *path,
                                     const svn_info_t *info,
                                     apr_pool_t *pool);
    static jobject createJavaInfo2(const char *path, const svn_info_t *info);

};
// !defined(AFX_SVNCLIENT_H__B5A135CD_3D7C_4ABC_8D75_643B14507979__INCLUDED_)
#endif
