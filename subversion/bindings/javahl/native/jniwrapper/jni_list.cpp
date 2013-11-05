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

const char* const BaseList::m_class_name = "java/util/List";

BaseList::ovector
BaseList::convert_to_vector(Env env, jclass cls, jobject jlist)
{
  const size_type length = size_type(
      env.CallIntMethod(jlist, env.GetMethodID(cls, "size", "()I")));

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

} // namespace Java
