/**
 * @copyright
 * ====================================================================
 * Copyright (c) 2003 CollabNet.  All rights reserved.
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
class JNIThreadData  
{
public:
	static void del(void *);
	static JNIThreadData * getThreadData();
	static bool initThreadData();
	JNIThreadData();
	~JNIThreadData();
	JNIEnv *m_env;
	bool m_exceptionThrown;
	char m_formatBuffer[JNIUtil::formatBufferSize];
	bool m_bInCatchEveryThing;
private:
	static apr_threadkey_t * g_key;
};

#endif // !defined(AFX_JNITHREADDATA_H__2BF37407_7EA8_4F74_9080_C86696617F7F__INCLUDED_)
