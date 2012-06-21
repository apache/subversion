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
 * @file RaSharedContext.h
 * @brief Interface of the class RaSharedContext
 */

#ifndef RASHAREDCONTEXT_H
#define RASHAREDCONTEXT_H

#include <string>

#include "svn_types.h"
#include "svn_client.h"

#include <jni.h>
#include "Pool.h"
#include "JNIStringHolder.h"

class Prompter;

/**
 * This class contains a Java objects implementing the interface RaSharedContext
 */
class RaSharedContext
{
 private:
  std::string m_userName;
  std::string m_passWord;
  std::string m_configDir;

  apr_hash_t * m_config;

  Prompter *m_prompter;
  bool m_cancelOperation;

 protected:
  SVN::Pool *m_pool;

  jobject m_jctx;
  static void progress(apr_off_t progressVal, apr_off_t total,
                       void *baton, apr_pool_t *pool);
 public:
  RaSharedContext(SVN::Pool &pool);
  void attachJavaObject(jobject contextHolder, const char *contextClassType, const char *contextFieldName, jfieldID * ctxFieldID);
  virtual ~RaSharedContext();

  static svn_error_t *checkCancel(void *cancelBaton);

  virtual void username(const char *pi_username);
  virtual void password(const char *pi_password);
  virtual void setPrompt(Prompter *prompter);
  svn_auth_baton_t *getAuthBaton(SVN::Pool &in_pool);

  void cancelOperation();
  void resetCancelRequest();
  virtual bool isCancelledOperation();
  const char *getConfigDirectory() const;

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

  static svn_error_t * clientName(void *baton, const char **name, apr_pool_t *pool);
  virtual const char * getClientName() const;
};

#endif // RASHAREDCONTEXT_H
