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

#ifndef SVN_JAVAHL_JNIWRAPPER_ARRAY_HPP
#define SVN_JAVAHL_JNIWRAPPER_ARRAY_HPP

#include <string>

#include "svn_string.h"

#include "jni_env.hpp"

namespace Java {

/**
 * Wrapper class for Java byte arrays.
 *
 * @since New in 1.9.
 */
class ByteArray
{
public:
  /**
   * Constructs a wrapper around an existing @a array.
   */
  explicit ByteArray(Env env, jbyteArray array)
    : m_env(env),
      m_length(array ? m_env.GetArrayLength(array) : -1),
      m_array(array)
    {}

  /**
   * Constructs a new array and wrapper from @a text.
   */
  explicit ByteArray(Env env, const char* text)
    : m_env(env),
      m_length(jsize(::strlen(text))),
      m_array(m_env.NewByteArray(m_length))
    {
      ByteArray::Contents contents(*this);
      ::memcpy(contents.data(), text, m_length);
    }

  /**
   * Constructs a new array and wrapper of size @a length
   * from @a data.
   */
  explicit ByteArray(Env env, const void* data, jsize length)
    : m_env(env),
      m_length(length),
      m_array(m_env.NewByteArray(m_length))
    {
      ByteArray::Contents contents(*this);
      ::memcpy(contents.data(), data, m_length);
    }

  /**
   * Constructs a new array and wrapper from @a text.
   */
  explicit ByteArray(Env env, const std::string& text)
    : m_env(env),
      m_length(jsize(text.size())),
      m_array(m_env.NewByteArray(m_length))
    {
      ByteArray::Contents contents(*this);
      ::memcpy(contents.data(), text.c_str(), m_length);
    }

  /**
   * Returns the wrapped native array reference.
   */
  jbyteArray get() const
    {
      return m_array;
    }

  /**
   * Returns the size of the wrapped array.
   */
  jsize length() const
    {
      return m_length;
    }

  /**
   * Accessor class for the contents of the byte array.
   *
   * Objects of this class should be created within the scope where
   * the raw data stored in the array must be manipulated. They will
   * create either a mutable or an immutable mirror of the array
   * contents, depending on the constantness of the array wrapper. The
   * data will be released (and changes copied into the JVM, depending
   * on access mode) by the destructor.
   */
  class Contents
  {
  public:
    /**
     * Constructs a mutable array contents accessor.
     */
    explicit Contents(ByteArray& array)
      : m_array(array),
        m_data(array.m_env.GetByteArrayElements(array.m_array, NULL)),
        m_mode(0)
      {
        //fprintf(stderr,
        //        "%s:%d: non-const byte array data, size=%d\n",
        //        __FILE__, __LINE__, int(m_array.m_length));
      }

    /**
     * Constructs an immutable array contents accessor.
     *
     * Whilst the returned contents are themselves mutable, the
     * destructor will discard any changes.
     */
    explicit Contents(const ByteArray& array)
      : m_array(array),
        m_data(array.m_env.GetByteArrayElements(array.m_array, NULL)),
        m_mode(JNI_ABORT)
      {
        //fprintf(stderr,
        //        "%s:%d: const byte array data, size=%d\n",
        //        __FILE__, __LINE__, int(m_array.m_length));
      }

    /**
     * Releases the array contents, possibly committing changes to the JVM.
     */
    ~Contents()
      {
        const Env& env = m_array.m_env;
        env.ReleaseByteArrayElements(m_array.m_array, m_data, m_mode);
      }

    /**
     * Returns the address of the array contents.
     */
    const char* data() const
      {
        return reinterpret_cast<const char*>(m_data);
      }

    /**
     * Returns the address of the array contents.
     * @throw std::logic_error if the data reference is immutable.
     */
    char* data()
      {
        if (!m_mode)
          return reinterpret_cast<char*>(const_cast<jbyte*>(m_data));
        throw std::logic_error(
            _("Can't make a writable pointer to immutable array contents."));
      }

    /**
     * Returns the size of the array contents.
     */
    jsize length() const
      {
        return m_array.m_length;
      }

    /**
     * Constructs @a str that refers to the array contents.
     */
    void get_string(svn_string_t* str) const
      {
        str->data = data();
        str->len = m_array.m_length;
      }

  private:
    const ByteArray& m_array;
    jbyte* const m_data;
    const jint m_mode;
  };

private:
  friend class Contents;
  const Env m_env;
  const jsize m_length;
  const jbyteArray m_array;
};

} // namespace Java

#endif // SVN_JAVAHL_JNIWRAPPER_ARRAY_HPP
