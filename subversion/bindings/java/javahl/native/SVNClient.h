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
class Targets;
class JNIByteArray;
class Prompter;
#include <svn_client.h>

class SVNClient
{
public:
	jobject revProperty(jobject jthis, const char *path, const char *name, Revision &rev);
	jobjectArray list(const char *url, Revision &revision, bool force);
	jbyteArray fileContent(const char *path, Revision &revision);
	void propertyCreate(const char *path, const char *name, JNIByteArray &value, bool recurse);
	void propertyCreate(const char *path, const char *name, const char *value, bool recurse);
	void propertyRemove(const char *path, const char *name, bool recurse);
	void propertySet(const char *path, const char *name, JNIByteArray &value, bool recurse);
	void propertySet(const char *path, const char *name, const char *value, bool recurse);
	jobjectArray properties(jobject jthis, const char *path);
	void merge(const char *path1, Revision &revision1, const char *path2, Revision &revision2,const char *localPath, bool force, bool recurse);
	void doImport(const char *path, const char *url, const char *newEntry, const char *message, bool recurse);
	void doSwitch(const char *path, const char *url, Revision &revision, bool recurse);
	void doExport(const char *srcPath, const char *destPath, Revision &revision);
	void resolved(const char *path, bool recurse);
	void cleanup(const char *path);
	void mkdir(const char *path, const char *message);
	void move(const char *srcPath, const char *destPath, const char *message, Revision &revision, bool force);
	void copy(const char *srcPath, const char *destPath, const char *message, Revision &revision);
	jlong commit(Targets &targets, const char *message, bool recurse);
	void update(const char *path, Revision &revision, bool recurse);
	void add(const char *path, bool recurse);
	void revert(const char *path, bool recurse);
	void remove(const char *path, const char *message, bool force);
	void notification(Notify *notify);
	void checkout(const char *moduleName, const char *destPath, Revision &revision, bool recurse);
	jobjectArray logMessages(const char *path, Revision &revisionStart, Revision &revisionEnd);
	void setPrompt(Prompter *prompter);
	void password(const char *password);
	void username(const char *username);
	jobject singleStatus(const char *path, bool onServer);
	jobjectArray status(const char *path, bool descend, bool onServer);
	const char * getLastPath();
	void finalize();
	void dispose(jobject jthis);
	static SVNClient * getCppObject(jobject jthis);
	jlong getCppAddr();
	SVNClient();
	virtual ~SVNClient();
private:
	void propertySet(const char *path, const char *name, svn_string_t *value, bool recurse);
	jobject createJavaProperty(jobject jthis, const char *path, const char *name, svn_string_t *value);
	jobject createJavaDirEntry(const char *path, svn_dirent_t *dirent);
	svn_client_ctx_t * getContext(const char *message);
	Notify *m_notify;
	Prompter *m_prompter;
    Path m_lastPath;
	static svn_error_t *getCommitMessage(const char **log_msg, const char **tmp_file,
                                apr_array_header_t *commit_items, void *baton,
                                apr_pool_t *pool);
	void *getCommitMessageBaton(const char *message, const char *baseDir = NULL);
    std::string m_userName;
    std::string m_passWord;
	jobject createJavaStatus(const char *path, svn_wc_status_t *status);
	jint mapStatusKind(int svnKind);
	static svn_error_t *messageReceiver (void *baton, apr_hash_t * changed_paths,
                 svn_revnum_t rev, const char *author, const char *date,
                 const char *msg, apr_pool_t * pool);
};

#endif // !defined(AFX_SVNCLIENT_H__B5A135CD_3D7C_4ABC_8D75_643B14507979__INCLUDED_)
