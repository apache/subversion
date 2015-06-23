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

#include "NativeStream.hpp"

#include "jniwrapper/jni_stack.hpp"
#include "jniwrapper/jni_exception.hpp"

#include "svn_private_config.h"

namespace JavaHL {

// Class JavaHL::NativeInputStream

const char* const NativeInputStream::m_class_name =
    JAVAHL_CLASS("/types/NativeInputStream");

NativeInputStream::~NativeInputStream() {}

void NativeInputStream::set_stream(svn_stream_t* stream)
{
  if (m_stream)
    throw std::logic_error(_("Native input stream is already bound"));
  m_stream = stream;
}

NativeInputStream*
NativeInputStream::get_self_unsafe(::Java::Env env, jobject jthis)
{
  jfieldID fid_cppaddr = NULL;
  const jlong cppaddr =
    findCppAddrForJObject(jthis, &fid_cppaddr, m_class_name);
  return reinterpret_cast<NativeInputStream*>(cppaddr);
}

NativeInputStream*
NativeInputStream::get_self(::Java::Env env, jobject jthis)
{
  NativeInputStream* self = get_self_unsafe(env, jthis);
  if (!self)
    ::Java::NullPointerException(env).raise(_("this [C++]"));
  return self;
}

void NativeInputStream::close(::Java::Env env, jobject jthis)
{
  SVN_JAVAHL_CHECK(env, svn_stream_close(m_stream));
  dispose(jthis);
}

bool NativeInputStream::mark_supported(::Java::Env env) const
{
  return svn_stream_supports_mark(m_stream);
}

void NativeInputStream::mark(::Java::Env env)
{
  if (!svn_stream_supports_mark(m_stream))
    return;
  SVN_JAVAHL_CHECK(env, svn_stream_mark(m_stream, &m_mark, pool.getPool()));
}

void NativeInputStream::reset(::Java::Env env)
{
  if (!svn_stream_supports_mark(m_stream))
    return;
  if (m_mark)
    SVN_JAVAHL_CHECK(env, svn_stream_seek(m_stream, m_mark));
  else
    ::Java::IOException(env).raise(_("Invalid seek on native stream"));
 }

jint NativeInputStream::read(::Java::Env env)
{
  apr_size_t len = 1;
  char byte;
  SVN_JAVAHL_CHECK(env, svn_stream_read_full(m_stream, &byte, &len));
  if (len == 0)
    return -1;                  // EOF
  if (len == 1)
    return jint(byte & 0xff);
  ::Java::IOException(env).raise(_("Read from native stream failed"));
  return -1;
}

jint NativeInputStream::read(::Java::Env env,
                             ::Java::ByteArray::MutableContents& dst,
                             jint offset, jint length)
{
  if (offset < 0 || length < 0 || offset + length > dst.length())
    ::Java::IndexOutOfBoundsException(env).raise();
  if (!dst.data())
    ::Java::NullPointerException(env).raise();

  apr_size_t len = length;
  if (svn_stream_supports_partial_read(m_stream))
    SVN_JAVAHL_CHECK(env, svn_stream_read2(m_stream,
                                           dst.data() + offset, &len));
  else
    SVN_JAVAHL_CHECK(env, svn_stream_read_full(m_stream,
                                               dst.data() + offset, &len));
  if (len == 0)
    return -1;                  // EOF
  if (len <= length)
    return jint(len);
  ::Java::IOException(env).raise(_("Read from native stream failed"));
  return -1;
}

jlong NativeInputStream::skip(::Java::Env env, jlong count)
{
  const apr_size_t len = count;
  SVN_JAVAHL_CHECK(env, svn_stream_skip(m_stream, len));
  return count;
}

void NativeInputStream::dispose(jobject jthis)
{
  jfieldID fid_cppaddr = NULL;
  SVNBase::dispose(jthis, &fid_cppaddr, m_class_name);
}


// Class JavaHL::NativeOutputStream

const char* const NativeOutputStream::m_class_name =
  JAVAHL_CLASS("/types/NativeOutputStream");

NativeOutputStream::~NativeOutputStream() {}

void NativeOutputStream::set_stream(svn_stream_t* stream)
{
  if (m_stream)
    throw std::logic_error(_("Native output stream is already bound"));
  m_stream = stream;
}

NativeOutputStream*
NativeOutputStream::get_self_unsafe(::Java::Env env, jobject jthis)
{
  jfieldID fid_cppaddr = NULL;
  const jlong cppaddr =
    findCppAddrForJObject(jthis, &fid_cppaddr, m_class_name);
  return reinterpret_cast<NativeOutputStream*>(cppaddr);
}

NativeOutputStream*
NativeOutputStream::get_self(::Java::Env env, jobject jthis)
{
  NativeOutputStream* self = get_self_unsafe(env, jthis);
  if (!self)
    ::Java::NullPointerException(env).raise(_("this [C++]"));
  return self;
}

void NativeOutputStream::close(::Java::Env env, jobject jthis)
{
  SVN_JAVAHL_CHECK(env, svn_stream_close(m_stream));
  dispose(jthis);
}

void NativeOutputStream::write(::Java::Env env, jint byte)
{
  const char data = char(byte & 0xff);
  apr_size_t len = 1;
  SVN_JAVAHL_CHECK(env, svn_stream_write(m_stream, &data, &len));
  if (len != 1)
    ::Java::IOException(env).raise(_("Write to native stream failed"));
}

void NativeOutputStream::write(::Java::Env env,
                               const ::Java::ByteArray::Contents& src,
                               jint offset, jint length)
{
  if (offset < 0 || length < 0 || offset + length > src.length())
    ::Java::IndexOutOfBoundsException(env).raise();
  if (!src.data())
    ::Java::NullPointerException(env).raise();

  apr_size_t len = length;
  SVN_JAVAHL_CHECK(env, svn_stream_write(m_stream, src.data() + offset, &len));
  if (len != length)
    ::Java::IOException(env).raise(_("Write to native stream failed"));
}

void NativeOutputStream::dispose(jobject jthis)
{
  jfieldID fid_cppaddr = NULL;
  SVNBase::dispose(jthis, &fid_cppaddr, m_class_name);
}

} // namespace JavaHL


