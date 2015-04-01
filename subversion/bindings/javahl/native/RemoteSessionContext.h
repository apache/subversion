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
 * @file RemoteSessionContext.h
 * @brief Interface of the class RemoteSessionContext
 */

#ifndef JAVAHL_REMOTE_SESSION_CONTEXT_H
#define JAVAHL_REMOTE_SESSION_CONTEXT_H

#include "svn_ra.h"

#include "OperationContext.h"

class RemoteSessionContext : public OperationContext
{
  public:
    RemoteSessionContext(SVN::Pool &pool,
                         const char* jconfigDirectory,
                         const char* jusername, const char* jpassword,
                         std::auto_ptr<Prompter> prompter,
                         jobject jcfgcb, jobject jtunnelcb);
    virtual ~RemoteSessionContext();
    void activate(jobject jremoteSession, jobject jprogress);
    void * getCallbackBaton();
    svn_ra_callbacks2_t* getCallbacks();

  private:
    svn_ra_callbacks2_t* m_raCallbacks;
};

#endif /* JAVAHL_REMOTE_SESSION_CONTEXT_H */
