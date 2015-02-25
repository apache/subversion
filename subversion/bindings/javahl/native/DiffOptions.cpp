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
 * @file DiffOptions.cpp
 * @brief Implementation of the DiffOptions class
 */

#include "DiffOptions.h"

namespace {
static jint get_diff_options_flags(jobject joptions)
{
  if (!joptions)
    return 0;

  JNIEnv *const env = JNIUtil::getEnv();

  static volatile jfieldID fid = 0;
  if (!fid)
    {
      fid = env->GetFieldID(env->GetObjectClass(joptions), "flags", "I");
      if (JNIUtil::isJavaExceptionThrown())
        return 0;
    }

  const jint flags = env->GetIntField(joptions, fid);
  if (JNIUtil::isJavaExceptionThrown())
    return 0;
  return flags;
}
} // anonymous namespace

DiffOptions::DiffOptions(jobject joptions)
  : flags(get_diff_options_flags(joptions))
{}


apr_array_header_t *DiffOptions::optionsArray(SVN::Pool &resultPool) const
{
  // JavaHL ignores the default diff options from the client config
  // files, so we always have to allocate an array, even if it will
  // remain empty.
  apr_array_header_t *opt = apr_array_make(resultPool.getPool(),
                                           0, sizeof(const char*));

  if (flags & IGNORE_ALL_SPACE)
    APR_ARRAY_PUSH(opt, const char*) = "--ignore-all-space";
  if (flags & IGNORE_SPACE_CHANGE)
    APR_ARRAY_PUSH(opt, const char*) = "--ignore-space-change";
  if (flags & IGNORE_EOL_STYLE)
    APR_ARRAY_PUSH(opt, const char*) = "--ignore-eol-style";
  if (flags & SHOW_C_FUNCTION)
    APR_ARRAY_PUSH(opt, const char*) = "--show-c-function";

  /* TODO: Support -U (context size) */

  return opt;
}

svn_diff_file_options_t *DiffOptions::fileOptions(SVN::Pool &resultPool) const
{
  svn_diff_file_options_t *opt;

  opt = svn_diff_file_options_create(resultPool.getPool());

  if (flags & IGNORE_ALL_SPACE)
    opt->ignore_space = svn_diff_file_ignore_space_all;
  else if (flags & IGNORE_SPACE_CHANGE)
    opt->ignore_eol_style = svn_diff_file_ignore_space_change;

  if (flags & IGNORE_EOL_STYLE)
    opt->ignore_eol_style = TRUE;

  if (flags & SHOW_C_FUNCTION)
    opt->show_c_function = TRUE;

  /* TODO: Support context size */

  return opt;
}
