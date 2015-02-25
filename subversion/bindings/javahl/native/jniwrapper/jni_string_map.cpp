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

// Class Java::BaseImmutableMap

const char* const BaseImmutableMap::m_class_name = "java/util/Map";

BaseImmutableMap::ClassImpl::ClassImpl(Env env, jclass cls)
  : Object::ClassImpl(env, cls),
    m_mid_put(env.GetMethodID(cls, "put",
                              "(Ljava/lang/Object;Ljava/lang/Object;)"
                              "Ljava/lang/Object;")),
    m_mid_clear(env.GetMethodID(cls, "clear", "()V")),
    m_mid_has_key(env.GetMethodID(cls, "containsKey",
                                  "(Ljava/lang/Object;)Z")),
    m_mid_get(env.GetMethodID(cls, "get",
                              "(Ljava/lang/Object;)Ljava/lang/Object;")),
    m_mid_size(env.GetMethodID(cls, "size", "()I")),
    m_mid_entry_set(env.GetMethodID(cls, "entrySet", "()Ljava/util/Set;"))
{}

BaseImmutableMap::ClassImpl::~ClassImpl() {}

jobject BaseImmutableMap::operator[](const std::string& index) const
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

BaseImmutableMap::Iterator BaseImmutableMap::get_iterator() const
{
  const jobject jentry_set =
    m_env.CallObjectMethod(m_jthis, impl().m_mid_entry_set);
  const jobject jiterator =
    m_env.CallObjectMethod(jentry_set, Set::impl(m_env).m_mid_iterator);
  return Iterator(m_env, jiterator);
}

// Class Java::BaseImmutableMap::Entry

const char* const BaseImmutableMap::Entry::m_class_name = "java/util/Map$Entry";

BaseImmutableMap::Entry::ClassImpl::ClassImpl(Env env, jclass cls)
  : Object::ClassImpl(env, cls),
    m_mid_get_key(env.GetMethodID(cls, "getKey", "()Ljava/lang/Object;")),
    m_mid_get_value(env.GetMethodID(cls, "getValue", "()Ljava/lang/Object;"))
{}

BaseImmutableMap::Entry::ClassImpl::~ClassImpl() {}

// Class Java::BaseImmutableMap::Set

const char* const BaseImmutableMap::Set::m_class_name = "java/util/Set";

BaseImmutableMap::Set::ClassImpl::ClassImpl(Env env, jclass cls)
  : Object::ClassImpl(env, cls),
    m_mid_iterator(env.GetMethodID(cls, "iterator",
                                   "()Ljava/util/Iterator;"))
{}

BaseImmutableMap::Set::ClassImpl::~ClassImpl() {}


// Class Java::BaseMap

const char* const BaseMap::m_class_name = "java/util/HashMap";

BaseMap::ClassImpl::ClassImpl(Env env, jclass cls)
  : BaseImmutableMap::ClassImpl(env, cls),
    m_mid_ctor(env.GetMethodID(cls, "<init>", "(I)V"))
{}

BaseMap::ClassImpl::~ClassImpl() {}

} // namespace Java
