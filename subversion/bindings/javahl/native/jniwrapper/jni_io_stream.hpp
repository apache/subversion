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

#ifndef SVN_JAVAHL_JNIWRAPPER_IO_STREAM_HPP
#define SVN_JAVAHL_JNIWRAPPER_IO_STREAM_HPP

#include <cstring>
#include <string>
#include <memory>

#include "jni_object.hpp"
#include "jni_array.hpp"

#include "../Pool.h"

#include "svn_io.h"

namespace Java {

/**
 * Object wrapper for @c java.io.InputStream.
 *
 * @since New in 1.9.
 */
class InputStream : public Object
{
public:
  /**
   * Constructs a wrapper around an existing @c InputStream @a jstream.
   */
  explicit InputStream(Env env, jobject jstream)
    : Object(env, m_class_name, jstream)
    {}

  /**
   * Creates a stand-alone @c svn_stream_t allocated from @a pool that
   * contains a global reference to @a jstream. This stream can safely
   * be used in long-lived bound objects.
   */
  static svn_stream_t*
  get_global_stream(Env env, jobject jstream, const SVN::Pool& pool);

  /**
   * Creates an @c svn_stream_t allocated from @a pool.
   * <b>Do not use the returned stream past the lifetime of the
   * current JNI native frame.</b>
   */
  svn_stream_t* get_stream(const SVN::Pool& pool);

public:
  /**
   * Implements @c InputStream.close()
   */
  void close()
    {
      if (!m_mid_close)
        m_mid_close = m_env.GetMethodID(m_class, "close", "()V");
      m_env.CallVoidMethod(m_jthis, m_mid_close);
    }

  /**
   * Implements @c InputStream.markSupported()
   */
  bool mark_supported()
    {
      if (!m_mid_mark_supported)
        m_mid_mark_supported = m_env.GetMethodID(m_class,
                                                 "markSupported", "()Z");
      return m_env.CallBooleanMethod(m_jthis, m_mid_mark_supported);
    }

  /**
   * Implements @c InputStream.mark(int)
   */
  void mark(jint readlimit)
    {
      if (!m_mid_mark)
        m_mid_mark = m_env.GetMethodID(m_class, "mark", "(I)V");
      m_env.CallVoidMethod(m_jthis, m_mid_mark, readlimit);
    }

  /**
   * Implements @c InputStream.reset()
   */
  void reset()
    {
      if (!m_mid_reset)
        m_mid_reset = m_env.GetMethodID(m_class, "reset", "()V");
      m_env.CallVoidMethod(m_jthis, m_mid_reset);
    }

  /**
   * Implements @c InputStream.read()
   */
  jint read()
    {
      if (!m_mid_read_byte)
        m_mid_read_byte = m_env.GetMethodID(m_class, "read", "()I");
      return m_env.CallIntMethod(m_jthis, m_mid_read_byte);
    }

  /**
   * Implements @c InputStream.read(byte[],int,int)
   */
  jint read(ByteArray& dst, jint length = -1, jint offset = 0)
    {
      if (!m_mid_read_bytearray)
        m_mid_read_bytearray = m_env.GetMethodID(m_class, "read", "([BII)I");
      return m_env.CallIntMethod(m_jthis, m_mid_read_bytearray,
                                 dst.get(), offset,
                                 (length >= 0 ? length
                                  : dst.length() - offset));
    }

  /**
   * Helper method to read data into a native buffer.
   */
  jint read(void* data, jint length, jint offset = 0)
    {
      ByteArray array(m_env, length);
      const jint size = read(array);
      ByteArray::Contents contents(array);
      ::memcpy(static_cast<char*>(data) + offset, contents.data(), size);
      return size;
    }

  /**
   * Implements @c InputStream.skip(long)
   */
  jlong skip(jlong count);

private:
  static const char* const m_class_name;
  MethodID m_mid_close;
  MethodID m_mid_mark_supported;
  MethodID m_mid_mark;
  MethodID m_mid_reset;
  MethodID m_mid_read_byte;
  MethodID m_mid_read_bytearray;
};


/**
 * Object wrapper for @c java.io.OutputStream.
 *
 * @since New in 1.9.
 */
class OutputStream : public Object
{
public:
  /**
   * Constructs a wrapper around an existing @c OutputStream @a jstream.
   */
  explicit OutputStream(Env env, jobject jstream)
    : Object(env, m_class_name, jstream)
    {}

  /**
   * Creates a stand-alone @c svn_stream_t allocated from @a pool that
   * contains a global reference to @a jstream. This stream can safely
   * be used in long-lived bound objects.
   */
  static svn_stream_t*
  get_global_stream(Env env, jobject jstream, const SVN::Pool& pool);

  /**
   * Creates an @c svn_stream_t allocated from @a pool.
   * <b>Do not use the returned stream past the lifetime of the
   * current JNI native frame.</b>
   */
  svn_stream_t* get_stream(const SVN::Pool& pool);

public:
  /**
   * Implements @c OuptutStream.close()
   */
  void close()
    {
      if (!m_mid_close)
        m_mid_close = m_env.GetMethodID(m_class, "close", "()V");
      m_env.CallVoidMethod(m_jthis, m_mid_close);
    }

  /**
   * Implements @c OuptutStream.write(int)
   */
  void write(jint byte)
    {
      if (!m_mid_write_byte)
        m_mid_write_byte = m_env.GetMethodID(m_class, "write", "(I)V");
      m_env.CallVoidMethod(m_jthis, m_mid_write_byte, byte);
    }

  /**
   * Implements @c OuptutStream.write(byte[],int,int)
   */
  void write(const ByteArray& src, jint length = -1, jint offset = 0)
    {
      if (!m_mid_write_bytearray)
        m_mid_write_bytearray = m_env.GetMethodID(m_class, "write", "([BII)V");
      m_env.CallVoidMethod(m_jthis, m_mid_write_bytearray,
                           src.get(), offset,
                           (length >= 0 ? length
                            : src.length() - offset));
    }

  /**
   * Helper method to write data from a native buffer.
   */
  void write(const void* data, jint length, jint offset = 0)
    {
      write(ByteArray(m_env, data, length));
    }

  /**
   * Helper method to write a C string to the stream.
   */
  void write(const char* text)
    {
      write(ByteArray(m_env, text));
    }

  /**
   * Helper method to write a C++ string to the stream.
   */
  void write(const std::string& text)
    {
      write(ByteArray(m_env, text));
    }

private:
  static const char* const m_class_name;
  MethodID m_mid_close;
  MethodID m_mid_write_byte;
  MethodID m_mid_write_bytearray;
};

} // namespace Java

#endif // SVN_JAVAHL_JNIWRAPPER_IO_STREAM_HPP
