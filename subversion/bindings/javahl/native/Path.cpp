/**
 * @copyright
 * ====================================================================
 *    Licensed to the Apache Software Foundation (ASF) under one
 *    or more contributor license agreements.  See the NOTICE file
 *    distributed with this work for additional information
 *    regarding copyright ownership.  The ASF licenses this file
 *    to you under the Apache License, Version 2.0 (the
 *    "License"); you may not use this file except in compliance
 *    with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing,
 *    software distributed under the License is distributed on an
 *    "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 *    KIND, either express or implied.  See the License for the
 *    specific language governing permissions and limitations
 *    under the License.
 * ====================================================================
 * @endcopyright
 *
 * @file Path.cpp
 * @brief Implementation of the class Path
 */

#include <jni.h>
#include "Path.h"
#include "svn_path.h"
#include "svn_dirent_uri.h"
#include "JNIUtil.h"
#include "JNIStringHolder.h"
#include "Pool.h"
#include "svn_private_config.h"

/**
 * Constructor
 *
 * @see PathBase::PathBase(const std::string &)
 * @param path Path string
 */
PathBase::PathBase(const char *pi_path,
                   svn_error_t* initfunc(const char*&, SVN::Pool&),
                   SVN::Pool &in_pool)
  : m_error_occurred(NULL)
{
  init(pi_path, initfunc, in_pool);
}

/**
 * Constructor that takes a string as parameter.  The string is
 * converted to subversion internal representation. The string is
 * copied.
 *
 * @param path Path string
 */
PathBase::PathBase(const std::string &pi_path,
                   svn_error_t* initfunc(const char*&, SVN::Pool&),
                   SVN::Pool &in_pool)
  : m_error_occurred(NULL)
{
  init(pi_path.c_str(), initfunc, in_pool);
}

/**
 * Constructor from a Java string.
 */
PathBase::PathBase(jstring jpath,
                   svn_error_t* initfunc(const char*&, SVN::Pool&),
                   SVN::Pool &in_pool)
  : m_error_occurred(NULL)
{
  JNIStringHolder path(jpath);
  if (JNIUtil::isJavaExceptionThrown())
    return;
  init(path, initfunc, in_pool);
}

/**
 * initialize the class
 *
 * @param path Path string
 */
void
PathBase::init(const char *pi_path,
               svn_error_t* initfunc(const char*&, SVN::Pool&),
               SVN::Pool &in_pool)
{
  if (pi_path && *pi_path)
    {
      m_error_occurred = initfunc(pi_path, in_pool);
      m_path = pi_path;
    }
}

/**
 * @return Path string
 */
const std::string &
PathBase::path() const
{
  return m_path;
}

/**
 * @return Path string as a C string
 */
const char *
PathBase::c_str() const
{
  return m_path.c_str();
}

/**
 * Assignment operator
 */
PathBase&
PathBase::operator=(const PathBase &pi_path)
{
  m_error_occurred = NULL;
  m_path = pi_path.m_path;

  return *this;
}

svn_error_t *PathBase::error_occurred() const
{
  return m_error_occurred;
}

jboolean PathBase::isValid(const char *p)
{
  if (p == NULL)
    return JNI_FALSE;

  SVN::Pool requestPool;
  svn_error_t *err = svn_path_check_valid(p, requestPool.getPool());
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

svn_error_t*
Path::initfunc(const char*& path, SVN::Pool& pool)
{
  return JNIUtil::preprocessPath(path, pool.getPool());
}

svn_error_t*
URL::initfunc(const char*& path, SVN::Pool& pool)
{
  if (svn_path_is_url(path))
    return JNIUtil::preprocessPath(path, pool.getPool());
  return svn_error_createf(SVN_ERR_BAD_URL, NULL,
                           _("Not an URL: %s"), path);
}

svn_error_t*
Relpath::initfunc(const char*& path, SVN::Pool& pool)
{
  path = svn_relpath__internal_style(path, pool.getPool());
  return SVN_NO_ERROR;
}
