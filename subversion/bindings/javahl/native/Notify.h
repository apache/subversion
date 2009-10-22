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
 * @file Notify.h
 * @brief Interface of the class Notify
 */

#ifndef NOTIFY_H
#define NOTIFY_H

#include <jni.h>
#include "svn_wc.h"

/**
 * This class passes notification from Subversion to a Java object.
 */
class Notify
{
 private:
  /**
   * The Java object to receive the notifications.  This is a global
   * reference because it has to live longer than the
   * SVNClient.notification call.
   */
  jobject m_notify;
  Notify(jobject p_notify);
 public:
  static Notify *makeCNotify(jobject notify);
  ~Notify();

  /**
   * notification function passed as svn_wc_notify_func_t
   * @param baton notification instance is passed using this parameter
   * @param path on which action happen
   * @param action subversion action, see svn_wc_notify_action_t
   * @param kind node kind of path after action occurred
   * @param mime_type mime type of path after action occurred
   * @param content_state state of content after action occurred
   * @param prop_state state of properties after action occurred
   * @param revision revision number after action occurred
   */
  static void notify(void *baton,
                     const char *path,
                     svn_wc_notify_action_t action,
                     svn_node_kind_t kind,
                     const char *mime_type,
                     svn_wc_notify_state_t content_state,
                     svn_wc_notify_state_t prop_state,
                     svn_revnum_t revision);

  /**
   * Handler for Subversion notifications.
   *
   * @param path on which action happen
   * @param action subversion action, see svn_wc_notify_action_t
   * @param kind node kind of path after action occurred
   * @param mime_type mime type of path after action occurred
   * @param content_state state of content after action occurred
   * @param prop_state state of properties after action occurred
   * @param revision revision number  after action occurred
   */
  void onNotify(const char *path,
                svn_wc_notify_action_t action,
                svn_node_kind_t kind,
                const char *mime_type,
                svn_wc_notify_state_t content_state,
                svn_wc_notify_state_t prop_state,
                svn_revnum_t revision);
};

#endif  // NOTIFY_H
