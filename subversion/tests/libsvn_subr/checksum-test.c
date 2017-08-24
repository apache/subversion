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

#include <zlib.h>

#include "svn_error.h"
#include "svn_io.h"

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
  SVN_ERR(checksum_parse_kind("74d82379bcc6771454377db03b912c2b62704139",
                              svn_checksum_sha1, "sha1", pool));
  SVN_ERR(checksum_parse_kind("deadbeef",
                              svn_checksum_fnv1a_32, "fnv-1a", pool));
  SVN_ERR(checksum_parse_kind("cafeaffe",
                              svn_checksum_fnv1a_32x4,
                              "modified fnv-1a", pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
test_checksum_empty(apr_pool_t *pool)
{
  svn_checksum_kind_t kind;
  for (kind = svn_checksum_md5; kind <= svn_checksum_fnv1a_32x4; ++kind)
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
  for (kind = svn_checksum_md5; kind <= svn_checksum_fnv1a_32x4; ++kind)
    SVN_ERR(zero_match_kind(kind, pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
zero_cross_match(apr_pool_t *pool)
{
  svn_checksum_kind_t i_kind;
  svn_checksum_kind_t k_kind;

  for (i_kind = svn_checksum_md5;
       i_kind <= svn_checksum_fnv1a_32x4;
       ++i_kind)
    {
      svn_checksum_t *i_zero;
      svn_checksum_t *i_A;

      i_zero = svn_checksum_create(i_kind, pool);
      SVN_ERR(svn_checksum_clear(i_zero));
      SVN_ERR(svn_checksum(&i_A, i_kind, "A", 1, pool));

      for (k_kind = svn_checksum_md5;
           k_kind <= svn_checksum_fnv1a_32x4;
           ++k_kind)
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

static svn_error_t *
zlib_expansion_test(const svn_test_opts_t *opts,
                    apr_pool_t *pool)
{
  const char *data_path;
  const char *srcdir;
  svn_stringbuf_t *deflated;
  Byte dst_buffer[256 * 1024];
  Byte *src_buffer;
  uInt sz;

  SVN_ERR(svn_test_get_srcdir(&srcdir, opts, pool));
  data_path = svn_dirent_join(srcdir, "zlib.deflated", pool);

  SVN_ERR(svn_stringbuf_from_file2(&deflated, data_path, pool));
  src_buffer = (Byte*)deflated->data;

  /* Try to decompress the same data with different blocksizes */
  for (sz = 1; sz < 256; sz++)
    {
      z_stream stream;
      uLong crc = crc32(0, Z_NULL, 0);
      memset(&stream, 0, sizeof(stream));
      inflateInit2(&stream, -15 /* DEFLATE_WINDOW_SIZE */);

      stream.avail_in = sz;
      stream.next_in = src_buffer;
      stream.avail_out = sizeof(dst_buffer);
      stream.next_out = dst_buffer;

      do
        {
          int zr = inflate(&stream, Z_NO_FLUSH);

          if (zr != Z_OK && zr != Z_STREAM_END)
          {
              return svn_error_createf(
                          SVN_ERR_TEST_FAILED, NULL,
                          "Failure decompressing with blocksize %u", sz);
          }
          crc = crc32(crc, dst_buffer, sizeof(dst_buffer) - stream.avail_out);
          stream.avail_out = sizeof(dst_buffer);
          stream.next_out = dst_buffer;
          stream.avail_in += sz;
      } while (stream.next_in + stream.avail_in  < src_buffer + deflated->len);

      stream.avail_in = (uInt) (deflated->len - stream.total_in);

      {
          int zr = inflate(&stream, Z_NO_FLUSH);

          if (zr != Z_STREAM_END)
            {
              return svn_error_createf(
                        SVN_ERR_TEST_FAILED, NULL,
                        "Final flush failed with blocksize %u", sz);
            }
          crc = crc32(crc, dst_buffer, sizeof(dst_buffer) - stream.avail_out);

          zr = inflateEnd(&stream);

          if (zr != Z_OK)
            {
              return svn_error_createf(
                        SVN_ERR_TEST_FAILED, NULL,
                        "End of stream handling failed with blocksize %u",
                        sz);
            }
      }

      if (stream.total_out != 242014 || crc != 0x8f03d934)
        {
          return svn_error_createf(
              SVN_ERR_TEST_FAILED, NULL,
              "Decompressed data doesn't match expected size or crc with "
              "blocksize %u: Found crc32=0x%08lx, size=%lu.\n"
              "Verify your ZLib installation, as this should never happen",
              sz, crc, stream.total_out);
        }
  }

  return SVN_NO_ERROR;
}

static svn_error_t *
test_serialization(apr_pool_t *pool)
{
  svn_checksum_kind_t kind;
  for (kind = svn_checksum_md5; kind <= svn_checksum_fnv1a_32x4; ++kind)
    {
      const svn_checksum_t *parsed_checksum;
      svn_checksum_t *checksum = svn_checksum_empty_checksum(kind, pool);
      const char *serialized = svn_checksum_serialize(checksum, pool, pool);

      SVN_ERR(svn_checksum_deserialize(&parsed_checksum, serialized, pool,
                                       pool));

      SVN_TEST_ASSERT(parsed_checksum->kind == kind);
      SVN_TEST_ASSERT(svn_checksum_match(checksum, parsed_checksum));
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
test_checksum_parse_all_zero(apr_pool_t *pool)
{
  svn_checksum_kind_t kind;
  for (kind = svn_checksum_md5; kind <= svn_checksum_fnv1a_32x4; ++kind)
    {
      svn_checksum_t *checksum;
      const char *hex;

      checksum = svn_checksum_create(kind, pool);

      hex = svn_checksum_to_cstring_display(checksum, pool);
      SVN_ERR(svn_checksum_parse_hex(&checksum, kind, hex, pool));

      /* All zeroes checksum is NULL by definition. See
         svn_checksum_parse_hex().*/
      SVN_TEST_ASSERT(checksum == NULL);
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
test_checksummed_stream_read(apr_pool_t *pool)
{
  const svn_string_t *str = svn_string_create("abcde", pool);
  svn_checksum_kind_t kind;

  for (kind = svn_checksum_md5; kind <= svn_checksum_fnv1a_32x4; ++kind)
    {
      svn_stream_t *stream;
      svn_checksum_t *expected_checksum;
      svn_checksum_t *actual_checksum;
      char buf[64];
      apr_size_t len;

      stream = svn_stream_from_string(str, pool);
      stream = svn_stream_checksummed2(stream, &actual_checksum, NULL,
                                       kind, TRUE, pool);
      len = str->len;
      SVN_ERR(svn_stream_read_full(stream, buf, &len));
      SVN_TEST_INT_ASSERT((int) len, str->len);

      SVN_ERR(svn_stream_close(stream));

      SVN_ERR(svn_checksum(&expected_checksum, kind,
                           str->data, str->len, pool));
      SVN_TEST_ASSERT(svn_checksum_match(expected_checksum, actual_checksum));
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
test_checksummed_stream_reset(apr_pool_t *pool)
{
  const svn_string_t *str = svn_string_create("abcde", pool);
  svn_checksum_kind_t kind;

  for (kind = svn_checksum_md5; kind <= svn_checksum_fnv1a_32x4; ++kind)
    {
      svn_stream_t *stream;
      svn_checksum_t *expected_checksum;
      svn_checksum_t *actual_checksum;
      char buf[64];
      apr_size_t len;

      stream = svn_stream_from_string(str, pool);
      stream = svn_stream_checksummed2(stream, &actual_checksum, NULL,
                                       kind, TRUE, pool);
      len = str->len;
      SVN_ERR(svn_stream_read_full(stream, buf, &len));
      SVN_TEST_INT_ASSERT((int) len, str->len);

      SVN_ERR(svn_stream_reset(stream));

      len = str->len;
      SVN_ERR(svn_stream_read_full(stream, buf, &len));
      SVN_TEST_INT_ASSERT((int) len, str->len);

      SVN_ERR(svn_stream_close(stream));

      SVN_ERR(svn_checksum(&expected_checksum, kind,
                           str->data, str->len, pool));
      SVN_TEST_ASSERT(svn_checksum_match(expected_checksum, actual_checksum));
    }

  return SVN_NO_ERROR;
}

/* An array of all test functions */

static int max_threads = 1;

static struct svn_test_descriptor_t test_funcs[] =
  {
    SVN_TEST_NULL,
    SVN_TEST_PASS2(test_checksum_parse,
                   "checksum parse"),
    SVN_TEST_PASS2(test_checksum_empty,
                   "checksum emptiness"),
    SVN_TEST_PASS2(zero_match,
                   "zero checksum matching"),
    SVN_TEST_OPTS_PASS(zlib_expansion_test,
                       "zlib expansion test (zlib regression)"),
    SVN_TEST_PASS2(zero_cross_match,
                   "zero checksum cross-type matching"),
    SVN_TEST_PASS2(test_serialization,
                   "checksum (de-)serialization"),
    SVN_TEST_PASS2(test_checksum_parse_all_zero,
                   "checksum parse all zero"),
    SVN_TEST_PASS2(test_checksummed_stream_read,
                   "read from checksummed stream"),
    SVN_TEST_PASS2(test_checksummed_stream_reset,
                   "reset checksummed stream"),
    SVN_TEST_NULL
  };

SVN_TEST_MAIN
