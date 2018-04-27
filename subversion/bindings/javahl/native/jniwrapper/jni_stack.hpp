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

#ifndef SVN_JAVAHL_JNIWRAPPER_STACK_HPP
#define SVN_JAVAHL_JNIWRAPPER_STACK_HPP

#ifdef SVN_JAVAHL_DEBUG
#  ifndef SVN_JAVAHL_ASSERT_EXCEPTION_THROWN
#    include <cassert>
#    define SVN_JAVAHL_ASSERT_EXCEPTION_THROWN(E) \
       assert((E).ExceptionCheck())
#  endif // SVN_JAVAHL_ASSERT_EXCEPTION_THROWN
#else
#  define SVN_JAVAHL_ASSERT_EXCEPTION_THROWN(E)
# endif // SVN_JAVAHL_DEBUG

#include "../JNIStackElement.h"
#include "jni_env.hpp"
#include "jni_exception.hpp"

#include "svn_error.h"

/**
 * Boilerplate for the native method implementation entry point.
 *
 * Every native method implementation should start by invoking this
 * macro to initialize the logging stack element and begin the
 * try/catch block of the function body.
 *
 * @param C The name of the Java class that declares this method.
 * @param M The (Java) name of the method.
 *
 * This macro expects two additional parameters to be available
 * (either as function arguments or local variables):
 * @c JNIEnv* @a jenv and @c jobject @a jthis.
 *
 * @since New in 1.9.
 */
#define SVN_JAVAHL_JNI_TRY(C, M)                                \
  ::JNIStackElement st_ac_ke_le_me_nt_(jenv, #C, #M, jthis);    \
  try

/**
 * Boilerplate for the native method implementation entry point.
 *
 * Initializes local variable named @a V as a pointer to an instance
 * of the native-bound class @a C.
 *
 * @since New in 1.9.
 */
#define SVN_JAVAHL_GET_BOUND_OBJECT(C, V)               \
  C* const V = C::get_self(::Java::Env(jenv), jthis)

/**
 * Boilerplate for the native method implementation entry point.
 *
 * Like #SVN_JAVAHL_JNI_TRY, but for static methods where the @c jthis
 * argument is not available.
 *
 * This macro expects two additional parameters to be available
 * (either as function arguments or local variables):
 * @c JNIEnv* @a jenv and @c jclass @a jclazz.
 *
 * @since New in 1.9.
 */
#define SVN_JAVAHL_JNI_TRY_STATIC(C, M)                         \
  ::JNIStackElement st_ac_ke_le_me_nt_(jenv, #C, #M, jclazz);   \
  try


/**
 * Boilerplate for the native method implementation exit point.
 *
 * Every native method implementation should end by invoking this
 * macro to close the try/catch block of the function body and handle
 * any exceptions thrown by the method implementation.
 *
 * This boilerplate variant converts C++ exceptions to the Java
 * exception type @a X, but retains exceptions that are already in
 * progress.
 *
 * @since New in 1.9.
 */
#define SVN_JAVAHL_JNI_CATCH_TO_EXCEPTION(X)                            \
  catch (const ::Java::SignalExceptionThrown&)                          \
    {                                                                   \
      SVN_JAVAHL_ASSERT_EXCEPTION_THROWN(::Java::Env(jenv));            \
    }                                                                   \
  catch (const ::std::exception& ex)                                    \
    {                                                                   \
      X(::Java::Env(jenv)).throw_java_exception(ex.what());             \
    }                                                                   \
  catch (...)                                                           \
    {                                                                   \
      const char* const msg = Java::unknown_cxx_exception_message();    \
      X(::Java::Env(jenv)).throw_java_exception(msg);                   \
    }

/**
 * Boilerplate for the native method implementation exit point.
 *
 * Invokes #SVN_JAVAHL_JNI_CATCH_TO_EXCEPTION to throw a
 * @c RuntimeException.
 *
 * @since New in 1.9.
 */
#define SVN_JAVAHL_JNI_CATCH                                            \
  SVN_JAVAHL_JNI_CATCH_TO_EXCEPTION(::Java::RuntimeException)

/**
 * Invocation wrapper for functions that return an @c svn_error_t *.
 *
 * @param E A wrapped environment (@c Java::Env) instance.
 * @param S The statement to execute in the checked context.
 *
 * @since New in 1.9.
 */
#define SVN_JAVAHL_CHECK(E, S)                                          \
  do {                                                                  \
    svn_error_t* const ja_va_hl_err_te_mp_ = (S);                       \
    if (ja_va_hl_err_te_mp_)                                            \
      ::Java::handle_svn_error((E), ja_va_hl_err_te_mp_);               \
  } while(0)

/**
 * Invocation wrapper for calling Java methods that may throw an
 * exception from within a native callback that is expected to return
 * an @c svn_error_t*.
 *
 * @param E A wrapped environment (@c Java::Env) instance.
 * @param C A Subversion or APR error code.
 * @param S The statement to execute in the checked context.
 *
 * @since New in 1.9.
 */
#define SVN_JAVAHL_CATCH(E, C, S)                                       \
  try                                                                   \
    {                                                                   \
      S;                                                                \
    }                                                                   \
  catch (const ::Java::SignalExceptionThrown&)                          \
    {                                                                   \
      SVN_JAVAHL_ASSERT_EXCEPTION_THROWN((E));                          \
      return Java::caught_java_exception_error((C));                    \
    }                                                                   \
  catch (const ::std::exception& ex)                                    \
    {                                                                   \
      const char* const msg = ex.what();                                \
      ::Java::RuntimeException((E)).throw_java_exception(msg);          \
      return svn_error_create((C), NULL, msg);                          \
    }                                                                   \
  catch (...)                                                           \
    {                                                                   \
      const char* const msg = Java::unknown_cxx_exception_message();    \
      ::Java::RuntimeException((E)).throw_java_exception(msg);          \
      return svn_error_create((C), NULL, msg);                          \
    }

/**
 * Exception checker for the oldstyle implementation that does not use
 * the @c Java::Env environment wrapper.
 *
 * @param E A wrapped environment (@c Java::Env) instance.
 *
 * @since New in 1.9.
 */
#define SVN_JAVAHL_OLDSTYLE_EXCEPTION_CHECK(E)  \
  do {                                          \
    if ((E).ExceptionCheck())                   \
      throw ::Java::SignalExceptionThrown();    \
  } while(0)


namespace Java {

/**
 * Handle an error @a err returned from a native function and throws
 * an appropriate Java exception.
 *
 * @since New in 1.9.
 */
void handle_svn_error(Env env, svn_error_t* err);

/**
 * Return a localized error string for an unknown C++ exception.
 *
 * @since New in 1.9.
 */
const char* unknown_cxx_exception_message() throw();

/**
 * Create an svn_error_t for a caught Java exception.
 *
 * @since New in 1.9.
 */
svn_error_t* caught_java_exception_error(apr_status_t status) throw();

} // namespace Java

#endif // SVN_JAVAHL_JNIWRAPPER_STACK_HPP
