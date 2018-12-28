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

#ifndef __cplusplus
#error "This is a C++ header file."
#endif

#ifndef SVNXX_PRIVATE_TRISTATE_HPP
#define SVNXX_PRIVATE_TRISTATE_HPP

#include "svnxx/tristate.hpp"
#include "svn_types.h"

namespace apache {
namespace subversion {
namespace svnxx {
namespace impl {

/**
 * Converts an @c svn_tristate_t value to a @c tristate value.
 */
inline constexpr tristate convert(svn_tristate_t x)
{
  return (x == svn_tristate_true ? tristate(true)
          : (x == svn_tristate_false ? tristate(false)
             : tristate::unknown()));
}

/**
 * Converts a @c tristate value to an @c svn_tristate_t value.
 */
inline constexpr svn_tristate_t convert(tristate t)
{
  return (t ? svn_tristate_true
          : (!t ? svn_tristate_false : svn_tristate_unknown));
}

} // namespace impl
} // namespace svnxx
} // namespace subversion
} // namespace apache

#endif // SVNXX_PRIVATE_TRISTATE_HPP
