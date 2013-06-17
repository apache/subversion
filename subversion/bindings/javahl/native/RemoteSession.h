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
                        jobject jprompter, jobject jprogress);
    static jobject open(jint jretryAttempts,
                        const char* url, const char* uuid,
                        const char* configDirectory,
                        const char* username, const char* password,
                        Prompter* prompter, jobject jprogress);
    ~RemoteSession();

    void cancelOperation() const { m_context->cancelOperation(); }

    virtual void dispose(jobject jthis);

    jstring getSessionUrl();
    jstring getReposUUID();
    jobject getLatestRevision();

    jobject getRevisionByTimestamp(jlong timestamp);
    jobject getLocks(jstring jpath, jobject jdepth);
    jobject checkPath(jstring jpath, jobject jrevision);

  private:
    RemoteSession(jobject*, int retryAttempts,
                  const char* url, const char* uuid,
                  const char* configDirectory,
                  const char* username, const char* password,
                  Prompter* prompter, jobject jprogress);

    svn_ra_session_t* m_session;
    RemoteSessionContext* m_context;
};

#endif // JAVAHL_REMOTE_SESSION_H
