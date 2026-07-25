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

#ifndef SVN_JAVAHL_JNIWRAPPER_GLOBALREF_HPP
#define SVN_JAVAHL_JNIWRAPPER_GLOBALREF_HPP

#include <memory>

#include <jni.h>

#include "jni_env.hpp"

namespace Java {

/**
 * Wrapper for a global object reference. The reference is held until
 * the wrapper goes out of scope (i.e., until the destructor is called).
 *
 * @since New in 1.9.
 */
class GlobalObject
{
public:
  explicit GlobalObject(Env env, jobject obj)
    : m_obj(obj ? env.NewGlobalRef(obj) : NULL)
    {}

  ~GlobalObject() throw()
    {
      if (m_obj)
        Env().DeleteGlobalRef(m_obj);
    }

  GlobalObject& operator=(jobject that)
    {
      this->~GlobalObject();
      return *new(this) GlobalObject(Env(), that);
    }

  jobject get() const throw()
    {
      return m_obj;
    }

private:
  GlobalObject(const GlobalObject&);
  GlobalObject& operator=(const GlobalObject&);

  jobject m_obj;
};

/**
 * Wrapper for a global class reference. Behaves just like the object
 * reference wrapper, but provides a more type-safe interface for
 * class references.
 *
 * @since New in 1.9.
 */
class GlobalClass : protected GlobalObject
{
public:
  explicit GlobalClass(Env env, jclass cls)
    : GlobalObject(env, cls)
    {}

  GlobalClass& operator=(jclass that)
    {
      GlobalObject::operator=(that);
      return *this;
    }

  jclass get() const throw()
    {
      return jclass(GlobalObject::get());
    }

private:
  GlobalClass(const GlobalClass&);
  GlobalClass& operator=(const GlobalClass&);
};

} // namespace Java

#endif // SVN_JAVAHL_JNIWRAPPER_GLOBALREF_HPP
