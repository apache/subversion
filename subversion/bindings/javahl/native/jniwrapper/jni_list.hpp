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

#include "jni_env.hpp"
#include "jni_object.hpp"
#include "jni_iterator.hpp"

namespace Java {

/**
 * Non-template base for an immutable type-safe Java list.
 *
 * @since New in 1.9.
 */
class BaseImmutableList : public Object
{
public:
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
   * Constructs the list wrapper
   */
  explicit BaseImmutableList(Env env, jobject jlist)
    : Object(env, ClassCache::get_list(env), jlist)
    {}

  /**
   * Constructor used by BaseList
   */
  explicit BaseImmutableList(Env env, const Object::ClassImpl* pimpl)
    : Object(env, pimpl)
    {}

  /**
   * Clears the contents of the list.
   */
  void clear()
    {
      m_env.CallVoidMethod(m_jthis, impl().m_mid_clear);
    }

  /**
   * Appends @a obj to the end of the list.
   */
  void add(jobject obj)
    {
      m_env.CallVoidMethod(m_jthis, impl().m_mid_add, obj);
    }

  /**
   * Returns the object reference at @a index.
   * @throw std::out_of_range if the index value is not valid.
   */
  jobject operator[](jint index) const;

  /**
   * Iterator used by subclasses.
   */
  class Iterator : public BaseIterator
  {
    friend class BaseImmutableList;
    explicit Iterator(Env env, jobject jiterator)
      : BaseIterator(env, jiterator)
      {}
  };

  /**
   * Returns an iterator instance.
   */
  Iterator get_iterator() const;

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
    const MethodID m_mid_add;
    const MethodID m_mid_clear;
    const MethodID m_mid_iter;
  };

private:
  friend class Iterator;
  friend class ClassCacheImpl;
  static const char* const m_class_name;

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
class ImmutableList : public BaseImmutableList
{
public:
  /**
   * Constructs the list wrapper.
   */
  explicit ImmutableList(Env env, jobject jlist)
    : BaseImmutableList(env, jlist)
    {}

  /**
   * Returns a wrapper object for the object reference at @a index.
   * @throw std::out_of_range if the index value is not valid.
   */
  T operator[](jint index) const
    {
      return T(m_env, NativeT(BaseImmutableList::operator[](index)));
    }

  /**
   * Iterates over the items in the list, calling @a function for
   * each item.
   * @see std::for_each
   */
  template<typename F>
  F for_each(F function) const
    {
      Iterator iter(get_iterator());
      while (iter.has_next())
        function(T(m_env, NativeT(iter.next())));
      return function;
    }
};

/**
 * Non-template base for a mutable type-safe Java list.
 *
 * @since New in 1.9.
 */
class BaseList : public BaseImmutableList
{
public:
  /**
   * Clears the contents of the list.
   */
  void clear()
    {
      BaseImmutableList::clear();
    }

protected:
  /**
   * Constructs the list wrapper, treating @a jlist as a @c java.util.List.
   */
  explicit BaseList(Env env, jobject jlist)
    : BaseImmutableList(env, jlist)
    {}

  /**
   * Constructs and wraps an empty list of type @c java.util.ArrayList
   * with initial allocation size @a length.
   */
  explicit BaseList(Env env, jint length)
    : BaseImmutableList(env, ClassCache::get_array_list(env))
    {
      set_this(env.NewObject(get_class(), impl().m_mid_ctor, length));
    }

  /**
   * Appends @a obj to the end of the list.
   */
  void add(jobject obj)
    {
      BaseImmutableList::add(obj);
    }

private:
  /**
   * This object's implementation details.
   */
  class ClassImpl : public BaseImmutableList::ClassImpl
  {
    friend class ClassCacheImpl;

  protected:
    explicit ClassImpl(Env env, jclass cls);

  public:
    virtual ~ClassImpl();

    const MethodID m_mid_ctor;
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
class List : public BaseList
{
public:
  /**
   * Constructs the list wrapper, deriving the class from @a jlist.
   */
  explicit List(Env env, jobject jlist)
    : BaseList(env, jlist)
    {}

  /**
   * Constructs and wraps an empty list of type @c java.util.ArrayList
   * with initial allocation size @a length.
   */
  explicit List(Env env, jint length_ = 0)
    : BaseList(env, length_)
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
   * Appends @a obj to the end of the list.
   */
  void add(const T& obj)
    {
      BaseList::add(obj.get());
    }

  /**
   * Iterates over the items in the list, calling @a function for
   * each item.
   * @see std::for_each
   */
  template<typename F>
  F for_each(F function) const
    {
      Iterator iter(get_iterator());
      while (iter.has_next())
        function(T(m_env, NativeT(iter.next())));
      return function;
    }
};

} // namespace Java

#endif // SVN_JAVAHL_JNIWRAPPER_LIST_HPP
