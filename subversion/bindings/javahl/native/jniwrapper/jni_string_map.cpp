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

#include <stdexcept>

#include "jni_string.hpp"
#include "jni_string_map.hpp"

#include "svn_private_config.h"

namespace Java {

// Class Java::BaseMap

const char* const BaseMap::m_class_name = "java/util/Map";

BaseMap::ClassImpl::ClassImpl(Env env, jclass cls)
  : Object::ClassImpl(env, cls),
    m_mid_size(env.GetMethodID(cls, "size", "()I")),
    m_mid_entry_set(env.GetMethodID(cls, "entrySet", "()Ljava/util/Set;"))
{}

BaseMap::ClassImpl::~ClassImpl() {}

const char* const BaseMap::Set::m_class_name = "java/util/Set";

BaseMap::Set::ClassImpl::ClassImpl(Env env, jclass cls)
  : Object::ClassImpl(env, cls),
    m_mid_iterator(env.GetMethodID(cls, "iterator",
                                   "()Ljava/util/Iterator;"))
{}

BaseMap::Set::ClassImpl::~ClassImpl() {}

const char* const BaseMap::Entry::m_class_name = "java/util/Map$Entry";

BaseMap::Entry::ClassImpl::ClassImpl(Env env, jclass cls)
  : Object::ClassImpl(env, cls),
    m_mid_get_key(env.GetMethodID(cls, "getKey", "()Ljava/lang/Object;")),
    m_mid_get_value(env.GetMethodID(cls, "getValue", "()Ljava/lang/Object;"))
{}

BaseMap::Entry::ClassImpl::~ClassImpl() {}


jobject BaseMap::operator[](const std::string& index) const
{
  somap::const_iterator it = m_contents.find(index);
  if (it == m_contents.end())
    {
      std::string msg(_("Map does not contain key: "));
      msg += index;
      throw std::out_of_range(msg.c_str());
    }
  return it->second;
}

BaseMap::somap BaseMap::convert_to_map(Env env, jobject jmap)
{
  const ClassImpl* pimpl =
    dynamic_cast<const ClassImpl*>(ClassCache::get_map(env));

  if (!env.CallIntMethod(jmap, pimpl->m_mid_size))
    return somap();

  // Get an iterator over the map's entry set
  const jobject entries = env.CallObjectMethod(jmap, pimpl->m_mid_entry_set);
  const Entry::ClassImpl& entimpl = Entry::impl(env);

  Iterator iterator(env, env.CallObjectMethod(entries,
                                              Set::impl(env).m_mid_iterator));

  // Iterate over the map, filling the native map
  somap contents;
  while (iterator.has_next())
    {
      const jobject entry = iterator.next();
      const String keystr(
          env, jstring(env.CallObjectMethod(entry, entimpl.m_mid_get_key)));
      const jobject value(
          env.CallObjectMethod(entry, entimpl.m_mid_get_value));
      const String::Contents key(keystr);
      contents.insert(somap::value_type(key.c_str(), value));
    }
  return contents;
}

// Class Java::BaseMutableMap

const char* const BaseMutableMap::m_class_name = "java/util/HashMap";

BaseMutableMap::ClassImpl::ClassImpl(Env env, jclass cls)
  : Object::ClassImpl(env, cls),
    m_mid_ctor(env.GetMethodID(cls, "<init>", "(I)V")),
    m_mid_put(env.GetMethodID(cls, "put",
                              "(Ljava/lang/Object;Ljava/lang/Object;)"
                              "Ljava/lang/Object;")),
    m_mid_clear(env.GetMethodID(cls, "clear", "()V")),
    m_mid_has_key(env.GetMethodID(cls, "containsKey",
                                  "(Ljava/lang/Object;)Z")),
    m_mid_get(env.GetMethodID(cls, "get",
                              "(Ljava/lang/Object;)Ljava/lang/Object;")),
    m_mid_size(env.GetMethodID(cls, "size", "()I"))
{}

BaseMutableMap::ClassImpl::~ClassImpl() {}

jobject BaseMutableMap::operator[](const std::string& index) const
{
  const String key(m_env, index);
  if (!m_env.CallBooleanMethod(m_jthis, impl().m_mid_has_key, key.get()))
    {
      std::string msg(_("Map does not contain key: "));
      msg += index;
      throw std::out_of_range(msg.c_str());
    }
  return m_env.CallObjectMethod(m_jthis, impl().m_mid_get, key.get());
}

} // namespace Java
