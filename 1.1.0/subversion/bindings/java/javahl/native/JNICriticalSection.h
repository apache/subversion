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
 * @file JNICriticalSection.h
 * @brief Interface of the class JNICriticalSection
 */

#if !defined(AFX_JNICRITICALSECTION_H__601F33CC_3378_4B09_9917_6242857EF2B7__INCLUDED_)
#define AFX_JNICRITICALSECTION_H__601F33CC_3378_4B09_9917_6242857EF2B7__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000
class JNIMutex;
/**
 * This class holds a mutex which will be locked during the constructor and
 * released during the destructor. If the object is created on the stack, this 
 * garanties that the mutex will be released all the time if the block is left.
 * Only one thread can enter all the critrical sections secured by the same 
 * mutex.
 */
class JNICriticalSection
{
public:
    JNICriticalSection(JNIMutex &mutex);
    ~JNICriticalSection();
private:
    /**
     * the mutex to be locked and released
     */
    JNIMutex * m_mutex;
};
// !defined(AFX_JNICRITICALSECTION_H__601F33CC_3378_4B09_9917_6242857EF2B7__INCLUDED_)
#endif 
