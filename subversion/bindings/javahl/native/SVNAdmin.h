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
 * @file SVNAdmin.h
 * @brief Interface of the class SVNAdmin
 */

#ifndef SVNADMIN_H
#define SVNADMIN_H

#include <jni.h>
#include "svn_repos.h"
#include "SVNBase.h"
#include "Revision.h"
#include "Outputer.h"
#include "Inputer.h"
#include "MessageReceiver.h"
#include "Targets.h"

class SVNAdmin : public SVNBase
{
 public:
  void rmlocks(const char *path, Targets &locks);
  jobjectArray lslocks(const char *path);
  void verify(const char *path, Outputer &messageOut,
              Revision &revisionStart, Revision &revisionEnd);
  void setRevProp(const char *path, Revision &revision,
                  const char *propName, const char *propValue,
                  bool usePreRevPropChangeHook,
                  bool usePostRevPropChangeHook);
  void rmtxns(const char *path, Targets &transactions);
  jlong recover(const char *path);
  void lstxns(const char *path, MessageReceiver &messageReceiver);
  void load(const char *path, Inputer &dataIn, Outputer &messageOut,
            bool ignoreUUID, bool forceUUID, bool usePreCommitHook,
            bool usePostCommitHook, const char *relativePath);
  void listUnusedDBLogs(const char *path,
                        MessageReceiver &messageReceiver);
  void listDBLogs(const char *path, MessageReceiver &messageReceiver);
  void hotcopy(const char *path, const char *targetPath, bool cleanLogs);
  void dump(const char *path, Outputer &dataOut, Outputer &messageOut,
            Revision &revsionStart, Revision &RevisionEnd,
            bool incremental, bool useDeltas);
  void deltify(const char *path, Revision &start, Revision &end);
  void create(const char *path, bool ignoreUUID, bool forceUUID,
              const char *configPath, const char *fstype);
  SVNAdmin();
  virtual ~SVNAdmin();
  void dispose(jobject jthis);
  static SVNAdmin *getCppObject(jobject jthis);

 private:
  static svn_error_t *getRevnum(svn_revnum_t *revnum,
                                const svn_opt_revision_t *revision,
                                svn_revnum_t youngest, svn_repos_t *repos,
                                apr_pool_t *pool);
};

#endif // SVNADMIN_H
