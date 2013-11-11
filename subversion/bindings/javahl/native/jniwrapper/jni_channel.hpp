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

#ifndef SVN_JAVAHL_JNIWRAPPER_CHANNEL_HPP
#define SVN_JAVAHL_JNIWRAPPER_CHANNEL_HPP

#include "jni_env.hpp"

namespace Java {

/**
 * Abstract base class for implementing channel read method internals.
 *
 * @since New in 1.9.
 */
struct ChannelReader
{
  /**
   * Reads at most @a length bytes into @a buffer, returning the
   * number of bytes read (which may be zero) or -1 if at
   * end-of-stream.
   */
  virtual jint operator()(Env env, void* buffer, jint length) = 0;
};

/**
 * Abstract base class for implementing channel write method internals.
 *
 * @since New in 1.9.
 */
struct ChannelWriter
{
  /**
   * Writes at exactly @a length bytes from @a buffer, returning the
   * number of bytes written (which may be zero).
   */
  virtual jint operator()(Env env, const void* buffer, jint length) = 0;
};


/**
 * Wrapper for @c java.nio.channels.ByteChannel. Unlike most wrappers,
 * this one does not actually represent a ByteChannel object. The
 * assumption is that the native implementation will want to implement
 * the read and write methods, not invoke them.
 *
 * Also serves as the (protected) base of the Readable- and
 * WritableByteChannel interfaces; this is for purposes of code
 * sharing only. We're not interested in replicating Java's class
 * hierarchy here.
 *
 * @since New in 1.9.
 */
class ByteChannel
{
public:
  /**
   * Constructs a wrapper for @a channel with @a reader and @a writer
   * as the read and write method implementations.
   */
  explicit ByteChannel(Env env, ChannelReader& reader, ChannelWriter& writer)
    : m_env(env),
      m_reader(reader),
      m_writer(writer),
      m_cls_byte_buffer(env.FindClass(m_byte_buffer_class_name))
    {}

  /**
   * Reads bytes into @a destination, which must be a
   * @c java.nio.ByteBuffer instance, from #m_reader.
   * @return the number of bytes read, or -1 if at end-of-stream.
   */
  jint read(jobject destination);

  /**
   * Writes bytes from @a source, which must be a
   * @c java.nio.ByteBuffer instance, to #m_writer.
   * @return the number of bytes written.
   */
  jint write(jobject source);

protected:
  /**
   * Constructor used by read-only subclasses.
   */
  explicit ByteChannel(Env env, ChannelReader& reader)
    : m_env(env),
      m_reader(reader),
      m_writer(m_null_writer),
      m_cls_byte_buffer(env.FindClass(m_byte_buffer_class_name))
    {}

  /**
   * Constructor used by write-only subclasses.
   */
  explicit ByteChannel(Env env, ChannelWriter& writer)
    : m_env(env),
      m_reader(m_null_reader),
      m_writer(writer),
      m_cls_byte_buffer(env.FindClass(m_byte_buffer_class_name))
    {}

private:
  Env m_env;
  ChannelReader& m_reader;
  ChannelWriter& m_writer;

  static ChannelReader& m_null_reader;
  static ChannelWriter& m_null_writer;

  // Private references for the java.nio.ByteBuffer class.
  static const char* const m_byte_buffer_class_name;
  const jclass m_cls_byte_buffer;
  MethodID m_mid_byte_buffer_has_array;
  MethodID m_mid_byte_buffer_get_array;
  MethodID m_mid_byte_buffer_get_array_offset;
  MethodID m_mid_byte_buffer_get_remaining;
  MethodID m_mid_byte_buffer_get_position;
  MethodID m_mid_byte_buffer_set_position;
  MethodID m_mid_byte_buffer_get_bytearray;
  MethodID m_mid_byte_buffer_put_bytearray;
};


/**
 * Wrapper for @c java.nio.channels.ReadableByteChannel.
 *
 * @since New in 1.9.
 */
class ReadableByteChannel : protected ByteChannel
{
public:
  /**
   * Constructs a wrapper for @a channel with @a reader the read
   * method implementation.
   */
  explicit ReadableByteChannel(Env env, ChannelReader& reader)
    : ByteChannel(env, reader)
    {}

  /**
   * Reads bytes into @a destination, which must be a
   * @c java.nio.ByteBuffer instance, from #m_reader.
   * @return the number of bytes read, or -1 if at end-of-stream.
   */
  jint read(jobject destination)
    {
      return ByteChannel::read(destination);
    }
};


/**
 * Wrapper @c java.nio.channels.WritableByteChannel.
 *
 * @since New in 1.9.
 */
class WritableByteChannel : protected ByteChannel
{
public:
  /**
   * Constructs a wrapper for @a channel with @a writer as the write
   * method implementation.
   */
  explicit WritableByteChannel(Env env, ChannelWriter& writer)
    : ByteChannel(env, writer)
    {}

  /**
   * Writes bytes from @a source, which must be a
   * @c java.nio.ByteBuffer instance, to #m_writer.
   * @return the number of bytes written.
   */
  jint write(jobject source)
    {
      return ByteChannel::write(source);
    }
};

} // namespace Java

#endif // SVN_JAVAHL_JNIWRAPPER_CHANNEL_HPP
