/**
 * @copyright
 * ====================================================================
 * Copyright (c) 2003-2007 CollabNet.  All rights reserved.
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
 * @file SVNClient.h
 * @brief Interface for the SVNClient class
 */

#if !defined(AFX_SVNCLIENT_H__B5A135CD_3D7C_4ABC_8D75_643B14507979__INCLUDED_)
#define AFX_SVNCLIENT_H__B5A135CD_3D7C_4ABC_8D75_643B14507979__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

#include <vector>
#include <jni.h>
#include "Path.h"

class Revision;
class RevisionRange;
class Notify;
class Notify2;
class ConflictResolverCallback;
class ProgressListener;
class Targets;
class JNIByteArray;
class Prompter;
class CopySources;
class DiffSummaryReceiver;
class BlameCallback;
class ProplistCallback;
class LogMessageCallback;
class InfoCallback;
class ListCallback;
class StatusCallback;
class CommitMessage;
#include "svn_types.h"
#include "svn_client.h"
#include "SVNBase.h"

class SVNClient :public SVNBase
{
 public:
  void info2(const char *path, Revision &revision, Revision &pegRevision,
             svn_depth_t depth, InfoCallback *callback);
  void unlock(Targets &targets, bool force);
  void lock(Targets &targets, const char *comment, bool force);
  jobjectArray revProperties(jobject jthis, const char *path,
                             Revision &revision);
  void cancelOperation();
  void commitMessageHandler(CommitMessage *commitMessage);
  const char *getConfigDirectory();

  /**
   * Set the configuration directory, taking the usual steps to
   * ensure that Subversion's config file templates exist in the
   * specified location.
   */
  void setConfigDirectory(const char *configDir);

