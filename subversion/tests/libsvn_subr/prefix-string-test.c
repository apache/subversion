/*
 * prefix-string-test.c:  a collection of svn_prefix_string__* tests
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

/* ====================================================================
   To add tests, look toward the bottom of this file.

*/



#include <stdio.h>
#include <string.h>
#include <apr_pools.h>

#include "../svn_test.h"

#include "svn_error.h"
#include "svn_string.h"   /* This includes <apr_*.h> */
#include "private/svn_string_private.h"

static svn_error_t *
test_empty_string(apr_pool_t *pool)
{
  svn_prefix_tree__t *tree = svn_prefix_tree__create(pool);
  svn_prefix_string__t *empty = svn_prefix_string__create(tree, "");

  /* same instance for all strings of the same value */
  SVN_TEST_ASSERT(empty == svn_prefix_string__create(tree, ""));

  /* does it actually have the right contents? */
  SVN_TEST_ASSERT(svn_prefix_string__expand(empty, pool)->len == 0);
  SVN_TEST_STRING_ASSERT(svn_prefix_string__expand(empty, pool)->data, "");

  /* strings shall be equal to themselves */
  SVN_TEST_ASSERT(0 == svn_prefix_string__compare(empty, empty));

  return SVN_NO_ERROR;
}

enum {TEST_CASE_COUNT = 9};

static const char *test_cases[TEST_CASE_COUNT] =
{
  "a longish string of sorts, longer than 7 anyway",
  "some other string",
  "more stuff on root",
  "some shorter string",
  "some short string",
  "some short str",
  "some short str2",
  "a longish string of sorts, longer than ?! anyway",
  "a"
};

static svn_error_t *
test_string_creation(apr_pool_t *pool)
{
  svn_prefix_tree__t *tree = svn_prefix_tree__create(pool);
  svn_prefix_string__t *strings[TEST_CASE_COUNT];
  int i;

  /* create strings and remember their initial references */
  for (i = 0; i < TEST_CASE_COUNT; ++i)
    strings[i] = svn_prefix_string__create(tree, test_cases[i]);

  /* doing this again must yield the same pointers */
  for (i = 0; i < TEST_CASE_COUNT; ++i)
    SVN_TEST_ASSERT(strings[i]
                    == svn_prefix_string__create(tree, test_cases[i]));

  /* converting them back to strings must be the initial values */
  for (i = 0; i < TEST_CASE_COUNT; ++i)
    {
      svn_string_t *expanded = svn_prefix_string__expand(strings[i], pool);

      SVN_TEST_ASSERT(expanded->len == strlen(test_cases[i]));
      SVN_TEST_STRING_ASSERT(expanded->data, test_cases[i]);

    }

  return SVN_NO_ERROR;
}

static svn_error_t *
test_string_comparison(apr_pool_t *pool)
{
  svn_prefix_tree__t *tree = svn_prefix_tree__create(pool);
  svn_prefix_string__t *strings[TEST_CASE_COUNT];
  int i, k;

  /* create strings */
  for (i = 0; i < TEST_CASE_COUNT; ++i)
    strings[i] = svn_prefix_string__create(tree, test_cases[i]);

  /* comparing them with themselves */
  for (i = 0; i < TEST_CASE_COUNT; ++i)
    SVN_TEST_ASSERT(! svn_prefix_string__compare(strings[i], strings[i]));

  /* compare with all other strings */
  for (i = 0; i < TEST_CASE_COUNT; ++i)
    {
      svn_string_t *lhs = svn_prefix_string__expand(strings[i], pool);
      for (k = 0; k < TEST_CASE_COUNT; ++k)
        {
          svn_string_t *rhs = svn_prefix_string__expand(strings[k], pool);
          int expected_diff = strcmp(lhs->data, rhs->data);
          int actual_diff = svn_prefix_string__compare(strings[i], strings[k]);

          SVN_TEST_ASSERT((actual_diff < 0) == (expected_diff < 0));
          SVN_TEST_ASSERT((actual_diff > 0) == (expected_diff > 0));
          SVN_TEST_ASSERT(!actual_diff == !expected_diff);
        }
    }

  return SVN_NO_ERROR;
}

/* An array of all test functions */

static int max_threads = 1;

static struct svn_test_descriptor_t test_funcs[] =
  {
    SVN_TEST_NULL,
    SVN_TEST_PASS2(test_empty_string,
                   "check empty strings"),
    SVN_TEST_PASS2(test_string_creation,
                   "create many strings"),
    SVN_TEST_PASS2(test_string_comparison,
                   "compare strings"),
    SVN_TEST_NULL
  };

SVN_TEST_MAIN
