/*
 * adler32-test.c:  tests the adler32 implementation.
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

#include <apr.h>
#include <apr_pools.h>
#include <apr_time.h>

#include <zlib.h>

#include "private/svn_adler32.h"
#include "../svn_test.h"


/* Allocate a buffer of size LEN from POOL and fill it with pseudo-random
   data. In fact, the size of the buffer will be rounded up to the next
   multiple of sizeof(apr_uint32_t). */
static const char *make_random_data(apr_uint32_t *initial_seed,
                                    apr_off_t len,
                                    apr_pool_t *pool)
{
  const apr_off_t count = (len /  sizeof(apr_uint32_t)
                           + (len %  sizeof(apr_uint32_t) ? 1 : 0));
  apr_uint32_t *data = apr_palloc(pool, count * sizeof(*data));
  apr_uint32_t seed = *initial_seed = (apr_uint32_t)apr_time_now();
  apr_off_t i;

  for (i = 0; i < count; ++i)
    data[i] = svn_test_rand(&seed);

  return (void*)data;
}


/* NOTE: The following constant arrays of data sizes must
         be sorted in ascending order. */
static const apr_off_t magic[8] = {
  0, 1, 79, 80, 81, 5551, 5552, 5553
};

static const apr_off_t prime[300] = {
  /* first 100 */
  2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37, 41, 43, 47, 53, 59, 61,
  67, 71, 73, 79, 83, 89, 97, 101, 103, 107, 109, 113, 127, 131, 137,
  139, 149, 151, 157, 163, 167, 173, 179, 181, 191, 193, 197, 199, 211,
  223, 227, 229, 233, 239, 241, 251, 257, 263, 269, 271, 277, 281, 283,
  293, 307, 311, 313, 317, 331, 337, 347, 349, 353, 359, 367, 373, 379,
  383, 389, 397, 401, 409, 419, 421, 431, 433, 439, 443, 449, 457, 461,
  463, 467, 479, 487, 491, 499, 503, 509, 521, 523, 541,

  /* 901st..1000th */
  7001, 7013, 7019, 7027, 7039, 7043, 7057, 7069, 7079, 7103,
  7109, 7121, 7127, 7129, 7151, 7159, 7177, 7187, 7193, 7207,
  7211, 7213, 7219, 7229, 7237, 7243, 7247, 7253, 7283, 7297,
  7307, 7309, 7321, 7331, 7333, 7349, 7351, 7369, 7393, 7411,
  7417, 7433, 7451, 7457, 7459, 7477, 7481, 7487, 7489, 7499,
  7507, 7517, 7523, 7529, 7537, 7541, 7547, 7549, 7559, 7561,
  7573, 7577, 7583, 7589, 7591, 7603, 7607, 7621, 7639, 7643,
  7649, 7669, 7673, 7681, 7687, 7691, 7699, 7703, 7717, 7723,
  7727, 7741, 7753, 7757, 7759, 7789, 7793, 7817, 7823, 7829,
  7841, 7853, 7867, 7873, 7877, 7879, 7883, 7901, 7907, 7919,

  /* 9901st..9000th */
  92177, 92179, 92189, 92203, 92219, 92221, 92227, 92233, 92237, 92243,
  92251, 92269, 92297, 92311, 92317, 92333, 92347, 92353, 92357, 92363,
  92369, 92377, 92381, 92383, 92387, 92399, 92401, 92413, 92419, 92431,
  92459, 92461, 92467, 92479, 92489, 92503, 92507, 92551, 92557, 92567,
  92569, 92581, 92593, 92623, 92627, 92639, 92641, 92647, 92657, 92669,
  92671, 92681, 92683, 92693, 92699, 92707, 92717, 92723, 92737, 92753,
  92761, 92767, 92779, 92789, 92791, 92801, 92809, 92821, 92831, 92849,
  92857, 92861, 92863, 92867, 92893, 92899, 92921, 92927, 92941, 92951,
  92957, 92959, 92987, 92993, 93001, 93047, 93053, 93059, 93077, 93083,
  93089, 93097, 93103, 93113, 93131, 93133, 93139, 93151, 93169, 93179

};

