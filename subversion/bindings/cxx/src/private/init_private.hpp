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

#ifndef SVNXX_PRIVATE_INIT_HPP
#define SVNXX_PRIVATE_INIT_HPP

#include <memory>
#include <mutex>
#include <stdexcept>

#include <apr_pools.h>

#include "svnxx/init.hpp"
#include "svnxx/detail/noncopyable.hpp"

namespace apache {
namespace subversion {
namespace svnxx {
namespace detail {

class global_state : noncopyable
{
public:
  using ptr = std::shared_ptr<global_state>;
  using weak_ptr = std::weak_ptr<global_state>;

  ~global_state();

  static ptr create();
  static ptr get()
    {
      auto state = self.lock();
      if (!state)
        {
          throw std::logic_error(
              "The SVN++ library is not initialized."
              " Did you forget to create an instance of "
              " the apache::subversion::svnxx::init class?");
        }
      return state;
    }

  apr_pool_t* get_root_pool() const noexcept
    {
      return root_pool;
    }

private:
  // Thou shalt not create global_states other than through the factory.
  global_state();

  apr_pool_t* root_pool{nullptr};

  static std::mutex guard;
  static weak_ptr self;
};

} // namespace detail
} // namespace svnxx
} // namespace subversion
} // namespace apache

#endif // SVNXX_PRIVATE_INIT_HPP
