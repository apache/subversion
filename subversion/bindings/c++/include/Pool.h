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
 *
 * @file Pool.h
 * @brief Interface of the class Pool
 */

#ifndef POOL_H
#define POOL_H

#include "svn_pools.h"

#include "apr_strings.h"

namespace SVN
{

  /**
   * This class manages one APR pool.  Objects of this class may be allocated
   * on the stack, ensuring the pool is destroyed when the function 
   * completes.
   *
   * Several methods here are one-liners, and so are defined here as well to
   * allow better inlining by the compiler.
   */
  class Pool
  {
    public:
      Pool();

      inline
      ~Pool()
      {
        svn_pool_destroy(m_pool);
      }

      template <typename T>
      inline T *
      alloc(apr_size_t sz)
      {
        return reinterpret_cast<T *>(apr_palloc(m_pool, sz));
      }

      inline char *
      strdup(const char *str)
      {
        return apr_pstrdup(m_pool, str);
      }

      inline void
      registerCleanup(apr_status_t (*cleanup_func)(void *), void *baton)
      {
        apr_pool_cleanup_register(m_pool, baton, cleanup_func,
                                  apr_pool_cleanup_null);
      }

      inline apr_pool_t *
      pool() const
      {
        return m_pool;
      }

      inline void
      clear() const
      {
        svn_pool_clear(m_pool);
      }

    private:
      /** The pool request pool.  */
      apr_pool_t *m_pool;

      /**
       * We declare the copy constructor and assignment operator private
       * here, so that the compiler won't inadvertently use them for us.
       * The default copy constructor just copies all the data members,
       * which would create two pointers to the same pool, one of which
       * would get destroyed while the other thought it was still
       * valid...and BOOM!  Hence the private declaration.
       */
      Pool &operator=(const Pool &that);
      Pool(const Pool &that);
  };

}


#endif // POOL_H
