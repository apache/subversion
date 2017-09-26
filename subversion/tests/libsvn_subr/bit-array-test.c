/*
 * bit-array-test.c:  a collection of svn_bit_array__* tests
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
#include "private/svn_subr_private.h"

static svn_error_t *
test_zero_defaults(apr_pool_t *pool)
{
  svn_bit_array__t *array = svn_bit_array__create(0, pool);

  /* Test (default) allocation boundaries */
  SVN_TEST_ASSERT(svn_bit_array__get(array, 0x7ffff) == 0);
  SVN_TEST_ASSERT(svn_bit_array__get(array, 0x80000) == 0);

  /* Test address boundaries */
  SVN_TEST_ASSERT(svn_bit_array__get(array, 0) == 0);
  SVN_TEST_ASSERT(svn_bit_array__get(array, APR_SIZE_MAX) == 0);

  return SVN_NO_ERROR;
}

static svn_error_t *
test_get_set(apr_pool_t *pool)
{
  svn_bit_array__t *array = svn_bit_array__create(0, pool);
  apr_size_t i, min = 0x7ff00, max = 0x7ff00 + 1025;

  /* All values default to 0. */
  for (i = min; i < max; ++i)
    SVN_TEST_ASSERT(svn_bit_array__get(array, i) == 0);

  /* Create a pattern, setting every other bit. Array will also auto-grow. */
  for (i = min; i < max; ++i)
    if (i % 2)
      svn_bit_array__set(array, i, 1);

  /* Verify pattern */
  for (i = min; i < max; ++i)
    {
      if (i % 2)
        SVN_TEST_ASSERT(svn_bit_array__get(array, i) == TRUE);
      else
        SVN_TEST_ASSERT(svn_bit_array__get(array, i) == FALSE);
    }

  /* Zero the zeros in the pattern -> should be no change. */
  for (i = min; i < max; ++i)
    if (i % 2 == 0)
      svn_bit_array__set(array, i, 0);

  /* Verify pattern */
  for (i = min; i < max; ++i)
    {
      if (i % 2)
        SVN_TEST_ASSERT(svn_bit_array__get(array, i) == TRUE);
      else
        SVN_TEST_ASSERT(svn_bit_array__get(array, i) == FALSE);
    }

  /* Write an inverted pattern while verifying the old one. */
  for (i = min; i < max; ++i)
    {
      if (i % 2)
        {
          SVN_TEST_ASSERT(svn_bit_array__get(array, i) == TRUE);
          svn_bit_array__set(array, i, FALSE);
        }
      else
        {
          SVN_TEST_ASSERT(svn_bit_array__get(array, i) == FALSE);
          svn_bit_array__set(array, i, TRUE);
        }
    }

  /* Verify pattern */
  for (i = min; i < max; ++i)
    {
      if (i % 2)
        SVN_TEST_ASSERT(svn_bit_array__get(array, i) == FALSE);
      else
        SVN_TEST_ASSERT(svn_bit_array__get(array, i) == TRUE);
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
test_sparse(apr_pool_t *pool)
{
  svn_bit_array__t *array = svn_bit_array__create(0, pool);
  apr_size_t i, k, min = 0x7ff00, max = 0x7ff00 + 1025, SCALE = 0x10000000;

  /* All values default to 0. */
  for (i = 0; i < 15; ++i)
    for (k = i * SCALE + min; k < i * SCALE +  max; ++k)
      SVN_TEST_ASSERT(svn_bit_array__get(array, k) == 0);

  /* Create a pattern, setting every other bit. Array will also auto-grow. */
  for (i = 0; i < 15; ++i)
    for (k = i * SCALE + min; k < i * SCALE +  max; ++k)
      if (k % 2)
        svn_bit_array__set(array, k, 1);

  /* Verify pattern */
  for (i = 0; i < 15; ++i)
    for (k = i * SCALE + min; k < i * SCALE +  max; ++k)
      {
        if (k % 2)
          SVN_TEST_ASSERT(svn_bit_array__get(array, k) == TRUE);
        else
          SVN_TEST_ASSERT(svn_bit_array__get(array, k) == FALSE);
      }

  return SVN_NO_ERROR;
}

/* An array of all test functions */

static int max_threads = 1;

static struct svn_test_descriptor_t test_funcs[] =
  {
    SVN_TEST_NULL,
    SVN_TEST_PASS2(test_zero_defaults,
                   "check entries to default to zero"),
    SVN_TEST_PASS2(test_get_set,
                   "get / set entries"),
    SVN_TEST_PASS2(test_sparse,
                   "get / set sparse entries"),
    SVN_TEST_NULL
  };

SVN_TEST_MAIN
