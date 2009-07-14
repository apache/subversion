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
 * @file ProgressListener.h
 * @brief Interface of the class ProgressListener
 */

#ifndef PROGRESSLISTENER_H
#define PROGRESSLISTENER_H

#include <jni.h>
#include "svn_wc.h"

/**
 * This class passes progress events from Subversion to a Java object.
 * @since 1.5
 */
class ProgressListener
{
 private:
  /**
   * The Java object which handles the progress event. This is a
   * global reference, because it has to live longer than the
   * ProgressListener.onProgress() call.
   */
  jobject m_progressListener;

  /**
   * Create a new instance, storing a global reference to the
   * corresponding Java object.
   *
   * @param jprogressListener Reference to the Java peer.
   */
  ProgressListener(jobject jprogressListener);

 public:
  /**
   * Destroy the instance, and delete the global reference to the
   * Java object.
   */
  ~ProgressListener();

  /** Constructor function called from C JNI glue code. */
  static ProgressListener *makeCProgressListener(jobject jprogressListener);

  /**
   * Implementation of the svn_ra_progress_notify_func_t API.
   *
   * @param nbrBytes The number of bytes already transferred.
   * @param total The total number of bytes.
   * @param baton A reference to the ProgressListener instance.
   * @param pool An APR pool from which to allocate memory.
   */
  static void progress(apr_off_t nbrBytes,
                       apr_off_t total,
                       void *baton,
                       apr_pool_t *pool);

 protected:
  /**
   * Handler for Subversion progress events.
   *
   * @param progress The number of bytes already transferred.
   * @param total The total number of bytes.
   * @param pool An APR pool from which to allocate memory.
   */
  void onProgress(apr_off_t progress,
                  apr_off_t total,
                  apr_pool_t *pool);
};

#endif // PROGRESSLISTENER_H
