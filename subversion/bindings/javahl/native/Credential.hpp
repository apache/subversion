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

#ifndef SVN_JAVAHL_CREDENTIAL_HPP
#define SVN_JAVAHL_CREDENTIAL_HPP

#include "jniwrapper/jni_object.hpp"
#include "jniwrapper/jni_string.hpp"

#include "AuthnCallback.hpp"

namespace JavaHL {

/**
 * Object wrapper for @c org.apache.subversion.javahl.SVNUtil.Credential.
 *
 * @since New in 1.9.
 */
class Credential : public ::Java::Object
{
public:
  /**
   * Object wrapper for @c ...Credential$Kind.
   */
  class Kind : public ::Java::Object
  {
  public:
    /**
     * Constructs a and wraps a new Credential$Kind object.
     */
    explicit Kind(::Java::Env env, const ::Java::String& value);

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

      const ::Java::MethodID m_static_mid_from_string;
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
  explicit Credential(::Java::Env env, jobject jthis)
    : ::Java::Object(env, ::Java::ClassCache::get_credential(env), jthis)
    {}

  /**
   * Constructs and wraps a new Credential object
   */
  explicit Credential(::Java::Env env, jobject kind,
                      const ::Java::String& realm,
                      const ::Java::String& store,
                      const ::Java::String& username,
                      const ::Java::String& password,
                      jobject info, jobject failures,
                      const ::Java::String& passphrase);

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

} // namespace JavaHL

#endif // SVN_JAVAHL_CREDENTIAL_HPP
