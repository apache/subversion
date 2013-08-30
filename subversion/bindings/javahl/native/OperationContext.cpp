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
 * @file OperationContext.cpp
 * @brief Implementation of the class OperationContext
 */

#include "svn_client.h"
#include "private/svn_wc_private.h"
#include "svn_private_config.h"

#include "OperationContext.h"
#include "JNIUtil.h"
#include "JNICriticalSection.h"

#include "Prompter.h"
#include "CreateJ.h"
#include "EnumMapper.h"
#include "CommitMessage.h"

OperationContext::OperationContext(SVN::Pool &pool)
  : m_config(NULL),
    m_prompter(NULL),
    m_cancelOperation(0),
    m_pool(&pool),
    m_jctx(NULL),
    m_jcfgcb(NULL)
{}

void
OperationContext::attachJavaObject(
    jobject contextHolder, const char *contextClassType,
    const char *contextFieldName, jfieldID * ctxFieldID)
{
  JNIEnv *env = JNIUtil::getEnv();

  /* Grab a global reference to the Java object embedded in the parent
     Java object. */
  if ((*ctxFieldID) == 0)
    {
      jclass clazz = env->GetObjectClass(contextHolder);
      if (JNIUtil::isJavaExceptionThrown())
        return;

      (*ctxFieldID) = env->GetFieldID(clazz, contextFieldName, contextClassType);
      if (JNIUtil::isJavaExceptionThrown() || (*ctxFieldID) == 0)
        return;

      env->DeleteLocalRef(clazz);
    }

  jobject jctx = env->GetObjectField(contextHolder, (*ctxFieldID));
  if (JNIUtil::isJavaExceptionThrown())
    return;

  m_jctx = env->NewGlobalRef(jctx);
  if (JNIUtil::isJavaExceptionThrown())
    return;

  env->DeleteLocalRef(jctx);
}

OperationContext::~OperationContext()
{
  delete m_prompter;

  JNIEnv *env = JNIUtil::getEnv();
  env->DeleteGlobalRef(m_jctx);
  if (m_jcfgcb)
    env->DeleteGlobalRef(m_jcfgcb);
}

apr_hash_t *
OperationContext::getConfigData()
{
  if(m_pool->getPool() == NULL)
    {
      JNIUtil::throwNullPointerException("pool is null");
    }

  if (m_config == NULL)
    {
      const char *configDir = m_configDir.c_str();
      if (m_configDir.empty())
        configDir = NULL;
      SVN_JNI_ERR(
          svn_config_get_config(&m_config, configDir, m_pool->getPool()), NULL);
      notifyConfigLoad();
    }

  return m_config;
}

