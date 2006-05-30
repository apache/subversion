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

#if !defined(AFX_NOTIFY_H__10E278E8_EA8C_4BD1_AF10_4DB1C0608F65__INCLUDED_)
#define AFX_NOTIFY_H__10E278E8_EA8C_4BD1_AF10_4DB1C0608F65__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000
#include <jni.h>
#include "svn_wc.h"
/**
 *  this class passes notification from subversion to a java object
 */
class Notify
{
private:
    /**
     * the java object to receive the notifications. This is a global reference
     * because it has to live longer than the SVNClient.notification call
     */ 
    jobject m_notify;
    Notify(jobject p_notify);
public:
    static Notify * makeCNotify(jobject notify);
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
// !defined(AFX_NOTIFY_H__10E278E8_EA8C_4BD1_AF10_4DB1C0608F65__INCLUDED_)
#endif
