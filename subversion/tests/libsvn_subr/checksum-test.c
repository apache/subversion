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

/* Verify that DIGEST of checksum type KIND can be parsed and
 * converted back to a string matching DIGEST.  NAME will be used
 * to identify the type of checksum in error messages.
 */
static svn_error_t *
checksum_parse_kind(const char *digest,
                    svn_checksum_kind_t kind,
                    const char *name,
                    apr_pool_t *pool)
{
  const char *checksum_display;
  svn_checksum_t *checksum;

  SVN_ERR(svn_checksum_parse_hex(&checksum, kind, digest, pool));
  checksum_display = svn_checksum_to_cstring_display(checksum, pool);

  if (strcmp(checksum_display, digest) != 0)
    return svn_error_createf
      (SVN_ERR_CHECKSUM_MISMATCH, NULL,
       "verify-checksum: %s checksum mismatch:\n"
       "   expected:  %s\n"
       "     actual:  %s\n", name, digest, checksum_display);

  return SVN_NO_ERROR;
}

static svn_error_t *
test_checksum_parse(apr_pool_t *pool)
{
  SVN_ERR(checksum_parse_kind("8518b76f7a45fe4de2d0955085b41f98",
                              svn_checksum_md5, "md5", pool));
  SVN_ERR(hecksum_parse_kind("74d82379bcc6771454377db03b912c2b62704139",
                             svn_checksum_sha1, "sha1", pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
test_checksum_empty(apr_pool_t *pool)
{
  svn_checksum_kind_t kind;
  for (kind = svn_checksum_md5; kind <= svn_checksum_sha1; ++kind)
    {
      svn_checksum_t *checksum;
      char data = '\0';

      checksum = svn_checksum_empty_checksum(kind, pool);
      SVN_TEST_ASSERT(svn_checksum_is_empty_checksum(checksum));

      SVN_ERR(svn_checksum(&checksum, kind, &data, 0, pool));
      SVN_TEST_ASSERT(svn_checksum_is_empty_checksum(checksum));
    }

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

/* Verify that "zero" checksums work properly for the given checksum KIND.
 */
static svn_error_t *
zero_match_kind(svn_checksum_kind_t kind, apr_pool_t *pool)
{
  svn_checksum_t *zero;
  svn_checksum_t *A;
  svn_checksum_t *B;

  zero = svn_checksum_create(kind, pool);
  SVN_ERR(svn_checksum_clear(zero));
  SVN_ERR(svn_checksum(&A, kind, "A", 1, pool));
  SVN_ERR(svn_checksum(&B, kind, "B", 1, pool));

  /* Different non-zero don't match. */
  SVN_TEST_ASSERT(!svn_checksum_match(A, B));

  /* Zero matches anything of the same kind. */
  SVN_TEST_ASSERT(svn_checksum_match(A, zero));
  SVN_TEST_ASSERT(svn_checksum_match(zero, B));

  return SVN_NO_ERROR;
}

static svn_error_t *
zero_match(apr_pool_t *pool)
{
  svn_checksum_kind_t kind;
  for (kind = svn_checksum_md5; kind <= svn_checksum_sha1; ++kind)
    SVN_ERR(zero_match_kind(kind, pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
zero_cross_match(apr_pool_t *pool)
{
  svn_checksum_kind_t i_kind;
  svn_checksum_kind_t k_kind;

  for (i_kind = svn_checksum_md5; i_kind <= svn_checksum_sha1; ++i_kind)
    {
      svn_checksum_t *i_zero;
      svn_checksum_t *i_A;
    
      i_zero = svn_checksum_create(i_kind, pool);
      SVN_ERR(svn_checksum_clear(i_zero));
      SVN_ERR(svn_checksum(&i_A, i_kind, "A", 1, pool));

      for (k_kind = svn_checksum_md5; k_kind <= svn_checksum_sha1; ++k_kind)
        {
          svn_checksum_t *k_zero;
          svn_checksum_t *k_A;
          if (i_kind == k_kind)
            continue;

          k_zero = svn_checksum_create(k_kind, pool);
          SVN_ERR(svn_checksum_clear(k_zero));
          SVN_ERR(svn_checksum(&k_A, k_kind, "A", 1, pool));

          /* Different non-zero don't match. */
          SVN_TEST_ASSERT(!svn_checksum_match(i_A, k_A));

          /* Zero doesn't match anything of a different kind... */
          SVN_TEST_ASSERT(!svn_checksum_match(i_zero, k_A));
          SVN_TEST_ASSERT(!svn_checksum_match(i_A, k_zero));

          /* ...even another zero. */
          SVN_TEST_ASSERT(!svn_checksum_match(i_zero, k_zero));
        }
    }

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
    SVN_TEST_PASS2(zero_cross_match,
                   "zero checksum cross-type matching"),
    SVN_TEST_NULL
  };