svn_auth_baton_t *
OperationContext::getAuthBaton(SVN::Pool &in_pool)
{
  svn_auth_baton_t *ab;
  apr_pool_t *pool = in_pool.getPool();

  apr_hash_t * configData = getConfigData();

  if (configData == NULL)
    {
      return NULL;
    }

  svn_config_t *config = reinterpret_cast<svn_config_t *>(apr_hash_get(configData,
      SVN_CONFIG_CATEGORY_CONFIG, APR_HASH_KEY_STRING));

  /* The whole list of registered providers */
  apr_array_header_t *providers;

  /* Populate the registered providers with the platform-specific providers */
  SVN_JNI_ERR(
      svn_auth_get_platform_specific_client_providers(&providers, config, pool),
      NULL);

  /* Use the prompter (if available) to prompt for password and cert
   * caching. */
  svn_auth_plaintext_prompt_func_t plaintext_prompt_func = NULL;
  void *plaintext_prompt_baton = NULL;
  svn_auth_plaintext_passphrase_prompt_func_t plaintext_passphrase_prompt_func;
  void *plaintext_passphrase_prompt_baton = NULL;

  if (m_prompter != NULL)
    {
      plaintext_prompt_func = Prompter::plaintext_prompt;
      plaintext_prompt_baton = m_prompter;
      plaintext_passphrase_prompt_func = Prompter::plaintext_passphrase_prompt;
      plaintext_passphrase_prompt_baton = m_prompter;
    }

  /* The main disk-caching auth providers, for both
   * 'username/password' creds and 'username' creds.  */
  svn_auth_provider_object_t *provider;

  svn_auth_get_simple_provider2(&provider, plaintext_prompt_func,
      plaintext_prompt_baton, pool);
  APR_ARRAY_PUSH(providers, svn_auth_provider_object_t *) = provider;

  svn_auth_get_username_provider(&provider, pool);
  APR_ARRAY_PUSH(providers, svn_auth_provider_object_t *) = provider;

  /* The server-cert, client-cert, and client-cert-password providers. */
  SVN_JNI_ERR(
      svn_auth_get_platform_specific_provider(&provider, "windows", "ssl_server_trust", pool),
      NULL);

  if (provider)
    APR_ARRAY_PUSH(providers, svn_auth_provider_object_t *) = provider;

  svn_auth_get_ssl_server_trust_file_provider(&provider, pool);
  APR_ARRAY_PUSH(providers, svn_auth_provider_object_t *) = provider;
  svn_auth_get_ssl_client_cert_file_provider(&provider, pool);
  APR_ARRAY_PUSH(providers, svn_auth_provider_object_t *) = provider;
  svn_auth_get_ssl_client_cert_pw_file_provider2(&provider,
      plaintext_passphrase_prompt_func, plaintext_passphrase_prompt_baton,
      pool);
  APR_ARRAY_PUSH(providers, svn_auth_provider_object_t *) = provider;

  if (m_prompter != NULL)
    {
      /* Two basic prompt providers: username/password, and just username.*/
      provider = m_prompter->getProviderSimple(in_pool);

      APR_ARRAY_PUSH(providers, svn_auth_provider_object_t *) = provider;

      provider = m_prompter->getProviderUsername(in_pool);
      APR_ARRAY_PUSH(providers, svn_auth_provider_object_t *) = provider;

      /* Three ssl prompt providers, for server-certs, client-certs,
       * and client-cert-passphrases.  */
      provider = m_prompter->getProviderServerSSLTrust(in_pool);
      APR_ARRAY_PUSH(providers, svn_auth_provider_object_t *) = provider;

      provider = m_prompter->getProviderClientSSL(in_pool);
      APR_ARRAY_PUSH(providers, svn_auth_provider_object_t *) = provider;

      provider = m_prompter->getProviderClientSSLPassword(in_pool);
      APR_ARRAY_PUSH(providers, svn_auth_provider_object_t *) = provider;
    }

  /* Build an authentication baton to give to libsvn_client. */
  svn_auth_open(&ab, providers, pool);

  /* Place any default --username or --password credentials into the
   * auth_baton's run-time parameter hash.  ### Same with --no-auth-cache? */
  if (!m_userName.empty())
    svn_auth_set_parameter(ab, SVN_AUTH_PARAM_DEFAULT_USERNAME,
        apr_pstrdup(in_pool.getPool(), m_userName.c_str()));
  if (!m_passWord.empty())
    svn_auth_set_parameter(ab, SVN_AUTH_PARAM_DEFAULT_PASSWORD,
        apr_pstrdup(in_pool.getPool(), m_passWord.c_str()));
  /* Store where to retrieve authentication data? */
  if (!m_configDir.empty())
    svn_auth_set_parameter(ab, SVN_AUTH_PARAM_CONFIG_DIR,
        apr_pstrdup(in_pool.getPool(), m_configDir.c_str()));
  return ab;
}

jobject OperationContext::getSelf() const
{
  return m_jctx;
}

void
OperationContext::username(const char *pi_username)
{
  m_userName = (pi_username == NULL ? "" : pi_username);
}

void
OperationContext::password(const char *pi_password)
{
  m_passWord = (pi_password == NULL ? "" : pi_password);
}

void
OperationContext::setPrompt(Prompter *prompter)
{
  delete m_prompter;
  m_prompter = prompter;
}

void
OperationContext::setConfigDirectory(const char *configDir)
{
  // A change to the config directory may necessitate creation of
  // the config templates.
  SVN::Pool requestPool;
  SVN_JNI_ERR(svn_config_ensure(configDir, requestPool.getPool()), );

  m_configDir = (configDir == NULL ? "" : configDir);

  m_config = NULL;
}

void
OperationContext::setConfigCallback(jobject configCallback)
{
  JNIEnv* env = JNIUtil::getEnv();

  if (m_jcfgcb)
    {
      env->DeleteGlobalRef(m_jcfgcb);
      m_jcfgcb = NULL;
    }
  if (configCallback)
    {
      m_jcfgcb = env->NewGlobalRef(configCallback);
      env->DeleteLocalRef(configCallback);
    }
}

const char *
OperationContext::getConfigDirectory() const
{
  return (m_configDir.empty() ? NULL : m_configDir.c_str());
}

jobject OperationContext::getConfigCallback() const
{
  return m_jcfgcb;
}

const char *
OperationContext::getUsername() const
{
  return (m_userName.empty() ? NULL : m_userName.c_str());
}

