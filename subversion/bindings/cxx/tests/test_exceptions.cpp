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

#include <algorithm>
#include <cstdio>
#include <iomanip>
#include <ios>
#include <iostream>

#include "svnxx.hpp"
#include "../src/private.hpp"

#include "svn_error.h"

#include "fixture_init.hpp"

BOOST_AUTO_TEST_SUITE(exceptions,
                      * boost::unit_test::fixture<init>());

namespace {
svn_error_t* make_error_test_error()
{
  svn_error_t* err;
  err = svn_error_create(SVN_ERR_TEST_FAILED, NULL, "original message");
  err = svn_error_create(SVN_ERR_BASE, err, "wrapper message");
  err = svn_error_trace(err);
  err = svn_error_create(SVN_ERR_UNSUPPORTED_FEATURE, err, NULL);
  err = svn_error_create(SVN_ERR_UNSUPPORTED_FEATURE, err, NULL);
  err = svn_error_trace(err);
  return err;
}
} // anonymous namespace

BOOST_AUTO_TEST_CASE(catch_error)
{
  BOOST_CHECK_THROW(
      svn::detail::checked_call(make_error_test_error()),
      svn::error);

  try
    {
      svn::detail::checked_call(make_error_test_error());
    }
  catch (const svn::error& err)
    {
      auto ml = err.messages();
      BOOST_TEST(ml.size() == 3);
      BOOST_TEST(ml[0].code() == SVN_ERR_UNSUPPORTED_FEATURE);
      BOOST_TEST(ml[0].name() == "SVN_ERR_UNSUPPORTED_FEATURE");
      BOOST_TEST(ml[1].code() == SVN_ERR_BASE);
      BOOST_TEST(ml[1].name() == "SVN_ERR_BASE");
      BOOST_TEST(ml[1].text() == "wrapper message");
      BOOST_TEST(ml[2].code() == SVN_ERR_TEST_FAILED);
      BOOST_TEST(ml[2].name() == "SVN_ERR_TEST_FAILED");
      BOOST_TEST(ml[2].text() == "original message");

      auto tml = err.traced_messages();
#ifdef SVN_DEBUG
      BOOST_TEST(tml.size() == 8);
      BOOST_TEST(tml[0].code() == SVN_ERR_UNSUPPORTED_FEATURE);
      BOOST_TEST(tml[1].code() == SVN_ERR_UNSUPPORTED_FEATURE);
      BOOST_TEST(tml[2].code() == SVN_ERR_UNSUPPORTED_FEATURE);
      BOOST_TEST(tml[3].code() == SVN_ERR_BASE);
      BOOST_TEST(tml[4].code() == SVN_ERR_BASE);
      BOOST_TEST(tml[5].code() == SVN_ERR_BASE);
      BOOST_TEST(tml[6].code() == SVN_ERR_TEST_FAILED);
      BOOST_TEST(tml[7].code() == SVN_ERR_TEST_FAILED);
#else  // !SVN_DEBUG
      BOOST_TEST(tml.size() == 3);
      BOOST_TEST(tml[0].code() == SVN_ERR_UNSUPPORTED_FEATURE);
      BOOST_TEST(tml[1].code() == SVN_ERR_BASE);
      BOOST_TEST(tml[2].code() == SVN_ERR_TEST_FAILED);
#endif // SVN_DEBUG
    }
}


namespace {
svn_error_t* make_cancel_test_error()
{
  svn_error_t* err;
  err = svn_error_create(SVN_ERR_CANCELLED, NULL, NULL);
  err = svn_error_create(SVN_ERR_CANCELLED, err, NULL);
  err = svn_error_trace(err);
  err = svn_error_create(SVN_ERR_TEST_FAILED, err, "original message");
  err = svn_error_create(SVN_ERR_BASE, err, "wrapper message");
  err = svn_error_trace(err);
  return err;
}
} // anonymous namespace

BOOST_AUTO_TEST_CASE(catch_canceled)
{
  BOOST_CHECK_THROW(
      svn::detail::checked_call(make_cancel_test_error()),
      svn::canceled);

  try
    {
      svn::detail::checked_call(make_cancel_test_error());
    }
  catch (const svn::canceled& err)
    {
      auto ml = err.messages();
      BOOST_TEST(ml.size() == 3);
      BOOST_TEST(ml[0].code() == SVN_ERR_BASE);
      BOOST_TEST(ml[0].name() == "SVN_ERR_BASE");
      BOOST_TEST(ml[0].text() == "wrapper message");
      BOOST_TEST(ml[1].code() == SVN_ERR_TEST_FAILED);
      BOOST_TEST(ml[1].name() == "SVN_ERR_TEST_FAILED");
      BOOST_TEST(ml[1].text() == "original message");
      BOOST_TEST(ml[2].code() == SVN_ERR_CANCELLED);
      BOOST_TEST(ml[2].name() == "SVN_ERR_CANCELLED");

      auto tml = err.traced_messages();
#ifdef SVN_DEBUG
      BOOST_TEST(tml.size() == 8);
      BOOST_TEST(tml[0].code() == SVN_ERR_BASE);
      BOOST_TEST(tml[1].code() == SVN_ERR_BASE);
      BOOST_TEST(tml[2].code() == SVN_ERR_BASE);
      BOOST_TEST(tml[3].code() == SVN_ERR_TEST_FAILED);
      BOOST_TEST(tml[4].code() == SVN_ERR_TEST_FAILED);
      BOOST_TEST(tml[5].code() == SVN_ERR_CANCELLED);
      BOOST_TEST(tml[6].code() == SVN_ERR_CANCELLED);
      BOOST_TEST(tml[7].code() == SVN_ERR_CANCELLED);
#else  // !SVN_DEBUG
      BOOST_TEST(tml.size() == 3);
      BOOST_TEST(tml[0].code() == SVN_ERR_BASE);
      BOOST_TEST(tml[1].code() == SVN_ERR_TEST_FAILED);
      BOOST_TEST(tml[2].code() == SVN_ERR_CANCELLED);
#endif // SVN_DEBUG
    }
}

BOOST_AUTO_TEST_SUITE_END();
