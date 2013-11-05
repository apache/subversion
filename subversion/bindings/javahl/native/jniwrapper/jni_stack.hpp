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

#include "../JNIStackElement.h"
#include "jni_env.hpp"
#include "jni_exception.hpp"

/**
 * Boilerplate for the native method implementation entry point.
 *
 * Every native method implementation should start by invoking this
 * macro to initialize the logging stack element and begin the
 * try/catch block of the function body.
 *
 * @param C The name of the Java class that declares this method.
 * @param M The (Java) name of the method
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
 * Like SVN_JAVAHL_JNI_TRY, but for static methods where the @c jthis
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
 * @since New in 1.9.
 */
#define SVN_JAVAHL_JNI_CATCH                                            \
  catch (const ::JavaHL::JavaException&)                                \
    {}                                                                  \
  catch (const ::std::exception& ex)                                    \
    {                                                                   \
      ::Java::RuntimeException(::Java::Env(jenv))                       \
        .throw_java_exception(ex.what());                               \
    }                                                                   \
  catch (...)                                                           \
    {                                                                   \
      ::Java::RuntimeException(::Java::Env(jenv))                       \
        .throw_java_exception(_("Caught unknown C++ exception"));       \
    }

/**
 * Invocation wrapper for functions that return an @c svn_error_t *.
 *
 * @since New in 1.9.
 */
#define SVN_JAVAHL_CHECK(e)                             \
  do {                                                  \
    svn_error_t* javahl__err__temp = (e);               \
    if (javahl__err__temp)                              \
      {                                                 \
        JNIUtil::handleSVNError(javahl__err__temp);     \
        throw JavaHL::JavaException();                  \
    }                                                   \
  } while(0)

#endif // SVN_JAVAHL_JNIWRAPPER_STACK_HPP
