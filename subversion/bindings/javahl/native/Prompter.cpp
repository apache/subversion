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
 * @file Prompter.cpp
 * @brief Implementation of the class Prompter
 */

#include "Prompter.h"
#include "AuthnCallback.hpp"

#include "JNIUtil.h"
#include "JNIStringHolder.h"
#include "../include/org_apache_subversion_javahl_callback_UserPasswordCallback.h"

#include <apr_strings.h>
#include "svn_error.h"
#include "svn_error_codes.h"
#include "svn_private_config.h"


#include "jniwrapper/jni_stack.hpp"
#include "jniwrapper/jni_string.hpp"

// Class Prompter

Prompter::UniquePtr Prompter::create(jobject jprompter)
{
  if (!jprompter)
    return UniquePtr(NULL);

  // Make sure no C++ exceptions are propagated from here.
  const ::Java::Env jenv;
  try
    {
      const jclass cls = ::Java::ClassCache::get_authn_cb(jenv)->get_class();
      if (!jenv.IsInstanceOf(jprompter, cls))
        return UniquePtr(NULL);

      return UniquePtr(new Prompter(jenv, jprompter));
    }
  SVN_JAVAHL_JNI_CATCH;
  return UniquePtr(NULL);
}

Prompter::UniquePtr Prompter::clone() const
{
  return create(m_prompter.get());
}

Prompter::Prompter(::Java::Env env, jobject jprompter)
  : m_prompter(env, jprompter)
{}

Prompter::~Prompter() {}


svn_auth_provider_object_t *
Prompter::get_provider_simple(SVN::Pool &in_pool)
{
  apr_pool_t *pool = in_pool.getPool();
  svn_auth_provider_object_t *provider;
  svn_auth_get_simple_prompt_provider(&provider,
                                      simple_prompt,
                                      this,
                                      2, /* retry limit */
                                      pool);

  return provider;
}

svn_auth_provider_object_t *
Prompter::get_provider_username(SVN::Pool &in_pool)
{
  apr_pool_t *pool = in_pool.getPool();
  svn_auth_provider_object_t *provider;
  svn_auth_get_username_prompt_provider(&provider,
                                        username_prompt,
                                        this,
                                        2, /* retry limit */
                                        pool);

  return provider;
}

svn_auth_provider_object_t *Prompter::
get_provider_server_ssl_trust(SVN::Pool &in_pool)
{
  apr_pool_t *pool = in_pool.getPool();
  svn_auth_provider_object_t *provider;
  svn_auth_get_ssl_server_trust_prompt_provider
    (&provider, ssl_server_trust_prompt, this, pool);

  return provider;
}

svn_auth_provider_object_t *Prompter::
get_provider_client_ssl(SVN::Pool &in_pool)
{
  apr_pool_t *pool = in_pool.getPool();
  svn_auth_provider_object_t *provider;
  svn_auth_get_ssl_client_cert_prompt_provider(&provider,
                                               ssl_client_cert_prompt,
                                               this,
                                               2 /* retry limit */,
                                               pool);

  return provider;
}

svn_auth_provider_object_t *
Prompter::get_provider_client_ssl_password(SVN::Pool &in_pool)
{
  apr_pool_t *pool = in_pool.getPool();
  svn_auth_provider_object_t *provider;
  svn_auth_get_ssl_client_cert_pw_prompt_provider
    (&provider, ssl_client_cert_pw_prompt, this, 2 /* retry limit */,
     pool);

  return provider;
}

svn_error_t *Prompter::simple_prompt(
    svn_auth_cred_simple_t **cred_p,
    void *baton,
    const char *realm,
    const char *username,
    svn_boolean_t may_save,
    apr_pool_t *pool)
{
  const ::Java::Env env;
  svn_error_t *err;
  SVN_JAVAHL_CATCH(
      env, SVN_ERR_RA_NOT_AUTHORIZED,
      err = static_cast<Prompter*>(baton)->dispatch_simple_prompt(
          env, cred_p, realm, username, may_save, pool));
  return err;
}

svn_error_t *Prompter::username_prompt(
    svn_auth_cred_username_t **cred_p,
    void *baton,
    const char *realm,
    svn_boolean_t may_save,
    apr_pool_t *pool)
{
  const ::Java::Env env;
  svn_error_t *err;
  SVN_JAVAHL_CATCH(
      env, SVN_ERR_RA_NOT_AUTHORIZED,
      err = static_cast<Prompter*>(baton)->dispatch_username_prompt(
          env, cred_p, realm, may_save, pool));
  return err;
}

