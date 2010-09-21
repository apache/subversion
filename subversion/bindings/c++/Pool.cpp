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
 * @file Pool.cpp
 * @brief Implementation of the class Pool
 */

#include "Pool.h"
#include "Core.h"

#include "svn_pools.h"

namespace SVN {

Pool::Pool()
{
  m_pool = svn_pool_create(SVN::Private::Core::getCore()->getGlobalPool());
}

Pool::Pool(SVN::Pool &parent)
{
  m_pool = svn_pool_create(parent.pool());
}

void *
Pool::alloc(apr_size_t sz)
{
  return apr_palloc(m_pool, sz);
}

Pool::~Pool()
{
  if (m_pool)
    svn_pool_destroy(m_pool);
}

}
