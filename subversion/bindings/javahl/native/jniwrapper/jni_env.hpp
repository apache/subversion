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

#ifndef SVN_JAVAHL_JNIWRAPPER_ENV_HPP
#define SVN_JAVAHL_JNIWRAPPER_ENV_HPP

#include <jni.h>
#include <cstdarg>
#include <stdexcept>

#ifdef SVN_JAVAHL_DEBUG
#  ifndef SVN_JAVAHL_JNIWRAPPER_LOG
#    include <iostream>
#    define SVN_JAVAHL_JNIWRAPPER_LOG(expr)      \
       (std::cerr << expr << std::endl)
#  endif // SVN_JAVAHL_JNIWRAPPER_LOG
#else
#  define SVN_JAVAHL_JNIWRAPPER_LOG(expr)
#endif // SVN_JAVAHL_DEBUG

namespace Java {

/**
 * A C++ exception object for signalling that a Java exception has
 * been thrown.
 *
 * Thrown to unwind the stack while avoiding code clutter when a Java
 * exception is detected in the JNI environment.
 *
 * @since New in 1.9.
 */
class SignalExceptionThrown {};

/**
 * Auto-initializing proxy for the JNI method ID.
 *
 * Behaves like a @c jmethodID but automatically initializes to @c NULL.
 *
 * @since New in 1.9.
 */
class MethodID
{
public:
  MethodID()
    : m_mid(NULL)
    {}

  MethodID(jmethodID mid)
    : m_mid(mid)
    {}

  MethodID(const MethodID& that)
    : m_mid(that.m_mid)
    {}

  MethodID& operator=(jmethodID mid)
    {
      m_mid = mid;
      return *this;
    }

  MethodID& operator=(const MethodID& that)
    {
      m_mid = that.m_mid;
      return *this;
    }

  operator jmethodID() const
    {
      return m_mid;
    }

  operator bool() const
    {
      return (NULL != m_mid);
    }

private:
  jmethodID m_mid;
};


/**
 * Auto-initializing proxy for the JNI field ID.
 *
 * Behaves like a @c jfieldID but automatically initializes to @c NULL.
 *
 * @since New in 1.9.
 */
class FieldID
{
public:
  FieldID()
    : m_fid(NULL)
    {}

  FieldID(jfieldID mid)
    : m_fid(mid)
    {}

  FieldID(const FieldID& that)
    : m_fid(that.m_fid)
    {}

  FieldID& operator=(jfieldID fid)
    {
      m_fid = fid;
      return *this;
    }

  FieldID& operator=(const FieldID& that)
    {
      m_fid = that.m_fid;
      return *this;
    }

  operator jfieldID() const
    {
      return m_fid;
    }

  operator bool() const
    {
      return (NULL != m_fid);
    }

private:
  jfieldID m_fid;
};

/**
 * Encapsulation of a JNI environment reference.
 *
 * This class wraps all (relevant) JNI functions and checks for thrown
 * exceptions, so that call sites don't have to be cluttered with KNI
 * exception checks.
 *
 * @since New in 1.9.
 */
class Env
{
public:
  /**
   * Constructs an environment object, retrieving the JNI environment
   * reference from the global JVM reference.
   */
  explicit Env()
    : m_env(env_from_jvm())
    {}

  /**
   * Given a JNI renvironment reference, constructs an environment object.
   */
  explicit Env(::JNIEnv* env)
    : m_env(env)
    {}

  /**
   * Returns the wrapped JNI environment reference.
   *
   * This method is present for compatibility with the old-style
   * native implmentation that needs the raw pointer, and will be
   * removed presently. Do not use it in new-style code.
   */
  ::JNIEnv* get() const
    {
      SVN_JAVAHL_JNIWRAPPER_LOG("Warning: Direct access to JNIEnv at "
                                << __FILE__ << ":" << __LINE__);
      return m_env;
    }

