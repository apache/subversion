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
 * @file JNICriticalSection.cpp
 * @brief Implementation of the class JNICriticalSection
 */

#include "JNICriticalSection.h"
#include <apr_strings.h>
#include <apr_tables.h>
#include <apr_general.h>
#include <apr_lib.h>
#include "JNIUtil.h"
#include "JNIMutex.h"

/**
 * Create the critical section and lock the mutex
 * @param mutext    the underlying mutex
 */
JNICriticalSection::JNICriticalSection(JNIMutex &mutex)
{
  m_mutex = &mutex;
  apr_status_t apr_err = apr_thread_mutex_lock (mutex.m_mutex);
  if (apr_err)
    {
      JNIUtil::handleAPRError(apr_err, "apr_thread_mutex_lock");
      return;
    }

}

/**
 * Release the mutex and the destroy the critical section.
 */
JNICriticalSection::~JNICriticalSection()
{
  apr_status_t apr_err = apr_thread_mutex_unlock (m_mutex->m_mutex);
  if (apr_err)
    {
      JNIUtil::handleAPRError(apr_err, "apr_thread_mutex_unlock");
      return;
    }
}
