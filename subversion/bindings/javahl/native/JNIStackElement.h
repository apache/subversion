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

#ifndef JNISTACKELEMENT_H
#define JNISTACKELEMENT_H

#include <jni.h>
#include "JNIUtil.h"

/**
 * Create a stack element on the stack, which will be used to track
 * the entry and exit of a method.  Assumes that there are a local
 * variables named "env" and "jthis" available.
 */
#define JNIEntry(c,m) JNIStackElement se(env, #c, #m, jthis);

/**
 * Create a stack element on the stack, which will be used to track
 * the entry and exit of a static method.  Assumes that there are a
 * local variables named "env" and "jthis" available.
 */
#define JNIEntryStatic(c,m) JNIStackElement se(env, #c, #m, jclazz);


/**
 * This class is used to mark the entry and exit of a method, and can
 * generate a log messages at those points.  The members are used to
 * generate the exit message
 */
class JNIStackElement
{
 public:
  JNIStackElement(JNIEnv *env, const char *clazz,
                  const char *method, jobject jthis);
  virtual ~JNIStackElement();

 private:
  /**
   * The name of the method.
   */
  const char *m_method;

  /**
   * The name of the class.
   */

  const char *m_clazz;

  /**
   * A buffer for the result for jthis.toString to identify the
   * object.
   */
  char m_objectID[JNIUtil::formatBufferSize];
};

#endif // JNISTACKELEMENT_H
