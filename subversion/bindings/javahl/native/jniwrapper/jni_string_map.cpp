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
MethodID BaseMap::m_mid_size;
MethodID BaseMap::m_mid_entry_set;
void BaseMap::static_init(Env env)
{
  const jclass cls = ClassCache::get_map();
  m_mid_size = env.GetMethodID(cls, "size", "()I");
  m_mid_entry_set = env.GetMethodID(cls, "entrySet", "()Ljava/util/Set;");
}

const char* const BaseMap::Set::m_class_name = "java/util/Set";
MethodID BaseMap::Set::m_mid_iterator;
void BaseMap::Set::static_init(Env env)
{
  m_mid_iterator = env.GetMethodID(ClassCache::get_set(), "iterator",
                                   "()Ljava/util/Iterator;");
}

const char* const BaseMap::Iterator::m_class_name = "java/util/Iterator";
MethodID BaseMap::Iterator::m_mid_has_next;
MethodID BaseMap::Iterator::m_mid_next;
void BaseMap::Iterator::static_init(Env env)
{
  const jclass cls = ClassCache::get_iterator();
  m_mid_has_next = env.GetMethodID(cls, "hasNext", "()Z");
  m_mid_next = env.GetMethodID(cls, "next", "()Ljava/lang/Object;");
}

const char* const BaseMap::Entry::m_class_name = "java/util/Map$Entry";
MethodID BaseMap::Entry::m_mid_get_key;
MethodID BaseMap::Entry::m_mid_get_value;
void BaseMap::Entry::static_init(Env env)
{
  const jclass cls = ClassCache::get_map_entry();
  m_mid_get_key = env.GetMethodID(cls, "getKey", "()Ljava/lang/Object;");
  m_mid_get_value = env.GetMethodID(cls, "getValue", "()Ljava/lang/Object;");
}


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
  if (!env.CallIntMethod(jmap, m_mid_size))
    return somap();

  // Get an iterator over the map's entry set
  const jobject entries = env.CallObjectMethod(jmap, m_mid_entry_set);
  const jobject iterator = env.CallObjectMethod(entries, Set::m_mid_iterator);

  // Yterate over the map, filling the native map
  somap contents;
  while (env.CallBooleanMethod(iterator, Iterator::m_mid_has_next))
    {
      const jobject entry =
        env.CallObjectMethod(iterator, Iterator::m_mid_next);
      const String keystr(
          env, jstring(env.CallObjectMethod(entry, Entry::m_mid_get_key)));
      const jobject value(
          env.CallObjectMethod(entry, Entry::m_mid_get_value));
      const String::Contents key(keystr);
      contents.insert(somap::value_type(key.c_str(), value));
    }
  return contents;
}

// Class Java::BaseMutableMap

const char* const BaseMutableMap::m_class_name = "java/util/HashMap";

MethodID BaseMutableMap::m_mid_ctor;
MethodID BaseMutableMap::m_mid_put;
MethodID BaseMutableMap::m_mid_clear;
MethodID BaseMutableMap::m_mid_has_key;
MethodID BaseMutableMap::m_mid_get;
MethodID BaseMutableMap::m_mid_size;

void BaseMutableMap::static_init(Env env)
{
  const jclass cls = ClassCache::get_hash_map();
  m_mid_ctor = env.GetMethodID(cls, "<init>", "(I)V");
  m_mid_put = env.GetMethodID(cls, "put",
                              "(Ljava/lang/Object;Ljava/lang/Object;)"
                              "Ljava/lang/Object;");
  m_mid_clear = env.GetMethodID(cls, "clear", "()V");
  m_mid_has_key = env.GetMethodID(cls, "containsKey",
                                  "(Ljava/lang/Object;)Z");
  m_mid_get = env.GetMethodID(cls, "get",
                              "(Ljava/lang/Object;)Ljava/lang/Object;");
  m_mid_size = env.GetMethodID(cls, "size", "()I");
}


jobject BaseMutableMap::operator[](const std::string& index) const
{
  const String key(m_env, index);
  if (!m_env.CallBooleanMethod(m_jthis, m_mid_has_key, key.get()))
    {
      std::string msg(_("Map does not contain key: "));
      msg += index;
      throw std::out_of_range(msg.c_str());
    }
  return m_env.CallObjectMethod(m_jthis, m_mid_get, key.get());
}

} // namespace Java
