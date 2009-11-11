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
 * @file BlameCallback.h
 * @brief Interface of the class BlameCallback
 */

#ifndef BLAMECALLBACK_H
#define BLAMECALLBACK_H

#include <jni.h>
#include "svn_client.h"

/**
 * This class holds a Java callback object, which will receive every
 * line of the file for which the callback information is requested.
 */
class BlameCallback
{
 public:
  BlameCallback(jobject jcallback);
  ~BlameCallback();

  static svn_error_t *callback(void *baton,
                               apr_int64_t line_no,
                               svn_revnum_t revision,
                               const char *author,
                               const char *date,
                               svn_revnum_t merged_revision,
                               const char *merged_author,
                               const char *merged_date,
                               const char *merged_path,
                               const char *line,
                               apr_pool_t *pool);

 protected:
  svn_error_t *singleLine(svn_revnum_t revision, const char *author,
                          const char *date, svn_revnum_t mergedRevision,
                          const char *mergedAuthor, const char *mergedDate,
                          const char *mergedPath, const char *line,
                          apr_pool_t *pool);

 private:
  /**
   * This a local reference to the Java object.
   */
  jobject m_callback;
};

#endif  // BLAMECALLBACK_H
