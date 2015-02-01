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

#ifndef SVN_JAVAHL_JNIWRAPPER_STRING_MAP_HPP
#define SVN_JAVAHL_JNIWRAPPER_STRING_MAP_HPP

#include <map>
#include <string>
#include <algorithm>

#include "jni_env.hpp"
#include "jni_object.hpp"

namespace Java {

/**
 * Non-template base for an immutable type-safe Java map with String keys.
 *
 * Converts the map to a @c std::map containing @c jobject references.
 *
 * @since New in 1.9.
 */
class BaseMap : public Object
{
  typedef std::map<std::string, jobject> somap;

public:
  /**
   * Returns the number of elements in the map.
   */
  jint length() const
    {
      return jint(m_contents.size());
    }

protected:
  /**
   * Constructs the map wrapper, converting the contents to an
   * @c std::map.
   */
  explicit BaseMap(Env env, jobject jmap)
    : Object(env, ClassCache::get_map(env), jmap),
      m_contents(convert_to_map(env, m_jthis))
    {}

  /**
   * Returns the object reference identified by @a index.
   * @throw std::out_of_range if there is no such element.
   */
  jobject operator[](const std::string& index) const;

  const somap m_contents;

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
    const MethodID m_mid_entry_set;
  };

  const ClassImpl& impl() const
    {
      return *dynamic_cast<const ClassImpl*>(m_impl);
    }

  friend class ClassCacheImpl;
  static const char* const m_class_name;
  static somap convert_to_map(Env env, jobject jmap);

  struct Set
  {
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

      const MethodID m_mid_iterator;
    };

    static const char* const m_class_name;
    static const ClassImpl& impl(Env env)
      {
        return *dynamic_cast<const ClassImpl*>(ClassCache::get_set(env));
      }
  };

  struct Iterator
  {
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

    static const char* const m_class_name;
    static const ClassImpl& impl(Env env)
      {
        return *dynamic_cast<const ClassImpl*>(ClassCache::get_iterator(env));
      }
  };

  struct Entry
  {
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

      const MethodID m_mid_get_key;
      const MethodID m_mid_get_value;
    };

    static const char* const m_class_name;
    static const ClassImpl& impl(Env env)
      {
        return *dynamic_cast<const ClassImpl*>(ClassCache::get_map_entry(env));
      }
  };
};

/**
 * Template wrapper for an immutable type-safe Java map.
 *
 * @since New in 1.9.
 */
template <typename T, typename NativeT=jobject>
class Map : public BaseMap
{
public:
  /**
   * Constructs the map wrapper, converting the contents to an
   * @c std::map.
   */
  explicit Map(Env env, jobject jmap)
    : BaseMap(env, jmap)
    {}

  /**
   * Returns a wrapper object for the object reference identified by @a index.
   * @throw std::out_of_range if there is no such element.
   */
  T operator[](const std::string& index) const
    {
      return T(m_env, NativeT(BaseMap::operator[](index)));
    }

  /**
   * Iterates over the items in the map, calling @a function for
   * each item.
   * @see std::for_each
   * @note Unlike std::for_each, which invokes the functor with a
   *     single @c value_type argument, this iterator adapts it to cal
   *     @a function with separate @c const references to the key and
   *     value.
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

    void operator()(const std::pair<std::string, jobject>& item) const
      {
        const std::string& key(item.first);
        const T value(m_env, NativeT(item.second));
        m_function(key, value);
      }

    const Env& m_env;
    F& m_function;
  };
};

/**
 * Non-template base for a mutable type-safe Java map with String keys.
 *
 * @since New in 1.9.
 */
class BaseMutableMap : public Object
{
public:
  /**
   * Clears the contents of the map.
   */
  void clear()
    {
      m_env.CallVoidMethod(m_jthis, impl().m_mid_clear);
    }

  /**
   * Returns the number of elements in the map.
   */
  jint length() const
    {
      return m_env.CallIntMethod(m_jthis, impl().m_mid_size);
    }

  /**
   * Checks if the map is empty.
   */
  bool is_empty() const
    {
      return (length() == 0);
    }

protected:
  /**
   * Constructs the map wrapper, deriving the class from @a jmap.
   */
  explicit BaseMutableMap(Env env, jobject jmap)
    : Object(env, ClassCache::get_hash_map(env), jmap)
    {}

  /**
   * Constructs and wraps an empty map of type @c java.util.HashMap
   * with initial allocation size @a length.
   */
  explicit BaseMutableMap(Env env, jint length)
    : Object(env, ClassCache::get_hash_map(env))
    {
      set_this(env.NewObject(get_class(), impl().m_mid_ctor, length));
    }


  /**
   * Inserts @a obj identified by @a key into the map.
   */
  void put(const std::string& key, jobject obj)
    {
      m_env.CallObjectMethod(m_jthis, impl().m_mid_put,
                             String(m_env, key).get(), obj);
    }

  /**
   * Returns the object reference identified by @a index.
   * @throw std::out_of_range if there is no such element.
   */
  jobject operator[](const std::string& index) const;

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
    const MethodID m_mid_put;
    const MethodID m_mid_clear;
    const MethodID m_mid_has_key;
    const MethodID m_mid_get;
    const MethodID m_mid_size;
  };

  const ClassImpl& impl() const
    {
      return *dynamic_cast<const ClassImpl*>(m_impl);
    }

  friend class ClassCacheImpl;
  static const char* const m_class_name;
};

/**
 * Template wrapper for a mutable type-safe Java map.
 *
 * @since New in 1.9.
 */
template <typename T, typename NativeT=jobject>
class MutableMap : public BaseMutableMap
{
public:
  /**
   * Constructs the map wrapper, deriving the class from @a jmap.
   */
  explicit MutableMap(Env env, jobject jmap)
    : BaseMutableMap(env, jmap)
    {}

  /**
   * Constructs and wraps an empty map of type @c java.util.HashMap
   * with initial allocation size @a length.
   */
  explicit MutableMap(Env env, jint length = 0)
    : BaseMutableMap(env, length)
    {}

  /**
   * Inserts @a obj identified by @a key into the map.
   */
  void put(const std::string& key, const T& obj)
    {
      BaseMutableMap::put(key, obj.get());
    }

  /**
   * Returns a wrapper object for the object reference identified by @a index.
   * @throw std::out_of_range if there is no such element.
   */
  T operator[](const std::string& index) const
    {
      return T(m_env, NativeT(BaseMutableMap::operator[](index)));
    }
};

} // namespace Java

#endif // SVN_JAVAHL_JNIWRAPPER_STRING_MAP_HPP
