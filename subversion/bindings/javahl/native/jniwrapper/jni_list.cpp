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

#include "jni_list.hpp"

namespace Java {

// Class Java::BaseList

const char* const BaseList::m_class_name = "java/util/List";

BaseList::ovector
BaseList::convert_to_vector(Env env, jclass cls, jobject jlist)
{
  const jint length = env.CallIntMethod(
      jlist, env.GetMethodID(cls, "size", "()I"));

  if (!length)
    return ovector();

  const jmethodID mid_get = env.GetMethodID(cls, "get",
                                            "(I)Ljava/lang/Object;");
  ovector contents(length);
  ovector::iterator it;
  jint i;
  for (i = 0, it = contents.begin(); it != contents.end(); ++it, ++i)
    *it = env.CallObjectMethod(jlist, mid_get, i);
  return contents;
}


// Class Java::BaseMutableList

const char* const BaseMutableList::m_class_name = "java/util/ArrayList";

namespace {
jobject make_array_list(Env env, const char* class_name, jint length)
{
  const jclass cls = env.FindClass(class_name);
  const jmethodID mid_ctor = env.GetMethodID(cls, "<init>", "(I)V");
  return env.NewObject(cls, mid_ctor, length);
}
} // anonymous namespace

BaseMutableList::BaseMutableList(Env env, jint length)
  : Object(env, m_class_name,
           make_array_list(env, m_class_name, length))
{}

void BaseMutableList::add(jobject obj)
{
  if (!m_mid_add)
    m_mid_add = m_env.GetMethodID(m_class, "add", "(Ljava/lang/Object;)Z");
  m_env.CallBooleanMethod(m_jthis, m_mid_add, obj);
}

void BaseMutableList::clear()
{
  if (!m_mid_clear)
    m_mid_clear = m_env.GetMethodID(m_class, "clear", "()V");
  m_env.CallVoidMethod(m_jthis, m_mid_clear);
}

jobject BaseMutableList::operator[](jint index) const
{
  if (!m_mid_get)
    m_mid_get = m_env.GetMethodID(m_class, "get", "(I)Ljava/lang/Object;");
  return m_env.CallObjectMethod(m_jthis, m_mid_get, index);
}

jint BaseMutableList::length() const
{
  if (!m_mid_size)
    m_mid_size = m_env.GetMethodID(m_class, "size", "()I");
  return m_env.CallIntMethod(m_jthis, m_mid_add);
}

} // namespace Java
