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

#include "svnxx/exception.hpp"
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

namespace {
int handle_failed_allocation(int)
{
  throw allocation_failed("");
}

apr_pool_t* create_root_pool()
{
  apr_allocator_t *allocator;
  if (apr_allocator_create(&allocator) || !allocator)
    throw allocation_failed("svn++ creating pool allocator");

  apr_pool_t* root_pool;
  apr_pool_create_ex(&root_pool, nullptr, handle_failed_allocation, allocator);
  if (!root_pool)
    throw allocation_failed("svn++ creating root pool");

#if APR_POOL_DEBUG
  apr_pool_tag(root_pool, "svn++ root pool");
#endif

#if APR_HAS_THREADS
  // SVN++ pools are always as thread safe as APR can make them.
  apr_thread_mutex_t *mutex;
  apr_thread_mutex_create(&mutex, APR_THREAD_MUTEX_DEFAULT, root_pool);
  apr_allocator_mutex_set(allocator, mutex);
#endif

  return root_pool;
}
} // anonymous namespace

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
  root_pool = create_root_pool();
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
