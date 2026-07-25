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

#include "Version.hpp"

#include "JNIUtil.h"

namespace JavaHL {

// Class JavaHL::Version
const char* const Version::m_class_name =
  JAVAHL_CLASS("/types/Version");

Version::ClassImpl::ClassImpl(::Java::Env env, jclass cls)
  : ::Java::Object::ClassImpl(env, cls),
    m_mid_get_instance(
        env.GetStaticMethodID(cls, "getInstance",
                              "(III)" JAVAHL_ARG("/types/Version") ";")),
    m_mid_get_major(env.GetMethodID(cls, "getMajor", "()I")),
    m_mid_get_minor(env.GetMethodID(cls, "getMinor", "()I")),
    m_mid_get_patch(env.GetMethodID(cls, "getPatch", "()I"))
{}

Version::ClassImpl::~ClassImpl() {}


jobject Version::getInstance(::Java::Env env,
                             const svn_version_t &version)
{
  const ClassImpl &impl =
    *dynamic_cast<const ClassImpl*>(::Java::ClassCache::get_version(env));

  return env.CallStaticObjectMethod(
      impl.get_class(), impl.m_mid_get_instance,
      version.major, version.minor, version.patch);
}

void Version::getVersion(svn_version_t &version) const
{
  jint major = m_env.CallIntMethod(m_jthis, impl().m_mid_get_major);
  jint minor = m_env.CallIntMethod(m_jthis, impl().m_mid_get_minor);
  jint patch = m_env.CallIntMethod(m_jthis, impl().m_mid_get_patch);

  version.major = major;
  version.minor = minor;
  version.patch = patch;
  version.tag = "";
}

} // namespace JavaHL
