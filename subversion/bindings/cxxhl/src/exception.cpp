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

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <new>
#include <sstream>

#include "svncxxhl/exception.hpp"
#include "private.hpp"
#include "aprwrap.hpp"

#include "svn_error.h"
#include "svn_utf.h"
#include "private/svn_atomic.h"
#include "private/svn_error_private.h"
#include "svn_private_config.h"
#undef TRUE
#undef FALSE

namespace apache {
namespace subversion {
namespace cxxhl {

namespace detail {

class ErrorDescription
{
public:
  typedef compat::shared_ptr<ErrorDescription> shared_ptr;

  static shared_ptr create(const char* message, int error_code,
                           const char *loc_file, long loc_line,
                           bool trace_link)
    {
      const bool empty_message = (message == NULL);
      const std::size_t length = (empty_message ? 0 : std::strlen(message));
      void* memblock = ::operator new(length + sizeof(ErrorDescription));

      ErrorDescription* description = new(memblock) ErrorDescription(
          error_code, loc_file, loc_line, trace_link, empty_message);
      if (length)
        std::memcpy(description->m_message, message, length);
      description->m_message[length] = 0;
      return shared_ptr(description);
    }

  static shared_ptr create(const char* message, int error_code)
    {
      return create(message, error_code, NULL, 0, false);
    }

  ~ErrorDescription() throw() {}

  const char* what() const throw() { return (m_empty ? NULL : m_message); }
  int code() const throw() { return m_errno; }
  const char* file() const throw() { return m_loc_file; }
  long line() const throw() { return m_loc_line; }
  bool trace() const throw() { return m_trace; }
  shared_ptr& nested() throw() { return m_nested; }
  const shared_ptr& nested() const throw() { return m_nested; }

private:
  ErrorDescription(int error_code,
                   const char *loc_file, long loc_line,
                   bool trace_link, bool empty_message) throw()
    : m_loc_file(loc_file),
      m_loc_line(loc_line),
      m_trace(trace_link),
      m_empty(empty_message),
      m_errno(error_code)
    {}

  const char* m_loc_file;
  long m_loc_line;
  bool m_trace;
  bool m_empty;

  shared_ptr m_nested;

