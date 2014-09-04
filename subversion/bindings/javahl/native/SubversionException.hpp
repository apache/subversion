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

#ifndef SVN_JAVAHL_SUBVERSION_EXCEPTION_HPP
#define SVN_JAVAHL_SUBVERSION_EXCEPTION_HPP

#include "jniwrapper/jni_exception.hpp"

namespace JavaHL {

/**
 * Generator class for exceptions of type
 * @c org.apache.subversion.javahl.SubversionException.
 *
 * The associated JNI class reference is stored for the lifetime of
 * the JVM in the global class cache.
 *
 * @since New in 1.9.
 */
class SubversionException : public ::Java::Exception
{
public:
  explicit SubversionException(::Java::Env env)
    : Java::Exception(env, ::Java::ClassCache::get_subversion_exception(env))
    {}

private:
  friend class ::Java::ClassCacheImpl;
  static const char* const m_class_name;
};

} // namespace JavaHL

#endif // SVN_JAVAHL_SUBVERSION_EXCEPTION_HPP