  /** Wrapped JNI function. */
  jobject NewGlobalRef(jobject obj) const
    {
      jobject ret = m_env->NewGlobalRef(obj);
      check_java_exception();
      if (!ret)
        throw_java_out_of_memory(error_create_global_reference());
      return ret;
    }

  /** Wrapped JNI function. */
  void DeleteGlobalRef(jobject obj) const throw()
    {
      m_env->DeleteGlobalRef(obj);
    }

  /** Wrapped JNI function. */
  void PushLocalFrame(jint capacity) const
    {
      if (0 > m_env->PushLocalFrame(capacity))
        throw_java_exception();
    }

  /** Wrapped JNI function. */
  void PopLocalFrame() const throw()
    {
      m_env->PopLocalFrame(NULL);
    }

  /** Wrapped JNI function. */
  jint Throw(jthrowable exc) const throw()
    {
      return m_env->Throw(exc);
    }

  /** Wrapped JNI function. */
  jint ThrowNew(jclass cls, const char* message) const throw()
    {
      return m_env->ThrowNew(cls, message);
    }

  /** Wrapped JNI function. */
  jboolean ExceptionCheck() const throw()
    {
      return m_env->ExceptionCheck();
    }

  /** Wrapped JNI function. */
  jthrowable ExceptionOccurred() const throw()
    {
      return m_env->ExceptionOccurred();
    }

  /** Wrapped JNI function. */
  void ExceptionClear() const throw()
    {
      m_env->ExceptionClear();
    }

  /** Wrapped JNI function. */
  jclass FindClass(const char* name) const
    {
      jclass cls = m_env->FindClass(name);
      check_java_exception();
      return cls;
    }

  /** Wrapped JNI function. */
  jobject NewObject(jclass cls, jmethodID ctor, ...) const
    {
      std::va_list args;
      va_start(args, ctor);
      jobject obj = m_env->NewObjectV(cls, ctor, args);
      va_end(args);
      check_java_exception();
      return obj;
    }

  /** Wrapped JNI function. */
  jclass GetObjectClass(jobject obj) const
    {
      jclass cls = m_env->GetObjectClass(obj);
      check_java_exception();
      return cls;
    }

  /** Wrapped JNI function. */
  jboolean IsInstanceOf(jobject obj, jclass cls) const throw()
    {
      return m_env->IsInstanceOf(obj, cls);
    }

  /** Wrapped JNI function. */
  jmethodID GetMethodID(jclass cls, const char* name, const char* sig) const
    {
      jmethodID mid = m_env->GetMethodID(cls, name, sig);
      check_java_exception();
      return mid;
    }

  /** Wrapped JNI function. */
  jmethodID GetStaticMethodID(jclass cls, const char* name,
                              const char* sig) const
    {
      jmethodID mid = m_env->GetStaticMethodID(cls, name, sig);
      check_java_exception();
      return mid;
    }

  /** Wrapped JNI function. */
  jfieldID GetFieldID(jclass cls, const char* name, const char* sig) const
    {
      jfieldID fid = m_env->GetFieldID(cls, name, sig);
      check_java_exception();
      return fid;
    }

  /** Wrapped JNI function. */
  jfieldID GetStaticFieldID(jclass cls, const char* name,
                            const char* sig) const
    {
      jfieldID fid = m_env->GetStaticFieldID(cls, name, sig);
      check_java_exception();
      return fid;
    }

  /** Wrapped JNI function. */
  jstring NewStringUTF(const char* text) const
    {
      if (!text)
        return NULL;

      jstring str = m_env->NewStringUTF(text);
      check_java_exception();
      return str;
    }

  /** Wrapped JNI function. */
  jsize GetStringLength(jstring str) const
    {
      jsize len = m_env->GetStringLength(str);
      check_java_exception();
      return len;
    }

  /** Wrapped JNI function. */
  jsize GetStringUTFLength(jstring str) const
    {
      jsize len = m_env->GetStringUTFLength(str);
      check_java_exception();
      return len;
    }

