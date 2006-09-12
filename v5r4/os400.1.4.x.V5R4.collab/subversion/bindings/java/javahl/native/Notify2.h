/**
 * @copyright
 * ====================================================================
 * Copyright (c) 2003-2005 CollabNet.  All rights reserved.
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

#if !defined(AFX_NOTIFY2_H__10E278E8_EA8C_4BD1_AF10_4DB1C0608F65__INCLUDED_)
#define AFX_NOTIFY2_H__10E278E8_EA8C_4BD1_AF10_4DB1C0608F65__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000
#include <jni.h>
#include "svn_wc.h"
/**
 *  this class passes notification from subversion to a java object
 *  (1.2 version)
 */
class Notify2
{
private:
    /**
     * the java object to receive the notifications. This is a global reference
     * because it has to live longer than the SVNClient.notification call
     */
    jobject m_notify;
    Notify2(jobject p_notify);
public:
    static Notify2 * makeCNotify(jobject notify);
    ~Notify2();
  /**
   * notification function passed as svn_wc_notify_func_t
   * @param baton notification instance is passed using this parameter
   * @param notify all the information about the event
   * @param pool an apr pool to allocated memory
   */
    static void notify(void *baton,
                       const svn_wc_notify_t *notify,
                       apr_pool_t *pool);
  /**
   * Handler for Subversion notifications.
   *
   * @param notify all the information about the event
   * @param pool an apr pool to allocated memory
   */
    void onNotify(const svn_wc_notify_t *notify,
                  apr_pool_t *pool);

};
// !defined(AFX_NOTIFY2_H__10E278E8_EA8C_4BD1_AF10_4DB1C0608F65__INCLUDED_)
#endif
