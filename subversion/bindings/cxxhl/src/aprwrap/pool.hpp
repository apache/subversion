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

#ifndef SVN_CXXHL_PRIVATE_APRWRAP_POOL_H
#define SVN_CXXHL_PRIVATE_APRWRAP_POOL_H

#include <cstdlib>

#include "svncxxhl/exception.hpp"
#include "svncxxhl/_compat.hpp"

#include "svn_pools.h"
#undef TRUE
#undef FALSE

namespace apache {
namespace subversion {
namespace cxxhl {
namespace apr {

// Forward declaration
class IterationPool;

/**
 * Encapsulates an APR pool.
 */
class Pool : compat::noncopyable
{
public:
  /**
   * Create a pool as a child of the applications' root pool.
   */
  Pool()
    : m_pool(svn_pool_create(get_root_pool()))
    {}

  /**
   * Create a pool as a child of @a parent.
   */
  explicit Pool(Pool* parent) throw()
    : m_pool(svn_pool_create(parent->m_pool))
    {}

  /**
   * Destroy the pool.
   */
  ~Pool() throw()
    {
      svn_pool_destroy(m_pool);
    }

  /**
   * Clear the pool.
   */
  void clear() throw()
    {
      apr_pool_clear(m_pool);
    }

  /**
   * Retuurn a pool pointer that can be used by the C APIs.
   */
  apr_pool_t* get() const throw()
    {
      return m_pool;
    }

  /**
   * Allocate space for @a count elements of type @a T from the pool.
   * The contents of the allocated buffer will contain unspecified data.
   */
  template<typename T>
  T* alloc(std::size_t count) throw()
    {
      return static_cast<T*>(apr_palloc(m_pool, count * sizeof(T)));
    }

  /**
   * Allocate space for @a count elements of type @a T from the pool.
   * The contents of the allocated buffer will be initialized to zero.
   */
  template<typename T>
  T* allocz(std::size_t count) throw()
    {
      return static_cast<T*>(apr_pcalloc(m_pool, count * sizeof(T)));
    }

private:
  static apr_pool_t* get_root_pool();
  apr_pool_t* const m_pool;

public:
  /**
   * Pool proxy used for iteration scratch pools.
   *
   * Construct this object inside a loop body in order to clear the
   * proxied pool on every iteration.
   */
  class Iteration : compat::noncopyable
  {
  public:
    /**
     * The constructor clears the proxied pool.
     */
    explicit Iteration(IterationPool& iterbase) throw();

    /**
     * Returns a reference to the proxied pool.
     */
    Pool& pool() const throw()
      {
        return m_pool;
      }

    /**
     * Proxy method for Pool::get
     */
    apr_pool_t* get() const throw()
      {
        return m_pool.get();
      }

    /**
     * Proxy method for Pool::alloc
     */
    template<typename T>
    T* alloc(std::size_t count) throw()
      {
        return m_pool.alloc<T>(count);
      }

    /**
     * Proxy method for Pool::allocz
     */
    template<typename T>
    T* allocz(std::size_t count) throw()
      {
        return m_pool.allocz<T>(count);
      }

  private:
    Pool& m_pool;
  };
};

/**
 * Pool wrapper that hides the pool implementation, except for construction.
 *
 * Construct this object outside a loop body, then within the body,
 * use Pool::Iteration to access the wrapped pool.
 */
class IterationPool : compat::noncopyable
{
public:
  IterationPool() {}

  explicit IterationPool(Pool* parent) throw()
    : m_pool(parent)
    {}

private:
  friend class Pool::Iteration;
  Pool m_pool;
};

// Pool::Iteration constructor implementation
inline Pool::Iteration::Iteration(IterationPool& iterbase) throw()
  : m_pool(iterbase.m_pool)
{
  m_pool.clear();
}

} // namespace apr
} // namespace cxxhl
} // namespace subversion
} // namespace apache

#endif // SVN_CXXHL_PRIVATE_APRWRAP_POOL_H
