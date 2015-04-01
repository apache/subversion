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

#ifndef SVN_JAVAHL_JNIWRAPPER_STRING_HPP
#define SVN_JAVAHL_JNIWRAPPER_STRING_HPP

#include <cstring>
#include <string>

#include <apr_pools.h>

#include "jni_object.hpp"

namespace Java {

/**
 * Object wrapper for @c java.lang.String.
 *
 * The associated JNI class reference is stored for the lifetime of
 * the JVM in the global class cache.
 *
 * @since New in 1.9.
 */
class String
{
public:
  /**
   * Constructs a wrapper around an existing string @a str.
   */
  explicit String(Env env, jstring str)
    : m_env(env),
      m_jthis(str)
    {}

  /**
   * Constructs a new string and wrapper from @a text.
   */
  explicit String(Env env, const char* text)
    : m_env(env),
      m_jthis(env.NewStringUTF(text))
    {}

  /**
   * Constructs a new string and wrapper from @a text.
   */
  explicit String(Env env, const std::string& text)
    : m_env(env),
      m_jthis(env.NewStringUTF(text.c_str()))
    {}

  /**
   * Returns the wrapped JNI object reference.
   */
  jstring get() const
    {
      return m_jthis;
    }

  /**
   * Returns the wrapped enviromnment reference.
   */
  Env get_env() const
    {
      return m_env;
    }

  /**
   * Returns the number of Unicode characters in the string.
   */
  jsize length() const
    {
      return m_env.GetStringLength(get());
    }

  /**
   * Returns the length of the modified UTF-8 representation of the
   * string.
   */
  jsize utf8_length() const
    {
      return m_env.GetStringUTFLength(get());
    }

  /**
   * Copies the contents of the modified UTF-8 representation of the
   * string into @a pool.
   */
  const char* strdup(apr_pool_t* pool) const;

  /**
   * Accessor class for the contents of the string.
   *
   * Objects of this class should be created within the scope where
   * the raw C string is required. They will create an immutable
   * modified UTF-8 representation of the string contents. The data
   * will be released by the destructor.
   */
  class Contents
  {
  public:
    /**
     * Constructs an immutable string contents accessor.
     */
    explicit Contents(const String& str)
      : m_str(str),
        m_text(!str.get() ? NULL
               : str.m_env.GetStringUTFChars(str.get(), NULL)),
        m_length(m_text ? jsize(::std::strlen(m_text)) : 0)
      {}

    /**
     * Releases the string contents, possibly committing changes to the JVM.
     */
    ~Contents()
      {
        if (m_text)
          m_str.m_env.ReleaseStringUTFChars(m_str.get(), NULL);
      }

    /**
     * Returns the C representation of the string contents.
     */
    const char* c_str() const
      {
        return m_text;
      }

    /**
     * Returns the length of the C representation of the string.
     */
    jsize utf8_length() const
      {
        return m_length;
      }

  protected:
    const String& m_str;
    const char* m_text;
    jsize m_length;
  };

  /**
   * Accessor class for the contents of the string.
   *
   * Behaves like the #Contents class, but the representation is
   * considered mutable and can be assigned a new value, which will be
   * subsequently committed to the JVM.
   */
  class MutableContents : protected Contents
  {
  public:
    /**
     * Constructs a mutable string contents accessor.
     */
    explicit MutableContents(String& str)
      : Contents(str),
        m_new_text(NULL)
      {}

    /**
     * Releases the string contents, possibly committing changes to the JVM.
     */
    ~MutableContents()
      {
        if (m_new_text)
          {
            // Prevent double-release by the Contents destructor.
            m_text = NULL;
            m_str.m_env.ReleaseStringUTFChars(m_str.get(), m_new_text);
          }
      }

    /**
     * Returns the C representation of the string contents.
     */
    const char* c_str() const
      {
        if (m_new_text)
          return m_new_text;
        return Contents::c_str();
      }

    /**
     * Returns the length of the C representation of the string.
     */
    jsize utf8_length() const
      {
        return Contents::utf8_length();
      }

    /**
     * Sets a new value for the string, to be committed to the JVM
     * when the accessor object is destroyed.
     * @throw std::invalid_argument if the @a new_text is @c null
     * @throw std::logic_error if this is a @c null or immutable string
     */
    void set_value(const char* new_text);

  private:
    const char* m_new_text;
  };

private:
  const Env m_env;
  const jstring m_jthis;

  /**
   * This object's implementation details.
   */
  class ClassImpl : public Object::ClassImpl
  {
    friend class ClassCacheImpl;

  protected:
    explicit ClassImpl(Env env, jclass cls)
      : Object::ClassImpl(env, cls)
      {}

  public:
    virtual ~ClassImpl();
  };

  friend class Contents;
  friend class MutableContents;
  friend class ClassCacheImpl;
  static const char* const m_class_name;
};

} // namespace Java

#endif // SVN_JAVAHL_JNIWRAPPER_STRING_HPP
