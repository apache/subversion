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

#include <sstream>

#include "private/init-private.hpp"

#include <apr_general.h>
#include "svn_pools.h"

namespace apache {
namespace subversion {
namespace svnxx {

init::init()
  : context(detail::context::create())
{}

namespace detail {

std::mutex context::guard;
context::weak_ptr context::self;

context::ptr context::create()
{
  std::unique_lock<std::mutex> lock(guard);
  auto ctx = self.lock();
  if (!ctx)
    {
      // Work around the private constructor: since this struct is
      // defined within a class member, the the private constructor
      // is accessible and std::make_shared won't complain about it.
      struct make_shared_hack : public context {};
      ctx = std::make_shared<make_shared_hack>();
      self = ctx;
    }
  return ctx;
}

context::context()
{
  const auto status = apr_initialize();
  if (status)
    {
      char errbuf[120];
      std::stringstream message;
      message << "APR initialization failed: "
              << apr_strerror(status, errbuf, sizeof(errbuf) - 1);
      throw std::runtime_error(message.str());
    }

  const auto allocator = svn_pool_create_allocator(true);
  root_pool = svn_pool_create_ex(nullptr, allocator);

  // TODO: Check root pool for null.
  // TODO: Change allocation-failed handler?
}

context::~context()
{
  std::unique_lock<std::mutex> lock(guard);
  apr_terminate();
  root_pool = nullptr;
}

} // namespace detail
} // namespace svnxx
} // namespace subversion
} // namespace apache
