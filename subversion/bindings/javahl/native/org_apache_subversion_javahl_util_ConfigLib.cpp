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
 * @file org_apache_subversion_javahl_util_ConfigLib.cpp
 * @brief Implementation of the native methods in the Java class ConfigLib
 */

#include "../include/org_apache_subversion_javahl_util_ConfigLib.h"

#include <apr_fnmatch.h>
#include <apr_strings.h>

#include "jniwrapper/jni_list.hpp"
#include "jniwrapper/jni_stack.hpp"

#include "AuthnCallback.hpp"
#include "Credential.hpp"
#include "SubversionException.hpp"

#include "GlobalConfig.h"
#include "JNIUtil.h"
#include "JNICriticalSection.h"

#include "svn_auth.h"
#include "svn_base64.h"
#include "svn_config.h"
#include "svn_hash.h"
#include "svn_x509.h"

#include "svn_private_config.h"

namespace {
bool g_ignore_native_credentials = false;
} // anonymous namespace

bool GlobalConfig::useNativeCredentialsStore()
{
  JNICriticalSection lock(*JNIUtil::g_configMutex);
  return !g_ignore_native_credentials;
}

JNIEXPORT void JNICALL
Java_org_apache_subversion_javahl_util_ConfigLib_enableNativeCredentialsStore(
    JNIEnv* jenv, jobject jthis)
{
  SVN_JAVAHL_JNI_TRY(ConfigLib, enableNativeCredentialsStore)
    {
      JNICriticalSection lock(*JNIUtil::g_configMutex);
      g_ignore_native_credentials = false;
    }
  SVN_JAVAHL_JNI_CATCH;
}

JNIEXPORT void JNICALL
Java_org_apache_subversion_javahl_util_ConfigLib_disableNativeCredentialsStore(
    JNIEnv* jenv, jobject jthis)
{
  SVN_JAVAHL_JNI_TRY(ConfigLib, disableNativeCredentialsStore)
    {
      JNICriticalSection lock(*JNIUtil::g_configMutex);
      g_ignore_native_credentials = true;
    }
  SVN_JAVAHL_JNI_CATCH;
}

JNIEXPORT jboolean JNICALL
Java_org_apache_subversion_javahl_util_ConfigLib_isNativeCredentialsStoreEnabled(
    JNIEnv* jenv, jobject jthis)
{
  SVN_JAVAHL_JNI_TRY(ConfigLib, isNativeCredentialsStoreEnabled)
    {
      return jboolean(GlobalConfig::useNativeCredentialsStore());
    }
  SVN_JAVAHL_JNI_CATCH;
  return JNI_FALSE;
}


