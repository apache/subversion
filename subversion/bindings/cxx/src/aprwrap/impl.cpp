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

#include <mutex>

#include "svn_private_config.h"

#include "pool.hpp"
#include "hash.hpp"

namespace apache {
namespace subversion {
namespace svnxx {
namespace apr {

//
// Pool implementation
//

apr_pool_t* Pool::get_root_pool()
{
  static std::once_flag atomic_init_flag;
  static apr_pool_t* root_pool = nullptr;

  std::call_once(
      atomic_init_flag,
      []()
        {
          const auto allocator = svn_pool_create_allocator(true);
          root_pool = svn_pool_create_ex(nullptr, allocator);
        });

  return root_pool;
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
} // namespace svnxx
} // namespace subversion
} // namespace apache
