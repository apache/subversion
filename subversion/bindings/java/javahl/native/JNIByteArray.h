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
 */
// JNIByteArray.h: interface for the JNIByteArray class.
//
//////////////////////////////////////////////////////////////////////

#if !defined(AFX_JNIBYTEARRAY_H__FB74054F_CD5E_41D5_A4B0_25DE9A8574CF__INCLUDED_)
#define AFX_JNIBYTEARRAY_H__FB74054F_CD5E_41D5_A4B0_25DE9A8574CF__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

#include <jni.h>

class JNIByteArray
{
private:
	jbyteArray m_array;
	jbyte *m_data;
public:
	const signed char * getBytes();
	int getLength();
	JNIByteArray(jbyteArray jba);
	~JNIByteArray();

};

#endif // !defined(AFX_JNIBYTEARRAY_H__FB74054F_CD5E_41D5_A4B0_25DE9A8574CF__INCLUDED_)
