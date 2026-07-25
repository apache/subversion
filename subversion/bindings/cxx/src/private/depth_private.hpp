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

#ifndef SVNXX_PRIVATE_DEPTH_HPP
#define SVNXX_PRIVATE_DEPTH_HPP

#include <stdexcept>

#include "svnxx/depth.hpp"

namespace apache {
namespace subversion {
namespace svnxx {
namespace impl {

/**
 * Convert @a d to an svn_depth_t.
 */
inline constexpr svn_depth_t convert(depth d)
{
#ifndef SVN_DEBUG
  return svn_depth_t(d);
#else
  // switch in constexpr is allowed in C++14 but not in C++11, so we
  // have to use a series of ternary operators.
  return (d == depth::unknown
          ? (svn_depth_t(d) != svn_depth_unknown
             ? throw std::range_error("convert(svn::depth::unknown)")
             : svn_depth_t(d))
          :
          d == depth::exclude
          ? (svn_depth_t(d) != svn_depth_exclude
             ? throw std::range_error("convert(svn::depth::exclude)")
             : svn_depth_t(d))
          :
          d == depth::empty
          ? (svn_depth_t(d) != svn_depth_empty
             ? throw std::range_error("convert(svn::depth::empty)")
             : svn_depth_t(d))
          :
          d == depth::files
          ? (svn_depth_t(d) != svn_depth_files
             ? throw std::range_error("convert(svn::depth::files)")
             : svn_depth_t(d))
          :
          d == depth::immediates
          ? (svn_depth_t(d) != svn_depth_immediates
             ? throw std::range_error("convert(svn::depth::immediates)")
             : svn_depth_t(d))
          :
          d == depth::infinity
          ? (svn_depth_t(d) != svn_depth_infinity
             ? throw std::range_error("convert(svn::depth::infinity)")
             : svn_depth_t(d))
          :
          throw std::range_error("convert: unknown svn::depth"));
#endif
}

/**
 * Convert @a d to an svn::depth.
 */
inline constexpr depth convert(svn_depth_t d)
{
#ifndef SVN_DEBUG
  return depth(d);
#else
  // switch in constexpr is allowed in C++14 but not in C++11, so we
  // have to use a series of ternary operators.
  return (d == svn_depth_unknown
          ? (d != svn_depth_t(depth::unknown)
             ? throw std::range_error("convert(svn_depth_unknown)")
             : depth(d))
          :
          d == svn_depth_exclude
          ? (d != svn_depth_t(depth::exclude)
             ? throw std::range_error("convert(svn_depth_exclude)")
             : depth(d))
          :
          d == svn_depth_empty
          ? (d != svn_depth_t(depth::empty)
             ? throw std::range_error("convert(svn_depth_empty)")
             : depth(d))
          :
          d == svn_depth_files
          ? (d != svn_depth_t(depth::files)
             ? throw std::range_error("convert(svn_depth_files)")
             : depth(d))
          :
          d == svn_depth_immediates
          ? (d != svn_depth_t(depth::immediates)
             ? throw std::range_error("convert(svn_depth_immediates)")
             : depth(d))
          :
          d == svn_depth_infinity
          ? (d != svn_depth_t(depth::infinity)
             ? throw std::range_error("convert(svn_depth_infinity)")
             : depth(d))
          :
          throw std::range_error("convert: unknown svn_depth_t"));
#endif
}

} // namespace impl
} // namespace svnxx
} // namespace subversion
} // namespace apache

#endif // SVNXX_PRIVATE_DEPTH_HPP
