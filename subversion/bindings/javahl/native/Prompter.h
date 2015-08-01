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
 * @file Prompter.h
 * @brief Interface of the class Prompter
 */

#ifndef SVN_JAVAHL_PROMPTER_H
#define SVN_JAVAHL_PROMPTER_H

#include <memory>

#include "svn_auth.h"

#include "Pool.h"

#include "jniwrapper/jni_globalref.hpp"

class Prompter
{
public:
  typedef ::std::auto_ptr<Prompter> UniquePtr;

  /**
   * Factory method; @a prompter is a local reference to the Java
   * callback object.
   */
  static Prompter::UniquePtr create(jobject jprompter);

  /**
   * Return a clone of the current object, referring to the same Java
   * prompter object.
   */
  virtual Prompter::UniquePtr clone() const;

  virtual ~Prompter();

  svn_auth_provider_object_t *get_provider_username(SVN::Pool &in_pool);
  svn_auth_provider_object_t *get_provider_simple(SVN::Pool &in_pool);
  svn_auth_provider_object_t *get_provider_server_ssl_trust(SVN::Pool &in_pool);
  svn_auth_provider_object_t *get_provider_client_ssl(SVN::Pool &in_pool);
  svn_auth_provider_object_t *get_provider_client_ssl_password(SVN::Pool &in_pool);

protected:
  explicit Prompter(::Java::Env env, jobject jprompter);

  /**
   * The Java callback object.
   */
  ::Java::GlobalObject m_prompter;

  virtual svn_error_t *dispatch_simple_prompt(
      ::Java::Env env,
      svn_auth_cred_simple_t **cred_p,
      const char *realm,
      const char *username,
      svn_boolean_t may_save,
      apr_pool_t *pool);

  static svn_error_t *simple_prompt(
      svn_auth_cred_simple_t **cred_p,
      void *baton,
      const char *realm,
      const char *username,
      svn_boolean_t may_save,
      apr_pool_t *pool);

  virtual svn_error_t *dispatch_username_prompt(
      ::Java::Env env,
      svn_auth_cred_username_t **cred_p,
      const char *realm,
      svn_boolean_t may_save,
      apr_pool_t *pool);

  static svn_error_t *username_prompt(
      svn_auth_cred_username_t **cred_p,
      void *baton,
      const char *realm,
      svn_boolean_t may_save,
      apr_pool_t *pool);

  virtual svn_error_t *dispatch_ssl_server_trust_prompt(
      ::Java::Env env,
      svn_auth_cred_ssl_server_trust_t **cred_p,
      const char *realm,
      apr_uint32_t failures,
      const svn_auth_ssl_server_cert_info_t *cert_info,
      svn_boolean_t may_save,
      apr_pool_t *pool);

  static svn_error_t *ssl_server_trust_prompt(
      svn_auth_cred_ssl_server_trust_t **cred_p,
      void *baton,
      const char *realm,
      apr_uint32_t failures,
      const svn_auth_ssl_server_cert_info_t *cert_info,
      svn_boolean_t may_save,
      apr_pool_t *pool);

  virtual svn_error_t *dispatch_ssl_client_cert_prompt(
      ::Java::Env env,
      svn_auth_cred_ssl_client_cert_t **cred_p,
      const char *realm,
      svn_boolean_t may_save,
      apr_pool_t *pool);

  static svn_error_t *ssl_client_cert_prompt(
      svn_auth_cred_ssl_client_cert_t **cred_p,
      void *baton,
      const char *realm,
      svn_boolean_t may_save,
      apr_pool_t *pool);

  virtual svn_error_t *dispatch_ssl_client_cert_pw_prompt(
      ::Java::Env env,
      svn_auth_cred_ssl_client_cert_pw_t **cred_p,
      const char *realm,
      svn_boolean_t may_save,
      apr_pool_t *pool);

  static svn_error_t *ssl_client_cert_pw_prompt(
      svn_auth_cred_ssl_client_cert_pw_t **cred_p,
      void *baton,
      const char *realm,
      svn_boolean_t may_save,
      apr_pool_t *pool);

protected:
  virtual svn_error_t *dispatch_plaintext_prompt(
      ::Java::Env env,
      svn_boolean_t *may_save_plaintext,
      const char *realmstring,
      apr_pool_t *pool);

public:
  static svn_error_t *plaintext_prompt(
      svn_boolean_t *may_save_plaintext,
      const char *realmstring,
      void *baton,
      apr_pool_t *pool);

protected:
  virtual svn_error_t *dispatch_plaintext_passphrase_prompt(
      ::Java::Env env,
      svn_boolean_t *may_save_plaintext,
      const char *realmstring,
      apr_pool_t *pool);

public:
  static svn_error_t *plaintext_passphrase_prompt(
      svn_boolean_t *may_save_plaintext,
      const char *realmstring,
      void *baton,
      apr_pool_t *pool);
};


/**
 * This class requests username/password and informations about
 * ssl-certificates from the user.
 */
class CompatPrompter : public Prompter
{
public:
  static Prompter::UniquePtr create(jobject jprompter);
  virtual Prompter::UniquePtr clone() const;
  virtual ~CompatPrompter();

protected:
  explicit CompatPrompter(::Java::Env env, jobject jprompter);

  virtual svn_error_t *dispatch_simple_prompt(
      ::Java::Env env,
      svn_auth_cred_simple_t **cred_p,
      const char *realm,
      const char *username,
      svn_boolean_t may_save,
      apr_pool_t *pool);

  virtual svn_error_t *dispatch_username_prompt(
      ::Java::Env env,
      svn_auth_cred_username_t **cred_p,
      const char *realm,
      svn_boolean_t may_save,
      apr_pool_t *pool);

  virtual svn_error_t *dispatch_ssl_server_trust_prompt(
      ::Java::Env env,
      svn_auth_cred_ssl_server_trust_t **cred_p,
      const char *realm,
      apr_uint32_t failures,
      const svn_auth_ssl_server_cert_info_t *cert_info,
      svn_boolean_t may_save,
      apr_pool_t *pool);

  virtual svn_error_t *dispatch_ssl_client_cert_prompt(
      ::Java::Env env,
      svn_auth_cred_ssl_client_cert_t **cred_p,
      const char *realm,
      svn_boolean_t may_save,
      apr_pool_t *pool);

  virtual svn_error_t *dispatch_ssl_client_cert_pw_prompt(
      ::Java::Env env,
      svn_auth_cred_ssl_client_cert_pw_t **cred_p,
      const char *realm,
      svn_boolean_t may_save,
      apr_pool_t *pool);

  virtual svn_error_t *dispatch_plaintext_prompt(
      ::Java::Env env,
      svn_boolean_t *may_save_plaintext,
      const char *realmstring,
      apr_pool_t *pool);

  virtual svn_error_t *dispatch_plaintext_passphrase_prompt(
      ::Java::Env env,
      svn_boolean_t *may_save_plaintext,
      const char *realmstring,
      apr_pool_t *pool);
};

#endif // SVN_JAVAHL_PROMPTER_H