svn_error_t *Prompter::ssl_server_trust_prompt(
    svn_auth_cred_ssl_server_trust_t **cred_p,
    void *baton,
    const char *realm,
    apr_uint32_t failures,
    const svn_auth_ssl_server_cert_info_t *cert_info,
    svn_boolean_t may_save,
    apr_pool_t *pool)
{
  const ::Java::Env env;
  svn_error_t *err;
  SVN_JAVAHL_CATCH(
      env, SVN_ERR_RA_NOT_AUTHORIZED,
      err = static_cast<Prompter*>(baton)->dispatch_ssl_server_trust_prompt(
          env, cred_p, realm, failures, cert_info, may_save, pool));
  return err;
}

svn_error_t *Prompter::ssl_client_cert_prompt(
    svn_auth_cred_ssl_client_cert_t **cred_p,
    void *baton,
    const char *realm,
    svn_boolean_t may_save,
    apr_pool_t *pool)
{
  const ::Java::Env env;
  svn_error_t *err;
  SVN_JAVAHL_CATCH(
      env, SVN_ERR_RA_NOT_AUTHORIZED,
      err = static_cast<Prompter*>(baton)->dispatch_ssl_client_cert_prompt(
          env, cred_p, realm, may_save, pool));
  return err;
}

svn_error_t *Prompter::ssl_client_cert_pw_prompt(
    svn_auth_cred_ssl_client_cert_pw_t **cred_p,
    void *baton,
    const char *realm,
    svn_boolean_t may_save,
    apr_pool_t *pool)
{
  const ::Java::Env env;
  svn_error_t *err;
  SVN_JAVAHL_CATCH(
      env, SVN_ERR_RA_NOT_AUTHORIZED,
      err = static_cast<Prompter*>(baton)->dispatch_ssl_client_cert_pw_prompt(
          env, cred_p, realm, may_save, pool));
  return err;
}

svn_error_t *Prompter::plaintext_prompt(
    svn_boolean_t *may_save_plaintext,
    const char *realmstring,
    void *baton,
    apr_pool_t *pool)
{
  const ::Java::Env env;
  svn_error_t *err;
  SVN_JAVAHL_CATCH(
      env, SVN_ERR_RA_NOT_AUTHORIZED,
      err = static_cast<Prompter*>(baton)->dispatch_plaintext_prompt(
          env, may_save_plaintext, realmstring, pool));
  return err;
}

svn_error_t *Prompter::plaintext_passphrase_prompt(
    svn_boolean_t *may_save_plaintext,
    const char *realmstring,
    void *baton,
    apr_pool_t *pool)
{
  const ::Java::Env env;
  svn_error_t *err;
  SVN_JAVAHL_CATCH(
      env, SVN_ERR_RA_NOT_AUTHORIZED,
      err = static_cast<Prompter*>(baton)->dispatch_plaintext_passphrase_prompt(
          env, may_save_plaintext, realmstring, pool));
  return err;
}


svn_error_t *Prompter::dispatch_simple_prompt(
    ::Java::Env env,
    svn_auth_cred_simple_t **cred_p,
    const char *realm,
    const char *username,
    svn_boolean_t may_save,
    apr_pool_t *pool)
{
  ::JavaHL::AuthnCallback authn(env, m_prompter.get());

  ::JavaHL::AuthnCallback::AuthnResult result(
      env,
      authn.user_password_prompt(::Java::String(env, realm),
                                 ::Java::String(env, username),
                                 may_save));
  if (!result.get())
    return svn_error_create(SVN_ERR_RA_NOT_AUTHORIZED, NULL,
                            _("User canceled dialog"));

  ::Java::String user(env, result.identity());
  ::Java::String pass(env, result.secret());
  svn_auth_cred_simple_t *cred =
    static_cast<svn_auth_cred_simple_t*>(apr_pcalloc(pool, sizeof(*cred)));
  cred->username = user.strdup(pool);
  cred->password  = pass.strdup(pool);
  cred->may_save = result.save();
  *cred_p = cred;

  return SVN_NO_ERROR;
}

svn_error_t *Prompter::dispatch_username_prompt(
    ::Java::Env env,
    svn_auth_cred_username_t **cred_p,
    const char *realm,
    svn_boolean_t may_save,
    apr_pool_t *pool)
{
  ::JavaHL::AuthnCallback authn(env, m_prompter.get());

  ::JavaHL::AuthnCallback::AuthnResult result(
      env,
      authn.username_prompt(::Java::String(env, realm), may_save));
  if (!result.get())
    return svn_error_create(SVN_ERR_RA_NOT_AUTHORIZED, NULL,
                            _("User canceled dialog"));

  ::Java::String user(env, result.identity());
  svn_auth_cred_username_t *cred =
    static_cast<svn_auth_cred_username_t*>(apr_pcalloc(pool, sizeof(*cred)));
  cred->username = user.strdup(pool);
  cred->may_save = result.save();
  *cred_p = cred;

  return SVN_NO_ERROR;
}

