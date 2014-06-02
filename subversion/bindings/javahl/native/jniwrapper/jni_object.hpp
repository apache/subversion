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
      return m_class;
    }

  /**
   * Returns the wrapped enviromnment reference.
   */
  Env get_env() const
    {
      return m_env;
    }

protected:
  /**
   * Constructs an object wrapper given the @a class_name and an
   * object reference @a jthis.
   */
  Object(Env env, const char* class_name, jobject jthis)
    : m_env(env),
      m_class(env.FindClass(class_name)),
      m_jthis(jthis)
    {}

  /**
   * Constructs an object wrapper given a class reference @a cls and
   * an object reference @a jthis.
   */
  Object(Env env, jclass cls, jobject jthis)
    : m_env(env),
      m_class(cls),
      m_jthis(jthis)
    {}

  /**
   * Constructs an object wrapper given an object reference @a jthis
   * and derives the class reference from it.
   */
  Object(Env env, jobject jthis)
    : m_env(env),
      m_class(env.GetObjectClass(jthis)),
      m_jthis(jthis)
    {}

  const Env m_env;        ///< JVM environment wrapper
  const jclass m_class;   ///< Class reference for this object wrapper
  const jobject m_jthis;  ///< @c this object reference

private:
  friend class ClassCache;
  static const char* const m_class_name;
};

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
 * @todo ### Change implementation to load classes at first use.
 */
class ClassCache
{
  explicit ClassCache(Env env);
  ~ClassCache();

  static const ClassCache* m_instance;

#define SVN_JAVAHL_JNIWRAPPER_CACHED_CLASS(N)   \
 private:                                       \
   GlobalClass m_##N;                           \
 public:                                        \
  static jclass get_##N()                       \
  {                                             \
    return m_instance->m_##N.get();             \
  }                                             \

  SVN_JAVAHL_JNIWRAPPER_CACHED_CLASS(object);
  SVN_JAVAHL_JNIWRAPPER_CACHED_CLASS(classtype);
  SVN_JAVAHL_JNIWRAPPER_CACHED_CLASS(throwable);
  SVN_JAVAHL_JNIWRAPPER_CACHED_CLASS(string);

  SVN_JAVAHL_JNIWRAPPER_CACHED_CLASS(list);
  SVN_JAVAHL_JNIWRAPPER_CACHED_CLASS(array_list);

  SVN_JAVAHL_JNIWRAPPER_CACHED_CLASS(map);
  SVN_JAVAHL_JNIWRAPPER_CACHED_CLASS(set);
  SVN_JAVAHL_JNIWRAPPER_CACHED_CLASS(iterator);
  SVN_JAVAHL_JNIWRAPPER_CACHED_CLASS(map_entry);
  SVN_JAVAHL_JNIWRAPPER_CACHED_CLASS(hash_map);

  SVN_JAVAHL_JNIWRAPPER_CACHED_CLASS(input_stream);
  SVN_JAVAHL_JNIWRAPPER_CACHED_CLASS(output_stream);

  SVN_JAVAHL_JNIWRAPPER_CACHED_CLASS(byte_buffer);

  SVN_JAVAHL_JNIWRAPPER_CACHED_CLASS(subversion_exception);

  SVN_JAVAHL_JNIWRAPPER_CACHED_CLASS(authn_cb);
  SVN_JAVAHL_JNIWRAPPER_CACHED_CLASS(authn_result);
  SVN_JAVAHL_JNIWRAPPER_CACHED_CLASS(authn_ssl_server_cert_failures);
  SVN_JAVAHL_JNIWRAPPER_CACHED_CLASS(authn_ssl_server_cert_info);

  SVN_JAVAHL_JNIWRAPPER_CACHED_CLASS(external_item);

  SVN_JAVAHL_JNIWRAPPER_CACHED_CLASS(editor_provide_base_cb);
  SVN_JAVAHL_JNIWRAPPER_CACHED_CLASS(editor_provide_base_cb_return_value);
  SVN_JAVAHL_JNIWRAPPER_CACHED_CLASS(editor_provide_props_cb);
  SVN_JAVAHL_JNIWRAPPER_CACHED_CLASS(editor_provide_props_cb_return_value);
  SVN_JAVAHL_JNIWRAPPER_CACHED_CLASS(editor_get_kind_cb);
#undef SVN_JAVAHL_JNIWRAPPER_CACHED_CLASS

public:
  /* This static initializer must only be called by JNI_OnLoad */
  static void create();

  /* This static finalizer must only be called by JNI_OnUnload */
  static void destroy();
};


/**
 * Object wrapper for @c java.lang.Class.
 *
 * The associated JNI class reference is stored for the lifetime of
 * the JVM in the global class cache.
 *
 * @since New in 1.9.
 */
class Class : public Object
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

private:
  friend class ClassCache;
  static void static_init(Env env);
  static const char* const m_class_name;
  static MethodID m_mid_get_class;
  static MethodID m_mid_get_name;
};

} // namespace Java

#endif // SVN_JAVAHL_JNIWRAPPER_OBJECT_HPP
