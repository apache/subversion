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
 * @file DiffOptions.h
 * @brief Native interpretation of the ...types.DiffOptions class
 */

#ifndef JAVAHL_DIFF_OPTIONS_H
#define JAVAHL_DIFF_OPTIONS_H

#include <apr_tables.h>
#include "svn_types.h"
#include "svn_diff.h"
#include "Pool.h"
#include "JNIUtil.h"

class DiffOptions
{
 public:
  DiffOptions(jobject joptions);

  apr_array_header_t *optionsArray(SVN::Pool &resultPool) const;
  svn_diff_file_options_t *fileOptions(SVN::Pool &resultPool) const;

  svn_boolean_t useGitDiffFormat() const
  {
    return (flags & USE_GIT_DIFF_FORMAT ? TRUE : FALSE);
  }

 private:
  static const jint IGNORE_ALL_SPACE    = 0x01;
  static const jint IGNORE_SPACE_CHANGE = 0x02;
  static const jint IGNORE_EOL_STYLE    = 0x04;
  static const jint SHOW_C_FUNCTION     = 0x08;
  static const jint USE_GIT_DIFF_FORMAT = 0x10;

  const jint flags;
};


#endif /* JAVAHL_DIFF_OPTIONS_H */
