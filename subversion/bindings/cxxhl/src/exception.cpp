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

#include "svn_error.h"
#include "svn_utf.h"
#include "private/svn_atomic.h"
#include "private/svn_error_private.h"
#include "svn_private_config.h"
#undef TRUE
#undef FALSE

namespace subversion {
namespace cxxhl {

namespace detail {

class error_description
{
public:
  static error_description* create(const char* message,
                                   const char *loc_file, long loc_line,
                                   bool trace_link)
    {
      bool empty_message = (message == NULL);
      const std::size_t length = (empty_message ? 0 : std::strlen(message));
      void *memblock = ::operator new(length + sizeof(error_description));

      error_description* description = new(memblock) error_description(
          loc_file, loc_line, trace_link, empty_message);
      if (length)
        std::memcpy(description->m_message, message, length);
      description->m_message[length] = 0;
      return description;
    }

  static error_description* create(const char* message)
    {
      return create(message, NULL, 0, false);
    }

  error_description* reference() throw()
    {
      if (this)
        svn_atomic_inc(&m_refcount);
      return this;
    }

  error_description* dereference() throw()
    {
      if (this && 0 == svn_atomic_dec(&m_refcount))
        {
          this->~error_description();
          ::operator delete(this, std::nothrow);
          return NULL;
        }
      return this;
    }

  const char* what() const throw() { return (m_empty ? NULL : m_message); }
  const char* file() const throw() { return m_loc_file; }
  long line() const throw() { return m_loc_line; }
  bool trace() const throw() { return m_trace; }

private:
  error_description(const char *loc_file, long loc_line,
                    bool trace_link, bool empty_message) throw()
    : m_loc_file(loc_file),
      m_loc_line(loc_line),
      m_trace(trace_link),
      m_empty(empty_message),
      m_refcount(0)
    {}

  ~error_description() throw() {}

  const char* m_loc_file;
  long m_loc_line;
  bool m_trace;
  bool m_empty;

  volatile svn_atomic_t m_refcount;
  char m_message[1];
};

} // namespace detail


namespace version_1_9_dev {

error::error(const char* description, int error_code)
  : m_errno(error_code),
    m_description(detail::error_description::create(description)->reference())
{}

error::error(const char* description, int error_code,
             error::shared_ptr nested_error)
  : m_errno(error_code),
    m_nested(nested_error),
    m_description(detail::error_description::create(description)->reference())
{}

error::error(const error& that) throw()
  : m_errno(that.m_errno),
    m_nested(that.m_nested),
    m_description(that.m_description->reference())
{}

error& error::operator=(const error& that) throw()
{
  if (this == &that)
    return *this;

  // This in-place destroy+copy implementation of the assignment
  // operator is safe because both the destructor and the copy
  // constructor do not throw exceptions.
  this->~error();
  return *new(this) error(that);
}

error::~error() throw()
{
  m_description->dereference();
}

const char* error::what() const throw()
{
  return m_description->what();
}

error::error(int error_code, detail::error_description* description) throw()
  : m_errno(error_code),
    m_description(description)
{}

void error::throw_svn_error(svn_error_t* err)
{
  const bool throw_cancelled = (err->apr_err == SVN_ERR_CANCELLED);
  detail::error_description* description = NULL;
  try
    {
      // Be very careful when creating the error descriptions, so that
      // the exception unwinder can free them if an allocation fails.
      // The private constructor does not increment the refcount
      // precisely for this reason.

      shared_ptr nested;
      shared_ptr* current = &nested;

      for (svn_error_t* next = err->child; next; next = next->child)
        {
          description = detail::error_description::create(
              next->message, next->file, next->line,
              svn_error__is_tracing_link(next));
          description->reference();
          current->reset(new error(next->apr_err, description));
          description = NULL;
          current = &(*current)->m_nested;
        }

      const int apr_err = err->apr_err;
      description = detail::error_description::create(
          err->message, err->file, err->line,
          svn_error__is_tracing_link(err));
      description->reference();
      svn_error_clear(err);
      if (throw_cancelled)
        {
          cancelled converted = cancelled(apr_err, description);
          description = NULL;
          converted.m_nested = nested;
          throw converted;
        }
      else
        {
          error converted = error(apr_err, description);
          description = NULL;
          converted.m_nested = nested;
          throw converted;
        }
    }
  catch (...)
    {
      description->dereference();
      throw;
    }
}


namespace {
void handle_one_error(error::message_list& ml, bool show_traces,
                      int error_code, detail::error_description* descr,
                      apr_pool_t* pool)
{
  if (show_traces && descr->file())
    {
      const char* file_utf8 = NULL;
      svn_error_t* err =
        svn_utf_cstring_to_utf8(&file_utf8, descr->file(), pool);
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
      buffer << ": (apr_err=" << error_code << ')';
      ml.push_back(error::message(0, buffer.str()));
    }

  if (descr->trace())
    return;

  const char *description = descr->what();
  if (!description)
    {
      char errorbuf[512];

      // Is this a Subversion-specific error code?
      if (error_code > APR_OS_START_USEERR
          && error_code <= APR_OS_START_CANONERR)
        description = svn_strerror(error_code, errorbuf, sizeof(errorbuf));
      // Otherwise, this must be an APR error code.
      else
        {
          svn_error_t* err = svn_utf_cstring_to_utf8(
              &description,
              apr_strerror(error_code, errorbuf, sizeof(errorbuf)),
              pool);
          if (err)
            {
              svn_error_clear(err);
              description = _("Can't recode error string from APR");
            }
        }
    }
  ml.push_back(error::message(error_code, std::string(description)));
}
} // anonymous namespace

error::message_list error::compile_messages(bool show_traces) const
{
  // Determine the maximum size of the returned list
  message_list::size_type max_length = 0;
  for (const error* err = this; err; err = err->m_nested.get())
    {
      if (show_traces && m_description->file())
        ++max_length;                   // We will display an error location
      if (!m_description->trace())
        ++max_length;                   // Traces do not emit a message line
    }
  message_list ml;
  ml.reserve(max_length);

  // This vector holds a list of all error codes that we've printed
  // the generic description for.  See svn_handle_error2 for details.
  std::vector<int> empties;
  empties.reserve(max_length);

  apr_pool_t* pool = NULL;
  apr_pool_create(&pool, NULL);
  try
    {
      for (const error* err = this; err; err = err->m_nested.get())
        {
          if (!err->m_description->what())
            {
              // Non-specific messages are printed only once.
              std::vector<int>::iterator it = std::find(
                  empties.begin(), empties.end(), err->m_errno);
              if (it != empties.end())
                continue;
              empties.push_back(err->m_errno);
            }
          handle_one_error(ml, show_traces,
                           err->m_errno, err->m_description,
                           pool);
        }
    }
  catch (...)
    {
      apr_pool_destroy(pool);
      throw;
    }

  apr_pool_destroy(pool);
  return ml;
}

} // namespace version_1_9_dev
} // namespace cxxhl
} // namespace subversion