  /** Wrapped JNI function. */
  const char* GetStringUTFChars(jstring str, jboolean* is_copy) const
    {
      if (!str)
        return NULL;

      const char* text = m_env->GetStringUTFChars(str, is_copy);
      check_java_exception();
      if (!text)
        throw_java_out_of_memory(error_get_contents_string());
      return text;
    }

  /** Wrapped JNI function. */
  void ReleaseStringUTFChars(jstring str, const char* new_text) const
    {
      if (!str)
        throw std::logic_error(error_release_null_string());
      m_env->ReleaseStringUTFChars(str, new_text);
    }

  /** Wrapped JNI function. */
  void CallVoidMethod(jobject obj, jmethodID mid, ...) const
    {
      std::va_list args;
      va_start(args, mid);
      m_env->CallObjectMethodV(obj, mid, args);
      va_end(args);
      check_java_exception();
    }

  /** Boilerplate generator for wrapped JNI functions. */
#define SVN_JAVAHL_JNIWRAPPER_CALL_X_METHOD(T, N)               \
  T Call##N##Method(jobject obj, jmethodID mid, ...) const      \
    {                                                           \
      std::va_list args;                                        \
      va_start(args, mid);                                      \
      T ret = m_env->Call##N##MethodV(obj, mid, args);          \
      va_end(args);                                             \
      check_java_exception();                                   \
      return ret;                                               \
    }
  SVN_JAVAHL_JNIWRAPPER_CALL_X_METHOD(jobject, Object)
  SVN_JAVAHL_JNIWRAPPER_CALL_X_METHOD(jboolean, Boolean)
  SVN_JAVAHL_JNIWRAPPER_CALL_X_METHOD(jbyte, Byte)
  SVN_JAVAHL_JNIWRAPPER_CALL_X_METHOD(jchar, Char)
  SVN_JAVAHL_JNIWRAPPER_CALL_X_METHOD(jshort, Short)
  SVN_JAVAHL_JNIWRAPPER_CALL_X_METHOD(jint, Int)
  SVN_JAVAHL_JNIWRAPPER_CALL_X_METHOD(jlong, Long)
  SVN_JAVAHL_JNIWRAPPER_CALL_X_METHOD(jfloat, Float)
  SVN_JAVAHL_JNIWRAPPER_CALL_X_METHOD(jdouble, Double)
#undef SVN_JAVAHL_JNIWRAPPER_CALL_X_METHOD

  /** Boilerplate generator for wrapped JNI functions. */
#define SVN_JAVAHL_JNIWRAPPER_CALL_STATIC_X_METHOD(T, N)                \
  T CallStatic##N##Method(jclass obj, jmethodID mid, ...) const         \
    {                                                                   \
      std::va_list args;                                                \
      va_start(args, mid);                                              \
      T ret = m_env->CallStatic##N##MethodV(obj, mid, args);            \
      va_end(args);                                                     \
      check_java_exception();                                           \
      return ret;                                                       \
    }
  SVN_JAVAHL_JNIWRAPPER_CALL_STATIC_X_METHOD(jobject, Object)
  SVN_JAVAHL_JNIWRAPPER_CALL_STATIC_X_METHOD(jboolean, Boolean)
  SVN_JAVAHL_JNIWRAPPER_CALL_STATIC_X_METHOD(jbyte, Byte)
  SVN_JAVAHL_JNIWRAPPER_CALL_STATIC_X_METHOD(jchar, Char)
  SVN_JAVAHL_JNIWRAPPER_CALL_STATIC_X_METHOD(jshort, Short)
  SVN_JAVAHL_JNIWRAPPER_CALL_STATIC_X_METHOD(jint, Int)
  SVN_JAVAHL_JNIWRAPPER_CALL_STATIC_X_METHOD(jlong, Long)
  SVN_JAVAHL_JNIWRAPPER_CALL_STATIC_X_METHOD(jfloat, Float)
  SVN_JAVAHL_JNIWRAPPER_CALL_STATIC_X_METHOD(jdouble, Double)
#undef SVN_JAVAHL_JNIWRAPPER_CALL_STATIC_X_METHOD

