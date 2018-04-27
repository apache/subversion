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

#include "jni_globalref.hpp"
#include "jni_io_stream.hpp"
#include "jni_stack.hpp"

#include "svn_private_config.h"

// Stream-wrapper-specific mark object type
struct svn_stream_mark_t
{
  void* m_baton;
};

namespace Java {

namespace {
svn_error_t* stream_close_input(void* baton)
{
  InputStream* const self = static_cast<InputStream*>(baton);
  SVN_JAVAHL_CATCH(self->get_env(), SVN_ERR_BASE, self->close());
  return SVN_NO_ERROR;
}

svn_error_t* stream_mark(void* baton, svn_stream_mark_t** mark,
                         apr_pool_t* result_pool)
{
  InputStream* const self = static_cast<InputStream*>(baton);
  SVN_JAVAHL_CATCH(self->get_env(), SVN_ERR_STREAM_SEEK_NOT_SUPPORTED,
                   self->mark(16384)); // FIXME: invent better readlimit

  *mark = static_cast<svn_stream_mark_t*>(
      apr_palloc(result_pool, sizeof(**mark)));
  (*mark)->m_baton = baton;
  return SVN_NO_ERROR;
}

svn_error_t* stream_seek(void* baton, const svn_stream_mark_t* mark)
{
  if (mark->m_baton != baton)
    return svn_error_create(SVN_ERR_STREAM_SEEK_NOT_SUPPORTED,
                            NULL, _("Invalid mark"));

  InputStream* const self = static_cast<InputStream*>(baton);
  SVN_JAVAHL_CATCH(self->get_env(), SVN_ERR_STREAM_SEEK_NOT_SUPPORTED,
                   self->reset());
  return SVN_NO_ERROR;
}

svn_error_t* stream_read(void* baton, char* buffer, apr_size_t* len)
{
  if (0 == *len)
    return SVN_NO_ERROR;

  jint length = jint(*len);
  InputStream* const self = static_cast<InputStream*>(baton);
  SVN_JAVAHL_CATCH(self->get_env(), SVN_ERR_BASE,
                   length = self->read(buffer, length));
  if (length < 0)
    *len = 0;
  else
    *len = length;
  return SVN_NO_ERROR;
}

svn_error_t* stream_skip(void* baton, apr_size_t len)
{
  InputStream* const self = static_cast<InputStream*>(baton);
  SVN_JAVAHL_CATCH(self->get_env(),  SVN_ERR_BASE, self->skip(jlong(len)));
  return SVN_NO_ERROR;
}

svn_error_t* stream_close_output(void* baton)
{
  OutputStream* const self = static_cast<OutputStream*>(baton);
  SVN_JAVAHL_CATCH(self->get_env(), SVN_ERR_BASE, self->close());
  return SVN_NO_ERROR;
}

svn_error_t* stream_write(void* baton, const char* data, apr_size_t* len)
{
  OutputStream* const self = static_cast<OutputStream*>(baton);
  SVN_JAVAHL_CATCH(self->get_env(), SVN_ERR_BASE,
                   self->write(data, jint(*len)));
  return SVN_NO_ERROR;
}


svn_error_t*
global_stream_close_input(void* baton)
{
  GlobalObject* ref = static_cast<GlobalObject*>(baton);
  InputStream stream(Env(), ref->get());
  return stream_close_input(&stream);
}

svn_error_t*
global_stream_mark(void* baton, svn_stream_mark_t** mark,
                   apr_pool_t* result_pool)
{
  GlobalObject* ref = static_cast<GlobalObject*>(baton);
  InputStream stream(Env(), ref->get());
  return stream_mark(&stream, mark, result_pool);
}

svn_error_t*
global_stream_seek(void* baton, const svn_stream_mark_t* mark)
{
  GlobalObject* ref = static_cast<GlobalObject*>(baton);
  InputStream stream(Env(), ref->get());
  return stream_seek(&stream, mark);
}

svn_error_t*
global_stream_read(void* baton, char* buffer, apr_size_t* len)
{
  GlobalObject* ref = static_cast<GlobalObject*>(baton);
  InputStream stream(Env(), ref->get());
  return stream_read(&stream, buffer, len);
}

svn_error_t*
global_stream_skip(void* baton, apr_size_t len)
{
  GlobalObject* ref = static_cast<GlobalObject*>(baton);
  InputStream stream(Env(), ref->get());
  return stream_skip(&stream, len);
}

svn_error_t*
global_stream_close_output(void* baton)
{
  GlobalObject* ref = static_cast<GlobalObject*>(baton);
  OutputStream stream(Env(), ref->get());
  return stream_close_output(&stream);
}

svn_error_t*
global_stream_write(void* baton, const char* data, apr_size_t* len)
{
  GlobalObject* ref = static_cast<GlobalObject*>(baton);
  OutputStream stream(Env(), ref->get());
  return stream_write(&stream, data, len);
}

apr_status_t cleanup_global_object(void* baton)
{
  delete static_cast<GlobalObject*>(baton);
  return APR_SUCCESS;
}
} // anonymous namespace


// Class Java::InputStream

const char* const InputStream::m_class_name = "java/io/InputStream";
InputStream::ClassImpl::ClassImpl(Env env, jclass cls)
  : Object::ClassImpl(env, cls),
    m_mid_close(env.GetMethodID(cls, "close", "()V")),
    m_mid_mark_supported(env.GetMethodID(cls, "markSupported", "()Z")),
    m_mid_mark(env.GetMethodID(cls, "mark", "(I)V")),
    m_mid_reset(env.GetMethodID(cls, "reset", "()V")),
    m_mid_read_byte(env.GetMethodID(cls, "read", "()I")),
    m_mid_read_bytearray(env.GetMethodID(cls, "read", "([BII)I")),
    m_mid_skip(env.GetMethodID(cls, "skip", "(J)J"))
{}

InputStream::ClassImpl::~ClassImpl() {}

svn_stream_t*
InputStream::get_global_stream(Env env, jobject jstream,
                               const SVN::Pool& pool)
{
  if (!jstream)
    return NULL;

  const bool has_mark = InputStream(env, jstream).mark_supported();

  std::auto_ptr<GlobalObject> baton(new GlobalObject(env, jstream));

  svn_stream_t* const stream = svn_stream_create(baton.get(), pool.getPool());
  svn_stream_set_read2(stream, global_stream_read,
                       NULL /* only partial read support */);
  svn_stream_set_skip(stream, global_stream_skip);
  svn_stream_set_close(stream, global_stream_close_input);
  if (has_mark)
    {
      svn_stream_set_mark(stream, global_stream_mark);
      svn_stream_set_seek(stream, global_stream_seek);
    }

  apr_pool_cleanup_register(pool.getPool(), baton.release(),
                            cleanup_global_object,
                            apr_pool_cleanup_null);
  return stream;
}

svn_stream_t* InputStream::get_stream(const SVN::Pool& pool)
{
  if (!m_jthis)
    return NULL;

  const bool has_mark = mark_supported();

  svn_stream_t* const stream = svn_stream_create(this, pool.getPool());
  svn_stream_set_read2(stream, stream_read,
                       NULL /* only partial read support */);
  svn_stream_set_skip(stream, stream_skip);
  svn_stream_set_close(stream, stream_close_input);
  if (has_mark)
    {
      svn_stream_set_mark(stream, stream_mark);
      svn_stream_set_seek(stream, stream_seek);
    }
  return stream;
}

jint InputStream::read(void* data, jint length)
{
  ByteArray array(m_env, length);
  const jint size = read(array);
  if (size > 0)
    {
      ByteArray::Contents contents(array);
      ::memcpy(static_cast<char*>(data), contents.data(), size);
    }
  return size;
}

// Class Java::OutputStream

const char* const OutputStream::m_class_name = "java/io/OutputStream";

OutputStream::ClassImpl::ClassImpl(Env env, jclass cls)
  : Object::ClassImpl(env, cls),
    m_mid_close(env.GetMethodID(cls, "close", "()V")),
    m_mid_write_byte(env.GetMethodID(cls, "write", "(I)V")),
    m_mid_write_bytearray(env.GetMethodID(cls, "write", "([BII)V"))
{}

OutputStream::ClassImpl::~ClassImpl() {}

svn_stream_t*
OutputStream::get_global_stream(Env env, jobject jstream,
                               const SVN::Pool& pool)
{
  if (!jstream)
    return NULL;

  std::auto_ptr<GlobalObject> baton(new GlobalObject(env, jstream));

  svn_stream_t* const stream = svn_stream_create(baton.get(), pool.getPool());
  svn_stream_set_write(stream, global_stream_write);
  svn_stream_set_close(stream, global_stream_close_output);

  apr_pool_cleanup_register(pool.getPool(), baton.release(),
                            cleanup_global_object,
                            apr_pool_cleanup_null);
  return stream;
}

svn_stream_t* OutputStream::get_stream(const SVN::Pool& pool)
{
  if (!m_jthis)
    return NULL;

  svn_stream_t* const stream = svn_stream_create(this, pool.getPool());
  svn_stream_set_write(stream, stream_write);
  svn_stream_set_close(stream, stream_close_output);
  return stream;
}

} // namespace Java
