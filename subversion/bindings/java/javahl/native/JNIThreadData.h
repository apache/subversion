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
 * @file JNIThreadData.h
 * @brief Interface of the class JNIData
 */

#if !defined(AFX_JNITHREADDATA_H__2BF37407_7EA8_4F74_9080_C86696617F7F__INCLUDED_)
#define AFX_JNITHREADDATA_H__2BF37407_7EA8_4F74_9080_C86696617F7F__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000
#include <jni.h>
#include "JNIUtil.h"
struct apr_threadkey_t;
class Pool;
/**
 * this class implements thread local storage for JNIUtil.
 */
class JNIThreadData
{
public:
    static void del(void *);
    static JNIThreadData * getThreadData();
    static bool initThreadData();
	static void pushNewThreadData();
	static void popThreadData();
    JNIThreadData();
    ~JNIThreadData();
    /**
     * the current JNI environment
     */
    JNIEnv *m_env;
    /**
     * flag that a java execption has been detected
     */
    bool m_exceptionThrown;
    /**
     * a buffer used for formating messages
     */
    char m_formatBuffer[JNIUtil::formatBufferSize];
    /**
     * the pool for the current request (call)
     */
    Pool *m_requestPool;
private:
	/**
	 *	pointer to previous thread information to enable reentrent calls
	 */
	JNIThreadData *m_previous;
    /**
     * the key to address this thread local storage
     */
    static apr_threadkey_t * g_key;
};
// !defined(AFX_JNITHREADDATA_H__2BF37407_7EA8_4F74_9080_C86696617F7F__INCLUDED_)
#endif 
