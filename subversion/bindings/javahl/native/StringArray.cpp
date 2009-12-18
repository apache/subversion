/**
 * @copyright
 * ====================================================================
 * Copyright (c) 2008 CollabNet.  All rights reserved.
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
 * @file StringArray.cpp
 * @brief Implementation of the class StringArray
 */

#include "StringArray.h"
#include "Pool.h"
#include "JNIUtil.h"
#include "JNIStringHolder.h"
#include <apr_tables.h>
#include <apr_strings.h>
#include "svn_path.h"
#include <iostream>

StringArray::~StringArray()
{
  if (m_stringArray != NULL)
    JNIUtil::getEnv()->DeleteLocalRef(m_stringArray);
}

const apr_array_header_t *StringArray::array(const Pool &pool)
{
  apr_array_header_t *strings
    = apr_array_make(pool.pool(), m_strings.size(), sizeof(char *));

  std::vector<std::string>::const_iterator it;
  for (it = m_strings.begin(); it < m_strings.end(); ++it)
    {
      APR_ARRAY_PUSH(strings, const char *) = it->c_str();
      if (JNIUtil::isExceptionThrown())
        return NULL;
    }

  return strings;
}

StringArray::StringArray(jobjectArray jstrings)
{
  m_stringArray = jstrings;

  if (jstrings != NULL)
    {
      JNIEnv *env = JNIUtil::getEnv();
      jint arraySize = env->GetArrayLength(jstrings);
      if (JNIUtil::isExceptionThrown())
        return;

      for (int i = 0; i < arraySize; ++i)
        {
          jobject jstr = env->GetObjectArrayElement(jstrings, i);
          if (JNIUtil::isExceptionThrown())
            return;

          JNIStringHolder str((jstring)jstr);
          if (JNIUtil::isExceptionThrown())
            return;

          m_strings.push_back(std::string((const char *)str));
        }
    }
}
