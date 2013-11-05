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

#ifndef SVN_JAVAHL_JNIWRAPPER_LIST_HPP
#define SVN_JAVAHL_JNIWRAPPER_LIST_HPP

#include <vector>

#include "jni_env.hpp"
#include "jni_object.hpp"

namespace Java {

/**
 * Non-template base for an immutable type-safe Java list.
 *
 * Converts the list to a @c std::vector of @c jobject references.
 *
 * @since New in 1.9.
 */
class BaseList : public Object
{
  typedef std::vector<jobject> ovector;

public:
  typedef ovector::size_type size_type;

  /**
   * Returns the number of elements in the list.
   */
  size_type length() const
    {
      return m_contents.size();
    }

protected:
  explicit BaseList(Env env, jobject jlist)
    : Object(env, m_class_name, jlist),
      m_contents(convert_to_vector(env, m_class, m_jthis))
    {}

  /**
   * Returns the object reference at @a index.
   */
  jobject operator[](ovector::size_type index) const
    {
      return m_contents[index];
    }

private:
  static const char* const m_class_name;
  static ovector convert_to_vector(Env env, jclass cls, jobject jlist);
  const ovector m_contents;
};

/**
 * Template wrapper for an immutable type-safe Java list.
 *
 * @since New in 1.9.
 */
template <typename T>
class List : public BaseList
{
public:
  /**
   * Constructs the list wrapper, converting the contents to an
   * @c std::vector.
   */
  explicit List(Env env, jobject jlist)
    : BaseList(env, jlist)
    {}

  /**
   * Returns a wrapper object for the object reference at @a index.
   */
  T operator[](size_type index) const
    {
      return T(m_env, BaseList::operator[](index));
    }
};

} // namespace Java

#endif // SVN_JAVAHL_JNIWRAPPER_LIST_HPP
