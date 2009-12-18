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
 * @file StringArray.h
 * @brief Interface of the class StringArray
 */

#ifndef STRINGARRAY_H
#define STRINGARRAY_H

#include <jni.h>

struct apr_array_header_t;
struct svn_error_t;
class Pool;

#include "Path.h"
#include <vector>
#include <string>

class StringArray
{
 private:
  std::vector<std::string> m_strings;
  jobjectArray m_stringArray;
 public:
  StringArray(jobjectArray jstrings);
  ~StringArray();
  const apr_array_header_t *array(const Pool &pool);
};

#endif // STRINGARRAY_H
