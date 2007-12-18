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

#ifndef JNISTRINGHOLDER_H
#define JNISTRINGHOLDER_H

#include <jni.h>
#include <apr_pools.h>

class JNIStringHolder
{
 public:
  JNIStringHolder(jstring jtext);
  ~JNIStringHolder();
  operator const char *() { return m_str; }
  const char *pstrdup(apr_pool_t *pool);

 protected:
  const char *m_str;
  JNIEnv *m_env;
  jstring m_jtext;
};

#endif  // JNISTRINGHOLDER_H
