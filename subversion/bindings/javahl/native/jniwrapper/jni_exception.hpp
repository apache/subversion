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
#include "jni_javahl_exception.hpp"

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
class Exception : public Object
{
public:
  /**
   * Constructs a wrapper for the @c jthrowable object @a exc.
   */
  explicit Exception(Env env, jthrowable exc)
    : Object(env, exc)
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
      throw ::JavaHL::JavaException();
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
      throw ::JavaHL::JavaException();
    }

  /**
   * Raises a Java exception of the concrete class, but does not throw
   * a native exception.
   *
   * It is an error to call this method if an existing @c jthrowable
   * object was wrapped.
   */
  void throw_java_exception() const
    {
      if (instantiated()
          ? m_env.Throw(throwable())
          : m_env.ThrowNew(m_class, NULL))
        throw std::runtime_error(_("Could not throw Java exception"));
    }

  /**
   * Raises a Java exception of the concrete class with the given
   * @a message, but does not throw a native exception.
   *
   * It is an error to call this method if an existing @c jthrowable
   * object was wrapped.
   */
  void throw_java_exception(const char* message) const
    {
      if (m_env.ThrowNew(m_class, message))
        throw std::runtime_error(_("Could not throw Java exception"));
    }

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
      return jthrowable(Object::get());
    }

  /**
   * Wrapper for the Java method @c getMessage().
   * Only valid if an existing @c jthrowable object was wrapped.
   */
  jstring get_message() const;

protected:
  /**
   * Constructs an exception generator with the concrete class
   * @a class_name.
   */
  explicit Exception(Env env, const char* class_name)
    : Object(env, class_name, NULL)
    {}

  /**
   * Constructs an exception generator with the concrete class @a cls.
   */
  explicit Exception(Env env, jclass cls)
    : Object(env, cls, NULL)
    {}

private:
  friend class ClassCache;
  static void static_init(Env env);
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

} // namespace Java

#endif // SVN_JAVAHL_JNIWRAPPER_ENV_HPP
