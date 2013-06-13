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
#include "Revision.h"

#include <set>

class SVNEditor;

/*
 * This class wraps Ra based operations from svn_ra.h
 */
class RemoteSession : public SVNBase
{
  public:
    static RemoteSession* getCppObject(jobject jthis);
    RemoteSession(jobject*, jstring jurl, jstring juuid,
                  jstring jconfigDirectory,
                  jstring jusername, jstring jpassword,
                  jobject jprompter, jobject jprogress);
    ~RemoteSession();

    jlong getLatestRevision();
    jstring getUUID();
    jstring getUrl();

    svn_revnum_t getDatedRev(jlong timestamp);
    jobject getLocks(jstring jpath, jobject jdepth);
    jobject checkPath(jstring jpath, jobject jrevision);

    virtual void dispose(jobject jthis);

  private:
    svn_ra_session_t* m_session;
    RemoteSessionContext* m_context;
};

#endif // JAVAHL_REMOTE_SESSION_H