namespace {
jobject build_credential(Java::Env env, apr_hash_t* cred,
                         const char* cred_kind, const char* realm,
                         apr_pool_t* scratch_pool)
{
  svn_string_t* entry = static_cast<svn_string_t*>(
      svn_hash_gets(cred, SVN_CONFIG_REALMSTRING_KEY));
  if (!entry || !realm || 0 != strcmp(realm, entry->data))
    {
      JavaHL::SubversionException(env).throw_java_exception(
          apr_psprintf(scratch_pool,
                       "Unexpected realm; got: [%s], expected: [%s]",
                       (entry ? entry->data : "(null)"),
                       (realm ? realm : "(null)")));
    }

  entry = static_cast<svn_string_t*>(
      svn_hash_gets(cred, SVN_CONFIG_AUTHN_PASSTYPE_KEY));

  const char* store = (entry ? entry->data : NULL);
  const char* username = NULL;
  const char* password = NULL;
  jobject info = NULL;
  jobject failures = NULL;
  const char* passphrase = NULL;

  if (0 == strcmp(cred_kind, SVN_AUTH_CRED_USERNAME))
    {
      entry = static_cast<svn_string_t*>(
          svn_hash_gets(cred, SVN_CONFIG_AUTHN_USERNAME_KEY));
      if (entry)
        username = entry->data;
    }
  else if (0 == strcmp(cred_kind, SVN_AUTH_CRED_SIMPLE))
    {
      entry = static_cast<svn_string_t*>(
          svn_hash_gets(cred, SVN_CONFIG_AUTHN_USERNAME_KEY));
      if (entry)
        username = entry->data;
      entry = static_cast<svn_string_t*>(
          svn_hash_gets(cred, SVN_CONFIG_AUTHN_PASSWORD_KEY));
      if (entry)
        password = entry->data;
    }
  else if (0 == strcmp(cred_kind, SVN_AUTH_CRED_SSL_SERVER_TRUST))
    {
      entry = static_cast<svn_string_t*>(
          svn_hash_gets(cred, SVN_CONFIG_AUTHN_ASCII_CERT_KEY));
      const char* ascii_cert = (entry ? entry->data : NULL);

      entry = static_cast<svn_string_t*>(
          svn_hash_gets(cred, SVN_CONFIG_AUTHN_FAILURES_KEY));
      jint failflags = (!entry ? 0 : jint(apr_atoi64(entry->data)));

      info = JavaHL::AuthnCallback::SSLServerCertInfo(env, ascii_cert).get();
      failures = JavaHL::AuthnCallback
        ::SSLServerCertFailures(env, failflags).get();
    }
  else if (0 == strcmp(cred_kind, SVN_AUTH_CRED_SSL_CLIENT_CERT_PW))
    {
      entry = static_cast<svn_string_t*>(
          svn_hash_gets(cred, SVN_CONFIG_AUTHN_PASSPHRASE_KEY));
      if (entry)
        passphrase = entry->data;
    }
  else
    {
      JavaHL::SubversionException(env).throw_java_exception(
          apr_psprintf(scratch_pool,
                       "Invalid credential type: [%s]",
                       cred_kind));
    }

  return JavaHL::Credential(
      env,
      JavaHL::Credential::Kind(env, Java::String(env, cred_kind)).get(),
      Java::String(env, realm), Java::String(env, store),
      Java::String(env, username), Java::String(env, password),
      info, failures, Java::String(env, passphrase)).get();
}

class WalkCredentialsCallback
{
public:
  static svn_error_t* walk_func(svn_boolean_t *delete_cred,
                                void *walk_baton,
                                const char *cred_kind,
                                const char *realmstring,
                                apr_hash_t *cred,
                                apr_pool_t *scratch_pool)
    {
      WalkCredentialsCallback& self =
        *static_cast<WalkCredentialsCallback*>(walk_baton);
      return self(delete_cred, cred_kind, realmstring, cred, scratch_pool);
    }

  virtual svn_error_t* operator()(svn_boolean_t *delete_cred,
                                  const char *cred_kind,
                                  const char *realmstring,
                                  apr_hash_t *cred_hash,
                                  apr_pool_t *scratch_pool) = 0;
};

class SimpleSearchCallback : public WalkCredentialsCallback
{
  const ::Java::Env m_env;
  const char* const m_cred_kind;
  const char* const m_realm;
  const bool m_delete_when_found;
  jobject m_credential;

public:
  explicit SimpleSearchCallback(::Java::Env env,
                                const char* cred_kind, const char* realm,
                                bool delete_when_found)
    : m_env(env),
      m_cred_kind(cred_kind),
      m_realm(realm),
      m_delete_when_found(delete_when_found),
      m_credential(NULL)
    {}

  jobject credential() const { return m_credential; }

  virtual svn_error_t* operator()(svn_boolean_t *delete_cred,
                                  const char *cred_kind,
                                  const char *realmstring,
                                  apr_hash_t *cred_hash,
                                  apr_pool_t *scratch_pool)
    {
      if (0 == strcmp(cred_kind, m_cred_kind)
          && 0 == strcmp(realmstring, m_realm))
        {
          m_credential = build_credential(m_env, cred_hash,
                                          cred_kind, realmstring,
                                          scratch_pool);
          *delete_cred = m_delete_when_found;
          return svn_error_create(SVN_ERR_CEASE_INVOCATION, NULL, "");
        }

      *delete_cred = false;
      return SVN_NO_ERROR;
    }
};
} // anonymous namespace


