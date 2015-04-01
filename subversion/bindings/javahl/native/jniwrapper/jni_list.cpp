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
#include <string>

#include "jni_list.hpp"

#include "svn_private_config.h"

namespace Java {

// Class Java::BaseImmutableList

const char* const BaseImmutableList::m_class_name = "java/util/List";

BaseImmutableList::ClassImpl::ClassImpl(Env env, jclass cls)
  : Object::ClassImpl(env, cls),
    m_mid_size(env.GetMethodID(cls, "size", "()I")),
    m_mid_get(env.GetMethodID(cls, "get", "(I)Ljava/lang/Object;")),
    m_mid_add(env.GetMethodID(cls, "add", "(Ljava/lang/Object;)Z")),
    m_mid_clear(env.GetMethodID(cls, "clear", "()V")),
    m_mid_iter(env.GetMethodID(cls, "listIterator", "()Ljava/util/ListIterator;"))
{}

BaseImmutableList::ClassImpl::~ClassImpl() {}

jobject BaseImmutableList::operator[](jint index) const
{
  try
    {
      return m_env.CallObjectMethod(m_jthis, impl().m_mid_get, index);
    }
  catch (const SignalExceptionThrown&)
    {
      // Just rethrow if it's not an IndexOutOfBoundsException.
      if (!m_env.IsInstanceOf(
              m_env.ExceptionOccurred(),
              ClassCache::get_exc_index_out_of_bounds(m_env)->get_class()))
        throw;

      m_env.ExceptionClear();
      std::string msg(_("List index out of bounds: "));
      msg += index;
      throw std::out_of_range(msg.c_str());
    }
}

BaseImmutableList::Iterator BaseImmutableList::get_iterator() const
{
  return Iterator(m_env, m_env.CallObjectMethod(m_jthis, impl().m_mid_iter));
}

// Class Java::BaseList

const char* const BaseList::m_class_name = "java/util/ArrayList";

BaseList::ClassImpl::ClassImpl(Env env, jclass cls)
  : BaseImmutableList::ClassImpl(env, cls),
    m_mid_ctor(env.GetMethodID(cls, "<init>", "(I)V"))
{}

BaseList::ClassImpl::~ClassImpl() {}

} // namespace Java
