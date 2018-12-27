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

#ifndef SVNXX_PRIVATE_APRWRAP_POOL_H
#define SVNXX_PRIVATE_APRWRAP_POOL_H

#include <cstdlib>
#include <functional>
#include <memory>

#include "svnxx/exception.hpp"
#include "svnxx/noncopyable.hpp"

#include "svn_pools.h"

namespace apache {
namespace subversion {
namespace svnxx {
namespace apr {

struct delete_pool
{
  void operator() (apr_pool_t* pool)
    {
      svn_pool_destroy(pool);
    }
};

using pool_ptr = std::unique_ptr<apr_pool_t, delete_pool>;

/**
 * Encapsulates an APR pool.
 */
class pool : pool_ptr
{
  friend class iteration;
  struct iteration_proxy
  {
    pool& proxied_pool;
  };

  static apr_pool_t* get_root_pool();

  struct allocation_size_overflowed final : public allocation_failed
  {
    allocation_size_overflowed() noexcept
    : allocation_failed("svn++ allocation size overflowed")
      {}
  };

  using allocator = std::function<void*(apr_pool_t*,apr_size_t)>;
  template<typename T>
  T* safe_alloc(std::size_t count, allocator alloc)
    {
      if (count > APR_SIZE_MAX / sizeof(T))
        throw allocation_size_overflowed();
      return static_cast<T*>(alloc(get(), count * sizeof(T)));
    }

public:
  /**
   * Create a pool as a child of the application's root pool.
   */
  pool()
    : pool_ptr(svn_pool_create(get_root_pool()))
    {}

  /**
   * Retuurn a pool pointer that can be used by the C APIs.
   */
  apr_pool_t* get() const noexcept
    {
      return pool_ptr::get();
    }

  /**
   * Create a pool as a child of @a parent.
   */
  explicit pool(const pool* parent)
    : pool_ptr(svn_pool_create(parent->get()))
    {}

  /**
   * Clear the pool.
   */
  void clear()
    {
      apr_pool_clear(get());
    }

  /**
   * Allocate space for @a count elements of type @a T from the pool.
   * The contents of the allocated buffer will contain unspecified data.
   */
  template<typename T>
  T* alloc(std::size_t count)
    {
      // apr_palloc may be a macro, so wrap it in a closure.
      static const auto palloc = [](apr_pool_t* p, apr_size_t size)
                                   {
                                     return apr_palloc(p, size);
                                   };
      return safe_alloc<T>(count, palloc);
    }

  /**
   * Allocate space for @a count elements of type @a T from the pool.
   * The contents of the allocated buffer will be initialized to zero.
   */
  template<typename T>
  T* allocz(std::size_t count)
    {
      // apr_pcalloc may be a macro, so wrap it in a closure.
      static const auto pcalloc = [](apr_pool_t* p, apr_size_t size)
                                    {
                                      return apr_pcalloc(p, size);
                                    };
      return safe_alloc<T>(count, pcalloc);
    }

  operator iteration_proxy() noexcept
    {
      return iteration_proxy{*this};
    }

public:
  /**
   * Pool proxy used for iteration scratch pools.
   *
   * Construct this object inside a loop body in order to clear the
   * proxied pool on every iteration.
   */
  class iteration : detail::noncopyable
  {
  public:
    /**
     * The constructor clears the proxied pool.
     */
    explicit iteration(pool::iteration_proxy iterbase)
      : proxied(iterbase.proxied_pool)
      {
        proxied.clear();
      }

    /**
     * Returns a reference to the proxied pool.
     */
    apr::pool& get_pool() const noexcept
      {
        return proxied;
      }

    /**
     * proxy method for pool::get()
     */
    apr_pool_t* get() const noexcept
      {
        return proxied.get();
      }

    /**
     * Proxy method for pool::alloc()
     */
    template<typename T>
    T* alloc(std::size_t count)
      {
        return proxied.alloc<T>(count);
      }

    /**
     * Proxy method for pool::allocz()
     */
    template<typename T>
    T* allocz(std::size_t count)
      {
        return proxied.allocz<T>(count);
      }

  private:
    apr::pool& proxied;
  };
};

} // namespace apr
} // namespace svnxx
} // namespace subversion
} // namespace apache

#endif // SVNXX_PRIVATE_APRWRAP_POOL_H