svn_error_t *Prompter::dispatch_ssl_server_trust_prompt(
    ::Java::Env env,
    svn_auth_cred_ssl_server_trust_t **cred_p,
    const char *realm,
    apr_uint32_t failures,
    const svn_auth_ssl_server_cert_info_t *cert_info,
    svn_boolean_t may_save,
    apr_pool_t *pool)
{
  ::JavaHL::AuthnCallback authn(env, m_prompter.get());

  ::JavaHL::AuthnCallback::AuthnResult result(
      env,
      authn.ssl_server_trust_prompt(
          ::Java::String(env, realm),
          ::JavaHL::AuthnCallback::SSLServerCertFailures(env, jint(failures)),
          ::JavaHL::AuthnCallback::SSLServerCertInfo(env, cert_info->ascii_cert),
          may_save));
  if (!result.get())
    return svn_error_create(SVN_ERR_RA_NOT_AUTHORIZED, NULL,
                            _("User canceled dialog"));

  const bool trust = result.trust();
  if (!trust)
    {
      *cred_p = NULL;
      return SVN_NO_ERROR;
    }

  const bool save = result.save();
  svn_auth_cred_ssl_server_trust_t *cred =
    static_cast<svn_auth_cred_ssl_server_trust_t*>(apr_pcalloc(pool, sizeof(*cred)));
  cred->may_save = save;
  cred->accepted_failures = failures;
  *cred_p = cred;

  return SVN_NO_ERROR;
}

svn_error_t *Prompter::dispatch_ssl_client_cert_prompt(
    ::Java::Env env,
    svn_auth_cred_ssl_client_cert_t **cred_p,
    const char *realm,
    svn_boolean_t may_save,
    apr_pool_t *pool)
{
  ::JavaHL::AuthnCallback authn(env, m_prompter.get());

  ::JavaHL::AuthnCallback::AuthnResult result(
      env,
      authn.ssl_client_cert_prompt(::Java::String(env, realm), may_save));
  if (!result.get())
    return svn_error_create(SVN_ERR_RA_NOT_AUTHORIZED, NULL,
                            _("User canceled dialog"));

  ::Java::String path(env, result.identity());
  svn_auth_cred_ssl_client_cert_t *cred =
    static_cast<svn_auth_cred_ssl_client_cert_t*>(apr_pcalloc(pool, sizeof(*cred)));
  cred->cert_file = path.strdup(pool);
  cred->may_save = result.save();
  *cred_p = cred;

  return SVN_NO_ERROR;
}

svn_error_t *Prompter::dispatch_ssl_client_cert_pw_prompt(
    ::Java::Env env,
    svn_auth_cred_ssl_client_cert_pw_t **cred_p,
    const char *realm,
    svn_boolean_t may_save,
    apr_pool_t *pool)
{
  ::JavaHL::AuthnCallback authn(env, m_prompter.get());

  ::JavaHL::AuthnCallback::AuthnResult result(
      env,
      authn.ssl_client_cert_passphrase_prompt(
          ::Java::String(env, realm), may_save));
  if (!result.get())
    return svn_error_create(SVN_ERR_RA_NOT_AUTHORIZED, NULL,
                            _("User canceled dialog"));

  ::Java::String passphrase(env, result.secret());
  svn_auth_cred_ssl_client_cert_pw_t *cred =
    static_cast<svn_auth_cred_ssl_client_cert_pw_t*>(apr_pcalloc(pool, sizeof(*cred)));
  cred->password = passphrase.strdup(pool);
  cred->may_save = result.save();
  *cred_p = cred;

  return SVN_NO_ERROR;
}

svn_error_t *Prompter::dispatch_plaintext_prompt(
    ::Java::Env env,
    svn_boolean_t *may_save_plaintext,
    const char *realmstring,
    apr_pool_t *pool)
{
  ::JavaHL::AuthnCallback authn(env, m_prompter.get());
  *may_save_plaintext =
    authn.allow_store_plaintext_password(::Java::String(env, realmstring));
  return SVN_NO_ERROR;
}

svn_error_t *Prompter::dispatch_plaintext_passphrase_prompt(
    ::Java::Env env,
    svn_boolean_t *may_save_plaintext,
    const char *realmstring,
    apr_pool_t *pool)
{
  ::JavaHL::AuthnCallback authn(env, m_prompter.get());
  *may_save_plaintext =
    authn.allow_store_plaintext_passphrase(::Java::String(env, realmstring));
  return SVN_NO_ERROR;
}


