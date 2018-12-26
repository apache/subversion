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

#ifndef SVNXX_EXCEPTION_HPP
#define SVNXX_EXCEPTION_HPP

#include <exception>
#include <stdexcept>
#include <memory>
#include <string>
#include <utility>
#include <vector>

/**
 * @defgroup svnxx_exceptions Exceptions
 *
 * Exceptions in SVN++
 * ===================
 *
 * SVN++ uses exceptions for the following purposes:
 * @li Reporting memory allocation failure; where Subversion's
 *     default hehaviour is to abort when an allocation from an APR
 *     pool fails, SVN++ throws an exception instead.
 * @li Reporting errors; Subversion's error messages are wrapped in
 *     exceptions.
 * @li Reporting cancelled operations; an operation that was
 *     cancelled from user code will report this by throwing a
 *     specific exception type.
 * @li Terminating iteration; user-level callbacks may throw a
 *     specific exception type to cancel an ongoing operation that
 *     is generating the callback messages. Other exceptions from
 *     user-level callbacks will be propagated back to the calling
 *     application.
 *
 * Exception Hierarchy
 * -------------------
 *
 * All SVN++ exceptions are ultimately derived from @c std:::exception.
 *
 * * <em>std::exception</em>
 *   + <em>std::runtime_error</em>
 *     - apache::subversion::svnxx::allocation_failed\n
 *       Thrown when memory cannot be allocated from an APR pool
 *   + apache::subversion::svnxx::error\n
 *     Thrown when an operation failed (see @ref svn_error_t)
 *     - apache::subversion::svnxx::canceled\n
 *       Thrown when an operation was canceled
 *   + apache::subversion::svnxx::cancel\n
 *     Thrown by user callbacks to terminate iteration
 *
 * @{
 */

namespace apache {
namespace subversion {
namespace svnxx {

/**
 * @brief Exception type that will be thrown when memory allocation fails.
 */
class allocation_failed : public std::runtime_error
{
public:
  explicit allocation_failed(const std::string& what_arg)
    : std::runtime_error(what_arg)
    {}

  explicit allocation_failed(const char* what_arg)
    : std::runtime_error(what_arg)
    {}

  virtual ~allocation_failed() {}
};

namespace detail {
struct wrapped_error;
using error_ptr = std::shared_ptr<wrapped_error>;
} // namespace detail

/**
 * Encapsulate a stack of Subversion error codes and messages.
 */
class error : public std::exception,
              protected detail::error_ptr
{
public:
  virtual ~error() {}

  /**
   * Returns the message associated with the top-level error that
   * caused the exception.
   *
   * @note the returned string is valid onlu as long as the @c error
   *       object is in scope.
   */
  virtual const char* what() const noexcept;

  /**
   * Returns the error code associated with the top-level error that
   * caused the exception.
   */
  virtual int code() const noexcept;

  /**
   * Returns the symbolic name of the error code associated with the
   * top-level error that caused the exception.
   * @note The returned value may be @c nullptr.
   */
  virtual const char* name() const noexcept;

  /**
   * Error message description.
   */
  class message
  {
  public:
    /**
     * Return the error code.
     */
    int code() const noexcept { return m_errno; }

    /**
     * Return the error name.
     * @note The returned value may be @c nullptr.
     */
    const char* name() const noexcept { return m_errname; }

    /**
     * Return the error message.
     */
    const std::string& text() const noexcept { return m_message; }

    /**
     * Return the generic error message associated with the error code.
     */
    std::string generic_text() const;

    /**
     * Check if this message is in fact a debugging traceback entry.
     */
    bool trace() const noexcept { return m_trace; }

  protected:
    message(int errval, const char* errname,
            const std::string& message, bool trace)
      : m_errno(errval),
        m_errname(errname),
        m_message(message),
        m_trace(trace)
      {}

  private:
    int m_errno;
    const char* m_errname;
    std::string m_message;
    bool m_trace;
  };

  /**
   * Returns the complete list of error messages, including those from
   * nested errors.
   */
  virtual std::vector<message> messages() const
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
  virtual std::vector<message> traced_messages() const
    {
      return compile_messages(true);
    }

protected:
  error(detail::error_ptr svn_error);
  const char* const m_message;
  std::vector<message> compile_messages(bool show_traces) const;
};

/**
 * Thrown instead of Error when the error chain contains a
 * @c SVN_ERR_CANCELLED error code.
 */
class canceled : public error
{
public:
  virtual ~canceled() {}

protected:
  canceled(detail::error_ptr svn_error)
    : error(svn_error)
    {}
};

/**
 * User code should throw this exception from callbacks to
 * cancel an operation.
 */
class cancel : public std::exception
{
public:
  cancel() {}
  virtual ~cancel() {}
  virtual const char* what() const noexcept;
};

} // namespace svnxx
} // namespace subversion
} // namespace apache

/**
 * @}
 */
#endif  // SVNXX_EXCEPTION_HPP
