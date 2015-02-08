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

#ifndef SVN_JAVAHL_JNIWRAPPER_ITERATOR_HPP
#define SVN_JAVAHL_JNIWRAPPER_ITERATOR_HPP

#include "jni_env.hpp"
#include "jni_object.hpp"

namespace Java {

/**
 * Non-template base for a Java iterator.
 *
 * @since New in 1.9.
 */
class BaseIterator : public Object
{
protected:
  /**
   * Constructs the iterator wrapper.
   */
  explicit BaseIterator(Env env, jobject jiterator)
    : Object(env, ClassCache::get_iterator(env), jiterator)
    {}

public:
  /**
   * Returns @c false at the end of the iteration.
   */
  bool has_next() const
    {
      return m_env.CallBooleanMethod(m_jthis, impl().m_mid_has_next);
    }

  /**
   * Returns the next object in the iteration.
   * @throw std::range_error if the next object is not available.
   */
  jobject next();

private:
  /**
   * This object's implementation details.
   */
  class ClassImpl : public Object::ClassImpl
  {
    friend class ClassCacheImpl;

  protected:
    explicit ClassImpl(Env env, jclass cls);

  public:
    virtual ~ClassImpl();

    const MethodID m_mid_has_next;
    const MethodID m_mid_next;
  };

  const ClassImpl& impl() const
    {
      return *dynamic_cast<const ClassImpl*>(m_impl);
    }

  friend class ClassCacheImpl;
  static const char* const m_class_name;
};

} // namespace Java

#endif // SVN_JAVAHL_JNIWRAPPER_ITERATOR_HPP
