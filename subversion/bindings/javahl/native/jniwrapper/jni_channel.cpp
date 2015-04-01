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

#include <stdexcept>

#include "jni_object.hpp"
#include "jni_array.hpp"
#include "jni_channel.hpp"

#include "svn_private_config.h"

namespace Java {

namespace {
// Get the ByteBuffer's internal array.
jbyteArray get_array(Env env, jobject buffer,
                     const MethodID& mid_has_array,
                     const MethodID& mid_get_array)
{
  if (!env.CallBooleanMethod(buffer, mid_has_array))
    return NULL;
  return jbyteArray(env.CallObjectMethod(buffer, mid_get_array));
}

// Get the offset in the ByteBuffer's array. NEVER call this function
// unless the buffer actually has an accessible array.
jint get_array_offset(Env env, jobject buffer,
                      const MethodID& mid_get_array_offset)
{
  return env.CallIntMethod(buffer, mid_get_array_offset);
}

// Get the remaining space in a ByteBuffer.
jint get_remaining(Env env, jobject buffer,
                   const MethodID& mid_get_remaining)
{
  return env.CallIntMethod(buffer, mid_get_remaining);
}

// Get the current position of a ByteBuffer.
jint get_position(Env env, jobject buffer,
                  const MethodID& mid_get_position)
{
  return env.CallIntMethod(buffer, mid_get_position);
}

// Set the new position of a ByteBuffer.
void set_position(Env env, jobject buffer,
                  const MethodID& mid_set_position,
                  jint new_position)
{
  env.CallObjectMethod(buffer, mid_set_position, new_position);
}

// Get byte array contents from a ByteBuffer.
void get_bytearray(Env env, jobject buffer,
                   const MethodID& mid_get_bytearray,
                   ByteArray& array, jint length = -1, jint offset = 0)
{
  env.CallObjectMethod(
      buffer, mid_get_bytearray, array.get(), offset,
      (length >= 0 ? length : (array.length() - offset)));
}

// Put byte array contents into a ByteBuffer.
void put_bytearray(Env env, jobject buffer,
                   const MethodID& mid_put_bytearray,
                   ByteArray& array, jint length = -1, jint offset = 0)
{
  env.CallObjectMethod(buffer, mid_put_bytearray,
                       array.get(), offset,
                       (length >= 0 ? length : (array.length() - offset)));
}

struct BadReaderWriter : public ChannelReader, ChannelWriter
{
  BadReaderWriter() {}

  virtual jint operator()(Env, void*, jint)
    {
      throw std::logic_error(_("Reading from write-only channel"));
    }

