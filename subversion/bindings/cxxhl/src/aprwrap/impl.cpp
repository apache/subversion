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

#include <apr_time.h>

#include "private/svn_atomic.h"
#include "svn_private_config.h"
#undef TRUE
#undef FALSE

#include "pool.hpp"
#include "hash.hpp"

namespace apache {
namespace subversion {
namespace cxxhl {
namespace apr {

//
// Pool implementation
//

apr_pool_t* Pool::get_root_pool()
{
  static const svn_atomic_t NONE = 0;
  static const svn_atomic_t START = 1;
  static const svn_atomic_t DONE = 2;

  static volatile svn_atomic_t init_state = NONE;
  static apr_pool_t* root_pool = NULL;

  svn_atomic_t state = svn_atomic_cas(&init_state, START, NONE);

  switch (state)
    {
    case DONE:
      // The root pool has already been initialized.
      return root_pool;

    case START:
      // Another thread is currently initializing the pool; Spin and
      // wait for it to finish, with exponential backoff, but no
      // longer than half a second.
      for (unsigned shift = 0; state == START && shift < 8; ++shift)
        {
          apr_sleep((APR_USEC_PER_SEC / 1000) << shift);
          state = svn_atomic_cas(&init_state, NONE, NONE);
        }
      if (state == START)
        throw cxxhl::InternalError(
            _("APR pool initialization failed: Timed out"));
      return root_pool;

    case NONE:
      // Initialize the root pool and release the lock.
      // We'll assume that we always need thread-safe allocation.
      root_pool = svn_pool_create_ex(NULL, svn_pool_create_allocator(true));
      svn_atomic_cas(&init_state, DONE, START);
      return root_pool;

    default:
      throw cxxhl::InternalError(
          _("APR pool initialization failed: Invalid state"));
    }
}

//
// Hash implementation
//

void Hash<void, void>::iterate(Hash<void, void>::Iteration& callback,
                               const Pool& scratch_pool)
{
  for (apr_hash_index_t* hi = apr_hash_first(scratch_pool.get(), m_hash);
       hi; hi = apr_hash_next(hi))
    {
      key_type key;
      value_type val;
      Key::size_type klen;

      apr_hash_this(hi, &key, &klen, &val);
      if (!callback(Key(key, klen), val))
        break;
    }
}

} // namespace apr
} // namespace cxxhl
} // namespace subversion
} // namespace apache
