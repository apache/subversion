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
 * @file RemoteSession.h
 * @brief Interface of the class RemoteSession
 */

#ifndef JAVAHL_REMOTE_SESSION_H
#define JAVAHL_REMOTE_SESSION_H

#include <jni.h>

#include "svn_ra.h"

#include "SVNBase.h"
#include "RemoteSessionContext.h"
#include "Prompter.h"

class CommitEditor;

/*
 * This class wraps Ra based operations from svn_ra.h
 */
class RemoteSession : public SVNBase
{
  public:
    static RemoteSession* getCppObject(jobject jthis);
    static jobject open(jint jretryAttempts,
                        jstring jurl, jstring juuid,
                        jstring jconfigDirectory,
                        jstring jusername, jstring jpassword,
                        jobject jprompter, jobject jdeprecatedPrompter,
                        jobject jprogress, jobject jcfgcb, jobject jtunnelcb);
    static jobject open(jint jretryAttempts,
                        const char* url, const char* uuid,
                        const char* configDirectory,
                        const char* username, const char* password,
                        Prompter::UniquePtr prompter, jobject jprogress,
                        jobject jcfgcb, jobject jtunnelcb);
    virtual ~RemoteSession();

    void cancelOperation() const { m_context->cancelOperation(); }

    virtual void dispose(jobject jthis);

    void reparent(jstring jurl);
    jstring getSessionUrl();
    jstring getSessionRelativePath(jstring jurl);
    jstring getReposRelativePath(jstring jurl);
    jstring getReposUUID();
    jstring getReposRootUrl();
    jlong getLatestRevision();
    jlong getRevisionByTimestamp(jlong jtimestamp);
    void changeRevisionProperty(jlong jrevision, jstring jname,
                                jbyteArray jold_value,
                                jbyteArray jvalue);
    jobject getRevisionProperties(jlong jrevision);
    jbyteArray getRevisionProperty(jlong jrevision, jstring jname);
    jlong getFile(jlong jrevision, jstring jpath,
                  jobject jcontents, jobject jproperties);
    jlong getDirectory(jlong jrevision, jstring jpath, jint jdirent_fields,
                       jobject jdirents, jobject jproperties);
    jobject getMergeinfo(jobject jpaths, jlong jrevision, jobject jinherit,
                         jboolean jinclude_descendants);
    // TODO: update
    // TODO: switch
    void status(jobject jthis, jstring jstatus_target,
                jlong jrevision, jobject jdepth,
                jobject jstatus_editor, jobject jreporter);
    // TODO: diff
    void getLog(jobject jpaths, jlong jstartrev, jlong jendrev, jint jlimit,
                jboolean jstrict_node_history, jboolean jdiscover_changed_paths,
                jboolean jinclude_merged_revisions,
                jobject jrevprops, jobject jlog_callback);
    jobject checkPath(jstring jpath, jlong jrevision);
    jobject stat(jstring jpath, jlong jrevision);
    jobject getLocations(jstring jpath, jlong jpeg_revision,
                         jobject jlocation_revisions);
    void getLocationSegments(jstring jpath, jlong jpeg_revision,
                             jlong jstart_revision, jlong jend_revision,
                             jobject jcallback);
    void getFileRevisions(jstring jpath,
                          jlong jstart_revision, jlong jend_revision,
                          jboolean jinclude_merged_revisions,
                          jobject jcallback);
    // TODO: lock
    // TODO: unlock
    // TODO: getLock
    jobject getLocks(jstring jpath, jobject jdepth);
    // TODO: replayRange
    // TODO: replay
    // TODO: getDeletedRevision
    // TODO: getInheritedProperties
    jboolean hasCapability(jstring capability);

  private:
    friend class CommitEditor;
    RemoteSession(int retryAttempts,
                  const char* url, const char* uuid,
                  const char* configDirectory,
                  const char* username, const char* password,
                  Prompter::UniquePtr prompter, jobject jcfgcb, jobject jtunnelcb);

    svn_ra_session_t* m_session;
    RemoteSessionContext* m_context;
};

#endif // JAVAHL_REMOTE_SESSION_H
