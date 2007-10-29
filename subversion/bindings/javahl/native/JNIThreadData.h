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

#ifndef JNITHREADDATA_H
#define JNITHREADDATA_H

#include <jni.h>
#include "JNIUtil.h"
struct apr_threadkey_t;
class Pool;

/**
 * This class implements thread local storage for JNIUtil.
 */
class JNIThreadData
{
 public:
  static void del(void *);
  static JNIThreadData *getThreadData();
  static bool initThreadData();
  static void pushNewThreadData();
  static void popThreadData();
  JNIThreadData();
  ~JNIThreadData();

  /**
   * The current JNI environment.
   */
  JNIEnv *m_env;

  /**
   * Flag that a Java execption has been detected.
   */
  bool m_exceptionThrown;

  /**
   * A buffer used for formating messages.
   */
  char m_formatBuffer[JNIUtil::formatBufferSize];

  /**
   * The pool for the current request (call).
   */
  Pool *m_requestPool;
 private:
  /**
   * Pointer to previous thread information to enable reentrent
   * calls.
   */
  JNIThreadData *m_previous;

  /**
   * The key to address this thread local storage.
   */
  static apr_threadkey_t *g_key;
};

#endif  // JNITHREADDATA_H
