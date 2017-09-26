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

#include <cstring>
#include <memory>
#include <apr.h>

#include "jni_env.hpp"
#include "jni_globalref.hpp"
#include "jni_exception.hpp"
#include "jni_object.hpp"
#include "jni_string.hpp"
#include "jni_array.hpp"
#include "jni_stack.hpp"

#include "../JNIUtil.h"
bool initialize_jni_util(JNIEnv *env);

#include "svn_private_config.h"


// Global library initializaiton

/**
 * Initializer function, called just after the JVM loads the native
 * library.  Stores the global JVM reference and creates the global
 * class cache.
 */
JNIEXPORT jint JNICALL
JNI_OnLoad(JavaVM* jvm, void*)
{
  ::Java::Env::static_init(jvm);
  const ::Java::Env env;

  const apr_status_t status = apr_initialize();
  if (!status)
    ::Java::ClassCache::create();
  else
    {
      char buf[2048];
      std::strcpy(buf, "Could not initialize APR: ");
      const std::size_t offset = std::strlen(buf);
      apr_strerror(status, buf + offset, sizeof(buf) - offset - 1);
      env.ThrowNew(env.FindClass("java/lang/Error"), buf);
    }

  // Initialize the old-style JavaHL infrastructure.
  if (!initialize_jni_util(env.get()) && !env.ExceptionCheck())
    {
      env.ThrowNew(env.FindClass("java/lang/LinkageError"),
                   "Native library initialization failed");
    }

  return JNI_VERSION_1_2;
}

/**
 * Cleanup function, called just before the JVM unloads the native
 * library.  Destroys the global class cache.
 */
JNIEXPORT void JNICALL
JNI_OnUnload(JavaVM*, void*)
{
  ::Java::ClassCache::destroy();
  apr_terminate();
}


