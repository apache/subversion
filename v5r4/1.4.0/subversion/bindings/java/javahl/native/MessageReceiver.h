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
 * @file MessageReceiver.h
 * @brief Interface of the class MessageReceiver
 */
#if !defined(AFX_MESSAGERECEIVER_H__D30A4B70_A630_45B3_AB3E_402A5AD7E6BA__INCLUDED_)
#define AFX_MESSAGERECEIVER_H__D30A4B70_A630_45B3_AB3E_402A5AD7E6BA__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000
#include <jni.h>

/**
 * this class holds a SVNAdmin.MessageReceiver objects and sends messages to it
 */
class MessageReceiver
{
    /**
     * a local reference to the java object
     */
    jobject m_jthis;
public:
    MessageReceiver(jobject jthis);
    ~MessageReceiver();
    void receiveMessage(const char *message);

};
// !defined(AFX_MESSAGERECEIVER_H__D30A4B70_A630_45B3_AB3E_402A5AD7E6BA__INCLUDED_)
#endif