  virtual jint operator()(Env, const void*, jint)
    {
      throw std::logic_error(_("Writing to read-only channel"));
    }
} bad_reader_writer;

} // anonymous namespace


ChannelReader& ByteChannel::m_null_reader = bad_reader_writer;
ChannelWriter& ByteChannel::m_null_writer = bad_reader_writer;

const char* const ByteChannel::ByteBuffer::m_class_name =
  "java/nio/ByteBuffer";

ByteChannel::ByteBuffer::ClassImpl::ClassImpl(Env env, jclass cls)
  : Object::ClassImpl(env, cls),
    m_mid_has_array(env.GetMethodID(cls, "hasArray", "()Z")),
    m_mid_get_array(env.GetMethodID(cls, "array", "()[B")),
    m_mid_get_array_offset(env.GetMethodID(cls, "arrayOffset", "()I")),
    m_mid_get_remaining(env.GetMethodID(cls, "remaining", "()I")),
    m_mid_get_position(env.GetMethodID(cls, "position", "()I")),
    m_mid_set_position(env.GetMethodID(cls,  "position",
                                       "(I)Ljava/nio/Buffer;")),
    m_mid_get_bytearray(env.GetMethodID(cls, "get",
                                        "([BII)Ljava/nio/ByteBuffer;")),
    m_mid_put_bytearray(env.GetMethodID(cls, "put",
                                        "([BII)Ljava/nio/ByteBuffer;"))
{}

ByteChannel::ByteBuffer::ClassImpl::~ClassImpl() {}

jint ByteChannel::read(jobject destination)
{
  const ByteBuffer::ClassImpl& bufimpl = ByteBuffer::impl(m_env);

  const jint remaining = get_remaining(m_env, destination,
                                       bufimpl.m_mid_get_remaining);
  if (!remaining)
    {
      // No space in the buffer; don't try to read anything.
      return 0;
    }

  const jint position = get_position(m_env, destination,
                                     bufimpl.m_mid_get_position);

  jint bytes_read = 0;
  void* data = m_env.GetDirectBufferAddress(destination);
  if (data)
    {
      data = static_cast<char*>(data) + position;
      bytes_read = m_reader(m_env, data, remaining);
    }
  else
    {
      // It was not a direct buffer ... see if it has an array.
      jbyteArray raw_array = get_array(m_env, destination,
                                       bufimpl.m_mid_has_array,
                                       bufimpl.m_mid_get_array);
      if (raw_array)
        {
          const jint array_offset = get_array_offset(
              m_env, destination,
              bufimpl.m_mid_get_array_offset);
          ByteArray array(m_env, raw_array);
          ByteArray::MutableContents contents(array);
          data = contents.data();
          data = static_cast<char*>(data) + position + array_offset;
          bytes_read = m_reader(m_env, data, remaining);
        }
    }
  if (data)
    {
      if (bytes_read > 0)
        set_position(m_env, destination,
                     bufimpl.m_mid_set_position,
                     position + bytes_read);
      return bytes_read;
    }

  // No accessible array, either. Oh well. Create a byte array and
  // push it into the buffer.
  ByteArray array(m_env, remaining);
  ByteArray::MutableContents contents(array);
  bytes_read = m_reader(m_env, contents.data(), contents.length());
  if (bytes_read > 0)
    put_bytearray(m_env, destination,
                  bufimpl.m_mid_put_bytearray,
                  array, bytes_read);
  return bytes_read;
}

jint ByteChannel::write(jobject source)
{
  const ByteBuffer::ClassImpl& bufimpl = ByteBuffer::impl(m_env);

  const jint remaining = get_remaining(m_env, source,
                                       bufimpl.m_mid_get_remaining);
  if (!remaining)
    {
      // No data in the buffer; don't try to write anything.
      return 0;
    }

  const jint position = get_position(m_env, source,
                                     bufimpl.m_mid_get_position);

  jint bytes_written = 0;
  const void* data = m_env.GetDirectBufferAddress(source);
  if (data)
    {
      data = static_cast<const char*>(data) + position;
      bytes_written = m_writer(m_env, data, remaining);
    }
  else
    {
      // It was not a direct buffer ... see if it has an array.
      jbyteArray raw_array = get_array(m_env, source,
                                       bufimpl.m_mid_has_array,
                                       bufimpl.m_mid_get_array);
      if (raw_array)
        {
          const jint array_offset = get_array_offset(
              m_env, source,
              bufimpl.m_mid_get_array_offset);
          const ByteArray array(m_env, raw_array);
          ByteArray::Contents contents(array);
          data = contents.data();
          data = static_cast<const char*>(data) + position + array_offset;
          bytes_written = m_writer(m_env, data, remaining);
        }
    }
  if (data)
    {
      if (bytes_written > 0)
        set_position(m_env, source,
                     bufimpl.m_mid_set_position,
                     position + bytes_written);
      return bytes_written;
    }

  // No accessible array, either. Oh well. Get an array from the
  // buffer and read data from that.
  ByteArray array(m_env, remaining);
  get_bytearray(m_env, source,
                bufimpl.m_mid_get_bytearray,
                array);
  ByteArray::Contents contents(array);
  bytes_written = m_writer(m_env, contents.data(), contents.length());
  return bytes_written;
}

} // namespace Java
