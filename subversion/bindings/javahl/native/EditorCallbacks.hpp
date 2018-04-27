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

#ifndef SVN_JAVAHL_EDITOR_CALLBACKS_HPP
#define SVN_JAVAHL_EDITOR_CALLBACKS_HPP

#include "svn_io.h"

#include "Pool.h"

#include "jniwrapper/jni_object.hpp"

namespace JavaHL {

/**
 * Object wrapper for the base contents callback interface in
 * @c org.apache.subversion.javahl.ISVNEditor.
 *
 * @since New in 1.9.
 */
class ProvideBaseCallback : public ::Java::Object
{
public:
  /**
   * Constructs a wrapper around @a jthis.
   * The constructor does not verify the class of the wrapped object.
   */
  explicit ProvideBaseCallback(::Java::Env env, jobject jthis)
    : ::Java::Object(env,
                     ::Java::ClassCache::get_editor_provide_base_cb(env),
                     jthis)
    {}

  /**
   * Invokes the callback.
   */
  jobject operator()(jstring relpath) const
    {
      return m_env.CallObjectMethod(m_jthis, impl().m_mid_get_contents,
                                    relpath);
    }

  class ReturnValue : public ::Java::Object
  {
  public:
    /**
     * Constructs a wrapper around @a jthis.
     * The constructor does not verify the class of the wrapped object.
     */
    explicit ReturnValue(::Java::Env env, jobject jthis)
      : Java::Object(env,
                     ::Java::ClassCache::get_editor_provide_base_cb_ret(env),
                     jthis)
      {}

    /**
     * Returns an @c svn_stream_t for the contents stream in the
     * wrapped return value, allocated from @a pool. The wrapped Java
     * stream will live as long as @a pool.
     */
    svn_stream_t* get_global_stream(apr_pool_t* pool) const;

    /**
     * Returns an @c svn_stream_t for the contents stream in the
     * wrapped return value, allocated from @a pool. The wrapped Java
     * stream will live as long as @a pool.
     */
    svn_stream_t* get_global_stream(const SVN::Pool& pool) const
      {
        return get_global_stream(pool.getPool());
      }

    /**
     * Returns the revision in the wrapped return value.
     */
    jlong get_revision() const
      {
        return m_env.GetLongField(m_jthis, impl().m_fid_revision);
      }

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

      const ::Java::FieldID m_fid_contents;
      const ::Java::FieldID m_fid_revision;
    };

    const ClassImpl& impl() const
      {
        return *dynamic_cast<const ClassImpl*>(m_impl);
      }

    friend class ::Java::ClassCacheImpl;
    static const char* const m_class_name;
  };

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

    const ::Java::MethodID m_mid_get_contents;
  };

  const ClassImpl& impl() const
    {
      return *dynamic_cast<const ClassImpl*>(m_impl);
    }

  friend class ::Java::ClassCacheImpl;
  static const char* const m_class_name;
};


/**
 * Object wrapper for the props callback interface in
 * @c org.apache.subversion.javahl.ISVNEditor.
 *
 * @since New in 1.9.
 */
class ProvidePropsCallback : public ::Java::Object
{
public:
  /**
   * Constructs a wrapper around @a jthis.
   * The constructor does not verify the class of the wrapped object.
   */
  explicit ProvidePropsCallback(::Java::Env env, jobject jthis)
    : ::Java::Object(env,
                     ::Java::ClassCache::get_editor_provide_props_cb(env),
                     jthis)
    {}

  /**
   * Invokes the callback.
   */
  jobject operator()(jstring relpath) const
    {
      return m_env.CallObjectMethod(m_jthis, impl().m_mid_get_props, relpath);
    }

  class ReturnValue : public ::Java::Object
  {
  public:
    /**
     * Constructs a wrapper around @a jthis.
     * The constructor does not verify the class of the wrapped object.
     */
    explicit ReturnValue(::Java::Env env, jobject jthis)
      : Java::Object(env,
                     ::Java::ClassCache::get_editor_provide_props_cb_ret(env),
                     jthis)
      {}

    /**
     * Returns an @c apr_hash_t of the node properties in the wrapped
     * return value, allocated from @a pool.
     */
    apr_hash_t* get_property_hash(apr_pool_t* pool) const;

    /**
     * Returns an @c apr_hash_t of the node properties in the wrapped
     * return value, allocated from @a pool.
     */
    apr_hash_t* get_property_hash(const SVN::Pool& pool) const
      {
        return get_property_hash(pool.getPool());
      }

    /**
     * Returns the revision in the wrapped return value.
     */
    jlong get_revision() const
      {
        return m_env.GetLongField(m_jthis, impl().m_fid_revision);
      }

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

      const ::Java::FieldID m_fid_properties;
      const ::Java::FieldID m_fid_revision;
    };

    const ClassImpl& impl() const
      {
        return *dynamic_cast<const ClassImpl*>(m_impl);
      }

    friend class ::Java::ClassCacheImpl;
    static const char* const m_class_name;
  };

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

    const ::Java::MethodID m_mid_get_props;
  };

  const ClassImpl& impl() const
    {
      return *dynamic_cast<const ClassImpl*>(m_impl);
    }

  friend class ::Java::ClassCacheImpl;
  static const char* const m_class_name;
};


/**
 * Object wrapper for the kind callback interface in
 * @c org.apache.subversion.javahl.ISVNEditor.
 *
 * @since New in 1.9.
 */
class GetNodeKindCallback : public ::Java::Object
{
public:
  /**
   * Constructs a wrapper around @a jthis.
   * The constructor does not verify the class of the wrapped object.
   */
  explicit GetNodeKindCallback(::Java::Env env, jobject jthis)
    : ::Java::Object(env,
                     ::Java::ClassCache::get_editor_get_kind_cb(env),
                     jthis)
    {}

  /**
   * Invokes the callback.
   */
  jobject operator()(jstring relpath, jlong revision) const
    {
      return m_env.CallObjectMethod(m_jthis, impl().m_mid_get_kind,
                                    relpath, revision);
    }

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

    const ::Java::MethodID m_mid_get_kind;
  };

  const ClassImpl& impl() const
    {
      return *dynamic_cast<const ClassImpl*>(m_impl);
    }

  friend class ::Java::ClassCacheImpl;
  static const char* const m_class_name;
};

} // namespace JavaHL

#endif // SVN_JAVAHL_EDITOR_CALLBACKS_HPP
