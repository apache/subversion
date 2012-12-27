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
 */

#ifndef __cplusplus
#error "This is a C++ header file."
#endif

#ifndef SVN_CXXHL_EXCEPTION_HPP
#define SVN_CXXHL_EXCEPTION_HPP

#include <exception>

#include "svncxxhl/_compat.hpp"

namespace subversion {
namespace cxxhl {

namespace compat {} // Announce the compat namespace for shared_ptr lookup

namespace detail {
struct error_description;
} // namespace detail

namespace version_1_9_dev {

class error : public std::exception
{
public:
  typedef compat::shared_ptr<error> shared_ptr;

  error(const char* description, int errno);
  error(const char* description, int errno, shared_ptr nested);

  error(const error& that) throw();
  error& operator=(const error& that) throw();
  virtual ~error() throw();

  virtual int code() const throw() { return m_errno; }
  virtual const char* what() const throw();
  virtual shared_ptr nested() const throw() { return m_nested; }

private:
  int m_errno;                ///< The (SVN or APR) error code
  shared_ptr m_nested;        ///< Optional pointer to nessted error

  /// Error message; will be @c NULL if this is a trace link.
  detail::error_description* m_description;

  /**
   * The location of the error in @a m_loc_file at @a m_loc_line.
   *
   * @a m_loc_file will be @c NULL if the location is not available
   * (i.e., if the wrapped Subversion library was not compiled in
   * maintaner mode.
   */
  const char* m_loc_file;
  int m_loc_line;
};

class canceled : public error {};

} // namespace version_1_9_dev
} // namespace cxxhl
} // namespace subversion

#endif  // SVN_CXXHL_EXCEPTION_HPP