  /** Boilerplate generator for wrapped JNI functions. */
#define SVN_JAVAHL_JNIWRAPPER_GET_X_FIELD(T, N)                 \
  T Get##N##Field(jobject obj, jfieldID fid) const              \
    {                                                           \
      T ret = m_env->Get##N##Field(obj, fid);                   \
      check_java_exception();                                   \
      return ret;                                               \
    }
  SVN_JAVAHL_JNIWRAPPER_GET_X_FIELD(jobject, Object)
  SVN_JAVAHL_JNIWRAPPER_GET_X_FIELD(jboolean, Boolean)
  SVN_JAVAHL_JNIWRAPPER_GET_X_FIELD(jbyte, Byte)
  SVN_JAVAHL_JNIWRAPPER_GET_X_FIELD(jchar, Char)
  SVN_JAVAHL_JNIWRAPPER_GET_X_FIELD(jshort, Short)
  SVN_JAVAHL_JNIWRAPPER_GET_X_FIELD(jint, Int)
  SVN_JAVAHL_JNIWRAPPER_GET_X_FIELD(jlong, Long)
  SVN_JAVAHL_JNIWRAPPER_GET_X_FIELD(jfloat, Float)
  SVN_JAVAHL_JNIWRAPPER_GET_X_FIELD(jdouble, Double)
#undef SVN_JAVAHL_JNIWRAPPER_GET_X_FIELD

  /** Boilerplate generator for wrapped JNI functions. */
#define SVN_JAVAHL_JNIWRAPPER_SET_X_FIELD(T, N)                 \
  void Set##N##Field(jobject obj, jfieldID fid, T val) const    \
    {                                                           \
      m_env->Set##N##Field(obj, fid, val);                      \
      check_java_exception();                                   \
    }
  SVN_JAVAHL_JNIWRAPPER_SET_X_FIELD(jobject, Object)
  SVN_JAVAHL_JNIWRAPPER_SET_X_FIELD(jboolean, Boolean)
  SVN_JAVAHL_JNIWRAPPER_SET_X_FIELD(jbyte, Byte)
  SVN_JAVAHL_JNIWRAPPER_SET_X_FIELD(jchar, Char)
  SVN_JAVAHL_JNIWRAPPER_SET_X_FIELD(jshort, Short)
  SVN_JAVAHL_JNIWRAPPER_SET_X_FIELD(jint, Int)
  SVN_JAVAHL_JNIWRAPPER_SET_X_FIELD(jlong, Long)
  SVN_JAVAHL_JNIWRAPPER_SET_X_FIELD(jfloat, Float)
  SVN_JAVAHL_JNIWRAPPER_SET_X_FIELD(jdouble, Double)
#undef SVN_JAVAHL_JNIWRAPPER_SET_X_FIELD

  /** Boilerplate generator for wrapped JNI functions. */
#define SVN_JAVAHL_JNIWRAPPER_GET_STATIC_X_FIELD(T, N)          \
  T GetStatic##N##Field(jclass cls, jfieldID fid) const         \
    {                                                           \
      T ret = m_env->GetStatic##N##Field(cls, fid);             \
      check_java_exception();                                   \
      return ret;                                               \
    }
  SVN_JAVAHL_JNIWRAPPER_GET_STATIC_X_FIELD(jobject, Object)
  SVN_JAVAHL_JNIWRAPPER_GET_STATIC_X_FIELD(jboolean, Boolean)
  SVN_JAVAHL_JNIWRAPPER_GET_STATIC_X_FIELD(jbyte, Byte)
  SVN_JAVAHL_JNIWRAPPER_GET_STATIC_X_FIELD(jchar, Char)
  SVN_JAVAHL_JNIWRAPPER_GET_STATIC_X_FIELD(jshort, Short)
  SVN_JAVAHL_JNIWRAPPER_GET_STATIC_X_FIELD(jint, Int)
  SVN_JAVAHL_JNIWRAPPER_GET_STATIC_X_FIELD(jlong, Long)
  SVN_JAVAHL_JNIWRAPPER_GET_STATIC_X_FIELD(jfloat, Float)
  SVN_JAVAHL_JNIWRAPPER_GET_STATIC_X_FIELD(jdouble, Double)
#undef SVN_JAVAHL_JNIWRAPPER_GET_STATIC_X_FIELD

