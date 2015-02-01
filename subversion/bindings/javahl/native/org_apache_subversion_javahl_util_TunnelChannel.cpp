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
 * @file org_apache_subversion_javahl_util_TunnelChannel.cpp
 * @brief Implementation of the native methods in the Java classes
 *        TunnelChannel, RequestChannel and ResponseChannel
 */

#include <string>

#include <apr_file_io.h>

#include "../include/org_apache_subversion_javahl_util_TunnelChannel.h"
#include "../include/org_apache_subversion_javahl_util_RequestChannel.h"
#include "../include/org_apache_subversion_javahl_util_ResponseChannel.h"

#include "jniwrapper/jni_exception.hpp"
#include "jniwrapper/jni_channel.hpp"
#include "jniwrapper/jni_stack.hpp"

#include "svn_private_config.h"

namespace {
apr_file_t* get_file_descriptor(Java::Env env, jlong jfd)
{
  apr_file_t* fd = reinterpret_cast<apr_file_t*>(jfd);
  if (!fd)
    Java::NullPointerException(env).raise("nativeChannel");
  return fd;
}

void throw_IOException(Java::Env env, const char* message,
                       apr_status_t status)
{
  char buf[1024];
  std::string msg(message);
  apr_strerror(status, buf, sizeof(buf) - 1);
  msg += buf;
  Java::IOException(env).raise(msg.c_str());
}

class TunnelReader : public Java::ChannelReader
{
public:
  explicit TunnelReader(Java::Env env, jlong jnative_channel)
    : m_fd(get_file_descriptor(env, jnative_channel))
    {}

  virtual jint operator()(Java::Env env, void* buffer, jint length)
    {
      if (!length)
        return 0;

      apr_size_t bytes_read = length;
      const apr_status_t status = apr_file_read(m_fd, buffer, &bytes_read);
      if (status && !APR_STATUS_IS_EOF(status))
        {
          throw_IOException(
              env, _("Error reading from native file handle: "),
              status);
          return -1;
        }
      if (APR_STATUS_IS_EOF(status))
        return -1;
      return jint(bytes_read);
    }

private:
  apr_file_t* const m_fd;
};

class TunnelWriter : public Java::ChannelWriter
{
public:
  explicit TunnelWriter(Java::Env env, jlong jnative_channel)
    : m_fd(get_file_descriptor(env, jnative_channel))
    {}

  virtual jint operator()(Java::Env env, const void* buffer, jint length)
    {
      if (!length)
        return 0;

      apr_size_t bytes_written;
      const apr_status_t status =
        apr_file_write_full(m_fd, buffer, length, &bytes_written);
      if (status)
        {
          throw_IOException(
              env, _("Error writing to native file handle: "),
              status);
          return -1;
        }
      return jint(bytes_written);
    }

private:
  apr_file_t* const m_fd;
};

} // anonymous namespace


JNIEXPORT void JNICALL
Java_org_apache_subversion_javahl_util_TunnelChannel_nativeClose(
    JNIEnv* jenv, jclass jclazz, jlong jnative_channel)
{
  SVN_JAVAHL_JNI_TRY_STATIC(TunnelChannel, close)
    {
      const Java::Env env(jenv);

      apr_file_t* const fd = get_file_descriptor(env, jnative_channel);
      if (!fd)
        return;

      const apr_status_t status = apr_file_close(fd);
      if (status)
        throw_IOException(
            env, _("Error closing native file handle: "),
            status);
    }
  SVN_JAVAHL_JNI_CATCH;
}

JNIEXPORT jint JNICALL
Java_org_apache_subversion_javahl_util_RequestChannel_nativeRead(
    JNIEnv* jenv, jclass jclazz, jlong jnative_channel, jobject jdst_buffer)
{
  SVN_JAVAHL_JNI_TRY_STATIC(RequestChannel, read)
    {
      const Java::Env env(jenv);

      TunnelReader reader(env, jnative_channel);
      Java::ReadableByteChannel channel(env, reader);
      return channel.read(jdst_buffer);
    }
  SVN_JAVAHL_JNI_CATCH;
  return -1;
}

JNIEXPORT jint JNICALL
Java_org_apache_subversion_javahl_util_ResponseChannel_nativeWrite(
    JNIEnv* jenv, jclass jclazz, jlong jnative_channel, jobject jsrc_buffer)
{
  SVN_JAVAHL_JNI_TRY_STATIC(ResponseChannel, write)
    {
      const Java::Env env(jenv);

      TunnelWriter writer(env, jnative_channel);
      Java::WritableByteChannel channel(env, writer);
      return channel.write(jsrc_buffer);
    }
  SVN_JAVAHL_JNI_CATCH;
  return -1;
}
