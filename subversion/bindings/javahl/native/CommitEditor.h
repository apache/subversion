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

#include <string>
#include <jni.h>

#include "svn_ra.h"

#include "JNIUtil.h"
#include "SVNBase.h"
#include "CommitCallback.h"

#include "jniwrapper/jni_globalref.hpp"

class RemoteSession;

// Forward-declare the currently private EV2 editor struct.
struct svn_editor_t;

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
                              jboolean jkeep_locks,
                              jobject jget_base_cb,
                              jobject jget_props_cb,
                              jobject jget_kind_cb);
  virtual ~CommitEditor();

  virtual void dispose(jobject jthis);

  void addDirectory(jstring jrelpath,
                    jobject jchildren, jobject jproperties,
                    jlong jreplaces_revision);
  void addFile(jstring jrelpath,
               jobject jchecksum, jobject jcontents,
               jobject jproperties,
               jlong jreplaces_revision);
  void addSymlink(jstring jrelpath,
                  jstring jtarget, jobject jproperties,
                  jlong jreplaces_revision);
  void addAbsent(jstring jrelpath,
                 jobject jkind, jlong jreplaces_revision);
  void alterDirectory(jstring jrelpath, jlong jrevision,
                      jobject jchildren, jobject jproperties);
  void alterFile(jstring jrelpath, jlong jrevision,
                 jobject jchecksum, jobject jcontents,
                 jobject jproperties);
  void alterSymlink(jstring jrelpath, jlong jrevision,
                    jstring jtarget, jobject jproperties);
  void remove(jstring jrelpath, jlong jrevision);
  void copy(jstring jsrc_relpath, jlong jsrc_revision,
            jstring jdst_relpath, jlong jreplaces_revision);
  void move(jstring jsrc_relpath, jlong jsrc_revision,
            jstring jdst_relpath, jlong jreplaces_revision);
  void complete();
  void abort();

private:
  CommitEditor(RemoteSession* session,
               jobject jrevprops, jobject jcommit_callback,
               jobject jlock_tokens, jboolean jkeep_locks,
               jobject jget_base_cb, jobject jget_props_cb,
               jobject jget_kind_cb);

  // This is our private callbacks for the commit editor.
  static svn_error_t* provide_base_cb(svn_stream_t **contents,
                                      svn_revnum_t *revision,
                                      void *baton,
                                      const char *repos_relpath,
                                      apr_pool_t *result_pool,
                                      apr_pool_t *scratch_pool);
  static svn_error_t* provide_props_cb(apr_hash_t **props,
                                       svn_revnum_t *revision,
                                       void *baton,
                                       const char *repos_relpath,
                                       apr_pool_t *result_pool,
                                       apr_pool_t *scratch_pool);
  static svn_error_t* get_copysrc_kind_cb(svn_node_kind_t* kind, void* baton,
                                          const char* repos_relpath,
                                          svn_revnum_t src_revision,
                                          apr_pool_t *scratch_pool);

  bool m_valid;
  PersistentCommitCallback m_callback;
  RemoteSession* m_session;
  svn_editor_t* m_editor;

  Java::GlobalObject m_get_base_cb;
  Java::GlobalObject m_get_props_cb;
  Java::GlobalObject m_get_kind_cb;

  // Temporary, while EV2 shims are in place
  svn_ra_session_t* m_callback_session;
  const char* m_callback_session_url;
  const char* m_callback_session_uuid;
};

#endif // JAVAHL_COMMIT_EDITOR_H