// Class CompatPrompter

Prompter::UniquePtr CompatPrompter::create(jobject jprompter)
{
  if (!jprompter)
    return UniquePtr(NULL);

  // Make sure no C++ exceptions are propagated from here.
  const ::Java::Env jenv;
  try
    {
      const jclass cls =
        ::Java::ClassCache::get_user_passwd_cb(jenv)->get_class();
      if (!jenv.IsInstanceOf(jprompter, cls))
        return UniquePtr(NULL);

      return UniquePtr(new CompatPrompter(jenv, jprompter));
    }
  SVN_JAVAHL_JNI_CATCH;
  return UniquePtr(NULL);
}

Prompter::UniquePtr CompatPrompter::clone() const
{
  return create(m_prompter.get());
}

CompatPrompter::CompatPrompter(::Java::Env env, jobject jprompter)
  : Prompter(env, jprompter)
{}

CompatPrompter::~CompatPrompter() {}

namespace {
jstring compat_ask_question(
    bool& allowed_save,
    ::Java::Env env,
    ::JavaHL::UserPasswordCallback& authn,
    const char *realm, const char *question,
    bool show_answer, bool may_save)
{
  const jstring janswer =
    authn.ask_question(::Java::String(env, realm),
                       ::Java::String(env, question),
                       show_answer, may_save);

  if (janswer)
    allowed_save = authn.user_allowed_save();
  else
    allowed_save = false;

  return janswer;
}
} // anonymous namespace

svn_error_t *CompatPrompter::dispatch_simple_prompt(
    ::Java::Env env,
    svn_auth_cred_simple_t **cred_p,
    const char *realm, const char *username,
    svn_boolean_t may_save,
    apr_pool_t *pool)
{
  ::JavaHL::UserPasswordCallback authn(env, m_prompter.get());

  if (!authn.prompt(::Java::String(env, realm),
                    ::Java::String(env, username),
                    may_save))
    return svn_error_create(SVN_ERR_RA_NOT_AUTHORIZED, NULL,
                            _("User canceled dialog"));
  const ::Java::String user(env, authn.get_username());
  const ::Java::String pass(env, authn.get_password());

  if (!user.get() || !pass.get())
    return svn_error_create(SVN_ERR_RA_NOT_AUTHORIZED, NULL,
                            _("User canceled dialog"));

  svn_auth_cred_simple_t *cred =
    static_cast<svn_auth_cred_simple_t*>(apr_pcalloc(pool, sizeof(*cred)));
  cred->username = user.strdup(pool);
  cred->password  = pass.strdup(pool);
  cred->may_save = authn.user_allowed_save();
  *cred_p = cred;

  return SVN_NO_ERROR;
}

svn_error_t *CompatPrompter::dispatch_username_prompt(
    ::Java::Env env,
    svn_auth_cred_username_t **cred_p,
    const char *realm,
    svn_boolean_t may_save,
    apr_pool_t *pool)
{
  ::JavaHL::UserPasswordCallback authn(env, m_prompter.get());

  bool allowed_save;
  const ::Java::String user(
      env,
      compat_ask_question(allowed_save, env, authn,
                          realm, _("Username: "), true, may_save));
  if (!user.get())
    return svn_error_create(SVN_ERR_RA_NOT_AUTHORIZED, NULL,
                            _("User canceled dialog"));

  svn_auth_cred_username_t *cred =
    static_cast<svn_auth_cred_username_t*>(apr_pcalloc(pool, sizeof(*cred)));
  cred->username = user.strdup(pool);
  cred->may_save = allowed_save;
  *cred_p = cred;

  return SVN_NO_ERROR;
}

