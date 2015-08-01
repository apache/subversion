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
 * @file OperationContext.h
 * @brief Interface of the class OperationContext
 */

#ifndef JAVAHL_OPERATION_CONTEXT_H
#define JAVAHL_OPERATION_CONTEXT_H

#include <string>
#include <memory>

#include "svn_types.h"
#include "svn_client.h"
#include "private/svn_atomic.h"

#include <jni.h>
#include "Pool.h"
#include "JNIStringHolder.h"

class Prompter;

/**
 * This class contains a Java objects implementing the interface RaSharedContext
 */
class OperationContext
{
 private:
  std::string m_userName;
  std::string m_passWord;
  std::string m_configDir;

  apr_hash_t * m_config;

  std::auto_ptr<Prompter> m_prompter;
  svn_atomic_t m_cancelOperation;

 protected:
  SVN::Pool *m_pool;

  jobject m_jctx;
  jobject m_jcfgcb;
  jobject m_jtunnelcb;

  static void progress(apr_off_t progressVal, apr_off_t total,
                       void *baton, apr_pool_t *pool);
  void notifyConfigLoad();

  static svn_boolean_t checkTunnel(
      void *tunnel_baton, const char *tunnel_name);

  static svn_error_t *openTunnel(
      svn_stream_t **request, svn_stream_t **response,
      svn_ra_close_tunnel_func_t *close_func, void **close_baton,
      void *tunnel_baton,
      const char *tunnel_name, const char *user,
      const char *hostname, int port,
      svn_cancel_func_t cancel_func, void *cancel_baton,
      apr_pool_t *pool);

  static void closeTunnel(
      void *tunnel_context, void *tunnel_baton);

 public:
  OperationContext(SVN::Pool &pool);
  void attachJavaObject(jobject contextHolder, const char *contextClassType, const char *contextFieldName, jfieldID * ctxFieldID);
  virtual ~OperationContext();

  static svn_error_t *checkCancel(void *cancelBaton);

  virtual void username(const char *pi_username);
  virtual void password(const char *pi_password);
  virtual void setPrompt(std::auto_ptr<Prompter> prompter);
  svn_auth_baton_t *getAuthBaton(SVN::Pool &in_pool);

  void cancelOperation();
  void resetCancelRequest();
  virtual bool isCancelledOperation();
  jobject getSelf() const;
  const char *getConfigDirectory() const;
  const char *getUsername() const;
  const char *getPassword() const;
  std::auto_ptr<Prompter> clonePrompter() const;

  /**
   * Set the configuration directory, taking the usual steps to
   * ensure that Subversion's config file templates exist in the
   * specified location.
   */
  void setConfigDirectory(const char *configDir);

  /**
   * Return configuration data for the context.
   * Read it from config directory if necessary
   */
  apr_hash_t *getConfigData();

  void setConfigEventHandler(jobject jcfgcb);
  jobject getConfigEventHandler() const;

  static svn_error_t * clientName(void *baton, const char **name, apr_pool_t *pool);
  virtual const char * getClientName() const;

  virtual void setTunnelCallback(jobject jtunnelcb);
  jobject getTunnelCallback() const;
};

#endif // JAVAHL_OPERATION_CONTEXT_H
