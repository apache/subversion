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

#include "private.hpp"

namespace apache {
namespace subversion {
namespace svnxx {
namespace client {

//
// class detail::context
//

namespace detail {

svn_client_ctx_t* context::create_ctx(const apr::pool& pool)
{
  svn_client_ctx_t* ctx;
  impl::checked_call(svn_client_create_context2(&ctx, nullptr, pool.get()));
  return ctx;
}

} // namespace detail

//
// class context
//

context::context()
  : inherited(new detail::context)
{}

context::~context()
{}

} // namespace client
} // namespace svnxx
} // namespace subversion
} // namespace apache
