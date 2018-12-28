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

#include <stdexcept>

#include "svnxx/depth.hpp"
#include "private.hpp"

namespace apache {
namespace subversion {
namespace svnxx {

std::string to_string(depth d)
{
  return std::string(svn_depth_to_word(detail::convert(d)));
}

std::wstring to_wstring(depth d)
{
  return detail::convert<wchar_t>(svn_depth_to_word(detail::convert(d)));
}

std::u16string to_u16string(depth d)
{
  return detail::convert<char16_t>(svn_depth_to_word(detail::convert(d)));
}

std::u32string to_u32string(depth d)
{
  return detail::convert<char32_t>(svn_depth_to_word(detail::convert(d)));
}

namespace detail {

svn_depth_t convert(depth d)
{
#ifdef SVN_DEBUG
  switch (d)
    {
    case depth::unknown:
      if (svn_depth_t(d) != svn_depth_unknown)
        throw std::range_error("convert svn::depth::unknown");
      break;

    case depth::exclude:
      if (svn_depth_t(d) != svn_depth_exclude)
        throw std::range_error("convert svn::depth::exclude");
      break;

    case depth::empty:
      if (svn_depth_t(d) != svn_depth_empty)
        throw std::range_error("convert svn::depth::empty");
      break;

    case depth::files:
      if (svn_depth_t(d) != svn_depth_files)
        throw std::range_error("convert svn::depth::files");
      break;

    case depth::immediates:
      if (svn_depth_t(d) != svn_depth_immediates)
        throw std::range_error("convert svn::depth::immediates");
      break;

    case depth::infinity:
      if (svn_depth_t(d) != svn_depth_infinity)
        throw std::range_error("convert svn::depth::infinity");
      break;

    default:
      throw std::range_error("unknown svn::depth");
    }
#endif
  return svn_depth_t(d);
}

depth convert(svn_depth_t d)
{
#ifdef SVN_DEBUG
  switch (d)
    {
    case svn_depth_unknown:
      if (d != svn_depth_t(depth::unknown))
        throw std::range_error("convert svn_depth_unknown");
      break;

    case svn_depth_exclude:
      if (d != svn_depth_t(depth::exclude))
        throw std::range_error("convert svn_depth_exclude");
      break;

    case svn_depth_empty:
      if (d != svn_depth_t(depth::empty))
        throw std::range_error("convert svn_depth_empty");
      break;

    case svn_depth_files:
      if (d != svn_depth_t(depth::files))
        throw std::range_error("convert svn_depth_files");
      break;

    case svn_depth_immediates:
      if (d != svn_depth_t(depth::immediates))
        throw std::range_error("convert svn_depth_immediates");
      break;

    case svn_depth_infinity:
      if (d != svn_depth_t(depth::infinity))
        throw std::range_error("convert svn_depth_infinity");
      break;

    default:
      throw std::range_error("unknown svn_depth_t");
    }
#endif
  return depth(d);
}

} // namespace detail
} // namespace svnxx
} // namespace subversion
} // namespace apache
