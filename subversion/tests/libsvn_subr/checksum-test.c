/*
 * checksum-test.c:  tests checksum functions.
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

#include <apr_pools.h>

#include "svn_error.h"
#include "private/svn_pseudo_md5.h"

#include "../svn_test.h"

static svn_error_t *
test_checksum_parse(apr_pool_t *pool)
{
  const char *md5_digest = "8518b76f7a45fe4de2d0955085b41f98";
  const char *sha1_digest = "74d82379bcc6771454377db03b912c2b62704139";
  const char *checksum_display;
  svn_checksum_t *checksum;

  SVN_ERR(svn_checksum_parse_hex(&checksum, svn_checksum_md5, md5_digest, pool));
  checksum_display = svn_checksum_to_cstring_display(checksum, pool);

  if (strcmp(checksum_display, md5_digest) != 0)
    return svn_error_createf
      (SVN_ERR_CHECKSUM_MISMATCH, NULL,
       "verify-checksum: md5 checksum mismatch:\n"
       "   expected:  %s\n"
       "     actual:  %s\n", md5_digest, checksum_display);

  SVN_ERR(svn_checksum_parse_hex(&checksum, svn_checksum_sha1, sha1_digest,
                                 pool));
  checksum_display = svn_checksum_to_cstring_display(checksum, pool);

  if (strcmp(checksum_display, sha1_digest) != 0)
    return svn_error_createf
      (SVN_ERR_CHECKSUM_MISMATCH, NULL,
       "verify-checksum: sha1 checksum mismatch:\n"
       "   expected:  %s\n"
       "     actual:  %s\n", sha1_digest, checksum_display);

  return SVN_NO_ERROR;
}

static svn_error_t *
test_checksum_empty(apr_pool_t *pool)
{
  svn_checksum_t *checksum;
  char data = '\0';

  checksum = svn_checksum_empty_checksum(svn_checksum_md5, pool);
  SVN_TEST_ASSERT(svn_checksum_is_empty_checksum(checksum));

  checksum = svn_checksum_empty_checksum(svn_checksum_sha1, pool);
  SVN_TEST_ASSERT(svn_checksum_is_empty_checksum(checksum));

  SVN_ERR(svn_checksum(&checksum, svn_checksum_md5, &data, 0, pool));
  SVN_TEST_ASSERT(svn_checksum_is_empty_checksum(checksum));

  SVN_ERR(svn_checksum(&checksum, svn_checksum_sha1, &data, 0, pool));
  SVN_TEST_ASSERT(svn_checksum_is_empty_checksum(checksum));

  return SVN_NO_ERROR;
}

static svn_error_t *
test_pseudo_md5(apr_pool_t *pool)
{
  apr_uint32_t input[16] = { 0 };
  apr_uint32_t digest_15[4] = { 0 };
  apr_uint32_t digest_31[4] = { 0 };
  apr_uint32_t digest_63[4] = { 0 };
  svn_checksum_t *checksum;

  /* input is all 0s but the hash shall be different
     (due to different input sizes)*/
  svn__pseudo_md5_15(digest_15, input);
  svn__pseudo_md5_31(digest_31, input);
  svn__pseudo_md5_63(digest_63, input);

  SVN_TEST_ASSERT(memcmp(digest_15, digest_31, sizeof(digest_15)));
  SVN_TEST_ASSERT(memcmp(digest_15, digest_63, sizeof(digest_15)));
  SVN_TEST_ASSERT(memcmp(digest_31, digest_63, sizeof(digest_15)));

  /* the checksums shall also be different from "proper" MD5 */
  SVN_ERR(svn_checksum(&checksum, svn_checksum_md5, input, 15, pool));
  SVN_TEST_ASSERT(memcmp(digest_15, checksum->digest, sizeof(digest_15)));

  SVN_ERR(svn_checksum(&checksum, svn_checksum_md5, input, 31, pool));
  SVN_TEST_ASSERT(memcmp(digest_31, checksum->digest, sizeof(digest_15)));

  SVN_ERR(svn_checksum(&checksum, svn_checksum_md5, input, 63, pool));
  SVN_TEST_ASSERT(memcmp(digest_63, checksum->digest, sizeof(digest_15)));

  return SVN_NO_ERROR;
}

static svn_error_t *
zero_match(apr_pool_t *pool)
{
  svn_checksum_t *zero_md5;
  svn_checksum_t *zero_sha1;
  svn_checksum_t *A_md5;
  svn_checksum_t *B_md5;
  svn_checksum_t *A_sha1;
  svn_checksum_t *B_sha1;


  zero_md5 = svn_checksum_create(svn_checksum_md5, pool);
  SVN_ERR(svn_checksum_clear(zero_md5));
  SVN_ERR(svn_checksum(&A_md5, svn_checksum_md5, "A", 1, pool));
  SVN_ERR(svn_checksum(&B_md5, svn_checksum_md5, "B", 1, pool));

  zero_sha1 = svn_checksum_create(svn_checksum_sha1, pool);
  SVN_ERR(svn_checksum_clear(zero_sha1));
  SVN_ERR(svn_checksum(&A_sha1, svn_checksum_sha1, "A", 1, pool));
  SVN_ERR(svn_checksum(&B_sha1, svn_checksum_sha1, "B", 1, pool));

  /* Different non-zero don't match. */
  SVN_TEST_ASSERT(!svn_checksum_match(A_md5, B_md5));
  SVN_TEST_ASSERT(!svn_checksum_match(A_sha1, B_sha1));
  SVN_TEST_ASSERT(!svn_checksum_match(A_md5, A_sha1));
  SVN_TEST_ASSERT(!svn_checksum_match(A_md5, B_sha1));

  /* Zero matches anything of the same kind. */
  SVN_TEST_ASSERT(svn_checksum_match(A_md5, zero_md5));
  SVN_TEST_ASSERT(svn_checksum_match(zero_md5, B_md5));
  SVN_TEST_ASSERT(svn_checksum_match(A_sha1, zero_sha1));
  SVN_TEST_ASSERT(svn_checksum_match(zero_sha1, B_sha1));

  /* Zero doesn't match anything of a different kind... */
  SVN_TEST_ASSERT(!svn_checksum_match(zero_md5, A_sha1));
  SVN_TEST_ASSERT(!svn_checksum_match(zero_sha1, A_md5));
  /* ...even another zero. */
  SVN_TEST_ASSERT(!svn_checksum_match(zero_md5, zero_sha1));

  return SVN_NO_ERROR;
}

/* An array of all test functions */
struct svn_test_descriptor_t test_funcs[] =
  {
    SVN_TEST_NULL,
    SVN_TEST_PASS2(test_checksum_parse,
                   "checksum parse"),
    SVN_TEST_PASS2(test_checksum_empty,
                   "checksum emptiness"),
    SVN_TEST_PASS2(test_pseudo_md5,
                   "pseudo-md5 compatibility"),
    SVN_TEST_PASS2(zero_match,
                   "zero checksum matching"),
    SVN_TEST_NULL
  };
