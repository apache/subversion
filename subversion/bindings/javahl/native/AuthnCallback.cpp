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

#include "jniwrapper/jni_stack.hpp"

#include "AuthnCallback.hpp"

namespace JavaHL {

// Class JavaHL::AuthnCallback
const char* const AuthnCallback::m_class_name =
  JAVA_PACKAGE"/callback/AuthnCallback";

::Java::MethodID AuthnCallback::m_mid_username_prompt;
::Java::MethodID AuthnCallback::m_mid_user_password_prompt;
::Java::MethodID AuthnCallback::m_mid_ssl_server_trust_prompt;
::Java::MethodID AuthnCallback::m_mid_ssl_client_cert_prompt;
::Java::MethodID AuthnCallback::m_mid_ssl_client_cert_passphrase_prompt;
::Java::MethodID AuthnCallback::m_mid_allow_store_plaintext_password;
::Java::MethodID AuthnCallback::m_mid_allow_store_plaintext_passphrase;

void AuthnCallback::static_init(::Java::Env env)
{
  const jclass cls = ::Java::ClassCache::get_authn_cb();

  m_mid_username_prompt =
    env.GetMethodID(cls, "usernamePrompt",
                    "(Ljava/lang/String;Z)"
                    "L"JAVA_PACKAGE"/callback/AuthnCallback"
                    "$UsernameResult");

  m_mid_user_password_prompt =
    env.GetMethodID(cls, "userPasswordPrompt",
                    "(Ljava/lang/String;"
                    "L"JAVA_PACKAGE"/callback/AuthnCallback"
                    "$SSLServerCertFailures"
                    "L"JAVA_PACKAGE"/callback/AuthnCallback"
                    "$SSLServerCertInfo"
                    "Z)"
                    "L"JAVA_PACKAGE"/callback/AuthnCallback"
                    "$SSLServerTrustResult");

  m_mid_ssl_server_trust_prompt =
    env.GetMethodID(cls, "sslServerTrustPrompt",
                    "(Ljava/lang/String;Ljava/lang/String;Z)"
                    "L"JAVA_PACKAGE"/callback/AuthnCallback"
                    "$UserPasswordResult");

  m_mid_ssl_client_cert_prompt =
    env.GetMethodID(cls, "sslClientCertPrompt",
                    "(Ljava/lang/String;Z)"
                    "L"JAVA_PACKAGE"/callback/AuthnCallback"
                    "$SSLClientCertResult");

  m_mid_ssl_client_cert_passphrase_prompt =
    env.GetMethodID(cls, "sslClientCertPassphrasePrompt",
                    "(Ljava/lang/String;Z)"
                    "L"JAVA_PACKAGE"/callback/AuthnCallback"
                    "$SSLClientCertPassphraseResult");

  m_mid_allow_store_plaintext_password =
    env.GetMethodID(cls, "allowStorePlaintextPassword",
                    "(Ljava/lang/String;)Z");

  m_mid_allow_store_plaintext_passphrase =
    env.GetMethodID(cls, "allowStorePlaintextPassphrase",
                    "(Ljava/lang/String;)Z");
}


// Class JavaHL::AuthnCallback::AuthnResult
const char* const AuthnCallback::AuthnResult::m_class_name =
  JAVA_PACKAGE"/callback/AuthnCallback$AuthnResult";

::Java::FieldID AuthnCallback::AuthnResult::m_fid_save;
::Java::FieldID AuthnCallback::AuthnResult::m_fid_trust;
::Java::FieldID AuthnCallback::AuthnResult::m_fid_identity;
::Java::FieldID AuthnCallback::AuthnResult::m_fid_secret;

void AuthnCallback::AuthnResult::static_init(::Java::Env env)
{
  const jclass cls = ::Java::ClassCache::get_authn_result();

  m_fid_save = env.GetFieldID(cls, "save", "Z");
  m_fid_trust = env.GetFieldID(cls, "trust", "Z");
  m_fid_identity = env.GetFieldID(cls, "identity", "Ljava/lang/String;");
  m_fid_secret = env.GetFieldID(cls, "secret", "Ljava/lang/String;");
}


// Class JavaHL::AuthnCallback::SSLServerCertFailures
const char* const AuthnCallback::SSLServerCertFailures::m_class_name =
  JAVA_PACKAGE"/callback/AuthnCallback$SSLServerCertFailures";

::Java::MethodID AuthnCallback::SSLServerCertFailures::m_mid_ctor;

void AuthnCallback::SSLServerCertFailures::static_init(::Java::Env env)
{
  const jclass cls = ::Java::ClassCache::get_authn_ssl_server_cert_failures();

  m_mid_ctor = env.GetMethodID(cls, "<init>", "(I)V");
}

AuthnCallback::SSLServerCertFailures::SSLServerCertFailures(
    ::Java::Env env, jint failures)
  : ::Java::Object(
      env,
      env.NewObject(::Java::ClassCache::get_authn_ssl_server_cert_failures(),
                    m_mid_ctor, failures))
{}


// Class JavaHL::AuthnCallback::SSLServerCertInfo
const char* const AuthnCallback::SSLServerCertInfo::m_class_name =
  JAVA_PACKAGE"/callback/AuthnCallback$SSLServerCertFailures";

::Java::MethodID AuthnCallback::SSLServerCertInfo::m_mid_ctor;

void AuthnCallback::SSLServerCertInfo::static_init(::Java::Env env)
{
  const jclass cls = ::Java::ClassCache::get_authn_ssl_server_cert_info();

  m_mid_ctor = env.GetMethodID(cls, "<init>",
                               "(Ljava/lang/String;"
                               "Ljava/lang/String;"
                               "Ljava/lang/String;"
                               "Ljava/lang/String;"
                               "Ljava/lang/String;"
                               "Ljava/lang/String;)V");
}

AuthnCallback::SSLServerCertInfo::SSLServerCertInfo(
    ::Java::Env env,
    const ::Java::String& hostname,
    const ::Java::String& fingerprint,
    const ::Java::String& validFrom,
    const ::Java::String& validUntil,
    const ::Java::String& issuer,
    const ::Java::String& der)
  : ::Java::Object(
      env,
      env.NewObject(::Java::ClassCache::get_authn_ssl_server_cert_failures(),
                    m_mid_ctor, hostname.get(), fingerprint.get(),
                    validFrom.get(), validUntil.get(),
                    issuer.get(), der.get()))
{}

} // namespace JavaHL
