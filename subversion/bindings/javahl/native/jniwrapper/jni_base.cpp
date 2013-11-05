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

#include "jni_env.hpp"
#include "jni_globalref.hpp"
#include "jni_exception.hpp"
#include "jni_object.hpp"
#include "jni_array.hpp"


// Global library initializaiton

/**
 * Initializer function, called just after the JVM loads the native
 * library.  Stores the global JVM reference and creates the global
 * class cache.
 */
JNIEXPORT jint JNICALL
JNI_OnLoad(JavaVM* jvm, void*)
{
  ::Java::Env::m_jvm = jvm;
  new ::Java::ClassCache(::Java::Env());
  return JNI_VERSION_1_2;
}

/**
 * Cleanup function, called just before the JVM unloads the native
 * library.  Destroys the global class cache.
 */
JNIEXPORT void JNICALL
JNI_OnUnload(JavaVM*, void*)
{
  delete ::Java::ClassCache::m_instance;
}


namespace Java {

// class Java::Env

::JavaVM* Env::m_jvm = NULL;

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


// class Java::GlobalObject

GlobalObject& GlobalObject::operator=(jobject that)
{
  this->~GlobalObject();
  return *new(this) GlobalObject(Env(), that);
}

GlobalObject::~GlobalObject()
{
  Env().DeleteGlobalRef(m_obj);
}


// class Java::GlobalClass

GlobalClass& GlobalClass::operator=(jclass that)
{
  this->~GlobalClass();
  return *new(this) GlobalClass(Env(), that);
}


// Class Java::Object

const char* const Object::m_class_name = "java/lang/Object";


// Class Java::Class

const char* const Class::m_class_name = "java/lang/Class";
jmethodID Class::m_mid_get_class = NULL;
jmethodID Class::m_mid_get_name = NULL;
void Class::static_init(Env env)
{
  m_mid_get_class = env.GetMethodID(
      ClassCache::get_object(),
      "getClass", "()Ljava/lang/Class;");
  m_mid_get_name = env.GetMethodID(
      ClassCache::get_classtype(),
      "getName",  "()Ljava/lang/String;");
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
  : Object(env, ClassCache::get_classtype(),
           get_class_of_object(env, obj, m_mid_get_class))
{}

Class::Class(const Object& obj)
  : Object(obj.get_env(), ClassCache::get_classtype(),
           get_class_of_object(obj.get_env(), obj.get(), m_mid_get_class))
{}

jstring Class::get_name() const
{
  if (!m_jthis)
    return NULL;
  return jstring(m_env.CallObjectMethod(m_jthis, m_mid_get_name));
}


// Class Java::String

const char* const String::m_class_name = "java/lang/String";
void String::static_init(Env env)
{
  // TODO: Init various string methods
}


// class Java::Exception

jstring Exception::get_message() const
{
  if (instantiated())
    return jstring(m_env.CallObjectMethod(m_jthis, m_mid_get_message));
  throw std::logic_error(_("Could not get exception message:"
                           " Exception instance is not available"));
}

const char* const Exception::m_class_name = "java/lang/Throwable";
jmethodID Exception::m_mid_get_message = NULL;
void Exception::static_init(Env env)
{
  m_mid_get_message = env.GetMethodID(
      ClassCache::get_throwable(),
      "getMessage", "()Ljava/lang/String;");
}


// Other exception class initializers

const char* const RuntimeException::m_class_name =
  "java/lang/RuntimeException";

const char* const NullPointerException::m_class_name =
  "java/lang/NullPointerException";

const char* const OutOfMemoryError::m_class_name =
  "java/lang/OutOfMemoryError";

} // namespace Java


namespace JavaHL {

// class JavaHL::JavaException

JavaException::~JavaException() throw() {}

const char* JavaException::what() const throw()
{
  // FIXME: Implement this?
  return "";
}

jthrowable JavaException::get_java_exception() const
{
  return ::Java::Env().ExceptionOccurred();
}

jthrowable JavaException::get_java_exception(const ::Java::Env& env) const
{
  return env.ExceptionOccurred();
}

} // namespace JavaHL
