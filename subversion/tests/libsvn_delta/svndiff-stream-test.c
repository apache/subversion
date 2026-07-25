/*
 * svndiff-stream-test.c:  test svndiff streams
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

#include "svn_delta.h"
#include "../svn_test.h"

static svn_error_t *
null_window(svn_txdelta_window_t **window,
            void *baton, apr_pool_t *pool)
{
  *window = NULL;
  return SVN_NO_ERROR;
}

static svn_error_t *
test_txdelta_to_svndiff_stream_small_reads(apr_pool_t *pool)
{
  svn_txdelta_stream_t *txstream;
  svn_stream_t *svndiff_stream;
  char buf[64];
  apr_size_t len;

  txstream = svn_txdelta_stream_create(NULL, null_window, NULL, pool);
  svndiff_stream = svn_txdelta_to_svndiff_stream(txstream, 0, 0, pool);

  len = 3;
  SVN_ERR(svn_stream_read_full(svndiff_stream, buf, &len));
  SVN_TEST_INT_ASSERT((int) len, 3);
  SVN_TEST_ASSERT(memcmp(buf, "SVN", len) == 0);

  len = 1;
  SVN_ERR(svn_stream_read_full(svndiff_stream, buf, &len));
  SVN_TEST_INT_ASSERT((int) len, 1);
  SVN_TEST_ASSERT(memcmp(buf, "\x00", len) == 0);

  /* Test receiving the EOF. */
  len = sizeof(buf);
  SVN_ERR(svn_stream_read_full(svndiff_stream, buf, &len));
  SVN_TEST_INT_ASSERT((int) len, 0);

  /* Test reading after the EOF. */
  len = sizeof(buf);
  SVN_ERR(svn_stream_read_full(svndiff_stream, buf, &len));
  SVN_TEST_INT_ASSERT((int) len, 0);

  return SVN_NO_ERROR;
}

static int max_threads = -1;

static struct svn_test_descriptor_t test_funcs[] =
{
  SVN_TEST_NULL,
  SVN_TEST_PASS2(test_txdelta_to_svndiff_stream_small_reads,
                 "test svn_txdelta_to_svndiff_stream() small reads"),
  SVN_TEST_NULL
};

SVN_TEST_MAIN
