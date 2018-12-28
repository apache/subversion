/**
 * @file svnxx/init.hpp
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

#ifndef SVNXX_INIT_HPP
#define SVNXX_INIT_HPP

#include <memory>

namespace apache {
namespace subversion {
namespace svnxx {

namespace detail {
// Forward declaration of the private API context.
class context;
} // namespace detail

/**
 * @brief SVN++ initialization.
 *
 * The @c init class takes care of library initialization and
 * teardown and maintains shared (global) internal state. You must
 * create an @c init object before you can use the SVN++ API. It is
 * safe to create create any number of these objects.
 */
class init
{
public:
  init();

private:
  std::shared_ptr<detail::context> context;
};

} // namespace svnxx
} // namespace subversion
} // namespace apache

#endif  // SVNXX_INIT_HPP
