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

#include "../src/private/init_private.hpp"

namespace svn = ::apache::subversion::svnxx;
namespace detail = ::apache::subversion::svnxx::detail;

BOOST_AUTO_TEST_SUITE(init);

BOOST_AUTO_TEST_CASE(context_with_init)
{
  svn::init svnxx_initialized;
  BOOST_TEST(detail::context::get());
}

BOOST_AUTO_TEST_CASE(context_without_init)
{
  BOOST_CHECK_THROW(detail::context::get(), std::logic_error);
}

BOOST_AUTO_TEST_CASE(init_scope)
{
  {
    svn::init svnxx_initialized;
    BOOST_TEST(detail::context::get());
  }
  BOOST_CHECK_THROW(detail::context::get(), std::logic_error);
}

BOOST_AUTO_TEST_CASE(multi_init_same_context)
{
  svn::init svnxx_initialized_first;
  const auto ctx = detail::context::get();
  BOOST_REQUIRE(ctx);

  {
    svn::init svnxx_initialized_second;
    BOOST_TEST(ctx == detail::context::get());
  }

  BOOST_TEST(ctx == detail::context::get());
}

BOOST_AUTO_TEST_SUITE_END();
