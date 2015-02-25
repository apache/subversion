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


#ifndef SVN_JAVAHL_JNIWRAPPER_EXCEPTION_HPP
#define SVN_JAVAHL_JNIWRAPPER_EXCEPTION_HPP

#include "jni_env.hpp"
#include "jni_object.hpp"

namespace Java {

/**
 * Base class for all exception generators, and generator class for
 * exceptions of type @c java.lang.Throwable.
 *
 * The associated JNI class reference is stored for the lifetime of
 * the JVM in the global class cache.
 *
 * @since New in 1.9.
 */
class Exception
{
public:
  /**
   * Constructs a wrapper for the @c jthrowable object @a exc.
   */
  explicit Exception(Env env, jthrowable exc)
    : m_env(env),
      m_jthis(exc),
      m_class(env.GetObjectClass(exc))
    {}

  /**
   * Raises a Java exception of the concrete class, and throws a
   * native exception at the same time.
   *
   * It is an error to call this method if an existing @c jthrowable
   * object was wrapped.
   */
  void raise() const
    {
      throw_java_exception();
      throw SignalExceptionThrown();
    }

  /**
   * Raises a Java exception of the concrete class with the givem
   * @a message, and throws a native exception at the same time.
   *
   * It is an error to call this method if an existing @c jthrowable
   * object was wrapped.
   */
  void raise(const char* message) const
    {
      throw_java_exception(message);
      throw SignalExceptionThrown();
    }

  /**
   * Raises a Java exception of the concrete class, but does not throw
   * a native exception.
   *
   * It is an error to call this method if an existing @c jthrowable
   * object was wrapped.
   */
  void throw_java_exception() const;

  /**
   * Raises a Java exception of the concrete class with the given
   * @a message, but does not throw a native exception.
   *
   * It is an error to call this method if an existing @c jthrowable
   * object was wrapped.
   */
  void throw_java_exception(const char* message) const;

  /**
   * Checks if an existing @c jthrowable object was wrapped.
   */
  bool instantiated() const
    {
      return (m_jthis != NULL);
    }

  /**
   * Returns the wrapped @c jthrowable object.
   */
  jthrowable throwable() const
    {
      return m_jthis;
    }

  /**
   * Wrapper for the Java method @c getMessage().
   * Only valid if an existing @c jthrowable object was wrapped.
   */
  jstring get_message() const;


  /**
   * Returns the wrapped exception instance.
   */
  jobject get() const
    {
      return m_jthis;
    }

  /**
   * Returns the wrapped exception class.
   */
  jclass get_class() const
    {
      return m_class;
    }

  /**
   * Returns the wrapped enviromnment reference.
   */
  Env get_env() const
    {
      return m_env;
    }

protected:
  /**
   * Constructs an exception generator with the concrete class
   * @a class_name.
   */
  explicit Exception(Env env, const char* class_name)
    : m_env(env),
      m_jthis(NULL),
      m_class(env.FindClass(class_name))
    {}

  /**
   * Constructs an exception generator with the concrete class @a cls.
   */
  explicit Exception(Env env, const Object::ClassImpl* impl)
    : m_env(env),
      m_jthis(NULL),
      m_class(impl->get_class())
    {}

private:
  /**
   * This object's implementation details.
   */
  class ClassImpl : public Object::ClassImpl
  {
    friend class ClassCacheImpl;

  protected:
    explicit ClassImpl(Env env, jclass cls)
      : Object::ClassImpl(env, cls)
      {}

  public:
    virtual ~ClassImpl();
  };

  const Env m_env;
  jthrowable m_jthis;
  jclass m_class;

  friend class ClassCacheImpl;
  static void static_init(Env env, jclass cls);
  static const char* const m_class_name;
  static MethodID m_mid_get_message;
};

/**
 * Generator class for exceptions of type @c java.lang.RuntimeException.
 *
 * @since New in 1.9.
 */
class RuntimeException : public Exception
{
public:
  /**
   * Constructs an exception generator object.
   */
  explicit RuntimeException(Env env)
    : Exception(env, m_class_name)
    {}

private:
  static const char* const m_class_name;
};


/**
 * Generator class for exceptions of type @c java.lang.NullPointerException.
 *
 * @since New in 1.9.
 */
class NullPointerException : public Exception
{
public:
  /**
   * Constructs an exception generator object.
   */
  explicit NullPointerException(Env env)
    : Exception(env, m_class_name)
    {}

private:
  static const char* const m_class_name;
};

/**
 * Generator class for exceptions of type @c java.lang.OutOfMemoryError.
 *
 * @since New in 1.9.
 */
class OutOfMemoryError : public Exception
{
public:
  /**
   * Constructs an exception generator object.
   */
  explicit OutOfMemoryError(Env env)
    : Exception(env, m_class_name)
    {}

private:
  static const char* const m_class_name;
};

/**
 * Generator class for exceptions of type
 * @c java.lang.IndexOutOfBoundsException.
 *
 * @since New in 1.9.
 */
class IndexOutOfBoundsException : public Exception
{
public:
  /**
   * Constructs an exception generator object.
   */
  explicit IndexOutOfBoundsException(Env env)
    : Exception(env, m_class_name)
    {}

private:
  /**
   * This object's implementation details.
   */
  class ClassImpl : public Object::ClassImpl
  {
    friend class ClassCacheImpl;

  protected:
    explicit ClassImpl(Env env, jclass cls)
      : Object::ClassImpl(env, cls)
      {}

  public:
    virtual ~ClassImpl();
  };

  friend class ClassCacheImpl;
  static const char* const m_class_name;
};

/**
 * Generator class for exceptions of type @c java.io.IOException.
 *
 * @since New in 1.9.
 */
class IOException : public Exception
{
public:
  /**
   * Constructs an exception generator object.
   */
  explicit IOException(Env env)
    : Exception(env, m_class_name)
    {}

private:
  static const char* const m_class_name;
};

/**
 * Generator class for exceptions of type @c java.lang.IllegalArgumentException.
 *
 * @since New in 1.9.
 */
class IllegalArgumentException : public Exception
{
public:
  /**
   * Constructs an exception generator object.
   */
  explicit IllegalArgumentException(Env env)
    : Exception(env, m_class_name)
    {}

private:
  static const char* const m_class_name;
};

/**
 * Generator class for exceptions of type
 * @c java.util.NoSuchElementException.
 *
 * @since New in 1.9.
 */
class NoSuchElementException : public Exception
{
public:
  /**
   * Constructs an exception generator object.
   */
  explicit NoSuchElementException(Env env)
    : Exception(env, m_class_name)
    {}

private:
  /**
   * This object's implementation details.
   */
  class ClassImpl : public Object::ClassImpl
  {
    friend class ClassCacheImpl;

  protected:
    explicit ClassImpl(Env env, jclass cls)
      : Object::ClassImpl(env, cls)
      {}

  public:
    virtual ~ClassImpl();
  };

  friend class ClassCacheImpl;
  static const char* const m_class_name;
};

} // namespace Java

#endif // SVN_JAVAHL_JNIWRAPPER_EXCEPTION_HPP