static const apr_off_t power2[66] = {
  /* There'll be some overlap with the not-quite-primes.
     That's all right, we won't use the same data. */
#define VALUES_FOR(n) ((n) - 1), (n), ((n) + 1)
#define _64k (64 * 1024)
  VALUES_FOR(2),         VALUES_FOR(4),         VALUES_FOR(8),
  VALUES_FOR(16),        VALUES_FOR(32),        VALUES_FOR(64),
  VALUES_FOR(128),       VALUES_FOR(256),       VALUES_FOR(512),
  VALUES_FOR(_64k / 64), VALUES_FOR(_64k / 32), VALUES_FOR(_64k / 16),
  VALUES_FOR(_64k / 8),  VALUES_FOR(_64k / 4),  VALUES_FOR(_64k / 2),
  VALUES_FOR(_64k),      VALUES_FOR(_64k * 2),  VALUES_FOR(_64k * 4),
  VALUES_FOR(_64k * 8),  VALUES_FOR(_64k * 16), VALUES_FOR(_64k * 32),
  VALUES_FOR(_64k * 64)
#undef _64k
#undef VALUES_FOR
};


static svn_error_t *
do_random_test(const apr_off_t lengths[],
               const apr_size_t array_size,
               apr_pool_t *pool)
{
  apr_uint32_t seed;
  const apr_off_t length = lengths[array_size - 1];
  const char *data = make_random_data(&seed, length, pool);

  apr_uint32_t value_from_svn;
  uLong value_from_zlib;
  int i;

  for (i = 0; i < array_size; ++i)
    {
      /* This will blow up if the lengths array isn't sorted. */
      SVN_TEST_ASSERT(lengths[i] <= length);

      value_from_svn = svn__adler32(0, data, lengths[i]);
      value_from_zlib = adler32(0, (const Bytef*)data, (uInt)lengths[i]);
      if (value_from_svn != value_from_zlib)
        {
          fprintf(stderr, "SEED: %lu\n", (unsigned long)seed);
          SVN_TEST_ASSERT(value_from_svn == value_from_zlib);
        }
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
test_magic_length(apr_pool_t *pool)
{
  return do_random_test(magic, sizeof(magic) / sizeof(magic[0]), pool);
}

static svn_error_t *
test_prime_length(apr_pool_t *pool)
{
  return do_random_test(prime, sizeof(prime) / sizeof(prime[0]), pool);
}

static svn_error_t *
test_power2_length(apr_pool_t *pool)
{
  return do_random_test(power2, sizeof(power2) / sizeof(power2[0]), pool);
}


/* Insert a static implementation of svn__adler32() with the maximum data
   size set low enough that we can test the rarely used large-block branch
   of the code without allocating too many terabytes of memory. */
#define SVN__ADLER32_STATIC static
#define svn__adler32 local_adler32
#define SVN__ADLER32_SIZE_MAX (256U * 256U - 1U) /* 2^16 - 1 = 0xFFFF */
#include "../../libsvn_subr/adler32.c"
#undef svn__adler32
#undef SVN__ADLER32_STATIC

static svn_error_t *
test_large_size(apr_pool_t *pool)
{
  apr_uint32_t seed;
  const svn__adler32_size_t length = 256 * 256 * 256; /* 2^24 = 16 MiB */
  const char *data = make_random_data(&seed, length, pool);

  apr_uint32_t value_from_svn;
  uLong value_from_zlib;

  SVN_TEST_ASSERT(length > 256 * SVN__ADLER32_SIZE_MAX);

  value_from_svn = local_adler32(0, data, length);
  value_from_zlib = svn__adler32_fn(0, data, length);

  if (value_from_svn != value_from_zlib)
    {
      fprintf(stderr, "SEED: %lu\n", (unsigned long)seed);
      SVN_TEST_ASSERT(value_from_svn == value_from_zlib);
    }

  return SVN_NO_ERROR;
}


/* An array of all test functions */

static int max_threads = 4;

static struct svn_test_descriptor_t test_funcs[] =
  {
    SVN_TEST_NULL,
    SVN_TEST_PASS2(test_magic_length,
                   "adler32 with magic length"),
    SVN_TEST_PASS2(test_prime_length,
                   "adler32 with prime length"),
    SVN_TEST_PASS2(test_power2_length,
                   "adler32 with 2^n length"),
    SVN_TEST_PASS2(test_large_size,
                   "adler32 with data size > block size"),
    SVN_TEST_NULL
  };

SVN_TEST_MAIN
