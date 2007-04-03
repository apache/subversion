/**
 * @copyright
 * ====================================================================
 * Copyright (c) 2007 CollabNet.  All rights reserved.
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
 * @file StatusCallback.h
 * @brief Interface of the class StatusCallback
 */

#if !defined(AFX_STATUSCALLBACK_H__3BBF3F0F_C80C_45C9_8AC3_E1AF07E5B810__INCLUDED_)
#define AFX_STATUSCALLBACK_H__3BBF3F0F_C80C_45C9_8AC3_E1AF07E5B810__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000
#include <jni.h>
#include "svn_client.h"

/**
 * this class holds a java callback object, each status item
 * for which the callback information is requested.
 */
class StatusCallback
{
public:
    StatusCallback(jobject jcallback);
    ~StatusCallback();

    static void callback(void *baton,
                         const char *path,
                         svn_wc_status2_t *status);

protected:
    void doStatus(const char *path, svn_wc_status2_t *status);

private:
    /**
     * this a local reference to the java object.
     */
    jobject m_callback;

    jobject createJavaStatus(const char *path,
                             svn_wc_status2_t *status);
};
// !defined(AFX_STATUSCALLBACK_H__3BBF3F0F_C80C_45C9_8AC3_E1AF07E5B810__INCLUDED_)
#endif
