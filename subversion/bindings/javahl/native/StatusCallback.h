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
 * @file StatusCallback.h
 * @brief Interface of the class StatusCallback
 */

#ifndef STATUSCALLBACK_H
#define STATUSCALLBACK_H

#include <jni.h>
#include "svn_client.h"

/**
 * This class holds a Java callback object, each status item
 * for which the callback information is requested.
 */
class StatusCallback
{
 public:
  StatusCallback(jobject jcallback);
  ~StatusCallback();

  static svn_error_t* callback(void *baton,
                               const char *path,
                               svn_wc_status2_t *status,
                               apr_pool_t *pool);

 protected:
  svn_error_t *doStatus(const char *path, svn_wc_status2_t *status);

 private:
  /**
   * This a local reference to the Java object.
   */
  jobject m_callback;

  jobject createJavaStatus(const char *path,
                           svn_wc_status2_t *status);
};

#endif // STATUSCALLBACK_H
