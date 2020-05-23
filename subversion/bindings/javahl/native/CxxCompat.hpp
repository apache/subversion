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

#ifndef SVN_JAVAHL_CXX_COMPAT_HPP
#define SVN_JAVAHL_CXX_COMPAT_HPP

#include <memory>

namespace JavaHL {
namespace cxx {

#if defined(__cplusplus) && __cplusplus >= 201103L
/*
 * C++11 and later
 */

// Use std::unique_ptr for exclusive ownership.
template<
  typename T,
  typename D = ::std::default_delete<T>
  >
using owned_ptr = ::std::unique_ptr<T, D>;

// Use std::move for transfering ownership of owned pointers.
using ::std::move;

#else
/*
 * C++03 and earlier
 */

// Use a dumbed-down std::auto_ptr for exclusive ownership.
template<class T>
class owned_ptr : public ::std::auto_ptr<T>
{
  typedef ::std::auto_ptr<T> auto_ptr;
  typedef ::std::auto_ptr_ref<T> auto_ptr_ref;

public:
  explicit owned_ptr(T *p = 0) throw()
    : auto_ptr(p)
    {}

  owned_ptr(owned_ptr& r) throw()
    : auto_ptr(r)
    {}

  owned_ptr(auto_ptr_ref r) throw()
    : auto_ptr(r)
    {}

  owned_ptr& operator=(owned_ptr& r) throw()
    {
      static_cast<auto_ptr&>(*this) = r;
      return *this;
    }

  owned_ptr& operator=(auto_ptr_ref r) throw()
    {
      static_cast<auto_ptr&>(*this) = r;
      return *this;
    }
};

// Fake std::move since there are no rvalue references.
template<class T>
T& move(T& t)
{
  return t;
}

#endif

} // namespace cxx
} // namespace JavaHL

#endif // SVN_JAVAHL_CXX_COMPAT_HPP
