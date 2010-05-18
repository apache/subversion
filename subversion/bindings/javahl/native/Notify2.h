/**
 * @copyright
 * ====================================================================
 * Copyright (c) 2003-2006 CollabNet.  All rights reserved.
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
 * @file Notify2.h
 * @brief Interface of the class Notify2
 */

#ifndef NOTIFY2_H
#define NOTIFY2_H

#include <jni.h>
#include "svn_wc.h"

/**
 * This class passes notification from subversion to a Java object
 * (1.2 version).
 */
class Notify2
{
 private:
  /**
   * The Java object to receive the notifications.  This is a global
   * reference because it has to live longer than the
   * SVNClient.notification call.
   */
  jobject m_notify;
  Notify2(jobject p_notify);

 public:
  static Notify2 *makeCNotify(jobject notify);
  ~Notify2();

  /**
   * Implementation of the svn_wc_notify_func_t API.
   *
   * @param baton notification instance is passed using this parameter
   * @param notify all the information about the event
   * @param pool An APR pool from which to allocate memory.
   */
  static void notify(void *baton,
                     const svn_wc_notify_t *notify,
                     apr_pool_t *pool);

  /**
   * Handler for Subversion notifications.
   *
   * @param notify all the information about the event
   * @param pool An APR pool from which to allocate memory.
   */
  void onNotify(const svn_wc_notify_t *notify,
                apr_pool_t *pool);
};

#endif  // NOTIFY2_H
