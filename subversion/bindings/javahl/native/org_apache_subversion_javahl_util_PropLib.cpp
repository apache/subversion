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

#include "../include/org_apache_subversion_javahl_util_PropLib.h"

#include "JNIStackElement.h"
#include "JNIStringHolder.h"
#include "JNIByteArray.h"
#include "JNIUtil.h"
#include "InputStream.h"
#include "EnumMapper.h"
#include "Path.h"
#include "Pool.h"

#include "svn_props.h"
#include "svn_wc.h"


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
} // anonymous namespace


JNIEXPORT jbyteArray JNICALL
Java_org_apache_subversion_javahl_util_PropLib_checkNodeProp(
    JNIEnv* env, jobject jthis,
    jstring jname, jbyteArray jvalue, jstring jpath, jobject jkind,
    jstring jmime_type, jobject jfile_contents,
    jboolean jskip_some_checks)
{
  JNIEntry(PropLib, checkLocalProp);

  JNIStringHolder name(jname);
  if (JNIUtil::isJavaExceptionThrown())
    return NULL;

  JNIByteArray value(jvalue);
  if (JNIUtil::isJavaExceptionThrown())
    return NULL;

  JNIStringHolder path(jpath);
  if (JNIUtil::isJavaExceptionThrown())
    return NULL;

  svn_node_kind_t kind = EnumMapper::toNodeKind(jkind);
  if (JNIUtil::isJavaExceptionThrown())
    return NULL;

  JNIStringHolder mime_type(jmime_type);
  if (JNIUtil::isJavaExceptionThrown())
    return NULL;

  InputStream contents(jfile_contents);
  if (JNIUtil::isJavaExceptionThrown())
    return NULL;

  // Using a "global" request pool since we don't keep a context with
  // its own pool around for these functions.
  SVN::Pool pool;

  PropGetter getter(mime_type.c_str(),
                    (jfile_contents ? contents.getStream(pool) : NULL));

  svn_string_t propval;
  propval.data = reinterpret_cast<const char*>(value.getBytes());
  propval.len = value.getLength();

  const svn_string_t* canonval;
  SVN_JNI_ERR(svn_wc_canonicalize_svn_prop(
                  &canonval, name.c_str(), &propval, path.c_str(),
                  kind, svn_boolean_t(jskip_some_checks),
                  PropGetter::callback, &getter,
                  pool.getPool()),
              NULL);

  return JNIUtil::makeJByteArray(canonval->data, int(canonval->len));
}
