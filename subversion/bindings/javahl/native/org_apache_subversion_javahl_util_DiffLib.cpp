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
 * @file org_apache_subversion_javahl_util_DiffLib.cpp
 * @brief Implementation of the native methods in the Java class DiffLib
 */

#include "../include/org_apache_subversion_javahl_util_DiffLib.h"

#include "JNIStackElement.h"
#include "JNIStringHolder.h"
#include "JNIUtil.h"
#include "OutputStream.h"
#include "Path.h"
#include "Pool.h"

#include "svn_diff.h"

JNIEXPORT jboolean JNICALL
Java_org_apache_subversion_javahl_util_DiffLib_nativeFileDiff(
    JNIEnv* env, jobject jthis,
    jstring joriginal_file,
    jstring jmodified_file,

    jint jignore_space_ordinal,
    jboolean jignore_eol_style,
    jboolean jshow_c_function,
    jint jcontext_size,

    jstring joriginal_header,
    jstring jmodified_header,
    jstring jheader_encoding,
    jstring jrelative_to_dir,

    jobject jresult_stream)
{
  JNIEntry(DiffLib, nativeFileDiff);

  // Using a "global" request pool since we don't keep a context with
  // its own pool around for these functions.
  SVN::Pool pool;

  Path original(joriginal_file, pool);
  if (JNIUtil::isJavaExceptionThrown())
    return false;
  SVN_JNI_ERR(original.error_occurred(), false);

  Path modified(jmodified_file, pool);
  if (JNIUtil::isJavaExceptionThrown())
    return false;
  SVN_JNI_ERR(modified.error_occurred(), false);

  svn_diff_t* diff;
  svn_diff_file_options_t* diff_options =
    svn_diff_file_options_create(pool.getPool());
  diff_options->ignore_space =
    svn_diff_file_ignore_space_t(jignore_space_ordinal);
  diff_options->ignore_eol_style = svn_boolean_t(jignore_eol_style);
  diff_options->show_c_function = svn_boolean_t(jshow_c_function);
  SVN_JNI_ERR(svn_diff_file_diff_2(&diff,
                                   original.c_str(),
                                   modified.c_str(),
                                   diff_options,
                                   pool.getPool()),
              false);

  const jboolean diffs = svn_diff_contains_diffs(diff);

  JNIStringHolder original_header(joriginal_header);
  if (JNIUtil::isJavaExceptionThrown())
    return false;

  JNIStringHolder modified_header(jmodified_header);
  if (JNIUtil::isJavaExceptionThrown())
    return false;

  JNIStringHolder header_encoding(jheader_encoding);
  if (JNIUtil::isJavaExceptionThrown())
    return false;

  JNIStringHolder relative_to_dir(jrelative_to_dir);
  if (JNIUtil::isJavaExceptionThrown())
    return false;

  OutputStream result_stream(jresult_stream);

  SVN_JNI_ERR(svn_diff_file_output_unified4(
                  result_stream.getStream(pool), diff,
                  original.c_str(), modified.c_str(),
                  original_header.c_str(), modified_header.c_str(),
                  header_encoding.c_str(), relative_to_dir.c_str(),
                  diff_options->show_c_function, int(jcontext_size),
                  NULL, NULL, pool.getPool()),
              false);

  return diffs;
}

JNIEXPORT jboolean JNICALL
Java_org_apache_subversion_javahl_util_DiffLib_nativeFileMerge(
    JNIEnv* env, jobject jthis,
    jstring joriginal_file,
    jstring jmodified_file,
    jstring jlatest_file,

    jint jignore_space_ordinal,
    jboolean jignore_eol_style,
    jboolean jshow_c_function,

    jstring jconflict_original,
    jstring jconflict_modified,
    jstring jconflict_latest,
    jstring jconflict_separator,
    jint jconflict_style_ordinal,

    jobject jresult_stream)
{
  JNIEntry(DiffLib, nativeFileMerge);

  // Using a "global" request pool since we don't keep a context with
  // its own pool around for these functions.
  SVN::Pool pool;

  Path original(joriginal_file, pool);
  if (JNIUtil::isJavaExceptionThrown())
    return false;
  SVN_JNI_ERR(original.error_occurred(), false);

  Path modified(jmodified_file, pool);
  if (JNIUtil::isJavaExceptionThrown())
    return false;
  SVN_JNI_ERR(modified.error_occurred(), false);

  Path latest(jlatest_file, pool);
  if (JNIUtil::isJavaExceptionThrown())
    return false;
  SVN_JNI_ERR(latest.error_occurred(), false);

  svn_diff_t* diff;
  svn_diff_file_options_t* diff_options =
    svn_diff_file_options_create(pool.getPool());
  diff_options->ignore_space =
    svn_diff_file_ignore_space_t(jignore_space_ordinal);
  diff_options->ignore_eol_style = svn_boolean_t(jignore_eol_style);
  diff_options->show_c_function = svn_boolean_t(jshow_c_function);
  SVN_JNI_ERR(svn_diff_file_diff3_2(&diff,
                                    original.c_str(),
                                    modified.c_str(),
                                    latest.c_str(),
                                    diff_options,
                                    pool.getPool()),
              false);

  const jboolean conflicts = svn_diff_contains_conflicts(diff);

  JNIStringHolder conflict_original(jconflict_original);
  if (JNIUtil::isJavaExceptionThrown())
    return false;

  JNIStringHolder conflict_modified(jconflict_modified);
  if (JNIUtil::isJavaExceptionThrown())
    return false;

  JNIStringHolder conflict_latest(jconflict_latest);
  if (JNIUtil::isJavaExceptionThrown())
    return false;

  JNIStringHolder conflict_separator(jconflict_separator);
  if (JNIUtil::isJavaExceptionThrown())
    return false;

  OutputStream result_stream(jresult_stream);

  SVN_JNI_ERR(svn_diff_file_output_merge3(
                  result_stream.getStream(pool), diff,
                  original.c_str(), modified.c_str(), latest.c_str(),
                  conflict_original.c_str(),
                  conflict_modified.c_str(),
                  conflict_latest.c_str(),
                  conflict_separator.c_str(),
                  svn_diff_conflict_display_style_t(jconflict_style_ordinal),
                  NULL, NULL,
                  pool.getPool()),
              false);

  return conflicts;
}
