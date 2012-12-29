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
#include <string>
#include <utility>
#include <vector>

#include "svncxxhl/_compat.hpp"

// Forward declaration of implementation-specific structure
struct svn_error_t;

namespace subversion {
namespace cxxhl {

namespace compat {} // Announce the compat namespace for shared_ptr lookup

namespace detail {
// Forward declaration of implementation-specific structure
class error_description;
} // namespace detail

namespace version_1_9_dev {

class error : public std::exception
{
public:
  typedef compat::shared_ptr<error> shared_ptr;

  error(const char* description, int error_code);
  error(const char* description, int error_code, shared_ptr nested_error);

  error(const error& that) throw();
  error& operator=(const error& that) throw();
  virtual ~error() throw();

  /**
   * Returns the error code associated with the exception.
   */
  virtual int code() const throw() { return m_errno; }

  /**
   * Returns a shared pointer to the nested exception object, if any.
   */
  virtual shared_ptr nested() const throw() { return m_nested; }

  /// Returns the message associated with this exception object.
  virtual const char* what() const throw();

  /**
   * Error message description.
   *
   * The first element of this pair is the error code, the second the
   * associated error message. If the error code is 0, the message
   * describes the location in the source code where the error was
   * generated from.
   */
  typedef std::pair<int, std::string> message;

  /**
   * The list of messages associated with an error.
   */
  typedef std::vector<message> message_list;

  /**
   * Returns the complete list of error messages, including those from
   * nested exceptions.
   */
  virtual message_list messages() const
    {
      return compile_messages(false);
    }

  /**
   * Like error::messages(), but includes debugging traceback.
   *
   * @note
   * Traceback is only available if the Subversion libraries were
   * compiled with tracing enabled.
   */
  virtual message_list traced_messages() const
    {
      return compile_messages(true);
    }

public:
  /** Used internally by the implementation. */
  static void throw_svn_error(svn_error_t*);

protected:
  error(int error_code, detail::error_description* description) throw();

private:
  std::vector<message> compile_messages(bool show_traces) const;

  int m_errno;                /**< The (SVN or APR) error code. */
  shared_ptr m_nested;        /**< Optional pointer to nessted error. */
  /** Error description and trace location information. */
  detail::error_description* m_description;
};

class cancelled : public error
{
  friend void error::throw_svn_error(svn_error_t*);

protected:
  cancelled(int error_code, detail::error_description* description) throw()
    : error(error_code, description)
    {}
};

} // namespace version_1_9_dev
} // namespace cxxhl
} // namespace subversion

#endif  // SVN_CXXHL_EXCEPTION_HPP
