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
#include "private/debug_private.hpp"
#include "private/init_private.hpp"

#include <apr_general.h>
#include <apr_pools.h>

namespace apache {
namespace subversion {
namespace svnxx {

init::init()
  : state(detail::global_state::create())
{
#ifdef SVNXX_POOL_DEBUG
  SVN_DBG(("svn++ created init object   %pp", static_cast<void*>(this)));
#endif
}

init::~init() noexcept
{
#ifdef SVNXX_POOL_DEBUG
  SVN_DBG(("svn++ destroyed init object %pp", static_cast<void*>(this)));
#endif
}

namespace detail {

namespace {
struct allocation_failed_builder final : public allocation_failed
{
  explicit allocation_failed_builder(const char* what_arg) noexcept
    : allocation_failed(what_arg)
    {}
};

int handle_failed_allocation(int)
{
  throw allocation_failed_builder("svn::allocation_failed");
}

#ifdef SVNXX_POOL_DEBUG
apr_status_t notify_root_pool_cleanup(void* key)
{
  if (key == impl::root_pool_key)
    SVN_DBG(("svn++ destroyed root pool"));
  return APR_SUCCESS;
}
#endif

apr_pool_t* create_root_pool()
{
  // Create the root pool's allocator.
  apr_allocator_t *allocator = nullptr;
  auto status = apr_allocator_create(&allocator);
  if (status || !allocator)
    throw allocation_failed_builder("svn++ creating pool allocator");

  // Create the root pool.
  apr_pool_t* root_pool = nullptr;
  status = apr_pool_create_ex(&root_pool, nullptr,
                              handle_failed_allocation, allocator);
  if (status || !root_pool)
    throw allocation_failed_builder("svn++ creating root pool");

#if APR_POOL_DEBUG
  apr_pool_tag(root_pool, impl::root_pool_tag);
#endif

#if APR_HAS_THREADS
  // SVN++ pools are always as thread safe as APR can make them.
  apr_thread_mutex_t *mutex = nullptr;
  status = apr_thread_mutex_create(&mutex, APR_THREAD_MUTEX_DEFAULT, root_pool);
  if (mutex && !status)
    apr_allocator_mutex_set(allocator, mutex);
  else
    {
#ifdef SVNXX_POOL_DEBUG
      SVN_DBG(("svn++ could not create allocator mutex, apr_err=%d", status));
#endif
      apr_pool_destroy(root_pool); // Don't leak the global pool.
      throw allocation_failed_builder("svn++ creating allocator mutex");
    }
#endif

#ifdef SVNXX_POOL_DEBUG
  apr_pool_cleanup_register(root_pool, impl::root_pool_key,
                            notify_root_pool_cleanup,
                            apr_pool_cleanup_null);
  SVN_DBG(("svn++ created root pool"));
#endif

  return root_pool;
}
} // anonymous namespace

std::mutex global_state::guard;
global_state::weak_ptr global_state::self;

global_state::ptr global_state::create()
{
  std::unique_lock<std::mutex> lock(guard);
  auto state = self.lock();
  if (!state)
    {
      // Work around the private constructor: since this struct is
      // defined within a class member, the the private constructor
      // is accessible and std::make_shared won't complain about it.
      struct global_state_builder final : public global_state {};
      state = std::make_shared<global_state_builder>();
      self = state;
    }
  return state;
}

global_state::global_state()
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
#ifdef SVNXX_POOL_DEBUG
  SVN_DBG(("svn++ created global state"));
#endif
}

global_state::~global_state()
{
#ifdef SVNXX_POOL_DEBUG
  SVN_DBG(("svn++ destroyed global state"));
#endif
  std::unique_lock<std::mutex> lock(guard);
  apr_terminate();
  root_pool = nullptr;
}

} // namespace detail
} // namespace svnxx
} // namespace subversion
} // namespace apache
