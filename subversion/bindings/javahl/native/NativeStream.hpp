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

#ifndef SVN_JAVAHL_NATIVE_STREAM_HPP
#define SVN_JAVAHL_NATIVE_STREAM_HPP

#include <stdexcept>

#include "jniwrapper/jni_array.hpp"

#include "SVNBase.h"

#include "svn_io.h"

namespace JavaHL {

/**
 * Object wrapper for @c org.apache.subversion.javahl.types.NativeInputStream.
 *
 * @since New in 1.9.
 */
class NativeInputStream : public ::SVNBase
{
public:
  /**
   * Construcs the native instance that will be wrapped in a Java obejct.
   * If @a stream is @c NULL, you must call #set_stream before creating
   * the Java wrapper.
   */
  explicit NativeInputStream(svn_stream_t* stream = NULL)
    : m_stream(stream),
      m_mark(NULL)
    {}

  ~NativeInputStream();

  /**
   * Returns a reference to the pool owned by this wrapped object.
   */
  const SVN::Pool& get_pool() const
    {
      return pool;
    }

  /**
   * Sets the @a stream that this object will own.
   * Do not call this function if a stream was passed to the constructor.
   */
  void set_stream(svn_stream_t* stream);

  /**
   * Create the Java object that binds to this native object.
   */
  jobject create_java_wrapper()
    {
      return createCppBoundObject(m_class_name);
    }

  /**
   * Retrieve the address of the native object from the bound Java object.
   */
  static NativeInputStream* get_self(::Java::Env env, jobject jthis);

  static NativeInputStream* get_self_unsafe(::Java::Env env, jobject jthis);

public:
  /**
   * Implements @c InputStream.close().
   * Also disposes the native object.
   */
  void close(::Java::Env env, jobject jthis);

  /**
   * Implements @c InputStream.markSupported().
   */
  bool mark_supported(::Java::Env env) const;

  /**
   * Implements @c InputStream.mark(int).
   * The @c readlimit parameter of the Java method is ignored.
   */
  void mark(::Java::Env env);

  /**
   * Implements @c InputStream.reset().
   */
  void reset(::Java::Env env);

  /**
   * Implements @c InputStream.read().
   */
  jint read(::Java::Env env);

  /**
   * Implements @c InputStream.read(byte[],int,int).
   */
  jint read(::Java::Env env,
            ::Java::ByteArray::MutableContents& dst,
            jint offset, jint length);

  /**
   * Implements @c InputStream.skip(long).
   */
  jlong skip(::Java::Env env, jlong count);

private:
  virtual void dispose(jobject jthis);

  static const char* const m_class_name;
  svn_stream_t* m_stream;
  svn_stream_mark_t* m_mark;
};


/**
 * Object wrapper for @c org.apache.subversion.javahl.types.NativeOutputStream.
 *
 * @since New in 1.9.
 */
class NativeOutputStream : public ::SVNBase
{
public:
  /**
   * Construcs the native instance that will be wrapped in a Java obejct.
   * If @a stream is @c NULL, you must call #set_stream before creating
   * the Java wrapper.
   */
  explicit NativeOutputStream(svn_stream_t* stream = NULL)
    : m_stream(stream)
    {}

  ~NativeOutputStream();

  /**
   * Returns a reference to the pool owned by this wrapped object.
   */
  const SVN::Pool& get_pool() const
    {
      return pool;
    }

  /**
   * Sets the @a stream that this object will own.
   * Do not call this function if a stream was passed to the constructor.
   */
  void set_stream(svn_stream_t* stream);

  /**
   * Create the Java object that binds to this native object.
   */
  jobject create_java_wrapper()
    {
      return createCppBoundObject(m_class_name);
    }

  /**
   * Retrieve the address of the native object from the bound Java object.
   */
  static NativeOutputStream* get_self(::Java::Env env, jobject jthis);

  static NativeOutputStream* get_self_unsafe(::Java::Env env, jobject jthis);

public:
  /**
   * Implements @c OutputStream.close().
   * Also disposes the native object.
   */
  void close(::Java::Env env, jobject jthis);

  /**
   * Implements @c OutputStream.write(int).
   */
  void write(::Java::Env env, jint byte);

  /**
   * Implements @c OutputStream.write(byte[],int,int).
   */
  void write(::Java::Env env,
             const ::Java::ByteArray::Contents& src,
             jint offset, jint length);

private:
  virtual void dispose(jobject jthis);

  static const char* const m_class_name;
  svn_stream_t* m_stream;
};

} // namespace JavaHL

#endif // SVN_JAVAHL_NATIVE_STREAM_HPP
