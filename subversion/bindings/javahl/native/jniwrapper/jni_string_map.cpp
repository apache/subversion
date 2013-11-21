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

namespace Java {

// Class Java::BaseMap

const char* const BaseMap::m_class_name = "java/util/Map";

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

BaseMap::somap BaseMap::convert_to_map(Env env, jclass cls, jobject jmap)
{
  if (!env.CallIntMethod(jmap, env.GetMethodID(cls, "size", "()I")))
    return somap();

  // Get an iterator over the map's entry set
  const jobject entries = env.CallObjectMethod(
      jmap, env.GetMethodID(cls, "entrySet", "()Ljava/util/Set;"));
  const jobject iterator = env.CallObjectMethod(
      entries, env.GetMethodID(env.GetObjectClass(entries), "iterator",
                               "()Ljava/util/Iterator;"));
  const jclass cls_iterator = env.GetObjectClass(iterator);
  const jmethodID mid_it_has_next = env.GetMethodID(cls_iterator,
                                                    "hasNext", "()Z");
  const jmethodID mid_it_next = env.GetMethodID(cls_iterator, "next",
                                                "()Ljava/lang/Object;");

  // Find the methods for retreiving the key and value from an entry
  const jclass cls_entry = env.FindClass("java/util/Map$Entry");
  const jmethodID mid_get_key = env.GetMethodID(cls_entry, "getKey",
                                                "()Ljava/lang/Object;");
  const jmethodID mid_get_value = env.GetMethodID(cls_entry, "getValue",
                                                  "()Ljava/lang/Object;");

  // And finally ... iterate over the map, filling the native map
  somap contents;
  while (env.CallBooleanMethod(iterator, mid_it_has_next))
    {
      const jobject e = env.CallObjectMethod(iterator, mid_it_next);
      const String keystr(env, jstring(env.CallObjectMethod(e, mid_get_key)));
      const jobject value(env.CallObjectMethod(e, mid_get_value));
      const String::Contents key(keystr);
      contents.insert(somap::value_type(key.c_str(), value));
    }
  return contents;
}

// Class Java::BaseMutableMap

const char* const BaseMutableMap::m_class_name = "java/util/HashMap";

namespace {
jobject make_hash_map(Env env, const char* class_name, jint length)
{
  const jclass cls = env.FindClass(class_name);
  const jmethodID mid_ctor = env.GetMethodID(cls, "<init>", "(I)V");
  return env.NewObject(cls, mid_ctor, length);
}
} // anonymous namespace

BaseMutableMap::BaseMutableMap(Env env, jint length)
  : Object(env, m_class_name,
           make_hash_map(env, m_class_name, length))
{}

void BaseMutableMap::clear()
{
  if (!m_mid_clear)
    m_mid_clear = m_env.GetMethodID(m_class, "clear", "()V");
  m_env.CallVoidMethod(m_jthis, m_mid_clear);
}

jint BaseMutableMap::length() const
{
  if (!m_mid_size)
    m_mid_size = m_env.GetMethodID(m_class, "size", "()I");
  return m_env.CallIntMethod(m_jthis, m_mid_size);
}

void BaseMutableMap::put(const std::string& key, jobject obj)
{
  if (!m_mid_put)
    m_mid_put = m_env.GetMethodID(m_class, "put",
                                  "(Ljava/lang/Object;Ljava/lang/Object;)"
                                  "Ljava/lang/Object;");
  m_env.CallObjectMethod(m_jthis, m_mid_put, String(m_env, key).get(), obj);
}

jobject BaseMutableMap::operator[](const std::string& index) const
{
  if (!m_mid_has_key)
    m_mid_has_key = m_env.GetMethodID(m_class, "containsKey",
                                      "(Ljava/lang/Object;)Z");

  const String key(m_env, index);
  if (!m_env.CallBooleanMethod(m_jthis, m_mid_has_key, key.get()))
    {
      std::string msg(_("Map does not contain key: "));
      msg += index;
      throw std::out_of_range(msg.c_str());
    }

  if (!m_mid_get)
    m_mid_get = m_env.GetMethodID(m_class, "get",
                                  "(Ljava/lang/Object;)Ljava/lang/Object;");
  return m_env.CallObjectMethod(m_jthis, m_mid_get, key.get());
}

} // namespace Java
