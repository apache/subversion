/**
 * @copyright
 * ====================================================================
 * Copyright (c) 2003-2006 CollabNet.  All rights reserved.
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
 * @file Path.cpp
 * @brief Implementation of the class Path
 */

#include <jni.h>
#include "Path.h"
#include "svn_path.h"
#include "JNIUtil.h"
#include "Pool.h"

/**
 * Constructor
 *
 * @see Path::Path(const std::string &)
 * @param path Path string
 */
Path::Path(const char *pi_path)
{
  init(pi_path);
}

/**
 * Constructor that takes a string as parameter.  The string is
 * converted to subversion internal representation. The string is
 * copied.
 *
 * @param path Path string
 */
Path::Path(const std::string &pi_path)
{
  init(pi_path.c_str());
}

/**
 * Copy constructor
 *
 * @param path Path to be copied
 */
Path::Path(const Path &pi_path)
{
  init(pi_path.c_str());
}

/**
 * initialize the class
 *
 * @param path Path string
 */
void
Path::init(const char *pi_path)
{
  if (*pi_path == 0)
    {
      m_error_occured = NULL;
      m_path = "";
    }
  else
    {
      m_error_occured =
        JNIUtil::preprocessPath(pi_path,
                                JNIUtil::getRequestPool()->pool() );

      m_path = pi_path;
    }
}

/**
 * @return Path string
 */
const std::string &
Path::path() const
{
  return m_path;
}

/**
 * @return Path string as a C string
 */
const char *
Path::c_str() const
{
  return m_path.c_str();
}

/**
 * Assignment operator
 */
Path&
Path::operator=(const Path &pi_path)
{
  init(pi_path.c_str());
  return *this;
}

  svn_error_t *Path::error_occured() const
{
  return m_error_occured;
}

jboolean Path::isValid(const char *p)
{
  if (p == NULL)
    return JNI_FALSE;

  Pool requestPool;
  svn_error_t *err = svn_path_check_valid(p, requestPool.pool());
  if (err == SVN_NO_ERROR)
    {
      return JNI_TRUE;
    }
  else
    {
      svn_error_clear(err);
      return JNI_FALSE;
    }
}
