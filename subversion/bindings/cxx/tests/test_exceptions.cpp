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

//#include <apr.h>
#include "svn_error.h"
#undef TRUE
#undef FALSE

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

BOOST_AUTO_TEST_SUITE(exceptions);

BOOST_AUTO_TEST_CASE(catch_error)
{
  try
    {
      SVN::detail::checked_call(make_error_test_error());
    }
  catch (const SVN::Error& err)
    {
      SVN::Error::MessageList ml = err.messages();
      BOOST_TEST(ml.size() == 3);
      BOOST_TEST(ml[0].code() == SVN_ERR_UNSUPPORTED_FEATURE);
      BOOST_TEST(ml[1].code() == SVN_ERR_BASE);
      BOOST_TEST(ml[2].code() == SVN_ERR_TEST_FAILED);

      SVN::Error::MessageList tml = err.traced_messages();
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
      BOOST_TEST(3, tml.size());
      BOOST_TEST(SVN_ERR_UNSUPPORTED_FEATURE, tml[0].code());
      BOOST_TEST(SVN_ERR_BASE, tml[1].code());
      BOOST_TEST(SVN_ERR_TEST_FAILED, tml[2].code());
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
  try
    {
      SVN::detail::checked_call(make_cancel_test_error());
    }
  catch (const SVN::Cancelled& err)
    {
      SVN::Error::MessageList ml = err.messages();
      BOOST_TEST(ml.size() == 3);
      BOOST_TEST(ml[0].code() == SVN_ERR_BASE);
      BOOST_TEST(ml[1].code() == SVN_ERR_TEST_FAILED);
      BOOST_TEST(ml[2].code() == SVN_ERR_CANCELLED);

      SVN::Error::MessageList tml = err.traced_messages();
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
