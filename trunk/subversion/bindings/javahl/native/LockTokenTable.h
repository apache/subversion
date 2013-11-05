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
 * @file LockTokenTable.h
 * @brief Interface of the class LockTokenTable
 */

#ifndef JAVAHL_LOCK_TOKEN_TABLE_H
#define JAVAHL_LOCK_TOKEN_TABLE_H

#include <jni.h>
#include "Pool.h"

struct apr_hash_t;

#include <map>
#include <string>

class LockTokenTable
{
 private:
  typedef std::map<std::string, std::string> lock_tokens_t;
  lock_tokens_t m_lock_tokens;
  jobject m_jlock_tokens;
 public:
  LockTokenTable(jobject jlock_tokens);
  ~LockTokenTable();
  apr_hash_t *hash(const SVN::Pool &pool, bool null_if_empty = true);
};

#endif // JAVAHL_LOCK_TOKEN_TABLE_H
