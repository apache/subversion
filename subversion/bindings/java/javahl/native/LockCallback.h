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
 * @file LockCallback.h
 * @brief Interface of the class LockCallback
 */

#if !defined(AFX_LOCKCALLBACK_H__7822FA05_9877_4F02_B5B3_7D3426D7A23E__INCLUDED_)
#define AFX_LOCKCALLBACK_H__7822FA05_9877_4F02_B5B3_7D3426D7A23E__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000
#include <jni.h>

class LockCallback  
{
public:
    LockCallback(jobject jcallback);
    ~LockCallback();
    void callback(const char *path, bool doLock, jobject jLock);
private:
    /**
     * this a local reference to the java object.
     */
    jobject m_callback;

};

#endif // !defined(AFX_LOCKCALLBACK_H__7822FA05_9877_4F02_B5B3_7D3426D7A23E__INCLUDED_)
