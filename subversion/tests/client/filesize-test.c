/* filesize-test.c --- tests for svn_cl__format_file_size
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

#include "../../svn/filesize.c"

#include "../svn_test.h"

typedef struct test_data_t
{
  svn_filesize_t size;
  const char* result;
} test_data_t;


static svn_error_t *
test_base2_file_size(apr_pool_t *pool)
{
  static const test_data_t data[] =
    {
      {APR_INT64_C(                  1),   "1 B"},
      {APR_INT64_C(                  9),   "9 B"},
      {APR_INT64_C(                 13),  "13 B"},
      {APR_INT64_C(                999), "999 B"},
      {APR_INT64_C(               1000), "1.0 KiB"},
      {APR_INT64_C(               1024), "1.0 KiB"},
      {APR_INT64_C(               3000), "2.9 KiB"},
      {APR_INT64_C(            1000000), "977 KiB"},
      {APR_INT64_C(            1048576), "1.0 MiB"},
      {APR_INT64_C(         1000000000), "954 MiB"},
      {APR_INT64_C(      1000000000000), "931 GiB"},
      {APR_INT64_C(   1000000000000000), "909 TiB"},
      {APR_INT64_C(1000000000000000000), "888 EiB"},
      {APR_INT64_C(9223372036854775807), "8.0 PiB"},
    };
  static const apr_size_t data_size = sizeof(data) / sizeof(data[0]);

  apr_size_t index;
  for (index = 0; index < data_size; ++index)
    {
      const char *result;
      SVN_ERR(svn_cl__format_file_size(&result, data[index].size,
                                       SVN_CL__SIZE_UNIT_BASE_2,
                                       TRUE, pool));
      SVN_TEST_STRING_ASSERT(result, data[index].result);
      /* fprintf(stderr, "%s\t%" APR_INT64_T_FMT "\n", result, data[index].size); */
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
test_base10_file_size(apr_pool_t *pool)
{
  static const test_data_t data[] =
    {
      {APR_INT64_C(                  1),   "1 B"},
      {APR_INT64_C(                  9),   "9 B"},
      {APR_INT64_C(                 13),  "13 B"},
      {APR_INT64_C(                999), "999 B"},
      {APR_INT64_C(               1000), "1.0 kB"},
      {APR_INT64_C(               3000), "3.0 kB"},
      {APR_INT64_C(             999499), "999 kB"},
      {APR_INT64_C(             999501), "1.0 MB"},
      {APR_INT64_C(            1000000), "1.0 MB"},
      {APR_INT64_C(            9900000), "9.9 MB"},
      {APR_INT64_C(            9950001),  "10 MB"},
      {APR_INT64_C(           99400001),  "99 MB"},
      {APR_INT64_C(           99500001), "100 MB"},
      {APR_INT64_C(          999444444), "999 MB"},
      {APR_INT64_C(          999999999), "1.0 GB"},
      {APR_INT64_C(         1000000000), "1.0 GB"},
      {APR_INT64_C(         1100000000), "1.1 GB"},
      {APR_INT64_C(      1000000000000), "1.0 TB"},
      {APR_INT64_C(   1000000000000000), "1.0 EB"},
      {APR_INT64_C( 999000000000000000), "999 EB"},
      {APR_INT64_C( 999500000000000000), "1.0 PB"},
      {APR_INT64_C(1000000000000000000), "1.0 PB"},
      {APR_INT64_C(1090000000000000000), "1.1 PB"},
      {APR_INT64_C(9223372036854775807), "9.2 PB"},
    };
  static const apr_size_t data_size = sizeof(data) / sizeof(data[0]);

  apr_size_t index;
  for (index = 0; index < data_size; ++index)
    {
      const char *result;
      SVN_ERR(svn_cl__format_file_size(&result, data[index].size,
                                       SVN_CL__SIZE_UNIT_BASE_10,
                                       TRUE, pool));
      SVN_TEST_STRING_ASSERT(result, data[index].result);
      /* fprintf(stderr, "%s\t%" APR_INT64_T_FMT "\n", result, data[index].size); */
    }

  return SVN_NO_ERROR;
}


/* The test table.  */

static int max_threads = 3;

static struct svn_test_descriptor_t test_funcs[] =
  {
    SVN_TEST_NULL,
    SVN_TEST_PASS2(test_base2_file_size,
                   "base-2 human-friendly file size"),
    SVN_TEST_PASS2(test_base10_file_size,
                   "base-10 human-friendly file size"),
    SVN_TEST_NULL
  };

SVN_TEST_MAIN
