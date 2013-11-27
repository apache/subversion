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

#include "EditorCallbacks.hpp"

#include "jniwrapper/jni_stack.hpp"
#include "jniwrapper/jni_string.hpp"
#include "jniwrapper/jni_io_stream.hpp"

#include "Utility.hpp"

namespace JavaHL {

// class JavaHL::ProvideBaseCallback

const char* const ProvideBaseCallback::m_class_name =
  JAVA_PACKAGE"/ISVNEditor$ProvideBaseCallback";

::Java::MethodID ProvideBaseCallback::m_mid_get_contents;

void ProvideBaseCallback::static_init(::Java::Env env)
{
  m_mid_get_contents =
    env.GetMethodID(::Java::ClassCache::get_editor_provide_base_cb(),
                    "getContents",
                    "(Ljava/lang/String;)"
                    "L"JAVA_PACKAGE
                    "/ISVNEditor$ProvideBaseCallback$ReturnValue;");
}


const char* const ProvideBaseCallback::ReturnValue::m_class_name =
  JAVA_PACKAGE"/ISVNEditor$ProvideBaseCallback$ReturnValue";

::Java::FieldID ProvideBaseCallback::ReturnValue::m_fid_contents;
::Java::FieldID ProvideBaseCallback::ReturnValue::m_fid_revision;

void ProvideBaseCallback::ReturnValue::static_init(::Java::Env env)
{
  const jclass cls =
    ::Java::ClassCache::get_editor_provide_base_cb_return_value();
  m_fid_contents = env.GetFieldID(cls, "contents", "Ljava/io/InputStream;");
  m_fid_revision = env.GetFieldID(cls, "revision", "J");
}


svn_stream_t*
ProvideBaseCallback::ReturnValue::get_global_stream(apr_pool_t* pool) const
{
  jobject jstream = m_env.GetObjectField(m_jthis, m_fid_contents);
  return ::Java::InputStream::get_global_stream(m_env, jstream, pool);
}


// class JavaHL::ProvidePropsCallback

const char* const ProvidePropsCallback::m_class_name =
  JAVA_PACKAGE"/ISVNEditor$ProvidePropsCallback";


::Java::MethodID ProvidePropsCallback::m_mid_get_props;

void ProvidePropsCallback::static_init(::Java::Env env)
{
  m_mid_get_props =
    env.GetMethodID(::Java::ClassCache::get_editor_provide_props_cb(),
                    "getProperties",
                    "(Ljava/lang/String;)"
                    "L"JAVA_PACKAGE
                    "/ISVNEditor$ProvidePropsCallback$ReturnValue;");
}

const char* const ProvidePropsCallback::ReturnValue::m_class_name =
  JAVA_PACKAGE"/ISVNEditor$ProvidePropsCallback$ReturnValue";

::Java::FieldID ProvidePropsCallback::ReturnValue::m_fid_properties;
::Java::FieldID ProvidePropsCallback::ReturnValue::m_fid_revision;

void ProvidePropsCallback::ReturnValue::static_init(::Java::Env env)
{
  const jclass cls =
    ::Java::ClassCache::get_editor_provide_props_cb_return_value();
  m_fid_properties = env.GetFieldID(cls, "properties", "Ljava/util/Map;");
  m_fid_revision = env.GetFieldID(cls, "revision", "J");
}

apr_hash_t*
ProvidePropsCallback::ReturnValue::get_property_hash(apr_pool_t* pool) const
{
  jobject jproperties = m_env.GetObjectField(m_jthis, m_fid_properties);
  return Util::make_property_hash(m_env, jproperties, pool);
}


// class JavaHL::GetNodeKindCallback

const char* const GetNodeKindCallback::m_class_name =
  JAVA_PACKAGE"/ISVNEditor$GetNodeKindCallback";

::Java::MethodID GetNodeKindCallback::m_mid_get_kind;

void GetNodeKindCallback::static_init(::Java::Env env)
{
  m_mid_get_kind =
    env.GetMethodID(::Java::ClassCache::get_editor_get_kind_cb(),
                    "getKind",
                    "(Ljava/lang/String;J)"
                    "L"JAVA_PACKAGE"/types/NodeKind;");
}

} // namespace JavaHL
