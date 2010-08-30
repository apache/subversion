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

#include "svn_types.h"
#include "svn_client.h"

#include <jni.h>
#include "Pool.h"
#include "JNIStringHolder.h"

class Prompter;
class ConflictResolverCallback;
class CommitMessage;

/**
 * This class contains a Java objects implementing the interface ClientContext
 * and implements the functions read & close of svn_stream_t.
 */
class ClientContext
{
 private:
  svn_client_ctx_t *persistentCtx;
  jobject m_jctx;

  std::string m_userName;
  std::string m_passWord;
  std::string m_configDir;

  Prompter *m_prompter;
  ConflictResolverCallback *m_conflictResolver;
  bool m_cancelOperation;

  CommitMessage *m_commitMessage;

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
 public:
  ClientContext(jobject jsvnclient);
  ~ClientContext();

  static svn_error_t *checkCancel(void *cancelBaton);

  svn_client_ctx_t *getContext(const char *message);

  void username(const char *pi_username);
  void password(const char *pi_password);
  void setPrompt(Prompter *prompter);
  void setConflictResolver(ConflictResolverCallback *conflictResolver);
  void commitMessageHandler(CommitMessage *commitMessage);
  void cancelOperation();
  const char *getConfigDirectory();

  /**
   * Set the configuration directory, taking the usual steps to
   * ensure that Subversion's config file templates exist in the
   * specified location.
   */
  void setConfigDirectory(const char *configDir);

  static void notify(void *baton, const svn_wc_notify_t *notify,
                     apr_pool_t *pool);
  static void progress(apr_off_t progressVal, apr_off_t total,
                       void *baton, apr_pool_t *pool);
};

#endif // CLIENTCONTEXT_H