JNIEXPORT jobject JNICALL
Java_org_apache_subversion_javahl_util_ConfigLib_nativeGetCredential(
    JNIEnv* jenv, jobject jthis,
    jstring jconfig_dir, jstring jcred_kind, jstring jrealm)
{
  SVN_JAVAHL_JNI_TRY(ConfigLib, nativeGetCredential)
    {
      if (!GlobalConfig::useNativeCredentialsStore())
        return NULL;

      const Java::Env env(jenv);
      const Java::String config_dir(env, jconfig_dir);
      const Java::String cred_kind(env, jcred_kind);
      const Java::String realm(env, jrealm);

      // Using a "global" request pool since we don't keep a context
      // with its own pool around for these functions.
      SVN::Pool pool;

      SimpleSearchCallback cb(env,
                              cred_kind.strdup(pool.getPool()),
                              realm.strdup(pool.getPool()),
                              false);

      SVN_JAVAHL_CHECK(env,
                       svn_config_walk_auth_data(
                           Java::String::Contents(config_dir).c_str(),
                           cb.walk_func, &cb, pool.getPool()));
      return cb.credential();
    }
  SVN_JAVAHL_JNI_CATCH;
  return NULL;
}

JNIEXPORT jobject JNICALL
Java_org_apache_subversion_javahl_util_ConfigLib_nativeRemoveCredential(
    JNIEnv* jenv, jobject jthis,
    jstring jconfig_dir, jstring jcred_kind, jstring jrealm)
{
  SVN_JAVAHL_JNI_TRY(ConfigLib, nativeRemoveCredential)
    {
      if (!GlobalConfig::useNativeCredentialsStore())
        return NULL;

      const Java::Env env(jenv);
      const Java::String config_dir(env, jconfig_dir);
      const Java::String cred_kind(env, jcred_kind);
      const Java::String realm(env, jrealm);

      // Using a "global" request pool since we don't keep a context
      // with its own pool around for these functions.
      SVN::Pool pool;

      SimpleSearchCallback cb(env,
                              cred_kind.strdup(pool.getPool()),
                              realm.strdup(pool.getPool()),
                              true);

      SVN_JAVAHL_CHECK(env,
                       svn_config_walk_auth_data(
                           Java::String::Contents(config_dir).c_str(),
                           cb.walk_func, &cb, pool.getPool()));
      return cb.credential();
    }
  SVN_JAVAHL_JNI_CATCH;
  return NULL;
}

