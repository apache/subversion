/**
 * @file svnxx/noncopyable.hpp
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

#ifndef SVNXX_NONCOPYABLE_HPP
#define SVNXX_NONCOPYABLE_HPP

namespace apache {
namespace subversion {
namespace svnxx {
namespace detail {

namespace noncopyable_ {

/**
 * @brief Base class for non-copyable objects.
 *
 * Objects of classes derived from @c noncopyable cannot be copyed,
 * but can used as rvalue references and with <tt>std::move</tt>.
 *
 * @note Use @e private inheritance to avoid polymorphism traps!
 */
class noncopyable
{
protected:
  constexpr noncopyable() = default;
  ~noncopyable() = default;
private:
  noncopyable(const noncopyable&) = delete;
  noncopyable& operator=(const noncopyable&) = delete;
};
} // namespace noncopyable_

using noncopyable = noncopyable_::noncopyable;

} // namespace detail
} // namespace svnxx
} // namespace subversion
} // namespace apache

#endif  // SVNXX_NONCOPYABLE_HPP
