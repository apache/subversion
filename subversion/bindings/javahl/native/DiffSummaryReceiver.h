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
 * @file DiffSummaryReceiver.h
 * @brief Interface of the class DiffSummaryReceiver
 */

#ifndef DIFFSUMMARYRECEIVER_H
#define DIFFSUMMARYRECEIVER_H

#include <jni.h>
#include "svn_client.h"

/**
 * A diff summary receiver callback.
 */
class DiffSummaryReceiver
{
 public:
  /**
   * Create a DiffSummaryReceiver object.
   * @param jreceiver The Java callback object.
   */
  DiffSummaryReceiver(jobject jreceiver);

  /**
   * Destroy a DiffSummaryReceiver object
   */
  ~DiffSummaryReceiver();

  /**
   * Implementation of the svn_client_diff_summarize_func_t API.
   *
   * @param diff The diff summary.
   * @param baton A reference to the DiffSummaryReceiver instance.
   * @param pool An APR pool from which to allocate memory.
   */
  static svn_error_t *summarize(const svn_client_diff_summarize_t *diff,
                                void *baton,
                                apr_pool_t *pool);

 protected:
  /**
   * Callback invoked for every diff summary.
   *
   * @param diff The diff summary.
   * @param pool An APR pool from which to allocate memory.
   */
  svn_error_t *onSummary(const svn_client_diff_summarize_t *diff,
                         apr_pool_t *pool);

 private:
  /**
   * A local reference to the Java DiffSummaryReceiver peer.
   */
  jobject m_receiver;
};

#endif  // DIFFSUMMARYRECEIVER_H