// Class JavaHL::NativeInputStream native method implementation
#include "../include/org_apache_subversion_javahl_types_NativeInputStream.h"

JNIEXPORT void JNICALL
Java_org_apache_subversion_javahl_types_NativeInputStream_close(
    JNIEnv* jenv, jobject jthis)
{
  SVN_JAVAHL_JNI_TRY(NativeInputStream, close)
    {
      SVN_JAVAHL_GET_BOUND_OBJECT(JavaHL::NativeInputStream, self);
      self->close(Java::Env(jenv), jthis);
    }
  SVN_JAVAHL_JNI_CATCH_TO_EXCEPTION(Java::IOException);
}

JNIEXPORT jboolean JNICALL
Java_org_apache_subversion_javahl_types_NativeInputStream_markSupported(
    JNIEnv* jenv, jobject jthis)
{
  SVN_JAVAHL_JNI_TRY(NativeInputStream, markSupported)
    {
      SVN_JAVAHL_GET_BOUND_OBJECT(JavaHL::NativeInputStream, self);
      self->mark_supported(Java::Env(jenv));
    }
  SVN_JAVAHL_JNI_CATCH_TO_EXCEPTION(Java::IOException);
  return false;
}

JNIEXPORT void JNICALL
Java_org_apache_subversion_javahl_types_NativeInputStream_mark(
    JNIEnv* jenv, jobject jthis, jint)
{
  SVN_JAVAHL_JNI_TRY(NativeInputStream, mark)
    {
      SVN_JAVAHL_GET_BOUND_OBJECT(JavaHL::NativeInputStream, self);
      self->mark(Java::Env(jenv));
    }
  SVN_JAVAHL_JNI_CATCH_TO_EXCEPTION(Java::IOException);
}

JNIEXPORT void JNICALL
Java_org_apache_subversion_javahl_types_NativeInputStream_reset(
    JNIEnv* jenv, jobject jthis)
{
  SVN_JAVAHL_JNI_TRY(NativeInputStream, reset)
    {
      SVN_JAVAHL_GET_BOUND_OBJECT(JavaHL::NativeInputStream, self);
      self->reset(Java::Env(jenv));
    }
  SVN_JAVAHL_JNI_CATCH_TO_EXCEPTION(Java::IOException);
}

JNIEXPORT jint JNICALL
Java_org_apache_subversion_javahl_types_NativeInputStream_read__(
    JNIEnv* jenv, jobject jthis)
{
  SVN_JAVAHL_JNI_TRY(NativeInputStream, read)
    {
      SVN_JAVAHL_GET_BOUND_OBJECT(JavaHL::NativeInputStream, self);
      return self->read(Java::Env(jenv));
    }
  SVN_JAVAHL_JNI_CATCH_TO_EXCEPTION(Java::IOException);
  return 0;
}

