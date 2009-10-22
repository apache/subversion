/**
 * @copyright
 * ====================================================================
 *    Licensed to the Subversion Corporation (SVN Corp.) under one
 *    or more contributor license agreements.  See the NOTICE file
 *    distributed with this work for additional information
 *    regarding copyright ownership.  The SVN Corp. licenses this file
 *    to you under the Apache License, Version 2.0 (the
 *    "License"); you may not use this file except in compliance
 *    with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing,
 *    software distributed under the License is distributed on an
 *    "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 *    KIND, either express or implied.  See the License for the
 *    specific language governing permissions and limitations
 *    under the License.
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