  void blame(const char *path, Revision &pegRevision,
             Revision &revisionStart, Revision &revisionEnd,
             bool ignoreMimeType, bool includeMergedRevisions,
             BlameCallback *callback);
  void relocate(const char *from, const char *to, const char *path,
                bool recurse);
  jbyteArray fileContent(const char *path, Revision &revision,
                         Revision &pegRevision);
  void streamFileContent(const char *path, Revision &revision,
                         Revision &pegRevision, jobject outputStream,
                         size_t bufSize);
  void propertyRemove(const char *path, const char *name, svn_depth_t depth);
  void propertySet(const char *path, const char *name, const char *value,
                   svn_depth_t depth, bool force);
  void properties(const char *path, Revision &revision,
                  Revision &pegRevision, svn_depth_t depth,
                  ProplistCallback *callback);
  jobject getMergeInfo(const char *target, Revision &pegRevision);
  jobjectArray getAvailableMerges(const char *target, Revision &pegRevision,
                                  const char *mergeSource);
  jobjectArray suggestMergeSources(const char *path, Revision &pegRevision);
  void merge(const char *path1, Revision &revision1, const char *path2,
             Revision &revision2, const char *localPath, bool force,
             svn_depth_t depth, bool ignoreAncestry, bool dryRun);
  void merge(const char *path, Revision &pegRevision,
             std::vector<RevisionRange> &rangesToMerge,
             const char *localPath, bool force, svn_depth_t depth,
             bool ignoreAncestry, bool dryRun);
  void doImport(const char *path, const char *url, const char *message,
                svn_depth_t depth, bool noIgnore, bool ignoreUnknownNodeTypes);
  jlong doSwitch(const char *path, const char *url, Revision &revision,
                 svn_depth_t depth, bool ignoreExternals,
                 bool allowUnverObstructions);
  jlong doExport(const char *srcPath, const char *destPath,
                 Revision &revision, Revision &pegRevision, bool force,
                 bool ignoreExternals, svn_depth_t depth,
                 const char *nativeEOL);
  void resolved(const char *path, svn_depth_t depth,
                svn_wc_conflict_choice_t choice);
  void cleanup(const char *path);
  void mkdir(Targets &targets, const char *message, bool makeParents);
  void move(Targets &srcPaths, const char *destPath,
            const char *message, bool force, bool moveAsChild,
            bool makeParents, bool withMergeHistory);
  void copy(CopySources &copySources, const char *destPath,
            const char *message, bool copyAsChild, bool makeParents,
            bool withMergeHistory);
  jlong commit(Targets &targets, const char *message, svn_depth_t depth,
               bool noUnlock, bool keepChangelist,
               const char *changelistName);
  jlongArray update(Targets &targets, Revision &revision, svn_depth_t depth,
                    bool ignoreExternals, bool allowUnverObstructions);
  void add(const char *path, svn_depth_t depth, bool force, bool no_ignore,
           bool add_parents);
  void revert(const char *path, svn_depth_t depth);
  void remove(Targets &targets, const char *message, bool force,
              bool keep_local);
  void notification(Notify *notify);
  void notification2(Notify2 *notify2);
  void setConflictResolver(ConflictResolverCallback *conflictResolver);
  void setProgressListener(ProgressListener *progressListener);
  jlong checkout(const char *moduleName, const char *destPath,
                 Revision &revision, Revision &pegRevsion, svn_depth_t depth,
                 bool ignoreExternals, bool allowUnverObstructions);
  void logMessages(const char *path, Revision &pegRevision,
                   Revision &revisionStart,
                   Revision &revisionEnd, bool stopOnCopy,
                   bool discoverPaths, bool includeMergedRevisions,
                   bool omitLogText, long limit,
                   LogMessageCallback *callback);
  void setPrompt(Prompter *prompter);
  void password(const char *pi_password);
  void username(const char *pi_username);
  jstring getAdminDirectoryName();
  jboolean isAdminDirectory(const char *name);
  jobject info(const char *path);
  void addToChangelist(Targets &srcPaths, const char *changelist);
  void removeFromChangelist(Targets &srcPaths, const char *changelist);
  jobjectArray getChangelist(const char *changelist, const char *rootPath);
  void status(const char *path, svn_depth_t depth, bool onServer,
              bool getAll, bool noIgnore, bool ignoreExternals,
              StatusCallback *callback);
  void list(const char *url, Revision &revision, Revision &pegRevision,
            svn_depth_t depth, int direntFields, bool fetchLocks,
            ListCallback *callback);
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
            const char *outfileName, svn_depth_t depth, bool ignoreAncestry,
            bool noDiffDelete, bool force);
  void diff(const char *target, Revision &pegevision,
            Revision &startRevision, Revision &endRevision,
            const char *outfileName, svn_depth_t depth, bool ignoreAncestry,
            bool noDiffDelete, bool force);
  void diffSummarize(const char *target1, Revision &revision1,
                     const char *target2, Revision &revision2,
                     svn_depth_t depth, bool ignoreAncestry,
                     DiffSummaryReceiver &receiver);
  void diffSummarize(const char *target, Revision &pegRevision,
                     Revision &startRevision, Revision &endRevision,
                     svn_depth_t depth, bool ignoreAncestry,
                     DiffSummaryReceiver &receiver);

  const char *getLastPath();
  void dispose(jobject jthis);
  static SVNClient *getCppObject(jobject jthis);
  SVNClient();
  virtual ~SVNClient();
  static jobject createJavaLock(const svn_lock_t *lock);
 private:
  static svn_error_t *checkCancel(void *cancelBaton);
  void propertySet(const char *path, const char *name,
                   svn_string_t *value, svn_depth_t depth, bool force,
                   svn_revnum_t baseRevisionForURL);
  jobject createJavaProperty(jobject jthis, const char *path,
                             const char *name, svn_string_t *value);
  svn_client_ctx_t *getContext(const char *message);
  svn_stream_t *createReadStream(apr_pool_t *pool, const char *path,
                                 Revision &revision, Revision &pegRevision,
                                 size_t &size);
  /**
   * Shared implementation for diff() APIs. When pegRevision is
   * provided, revision1 and revision2 equate to startRevision and
   * endRevision (respectively), and target2 is ignored.
   */
  void diff(const char *target1, Revision &revision1,
            const char *target2, Revision &revision2,
            Revision *pegRevision,
            const char *outfileName, svn_depth_t depth, bool ignoreAncestry,
            bool noDiffDelete, bool force);

  jobject createJavaInfo(const svn_wc_entry_t *entry);

  Notify *m_notify;
  Notify2 *m_notify2;
  ConflictResolverCallback *m_conflictResolver;
  ProgressListener *m_progressListener;
  Prompter *m_prompter;
  Path m_lastPath;
  bool m_cancelOperation;
  CommitMessage *m_commitMessage;

  jobjectArray makeJRevisionRangeArray(apr_array_header_t *ranges);

  /**
   * Implements the svn_client_get_commit_log3_t API.
   */
  static svn_error_t *getCommitMessage(const char **log_msg,
                                       const char **tmp_file,
                                       const apr_array_header_t *
                                       commit_items,
                                       void *baton,
                                       apr_pool_t *pool);
  /**
   * Produce a baton for the getCommitMessage() callback.
   */
  void *getCommitMessageBaton(const char *message);

  std::string m_userName;
  std::string m_passWord;
  std::string m_configDir;
};

// !defined(AFX_SVNCLIENT_H__B5A135CD_3D7C_4ABC_8D75_643B14507979__INCLUDED_)
#endif