JNIEXPORT jint JNICALL
Java_org_apache_subversion_javahl_types_NativeInputStream_read___3BII(
    JNIEnv* jenv, jobject jthis, jbyteArray jdst, jint joffset, jint jlength)
{
  SVN_JAVAHL_JNI_TRY(NativeInputStream, read)
    {
      SVN_JAVAHL_GET_BOUND_OBJECT(JavaHL::NativeInputStream, self);

      const Java::Env env(jenv);
      Java::ByteArray dst(env, jdst);
      Java::ByteArray::MutableContents dst_contents(dst);

      return self->read(env, dst_contents, joffset, jlength);
    }
  SVN_JAVAHL_JNI_CATCH_TO_EXCEPTION(Java::IOException);
  return 0;
}

JNIEXPORT jlong JNICALL
Java_org_apache_subversion_javahl_types_NativeInputStream_skip(
    JNIEnv* jenv, jobject jthis, jlong jcount)
{
  SVN_JAVAHL_JNI_TRY(NativeInputStream, skip)
    {
      SVN_JAVAHL_GET_BOUND_OBJECT(JavaHL::NativeInputStream, self);
      return self->skip(Java::Env(jenv), jcount);
    }
  SVN_JAVAHL_JNI_CATCH_TO_EXCEPTION(Java::IOException);
  return 0;
}

JNIEXPORT void JNICALL
Java_org_apache_subversion_javahl_types_NativeInputStream_finalize(
    JNIEnv* jenv, jobject jthis)
{
  SVN_JAVAHL_JNI_TRY(NativeInputStream, finalize)
    {
      JavaHL::NativeInputStream* native =
        JavaHL::NativeInputStream::get_self_unsafe(Java::Env(jenv), jthis);
      if (native != NULL)
          native->finalize();
    }
  SVN_JAVAHL_JNI_CATCH;
}


// Class JavaHL::NativeOutputStream native method implementation
#include "../include/org_apache_subversion_javahl_types_NativeOutputStream.h"

JNIEXPORT void JNICALL
Java_org_apache_subversion_javahl_types_NativeOutputStream_close(
    JNIEnv* jenv, jobject jthis)
{
  SVN_JAVAHL_JNI_TRY(NativeOutputStream, close)
    {
      SVN_JAVAHL_GET_BOUND_OBJECT(JavaHL::NativeOutputStream, self);
      self->close(Java::Env(jenv), jthis);
    }
  SVN_JAVAHL_JNI_CATCH_TO_EXCEPTION(Java::IOException);
}

JNIEXPORT void JNICALL
Java_org_apache_subversion_javahl_types_NativeOutputStream_write__I(
    JNIEnv* jenv, jobject jthis, jint byte)
{
  SVN_JAVAHL_JNI_TRY(NativeOutputStream, write)
    {
      SVN_JAVAHL_GET_BOUND_OBJECT(JavaHL::NativeOutputStream, self);
      self->write(Java::Env(jenv), byte);
    }
  SVN_JAVAHL_JNI_CATCH_TO_EXCEPTION(Java::IOException);
}

JNIEXPORT void JNICALL
Java_org_apache_subversion_javahl_types_NativeOutputStream_write___3BII(
    JNIEnv* jenv, jobject jthis, jbyteArray jsrc, jint joffset, jint jlength)
{
  SVN_JAVAHL_JNI_TRY(NativeOutputStream, write)
    {
      SVN_JAVAHL_GET_BOUND_OBJECT(JavaHL::NativeOutputStream, self);

      const Java::Env env(jenv);
      const Java::ByteArray src(env, jsrc);

      self->write(env, Java::ByteArray::Contents(src), joffset, jlength);
    }
  SVN_JAVAHL_JNI_CATCH_TO_EXCEPTION(Java::IOException);
}

JNIEXPORT void JNICALL
Java_org_apache_subversion_javahl_types_NativeOutputStream_finalize(
    JNIEnv* jenv, jobject jthis)
{
  SVN_JAVAHL_JNI_TRY(NativeOutputStream, finalize)
    {
      JavaHL::NativeOutputStream* native =
        JavaHL::NativeOutputStream::get_self_unsafe(Java::Env(jenv), jthis);
      if (native != NULL)
          native->finalize();
    }
  SVN_JAVAHL_JNI_CATCH;
}
