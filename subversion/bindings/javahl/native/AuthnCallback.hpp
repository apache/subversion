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

#ifndef SVN_JAVAHL_AUTHN_CALLBACK_HPP
#define SVN_JAVAHL_AUTHN_CALLBACK_HPP

#include "svn_auth.h"

#include "Pool.h"

#include "jniwrapper/jni_object.hpp"
#include "jniwrapper/jni_string.hpp"

namespace JavaHL {

/**
 * Object wrapper for @c org.apache.subversion.javahl.callback.AuthnCallback.
 *
 * @since New in 1.9.
 */
class AuthnCallback : public ::Java::Object
{
public:
  /**
   * Object wrapper for @c ...AuthnCallback$AuthnResult.
   */
  class AuthnResult : public ::Java::Object
  {
  public:
    /**
     * Constructs a wrapper around @a jthis.
     * The constructor does not verify the class of the wrapped object.
     */
    explicit AuthnResult(::Java::Env env, jobject jthis)
      : ::Java::Object(env, ::Java::ClassCache::get_authn_result(env), jthis)
      {}

    bool save() const
      {
        return (0 != m_env.GetBooleanField(m_jthis, impl().m_fid_save));
      }

    bool trust() const
      {
        return (0 != m_env.GetBooleanField(m_jthis, impl().m_fid_trust));
      }

    jstring identity() const
      {
        return jstring(m_env.GetObjectField(m_jthis, impl().m_fid_identity));
      }

    jstring secret() const
      {
        return jstring(m_env.GetObjectField(m_jthis, impl().m_fid_secret));
      }

  private:
    /**
     * This object's implementation details.
     */
    class ClassImpl : public Object::ClassImpl
    {
      friend class ::Java::ClassCacheImpl;

    protected:
      explicit ClassImpl(::Java::Env env, jclass cls);

    public:
      virtual ~ClassImpl();

      const ::Java::FieldID m_fid_save;
      const ::Java::FieldID m_fid_trust;
      const ::Java::FieldID m_fid_identity;
      const ::Java::FieldID m_fid_secret;
    };

    const ClassImpl& impl() const
      {
        return *dynamic_cast<const ClassImpl*>(m_impl);
      }

    friend class ::Java::ClassCacheImpl;
    static const char* const m_class_name;
  };


  /**
   * Object wrapper for @c ...AuthnCallback$SSLServerCertFailures.
   */
  class SSLServerCertFailures : public ::Java::Object
  {
  public:
    /**
     * Creates and initializes a wrapped object;
     * @a failures is a set of flags.
     */
    explicit SSLServerCertFailures(::Java::Env env, jint failures);

  private:
    /**
     * This object's implementation details.
     */
    class ClassImpl : public Object::ClassImpl
    {
      friend class ::Java::ClassCacheImpl;

    protected:
      explicit ClassImpl(::Java::Env env, jclass cls);

    public:
      virtual ~ClassImpl();

      const ::Java::MethodID m_mid_ctor;
    };

    const ClassImpl& impl() const
      {
        return *dynamic_cast<const ClassImpl*>(m_impl);
      }

    friend class ::Java::ClassCacheImpl;
    static const char* const m_class_name;
  };


  /**
   * Object wrapper for @c ...AuthnCallback$SSLServerCertInfo.
   */
  class SSLServerCertInfo : public ::Java::Object
  {
  public:
    /**
     * Creates and initializes a wrapped object;
     */
    explicit SSLServerCertInfo(::Java::Env env, const char* ascii_cert);

  private:
    /**
     * This object's implementation details.
     */
    class ClassImpl : public Object::ClassImpl
    {
      friend class ::Java::ClassCacheImpl;

    protected:
      explicit ClassImpl(::Java::Env env, jclass cls);

    public:
      virtual ~ClassImpl();

      const ::Java::MethodID m_mid_ctor;
    };

    const ClassImpl& impl() const
      {
        return *dynamic_cast<const ClassImpl*>(m_impl);
      }

    friend class ::Java::ClassCacheImpl;
    static const char* const m_class_name;
  };


  /**
   * Constructs a wrapper around @a jthis.
   * The constructor does not verify the class of the wrapped object.
   */
  explicit AuthnCallback(::Java::Env env, jobject jthis)
    : ::Java::Object(env, ::Java::ClassCache::get_authn_cb(env), jthis)
    {}

  /**
   * Invokes the Java method AuthnCallback.usernamePrompt().
   */
  jobject username_prompt(const ::Java::String& realm, bool may_save);


  /**
   * Invokes the Java method AuthnCallback.userPasswordPrompt().
   */
  jobject user_password_prompt(const ::Java::String& realm,
                               const ::Java::String& username,
                               bool may_save);

  /**
   * Invokes the Java method AuthnCallback.sslServerTrustPrompt().
   */
  jobject ssl_server_trust_prompt(const ::Java::String& realm,
                                  const SSLServerCertFailures& failures,
                                  const SSLServerCertInfo& info,
                                  bool may_save);

