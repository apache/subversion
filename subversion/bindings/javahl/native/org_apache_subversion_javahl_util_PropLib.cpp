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
 *
 * @file org_apache_subversion_javahl_util_PropLib.cpp
 * @brief Implementation of the native methods in the Java class PropLib
 */

#include <iostream>
#include <sstream>

#include "../include/org_apache_subversion_javahl_util_PropLib.h"

#include "jniwrapper/jni_stack.hpp"
#include "jniwrapper/jni_array.hpp"
#include "jniwrapper/jni_list.hpp"
#include "jniwrapper/jni_string.hpp"
#include "jniwrapper/jni_io_stream.hpp"
#include "ExternalItem.hpp"
#include "SubversionException.hpp"

#include "EnumMapper.h"
#include "Pool.h"

#include "svn_props.h"
#include "svn_time.h"
#include "svn_wc.h"

#include "private/svn_wc_private.h"
#include "svn_private_config.h"


namespace {
class PropGetter
{
public:
  PropGetter(const char* mime_type, svn_stream_t* contents)
    : m_mime_type(mime_type),
      m_contents(contents)
    {}

  static svn_error_t* callback(const svn_string_t** mime_type,
                               svn_stream_t* stream, void* baton,
                               apr_pool_t* pool)
    {
      PropGetter* self = static_cast<PropGetter*>(baton);
      if (mime_type)
        {
          if (self->m_mime_type)
            *mime_type = svn_string_create(self->m_mime_type, pool);
          else
            *mime_type = svn_string_create_empty(pool);
        }

      if (stream && self->m_contents)
        {
          SVN_ERR(svn_stream_copy3(self->m_contents,
                                   svn_stream_disown(stream, pool),
                                   NULL, NULL, pool));
        }

      return SVN_NO_ERROR;
    }

private:
  const char* m_mime_type;
  svn_stream_t* m_contents;
};

struct FormatRevision
{
  explicit FormatRevision(const svn_opt_revision_t* const& revarg,
                          const SVN::Pool& poolarg)
    : rev(revarg), pool(poolarg)
    {}

  const svn_opt_revision_t* const& rev;
  const SVN::Pool& pool;
};

std::ostream& operator<<(std::ostream& os, const FormatRevision& pr)
{
  switch (pr.rev->kind)
    {
    case svn_opt_revision_number:
      os << pr.rev->value.number;
      break;
    case svn_opt_revision_date:
      os << '{'
         << svn_time_to_cstring(pr.rev->value.date, pr.pool.getPool())
         << '}';
      break;
    default:
      throw std::logic_error(
          _("Invalid revision tag; must be a number or a date"));
    }
  return os;
}

bool operator==(const svn_opt_revision_t& a,
                const svn_opt_revision_t& b)
{
  if (a.kind != b.kind)
    return false;
  if (a.kind == svn_opt_revision_number
      && a.value.number != b.value.number)
    return false;
  if (a.kind == svn_opt_revision_date
      && a.value.date != b.value.date)
    return false;
  return true;
}

inline bool operator!=(const svn_opt_revision_t& a,
                       const svn_opt_revision_t& b)
{
  return !(a == b);
}

class UnparseFunctor
{
public:
  explicit UnparseFunctor(std::ostringstream& buffer, bool old_format,
                          SVN::Pool& iterpool)
    : m_buffer(buffer),
      m_old_format(old_format),
      m_iterpool(iterpool)
    {}

  void operator()(const JavaHL::ExternalItem& item)
    {
      m_iterpool.clear();

      const Java::Env env(item.get_env());
      const Java::LocalFrame frame(env);

      if (!m_old_format)
        {
          if (item.revision()->kind != svn_opt_revision_head
              && *item.revision() != *item.peg_revision())
            {
              m_buffer << "-r"
                       << FormatRevision(item.revision(), m_iterpool)
                       << ' ';
            }
          if (item.peg_revision()->kind == svn_opt_revision_head)
            m_buffer << item.url() << ' ';
          else
            {
              m_buffer << item.url() << '@'
                       << FormatRevision(item.peg_revision(), m_iterpool)
                       << ' ';
            }
          m_buffer << item.target_dir() << '\n';
        }
      else
        {
          // Sanity check: old format does not support peg revisions
          if (item.peg_revision()->kind != svn_opt_revision_head
              && *item.revision() != *item.peg_revision())
            {
              JavaHL::SubversionException(env)
                .raise(_("Clients older than Subversion 1.5"
                         " do not support peg revision syntax"
                         " in the svn:externals property"));
            }

          // Sanity check: old format does not support relative URLs
          const std::string url = item.url();
          if (   (url.size() >= 1 && (url[0] == '.' || url[0] == '/'))
                 || (url.size() >= 2 && (url[0] == '^' && url[1] == '/')))
            {
              JavaHL::SubversionException(env)
                .raise(_("Clients older than Subversion 1.5"
                         " do not support relative URLs"
                         " in the svn:externals property"));
            }

          m_buffer << item.target_dir() << ' ';
          if (item.revision()->kind != svn_opt_revision_head)
            {
              m_buffer << "-r"
                       << FormatRevision(item.revision(), m_iterpool)
                       << ' ';
            }
          m_buffer << url << '\n';
        }
    }

private:
  std::ostringstream& m_buffer;
  const bool m_old_format;
  SVN::Pool& m_iterpool;
};
} // anoymous namespace