svn_error_t *
CompatPrompter::dispatch_ssl_server_trust_prompt(
    ::Java::Env env,
    svn_auth_cred_ssl_server_trust_t **cred_p,
    const char *realm,
    apr_uint32_t failures,
    const svn_auth_ssl_server_cert_info_t *cert_info,
    svn_boolean_t may_save,
    apr_pool_t *pool)
{
  ::JavaHL::UserPasswordCallback authn(env, m_prompter.get());

  std::string question = _("Error validating server certificate for ");
  question += realm;
  question += ":\n";

  if (failures & SVN_AUTH_SSL_UNKNOWNCA)
    {
      question += _(" - Unknown certificate issuer\n");
      question += _("   Fingerprint: ");
      question += cert_info->fingerprint;
      question += "\n";
      question += _("   Distinguished name: ");
      question += cert_info->issuer_dname;
      question += "\n";
    }

  if (failures & SVN_AUTH_SSL_CNMISMATCH)
    {
      question += _(" - Hostname mismatch (");
      question += cert_info->hostname;
      question += _(")\n");
    }

  if (failures & SVN_AUTH_SSL_NOTYETVALID)
    {
      question += _(" - Certificate is not yet valid\n");
      question += _("   Valid from ");
      question += cert_info->valid_from;
      question += "\n";
    }

  if (failures & SVN_AUTH_SSL_EXPIRED)
    {
      question += _(" - Certificate is expired\n");
      question += _("   Valid until ");
      question += cert_info->valid_until;
      question += "\n";
    }

  svn_auth_cred_ssl_server_trust_t *cred =
    static_cast<svn_auth_cred_ssl_server_trust_t*>(apr_pcalloc(pool, sizeof(*cred)));

  switch (authn.ask_trust_ssl_server(::Java::String(env, question), may_save))
    {
    case org_apache_subversion_javahl_callback_UserPasswordCallback_AcceptTemporary:
      cred->may_save = FALSE;
      cred->accepted_failures = failures;
      *cred_p = cred;
      break;
    case org_apache_subversion_javahl_callback_UserPasswordCallback_AcceptPermanently:
      cred->may_save = TRUE;
      cred->accepted_failures = failures;
      *cred_p = cred;
      break;
    default:
      *cred_p = NULL;
    }
  return SVN_NO_ERROR;
}

svn_error_t *
CompatPrompter::dispatch_ssl_client_cert_prompt(
    ::Java::Env env,
    svn_auth_cred_ssl_client_cert_t **cred_p,
    const char *realm,
    svn_boolean_t may_save,
    apr_pool_t *pool)
{
  ::JavaHL::UserPasswordCallback authn(env, m_prompter.get());

  bool allowed_save;
  const ::Java::String path(
      env,
      compat_ask_question(allowed_save, env, authn, realm,
                          _("Client certificate filename: "),
                          true, may_save));
  if (!path.get())
    return svn_error_create(SVN_ERR_RA_NOT_AUTHORIZED, NULL,
                            _("User canceled dialog"));

  svn_auth_cred_ssl_client_cert_t *cred =
    static_cast<svn_auth_cred_ssl_client_cert_t*>(apr_pcalloc(pool, sizeof(*cred)));
  cred->cert_file = path.strdup(pool);
  cred->may_save = allowed_save;
  *cred_p = cred;
  return SVN_NO_ERROR;
}

svn_error_t *
CompatPrompter::dispatch_ssl_client_cert_pw_prompt(
    ::Java::Env env,
    svn_auth_cred_ssl_client_cert_pw_t **cred_p,
    const char *realm,
    svn_boolean_t may_save,
    apr_pool_t *pool)
{
  ::JavaHL::UserPasswordCallback authn(env, m_prompter.get());

  bool allowed_save;
  const ::Java::String info(
      env,
      compat_ask_question(allowed_save, env, authn, realm,
                          _("Client certificate passphrase: "),
                          false, may_save));
  if (!info.get())
    return svn_error_create(SVN_ERR_RA_NOT_AUTHORIZED, NULL,
                            _("User canceled dialog"));

  svn_auth_cred_ssl_client_cert_pw_t *cred =
    static_cast<svn_auth_cred_ssl_client_cert_pw_t*>(apr_pcalloc(pool, sizeof(*cred)));
  cred->password = info.strdup(pool);
  cred->may_save = allowed_save;
  *cred_p = cred;
  return SVN_NO_ERROR;
}

svn_error_t *
CompatPrompter::dispatch_plaintext_prompt(
    ::Java::Env env,
    svn_boolean_t *may_save_plaintext,
    const char *realmstring,
    apr_pool_t *pool)
{
  ::JavaHL::UserPasswordCallback authn(env, m_prompter.get());

  *may_save_plaintext = authn.ask_yes_no(
      ::Java::String(env, realmstring),
      ::Java::String(env, _("Store password unencrypted?")),
      false);

  return SVN_NO_ERROR;
}

svn_error_t *
CompatPrompter::dispatch_plaintext_passphrase_prompt(
    ::Java::Env env,
    svn_boolean_t *may_save_plaintext,
    const char *realmstring,
    apr_pool_t *pool)
{
  ::JavaHL::UserPasswordCallback authn(env, m_prompter.get());

  *may_save_plaintext = authn.ask_yes_no(
      ::Java::String(env, realmstring),
      ::Java::String(env, _("Store passphrase unencrypted?")),
      false);

  return SVN_NO_ERROR;
}
