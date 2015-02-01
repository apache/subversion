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

#include <algorithm>
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
  /**
   * Returns the number of elements in the list.
   */
  jint length() const
    {
      return jint(m_contents.size());
    }

protected:
  /**
   * Constructs the list wrapper, converting the contents to an
   * @c std::vector.
   */
  explicit BaseList(Env env, jobject jlist)
    : Object(env, ClassCache::get_list(env), jlist),
      m_contents(convert_to_vector(env, m_jthis))
    {}

  /**
   * Returns the object reference at @a index.
   * @throw std::out_of_range if the index value is not valid.
   */
  jobject operator[](jint index) const
    {
      return m_contents[ovector::size_type(index)];
    }

  const ovector m_contents;

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

    const MethodID m_mid_size;
    const MethodID m_mid_get;
  };

  friend class ClassCacheImpl;
  static const char* const m_class_name;
  static ovector convert_to_vector(Env env, jobject jlist);

  const ClassImpl& impl() const
    {
      return *dynamic_cast<const ClassImpl*>(m_impl);
    }
};

/**
 * Template wrapper for an immutable type-safe Java list.
 *
 * @since New in 1.9.
 */
template <typename T, typename NativeT=jobject>
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
   * @throw std::out_of_range if the index value is not valid.
   */
  T operator[](jint index) const
    {
      return T(m_env, NativeT(BaseList::operator[](index)));
    }

  /**
   * Iterates over the items in the list, calling @a function for
   * each item.
   * @see std::for_each
   */
  template<typename F>
  F for_each(F function) const
    {
      const FunctorAdapter<F> adapter(m_env, function);
      std::for_each(m_contents.begin(), m_contents.end(), adapter);
      return function;
    }

private:
  template<typename F>
  struct FunctorAdapter
  {
    explicit FunctorAdapter(const Env& env, F& function)
      : m_env(env),
        m_function(function)
      {}

    void operator()(const jobject& obj) const
      {
        const T item(m_env, NativeT(obj));
        m_function(item);
      }

    const Env& m_env;
    F& m_function;
  };
};

/**
 * Non-template base for a mutable type-safe Java list.
 *
 * @since New in 1.9.
 */
class BaseMutableList : public Object
{
public:
  /**
   * Clears the contents of the list.
   */
  void clear()
    {
      m_env.CallVoidMethod(m_jthis, impl().m_mid_clear);
    }

  /**
   * Returns the number of elements in the list.
   */
  jint length() const
    {
      return m_env.CallIntMethod(m_jthis, impl().m_mid_size);
    }

  /**
   * Checks if the list is empty.
   */
  bool is_empty() const
    {
      return (length() == 0);
    }

protected:
  /**
   * Constructs the list wrapper, deriving the class from @a jlist.
   */
  explicit BaseMutableList(Env env, jobject jlist)
    : Object(env, ClassCache::get_array_list(env), jlist)
    {}

  /**
   * Constructs and wraps an empty list of type @c java.util.ArrayList
   * with initial allocation size @a length.
   */
  explicit BaseMutableList(Env env, jint length)
    : Object(env, ClassCache::get_array_list(env))
    {
      set_this(env.NewObject(get_class(), impl().m_mid_ctor, length));
    }


  /**
   * Appends @a obj to the end of the list.
   */
  void add(jobject obj)
    {
      m_env.CallBooleanMethod(m_jthis, impl().m_mid_add, obj);
    }

  /**
   * Returns the object reference at @a index.
   * @note Throws a Java exception if the index value is not valid.
   */
  jobject operator[](jint index) const
    {
      return m_env.CallObjectMethod(m_jthis, impl().m_mid_get, index);
    }

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

    const MethodID m_mid_ctor;
    const MethodID m_mid_add;
    const MethodID m_mid_clear;
    const MethodID m_mid_get;
    const MethodID m_mid_size;
  };

  friend class ClassCacheImpl;
  static const char* const m_class_name;

  const ClassImpl& impl() const
    {
      return *dynamic_cast<const ClassImpl*>(m_impl);
    }
};

/**
 * Template wrapper for a mutable type-safe Java list.
 *
 * @since New in 1.9.
 */
template <typename T, typename NativeT=jobject>
class MutableList : public BaseMutableList
{
public:
  /**
   * Constructs the list wrapper, deriving the class from @a jlist.
   */
  explicit MutableList(Env env, jobject jlist)
    : BaseMutableList(env, jlist)
    {}

  /**
   * Constructs and wraps an empty list of type @c java.util.ArrayList
   * with initial allocation size @a length.
   */
  explicit MutableList(Env env, jint length_ = 0)
    : BaseMutableList(env, length_)
    {}

  /**
   * Appends @a obj to the end of the list.
   */
  void add(const T& obj)
    {
      BaseMutableList::add(obj.get());
    }

  /**
   * Returns a wrapper object for the object reference at @a index.
   * @note Throws a Java exception if the index value is not valid.
   */
  T operator[](jint index) const
    {
      return T(m_env, NativeT(BaseMutableList::operator[](index)));
    }
};

} // namespace Java

#endif // SVN_JAVAHL_JNIWRAPPER_LIST_HPP