JNIEXPORT jbyteArray JNICALL
Java_org_apache_subversion_javahl_util_PropLib_checkNodeProp(
    JNIEnv* jenv, jobject jthis,
    jstring jname, jbyteArray jvalue, jstring jpath, jobject jkind,
    jstring jmime_type, jobject jfile_contents,
    jboolean jskip_some_checks)
{
  SVN_JAVAHL_JNI_TRY(PropLib, checkLocalProp)
    {
      const Java::Env env(jenv);

      const svn_node_kind_t kind = EnumMapper::toNodeKind(jkind);
      SVN_JAVAHL_OLDSTYLE_EXCEPTION_CHECK(env);

      const Java::String name_str(env, jname);
      const Java::ByteArray value(env, jvalue);
      const Java::String path_str(env, jpath);
      const Java::String mime_type_str(env, jmime_type);
      Java::InputStream file_contents(env, jfile_contents);

      // Using a "global" request pool since we don't keep a context
      // with its own pool around for these functions.
      SVN::Pool pool;

      const Java::String::Contents name(name_str);
      const Java::String::Contents path(path_str);
      const Java::String::Contents mime_type(mime_type_str);
      PropGetter getter(mime_type.c_str(), file_contents.get_stream(pool));

      const svn_string_t* canonval;
      SVN_JAVAHL_CHECK(env,
                       svn_wc_canonicalize_svn_prop(
                           &canonval, name.c_str(),
                           Java::ByteArray::Contents(value).get_string(pool),
                           path.c_str(), kind,
                           svn_boolean_t(jskip_some_checks),
                           PropGetter::callback, &getter,
                           pool.getPool()));
      return Java::ByteArray(env, canonval->data, jint(canonval->len)).get();
    }
  SVN_JAVAHL_JNI_CATCH;
  return NULL;
}


JNIEXPORT jobject JNICALL
Java_org_apache_subversion_javahl_util_PropLib_parseExternals(
    JNIEnv* jenv, jobject jthis,
    jbyteArray jdescription, jstring jparent_dir, jboolean jcanonicalize_url)
{
  SVN_JAVAHL_JNI_TRY(PropLib, parseExternals)
    {
      const Java::Env env(jenv);

      const Java::ByteArray description(env, jdescription);
      const Java::String parent_dir(env, jparent_dir);

      // Using a "global" request pool since we don't keep a context
      // with its own pool around for these functions.
      SVN::Pool pool;

      apr_array_header_t* externals;
      {
        // There is no guarantee that the description contents are
        // null-terminated. Copy them to an svn_string_t to make sure
        // that they are.
        svn_string_t* const description_contents =
          Java::ByteArray::Contents(description).get_string(pool);

        SVN_JAVAHL_CHECK(env,
                         svn_wc_parse_externals_description3(
                             &externals,
                             Java::String::Contents(parent_dir).c_str(),
                             description_contents->data,
                             svn_boolean_t(jcanonicalize_url),
                             pool.getPool()));
      }

      Java::List<JavaHL::ExternalItem> items(env, externals->nelts);
      for (jint i = 0; i < externals->nelts; ++i)
        {
          // References to the newly created external items are stored
          // in the list, so make sure the local reference in this
          // frame get cleared on each iteration.
          Java::LocalFrame frame;

          const svn_wc_external_item2_t* const item =
            APR_ARRAY_IDX(externals, i, svn_wc_external_item2_t*);
          items.add(JavaHL::ExternalItem(env,
                                         item->target_dir,
                                         item->url,
                                         &item->revision,
                                         &item->peg_revision));
        }
      return items.get();
    }
  SVN_JAVAHL_JNI_CATCH;
  return NULL;
}


JNIEXPORT jbyteArray JNICALL
Java_org_apache_subversion_javahl_util_PropLib_unparseExternals(
    JNIEnv* jenv, jobject jthis,
    jobject jitems, jstring jparent_dir, jboolean jold_format)
{
  SVN_JAVAHL_JNI_TRY(PropLib, unparseExternals)
    {
      const Java::Env env(jenv);

      const Java::ImmutableList<JavaHL::ExternalItem> items(env, jitems);
      const Java::String parent_dir(env, jparent_dir);

      // Using a "global" iteration pool since we don't keep a context
      // with its own pool around for these functions.
      SVN::Pool iterpool;

      std::ostringstream buffer;
      items.for_each(UnparseFunctor(buffer, jold_format, iterpool));
      const std::string description(buffer.str());

      // Validate the result. Even though we generated the string
      // ourselves, we did not validate the input paths and URLs.
      SVN_JAVAHL_CHECK(env,
                       svn_wc_parse_externals_description3(
                           NULL,
                           Java::String::Contents(parent_dir).c_str(),
                           description.c_str(),
                           false, iterpool.getPool()));
      return Java::ByteArray(env, description).get();
    }
  SVN_JAVAHL_JNI_CATCH;
  return NULL;
}


JNIEXPORT jstring JNICALL
Java_org_apache_subversion_javahl_util_PropLib_resolveExternalsUrl(
    JNIEnv* jenv, jobject jthis,
    jobject jitem, jstring jrepos_root_url, jstring jparent_dir_url)
{
  SVN_JAVAHL_JNI_TRY(PropLib, unparseExternals)
    {
      const Java::Env env(jenv);

      const Java::String repos_root_url(env, jrepos_root_url);
      const Java::String parent_dir_url(env, jparent_dir_url);
      const JavaHL::ExternalItem item(env, jitem);

      // Using a "global" request pool since we don't keep a context
      // with its own pool around for these functions.
      SVN::Pool pool;

      const char* resolved_url;
      SVN_JAVAHL_CHECK(env,
                       svn_wc__resolve_relative_external_url(
                           &resolved_url,
                           item.get_external_item(pool),
                           Java::String::Contents(repos_root_url).c_str(),
                           Java::String::Contents(parent_dir_url).c_str(),
                           pool.getPool(), pool.getPool()));
      return Java::String(env, resolved_url).get();
    }
  SVN_JAVAHL_JNI_CATCH;
  return NULL;
}
