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
 */
// JNIByteArray.cpp: implementation of the JNIByteArray class.
//
//////////////////////////////////////////////////////////////////////

#include "JNIByteArray.h"
#include "JNIUtil.h"
//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

JNIByteArray::JNIByteArray(jbyteArray jba)
{
	m_array = jba;
	if(jba != NULL)
	{
		JNIEnv *env = JNIUtil::getEnv();
		m_data = env->GetByteArrayElements(jba, NULL);
	}
	else
	{
		m_data = NULL;
	}
}

JNIByteArray::~JNIByteArray()
{
	if(m_array != NULL)
	{
		JNIUtil::getEnv()->ReleaseByteArrayElements(m_array, m_data, JNI_ABORT);
	}
}

int JNIByteArray::getLength()
{
	if(m_data == NULL)
	{
		return 0;
	}
	else
	{
		return JNIUtil::getEnv()->GetArrayLength(m_array);
	}
}

const signed char * JNIByteArray::getBytes()
{
	return m_data;
}
