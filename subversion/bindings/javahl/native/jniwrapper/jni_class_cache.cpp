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

#define SVN_JAVAHL_JNIWRAPPER_LOG(expr)
#include "jni_env.hpp"
#include "jni_globalref.hpp"
#include "jni_exception.hpp"
#include "jni_object.hpp"
#include "jni_string.hpp"

#include "jni_channel.hpp"
#include "jni_io_stream.hpp"
#include "jni_list.hpp"
#include "jni_string_map.hpp"

#include "../SubversionException.hpp"

namespace Java {

const ClassCache* ClassCache::m_instance = NULL;

void ClassCache::create()
{
  const char* exception_message = NULL;

  try
    {
      new ClassCache(Env());
    }
  catch (const std::exception& ex)
    {
      exception_message = ex.what();
    }
  catch (...)
    {
      exception_message = "Caught unknown C++ exception";
    }

  const ::Java::Env env;
  if (exception_message || env.ExceptionCheck())
    {
      const jclass rtx = env.FindClass("java/lang/RuntimeException");
      const jmethodID ctor = env.GetMethodID(rtx, "<init>",
                                             "(Ljava/lang/String;"
                                             "Ljava/lang/Throwable;)V");
      jobject cause = env.ExceptionOccurred();
      if (!cause && exception_message)
        {
          const jstring msg = env.NewStringUTF(exception_message);
          cause = env.NewObject(rtx, ctor, msg, jthrowable(0));
        }
      const jstring reason =
        env.NewStringUTF("JavaHL native library initialization failed");
      const jobject exception = env.NewObject(rtx, ctor, reason, cause);
      env.Throw(jthrowable(exception));
    }
}

void ClassCache::destroy()
{
  const ClassCache* const instance = m_instance;
  m_instance = NULL;
  delete instance;
}

ClassCache::~ClassCache() {}

#define SVN_JAVAHL_JNIWRAPPER_CLASS_CACHE_INIT(M, C)    \
  m_##M(env, env.FindClass(C::m_class_name))
ClassCache::ClassCache(Env env)
  : SVN_JAVAHL_JNIWRAPPER_CLASS_CACHE_INIT(object, Object),
    SVN_JAVAHL_JNIWRAPPER_CLASS_CACHE_INIT(classtype, Class),
    SVN_JAVAHL_JNIWRAPPER_CLASS_CACHE_INIT(throwable, Exception),
    SVN_JAVAHL_JNIWRAPPER_CLASS_CACHE_INIT(string, String),

    SVN_JAVAHL_JNIWRAPPER_CLASS_CACHE_INIT(list, BaseList),
    SVN_JAVAHL_JNIWRAPPER_CLASS_CACHE_INIT(array_list, BaseMutableList),

    SVN_JAVAHL_JNIWRAPPER_CLASS_CACHE_INIT(map, BaseMap),
    SVN_JAVAHL_JNIWRAPPER_CLASS_CACHE_INIT(set, BaseMap::Set),
    SVN_JAVAHL_JNIWRAPPER_CLASS_CACHE_INIT(iterator, BaseMap::Iterator),
    SVN_JAVAHL_JNIWRAPPER_CLASS_CACHE_INIT(map_entry, BaseMap::Entry),
    SVN_JAVAHL_JNIWRAPPER_CLASS_CACHE_INIT(hash_map, BaseMutableMap),

    SVN_JAVAHL_JNIWRAPPER_CLASS_CACHE_INIT(input_stream, InputStream),
    SVN_JAVAHL_JNIWRAPPER_CLASS_CACHE_INIT(output_stream, OutputStream),

    SVN_JAVAHL_JNIWRAPPER_CLASS_CACHE_INIT(byte_buffer,
                                           ByteChannel::ByteBuffer),

    SVN_JAVAHL_JNIWRAPPER_CLASS_CACHE_INIT(subversion_exception,
                                           ::JavaHL::SubversionException)
{
  m_instance = this;
  // no-op: Object::static_init(env);
  Class::static_init(env);
  Exception::static_init(env);
  // no-op: String::static_init(env);

  BaseList::static_init(env);
  BaseMutableList::static_init(env);

  BaseMap::static_init(env);
  BaseMap::Set::static_init(env);
  BaseMap::Iterator::static_init(env);
  BaseMap::Entry::static_init(env);
  BaseMutableMap::static_init(env);

  InputStream::static_init(env);
  OutputStream::static_init(env);

  ByteChannel::ByteBuffer::static_init(env);

  // no-op: ::JavaHL::SubversionException::static_init(env);
}
#undef SVN_JAVAHL_JNIWRAPPER_CLASS_CACHE_INIT

} // namespace Java
