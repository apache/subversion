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

#include <csignal>
#include <cstddef>
#include <cstring>
#include <new>

#include "svncxxhl/exception.hpp"

#include "svn_error.h"
#undef TRUE
#undef FALSE

namespace subversion {
namespace cxxhl {

namespace detail {

struct error_description
{
  volatile std::sig_atomic_t m_refcount;
  char m_message[1];

  static error_description* create(const char* message) throw(std::bad_alloc)
    {
      const std::size_t len = std::strlen(message);
      void *memblock = ::operator new(len + sizeof(error_description));

      error_description* err = new(memblock) error_description;
      std::memcpy(err->m_message, message, len);
      err->m_message[len] = 0;
      err->m_refcount = 0;
      return err;
    }

  static void destroy(error_description* message) throw()
    {
      // TODO: DEBUG assert(message->m_refcount == 0)
      ::operator delete(message, std::nothrow);
    }
};

} // namespace detail


namespace version_1_9_dev {

error::error(const char* description, int error_code)
  : m_errno(error_code),
    m_description(detail::error_description::create(description))
{
  ++m_description->m_refcount;
}

error::error(const char* description, int error_code,
             error::shared_ptr nested_error)
  : m_errno(error_code),
    m_nested(nested_error),
    m_description(detail::error_description::create(description))
{
  ++m_description->m_refcount;
}

error::error(const error& that) throw()
  : m_errno(that.m_errno),
    m_nested(that.m_nested),
    m_description(that.m_description)
{
  ++m_description->m_refcount;
}

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
  if (!--m_description->m_refcount)
    detail::error_description::destroy(m_description);
}

const char* error::what() const throw()
{
  return m_description->m_message;
}

} // namespace version_1_9_dev
} // namespace cxxhl
} // namespace subversion
