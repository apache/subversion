/**
 * @file svnxx/exception.hpp
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

#include "svn_types_impl.h"

#include <exception>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <vector>

/**
 * @defgroup svnxx_exceptions SVN++ Exceptions
 * @brief Exceptions in SVN++
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
 * The SVN++ implementation will also throw standard exceptions when
 * appropriate. Their use is documented in the relevant modules.
 *
 * Exception Hierarchy
 * -------------------
 *
 * All SVN++ exceptions are ultimately derived from @c std:::exception.
 *
 * * <em>std::exception</em>
 *   + <em>std::bad_alloc</em>
 *     - apache::subversion::svnxx::allocation_failed\n
 *       Thrown when memory cannot be allocated from an APR pool
 *   + <em>std::runtime_error</em>
 *     - apache::subversion::svnxx::error\n
 *       Thrown when an operation failed (see @ref svn_error_t)
 *       - apache::subversion::svnxx::cancelled\n
 *         Thrown when an operation was cancelled, including by the
 *         user code throwing a @c stop_iteration exception (see
 *         below)
 *   + apache::subversion::svnxx::stop_iteration\n
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
class allocation_failed : public std::bad_alloc
{
public:
  virtual ~allocation_failed() noexcept {}

  virtual const char* what() const noexcept override
    {
      return reason;
    }

protected:
  explicit allocation_failed(const char* what_arg) noexcept
    : reason(what_arg)
    {}

private:
  const char* reason;
};

namespace detail {
using error_ptr = std::shared_ptr<svn_error_t>;
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
   * @note the returned string is valid only as long as this @c error
   *       object is in scope.
   */
  virtual const char* what() const noexcept override;

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
            const std::string& message_, bool trace)
      : m_errno(errval),
        m_errname(errname),
        m_message(message_),
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
  error(detail::error_ptr err);
  const char* const m_message;
  std::vector<message> compile_messages(bool show_traces) const;
};

/**
 * Thrown instead of Error when the error chain contains a
 * @c SVN_ERR_CANCELLED error code.
 */
class cancelled : public error
{
public:
  virtual ~cancelled() {}

protected:
  cancelled(detail::error_ptr err)
    : error(err)
    {}
};

/**
 * User code should throw this exception from callbacks to
 * cancel an operation.
 */
class stop_iteration : public std::exception
{
public:
  stop_iteration() {}
  virtual ~stop_iteration() {}
  virtual const char* what() const noexcept override;
};

} // namespace svnxx
} // namespace subversion
} // namespace apache

/**
 * @}
 */
#endif  // SVNXX_EXCEPTION_HPP
