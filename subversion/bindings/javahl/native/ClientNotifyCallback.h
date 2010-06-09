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
 * @file ClientNotifyCallback.h
 * @brief Interface of the class ClientNotifyCallback
 */

#ifndef CLIENTNOTIFYCALLBACK_H
#define CLIENTNOTIFYCALLBACK_H

#include <jni.h>
#include "svn_wc.h"

/**
 * This class passes notification from subversion to a Java object
 * (1.2 version).
 */
class ClientNotifyCallback
{
 private:
  /**
   * The Java object to receive the notifications.  This is a global
   * reference because it has to live longer than the
   * SVNClient.notification call.
   */
  jobject m_notify;
  ClientNotifyCallback(jobject p_notify);

 public:
  static ClientNotifyCallback *makeCNotify(jobject notify);
  ~ClientNotifyCallback();

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

#endif  // CLIENTNOTIFYCALLBACK_H
