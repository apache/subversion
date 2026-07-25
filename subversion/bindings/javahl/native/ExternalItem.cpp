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

#include "jniwrapper/jni_stack.hpp"

#include "ExternalItem.hpp"
#include "Revision.h"

namespace JavaHL {

const char* const ExternalItem::m_class_name =
  JAVAHL_CLASS("/types/ExternalItem");

ExternalItem::ClassImpl::ClassImpl(::Java::Env env, jclass cls)
  : ::Java::Object::ClassImpl(env, cls),
    m_mid_ctor(
        env.GetMethodID(cls, "<init>",
                        "(ZLjava/lang/String;Ljava/lang/String;"
                        JAVAHL_ARG("/types/Revision;")
                        JAVAHL_ARG("/types/Revision;")
                        ")V")),
    m_fid_target_dir(env.GetFieldID(cls, "targetDir", "Ljava/lang/String;")),
    m_fid_url(env.GetFieldID(cls, "url", "Ljava/lang/String;")),
    m_fid_revision(env.GetFieldID(cls, "revision",
                                  JAVAHL_ARG("/types/Revision;"))),
    m_fid_peg_revision(env.GetFieldID(cls, "pegRevision",
                                      JAVAHL_ARG("/types/Revision;")))
{}

ExternalItem::ClassImpl::~ClassImpl() {}

namespace {
inline jstring
get_string_field(::Java::Env env, jobject jthis,
                 const ::Java::FieldID& fid)
{
  return jstring(env.GetObjectField(jthis, fid));
}

inline svn_opt_revision_t
get_revision_field(::Java::Env env, jobject jthis,
                   const ::Java::FieldID& fid)
{
  const jobject rev =  env.GetObjectField(jthis, fid);
  return *Revision(rev).revision();
}

inline jobject
make_external_item(::Java::Env env,
                   jclass cls, const ::Java::MethodID& mid_ctor,
                   const char* target_dir,
                   const char* url,
                   const svn_opt_revision_t* revision,
                   const svn_opt_revision_t* peg_revision)
{
  return env.NewObject(cls, mid_ctor,
                       JNI_FALSE,
                       env.NewStringUTF(target_dir),
                       env.NewStringUTF(url),
                       Revision::makeJRevision(*revision),
                       Revision::makeJRevision(*peg_revision));
}
} // anonymous namespace

ExternalItem::ExternalItem(::Java::Env env, jobject jthis)
  : Object(env, ::Java::ClassCache::get_external_item(env), jthis),
    m_target_dir(env, get_string_field(env, jthis, impl().m_fid_target_dir)),
    m_url(env, get_string_field(env, jthis, impl().m_fid_url)),
    m_revision(get_revision_field(env, jthis, impl().m_fid_revision)),
    m_peg_revision(get_revision_field(env, jthis, impl().m_fid_peg_revision))
{}

ExternalItem::ExternalItem(::Java::Env env,
                           const char* target_dir,
                           const char* url,
                           const svn_opt_revision_t* revision,
                           const svn_opt_revision_t* peg_revision)
  : Object(env, ::Java::ClassCache::get_external_item(env)),
    m_target_dir(env, target_dir),
    m_url(env, url),
    m_revision(*revision),
    m_peg_revision(*peg_revision)
{
  set_this(make_external_item(env, get_class(), impl().m_mid_ctor,
                              target_dir, url, revision, peg_revision));
}

svn_wc_external_item2_t*
ExternalItem::get_external_item(SVN::Pool& svnpool) const
{
  svn_wc_external_item2_t* item;
  apr_pool_t* const pool = svnpool.getPool();
  SVN_JAVAHL_CHECK(m_env, svn_wc_external_item2_create(&item, pool));

  item->target_dir = apr_pstrdup(
      pool, Java::String::Contents(m_target_dir).c_str());
  item->url = apr_pstrdup(
      pool, Java::String::Contents(m_url).c_str());
  item->revision = m_revision;
  item->peg_revision = m_peg_revision;
  return item;
}

} // namespace JavaHL


// FIXME: Should be in another source file, but Revision.cpp is
// old-style, so we'll put this implementation here for now.
namespace {
inline jobject get_static_revision(::Java::Env env, jclass cls,
                                   const char* field_name)
{
  return env.GetStaticObjectField(
      cls, env.GetStaticFieldID(cls, field_name,
                                JAVAHL_ARG("/types/Revision;")));
}
} // anonymous namespace

jobject Revision::makeJRevision(const svn_opt_revision_t& rev)
{
  if (rev.kind == svn_opt_revision_number)
      return Revision::makeJRevision(rev.value.number);

  const ::Java::Env env;

  if (rev.kind == svn_opt_revision_date)
    {
      const jclass cls = env.FindClass(
          JAVAHL_CLASS("/types/Revision$DateSpec"));
      return env.NewObject(cls, env.GetMethodID(cls, "<init>", "(J)V"),
                           jlong(rev.value.date / 1000));
    }

  const jclass cls = env.FindClass(JAVAHL_CLASS("/types/Revision"));
  switch (rev.kind)
    {
    case svn_opt_revision_committed:
      return get_static_revision(env, cls, "COMMITTED");

    case svn_opt_revision_previous:
      return get_static_revision(env, cls, "PREVIOUS");

    case svn_opt_revision_base:
      return get_static_revision(env, cls, "BASE");

    case svn_opt_revision_working:
      return get_static_revision(env, cls, "WORKING");

    case svn_opt_revision_head:
      return get_static_revision(env, cls, "HEAD");

    case svn_opt_revision_unspecified:
    default:
      return get_static_revision(env, cls, "UNSPECIFIED");
    }
}
