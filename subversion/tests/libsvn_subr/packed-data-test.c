/*
 * packed-data-test.c:  a collection of svn_packed__* tests
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
#include "private/svn_packed_data.h"

static svn_error_t*
get_read_root(svn_packed__data_root_t **read_root,
              svn_packed__data_root_t *write_root,
              apr_pool_t *pool)
{
  svn_stringbuf_t *stream_buffer = svn_stringbuf_create_empty(pool);
  svn_stream_t *stream;

  stream = svn_stream_from_stringbuf(stream_buffer, pool);
  SVN_ERR(svn_packed__data_write(stream, write_root, pool));
  SVN_ERR(svn_stream_close(stream));

  stream = svn_stream_from_stringbuf(stream_buffer, pool);
  SVN_ERR(svn_packed__data_read(read_root, stream, pool, pool));
  SVN_ERR(svn_stream_close(stream));

  return SVN_NO_ERROR;
}

static svn_error_t *
test_empty_container(apr_pool_t *pool)
{
  /* create an empty, readable container */
  svn_packed__data_root_t *root = svn_packed__data_create_root(pool);
  SVN_ERR(get_read_root(&root, root, pool));

  /* there should be no sub-streams */
  SVN_TEST_ASSERT(svn_packed__first_int_stream(root) == NULL);
  SVN_TEST_ASSERT(svn_packed__first_byte_stream(root) == NULL);

  return SVN_NO_ERROR;
}

/* An array of all test functions */
struct svn_test_descriptor_t test_funcs[] =
  {
    SVN_TEST_NULL,
    SVN_TEST_PASS2(test_empty_container,
                   "test empty container"),
    SVN_TEST_NULL
  };
