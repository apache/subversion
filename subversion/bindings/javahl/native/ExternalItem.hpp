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

#ifndef SVN_JAVAHL_EXTERNAL_ITEM_HPP
#define SVN_JAVAHL_EXTERNAL_ITEM_HPP

#include <string>

#include "svn_opt.h"
#include "svn_wc.h"

#include "Pool.h"

#include "jniwrapper/jni_object.hpp"
#include "jniwrapper/jni_string.hpp"

namespace JavaHL {

/**
 * Object wrapper for @c org.apache.subversion.javahl.types.ExternalItem.
 *
 * @since New in 1.9.
 */
class ExternalItem : public ::Java::Object
{
public:
  /**
   * Constructs a wrapper around @a jthis.
   * The constructor does not verify the class of the wrapped object.
   */
  explicit ExternalItem(::Java::Env env, jobject jthis);

  /**
   * Constructs a new @c ExternalItem object and wrapper.
   */
  explicit ExternalItem(::Java::Env env,
                        const char* target_dir,
                        const char* url,
                        const svn_opt_revision_t* revision,
                        const svn_opt_revision_t* peg_revision);

  /**
   * Returns the value of the wrapped object's @c targetDir member.
   */
  std::string target_dir() const
    {
      const ::Java::String::Contents contents(m_target_dir);
      return std::string(contents.c_str());
    }

  /**
   * Returns the value of the wrapped object's @c url member.
   */
  std::string url() const
    {
      const ::Java::String::Contents contents(m_url);
      return std::string(contents.c_str());
    }

  /**
   * Returns the value of the wrapped object's @c revision member.
   */
  const svn_opt_revision_t* revision() const
    {
      return &m_revision;
    }

  /**
   * Returns the value of the wrapped object's @c pegRevision member.
   */
  const svn_opt_revision_t* peg_revision() const
    {
      return &m_peg_revision;
    }

  /**
   * Returns an @c svn_wc_external_item2_t allocated from @a pool and
   * filled in with this object's values.
   */
  svn_wc_external_item2_t* get_external_item(SVN::Pool& pool) const;

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

    const ::Java::MethodID m_mid_ctor;
    const ::Java::FieldID m_fid_target_dir;
    const ::Java::FieldID m_fid_url;
    const ::Java::FieldID m_fid_revision;
    const ::Java::FieldID m_fid_peg_revision;
  };

  const ClassImpl& impl() const
    {
      return *dynamic_cast<const ClassImpl*>(m_impl);
    }

  friend class ::Java::ClassCacheImpl;
  static const char* const m_class_name;

  ::Java::String m_target_dir;
  ::Java::String m_url;
  svn_opt_revision_t m_revision;
  svn_opt_revision_t m_peg_revision;
};

} // namespace JavaHL

#endif // SVN_JAVAHL_EXTERNAL_ITEM_HPP
