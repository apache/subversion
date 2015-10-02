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

#ifndef SVN_JAVAHL_JNIWRAPPER_OBJECT_HPP
#define SVN_JAVAHL_JNIWRAPPER_OBJECT_HPP

#include "jni_env.hpp"
#include "jni_globalref.hpp"

namespace Java {

/**
 * An abstract wrapper for a @c java.lang.Object instance.
 *
 * This is the base class for all concrete object wrapper classes. It
 * is self-contained in the sense that it heeps its own JVM
 * environment, class and object reference; Java object methods are
 * expected to be exposed as methods of derived classes.
 *
 * The associated JNI class reference is stored for the lifetime of
 * the JVM in the global class cache.
 *
 * @since New in 1.9.
 */
class Object
{
public:
  /**
   * Returns the wrapped JNI object reference.
   */
  jobject get() const
    {
      return m_jthis;
    }

  /**
   * Returns the wrapped JNI class reference.
   */
  jclass get_class() const
    {
      return m_impl->get_class();
    }

  /**
   * Returns the wrapped enviromnment reference.
   */
  Env get_env() const
    {
      return m_env;
    }

  /**
   * This object's implementation details.
   */
  class ClassImpl
  {
  public:
    jclass get_class() const
      {
        return m_class.get();
      }

    virtual ~ClassImpl();

  protected:
    explicit ClassImpl(Env env, jclass cls)
      : m_class(env, cls)
      {}

  private:
    friend class ClassCacheImpl;

    GlobalClass m_class;  ///< Class reference for this object wrapper

    // Non-copyable
    ClassImpl(const ClassImpl&);
    ClassImpl& operator=(const ClassImpl&);
  };

protected:
  /**
   * constructs an object wrapper given the class @a impl and an
   * object reference @a jthis.
   */
  Object(Env env, const ClassImpl* impl, jobject jthis = NULL)
    : m_env(env),
      m_impl(impl),
      m_jthis(jthis)
    {}

  const Env m_env;               ///< JVM environment wrapper
  const ClassImpl* const m_impl; ///< Class implementation details
  const jobject m_jthis;         ///< @c this object reference

  /**
   * Certain subclasses need a fully constructed base Object before
   * they can create the wrapped JNI object. They can use this
   * function to oveerride the constness of @c m_jthis, but only if
   * they're changing a @c NULL @c m_jthis to a concrete value.
   */
  void set_this(jobject jthis)
    {
      if (!m_jthis && jthis)
        *const_cast<jobject*>(&m_jthis) = jthis;
    }

private:
  friend class ClassCacheImpl;
  static const char* const m_class_name;
};

// Forward declaration
class ClassCacheImpl;

/**
 * A singleton cache for global class references.
 *
 * The instance is created when the native library is loaded by the
 * JVM, and destroyed when it is unloaded. It creates global
 * references for a number of classes and calls said classes'
 * single-threded static initializers, which usually find and store
 * method and field IDs (which are usually only valid until the
 * associated class is garbage-collected).
 *
 * Be aware that as long as the global references exist, these classes
 * cannot be garbage-collected. The number of classes stored in this
 * cache should therefore be kept to a reasonable minimum.
 *
 * @since New in 1.9.
 */
class ClassCache
{
  // Cannot create instances of this type.
  ClassCache();
  ~ClassCache();

  static ClassCacheImpl* m_impl;

public:
  /* This static initializer must only be called by JNI_OnLoad */
  static void create();

  /* This static finalizer must only be called by JNI_OnUnload */
  static void destroy();

#define JNIWRAPPER_DECLARE_CACHED_CLASS_ACCESSOR(M)     \
  static const Object::ClassImpl* get_##M(Env env);

  JNIWRAPPER_DECLARE_CACHED_CLASS_ACCESSOR(object);
  JNIWRAPPER_DECLARE_CACHED_CLASS_ACCESSOR(classtype);
  JNIWRAPPER_DECLARE_CACHED_CLASS_ACCESSOR(throwable);
  JNIWRAPPER_DECLARE_CACHED_CLASS_ACCESSOR(string);

  JNIWRAPPER_DECLARE_CACHED_CLASS_ACCESSOR(exc_index_out_of_bounds);
  JNIWRAPPER_DECLARE_CACHED_CLASS_ACCESSOR(exc_no_such_element);

