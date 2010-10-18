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

#include "svn_string.h"

#include "Pool.h"
#include "Revision.h"
#include "Common.h"
#include "../Utility.h"

#include <sstream>
#include <string>
#include <vector>

using namespace SVN;

static svn_error_t *
test_pools(apr_pool_t *p)
{
  // We ignore the passed in pool, in favor of creating our own.
  Pool pool;

  void *mem = pool.alloc<void>(1000);
  const char *foo = pool.alloc<const char>(100);

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

static svn_error_t *
test_vector_wrapping(apr_pool_t *p)
{
  Pool pool;
  std::vector<std::string> vec;

  vec.push_back("Mary");
  vec.push_back("had");
  vec.push_back("a");
  vec.push_back("little");
  vec.push_back("lamb");

  apr_array_header_t *arr = Private::Utility::make_string_array(vec, pool);
  SVN_TEST_ASSERT(arr->nelts == vec.size());

  std::vector<std::string>::const_iterator it = vec.begin();
  for (int i = 0; i < arr->nelts; ++i, ++it)
    {
      const char *str = APR_ARRAY_IDX(arr, i, const char *);
      SVN_TEST_ASSERT(*it == str);
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
test_map_wrapping(apr_pool_t *p)
{
  Pool pool;
  PropTable map;

  map["one"] = "the lonliest number\000\001";
  map["two"] = "as bad as one\000\002";

  apr_hash_t *hash = Private::Utility::make_prop_table(map, pool);
  SVN_TEST_ASSERT(apr_hash_count(hash) == map.size());

  PropTable::const_iterator it = map.begin();
  for (apr_hash_index_t *hi = apr_hash_first(pool.pool(), hash); hi;
        hi = apr_hash_next(hi), ++it)
    {
      const char *key = 
                reinterpret_cast<const char *>(svn__apr_hash_index_key(hi));
      const std::string &map_val = it->second;
      svn_string_t *str1 =
                reinterpret_cast<svn_string_t *>(svn__apr_hash_index_val(hi));
      svn_string_t *str2 =
                svn_string_ncreate(map_val.data(), map_val.size(), pool.pool());

      SVN_TEST_ASSERT(svn_string_compare(str1, str2));
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
test_struct_wrapping(apr_pool_t *p)
{
  apr_pool_t *subpool = svn_pool_create(p);
  svn_commit_info_t *commit_info = svn_create_commit_info(subpool);

  commit_info->author = "hwright";

  CommitInfo info(commit_info);
  svn_pool_destroy(subpool);

  SVN_TEST_ASSERT(info.getAuthor() == "hwright");

  CommitInfo info2(info);
  SVN_TEST_ASSERT(info2.getAuthor() == "hwright");

  CommitInfo *info3 = new CommitInfo(info2);
  SVN_TEST_ASSERT(info3->getAuthor() == "hwright");

  CommitInfo *info4 = new CommitInfo(*info3);
  SVN_TEST_ASSERT(info4->getAuthor() == "hwright");

  delete info3;
  SVN_TEST_ASSERT(info.getAuthor() == "hwright");
  SVN_TEST_ASSERT(info4->getAuthor() == "hwright");

  delete info4;

  return SVN_NO_ERROR;
}

static svn_error_t *
test_null_objects(apr_pool_t *p)
{
  // This test is in no way comprehensive

  Lock lock(NULL);
  std::string path;

  if (lock)
    path = lock.getPath();

  Lock l2(lock);

  Lock l3 = l2;

  lock = l2;

  // To bad we can't do something interesting here and check for a segfault...

  return SVN_NO_ERROR;
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
    SVN_TEST_PASS2(test_vector_wrapping,
                   "test various vector to array transforms"),
    SVN_TEST_PASS2(test_map_wrapping,
                   "test various map to hash transforms"),
    SVN_TEST_PASS2(test_struct_wrapping,
                   "test our ref-counted struct wrappers"),
    SVN_TEST_PASS2(test_null_objects,
                   "test wrapping of various NULL objects"),
    SVN_TEST_NULL
  };
