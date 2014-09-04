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

#include <cstring>
#include <string>

#include "svn_string.h"

#include "jni_env.hpp"
#include "../Pool.h"

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
      m_length(array ? m_env.GetArrayLength(array) : 0),
      m_array(array)
    {}

  /**
   * Constructs a new, uninitialized array of size @a length.
   */
  explicit ByteArray(Env env, jsize length)
    : m_env(env),
      m_length(length),
      m_array(m_env.NewByteArray(m_length))
    {}

  /**
   * Constructs a new array and wrapper from @a text.
   */
  explicit ByteArray(Env env, const char* text)
    : m_env(env),
      m_length(jsize(::std::strlen(text))),
      m_array(m_env.NewByteArray(m_length))
    {
      ByteArray::MutableContents contents(*this);
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
      ByteArray::MutableContents contents(*this);
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
      ByteArray::MutableContents contents(*this);
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
   * create an immutable mirror of the array contents.
   */
  class Contents
  {
  public:
    /**
     * Constructs an immutable array contents accessor.
     *
     * Whilst the returned contents are themselves mutable, the
     * destructor will discard any changes.
     */
    explicit Contents(const ByteArray& array)
      : m_array(array),
        m_data(!array.m_array ? NULL
               : array.m_env.GetByteArrayElements(array.m_array, NULL))
      {}

    /**
     * Releases the array contents.
     */
    ~Contents()
      {
        if (m_data)
          {
            const Env& env = m_array.m_env;
            env.ReleaseByteArrayElements(m_array.m_array, m_data, JNI_ABORT);
          }
      }

    /**
     * Returns the address of the immutable array contents.
     * @note The data will @b not be NUL-terminated!
     */
    const char* data() const
      {
        return reinterpret_cast<const char*>(m_data);
      }

    /**
     * Returns the size of the array contents.
     */
    jsize length() const
      {
        return m_array.m_length;
      }

    /**
     * Copies the array contents to a NUL-terminated string allocated
     * from @a result_pool.
     */
    svn_string_t* get_string(apr_pool_t* result_pool) const
      {
        if (m_data)
          return svn_string_ncreate(data(), m_array.m_length, result_pool);
        return NULL;
      }

    /**
     * Copies the array contents to a NUL-terminated string allocated
     * from @a result_pool.
     */
    svn_string_t* get_string(const ::SVN::Pool& result_pool) const
      {
        return get_string(result_pool.getPool());
      }

  protected:
    const ByteArray& m_array;
    jbyte* m_data;
  };

  /**
   * Accessor class for the contents of the byte array.
   *
   * Behaves like the #Contents class, but the mirrored contents are
   * considered mutable and any changes made to them will be committed
   * to the JVM.
   */
  class MutableContents : protected Contents
  {
  public:
    /**
     * Constructs a mutable array contents accessor.
     */
    explicit MutableContents(ByteArray& array)
      : Contents(array)
      {}

    /**
     * Releases the array contents, committing changes to the JVM.
     */
    ~MutableContents()
      {
        if (m_data)
          {
            // Prevent double-release by the Contents desctuctor
            jbyte* const data = m_data;
            m_data = NULL;
            m_array.m_env.ReleaseByteArrayElements(m_array.m_array, data, 0);
          }
      }
    /**
     * Returns the mutable address of the array contents.
     * @note The data will @b not be NUL-terminated!
     */
    char* data()
      {
        return const_cast<char*>(Contents::data());
      }

    /**
     * Returns the size of the array contents.
     */
    jsize length() const
      {
        return Contents::length();
      }

    /**
     * Copies the array contents to a NUL-terminated string allocated
     * from @a result_pool.
     */
    svn_string_t* get_string(apr_pool_t* result_pool) const
      {
        return Contents::get_string(result_pool);
      }

    /**
     * Copies the array contents to a NUL-terminated string allocated
     * from @a result_pool.
     */
    svn_string_t* get_string(const ::SVN::Pool& result_pool) const
      {
        return Contents::get_string(result_pool.getPool());
      }
  };

private:
  friend class Contents;
  const Env m_env;
  const jsize m_length;
  const jbyteArray m_array;
};

} // namespace Java

#endif // SVN_JAVAHL_JNIWRAPPER_ARRAY_HPP
