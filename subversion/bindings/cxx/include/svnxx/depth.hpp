/**
 * @file svnxx/depth.hpp
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

#ifndef SVNXX_DEPTH_HPP
#define SVNXX_DEPTH_HPP

#include "svn_types_impl.h"

#include <cstdint>
#include <string>

namespace apache {
namespace subversion {
namespace svnxx {

/**
 * @brief The concept of depth for directories (see @ref svn_depth_t).
 */
// NOTE: Keep these values identical to those in svn_depth_t!
enum class depth : std::int8_t
  {
    unknown    = svn_depth_unknown,
    exclude    = svn_depth_exclude,
    empty      = svn_depth_empty,
    files      = svn_depth_files,
    immediates = svn_depth_immediates,
    infinity   = svn_depth_infinity,
  };

/**
 * @brief Converts a depth constant to its string representation.
 * @see svn_depth_to_word()
 */
std::string to_string(depth);

/**
 * @brief Converts a depth constant to its wide-string representation.
 * @see svn_depth_to_word()
 */
std::wstring to_wstring(depth);

/**
 * @brief Converts a depth constant to its UTF-16 string representation.
 * @see svn_depth_to_word()
 */
std::u16string to_u16string(depth);

/**
 * @brief Converts a depth constant to its UTF-32 string representation.
 * @see svn_depth_to_word()
 */
std::u32string to_u32string(depth);

} // namespace svnxx
} // namespace subversion
} // namespace apache

#endif  // SVNXX_DEPTH_HPP
