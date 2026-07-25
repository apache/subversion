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
 * @file Path.h
 * @brief Interface of the C++ class Path.
 */

#ifndef JAVAHL_PATH_H
#define JAVAHL_PATH_H

#include <string>
#include <jni.h>
#include "Pool.h"
struct svn_error_t;


/**
 * Encapsulation for Subversion Path handling
 */
class PathBase
{
 private:
  // The path to be stored.
  std::string m_path;

  svn_error_t *m_error_occurred;

  /**
   * Initialize the class.
   *
   * @param pi_path Path string
   */
  void init(const char *pi_path,
            svn_error_t* initfunc(const char*&, SVN::Pool&),
            SVN::Pool &in_pool);

 protected:
  /**
   * Constructor that takes a string as parameter.
   * The string is converted to subversion internal
   * representation. The string is copied.
   *
   * @param pi_path Path string
   */
  PathBase(const std::string &pi_path,
           svn_error_t* initfunc(const char*&, SVN::Pool&),
           SVN::Pool &in_pool);

  /**
   * Constructor
   *
   * @see PathBase::PathBase (const std::string &)
   * @param pi_path Path string
   */
  PathBase(const char *pi_path,
           svn_error_t* initfunc(const char*&, SVN::Pool&),
           SVN::Pool &in_pool);

  /**
   * Constructor from a Java string.
   */
  PathBase(jstring jpath,
           svn_error_t* initfunc(const char*&, SVN::Pool&),
           SVN::Pool &in_pool);

  /**
   * Assignment operator
   */
  PathBase &operator=(const PathBase&);

  /**
   * @return Path string
   */
  const std::string &path() const;

  /**
   * @return Path string as a C string
   */
  const char *c_str() const;

  svn_error_t *error_occurred() const;

public:
  /**
   * Returns whether @a path is non-NULL and passes the @c
   * svn_path_check_valid() test.
   *
   * @since 1.4.0
   */
  static jboolean isValid(const char *path);
};


/**
 * Dirent or URI
 */
class Path : protected PathBase
{
 public:
  Path(const std::string &pi_path, SVN::Pool &in_pool)
    : PathBase(pi_path, initfunc, in_pool)
    {}

  Path(const char *pi_path, SVN::Pool &in_pool)
    : PathBase(pi_path, initfunc, in_pool)
    {}

  Path(jstring jpath, SVN::Pool &in_pool)
    : PathBase(jpath, initfunc, in_pool)
    {}

  Path& operator=(const Path& that)
    {
      PathBase::operator=(that);
      return *this;
    }

  const std::string &path() const { return PathBase::path(); }
  const char *c_str() const { return PathBase::c_str(); }

  svn_error_t *error_occurred() const
    {
      return PathBase::error_occurred();
    }

 private:
  static svn_error_t* initfunc(const char*&, SVN::Pool&);
};

/**
 * URL
 */
class URL : protected PathBase
{
 public:
  URL(const std::string &pi_path, SVN::Pool &in_pool)
    : PathBase(pi_path, initfunc, in_pool)
    {}

  URL(const char *pi_path, SVN::Pool &in_pool)
    : PathBase(pi_path, initfunc, in_pool)
    {}

  URL(jstring jpath, SVN::Pool &in_pool)
    : PathBase(jpath, initfunc, in_pool)
    {}

  URL& operator=(const URL& that)
    {
      PathBase::operator=(that);
      return *this;
    }

  const std::string &path() const { return PathBase::path(); }
  const char *c_str() const { return PathBase::c_str(); }

  svn_error_t *error_occurred() const
    {
      return PathBase::error_occurred();
    }

 private:
  static svn_error_t* initfunc(const char*&, SVN::Pool&);
};

/**
 * Relative path
 */
class Relpath : protected PathBase
{
 public:
  Relpath(const std::string &pi_path, SVN::Pool &in_pool)
    : PathBase(pi_path, initfunc, in_pool)
    {}

  Relpath(const char *pi_path, SVN::Pool &in_pool)
    : PathBase(pi_path, initfunc, in_pool)
    {}

  Relpath(jstring jpath, SVN::Pool &in_pool)
    : PathBase(jpath, initfunc, in_pool)
    {}

  Relpath& operator=(const Relpath& that)
    {
      PathBase::operator=(that);
      return *this;
    }

  const std::string &path() const { return PathBase::path(); }
  const char *c_str() const { return PathBase::c_str(); }

  svn_error_t *error_occurred() const
    {
      return PathBase::error_occurred();
    }

 private:
  static svn_error_t* initfunc(const char*&, SVN::Pool&);
};

#endif  // JAVAHL_PATH_H
