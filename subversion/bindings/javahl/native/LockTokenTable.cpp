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
 * @file LockTokenTable.cpp
 * @brief Implementation of the class LockTokenTable
 */

#include "LockTokenTable.h"
#include "JNIUtil.h"
#include "JNIStringHolder.h"
#include "Array.h"
#include <apr_hash.h>

LockTokenTable::~LockTokenTable()
{
  if (m_jlock_tokens)
    JNIUtil::getEnv()->DeleteLocalRef(m_jlock_tokens);
}

apr_hash_t*
LockTokenTable::hash(const SVN::Pool &pool, bool null_if_empty)
{
  if (m_lock_tokens.size() == 0 && null_if_empty)
    return NULL;

  apr_pool_t* result_pool = pool.getPool();
  apr_hash_t* lock_table = apr_hash_make(result_pool);

  for (lock_tokens_t::const_iterator it = m_lock_tokens.begin();
       it != m_lock_tokens.end(); ++it)
    {
      const char *path = apr_pstrdup(result_pool, it->first.c_str());
      const char *token = apr_pstrdup(result_pool, it->second.c_str());
      apr_hash_set(lock_table, path, APR_HASH_KEY_STRING, token);
    }

  return lock_table;
}

LockTokenTable::LockTokenTable(jobject jlock_tokens)
  : m_jlock_tokens(jlock_tokens)
{

  if (jlock_tokens != NULL)
    {
      JNIEnv *env = JNIUtil::getEnv();

      jclass lock_cls = env->FindClass(JAVAHL_CLASS("/types/Lock"));
      if (JNIUtil::isExceptionThrown())
        return;

      static jmethodID getPath_mid = 0;
      if (0 == getPath_mid)
        {
          getPath_mid = env->GetMethodID(lock_cls, "getPath",
                                         "()Ljava/lang/String;");
          if (JNIUtil::isExceptionThrown())
            return;
        }

      static jmethodID getToken_mid = 0;
      if (0 == getToken_mid)
        {
          getToken_mid = env->GetMethodID(lock_cls, "getToken",
                                          "()Ljava/lang/String;");
          if (JNIUtil::isExceptionThrown())
            return;
        }

      std::vector<jobject> locks = Array(jlock_tokens).vector();
      for (std::vector<jobject>::const_iterator it = locks.begin();
           it != locks.end(); ++it)
        {
          jobject jpath = env->CallObjectMethod(*it, getPath_mid);
          if (JNIUtil::isExceptionThrown())
            return;
          jobject jtoken = env->CallObjectMethod(*it, getToken_mid);
          if (JNIUtil::isExceptionThrown())
            return;

          JNIStringHolder path((jstring)jpath);
          if (JNIUtil::isExceptionThrown())
            return;
          JNIStringHolder token((jstring)jtoken);
          if (JNIUtil::isExceptionThrown())
            return;

          m_lock_tokens[std::string(static_cast<const char *>(path))] =
            std::string(static_cast<const char *>(token));

          env->DeleteLocalRef(jpath);
          env->DeleteLocalRef(jtoken);
        }
    }
}
