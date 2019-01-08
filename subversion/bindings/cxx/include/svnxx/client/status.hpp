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

#include "svnxx/detail/future.hpp"
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
 * @ingroup svnxx_client
 * @brief Perform a status operation on @a path.
 * @param ctx the #context object to use for this operation
 * @param path the (root) path for the status walk.
 * @param rev the revision to use when @c check_out_of_date is set in @a flags
 * @param depth the depth of the operation
 * @param flags a combination of @c status_flags
 * @param callback a function that will be called for each status target
 * @warning TODO: Work in progress
 * @see svn_client_status6
 */
revision::number
status(context& ctx, const char* path,
       const revision& rev, depth depth, status_flags flags,
       status_callback callback);

namespace async {

/**
 * @ingroup svnxx_client
 * @brief Perform an asynchronous status operation on @a path.
 *
 * Behaves as if svn::client::status() were invoked through
 * <tt>std::async()</tt>, but also maintains the lifetime of
 * internal state relevant to the status operation.
 *
 * @warning Any callbacks regietered in the context @a ctx, as well
 *          as the status @a callback itself, may be called in the
 *          context of a different thread than the one that created
 *          this asynchronous operation.
 */
svnxx::detail::future<revision::number>
status(std::launch policy, context& ctx, const char* path,
       const revision& rev, depth depth_, status_flags flags,
       status_callback callback);

/**
 * @overload
 * @ingroup svnxx_client
 * @note Uses the <tt>std::launch</tt> @a policy set to
 *       <tt>std::launch::async|std::launch::deferred</tt>.
 */
svnxx::detail::future<revision::number>
status(context& ctx, const char* path,
       const revision& rev, depth depth_, status_flags flags,
       status_callback callback);

} // namespace async
} // namespace client
} // namespace svnxx
} // namespace subversion
} // namespace apache

#endif  // SVNXX_CLIENT_STATUS_HPP
