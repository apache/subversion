/*
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
 */

#include <boost/test/unit_test.hpp>

#include "../src/private/depth-private.hpp"

namespace svn = ::apache::subversion::svnxx;
namespace detail = ::apache::subversion::svnxx::detail;

BOOST_AUTO_TEST_SUITE(depth);

BOOST_AUTO_TEST_CASE(convert_to)
{
  BOOST_TEST((detail::convert(svn::depth::unknown)    == svn_depth_unknown));
  BOOST_TEST((detail::convert(svn::depth::exclude)    == svn_depth_exclude));
  BOOST_TEST((detail::convert(svn::depth::empty)      == svn_depth_empty));
  BOOST_TEST((detail::convert(svn::depth::files)      == svn_depth_files));
  BOOST_TEST((detail::convert(svn::depth::immediates) == svn_depth_immediates));
  BOOST_TEST((detail::convert(svn::depth::infinity)   == svn_depth_infinity));
}

BOOST_AUTO_TEST_CASE(convert_from)
{
  BOOST_TEST((detail::convert(svn_depth_unknown)    == svn::depth::unknown));
  BOOST_TEST((detail::convert(svn_depth_exclude)    == svn::depth::exclude));
  BOOST_TEST((detail::convert(svn_depth_empty)      == svn::depth::empty));
  BOOST_TEST((detail::convert(svn_depth_files)      == svn::depth::files));
  BOOST_TEST((detail::convert(svn_depth_immediates) == svn::depth::immediates));
  BOOST_TEST((detail::convert(svn_depth_infinity)   == svn::depth::infinity));
}

BOOST_AUTO_TEST_CASE(char_names)
{
  BOOST_TEST((to_string(svn::depth::unknown)    == "unknown"));
  BOOST_TEST((to_string(svn::depth::exclude)    == "exclude"));
  BOOST_TEST((to_string(svn::depth::empty)      == "empty"));
  BOOST_TEST((to_string(svn::depth::files)      == "files"));
  BOOST_TEST((to_string(svn::depth::immediates) == "immediates"));
  BOOST_TEST((to_string(svn::depth::infinity)   == "infinity"));
}

BOOST_AUTO_TEST_CASE(wchar_names)
{
  BOOST_TEST((to_wstring(svn::depth::unknown)    == L"unknown"));
  BOOST_TEST((to_wstring(svn::depth::exclude)    == L"exclude"));
  BOOST_TEST((to_wstring(svn::depth::empty)      == L"empty"));
  BOOST_TEST((to_wstring(svn::depth::files)      == L"files"));
  BOOST_TEST((to_wstring(svn::depth::immediates) == L"immediates"));
  BOOST_TEST((to_wstring(svn::depth::infinity)   == L"infinity"));
}

BOOST_AUTO_TEST_CASE(char16_names)
{
  BOOST_TEST((to_u16string(svn::depth::unknown)    == u"unknown"));
  BOOST_TEST((to_u16string(svn::depth::exclude)    == u"exclude"));
  BOOST_TEST((to_u16string(svn::depth::empty)      == u"empty"));
  BOOST_TEST((to_u16string(svn::depth::files)      == u"files"));
  BOOST_TEST((to_u16string(svn::depth::immediates) == u"immediates"));
  BOOST_TEST((to_u16string(svn::depth::infinity)   == u"infinity"));
}

BOOST_AUTO_TEST_CASE(char32_names)
{
  BOOST_TEST((to_u32string(svn::depth::unknown)    == U"unknown"));
  BOOST_TEST((to_u32string(svn::depth::exclude)    == U"exclude"));
  BOOST_TEST((to_u32string(svn::depth::empty)      == U"empty"));
  BOOST_TEST((to_u32string(svn::depth::files)      == U"files"));
  BOOST_TEST((to_u32string(svn::depth::immediates) == U"immediates"));
  BOOST_TEST((to_u32string(svn::depth::infinity)   == U"infinity"));
}

BOOST_AUTO_TEST_SUITE_END();
