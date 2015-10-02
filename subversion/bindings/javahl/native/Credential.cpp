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

#include "Credential.hpp"

#include "JNIUtil.h"

namespace JavaHL {

// Class JavaHL::Credential
const char* const Credential::m_class_name =
  JAVAHL_CLASS("/SVNUtil$Credential");

Credential::ClassImpl::ClassImpl(::Java::Env env, jclass cls)
  : ::Java::Object::ClassImpl(env, cls),
    m_mid_ctor(
        env.GetMethodID(cls, "<init>",
                        "(" JAVAHL_ARG("/SVNUtil$Credential$Kind;")
                        "Ljava/lang/String;Ljava/lang/String;"
                        "Ljava/lang/String;Ljava/lang/String;"
                        JAVAHL_ARG("/callback/AuthnCallback$SSLServerCertInfo;")
                        JAVAHL_ARG("/callback/AuthnCallback$SSLServerCertFailures;")
                        "Ljava/lang/String;)V"))
{}

Credential::ClassImpl::~ClassImpl() {}

Credential::Credential(::Java::Env env, jobject kind,
                       const ::Java::String& realm,
                       const ::Java::String& store,
                       const ::Java::String& username,
                       const ::Java::String& password,
                       jobject info, jobject failures,
                       const ::Java::String& passphrase)
  : ::Java::Object(env, ::Java::ClassCache::get_credential(env))
{
  set_this(env.NewObject(get_class(), impl().m_mid_ctor,
                         kind, realm.get(), store.get(),
                         username.get(), password.get(),
                         info, failures, passphrase.get()));
}

// Enum JavaHL::Credential::Kind
const char* const Credential::Kind::m_class_name =
  JAVAHL_CLASS("/SVNUtil$Credential$Kind");

Credential::Kind::ClassImpl::ClassImpl(::Java::Env env, jclass cls)
  : ::Java::Object::ClassImpl(env, cls),
    m_static_mid_from_string(
        env.GetStaticMethodID(cls, "fromString",
                              "(Ljava/lang/String;)"
                              JAVAHL_ARG("/SVNUtil$Credential$Kind;")))
{}

Credential::Kind::ClassImpl::~ClassImpl() {}

Credential::Kind::Kind(::Java::Env env,
                       const ::Java::String& value)
  : ::Java::Object(env, ::Java::ClassCache::get_credential_kind(env))
{
  set_this(env.CallStaticObjectMethod(
               get_class(), impl().m_static_mid_from_string, value.get()));
}

} // namespace JavaHL
