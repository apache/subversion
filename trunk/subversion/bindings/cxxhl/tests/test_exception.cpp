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

#include <algorithm>
#include <cstdio>
#include <iomanip>
#include <ios>
#include <iostream>

#include "svncxxhl.hpp"
#include "../src/private.hpp"

#include <apr.h>
#include "svn_error.h"
#undef TRUE
#undef FALSE

#include <gmock/gmock.h>

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

TEST(Exceptions, CatchError)
{
  try
    {
      SVN::detail::checked_call(make_error_test_error());
    }
  catch (const SVN::Error& err)
    {
      SVN::Error::MessageList ml = err.messages();
      EXPECT_EQ(3, ml.size());
      EXPECT_EQ(SVN_ERR_UNSUPPORTED_FEATURE, ml[0].code());
      EXPECT_EQ(SVN_ERR_BASE, ml[1].code());
      EXPECT_EQ(SVN_ERR_TEST_FAILED, ml[2].code());

      SVN::Error::MessageList tml = err.traced_messages();
#ifdef SVN_DEBUG
      EXPECT_EQ(8, tml.size());
      EXPECT_EQ(SVN_ERR_UNSUPPORTED_FEATURE, tml[0].code());
      EXPECT_EQ(SVN_ERR_UNSUPPORTED_FEATURE, tml[1].code());
      EXPECT_EQ(SVN_ERR_UNSUPPORTED_FEATURE, tml[2].code());
      EXPECT_EQ(SVN_ERR_BASE, tml[3].code());
      EXPECT_EQ(SVN_ERR_BASE, tml[4].code());
      EXPECT_EQ(SVN_ERR_BASE, tml[5].code());
      EXPECT_EQ(SVN_ERR_TEST_FAILED, tml[6].code());
      EXPECT_EQ(SVN_ERR_TEST_FAILED, tml[7].code());
#else  // !SVN_DEBUG
      EXPECT_EQ(3, tml.size());
      EXPECT_EQ(SVN_ERR_UNSUPPORTED_FEATURE, tml[0].code());
      EXPECT_EQ(SVN_ERR_BASE, tml[1].code());
      EXPECT_EQ(SVN_ERR_TEST_FAILED, tml[2].code());
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

TEST(Exceptions, CatchCancelled)
{
  try
    {
      SVN::detail::checked_call(make_cancel_test_error());
    }
  catch (const SVN::Cancelled& err)
    {
      SVN::Error::MessageList ml = err.messages();
      EXPECT_EQ(3, ml.size());
      EXPECT_EQ(SVN_ERR_BASE, ml[0].code());
      EXPECT_EQ(SVN_ERR_TEST_FAILED, ml[1].code());
      EXPECT_EQ(SVN_ERR_CANCELLED, ml[2].code());

      SVN::Error::MessageList tml = err.traced_messages();
#ifdef SVN_DEBUG
      EXPECT_EQ(8, tml.size());
      EXPECT_EQ(SVN_ERR_BASE, tml[0].code());
      EXPECT_EQ(SVN_ERR_BASE, tml[1].code());
      EXPECT_EQ(SVN_ERR_BASE, tml[2].code());
      EXPECT_EQ(SVN_ERR_TEST_FAILED, tml[3].code());
      EXPECT_EQ(SVN_ERR_TEST_FAILED, tml[4].code());
      EXPECT_EQ(SVN_ERR_CANCELLED, tml[5].code());
      EXPECT_EQ(SVN_ERR_CANCELLED, tml[6].code());
      EXPECT_EQ(SVN_ERR_CANCELLED, tml[7].code());
#else  // !SVN_DEBUG
      EXPECT_EQ(3, tml.size());
      EXPECT_EQ(SVN_ERR_BASE, tml[0].code());
      EXPECT_EQ(SVN_ERR_TEST_FAILED, tml[1].code());
      EXPECT_EQ(SVN_ERR_CANCELLED, tml[2].code());
#endif // SVN_DEBUG
    }
}