  /** Boilerplate generator for wrapped JNI functions. */
#define SVN_JAVAHL_JNIWRAPPER_SET_STATIC_X_FIELD(T, N)                  \
  void SetStatic##N##Field(jclass cls, jfieldID fid, T val) const       \
    {                                                                   \
      m_env->SetStatic##N##Field(cls, fid, val);                        \
      check_java_exception();                                           \
    }
  SVN_JAVAHL_JNIWRAPPER_SET_STATIC_X_FIELD(jobject, Object)
  SVN_JAVAHL_JNIWRAPPER_SET_STATIC_X_FIELD(jboolean, Boolean)
  SVN_JAVAHL_JNIWRAPPER_SET_STATIC_X_FIELD(jbyte, Byte)
  SVN_JAVAHL_JNIWRAPPER_SET_STATIC_X_FIELD(jchar, Char)
  SVN_JAVAHL_JNIWRAPPER_SET_STATIC_X_FIELD(jshort, Short)
  SVN_JAVAHL_JNIWRAPPER_SET_STATIC_X_FIELD(jint, Int)
  SVN_JAVAHL_JNIWRAPPER_SET_STATIC_X_FIELD(jlong, Long)
  SVN_JAVAHL_JNIWRAPPER_SET_STATIC_X_FIELD(jfloat, Float)
  SVN_JAVAHL_JNIWRAPPER_SET_STATIC_X_FIELD(jdouble, Double)
#undef SVN_JAVAHL_JNIWRAPPER_SET_STATIC_X_FIELD

  /** Wrapped JNI function. */
  jsize GetArrayLength(jarray array) const
    {
      if (!array)
        return 0;
      return m_env->GetArrayLength(array);
    }

  /** Wrapped JNI function. */
  jobjectArray NewObjectArray(jsize length, jclass cls, jobject init) const
    {
      jobjectArray array = m_env->NewObjectArray(length, cls, init);
      if (!array)
        throw_java_out_of_memory(error_create_object_array());
      return array;
    }

  /** Wrapped JNI function. */
  jobject GetObjectArrayElement(jobjectArray array, jsize index) const
    {
      jobject obj = m_env->GetObjectArrayElement(array, index);
      check_java_exception();
      return obj;
    }

  /** Wrapped JNI function. */
  void SetObjectArrayElement(jobjectArray array,
                             jsize index, jobject value) const
    {
      m_env->SetObjectArrayElement(array, index, value);
      check_java_exception();
    }

