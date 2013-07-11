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

#include "svn_pools.h"
#undef TRUE
#undef FALSE

namespace apache {
namespace subversion {
namespace cxxhl {
namespace apr {

/**
 * Encapsulates an APR pool.
 */
class Pool
{
public:
  /**
   * Create a pool as a child of the applications' root pool.
   */
  Pool() throw(cxxhl::InternalError)
    : m_pool(svn_pool_create(get_root_pool()))
    {}

  /**
   * Create a pool as a child of @a parent.
   */
  Pool(const Pool* parent) throw()
    : m_pool(svn_pool_create(parent->m_pool))
    {}

  /**
   * Destroy the pool.
   */
  ~Pool() throw() { svn_pool_destroy(m_pool); }

  /**
   * Clear the pool.
   */
  void clear() throw() { apr_pool_clear(m_pool); }

  /**
   * Retuurn a pool pointer that can be used by the C APIs.
   */
  apr_pool_t* get() const throw() { return m_pool; }

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
  static apr_pool_t* get_root_pool() throw(cxxhl::InternalError);
  apr_pool_t* const m_pool;
};

} // namespace apr
} // namespace cxxhl
} // namespace subversion
} // namespace apache

#endif // SVN_CXXHL_PRIVATE_APRWRAP_POOL_H
