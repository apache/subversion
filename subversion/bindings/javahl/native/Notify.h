/**
 * @copyright
 * ====================================================================
 * Copyright (c) 2003-2004 CollabNet.  All rights reserved.
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