const char *
OperationContext::getPassword() const
{
  return (m_passWord.empty() ? NULL : m_passWord.c_str());
}

const Prompter& OperationContext::getPrompter() const
{
  return *m_prompter;
}

void
OperationContext::cancelOperation()
{
  svn_atomic_set(&m_cancelOperation, 1);
}

void
OperationContext::resetCancelRequest()
{
  svn_atomic_set(&m_cancelOperation, 0);
}

bool
OperationContext::isCancelledOperation()
{
  return bool(svn_atomic_read(&m_cancelOperation));
}

svn_error_t *
OperationContext::checkCancel(void *cancelBaton)
{
  OperationContext *that = static_cast<OperationContext *>(cancelBaton);
  if (that->isCancelledOperation())
    return svn_error_create(SVN_ERR_CANCELLED, NULL, _("Operation cancelled"));
  else
    return SVN_NO_ERROR;
}

void
OperationContext::progress(apr_off_t progressVal, apr_off_t total, void *baton,
    apr_pool_t *pool)
{
  jobject jctx = (jobject) baton;
  JNIEnv *env = JNIUtil::getEnv();

  // Create a local frame for our references
  env->PushLocalFrame(LOCAL_FRAME_SIZE);
  if (JNIUtil::isJavaExceptionThrown())
    return;

  static jmethodID mid = 0;
  if (mid == 0)
    {
      jclass clazz = env->GetObjectClass(jctx);
      if (JNIUtil::isJavaExceptionThrown())
        POP_AND_RETURN_NOTHING();

      mid = env->GetMethodID(clazz, "onProgress",
          "(L"JAVA_PACKAGE"/ProgressEvent;)V");
      if (JNIUtil::isJavaExceptionThrown() || mid == 0)
        POP_AND_RETURN_NOTHING();
    }

  static jmethodID midCT = 0;
  jclass clazz = env->FindClass(JAVA_PACKAGE"/ProgressEvent");
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NOTHING();

  if (midCT == 0)
    {
      midCT = env->GetMethodID(clazz, "<init>", "(JJ)V");
      if (JNIUtil::isJavaExceptionThrown() || midCT == 0)
        POP_AND_RETURN_NOTHING();
    }

  // Call the Java method.
  jobject jevent = env->NewObject(clazz, midCT, (jlong) progressVal,
      (jlong) total);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NOTHING();

  env->CallVoidMethod(jctx, mid, jevent);

  POP_AND_RETURN_NOTHING();
}

const char *
OperationContext::getClientName() const
{
  return "javahl";
}

svn_error_t *
OperationContext::clientName(void *baton, const char **name, apr_pool_t *pool)
{
  OperationContext *that = (OperationContext *) baton;

  *name = that->getClientName();

  return SVN_NO_ERROR;
}

void
OperationContext::notifyConfigLoad()
{
  if (!m_jcfgcb)
    return;

  JNIEnv *env = JNIUtil::getEnv();

  static jmethodID onload_mid = 0;
  if (0 == onload_mid)
    {
      jclass cls = env->FindClass(JAVA_PACKAGE"/callback/ConfigEvent");
      if (JNIUtil::isJavaExceptionThrown())
        return;
      onload_mid = env->GetMethodID(cls, "onLoad",
                                    "(L"JAVA_PACKAGE"/ISVNConfig;)V");
      if (JNIUtil::isJavaExceptionThrown())
        return;
    }

  jclass cfg_cls = env->FindClass(JAVA_PACKAGE"/util/ConfigImpl");
  if (JNIUtil::isJavaExceptionThrown())
    return;

  static jmethodID ctor_mid = 0;
  if (0 == ctor_mid)
    {
      ctor_mid = env->GetMethodID(cfg_cls, "<init>", "(J)V");
      if (JNIUtil::isJavaExceptionThrown())
        return;
    }

  static jmethodID dispose_mid = 0;
  if (0 == dispose_mid)
    {
      dispose_mid = env->GetMethodID(cfg_cls, "dispose", "()V");
      if (JNIUtil::isJavaExceptionThrown())
        return;
    }

  jobject jcbimpl = env->NewObject(cfg_cls, ctor_mid,
                                   reinterpret_cast<jlong>(this));
  if (JNIUtil::isJavaExceptionThrown())
    return;
  env->CallVoidMethod(m_jcfgcb, onload_mid, jcbimpl);
  if (JNIUtil::isJavaExceptionThrown())
    return;
  env->CallVoidMethod(jcbimpl, dispose_mid);
  env->DeleteLocalRef(jcbimpl);
}