  int m_errno;                  ///< The (SVN or APR) error code.
  char m_message[1];            ///< The error message
};

} // namespace detail

//
// Class InternalError
//

InternalError::InternalError(const char* description)
  : m_description(detail::ErrorDescription::create(description, 0))
{}

InternalError::InternalError(const InternalError& that) throw()
  : m_description(that.m_description)
{}

InternalError& InternalError::operator=(const InternalError& that) throw()
{
  if (this == &that)
    return *this;

  // This in-place destroy+copy implementation of the assignment
  // operator is safe because both the destructor and the copy
  // constructor do not throw exceptions.
  this->~InternalError();
  return *new(this) InternalError(that);
}

InternalError::~InternalError() throw() {}

const char* InternalError::what() const throw()
{
  return m_description->what();
}

InternalError::InternalError(description_ptr description) throw()
  : m_description(description)
{}

//
// Class Error
//

Error::Error(const Error& that) throw()
  : InternalError(that.m_description)
{}

Error& Error::operator=(const Error& that) throw()
{
  if (this == &that)
    return *this;

  // This in-place destroy+copy implementation of the assignment
  // operator is safe because both the destructor and the copy
  // constructor do not throw exceptions.
  this->~Error();
  return *new(this) Error(that);
}

Error::~Error() throw() {}

int Error::code() const throw()
{
  return m_description->code();
}

namespace {
const char* get_generic_message(apr_status_t error_code,
                                const APR::Pool& scratch_pool)
{
  char errorbuf[512];

  // Is this a Subversion-specific error code?
  if (error_code > APR_OS_START_USEERR && error_code <= APR_OS_START_CANONERR)
    return svn_strerror(error_code, errorbuf, sizeof(errorbuf));
  // Otherwise, this must be an APR error code.
  else
    {
      const char* generic;
      svn_error_t* err = svn_utf_cstring_to_utf8(
          &generic,
          apr_strerror(error_code, errorbuf, sizeof(errorbuf)),
          scratch_pool.get());
      if (!err)
        return generic;

      // Use fuzzy transliteration instead.
      svn_error_clear(err);
      return svn_utf_cstring_from_utf8_fuzzy(errorbuf, scratch_pool.get());
    }
}

void handle_one_error(Error::MessageList& ml, bool show_traces,
                      const detail::ErrorDescription* descr,
                      const APR::Pool& pool)
{
  const int error_code = descr->code();

  if (show_traces && descr->file())
    {
      const char* file_utf8 = NULL;
      svn_error_t* err =
        svn_utf_cstring_to_utf8(&file_utf8, descr->file(), pool.get());
      if (err)
        {
          svn_error_clear(err);
          file_utf8 = NULL;
        }
      std::ostringstream buffer;
      if (file_utf8)
        buffer << file_utf8 << ':' << descr->line();
      else
        buffer << "svn:<undefined>";
      if (descr->trace())
        buffer << ',';
      else
        {
#ifdef SVN_DEBUG
          if (const char* symbolic_name = svn_error_symbolic_name(error_code))
            buffer << ": (apr_err=" << symbolic_name << ')';
          else
#endif
            buffer << ": (apr_err=" << error_code << ')';
        }
      ml.push_back(Error::Message(error_code, buffer.str(), true));
    }

  if (descr->trace())
    return;

  const char *description = descr->what();
  if (!description)
    description = get_generic_message(error_code, pool);
  ml.push_back(Error::Message(error_code, std::string(description), false));
}
} // anonymous namespace

Error::MessageList Error::compile_messages(bool show_traces) const
{
  // Determine the maximum size of the returned list
  MessageList::size_type max_length = 0;
  for (const detail::ErrorDescription* description = m_description.get();
       description; description = description->nested().get())
    {
      if (show_traces && description->file())
        ++max_length;                   // We will display an error location
      if (!description->trace())
        ++max_length;                   // Traces do not emit a message line
    }
  MessageList ml;
  ml.reserve(max_length);

  // This vector holds a list of all error codes that we've printed
  // the generic description for.  See svn_handle_error2 for details.
  std::vector<int> empties;
  empties.reserve(max_length);

  APR::IterationPool iterbase;
  for (const detail::ErrorDescription* description = m_description.get();
       description; description = description->nested().get())
    {
      APR::Pool::Iteration iterpool(iterbase);

      if (!description->what())
        {
          // Non-specific messages are printed only once.
          std::vector<int>::iterator it = std::find(
              empties.begin(), empties.end(), description->code());
          if (it != empties.end())
            continue;
          empties.push_back(description->code());
        }
      handle_one_error(ml, show_traces, description, iterpool.pool());
    }
  return ml;
}

const char* Error::Message::generic_message() const
{
  APR::Pool pool;
  return get_generic_message(m_errno, pool);
}

namespace detail {
void checked_call(svn_error_t* err)
{
  if (!err)
    return;

  struct ErrorBuilder : public Error
  {
    explicit ErrorBuilder (ErrorDescription::shared_ptr description)
      : Error(description)
      {}
  };

  struct CancelledBuilder : public Cancelled
  {
    explicit CancelledBuilder (ErrorDescription::shared_ptr description)
      : Cancelled(description)
      {}
  };

  ErrorDescription::shared_ptr description;
  ErrorDescription::shared_ptr* current = &description;

  bool cancelled = false;
  for (svn_error_t* next = err; next; next = next->child)
    {
      *current = ErrorDescription::create(next->message, next->apr_err,
                                          next->file, next->line,
                                          svn_error__is_tracing_link(next));
      current = &(*current)->nested();
      if (next->apr_err == SVN_ERR_CANCELLED)
        cancelled = true;
    }
  svn_error_clear(err);

  if (cancelled)
    throw CancelledBuilder(description);
  else
    throw ErrorBuilder(description);
}
} // namespace detail

} // namespace cxxhl
} // namespace subversion
} // namespace apache
