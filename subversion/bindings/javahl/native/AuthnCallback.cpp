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
 */

#include "svn_base64.h"
#include "svn_x509.h"

#include "jniwrapper/jni_stack.hpp"
#include "jniwrapper/jni_exception.hpp"
#include "jniwrapper/jni_string.hpp"
#include "jniwrapper/jni_array.hpp"
#include "jniwrapper/jni_list.hpp"

#include "AuthnCallback.hpp"

#include "svn_private_config.h"

namespace JavaHL {

// Class JavaHL::AuthnCallback
const char* const AuthnCallback::m_class_name =
  JAVAHL_CLASS("/callback/AuthnCallback");

AuthnCallback::ClassImpl::ClassImpl(::Java::Env env, jclass cls)
  : ::Java::Object::ClassImpl(env, cls),
    m_mid_username_prompt(
        env.GetMethodID(cls, "usernamePrompt",
                        "(Ljava/lang/String;Z)"
                        JAVAHL_ARG("/callback/AuthnCallback")
                        "$UsernameResult;")),
    m_mid_user_password_prompt(
        env.GetMethodID(cls, "userPasswordPrompt",
                        "(Ljava/lang/String;Ljava/lang/String;Z)"
                        JAVAHL_ARG("/callback/AuthnCallback")
                        "$UserPasswordResult;")),
    m_mid_ssl_server_trust_prompt(
        env.GetMethodID(cls, "sslServerTrustPrompt",
                        "(Ljava/lang/String;"
                        JAVAHL_ARG("/callback/AuthnCallback")
                        "$SSLServerCertFailures;"
                        JAVAHL_ARG("/callback/AuthnCallback")
                        "$SSLServerCertInfo;"
                        "Z)"
                        JAVAHL_ARG("/callback/AuthnCallback")
                        "$SSLServerTrustResult;")),
    m_mid_ssl_client_cert_prompt(
        env.GetMethodID(cls, "sslClientCertPrompt",
                        "(Ljava/lang/String;Z)"
                        JAVAHL_ARG("/callback/AuthnCallback")
                        "$SSLClientCertResult;")),
    m_mid_ssl_client_cert_passphrase_prompt(
        env.GetMethodID(cls, "sslClientCertPassphrasePrompt",
                        "(Ljava/lang/String;Z)"
                        JAVAHL_ARG("/callback/AuthnCallback")
                        "$SSLClientCertPassphraseResult;")),
    m_mid_allow_store_plaintext_password(
        env.GetMethodID(cls, "allowStorePlaintextPassword",
                        "(Ljava/lang/String;)Z")),
    m_mid_allow_store_plaintext_passphrase(
        env.GetMethodID(cls, "allowStorePlaintextPassphrase",
                        "(Ljava/lang/String;)Z"))
{}

AuthnCallback::ClassImpl::~ClassImpl() {}

jobject AuthnCallback::username_prompt(const ::Java::String& realm,
                                       bool may_save)
{
  return m_env.CallObjectMethod(m_jthis, impl().m_mid_username_prompt,
                                realm.get(), jboolean(may_save));
}


jobject AuthnCallback::user_password_prompt(const ::Java::String& realm,
                                            const ::Java::String& username,
                                            bool may_save)
{
  return m_env.CallObjectMethod(m_jthis, impl().m_mid_user_password_prompt,
                                realm.get(), username.get(),
                                jboolean(may_save));
}

jobject AuthnCallback::ssl_server_trust_prompt(
    const ::Java::String& realm,
    const SSLServerCertFailures& failures,
    const SSLServerCertInfo& info,
    bool may_save)
{
  return m_env.CallObjectMethod(m_jthis, impl().m_mid_ssl_server_trust_prompt,
                                realm.get(), failures.get(), info.get(),
                                jboolean(may_save));
}

jobject AuthnCallback::ssl_client_cert_prompt(const ::Java::String&
                                              realm, bool may_save)
{
  return m_env.CallObjectMethod(m_jthis, impl().m_mid_ssl_client_cert_prompt,
                                realm.get(), jboolean(may_save));
}

jobject AuthnCallback::ssl_client_cert_passphrase_prompt(
    const ::Java::String& realm,
    bool may_save)
{
  return m_env.CallObjectMethod(m_jthis,
                                impl().m_mid_ssl_client_cert_passphrase_prompt,
                                realm.get(), jboolean(may_save));
}

bool AuthnCallback::allow_store_plaintext_password(const ::Java::String& realm)
{
  return m_env.CallBooleanMethod(m_jthis,
                                 impl().m_mid_allow_store_plaintext_password,
                                 realm.get());
}

bool AuthnCallback::allow_store_plaintext_passphrase(const ::Java::String& realm)
{
  return m_env.CallBooleanMethod(m_jthis,
                                 impl().m_mid_allow_store_plaintext_passphrase,
                                 realm.get());
}


// Class JavaHL::AuthnCallback::AuthnResult
const char* const AuthnCallback::AuthnResult::m_class_name =
  JAVAHL_CLASS("/callback/AuthnCallback$AuthnResult");

AuthnCallback::AuthnResult::ClassImpl::ClassImpl(::Java::Env env, jclass cls)
  : ::Java::Object::ClassImpl(env, cls),
    m_fid_save(env.GetFieldID(cls, "save", "Z")),
    m_fid_trust(env.GetFieldID(cls, "trust", "Z")),
    m_fid_identity(env.GetFieldID(cls, "identity", "Ljava/lang/String;")),
    m_fid_secret(env.GetFieldID(cls, "secret", "Ljava/lang/String;"))
{}

AuthnCallback::AuthnResult::ClassImpl::~ClassImpl() {}

// Class JavaHL::AuthnCallback::SSLServerCertFailures
const char* const AuthnCallback::SSLServerCertFailures::m_class_name =
  JAVAHL_CLASS("/callback/AuthnCallback$SSLServerCertFailures");

AuthnCallback::SSLServerCertFailures::ClassImpl::ClassImpl(
    ::Java::Env env, jclass cls)
  : ::Java::Object::ClassImpl(env, cls),
    m_mid_ctor(env.GetMethodID(cls, "<init>", "(I)V"))
{}

AuthnCallback::SSLServerCertFailures::ClassImpl::~ClassImpl() {}

AuthnCallback::SSLServerCertFailures::SSLServerCertFailures(
    ::Java::Env env, jint failures)
  : ::Java::Object(env,
                   ::Java::ClassCache::get_authn_ssl_server_cert_failures(env))
{
  set_this(env.NewObject(get_class(), impl().m_mid_ctor, failures));
}


// Class JavaHL::AuthnCallback::SSLServerCertInfo
const char* const AuthnCallback::SSLServerCertInfo::m_class_name =
  JAVAHL_CLASS("/callback/AuthnCallback$SSLServerCertInfo");

AuthnCallback::SSLServerCertInfo::ClassImpl::ClassImpl(
    ::Java::Env env, jclass cls)
  : ::Java::Object::ClassImpl(env, cls),
    m_mid_ctor(env.GetMethodID(cls, "<init>",
                               "(Ljava/lang/String;"
                               "Ljava/lang/String;JJ[B"
                               "Ljava/util/List;"
                               "Ljava/lang/String;)V"))
{}

AuthnCallback::SSLServerCertInfo::ClassImpl::~ClassImpl() {}

AuthnCallback::SSLServerCertInfo::SSLServerCertInfo(
    ::Java::Env env, const char* ascii_cert)
  : ::Java::Object(env,
                   ::Java::ClassCache::get_authn_ssl_server_cert_info(env))
{
  SVN::Pool pool;

  /* Convert header-less PEM to DER by undoing base64 encoding. */
  const svn_string_t cert_string = { ascii_cert, strlen(ascii_cert) };
  const svn_string_t* der = svn_base64_decode_string(&cert_string,
                                                     pool.getPool());

  svn_x509_certinfo_t *certinfo;
  SVN_JAVAHL_CHECK(env, svn_x509_parse_cert(&certinfo, der->data, der->len,
                                            pool.getPool(), pool.getPool()));

  const ::Java::String subject(
      env, svn_x509_certinfo_get_subject(certinfo, pool.getPool()));
  const ::Java::String issuer(
      env, svn_x509_certinfo_get_issuer(certinfo, pool.getPool()));
  const ::Java::String cert(env, ascii_cert);
  const jlong valid_from =
    (jlong(svn_x509_certinfo_get_valid_from(certinfo)) + 500) / 1000;
  const jlong valid_to =
    (jlong(svn_x509_certinfo_get_valid_to(certinfo)) + 500) / 1000;

  const svn_checksum_t* digest = svn_x509_certinfo_get_digest(certinfo);
  jsize digest_size;
  switch (digest->kind)
    {
    case svn_checksum_sha1:
      digest_size = 160 / 8;
      break;

    case svn_checksum_md5:
      digest_size = 128 / 8;
      break;

    default:
      digest_size = 0;          // Initialize this to avoid compiler warnings
      ::Java::IllegalArgumentException(env).raise(
          _("Unknown certificate digest type"));
    }
  const ::Java::ByteArray fingerprint(env, digest->digest, digest_size);

  jobject jhostnames = NULL;
  const apr_array_header_t* hostnames =
    svn_x509_certinfo_get_hostnames(certinfo);
  if (hostnames)
    {
      ::Java::List< ::Java::String> hn(env, hostnames->nelts);
      for (int i = 0; i < hostnames->nelts; ++i)
        hn.add(::Java::String(env, APR_ARRAY_IDX(hostnames, i, const char*)));
      jhostnames = hn.get();
    }

  set_this(env.NewObject(get_class(), impl().m_mid_ctor,
                         subject.get(), issuer.get(),
                         valid_from, valid_to,
                         fingerprint.get(),
                         jhostnames,
                         cert.get()));
}


// Class JavaHL::UserPasswordCallback
const char* const UserPasswordCallback::m_class_name =
  JAVAHL_CLASS("/callback/UserPasswordCallback");

UserPasswordCallback::ClassImpl::ClassImpl(::Java::Env env, jclass cls)
  : ::Java::Object::ClassImpl(env, cls),
    m_mid_ask_trust_ssl_server(
        env.GetMethodID(cls, "askTrustSSLServer",
                        "(Ljava/lang/String;Z)I")),
    m_mid_prompt_2arg(
        env.GetMethodID(cls, "prompt",
                        "(Ljava/lang/String;Ljava/lang/String;)Z")),
    m_mid_ask_yes_no(
        env.GetMethodID(cls, "askYesNo",
                        "(Ljava/lang/String;Ljava/lang/String;Z)Z")),
    m_mid_ask_question_3arg(
        env.GetMethodID(cls, "askQuestion",
                        "(Ljava/lang/String;Ljava/lang/String;Z)"
                        "Ljava/lang/String;")),
    m_mid_get_username(
        env.GetMethodID(cls, "getUsername",
                        "()Ljava/lang/String;")),
    m_mid_get_password(
        env.GetMethodID(cls, "getPassword",
                        "()Ljava/lang/String;")),
    m_mid_prompt(
        env.GetMethodID(cls, "prompt",
                        "(Ljava/lang/String;Ljava/lang/String;Z)Z")),
    m_mid_ask_question(
        env.GetMethodID(cls, "askQuestion",
                        "(Ljava/lang/String;Ljava/lang/String;ZZ)"
                        "Ljava/lang/String;")),
    m_mid_user_allowed_save(
        env.GetMethodID(cls, "userAllowedSave", "()Z"))
{}

UserPasswordCallback::ClassImpl::~ClassImpl() {}

jint UserPasswordCallback::ask_trust_ssl_server(const ::Java::String& info,
                                                bool allow_permanently)
{
  return m_env.CallIntMethod(m_jthis, impl().m_mid_ask_trust_ssl_server,
                             info.get(), jboolean(allow_permanently));
}

bool UserPasswordCallback::prompt(const ::Java::String& realm,
                                  const ::Java::String& username)
{
  return m_env.CallBooleanMethod(m_jthis, impl().m_mid_prompt_2arg,
                                 realm.get(), username.get());
}

bool UserPasswordCallback::ask_yes_no(const ::Java::String& realm,
                                      const ::Java::String& question,
                                      bool yes_is_default)
{
  return m_env.CallBooleanMethod(m_jthis, impl().m_mid_ask_yes_no,
                                 realm.get(), question.get(),
                                 jboolean(yes_is_default));
}

jstring UserPasswordCallback::ask_question(const ::Java::String& realm,
                                           const ::Java::String& question,
                                           bool show_answer)
{
  return jstring(m_env.CallObjectMethod(m_jthis,
                                        impl().m_mid_ask_question_3arg,
                                        realm.get(), question.get(),
                                        jboolean(show_answer)));
}

jstring UserPasswordCallback::get_username()
{
  return jstring(m_env.CallObjectMethod(m_jthis, impl().m_mid_get_username));
}

jstring UserPasswordCallback::get_password()
{
  return jstring(m_env.CallObjectMethod(m_jthis, impl().m_mid_get_password));
}

bool UserPasswordCallback::prompt(const ::Java::String& realm,
                                  const ::Java::String& username,
                                  bool may_save)
{
  return m_env.CallBooleanMethod(m_jthis, impl().m_mid_prompt,
                                 realm.get(), username.get(),
                                 jboolean(may_save));
}

jstring UserPasswordCallback::ask_question(const ::Java::String& realm,
                                           const ::Java::String& question,
                                           bool show_answer, bool may_save)
{
  return jstring(m_env.CallObjectMethod(m_jthis, impl().m_mid_ask_question,
                                        realm.get(), question.get(),
                                        jboolean(show_answer),
                                        jboolean(may_save)));
}

bool UserPasswordCallback::user_allowed_save()
{
  return m_env.CallBooleanMethod(m_jthis, impl().m_mid_user_allowed_save);
}

} // namespace JavaHL
