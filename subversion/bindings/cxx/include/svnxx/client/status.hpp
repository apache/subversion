/**
 * @file svnxx/client/status.hpp
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

#ifndef SVNXX_CLIENT_STATUS_HPP
#define SVNXX_CLIENT_STATUS_HPP

#include <cstdint>
#include <functional>
#include <future>

#include "svnxx/client/context.hpp"

#include "svnxx/depth.hpp"
#include "svnxx/revision.hpp"

namespace apache {
namespace subversion {
namespace svnxx {
namespace client {

/**
 * @warning TODO: Work in progress
 */
struct status_notification{};

/**
 * @warning TODO: Work in progress
 */
using status_callback = std::function<void(const char* path,
                                           const status_notification& st)>;

/**
 * @brief Flags that modify the behaviour of the status operation.
 * @see svn_client_status6
 */
enum class status_flags : std::uint_least32_t
  {
    empty              = 0U,
    get_all            = 1U << 0,
    check_out_of_date  = 1U << 1,
    check_working_copy = 1U << 2,
    no_ignore          = 1U << 3,
    ignore_externals   = 1U << 4,
    depth_as_sticky    = 1U << 5,
  };

/**
 * @brief Bitwise conjunction operator for @c status_flags.
 */
inline status_flags operator&(status_flags a, status_flags b)
{
  return status_flags(std::uint_least32_t(a) & std::uint_least32_t(b));
}

/**
 * @brief Bitwise disjunction operator for  @c status_flags.
 */
inline status_flags operator|(status_flags a, status_flags b)
{
  return status_flags(std::uint_least32_t(a) | std::uint_least32_t(b));
}

/**
 * @brief Perform a status walk on @a path.
 * @see svn_client_status6
 * @param ctx the #context object to use for this operation
 * @param path the (root) path for the status walk.
 * @param rev the revision to use when @c check_out_of_date is set in @a flags
 * @param depth the depth of the operation
 * @param flags a combination of @c status_flags
 * @param callback a function that will be called for each status target
 * @warning TODO: Work in progress
 */
revision::number
status(context& ctx, const char* path,
       revision rev, depth depth, status_flags flags,
       status_callback callback);

} // namespace client
} // namespace svnxx
} // namespace subversion
} // namespace apache

#endif  // SVNXX_CLIENT_STATUS_HPP
