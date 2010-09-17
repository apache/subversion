/*
 * util-test.c -- test the libsvn++ utilities
 *
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

#include "../svn_test.h"

#include "Pool.h"
#include "Revision.h"
#include "Common.h"
#include "../Utility.h"

#include <sstream>

using namespace SVN;

static svn_error_t *
test_pools(apr_pool_t *p)
{
  // We ignore the passed in pool, in favor of creating our own.
  Pool pool;

  void *mem = pool.alloc(1000);

  pool.clear();

  return SVN_NO_ERROR;
}

static svn_error_t *
test_revision(apr_pool_t *p)
{
  SVN_TEST_ASSERT(Revision::HEAD.revision()->kind == svn_opt_revision_head);
  SVN_TEST_ASSERT(Revision::COMMITTED.revision()->kind
                                             == svn_opt_revision_committed);
  SVN_TEST_ASSERT(Revision::PREVIOUS.revision()->kind
                                             == svn_opt_revision_previous);
  SVN_TEST_ASSERT(Revision::BASE.revision()->kind == svn_opt_revision_base);
  SVN_TEST_ASSERT(Revision::WORKING.revision()->kind
                                             == svn_opt_revision_working);

  Revision r1 = Revision::getNumberRev(1);
  svn_opt_revision_t rev;
  rev.kind = svn_opt_revision_number;
  rev.value.number = 1;
  SVN_TEST_ASSERT(r1 == &rev);

  apr_time_t date = 0xdeadbeef;
  Revision rToday = Revision::getDateRev(date);
  rev.kind = svn_opt_revision_date;
  rev.value.date = date;
  SVN_TEST_ASSERT(rToday == &rev);

  return SVN_NO_ERROR;
}

#define TEST_STR "Mary had a little lamb.\n"
static svn_error_t *
test_streams(apr_pool_t *p)
{
  Pool pool;
  std::ostringstream oss;

  svn_stream_t *out = Private::Utility::ostream_wrapper(oss, pool);
  apr_size_t len = strlen(TEST_STR);

  SVN_ERR(svn_stream_write(out, TEST_STR, &len));
  SVN_TEST_ASSERT(oss.str() == TEST_STR);

  return SVN_NO_ERROR;
}

static svn_error_t *
test_exceptions(apr_pool_t *p)
{
  svn_error_t *err = svn_error_create(SVN_ERR_CLIENT_BAD_REVISION, NULL, NULL);

  try
    {
      throw Exception(err);
    }
  catch (Exception ex)
    {
      return SVN_NO_ERROR;
    }

  return svn_error_create(SVN_ERR_TEST_FAILED, NULL, NULL);
}

/* The test table.  */

struct svn_test_descriptor_t test_funcs[] =
  {
    SVN_TEST_NULL,
    SVN_TEST_PASS2(test_pools,
                   "test Pool class"),
    SVN_TEST_PASS2(test_revision,
                   "test Revision class"),
    SVN_TEST_PASS2(test_streams,
                   "test stream wrapping"),
    SVN_TEST_PASS2(test_exceptions,
                   "test error and exception handling"),
    SVN_TEST_NULL
  };
