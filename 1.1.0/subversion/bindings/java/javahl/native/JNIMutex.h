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
 * @file JNIMutex.h
 * @brief Interface of the class JNIMutex
 */

#if !defined(AFX_JNIMUTEX_H__958B52A6_00A3_458F_981F_0A3095D39EE8__INCLUDED_)
#define AFX_JNIMUTEX_H__958B52A6_00A3_458F_981F_0A3095D39EE8__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000
class JNICriticalSection;
struct apr_pool_t;
struct apr_thread_mutex_t;
/**
 * This class holds a apr mutex for the use of JNICriticalSection
 */
class JNIMutex
{
public:
    JNIMutex(apr_pool_t *pool);
    ~JNIMutex();
    friend class JNICriticalSection;
private:
    /**
     * the apr mutex
     */
    apr_thread_mutex_t * m_mutex;
};
// !defined(AFX_JNIMUTEX_H__958B52A6_00A3_458F_981F_0A3095D39EE8__INCLUDED_)
#endif
