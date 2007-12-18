/**
 * @copyright
 * ====================================================================
 * Copyright (c) 2006 CollabNet.  All rights reserved.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution.  The terms
 * are also available at http://subversion.tigris.org/license-1.html.
 * If newer versions of this license are posted there, you may use a
 * newer version instead, at your option.
 *
 * This software consists of voluntary contributions made by many
 * individuals.  For exact contribution history, see the revision
 * history and logs, available at http://subversion.tigris.org/.
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
