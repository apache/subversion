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
 * @file JNIStringHolder.h
 * @brief Interface of the class JNIStringHolder
 */

#if !defined(AFX_JNISTRINGHOLDER_H__66F98D96_0371_471D_9E5C_EE5D45954C75__INCLUDED_)
#define AFX_JNISTRINGHOLDER_H__66F98D96_0371_471D_9E5C_EE5D45954C75__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

class JNIStringHolder
{
public:
    JNIStringHolder(jstring jtext);
    ~JNIStringHolder();
    operator const char *() { return m_str; }
protected:
    const char *m_str;
    JNIEnv *m_env;
    jstring m_jtext;
};

#endif // !defined(AFX_JNISTRINGHOLDER_H__66F98D96_0371_471D_9E5C_EE5D45954C75__INCLUDED_)
