/**
 * @copyright
 * ====================================================================
 * Copyright (c) 2003 QintSoft.  All rights reserved.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution.  The terms
 * are also available at http://subversion.tigris.org/license-1.html.
 * If newer versions of this license are posted there, you may use a
 * newer version instead, at your option.
 *
 * This software consists of voluntary contributions made by many
 * individuals.  For exact contribution history, see the revision
 * history and logs, available at http://svnup.tigris.org/.
 * ====================================================================
 * @endcopyright
 *
 * @file JNIThreadData.cpp
 * @brief Implementation of the class JNIThreadData
 */

#include "JNIThreadData.h"
#include <apr_strings.h>
#include <apr_tables.h>
#include <apr_general.h>
#include <apr_lib.h>
#include <apr_thread_proc.h>
#include "JNIUtil.h"

apr_threadkey_t *JNIThreadData::g_key;

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

JNIThreadData::JNIThreadData()
{
	m_env = NULL;
	m_exceptionThrown = false;
	m_bInCatchEveryThing = false;
}

JNIThreadData::~JNIThreadData()
{

}

bool JNIThreadData::initThreadData()
{
	if(g_key != NULL) 
	{
		return false;
	}

	apr_status_t apr_err = apr_threadkey_private_create  ( &g_key, del, JNIUtil::getPool()); 
	if(apr_err)
	{
		JNIUtil::handleAPRError(apr_err, "apr_threadkey_private_create");
		return false;
	}

	return true;
}

JNIThreadData * JNIThreadData::getThreadData()
{
	if(g_key == NULL) 
	{
		return NULL;
	}
	JNIThreadData *data = NULL;
	apr_status_t apr_err = apr_threadkey_private_get (reinterpret_cast<void**>(&data), g_key);
	if(apr_err)
	{
		JNIUtil::handleAPRError(apr_err, "apr_threadkey_private_get");
		return NULL;
	}
	if(data == NULL)
	{
		data = new JNIThreadData;
		apr_err = apr_threadkey_private_set (data, g_key);
		if(apr_err)
		{
			JNIUtil::handleAPRError(apr_err, "apr_threadkey_private_set");
			return NULL;
		}
	}
	return data;
}

void JNIThreadData::del(void *p)
{
	delete reinterpret_cast<JNIThreadData*>(p);
}