  JNIWRAPPER_DECLARE_CACHED_CLASS_ACCESSOR(list);
  JNIWRAPPER_DECLARE_CACHED_CLASS_ACCESSOR(array_list);

  JNIWRAPPER_DECLARE_CACHED_CLASS_ACCESSOR(map);
  JNIWRAPPER_DECLARE_CACHED_CLASS_ACCESSOR(set);
  JNIWRAPPER_DECLARE_CACHED_CLASS_ACCESSOR(iterator);
  JNIWRAPPER_DECLARE_CACHED_CLASS_ACCESSOR(map_entry);
  JNIWRAPPER_DECLARE_CACHED_CLASS_ACCESSOR(hash_map);

  JNIWRAPPER_DECLARE_CACHED_CLASS_ACCESSOR(input_stream);
  JNIWRAPPER_DECLARE_CACHED_CLASS_ACCESSOR(output_stream);

  JNIWRAPPER_DECLARE_CACHED_CLASS_ACCESSOR(byte_buffer);

  JNIWRAPPER_DECLARE_CACHED_CLASS_ACCESSOR(subversion_exception);

  JNIWRAPPER_DECLARE_CACHED_CLASS_ACCESSOR(authn_cb);
  JNIWRAPPER_DECLARE_CACHED_CLASS_ACCESSOR(authn_result);
  JNIWRAPPER_DECLARE_CACHED_CLASS_ACCESSOR(authn_ssl_server_cert_failures);
  JNIWRAPPER_DECLARE_CACHED_CLASS_ACCESSOR(authn_ssl_server_cert_info);
  JNIWRAPPER_DECLARE_CACHED_CLASS_ACCESSOR(user_passwd_cb);

  JNIWRAPPER_DECLARE_CACHED_CLASS_ACCESSOR(credential);
  JNIWRAPPER_DECLARE_CACHED_CLASS_ACCESSOR(credential_kind);

  JNIWRAPPER_DECLARE_CACHED_CLASS_ACCESSOR(external_item);

  JNIWRAPPER_DECLARE_CACHED_CLASS_ACCESSOR(editor_provide_base_cb);
  JNIWRAPPER_DECLARE_CACHED_CLASS_ACCESSOR(editor_provide_base_cb_ret);
  JNIWRAPPER_DECLARE_CACHED_CLASS_ACCESSOR(editor_provide_props_cb);
  JNIWRAPPER_DECLARE_CACHED_CLASS_ACCESSOR(editor_provide_props_cb_ret);
  JNIWRAPPER_DECLARE_CACHED_CLASS_ACCESSOR(editor_get_kind_cb);
#undef JNIWRAPPER_DECLARE_CACHED_CLASS
};


/**
 * Object wrapper for @c java.lang.Class.
 *
 * The associated JNI class reference is stored for the lifetime of
 * the JVM in the global class cache.
 *
 * @since New in 1.9.
 */
class Class
{
public:
  /**
   * Constructs class instance wrapper for @a obj.
   */
  explicit Class(Env env, jobject obj);

  /**
   * Constructs class instance wrapper for @a obj.
   */
  explicit Class(const Object& obj);

  /**
   * Wrapper for the Java @c getName() method.
   */
  jstring get_name() const;

  /**
   * Returns the wrapped class instance.
   */
  jobject get() const
    {
      return m_jthis;
    }

  /**
   * Returns the wrapped enviromnment reference.
   */
  Env get_env() const
    {
      return m_env;
    }

private:
  /**
   * This object's implementation details.
   */
  class ClassImpl : public Object::ClassImpl
  {
    friend class ClassCacheImpl;

  protected:
    explicit ClassImpl(Env env, jclass cls)
      : Object::ClassImpl(env, cls)
      {}

  public:
    virtual ~ClassImpl();
  };

  const Env m_env;        ///< JVM environment wrapper
  const jobject m_jthis;  ///< Class instance

  friend class ClassCacheImpl;
  static const char* const m_class_name;
  static void static_init(Env env, jclass class_type);

  static MethodID m_mid_get_class;
  static MethodID m_mid_get_name;
};

} // namespace Java

#endif // SVN_JAVAHL_JNIWRAPPER_OBJECT_HPP
