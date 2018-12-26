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

#include <cstddef>
#include <sstream>
#include <unordered_set>

#include "svnxx/exception.hpp"
#include "private.hpp"
#include "aprwrap.hpp"

#include "svn_error.h"
#include "svn_utf.h"
#include "private/svn_error_private.h"

namespace apache {
namespace subversion {
namespace svnxx {

namespace detail {
struct wrapped_error final
{
  svn_error_t* err;
  wrapped_error(svn_error_t* const& e) : err(e) {}
  ~wrapped_error()
    {
      svn_error_clear(err);
    }
};
} // namespace detail

//
// Class error
//

namespace {
inline const char* best_message(const detail::error_ptr& wrapper)
{
  if (!wrapper || !wrapper->err)
    return "";

  const apr_size_t bufsize = 512;
  char* buf = static_cast<char*>(apr_palloc(wrapper->err->pool, bufsize));
  return svn_err_best_message(wrapper->err, buf, bufsize);
}
} // anonymous namspace

error::error(detail::error_ptr svn_error)
  : detail::error_ptr(svn_error),
    m_message(best_message(svn_error))
{}

const char* error::what() const noexcept
{
  return m_message;
}

int error::code() const noexcept
{
  const auto wrapper = get();
  if (!wrapper || !wrapper->err)
    return 0;

  return static_cast<int>(wrapper->err->apr_err);
}

const char* error::name() const noexcept
{
  const auto wrapper = get();
  return svn_error_symbolic_name(!wrapper || !wrapper->err ? 0
                                 : wrapper->err->apr_err);
}

namespace {
const char* get_generic_message(apr_status_t error_code,
                                apr::pool& result_pool)
{
  const std::size_t errorbuf_size = 512;
  const auto errorbuf = result_pool.alloc<char>(errorbuf_size);

  // Wondering about what's in UTF-8? Yes, do keep on wondering ...
  return svn_strerror(error_code, errorbuf, errorbuf_size);
}

//
// Class error::message
//

void handle_one_error(std::vector<error::message>& messages,
                      bool show_traces,
                      const svn_error_t* err,
                      apr::pool& scratch_pool)
{
  struct message_builder final : public error::message
  {
    message_builder(apr_status_t errval, const char* errname,
                    const std::string& message, bool trace)
      : error::message(static_cast<int>(errval), errname, message, trace)
      {}
  };

  const char* const symbolic_name = svn_error_symbolic_name(err->apr_err);
  const bool tracing_link = svn_error__is_tracing_link(err);

  if (show_traces && err->file)
    {
      const char* file_utf8 = nullptr;
      svn_error_t* inner_err =
        svn_utf_cstring_to_utf8(&file_utf8, err->file, scratch_pool.get());
      if (inner_err)
        {
          svn_error_clear(inner_err);
          file_utf8 = nullptr;
        }

      std::ostringstream buffer;
      if (file_utf8)
        buffer << file_utf8 << ':' << err->line;
      else
        buffer << "svn:<undefined>";

      if (tracing_link)
        buffer << ',';
      else
        {
          if (symbolic_name)
            buffer << ": (apr_err=" << symbolic_name << ')';
          else
            buffer << ": (apr_err=" << err->apr_err << ')';
        }
      messages.emplace_back(message_builder(err->apr_err, symbolic_name,
                                            buffer.str(), true));
    }

  if (tracing_link)
    return;

  const char* description = err->message;
  if (!description)
    description = get_generic_message(err->apr_err, scratch_pool);
  messages.emplace_back(message_builder(err->apr_err, symbolic_name,
                                        description, false));
}
} // anonymous namespace

std::vector<error::message> error::compile_messages(bool show_traces) const
{
  // Determine the maximum size of the returned list
  std::vector<message>::size_type max_length = 0;
  for (svn_error_t* err = get()->err; err; err = err->child)
    {
      if (show_traces && err->file)
        ++max_length;                   // We will display an error location
      if (!svn_error__is_tracing_link(err))
        ++max_length;                   // Traces do not emit a message line
    }

  std::vector<message> messages;
  messages.reserve(max_length);

  // This the set of error codes that we've printed the generic
  // description for.  See svn_handle_error2 for details.
  std::unordered_set<apr_status_t> empties;
  empties.reserve(max_length);

  apr::pool iterbase;
  for (svn_error_t* err = get()->err; err; err = err->child)
    {
      apr::pool::iteration iterpool(iterbase);

      if (!err->message)
        {
          // Non-specific messages are printed only once.
          if (empties.count(err->apr_err))
            continue;
          empties.emplace(err->apr_err);
        }
      handle_one_error(messages, show_traces, err, iterpool.get_pool());
    }
  return messages;
}

std::string error::message::generic_text() const
{
  apr::pool scratch_pool;
  return get_generic_message(m_errno, scratch_pool);
}

//
// Class cancel
//

const char* cancel::what() const noexcept
{
  return "svn::cancel";
}

//
// checked_call
//

namespace detail {
void checked_call(svn_error_t* const err)
{
  struct error_builder final : public error
  {
    explicit error_builder(error_ptr svn_error)
      : error(svn_error)
      {}
  };

  struct canceled_builder final : public canceled
  {
    explicit canceled_builder(error_ptr svn_error)
      : canceled(svn_error)
      {}
  };

  if (!err)
    return;

  auto wrapper = std::make_shared<wrapped_error>(err);
  for (svn_error_t* next = err; next; next = next->child)
    {
      if (next->apr_err == SVN_ERR_CANCELLED)
        throw canceled_builder(wrapper);
    }
  throw error_builder(wrapper);
}
} // namespace detail
} // namespace svnxx
} // namespace subversion
} // namespace apache
