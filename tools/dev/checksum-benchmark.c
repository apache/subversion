/* checksum-benchmark.c -- benchmark checksum calculation.
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
#include "svn_cmdline.h"
#include "svn_checksum.h"
#include "private/svn_cmdline_private.h"

#include "svn_private_config.h"

#if defined(SVN_CHECKSUM_BACKEND_APR)
#define CHECKSUM_BACKEND "apr"
#elif defined(SVN_CHECKSUM_BACKEND_OPENSSL)
#define CHECKSUM_BACKEND "openssl"
#elif defined(SVN_CHECKSUM_BACKEND_BCRYPT)
#define CHECKSUM_BACKEND "bcrypt"
#elif defined(SVN_CHECKSUM_BACKEND_XDIGEST)
#define CHECKSUM_BACKEND "xdigest"
#else
#error Cannot determine checksum backend.
#endif

static apr_uint32_t
test_rand(apr_uint32_t *seed)
{
  *seed = (*seed * 1103515245UL + 12345UL) & 0xffffffffUL;
  return *seed;
}

static svn_error_t *
do_bench_test(apr_size_t blocksize, svn_checksum_kind_t kind, apr_pool_t *pool)
{
  svn_checksum_ctx_t *ctx = svn_checksum_ctx_create(kind, pool);
  svn_checksum_t *checksum;
  char *buf = apr_palloc(pool, blocksize);
  apr_time_t start;
  apr_size_t count = 0;
  apr_uint32_t seed = 67;
  apr_size_t i;
  apr_interval_time_t elapsed;

  for (i = 0; i < blocksize; i++)
    buf[i] = (char)test_rand(&seed);

  start = apr_time_now();

  do
    {
      SVN_ERR(svn_checksum_update(ctx, buf, blocksize));
      count++;
      elapsed = apr_time_now() - start;
    }
  while (elapsed < apr_time_from_sec(1));

  SVN_ERR(svn_checksum_final(&checksum, ctx, pool));

  {
    apr_size_t bytes_in_gb = 1024 * 1024 * 1024;
    double elapsed_sec = (double)elapsed / apr_time_from_sec(1);
    double gb_per_sec =
        (double)count * blocksize / (bytes_in_gb * elapsed_sec);

    /* Calling svn_checksum_serialize() is the simplest way to stringify
     * checksum kind yet, although it also includes extra information such as
     * the digest itself. */
    const char *checksum_str = svn_checksum_serialize(checksum, pool, pool);

    SVN_ERR(svn_cmdline_fprintf(stdout, pool,
                                "%s: processed "
                                "%" APR_SIZE_T_FMT " blocks of "
                                "%" APR_SIZE_T_FMT " bytes in "
                                "%" APR_TIME_T_FMT "ms (%.2f GB/s)\n",
                                checksum_str, count, blocksize,
                                apr_time_as_msec(elapsed), gb_per_sec));
  }

  return SVN_NO_ERROR;
}

static svn_error_t *
test_checksum_performance(apr_pool_t *pool)
{
  SVN_ERR(do_bench_test(/* 16 KB */ 16 * (1 << 10), svn_checksum_sha1, pool));
  SVN_ERR(do_bench_test(/* 1 MB  */ 1 * (1 << 20), svn_checksum_sha1, pool));
  SVN_ERR(do_bench_test(/* 64 MB */ 64 * (1 << 20), svn_checksum_sha1, pool));
  SVN_ERR(do_bench_test(/* 16 B  */ 16, svn_checksum_sha1, pool));

  SVN_ERR(do_bench_test(/* 16 KB */ 16 * (1 << 10), svn_checksum_md5, pool));
  SVN_ERR(do_bench_test(/* 1 MB  */ 1 * (1 << 20), svn_checksum_md5, pool));
  SVN_ERR(do_bench_test(/* 64 MB */ 64 * (1 << 20), svn_checksum_md5, pool));
  SVN_ERR(do_bench_test(/* 16 B  */ 16, svn_checksum_md5, pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
sub_main(int *exit_code,
         int argc,
         const svn_cmdline__argv_char_t *cmdline_argv[],
         apr_pool_t *pool)
{
  SVN_ERR(svn_cmdline_fputs("Checksum backend: " CHECKSUM_BACKEND "\n",
                            stdout, pool));

  SVN_ERR(test_checksum_performance(pool));

  return SVN_NO_ERROR;
}

int
SVN_CMDLINE__MAIN(int argc, const svn_cmdline__argv_char_t *argv[])
{
  apr_pool_t *pool = NULL;
  int exit_code = EXIT_SUCCESS;
  svn_error_t *err;

  /* Initialize the app. */
  if (svn_cmdline_init("checksum-benchmark", stderr) != EXIT_SUCCESS)
    return EXIT_FAILURE;

  pool = svn_pool_create(NULL);

  err = sub_main(&exit_code, argc, argv, pool);

  /* Flush stdout and report if it fails. It would be flushed on exit anyway
     but this makes sure that output is not silently lost if it fails. */
  err = svn_error_compose_create(err, svn_cmdline_fflush(stdout));

  if (err)
    {
      return svn_cmdline_handle_exit_error(err, pool, "checksum-benchmark: ");
    }

  return exit_code;
}
