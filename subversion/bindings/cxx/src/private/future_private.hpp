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

#ifndef SVNXX_PRIVATE_FUTURE_HPP
#define SVNXX_PRIVATE_FUTURE_HPP

#include "svnxx/detail/future.hpp"

#include "../private/init_private.hpp"
#include "../aprwrap.hpp"

namespace apache {
namespace subversion {
namespace svnxx {
namespace detail {
namespace future_ {


// Encapsulates a result pool that will contain pool-allocated
// objects returned from asynchronous operations. Consequently,
// keeps a reference to the global state that holds the root pool
// that is the parent of this result pool.
class result
{
public:
  explicit result(const detail::global_state::ptr& state_)
    : state(state_),
      result_pool(state_)
    {}

  apr::pool& get_pool() noexcept
    {
      return result_pool;
    }

private:
  const detail::global_state::ptr state;
  apr::pool result_pool;
};

} // namespace future_
} // namespace detail
namespace impl {


// Creates a detail::future_::result pointer for initializing
// detail::future objects.
inline detail::future_::unique_ptr
make_future_result(const detail::global_state::ptr& state)
{
  using namespace detail::future_;
  return unique_ptr(new result(state));
}

// Creates a null detail::future_::result pointer for cases where we
// do not need a result pool.
inline detail::future_::unique_ptr
make_future_result()
{
  return detail::future_::unique_ptr();
}


// Wrapper for detail::future with public constructor.
template<typename T>
struct future final : public detail::future_::future<T>
{
  using inherited = typename detail::future_::future<T>::inherited;
  future(inherited that, detail::future_::unique_ptr&& ctx) noexcept
    : detail::future_::future<T>(std::move(that), std::move(ctx))
    {}
};


// Wrapper for detail::shared_future with public constructor.
template<typename T>
struct shared_future final : public detail::future_::shared_future<T>
{
  using inherited = typename detail::future_::shared_future<T>::inherited;
  shared_future(inherited that, detail::future_::shared_ptr ctx) noexcept
    : detail::future_::shared_future<T>(std::move(that), ctx)
    {}
};

} // namespace impl
} // namespace svnxx
} // namespace subversion
} // namespace apache

#endif // SVNXX_PRIVATE_FUTURE_HPP