JNIEXPORT jobject JNICALL
Java_org_apache_subversion_javahl_util_ConfigLib_nativeSearchCredentials(
    JNIEnv* jenv, jobject jthis,
    jstring jconfig_dir, jstring jcred_kind,
    jstring jrealm_pattern, jstring jusername_pattern,
    jstring jhostname_pattern, jstring jtext_pattern)
{
  SVN_JAVAHL_JNI_TRY(ConfigLib, iterateCredentials)
    {
      if (!GlobalConfig::useNativeCredentialsStore())
        return NULL;

      const Java::Env env(jenv);
      const Java::String config_dir(env, jconfig_dir);
      const Java::String cred_kind(env, jcred_kind);
      const Java::String realm_pattern(env, jrealm_pattern);
      const Java::String username_pattern(env, jusername_pattern);
      const Java::String hostname_pattern(env, jhostname_pattern);
      const Java::String text_pattern(env, jtext_pattern);

      // Using a "global" request pool since we don't keep a context
      // with its own pool around for these functions.
      SVN::Pool pool;

      class Callback : public WalkCredentialsCallback
      {
        const char* const m_cred_kind;
        const char* const m_realm_pattern;
        const char* const m_username_pattern;
        const char* const m_hostname_pattern;
        const char* const m_text_pattern;

        const ::Java::Env m_env;
        ::Java::List<JavaHL::Credential> m_credentials;

        bool match_array(const char* pattern,
                         const apr_array_header_t* hostnames)
          {
            for (int i = 0; i < hostnames->nelts; ++i)
              {
                const char* const hostname =
                  APR_ARRAY_IDX(hostnames, i, const char*);
                if (!apr_fnmatch(pattern, hostname, 0))
                  return true;
              }
            return false;
          }

      public:
        explicit Callback(::Java::Env ctor_env,
                          const char* ctor_cred_kind,
                          const char* ctor_realm_pattern,
                          const char* ctor_username_pattern,
                          const char* ctor_hostname_pattern,
                          const char* ctor_text_pattern)
          : m_cred_kind(ctor_cred_kind),
            m_realm_pattern(ctor_realm_pattern),
            m_username_pattern(ctor_username_pattern),
            m_hostname_pattern(ctor_hostname_pattern),
            m_text_pattern(ctor_text_pattern),
            m_env(ctor_env),
            m_credentials(ctor_env)
          {}

        jobject credentials() const
          {
            if (m_credentials.is_empty())
              return NULL;
            return m_credentials.get();
          }

        virtual svn_error_t* operator()(svn_boolean_t *delete_cred,
                                        const char *cb_cred_kind,
                                        const char *cb_realmstring,
                                        apr_hash_t *cb_cred_hash,
                                        apr_pool_t *cb_scratch_pool)
          {
            *delete_cred = false;
            if (m_cred_kind && 0 != strcmp(cb_cred_kind, m_cred_kind))
              return SVN_NO_ERROR;

            svn_string_t* entry = static_cast<svn_string_t*>(
                svn_hash_gets(cb_cred_hash, SVN_CONFIG_AUTHN_USERNAME_KEY));
            const char* const username = (entry ? entry->data : NULL);

            entry = static_cast<svn_string_t*>(
                svn_hash_gets(cb_cred_hash, SVN_CONFIG_AUTHN_PASSTYPE_KEY));
            const char* const store = (entry ? entry->data : NULL);

            const svn_string_t* ascii_cert = static_cast<svn_string_t*>(
                svn_hash_gets(cb_cred_hash, SVN_CONFIG_AUTHN_ASCII_CERT_KEY));

            /* Parsed certificate data. */
            const char* subject = NULL;
            const char* issuer = NULL;
            const char* fingerprint = NULL;
            const apr_array_header_t* hostnames = NULL;

            if (ascii_cert)
              {
                const svn_string_t* const der =
                  svn_base64_decode_string(ascii_cert, cb_scratch_pool);
                svn_x509_certinfo_t* certinfo;
                svn_error_t* err = svn_x509_parse_cert(
                    &certinfo, der->data, der->len,
                    cb_scratch_pool, cb_scratch_pool);
                if (err)
                  {
                    // Ignore credentials that can't be parsed.
                    svn_error_clear(err);
                    return SVN_NO_ERROR;
                  }
                else
                  {
                    subject = svn_x509_certinfo_get_subject(
                        certinfo, cb_scratch_pool);
                    issuer = svn_x509_certinfo_get_issuer(
                        certinfo, cb_scratch_pool);
                    fingerprint = svn_checksum_to_cstring_display(
                        svn_x509_certinfo_get_digest(certinfo),
                        cb_scratch_pool);
                    hostnames = svn_x509_certinfo_get_hostnames(certinfo);
                  }
              }

            bool match = (m_realm_pattern
                          && !apr_fnmatch(m_realm_pattern, cb_realmstring, 0));

            if (!match && m_username_pattern)
              match = (match
                       || (username
                           && !apr_fnmatch(m_username_pattern, username, 0)));

            if (!match && m_hostname_pattern)
              match = (match
                       || (hostnames
                           &&  match_array(m_hostname_pattern, hostnames)));

            if (!match && m_text_pattern)
              match = (match
                       || (username
                           && !apr_fnmatch(m_text_pattern, username, 0))
                       || (store
                           && !apr_fnmatch(m_text_pattern, store, 0))
                       || (subject
                           && !apr_fnmatch(m_text_pattern, subject, 0))
                       || (issuer
                           && !apr_fnmatch(m_text_pattern, issuer, 0))
                       || (fingerprint
                           && !apr_fnmatch(m_text_pattern, fingerprint, 0))
                       || (hostnames
                           && match_array(m_text_pattern, hostnames)));

            if (match)
              m_credentials.add(
                  ::JavaHL::Credential(m_env,
                                       build_credential(m_env,
                                                        cb_cred_hash,
                                                        cb_cred_kind,
                                                        cb_realmstring,
                                                        cb_scratch_pool)));

            return SVN_NO_ERROR;
          }
      } cb(env,
           cred_kind.strdup(pool.getPool()),
           realm_pattern.strdup(pool.getPool()),
           username_pattern.strdup(pool.getPool()),
           hostname_pattern.strdup(pool.getPool()),
           text_pattern.strdup(pool.getPool()));

      SVN_JAVAHL_CHECK(env,
                       svn_config_walk_auth_data(
                           Java::String::Contents(config_dir).c_str(),
                           cb.walk_func, &cb, pool.getPool()));
      return cb.credentials();
    }
  SVN_JAVAHL_JNI_CATCH;
  return NULL;
}
