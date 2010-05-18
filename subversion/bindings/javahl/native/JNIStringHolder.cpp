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
 * @file JNIStringHolder.cpp
 * @brief Implementation of the class JNIStringHolder
 */

#include <jni.h>
#include <apr_pools.h>
#include <apr_strings.h>
#include "JNIStringHolder.h"
#include "JNIUtil.h"

JNIStringHolder::JNIStringHolder(jstring jtext)
{
  if (jtext == NULL)
    {
      m_str = NULL;
      m_jtext = NULL;
      return;
    }
  m_str = JNIUtil::getEnv()->GetStringUTFChars(jtext, NULL);
  m_jtext = jtext;
  m_env = JNIUtil::getEnv();
}

JNIStringHolder::~JNIStringHolder()
{
  if (m_jtext && m_str)
    m_env->ReleaseStringUTFChars(m_jtext, m_str);
}

const char *
JNIStringHolder::pstrdup(apr_pool_t *pool)
{
  return (m_str ? apr_pstrdup(pool, m_str) : NULL);
}
