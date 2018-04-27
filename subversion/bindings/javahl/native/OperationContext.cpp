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

#include <apr_file_io.h>

#include "GlobalConfig.h"
#include "OperationContext.h"
#include "JNIUtil.h"
#include "JNICriticalSection.h"

#include "Prompter.h"
#include "CreateJ.h"
#include "EnumMapper.h"
#include "CommitMessage.h"

#include "svn_client.h"
#include "private/svn_wc_private.h"
#include "private/svn_dep_compat.h"
#include "svn_private_config.h"

OperationContext::OperationContext(SVN::Pool &pool)
  : m_config(NULL),
    m_prompter(NULL),
    m_cancelOperation(0),
    m_pool(&pool),
    m_jctx(NULL),
    m_jcfgcb(NULL),
    m_jtunnelcb(NULL)
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
  JNIEnv *env = JNIUtil::getEnv();
  env->DeleteGlobalRef(m_jctx);
  if (m_jcfgcb)
    env->DeleteGlobalRef(m_jcfgcb);
  if (m_jtunnelcb)
    env->DeleteGlobalRef(m_jtunnelcb);
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

  svn_config_t *config = static_cast<svn_config_t *>(apr_hash_get(configData,
      SVN_CONFIG_CATEGORY_CONFIG, APR_HASH_KEY_STRING));

  const bool use_native_store = GlobalConfig::useNativeCredentialsStore();

  /* The whole list of registered providers */
  apr_array_header_t *providers;
  svn_auth_provider_object_t *provider;

  if (use_native_store)
    {
      /* Populate the registered providers with the platform-specific providers */
      SVN_JNI_ERR(
          svn_auth_get_platform_specific_client_providers(
              &providers, config, pool),
          NULL);

      /* Use the prompter (if available) to prompt for password and cert
       * caching. */
      svn_auth_plaintext_prompt_func_t plaintext_prompt_func;
      void *plaintext_prompt_baton;
      svn_auth_plaintext_passphrase_prompt_func_t plaintext_passphrase_prompt_func;
      void *plaintext_passphrase_prompt_baton;

      if (m_prompter.get())
        {
          plaintext_prompt_func = Prompter::plaintext_prompt;
          plaintext_prompt_baton = m_prompter.get();
          plaintext_passphrase_prompt_func = Prompter::plaintext_passphrase_prompt;
          plaintext_passphrase_prompt_baton = m_prompter.get();
        }
      else
        {
          plaintext_prompt_func = NULL;
          plaintext_prompt_baton = NULL;
          plaintext_passphrase_prompt_func = NULL;
          plaintext_passphrase_prompt_baton = NULL;
        }

      /* The main disk-caching auth providers, for both
       * 'username/password' creds and 'username' creds.  */

      svn_auth_get_simple_provider2(&provider, plaintext_prompt_func,
                                    plaintext_prompt_baton, pool);
      APR_ARRAY_PUSH(providers, svn_auth_provider_object_t *) = provider;

      svn_auth_get_username_provider(&provider, pool);
      APR_ARRAY_PUSH(providers, svn_auth_provider_object_t *) = provider;

      svn_auth_get_ssl_server_trust_file_provider(&provider, pool);
      APR_ARRAY_PUSH(providers, svn_auth_provider_object_t *) = provider;
      svn_auth_get_ssl_client_cert_file_provider(&provider, pool);
      APR_ARRAY_PUSH(providers, svn_auth_provider_object_t *) = provider;
      svn_auth_get_ssl_client_cert_pw_file_provider2(
          &provider,
          plaintext_passphrase_prompt_func, plaintext_passphrase_prompt_baton,
          pool);
      APR_ARRAY_PUSH(providers, svn_auth_provider_object_t *) = provider;
    }
  else
    {
      // Not using hte native credentials store, start with an empty
      // providers array.
      providers = apr_array_make(pool, 0, sizeof(svn_auth_provider_object_t *));
    }

  if (m_prompter.get())
    {
      /* Two basic prompt providers: username/password, and just username.*/
      provider = m_prompter->get_provider_simple(in_pool);
      APR_ARRAY_PUSH(providers, svn_auth_provider_object_t *) = provider;

      provider = m_prompter->get_provider_username(in_pool);
      APR_ARRAY_PUSH(providers, svn_auth_provider_object_t *) = provider;

      /* Three ssl prompt providers, for server-certs, client-certs,
       * and client-cert-passphrases.  */
      provider = m_prompter->get_provider_server_ssl_trust(in_pool);
      APR_ARRAY_PUSH(providers, svn_auth_provider_object_t *) = provider;

      provider = m_prompter->get_provider_client_ssl(in_pool);
      APR_ARRAY_PUSH(providers, svn_auth_provider_object_t *) = provider;

      provider = m_prompter->get_provider_client_ssl_password(in_pool);
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
OperationContext::setPrompt(Prompter::UniquePtr prompter)
{
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

const char *
OperationContext::getConfigDirectory() const
{
  return (m_configDir.empty() ? NULL : m_configDir.c_str());
}

void OperationContext::setConfigEventHandler(jobject jcfgcb)
{
  JNIEnv *env = JNIUtil::getEnv();
  if (jcfgcb)
    {
      jcfgcb = env->NewGlobalRef(jcfgcb);
      if (JNIUtil::isJavaExceptionThrown())
        return;
    }

  if (m_jcfgcb)
    env->DeleteGlobalRef(m_jcfgcb);
  m_jcfgcb = jcfgcb;
}

jobject OperationContext::getConfigEventHandler() const
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

Prompter::UniquePtr OperationContext::clonePrompter() const
{
  if (m_prompter.get())
    return m_prompter->clone();
  return Prompter::UniquePtr(NULL);
}

void OperationContext::setTunnelCallback(jobject jtunnelcb)
{
  JNIEnv *env = JNIUtil::getEnv();
  if (jtunnelcb)
    {
      jtunnelcb = env->NewGlobalRef(jtunnelcb);
      if (JNIUtil::isJavaExceptionThrown())
        return;
    }

  if (m_jtunnelcb)
    env->DeleteGlobalRef(m_jtunnelcb);
  m_jtunnelcb = jtunnelcb;
}

jobject OperationContext::getTunnelCallback() const
{
  return m_jtunnelcb;
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
  else if (JNIUtil::isJavaExceptionThrown())
    return svn_error_create(SVN_ERR_CANCELLED, JNIUtil::wrapJavaException(),
                            _("Operation cancelled"));
  else
    return SVN_NO_ERROR;
}

void
OperationContext::progress(apr_off_t progressVal, apr_off_t total, void *baton,
    apr_pool_t *pool)
{
  jobject jctx = (jobject) baton;
  if (!jctx)
    return;

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
                             "(" JAVAHL_ARG("/ProgressEvent;") ")V");
      if (JNIUtil::isJavaExceptionThrown() || mid == 0)
        POP_AND_RETURN_NOTHING();
    }

  static jmethodID midCT = 0;
  jclass clazz = env->FindClass(JAVAHL_CLASS("/ProgressEvent"));
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
      jclass cls = env->FindClass(JAVAHL_CLASS("/callback/ConfigEvent"));
      if (JNIUtil::isJavaExceptionThrown())
        return;
      onload_mid = env->GetMethodID(cls, "onLoad",
                                    "(" JAVAHL_ARG("/ISVNConfig;") ")V");
      if (JNIUtil::isJavaExceptionThrown())
        return;
    }

  jclass cfg_cls = env->FindClass(JAVAHL_CLASS("/util/ConfigImpl"));
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

