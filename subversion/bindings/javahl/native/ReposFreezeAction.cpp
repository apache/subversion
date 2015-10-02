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
 *
 * @file ReposFreezeAction.cpp
 * @brief Native mirror of the ReposFreezeAction callback interface
 */

#include "ReposFreezeAction.h"

svn_error_t* ReposFreezeAction::callback(void* baton, apr_pool_t*)
{
  ReposFreezeAction* that = static_cast<ReposFreezeAction*>(baton);
  return that->invoke();
}

svn_error_t* ReposFreezeAction::invoke()
{
  JNIEnv *const env = JNIUtil::getEnv();

  static volatile jmethodID mid = 0;
  if (!mid)
    {
      jclass cls = env->FindClass(JAVAHL_CLASS("/callback/ReposFreezeAction"));
      if (!JNIUtil::isJavaExceptionThrown())
        mid = env->GetMethodID(cls, "invoke", "()V");
    }

  if (!JNIUtil::isJavaExceptionThrown())
    env->CallVoidMethod(m_jaction, mid);
  return SVN_NO_ERROR;
}
