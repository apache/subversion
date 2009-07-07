/**
 * @copyright
 * ====================================================================
 *    Licensed to the Subversion Corporation (SVN Corp.) under one
 *    or more contributor license agreements.  See the NOTICE file
 *    distributed with this work for additional information
 *    regarding copyright ownership.  The SVN Corp. licenses this file
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
 * @file StringArray.h
 * @brief Interface of the class StringArray
 */

#ifndef STRINGARRAY_H
#define STRINGARRAY_H

#include <jni.h>
#include "Pool.h"

struct apr_array_header_t;
struct svn_error_t;
class SVN::Pool;

#include "Path.h"
#include <vector>
#include <string>

class StringArray
{
 private:
  std::vector<std::string> m_strings;
  jobjectArray m_stringArray;
 public:
  StringArray(jobjectArray jstrings);
  ~StringArray();
  const apr_array_header_t *array(const SVN::Pool &pool);
};

#endif // STRINGARRAY_H