  /**
   * Invokes the Java method AuthnCallback.sslClientCertPrompt().
   */
  jobject ssl_client_cert_prompt(const ::Java::String& realm, bool may_save);

  /**
   * Invokes the Java method AuthnCallback.sslClientCertPassphrasePrompt().
   */
  jobject ssl_client_cert_passphrase_prompt(const ::Java::String& realm,
                                            bool may_save);

  /**
   * Invokes the Java method AuthnCallback.allowStorePlaintextPassword().
   */
  bool allow_store_plaintext_password(const ::Java::String& realm);

  /**
   * Invokes the Java method AuthnCallback.allowStorePlaintextPassphrase().
   */
  bool allow_store_plaintext_passphrase(const ::Java::String& realm);

private:
  /**
   * This object's implementation details.
   */
  class ClassImpl : public Object::ClassImpl
  {
    friend class ::Java::ClassCacheImpl;

  protected:
    explicit ClassImpl(::Java::Env env, jclass cls);

  public:
    virtual ~ClassImpl();

    const ::Java::MethodID m_mid_username_prompt;
    const ::Java::MethodID m_mid_user_password_prompt;
    const ::Java::MethodID m_mid_ssl_server_trust_prompt;
    const ::Java::MethodID m_mid_ssl_client_cert_prompt;
    const ::Java::MethodID m_mid_ssl_client_cert_passphrase_prompt;
    const ::Java::MethodID m_mid_allow_store_plaintext_password;
    const ::Java::MethodID m_mid_allow_store_plaintext_passphrase;
  };

  const ClassImpl& impl() const
    {
      return *dynamic_cast<const ClassImpl*>(m_impl);
    }

  friend class ::Java::ClassCacheImpl;
  static const char* const m_class_name;
};


/**
 * Object wrapper for the deprecated interface
 * @c org.apache.subversion.javahl.callback.UserPasswordCallback.
 *
 * @since New in 1.9.
 */
class UserPasswordCallback : public ::Java::Object
{
public:
  /**
   * Constructs a wrapper around @a jthis.
   * The constructor does not verify the class of the wrapped object.
   */
  explicit UserPasswordCallback(::Java::Env env, jobject jthis)
    : ::Java::Object(env, ::Java::ClassCache::get_user_passwd_cb(env), jthis)
    {}

  /**
   * Invokes the Java method UserPasswordCallback.askTrustSSLServer().
   */
  jint ask_trust_ssl_server(const ::Java::String& info,
                            bool allow_permanently);

  /**
   * Invokes the Java method UserPasswordCallback.prompt().
   */
  bool prompt(const ::Java::String& realm,
              const ::Java::String& username);

  /**
   * Invokes the Java method UserPasswordCallback.askYesNo().
   */
  bool ask_yes_no(const ::Java::String& realm,
                  const ::Java::String& question,
                  bool yes_is_default);

  /**
   * Invokes the Java method UserPasswordCallback.askQuestion().
   */
  jstring ask_question(const ::Java::String& realm,
                       const ::Java::String& question,
                       bool show_answer);

  /**
   * Invokes the Java method UserPasswordCallback.getUsername().
   */
  jstring get_username();

  /**
   * Invokes the Java method UserPasswordCallback.getPassword().
   */
  jstring get_password();

  /**
   * Invokes the Java method UserPasswordCallback.prompt().
   */
  bool prompt(const ::Java::String& realm,
              const ::Java::String& username,
              bool may_save);

  /**
   * Invokes the Java method UserPasswordCallback.askQuestion().
   */
  jstring ask_question(const ::Java::String& realm,
                       const ::Java::String& question,
                       bool show_answer, bool may_save);

  /**
   * Invokes the Java method UserPasswordCallback.userAllowedSave().
   */
  bool user_allowed_save();

private:
  /**
   * This object's implementation details.
   */
  class ClassImpl : public Object::ClassImpl
  {
    friend class ::Java::ClassCacheImpl;

  protected:
    explicit ClassImpl(::Java::Env env, jclass cls);

  public:
    virtual ~ClassImpl();

    const ::Java::MethodID m_mid_ask_trust_ssl_server;
    const ::Java::MethodID m_mid_prompt_2arg;
    const ::Java::MethodID m_mid_ask_yes_no;
    const ::Java::MethodID m_mid_ask_question_3arg;
    const ::Java::MethodID m_mid_get_username;
    const ::Java::MethodID m_mid_get_password;
    const ::Java::MethodID m_mid_prompt;
    const ::Java::MethodID m_mid_ask_question;
    const ::Java::MethodID m_mid_user_allowed_save;
  };

  const ClassImpl& impl() const
    {
      return *dynamic_cast<const ClassImpl*>(m_impl);
    }

  friend class ::Java::ClassCacheImpl;
  static const char* const m_class_name;
};

} // namespace JavaHL

#endif // SVN_JAVAHL_AUTHN_CALLBACK_HPP
