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

#include <cstring>
#include <string>

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
 */
class ClassCache
{
  JNIEXPORT jint JNICALL friend ::JNI_OnLoad(::JavaVM*, void*);
  JNIEXPORT void JNICALL friend ::JNI_OnUnload(::JavaVM*, void*);
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
  SVN_JAVAHL_JNIWRAPPER_CACHED_CLASS(subversion_exception);
#undef SVN_JAVAHL_JNIWRAPPER_CACHED_CLASS
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
  static jmethodID m_mid_get_class;
  static jmethodID m_mid_get_name;
};


/**
 * Object wrapper for @c java.lang.String.
 *
 * The associated JNI class reference is stored for the lifetime of
 * the JVM in the global class cache.
 *
 * @since New in 1.9.
 */
class String : public Object
{
public:
  /**
   * Constructs a wrapper around an existing string @a str.
   */
  explicit String(Env env, jstring str)
    : Object(env, ClassCache::get_string(), str)
    {}

  /**
   * Constructs a new string and wrapper from @a text.
   */
  explicit String(Env env, const char* text)
    : Object(env, ClassCache::get_string(), env.NewStringUTF(text))
    {}

  /**
   * Returns the wrapped JNI object reference. Overridden from the
   * base class in order to return the correct JNI reference type.
   */
  jstring get() const
    {
      return jstring(Object::get());
    }

  /**
   * Returns the number of Unicode characters in the string.
   */
  jsize length() const
    {
      return m_env.GetStringLength(get());
    }

  /**
   * Returns the length of the modified UTF-8 representation of the
   * string.
   */
  jsize utf8_length() const
    {
      return m_env.GetStringUTFLength(get());
    }

  /**
   * Accessor class for the contents of the string.
   *
   * Objects of this class should be created within the scope where
   * the raw C string is required. They will create either a mutable
   * or an immutable modified UTF-8 representation of the string
   * contents, depending on the constantness of the string
   * wrapper. The data will be released (and changes copied into the
   * JVM, depending on access mode) by the destructor.
   */
  class Contents
  {
  public:
    /**
     * Constructs a mutable string contents accessor.
     */
    explicit Contents(String& str)
      : m_str(str),
        m_text(!str.get() ? NULL
               : str.m_env.GetStringUTFChars(str.get(), NULL)),
        m_new_text(NULL),
        m_mutable(true),
        m_length(m_text ? jsize(::std::strlen(m_text)) : 0)
      {}

    /**
     * Constructs an immutable string contents accessor.
     */
    explicit Contents(const String& str)
      : m_str(str),
        m_text(!str.get() ? NULL
               : str.m_env.GetStringUTFChars(str.get(), NULL)),
        m_new_text(NULL),
        m_mutable(false),
        m_length(m_text ? jsize(::std::strlen(m_text)) : 0)
      {}

    /**
     * Releases the string contents, possibly committing changes to the JVM.
     */
    ~Contents()
      {
        if (m_text)
          m_str.m_env.ReleaseStringUTFChars(m_str.get(), m_new_text);
      }

    /**
     * Returns the C representation of the string contents.
     */
    const char* c_str() const
      {
        if (m_new_text)
          return m_new_text;
        return m_text;
      }

    /**
     * Returns the length of the C representation of the string.
     */
    jsize utf8_length() const
      {
        return m_length;
      }

    /**
     * Sets a new value for the string, to be committed to the JVM
     * when the accessor object is destroyed.
     * @throw std::logic_error if this is a @c null or immutable string.
     */
    void set_value(const char* new_text)
      {
        if (m_text && m_mutable)
          {
            m_new_text = new_text;
            m_length = jsize(::std::strlen(new_text));
          }
        else if (!m_mutable)
          throw std::logic_error(
              _("Cannot change the contents of an immutable String"));
        else
          throw std::logic_error(
              _("Cannot change the contents of a null String"));
      }

  private:
    const String& m_str;
    const char* m_text;
    const char* m_new_text;
    const bool m_mutable;
    jsize m_length;
  };

private:
  friend class Contents;
  friend class ClassCache;
  static void static_init(Env env);
  static const char* const m_class_name;
};

} // namespace Java

#endif // SVN_JAVAHL_JNIWRAPPER_OBJECT_HPP
