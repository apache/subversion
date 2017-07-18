/*
 * compress-test.c:  tests the compression functions.
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

#include "svn_pools.h"
#include "private/svn_subr_private.h"
#include "../svn_test.h"

static svn_error_t *
test_decompress_lz4(apr_pool_t *pool)
{
  const char input[] =
    "\x61\xc0\x61\x61\x61\x61\x62\x62\x62\x62\x63\x63\x63\x63\x0c\x00\x00\x08"
    "\x00\x00\x10\x00\x00\x0c\x00\x08\x08\x00\x00\x18\x00\x00\x14\x00\x00\x08"
    "\x00\x08\x18\x00\x00\x14\x00\x00\x10\x00\x00\x18\x00\x00\x0c\x00\x00\x08"
    "\x00\x00\x10\x00\x90\x61\x61\x61\x61\x62\x62\x62\x62";
  svn_stringbuf_t *decompressed = svn_stringbuf_create_empty(pool);

  SVN_ERR(svn__decompress_lz4(input, sizeof(input), decompressed, 100));
  SVN_TEST_STRING_ASSERT(decompressed->data,
                         "aaaabbbbccccaaaaccccbbbbaaaabbbb"
                         "aaaabbbbccccaaaaccccbbbbaaaabbbb"
                         "aaaabbbbccccaaaaccccbbbbaaaabbbb");

  return SVN_NO_ERROR;
}

static svn_error_t *
test_compress_lz4(apr_pool_t *pool)
{
  const char input[] =
    "aaaabbbbccccaaaaccccbbbbaaaabbbb"
    "aaaabbbbccccaaaaccccbbbbaaaabbbb"
    "aaaabbbbccccaaaaccccbbbbaaaabbbb";
  svn_stringbuf_t *compressed = svn_stringbuf_create_empty(pool);
  svn_stringbuf_t *decompressed = svn_stringbuf_create_empty(pool);

  SVN_ERR(svn__compress_lz4(input, sizeof(input), compressed));
  SVN_ERR(svn__decompress_lz4(compressed->data, compressed->len,
                              decompressed, 100));
  SVN_TEST_STRING_ASSERT(decompressed->data, input);

  return SVN_NO_ERROR;
}

static svn_error_t *
test_compress_lz4_empty(apr_pool_t *pool)
{
  svn_stringbuf_t *compressed = svn_stringbuf_create_empty(pool);
  svn_stringbuf_t *decompressed = svn_stringbuf_create_empty(pool);

  SVN_ERR(svn__compress_lz4("", 0, compressed));
  SVN_ERR(svn__decompress_lz4(compressed->data, compressed->len,
                              decompressed, 100));
  SVN_TEST_STRING_ASSERT(decompressed->data, "");

  return SVN_NO_ERROR;
}

static int max_threads = -1;

static struct svn_test_descriptor_t test_funcs[] =
{
  SVN_TEST_NULL,
  SVN_TEST_PASS2(test_decompress_lz4,
                 "test svn__decompress_lz4()"),
  SVN_TEST_PASS2(test_compress_lz4,
                 "test svn__compress_lz4()"),
  SVN_TEST_PASS2(test_compress_lz4_empty,
                 "test svn__compress_lz4() with empty input"),
  SVN_TEST_NULL
};

SVN_TEST_MAIN