  /** Boilerplate generator for wrapped JNI functions. */
#define SVN_JAVAHL_JNIWRAPPER_PRIMITIVE_TYPE_ARRAY(T, N)                \
  T##Array New##N##Array(jsize length) const                            \
    {                                                                   \
      T##Array array = m_env->New##N##Array(length);                    \
      if (!array)                                                       \
        throw_java_out_of_memory(error_create_array(#T));               \
      return array;                                                     \
    }                                                                   \
  T* Get##N##ArrayElements(T##Array array, jboolean* is_copy) const     \
    {                                                                   \
      if (!array)                                                       \
        return NULL;                                                    \
                                                                        \
      T* data = m_env->Get##N##ArrayElements(array, is_copy);           \
      check_java_exception();                                           \
      if (!data)                                                        \
        throw_java_out_of_memory(error_get_contents_array(#N));         \
      return data;                                                      \
    }                                                                   \
  void Release##N##ArrayElements(T##Array array, T* data, jint mode) const \
    {                                                                   \
      if (!array)                                                       \
        throw std::logic_error(error_release_null_array(#T));           \
      m_env->Release##N##ArrayElements(array, data, mode);              \
    }

  SVN_JAVAHL_JNIWRAPPER_PRIMITIVE_TYPE_ARRAY(jboolean, Boolean)
  SVN_JAVAHL_JNIWRAPPER_PRIMITIVE_TYPE_ARRAY(jbyte, Byte)
  SVN_JAVAHL_JNIWRAPPER_PRIMITIVE_TYPE_ARRAY(jchar, Char)
  SVN_JAVAHL_JNIWRAPPER_PRIMITIVE_TYPE_ARRAY(jshort, Short)
  SVN_JAVAHL_JNIWRAPPER_PRIMITIVE_TYPE_ARRAY(jint, Int)
  SVN_JAVAHL_JNIWRAPPER_PRIMITIVE_TYPE_ARRAY(jlong, Long)
  SVN_JAVAHL_JNIWRAPPER_PRIMITIVE_TYPE_ARRAY(jfloat, Float)
  SVN_JAVAHL_JNIWRAPPER_PRIMITIVE_TYPE_ARRAY(jdouble, Double)
#undef SVN_JAVAHL_JNIWRAPPER_PRIMITIVE_TYPE_ARRAY

  /** Wrapped JNI function. */
  void* GetDirectBufferAddress(jobject buffer) const
    {
      void* const addr = m_env->GetDirectBufferAddress(buffer);
      check_java_exception();
      return addr;
    }

private:
  ::JNIEnv* m_env;
  static ::JavaVM* m_jvm;
  static ::JNIEnv* env_from_jvm();

  void throw_java_exception() const
    {
      throw SignalExceptionThrown();
    }

  void check_java_exception() const
    {
      if (m_env->ExceptionCheck())
        throw SignalExceptionThrown();
    }

  void throw_java_out_of_memory(const char* message) const;

  // We cannont use svn_private_config.h in a header, so we move the
  // actual message translations into the implementation file.
  static const char* error_create_global_reference() throw();
  static const char* error_get_contents_string() throw();
  static const char* error_release_null_string() throw();

  static const char* error_create_object_array() throw();
  static const char* error_create_array(const char* type) throw();
  static const char* error_get_contents_array(const char* type) throw();
  static const char* error_release_null_array(const char* type) throw();

public:
  // This static initializer must only be called by JNI_OnLoad
  static void static_init(::JavaVM*);
};


/**
 * Encapsulation of a JNI local frame.
 *
 * Used within loop bodies to limit the proliferation of local
 * references, or anywhere else where such references should be
 * pre-emptively discarded.
 *
 * @since New in 1.9.
 */
class LocalFrame
{
  static const jint DEFAULT_CAPACITY;

public:
  /**
   * Constructs a local frame, retrieving the JNI environment
   * reference from the global JVM reference.
   */
  explicit LocalFrame()
    : m_env(Env())
    {
      m_env.PushLocalFrame(DEFAULT_CAPACITY);
    }

  /**
   * Given a JNI renvironment reference, constructs a local frame.
   */
  explicit LocalFrame(Env env)
    : m_env(env)
    {
      m_env.PushLocalFrame(DEFAULT_CAPACITY);
    }

  /**
   * Constructs a local frame with the given initial @a capacity,
   * retrieving the JNI environment reference from the global JVM
   * reference.
   */
  explicit LocalFrame(jint capacity)
    : m_env(Env())
    {
      m_env.PushLocalFrame(capacity);
    }

  /**
   * Given a JNI renvironment reference, constructs a local frame with
   * the given initial @a capacity.
   */
  explicit LocalFrame(Env env, jint capacity)
    : m_env(env)
    {
      m_env.PushLocalFrame(capacity);
    }

  ~LocalFrame()
    {
      m_env.PopLocalFrame();
    }

  /**
   * Returns the stored enviromnent object.
   */
  Env get_env() const
    {
      return m_env;
    }

private:
  const Env m_env;
  LocalFrame(const LocalFrame&);
  LocalFrame& operator=(const LocalFrame&);
};

} // namespace Java

#endif // SVN_JAVAHL_JNIWRAPPER_ENV_HPP