namespace {
class TunnelContext
{
public:
  explicit TunnelContext(apr_pool_t *pool)
    : request_in(NULL),
      request_out(NULL),
      response_in(NULL),
      response_out(NULL),
      jclosecb(NULL)
    {
      status = apr_file_pipe_create_ex(&request_in, &request_out,
                                       APR_FULL_BLOCK, pool);
      if (!status)
        status = apr_file_pipe_create_ex(&response_in, &response_out,
                                         APR_FULL_BLOCK, pool);
    }

  ~TunnelContext()
    {
      apr_file_close(request_out);
      apr_file_close(response_in);
    }

  apr_file_t *request_in;
  apr_file_t *request_out;
  apr_file_t *response_in;
  apr_file_t *response_out;
  apr_status_t status;
  jobject jclosecb;
};

jobject create_Channel(const char *class_name, JNIEnv *env, apr_file_t *fd)
{
  jclass cls = env->FindClass(class_name);
  if (JNIUtil::isJavaExceptionThrown())
    return NULL;
  jmethodID ctor = env->GetMethodID(cls, "<init>", "(J)V");
  if (JNIUtil::isJavaExceptionThrown())
    return NULL;
  return env->NewObject(cls, ctor, reinterpret_cast<jlong>(fd));
}

jobject create_RequestChannel(JNIEnv *env, apr_file_t *fd)
{
  return create_Channel(JAVAHL_CLASS("/util/RequestChannel"), env, fd);
}
jobject create_ResponseChannel(JNIEnv *env, apr_file_t *fd)
{
  return create_Channel(JAVAHL_CLASS("/util/ResponseChannel"), env, fd);
}
} // anonymous namespace

