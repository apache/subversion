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

#ifndef SVNXX_PRIVATE_CLIENT_CONTEXT_HPP
#define SVNXX_PRIVATE_CLIENT_CONTEXT_HPP

#include "svnxx/client/context.hpp"

#include "../private/init_private.hpp"
#include "../aprwrap.hpp"

#include "svn_client.h"

namespace apache {
namespace subversion {
namespace svnxx {
namespace client {
namespace detail {

// TODO: document this
class context
{
  using global_state = svnxx::detail::global_state;

public:
  context()
    : state(global_state::get()),
      ctx_pool(state),
      ctx(create_ctx(ctx_pool))
    {}

  const global_state::ptr& get_state() const noexcept { return state; }
  const apr::pool& get_pool() const noexcept { return ctx_pool; }
  svn_client_ctx_t* get_ctx() const noexcept { return ctx; };

private:
  const global_state::ptr state;
  apr::pool ctx_pool;
  svn_client_ctx_t* const ctx;

  static svn_client_ctx_t* create_ctx(const apr::pool& pool);
};

} // namespace detail
} // namespace client
namespace impl {

// TODO: document this
inline client::detail::context_ptr unwrap(client::context& ctx)
{
  struct context_wrapper final : public client::context
  {
    inherited get() const noexcept
      {
        return *this;
      }
  };

  return static_cast<context_wrapper&>(ctx).get();
}

} // namesapce impl
} // namespace svnxx
} // namespace subversion
} // namespace apache

#endif // SVNXX_PRIVATE_CLIENT_CONTEXT_HPP
