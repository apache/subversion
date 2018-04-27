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
 * @file ClientContext.h
 * @brief Interface of the class ClientContext
 */

#ifndef CLIENTCONTEXT_H
#define CLIENTCONTEXT_H

#include <string>

#include "OperationContext.h"

#include "svn_types.h"
#include "svn_client.h"

#include <jni.h>
#include "Pool.h"
#include "JNIStringHolder.h"

class CommitMessage;

/**
 * This class contains a Java objects implementing the interface ClientContext
 * and implements the functions read & close of svn_stream_t.
 *
 */
class ClientContext : public OperationContext
{
 private:
  svn_client_ctx_t *m_context;

 protected:
  static void notify(void *baton, const svn_wc_notify_t *notify,
                     apr_pool_t *pool);
  static svn_error_t *resolve(svn_wc_conflict_result_t **result,
                              const svn_wc_conflict_description2_t *desc,
                              void *baton,
                              apr_pool_t *result_pool,
                              apr_pool_t *scratch_pool);
  static svn_wc_conflict_result_t *javaResultToC(jobject result,
                                                 apr_pool_t *pool);

 public:
  ClientContext(jobject jsvnclient, SVN::Pool &pool);
  virtual ~ClientContext();
  virtual void setTunnelCallback(jobject jtunnelcb);

  svn_client_ctx_t *getContext(CommitMessage *message, SVN::Pool &in_pool);
};

#endif // CLIENTCONTEXT_H
