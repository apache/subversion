/**
 * @copyright
 * ====================================================================
 *    Licensed to the Apache Software Foundation (ASF) under one
 *    or more contributor license agreements.  See the NOTICE file
 *    distributed with this work for additional information
 *    regarding copyright ownership.  The ASF licenses this file
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
 * @file CommitEditor.h
 * @brief Interface of the class CommitEditor
 */

#ifndef JAVAHL_COMMIT_EDITOR_H
#define JAVAHL_COMMIT_EDITOR_H

#include <jni.h>

#include "svn_ra.h"

#include "JNIUtil.h"
#include "SVNBase.h"
#include "CommitCallback.h"

// Forward-declare the currently private EV2 editor structs.
struct svn_editor_t;
struct svn_delta__extra_baton;

/*
 * This class wraps an EV2 commit editor.
 */
class CommitEditor : public SVNBase
{
public:
  static CommitEditor* getCppObject(jobject jthis);
  static jlong createInstance(jobject jsession,
                              jobject jrevprops,
                              jobject jcommit_callback,
                              jobject jlock_tokens,
                              jboolean jkeep_locks);
  virtual ~CommitEditor();

  virtual void dispose(jobject jthis);

  void addDirectory(jobject jsession, jstring jrelpath,
                    jobject jchildren, jobject jproperties,
                    jlong jreplaces_revision);
  void addFile(jobject jsession, jstring jrelpath,
               jobject jchecksum, jobject jcontents,
               jobject jproperties,
               jlong jreplaces_revision);
  void addSymlink(jobject jsession, jstring jrelpath,
                  jstring jtarget, jobject jproperties,
                  jlong jreplaces_revision);
  void addAbsent(jobject jsession, jstring jrelpath,
                 jobject jkind, jlong jreplaces_revision);
  void alterDirectory(jobject jsession,
                      jstring jrelpath, jlong jrevision,
                      jobject jchildren, jobject jproperties);
  void alterFile(jobject jsession,
                 jstring jrelpath, jlong jrevision,
                 jobject jchecksum, jobject jcontents,
                 jobject jproperties);
  void alterSymlink(jobject jsession,
                    jstring jrelpath, jlong jrevision,
                    jstring jtarget, jobject jproperties);
  void remove(jobject jsession, jstring jrelpath, jlong jrevision);
  void copy(jobject jsession,
            jstring jsrc_relpath, jlong jsrc_revision,
            jstring jdst_relpath, jlong jreplaces_revision);
  void move(jobject jsession,
            jstring jsrc_relpath, jlong jsrc_revision,
            jstring jdst_relpath, jlong jreplaces_revision);
  void rotate(jobject jsession, jobject jelements);
  void complete(jobject jsession);
  void abort(jobject jsession);

private:
  CommitEditor(svn_ra_session_t* session,
               jobject jrevprops, jobject jcommit_callback,
               jobject jlock_tokens, bool keep_locks);

  bool m_valid;
  CommitCallback m_callback;
  svn_editor_t* m_editor;
  svn_delta__extra_baton* m_extra_baton;
};

#endif // JAVAHL_COMMIT_EDITOR_H