namespace Java {

// class Java::Env

::JavaVM* Env::m_jvm = NULL;
void Env::static_init(::JavaVM* jvm)
{
  m_jvm = jvm;
}

const char* Env::error_create_global_reference() throw()
{
  return _("Could not create global reference");
}

const char* Env::error_get_contents_string() throw()
{
  return _("Could not get contents of Java String");
}

const char* Env::error_release_null_string() throw()
{
  return _("Could not release contents of a null String");
}

const char* Env::error_create_object_array() throw()
{
  return _("Could not create Object array");
}

namespace {
// The typed array error messages are always fatal, so allocating
// the buffer on the heap and never releasing it does not really
// constitute a memory leak.
const char* make_typed_error(const char* fmt, const char* type) throw()
{
  const apr_size_t bufsize = 512;
  char *msg = new(std::nothrow) char[bufsize];
  apr_snprintf(msg, bufsize, fmt, type);
  return msg;
}
} // anonymous namespace

const char* Env::error_create_array(const char* type) throw()
{
  return make_typed_error(_("Could not create %sArray"), type);
}

const char* Env::error_get_contents_array(const char* type) throw()
{
  return make_typed_error(_("Could not get %s array contents"), type);
}

const char* Env::error_release_null_array(const char* type) throw()
{
  return make_typed_error(
      _("Could not release contents of a null %sArray"), type);
}

::JNIEnv* Env::env_from_jvm()
{
  if (m_jvm)
    {
      void *penv;
      switch (m_jvm->GetEnv(&penv, JNI_VERSION_1_2))
        {
        case JNI_OK:
          return static_cast<JNIEnv*>(penv);

        case JNI_EDETACHED:
          throw std::runtime_error(
              _("Native thread is not attached to a Java VM"));

        case JNI_EVERSION:
          throw std::runtime_error(_("Unsupported JNI version"));

        default:
          throw std::runtime_error(_("Invalid JNI environment"));
        }
    }
  throw std::logic_error(_("JavaVM instance was not initialized"));
}

void Env::throw_java_out_of_memory(const char* message) const
{
  OutOfMemoryError(*this).raise(message);
}

// class Java::LocalFrame

const jint LocalFrame::DEFAULT_CAPACITY = 16;


// Class Java::Object

const char* const Object::m_class_name = "java/lang/Object";
Object::ClassImpl::~ClassImpl() {}


// Class Java::Class

const char* const Class::m_class_name = "java/lang/Class";
Class::ClassImpl::~ClassImpl() {}

MethodID Class::m_mid_get_class;
MethodID Class::m_mid_get_name;

void Class::static_init(Env env, jclass cls)
{
  m_mid_get_class = env.GetMethodID(
      ClassCache::get_object(env)->get_class(),
      "getClass", "()Ljava/lang/Class;");
  m_mid_get_name = env.GetMethodID(
      cls, "getName",  "()Ljava/lang/String;");
}

namespace{
jobject get_class_of_object(Env env, jobject obj, jmethodID mid_get_class)
{
  if (!obj)
    return NULL;
  return env.CallObjectMethod(obj, mid_get_class);
}
} // anonymous namespace

Class::Class(Env env, jobject obj)
  : m_env(env),
    m_jthis(get_class_of_object(env, obj, m_mid_get_class))
{}

Class::Class(const Object& obj)
  : m_env(obj.get_env()),
    m_jthis(get_class_of_object(obj.get_env(), obj.get(), m_mid_get_class))
{}

jstring Class::get_name() const
{
  if (!m_jthis)
    return NULL;
  return jstring(m_env.CallObjectMethod(m_jthis, m_mid_get_name));
}


// Class Java::String

const char* const String::m_class_name = "java/lang/String";
String::ClassImpl::~ClassImpl() {}

const char* String::strdup(apr_pool_t* pool) const
{
  return apr_pstrdup(pool, String::Contents(*this).c_str());
}

void String::MutableContents::set_value(const char* new_text)
{
  if (!m_new_text)
    throw std::invalid_argument(
        _("Cannot set String contents to null"));
  if (m_text)
    {
      m_new_text = new_text;
      m_length = jsize(::std::strlen(new_text));
    }
  else
    throw std::logic_error(
        _("Cannot change the contents of a null String"));
}

// class Java::Exception

void Exception::throw_java_exception() const
{
  if (instantiated()
      ? m_env.Throw(m_jthis)
      : m_env.ThrowNew(m_class, NULL))
    throw std::runtime_error(_("Could not throw Java exception"));
}

void Exception::throw_java_exception(const char* message) const
{
  if (m_env.ThrowNew(m_class, message))
    throw std::runtime_error(_("Could not throw Java exception"));
}

jstring Exception::get_message() const
{
  if (instantiated())
    return jstring(m_env.CallObjectMethod(m_jthis, m_mid_get_message));
  throw std::logic_error(_("Could not get exception message:"
                           " Exception instance is not available"));
}

const char* const Exception::m_class_name = "java/lang/Throwable";
Exception::ClassImpl::~ClassImpl() {}

MethodID Exception::m_mid_get_message;
void Exception::static_init(Env env, jclass cls)
{
  m_mid_get_message = env.GetMethodID(
      cls, "getMessage", "()Ljava/lang/String;");
}

// Other exception class initializers

const char* const RuntimeException::m_class_name =
  "java/lang/RuntimeException";

const char* const NullPointerException::m_class_name =
  "java/lang/NullPointerException";

const char* const OutOfMemoryError::m_class_name =
  "java/lang/OutOfMemoryError";

const char* const IndexOutOfBoundsException::m_class_name =
  "java/lang/IndexOutOfBoundsException";
IndexOutOfBoundsException::ClassImpl::~ClassImpl() {}

const char* const IOException::m_class_name =
  "java/io/IOException";

const char* const IllegalArgumentException::m_class_name =
  "java/lang/IllegalArgumentException";

const char *const NoSuchElementException::m_class_name =
  "java/util/NoSuchElementException";
NoSuchElementException::ClassImpl::~ClassImpl() {}

// Implementation of jni_stack.hpp

void handle_svn_error(Env env, ::svn_error_t* err)
{
  jthrowable cause = NULL;

  // If the exception being currently thrown was generated by the
  // JavaHL bindings, then assume the error was propagated through
  // native code and do not re-throw it.
  if (env.ExceptionCheck())
    {
      cause = env.ExceptionOccurred();
      if (env.IsInstanceOf(
              cause, ClassCache::get_subversion_exception(env)->get_class()))
        {
          // XXX FIXME: Should really have a special error code
          // specifically for propagating Java exceptions from
          // callbacks through native code.
          svn_error_clear(err);
          throw SignalExceptionThrown();
        }
    }

  // Make sure there's only a single exception in the environment.
  if (cause)
    env.ExceptionClear();

  ::JNIUtil::handleSVNError(err, cause);
  throw SignalExceptionThrown();
}

const char* unknown_cxx_exception_message() throw()
{
  return _("Caught unknown C++ exception");
}

svn_error_t* caught_java_exception_error(apr_status_t status) throw()
{
  return svn_error_create(status, JNIUtil::wrapJavaException(),
                          _("Java exception"));
}

} // namespace Java
