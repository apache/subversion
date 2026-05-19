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

#ifndef SVN_JAVAHL_VERSION_HPP
#define SVN_JAVAHL_VERSION_HPP

#include "jniwrapper/jni_object.hpp"

#include "svn_version.h"

namespace JavaHL {

/**
 * Object wrapper for @c org.apache.subversion.javahl.types.Version.
 *
 * @since New in 1.15.
 */
class Version : public ::Java::Object
{
public:
  /**
   * Constructs a wrapper around @a jthis.
   * The constructor does not verify the class of the wrapped object.
   */
  explicit Version(::Java::Env env, jobject jthis)
    : ::Java::Object(env, ::Java::ClassCache::get_version(env), jthis)
    {}

  /**
   * Constructs a new Version object from an svn_version_t.
   */
  static jobject getInstance(::Java::Env env, const svn_version_t &version);

  /**
   * Fills an svn_version_t from this object.
   */
  void getVersion(svn_version_t &version) const;

private:
  /**
   * This object's implementation details.
   */
  class ClassImpl : public Object::ClassImpl
  {
    friend class ::Java::ClassCacheImpl;

  protected:
    explicit ClassImpl(::Java::Env env, jclass cls);

  public:
    virtual ~ClassImpl();

    const ::Java::MethodID m_mid_get_instance;
    const ::Java::MethodID m_mid_get_major;
    const ::Java::MethodID m_mid_get_minor;
    const ::Java::MethodID m_mid_get_patch;
  };

  const ClassImpl& impl() const
    {
      return *dynamic_cast<const ClassImpl*>(m_impl);
    }

  friend class ::Java::ClassCacheImpl;
  static const char* const m_class_name;
};

} // namespace JavaHL

#endif // SVN_JAVAHL_VERSION_HPP
