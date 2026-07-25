/**
 * @file svnxx/detail/future.hpp
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

#ifndef SVNXX_DETAIL_FUTURE_HPP
#define SVNXX_DETAIL_FUTURE_HPP

#include <future>
#include <memory>

namespace apache {
namespace subversion {
namespace svnxx {
namespace detail {
namespace future_ {

// Forward delcaration of the future result context.
class result;
using shared_ptr = std::shared_ptr<result>;
using unique_ptr = std::unique_ptr<result>;


// Base class for template shared_future.
class shared_future_base
{
protected:
  shared_future_base() noexcept {}

  shared_future_base(const shared_future_base& that)
    : shared_result(that.shared_result)
    {}

  shared_future_base(shared_future_base&& that)
    : shared_result(std::move(that.shared_result))
    {}

  explicit shared_future_base(shared_ptr shared_result_)
    : shared_result(shared_result_)
    {}

private:
  shared_ptr shared_result;
};

// Template forward declaration for shared_future constructor.
template<typename T> class future;

/**
 * @ingroup svnxx_detail
 * @brief like <tt>std::shared_future</tt>, but also maintains
 * internal state relevant to the asynchronous SVN++ operation.
 */
template<typename T>
class shared_future : private std::shared_future<T>,
                      private shared_future_base
{
protected:
  using inherited = std::shared_future<T>;

  shared_future(inherited&& that, shared_ptr shared_result_) noexcept
    : inherited(that), shared_future_base(shared_result_)
    {}

public:
  shared_future() noexcept {}

  shared_future(const shared_future& that) noexcept
    : inherited(that), shared_future_base(that)
    {}

  shared_future(shared_future&& that) noexcept
    : inherited(std::move(that)), shared_future_base(std::move(that))
    {}

  shared_future(future<T>&& that) noexcept;

  using inherited::get;
  using inherited::valid;
  using inherited::wait;
  using inherited::wait_for;
  using inherited::wait_until;
};


// Base class for template future.
class future_base
{
protected:
  future_base() noexcept;
  ~future_base() noexcept;
  future_base(future_base&& that) noexcept;
  future_base(const future_base&) = delete;
  explicit future_base(unique_ptr&& unique_result_) noexcept;

  shared_ptr share() noexcept;

private:
  unique_ptr unique_result;
};

/**
 * @ingroup svnxx_detail
 * @brief like <tt>std::future</tt>, but also maintains internal
 * state relevant to the asynchronous SVN++ operation.
 */
template<typename T>
class future : private std::future<T>,
               private future_base
{
  // shared_future constructor must be able to access our base classes.
  friend class shared_future<T>;

protected:
  using inherited = std::future<T>;

  future(inherited&& that, unique_ptr&& unique_result_) noexcept
    : inherited(std::move(that)), future_base(std::move(unique_result_))
    {}

public:
  future() noexcept {}

  future(future&& that) noexcept
    : inherited(std::move(that)), future_base(std::move(that))
    {}

  shared_future<T> share() noexcept
    {
      return shared_future<T>(std::move(*this));
    }

  using inherited::get;
  using inherited::valid;
  using inherited::wait;
  using inherited::wait_for;
  using inherited::wait_until;
};

// Implement the constructor here since it has to see the whole future class.
template<typename T>
inline shared_future<T>::shared_future(future<T>&& that) noexcept
  : inherited(std::move(that)), shared_future_base(that.future_base::share())
{}

} // namespace future_

template<typename T> using future = future_::future<T>;
template<typename T> using shared_future = future_::shared_future<T>;

} // namespace detail
} // namespace svnxx
} // namespace subversion
} // namespace apache

#endif  // SVNXX_DETAIL_FUTURE_HPP
