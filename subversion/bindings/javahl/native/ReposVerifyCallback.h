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
 * @file ReposVerifyCallback.h
 * @brief Interface of the class ReposVerifyCallback
 */

#ifndef SVN_JAVAHL_REPOS_VERIFY_CALLBACK_H
#define SVN_JAVAHL_REPOS_VERIFY_CALLBACK_H

#include <jni.h>
#include "svn_repos.h"

/**
 * This class passes notification from subversion to a Java object
 * (1.2 version).
 */
class ReposVerifyCallback
{
 private:
  /**
   * The local reference to the Java object.
   */
  jobject m_jverify_cb;

 public:
  /**
   * Create a new object and store the Java object.
   * @param verify_cb  global reference to the Java object
   */
  ReposVerifyCallback(jobject jverify_cb);

  ~ReposVerifyCallback();

  /**
   * Implementation of the svn_repos_verify_callback_t API.
   *
   * @param baton Notification instance is passed using this parameter
   * @param revision The revision that the error was emitted for
   * @param verify_err The emitted error
   * @param scratch_pool An APR pool from which to allocate memory.
   */
  static svn_error_t * callback(void *baton,
                                svn_revnum_t revision,
                                svn_error_t *verify_err,
                                apr_pool_t *scratch_pool);

  /**
   * Handler for Subversion notifications.
   */
  void onVerifyError(svn_revnum_t revision,
                     svn_error_t *verify_err,
                     apr_pool_t *scratch_pool);
};

#endif  // SVN_JAVAHL_REPOS_VERIFY_CALLBACK_H
