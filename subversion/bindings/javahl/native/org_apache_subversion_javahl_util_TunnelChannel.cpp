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

#include <assert.h>             // TEMPORARY until we handle weird byte arrays

#include <string>

#include <apr_file_io.h>

#include "../include/org_apache_subversion_javahl_util_TunnelChannel.h"
#include "../include/org_apache_subversion_javahl_util_RequestChannel.h"
#include "../include/org_apache_subversion_javahl_util_ResponseChannel.h"

#include "JNIUtil.h"
#include "JNIStackElement.h"
#include "JNIByteArray.h"

#include "svn_private_config.h"

namespace {
apr_file_t* get_file_descriptor(jlong jfd)
{
  apr_file_t* fd = reinterpret_cast<apr_file_t*>(jfd);
  if (!fd)
    {
      JNIUtil::throwNullPointerException("nativeChannel");
      return NULL;
    }
  return fd;
}

void throw_IOException(const char* message, apr_status_t status)
{
  std::string msg(message);
  if (status)
    {
      char buf[1024];
      apr_strerror(status, buf, sizeof(buf) - 1);
      msg += ": ";
      msg += buf;
    }
  JNIUtil::raiseThrowable("java/io/IOException", msg.c_str());
}

class ByteBufferProxy
{
public:
  ByteBufferProxy(jobject buf, JNIEnv* env)
    : m_buf(buf),
      m_direct(env->GetDirectBufferAddress(buf)),
      m_array(m_direct ? NULL : get_array(buf, env)),
      m_array_offset(m_array ? get_array_offset(buf, env) : 0),
      m_offset(get_position(buf, env)),
      m_size(get_remaining(buf, env))
    {}

  jint read(apr_file_t* fd, JNIEnv* env)
    {
      if (!m_size)
        return 0;

      JNIByteArray arr(m_array, false, false);
      apr_size_t bytes_read = m_size;
      apr_status_t status = apr_file_read(
          fd, get_base_address(arr), &bytes_read);
      if (status && !APR_STATUS_IS_EOF(status))
        {
          throw_IOException(_("Error reading from native file handle"),
                            status);
          return 0;
        }
      update_position(bytes_read, env);
      return jint(bytes_read);
    }

  jint write(apr_file_t* fd, JNIEnv* env)
    {
      if (!m_size)
        return 0;

      JNIByteArray arr(m_array);
      apr_size_t bytes_written;
      apr_status_t status = apr_file_write_full(
          fd, get_base_address(arr), m_size, &bytes_written);
      if (status)
        {
          throw_IOException(_("Error writing to native file handle"),
                            status);
          return 0;
        }
      update_position(bytes_written, env);
      return jint(bytes_written);
    }

private:
  void *get_base_address(JNIByteArray& arr)
    {
      void* base = (m_direct ? m_direct
                    : const_cast<signed char*>(arr.getBytes()));
      // FIXME: We do not currently support buffers that are nether
      // direct, nor have an accessible array.
      assert(base != 0);
      return static_cast<char*>(base) + m_offset + m_array_offset;
    }

  void update_position(apr_size_t amount, JNIEnv* env)
    {
      jmethodID mid = env->GetMethodID(
          env->GetObjectClass(m_buf), "position", "(I)Ljava/nio/Buffer;");
      if (mid)
        env->CallObjectMethod(m_buf, mid, jint(amount));
    }

  static jbyteArray get_array(jobject buf, JNIEnv* env)
    {
      jclass cls = env->GetObjectClass(buf);
      jmethodID mid = env->GetMethodID(cls, "hasArray", "()Z");
      if (!mid)
        return NULL;

      jboolean has_array = env->CallBooleanMethod(buf, mid);
      if (!has_array)
        return NULL;

      mid = env->GetMethodID(cls, "array", "()[B");
      if (mid)
        return jbyteArray(env->CallObjectMethod(buf, mid));
      return NULL;
    }

  static apr_size_t get_array_offset(jobject buf, JNIEnv* env)
    {
      jmethodID mid = env->GetMethodID(
          env->GetObjectClass(buf), "arrayOffset", "()I");
      if (mid)
        return env->CallIntMethod(buf, mid);
      return 0;
    }

  static apr_size_t get_position(jobject buf, JNIEnv* env)
    {
      jmethodID mid = env->GetMethodID(
          env->GetObjectClass(buf), "position", "()I");
      if (mid)
        return env->CallIntMethod(buf, mid);
      return 0;
    }

  static apr_size_t get_remaining(jobject buf, JNIEnv* env)
    {
      jmethodID mid = env->GetMethodID(
          env->GetObjectClass(buf), "remaining", "()I");
      if (mid)
        return env->CallIntMethod(buf, mid);
      return 0;
    }

  jobject m_buf;
  void *m_direct;
  jbyteArray m_array;
  apr_size_t m_array_offset;
  apr_size_t m_offset;
  apr_size_t m_size;
};
} // anonymous namespace

JNIEXPORT void JNICALL
Java_org_apache_subversion_javahl_util_TunnelChannel_nativeClose(
    JNIEnv* env, jclass jclazz, jlong nativeChannel)
{
  JNIEntryStatic(TunnelChannel, close);
  apr_file_t* fd = reinterpret_cast<apr_file_t*>(nativeChannel);
  if (!fd)
    return;

  apr_status_t status = apr_file_close(fd);
  if (status)
    throw_IOException(_("Error closing native file handle"), status);
}

JNIEXPORT jint JNICALL
Java_org_apache_subversion_javahl_util_RequestChannel_nativeRead(
    JNIEnv* env, jclass jclazz, jlong nativeChannel, jobject dst)
{
  JNIEntryStatic(RequestChannel, read);
  apr_file_t* fd = get_file_descriptor(nativeChannel);
  if (fd)
    return ByteBufferProxy(dst, env).read(fd, env);
  return -1;
}

JNIEXPORT jint JNICALL
Java_org_apache_subversion_javahl_util_ResponseChannel_nativeWrite(
    JNIEnv* env, jclass jclazz, jlong nativeChannel, jobject src)
{
  JNIEntryStatic(ResponseChannel, write);
  apr_file_t* fd = get_file_descriptor(nativeChannel);
  if (fd)
    return ByteBufferProxy(src, env).write(fd, env);
  return -1;
}
