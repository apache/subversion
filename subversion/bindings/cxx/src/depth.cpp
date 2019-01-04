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

#include "private/depth_private.hpp"
#include "private/strings_private.hpp"

#include "svn_types.h"

namespace apache {
namespace subversion {
namespace svnxx {

std::string to_string(depth d)
{
  return std::string(svn_depth_to_word(impl::convert(d)));
}

std::wstring to_wstring(depth d)
{
  return impl::convert<wchar_t>(svn_depth_to_word(impl::convert(d)));
}

std::u16string to_u16string(depth d)
{
  return impl::convert<char16_t>(svn_depth_to_word(impl::convert(d)));
}

std::u32string to_u32string(depth d)
{
  return impl::convert<char32_t>(svn_depth_to_word(impl::convert(d)));
}

} // namespace svnxx
} // namespace subversion
} // namespace apache
