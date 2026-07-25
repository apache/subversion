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
 * @file PropertyTable.h
 * @brief Interface of the class PropertyTable
 */

#ifndef REVPROPTABLE_H
#define REVPROPTABLE_H

#include <jni.h>
#include "Pool.h"

struct apr_hash_t;

#include "Path.h"
#include <map>
#include <string>

class PropertyTable
{
 private:
  std::map<std::string, std::string> m_revprops;
  jobject m_revpropTable;
  bool m_empty_if_null;
 public:
  PropertyTable(jobject jrevpropTable,
                bool bytearray_values,
                bool empty_if_null);
  ~PropertyTable();
  apr_hash_t *hash(const SVN::Pool &pool);
};

#endif // REVPROPTABLE_H
