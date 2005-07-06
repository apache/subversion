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
 * @file JNIStackElement.h
 * @brief Interface of the class JNIStackElement
 */

#if !defined(AFX_JNISTACKELEMENT_H__81945F80_D56F_4782_B8E7_6A82483E6463__INCLUDED_)
#define AFX_JNISTACKELEMENT_H__81945F80_D56F_4782_B8E7_6A82483E6463__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000
#include <jni.h>
#include "JNIUtil.h"

/**
 * JNIEntry create a stack element on the stack, which will be used to track
 * the entry and exit of methods
 */ 
#define JNIEntry(c,m) JNIStackElement se(env, #c, #m, jthis);
/**
 * JNIEntry create a stack element on the stack, which will be used to track
 * the entry and exit of static methods
 */ 
#define JNIEntryStatic(c,m) JNIStackElement se(env, #c, #m, jclazz);

/**
 * This class is used to mark the entry and exists of methods. It can generate
 * a log message on entry and exits. The members are used to generate the exit
 * message
 */
class JNIStackElement
{
public:
    JNIStackElement(JNIEnv *env, const char *clazz,
                        const char *method, jobject jthis);
    virtual ~JNIStackElement();

private:
    /**
     * the name of the method
     */
    const char * m_method;
    /**
     * the name of the clazz
     */
    const char * m_clazz;
    /**
     * a buffer for the result for jthis.toString to identify the object
     */
    char m_objectID[JNIUtil::formatBufferSize];
};
// !defined(AFX_JNISTACKELEMENT_H__81945F80_D56F_4782_B8E7_6A82483E6463__INCLUDED_)
#endif 
