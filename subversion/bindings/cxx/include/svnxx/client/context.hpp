/**
 * @file svnxx/client/context.hpp
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

#ifndef SVNXX_CLIENT_CONTEXT_HPP
#define SVNXX_CLIENT_CONTEXT_HPP

#include <memory>

namespace apache {
namespace subversion {
namespace svnxx {
namespace client {

namespace detail {
class context;
using context_ptr = std::shared_ptr<context>;
using weak_context_ptr = std::weak_ptr<context>;
} // namespace detail

/**
 * @brief The context for client operations, see @ref svn_client_ctx_t.
 * @warning TODO: Work in progress.
 */
class context : protected detail::context_ptr
{
public:
  context();
  ~context();

protected:
  using inherited = detail::context_ptr;
};

} // namespace client
} // namespace svnxx
} // namespace subversion
} // namespace apache

#endif  // SVNXX_CLIENT_CONTEXT_HPP