svn_boolean_t
OperationContext::checkTunnel(void *tunnel_baton, const char *tunnel_name)
{
  JNIEnv *env = JNIUtil::getEnv();

  jstring jtunnel_name = JNIUtil::makeJString(tunnel_name);
  if (JNIUtil::isJavaExceptionThrown())
    return false;

  static jmethodID mid = 0;
  if (0 == mid)
    {
      jclass cls = env->FindClass(JAVAHL_CLASS("/callback/TunnelAgent"));
      if (JNIUtil::isJavaExceptionThrown())
        return false;
      mid = env->GetMethodID(cls, "checkTunnel",
                             "(Ljava/lang/String;)Z");
        if (JNIUtil::isJavaExceptionThrown())
          return false;
    }

  jobject jtunnelcb = jobject(tunnel_baton);
  jboolean check = env->CallBooleanMethod(jtunnelcb, mid, jtunnel_name);
  if (JNIUtil::isJavaExceptionThrown())
    return false;

  return svn_boolean_t(check);
}

svn_error_t *
OperationContext::openTunnel(svn_stream_t **request, svn_stream_t **response,
                             svn_ra_close_tunnel_func_t *close_func,
                             void **close_baton,
                             void *tunnel_baton,
                             const char *tunnel_name, const char *user,
                             const char *hostname, int port,
                             svn_cancel_func_t cancel_func, void *cancel_baton,
                             apr_pool_t *pool)
{
  TunnelContext *tc = new TunnelContext(pool);
  if (tc->status)
    {
      delete tc;
      return svn_error_trace(
          svn_error_wrap_apr(tc->status, _("Could not open tunnel streams")));
    }

  *close_func = closeTunnel;
  *close_baton = tc;
  *request = svn_stream_from_aprfile2(tc->request_out, FALSE, pool);
  *response = svn_stream_from_aprfile2(tc->response_in, FALSE, pool);

  JNIEnv *env = JNIUtil::getEnv();

  jobject jrequest = create_RequestChannel(env, tc->request_in);
  SVN_JNI_CATCH(, SVN_ERR_BASE);

  jobject jresponse = create_ResponseChannel(env, tc->response_out);
  SVN_JNI_CATCH(, SVN_ERR_BASE);

  jstring jtunnel_name = JNIUtil::makeJString(tunnel_name);
  SVN_JNI_CATCH(, SVN_ERR_BASE);

  jstring juser = JNIUtil::makeJString(user);
  SVN_JNI_CATCH(, SVN_ERR_BASE);

  jstring jhostname = JNIUtil::makeJString(hostname);
  SVN_JNI_CATCH(, SVN_ERR_BASE);

  static jmethodID mid = 0;
  if (0 == mid)
    {
      jclass cls = env->FindClass(JAVAHL_CLASS("/callback/TunnelAgent"));
      SVN_JNI_CATCH(, SVN_ERR_BASE);
      SVN_JNI_CATCH(
          mid = env->GetMethodID(
              cls, "openTunnel",
              "(Ljava/nio/channels/ReadableByteChannel;"
              "Ljava/nio/channels/WritableByteChannel;"
              "Ljava/lang/String;"
              "Ljava/lang/String;"
              "Ljava/lang/String;I)"
              JAVAHL_ARG("/callback/TunnelAgent$CloseTunnelCallback;")),
          SVN_ERR_BASE);
    }

  jobject jtunnelcb = jobject(tunnel_baton);
  SVN_JNI_CATCH(
      tc->jclosecb = env->CallObjectMethod(
          jtunnelcb, mid, jrequest, jresponse,
          jtunnel_name, juser, jhostname, jint(port)),
      SVN_ERR_BASE);

  return SVN_NO_ERROR;
}

void
OperationContext::closeTunnel(void *tunnel_context, void *)
{
  TunnelContext* tc = static_cast<TunnelContext*>(tunnel_context);
  jobject jclosecb = tc->jclosecb;
  delete tc;

  if (!jclosecb)
    return;

  JNIEnv *env = JNIUtil::getEnv();
  if (JNIUtil::isJavaExceptionThrown())
    return;

  static jmethodID mid = 0;
  if (0 == mid)
    {
      jclass cls;
      SVN_JNI_CATCH_VOID(
          cls= env->FindClass(
              JAVAHL_CLASS("/callback/TunnelAgent$CloseTunnelCallback")));
      SVN_JNI_CATCH_VOID(mid = env->GetMethodID(cls, "closeTunnel", "()V"));
    }
  SVN_JNI_CATCH_VOID(env->CallVoidMethod(jclosecb, mid));
}
