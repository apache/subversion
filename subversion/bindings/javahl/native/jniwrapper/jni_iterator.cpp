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

#include "jni_iterator.hpp"

#include "svn_private_config.h"

namespace Java {

// Class Java::BaseIterator

const char* const BaseIterator::m_class_name = "java/util/Iterator";

BaseIterator::ClassImpl::ClassImpl(Env env, jclass cls)
  : Object::ClassImpl(env, cls),
    m_mid_has_next(env.GetMethodID(cls, "hasNext", "()Z")),
    m_mid_next(env.GetMethodID(cls, "next", "()Ljava/lang/Object;"))
{}

BaseIterator::ClassImpl::~ClassImpl() {}

jobject BaseIterator::next()
{
  try
    {
      return m_env.CallObjectMethod(m_jthis, impl().m_mid_next);
    }
  catch (const SignalExceptionThrown&)
    {
      // Just rethrow if it's not a NoSuchElementException.
      if (!m_env.IsInstanceOf(
              m_env.ExceptionOccurred(),
              ClassCache::get_exc_no_such_element(m_env)->get_class()))
        throw;

      m_env.ExceptionClear();
      throw std::range_error(_("Iterator out of bounds"));
    }
}

} // namespace Java
