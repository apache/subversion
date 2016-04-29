/* fs-fs-fuzzy-test.c --- fuzzing tests for the FSFS filesystem
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

#include <stdlib.h>
#include <string.h>
#include <apr_pools.h>

#include "../svn_test.h"
#include "../../libsvn_fs_fs/fs.h"
#include "../../libsvn_fs_fs/fs_fs.h"
#include "../../libsvn_fs_fs/rev_file.h"

#include "svn_hash.h"
#include "svn_pools.h"
#include "svn_props.h"
#include "svn_fs.h"
#include "private/svn_string_private.h"
#include "private/svn_string_private.h"

#include "../svn_test_fs.h"



/*** Helper Functions ***/

/* We won't log or malfunction() upon errors. */
static void
dont_filter_warnings(void *baton, svn_error_t *err)
{
  return;
}


/*** Test core code ***/

/* Verify that a modification of any single byte in REVISION of FS at
 * REPO_NAME using MODIFIER with BATON will be detected. */
static svn_error_t *
fuzzing_1_byte_1_rev(const char *repo_name,
                     svn_fs_t *fs,
                     svn_revnum_t revision,
                     unsigned char (* modifier)(unsigned char c, void *baton),
                     void *baton,
                     apr_pool_t *pool)
{
  svn_repos_t *repos;
  apr_hash_t *fs_config;
  svn_fs_fs__revision_file_t *rev_file;
  apr_off_t filesize = 0, offset;
  apr_off_t i;
  unsigned char footer_len;

  apr_pool_t *iterpool = svn_pool_create(pool);

  /* Open the revision file for modification. */
  SVN_ERR(svn_fs_fs__open_pack_or_rev_file_writable(&rev_file, fs, revision,
                                                    pool, iterpool));
  SVN_ERR(svn_fs_fs__auto_read_footer(rev_file));
  SVN_ERR(svn_io_file_seek(rev_file->file, APR_END, &filesize, iterpool));

  offset = filesize - 1;
  SVN_ERR(svn_io_file_seek(rev_file->file, APR_SET, &offset, iterpool));
  SVN_ERR(svn_io_file_getc((char *)&footer_len, rev_file->file, iterpool));

  /* We want all the caching we can get.  More importantly, we want to
     change the cache namespace before each test iteration. */
  fs_config = apr_hash_make(pool);
  svn_hash_sets(fs_config, SVN_FS_CONFIG_FSFS_CACHE_DELTAS, "1");
  svn_hash_sets(fs_config, SVN_FS_CONFIG_FSFS_CACHE_FULLTEXTS, "1");
  svn_hash_sets(fs_config, SVN_FS_CONFIG_FSFS_CACHE_NODEPROPS, "1");
  svn_hash_sets(fs_config, SVN_FS_CONFIG_FSFS_CACHE_REVPROPS, "2");
  svn_hash_sets(fs_config, SVN_FS_CONFIG_FSFS_BLOCK_READ, "0");

  /* Manipulate all bytes one at a time. */
  for (i = 0; i < filesize; ++i)
    {
      svn_error_t *err = SVN_NO_ERROR;

      /* Read byte */
      unsigned char c_old, c_new;
      SVN_ERR(svn_io_file_seek(rev_file->file, APR_SET, &i, iterpool));
      SVN_ERR(svn_io_file_getc((char *)&c_old, rev_file->file, iterpool));

      /* What to replace it with. Skip if there is no change. */
      c_new = modifier(c_old, baton);
      if (c_new == c_old)
        continue;

      /* Modify / corrupt the data. */
      SVN_ERR(svn_io_file_seek(rev_file->file, APR_SET, &i, iterpool));
      SVN_ERR(svn_io_file_putc((char)c_new, rev_file->file, iterpool));
      SVN_ERR(svn_io_file_flush(rev_file->file, iterpool));

      /* Make sure we use a different namespace for the caches during
         this iteration. */
      svn_hash_sets(fs_config, SVN_FS_CONFIG_FSFS_CACHE_NS,
                               svn_uuid_generate(iterpool));
      SVN_ERR(svn_repos_open3(&repos, repo_name, fs_config, iterpool, iterpool));
      svn_fs_set_warning_func(svn_repos_fs(repos), dont_filter_warnings, NULL);

      /* This shall detect the corruption and return an error. */
      err = svn_repos_verify_fs3(repos, revision, revision, FALSE, FALSE,
                                 NULL, NULL, NULL, NULL, NULL, NULL,
                                 iterpool);

      /* Case-only changes in checksum digests are not an error.
       * We allow upper case chars to be used in MD5 checksums in all other
       * places, thus restricting them here would be inconsistent. */
      if (   i >= filesize - footer_len         /* Within footer */
          && c_old >= 'a' && c_old <= 'f'       /* 'a' to 'f', only appear
                                                   in checksum digests */
          && c_new == c_old - 'a' + 'A')        /* respective upper case */
        {
          if (err)
            {
              /* Let us know where we were too strict ... */
              printf("Detected case change in checksum digest at offset 0x%"
                     APR_UINT64_T_HEX_FMT " (%" APR_OFF_T_FMT ") in r%ld: "
                     "%c -> %c\n", (apr_uint64_t)i, i, revision, c_old, c_new);

              SVN_ERR(err);
            }
        }
      else if (!err)
        {
          /* Let us know where we miss changes ... */
          printf("Undetected mod at offset 0x%"APR_UINT64_T_HEX_FMT
                " (%"APR_OFF_T_FMT") in r%ld: 0x%02x -> 0x%02x\n",
                (apr_uint64_t)i, i, revision, c_old, c_new);

          SVN_TEST_ASSERT(err);
        }

      svn_error_clear(err);

      /* Undo the corruption. */
      SVN_ERR(svn_io_file_seek(rev_file->file, APR_SET, &i, iterpool));
      SVN_ERR(svn_io_file_putc((char)c_old, rev_file->file, iterpool));

      svn_pool_clear(iterpool);
    }

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

/* Create a greek repo with OPTS at REPO_NAME.  Verify that a modification
 * of any single byte using MODIFIER with BATON will be detected. */
static svn_error_t *
fuzzing_1_byte_test(const svn_test_opts_t *opts,
                    const char *repo_name,
                    unsigned char (* modifier)(unsigned char c, void *baton),
                    void *baton,
                    apr_pool_t *pool)
{
  svn_repos_t *repos;
  svn_fs_t *fs;
  svn_fs_txn_t *txn;
  svn_fs_root_t *txn_root;
  svn_revnum_t rev;
  svn_revnum_t i;

  apr_pool_t *iterpool = svn_pool_create(pool);

  /* Bail (with success) on known-untestable scenarios */
  if (strcmp(opts->fs_type, "fsfs") != 0)
    return svn_error_create(SVN_ERR_TEST_SKIPPED, NULL,
                            "this will test FSFS repositories only");
  /* Create a filesystem */
  SVN_ERR(svn_test__create_repos(&repos, repo_name, opts, pool));
  fs = svn_repos_fs(repos);

  /* Revision 1 (one and only revision): the Greek tree */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, 0, pool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, pool));
  SVN_ERR(svn_test__create_greek_tree(txn_root, pool));
  SVN_ERR(svn_fs_commit_txn(NULL, &rev, txn, pool));
  SVN_TEST_ASSERT(SVN_IS_VALID_REVNUM(rev));

  for (i = 0; i <= rev; ++i)
    {
      svn_pool_clear(iterpool);
      SVN_ERR(fuzzing_1_byte_1_rev(repo_name, fs, i, modifier, baton,
                                   iterpool));
    }

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

/* Modifier function to be used with fuzzing_set_byte_test.
 * We return the fixed char value given as *BATON. */
static unsigned char
set_byte(unsigned char c, void *baton)
{
  return *(const unsigned char *)baton;
}

/* Run the fuzzing test setting any byte in the repo to all values MIN to
 * MAX-1. */
static svn_error_t *
fuzzing_set_byte_test(const svn_test_opts_t *opts,
                      int min,
                      int max,
                      apr_pool_t *pool)
{
  apr_pool_t *iterpool = svn_pool_create(pool);
  unsigned i = 0;
  for (i = min; i < max; ++i)
    {
      unsigned char c = i;
      const char *repo_name;
      svn_pool_clear(iterpool);

      repo_name = apr_psprintf(iterpool, "test-repo-fuzzing_set_byte_%d_%d",
                               min, max);
      SVN_ERR(fuzzing_1_byte_test(opts, repo_name, set_byte, &c, iterpool));
    }

  svn_pool_destroy(iterpool);
  return SVN_NO_ERROR;
}



/*** Tests ***/

/* ------------------------------------------------------------------------ */

static unsigned char
invert_byte(unsigned char c, void *baton)
{
  return ~c;
}

static svn_error_t *
fuzzing_invert_byte_test(const svn_test_opts_t *opts,
                         apr_pool_t *pool)
{
  SVN_ERR(fuzzing_1_byte_test(opts, "test-repo-fuzzing_invert_byte",
                              invert_byte, NULL, pool));

  return SVN_NO_ERROR;
}

/* ------------------------------------------------------------------------ */

static unsigned char
increment_byte(unsigned char c, void *baton)
{
  return c + 1;
}

static svn_error_t *
fuzzing_increment_byte_test(const svn_test_opts_t *opts,
                            apr_pool_t *pool)
{
  SVN_ERR(fuzzing_1_byte_test(opts, "test-repo-fuzzing_increment_byte",
                              increment_byte, NULL, pool));

  return SVN_NO_ERROR;
}

/* ------------------------------------------------------------------------ */

static unsigned char
decrement_byte(unsigned char c, void *baton)
{
  return c - 1;
}

static svn_error_t *
fuzzing_decrement_byte_test(const svn_test_opts_t *opts,
                            apr_pool_t *pool)
{
  SVN_ERR(fuzzing_1_byte_test(opts, "test-repo-fuzzing_decrement_byte",
                              decrement_byte, NULL, pool));

  return SVN_NO_ERROR;
}

/* ------------------------------------------------------------------------ */

static unsigned char
null_byte(unsigned char c, void *baton)
{
  return 0;
}

static svn_error_t *
fuzzing_null_byte_test(const svn_test_opts_t *opts,
                       apr_pool_t *pool)
{
  SVN_ERR(fuzzing_1_byte_test(opts, "test-repo-fuzzing_null_byte",
                              null_byte, NULL, pool));

  return SVN_NO_ERROR;
}

/* ------------------------------------------------------------------------ */

/* Generator macro: define a test function covering byte values N to M-1 */
#define FUZZING_SET_BYTE_TEST_N(N,M)\
  static svn_error_t * \
  fuzzing_set_byte_test_ ##N(const svn_test_opts_t *opts, \
                             apr_pool_t *pool) \
  { \
    return svn_error_trace(fuzzing_set_byte_test(opts, N, M, pool)); \
  }

/* Add the test function declared above to the test_funcs array. */
#define TEST_FUZZING_SET_BYTE_TEST_N(N,M)\
  SVN_TEST_OPTS_PASS(fuzzing_set_byte_test_ ##N, \
                     "set any byte to any value between " #N " and " #M)

/* Declare tests that will cover all possible byte values. */
FUZZING_SET_BYTE_TEST_N(0,16)
FUZZING_SET_BYTE_TEST_N(16,32)
FUZZING_SET_BYTE_TEST_N(32,48)
FUZZING_SET_BYTE_TEST_N(48,64)
FUZZING_SET_BYTE_TEST_N(64,80)
FUZZING_SET_BYTE_TEST_N(80,96)
FUZZING_SET_BYTE_TEST_N(96,112)
FUZZING_SET_BYTE_TEST_N(112,128)
FUZZING_SET_BYTE_TEST_N(128,144)
FUZZING_SET_BYTE_TEST_N(144,160)
FUZZING_SET_BYTE_TEST_N(160,176)
FUZZING_SET_BYTE_TEST_N(176,192)
FUZZING_SET_BYTE_TEST_N(192,208)
FUZZING_SET_BYTE_TEST_N(208,224)
FUZZING_SET_BYTE_TEST_N(224,240)
FUZZING_SET_BYTE_TEST_N(240,256)


/* The test table.  */

/* Allow for any number of tests to run in parallel. */
static int max_threads = 0;

static struct svn_test_descriptor_t test_funcs[] =
  {
    SVN_TEST_NULL,
    SVN_TEST_OPTS_PASS(fuzzing_invert_byte_test,
                       "fuzzing: invert any byte"),
    SVN_TEST_OPTS_PASS(fuzzing_increment_byte_test,
                       "fuzzing: increment any byte"),
    SVN_TEST_OPTS_PASS(fuzzing_decrement_byte_test,
                       "fuzzing: decrement any byte"),
    SVN_TEST_OPTS_PASS(fuzzing_null_byte_test,
                       "fuzzing: set any byte to 0"),

    /* Register generated tests. */
    TEST_FUZZING_SET_BYTE_TEST_N(0,16),
    TEST_FUZZING_SET_BYTE_TEST_N(16,32),
    TEST_FUZZING_SET_BYTE_TEST_N(32,48),
    TEST_FUZZING_SET_BYTE_TEST_N(48,64),
    TEST_FUZZING_SET_BYTE_TEST_N(64,80),
    TEST_FUZZING_SET_BYTE_TEST_N(80,96),
    TEST_FUZZING_SET_BYTE_TEST_N(96,112),
    TEST_FUZZING_SET_BYTE_TEST_N(112,128),
    TEST_FUZZING_SET_BYTE_TEST_N(128,144),
    TEST_FUZZING_SET_BYTE_TEST_N(144,160),
    TEST_FUZZING_SET_BYTE_TEST_N(160,176),
    TEST_FUZZING_SET_BYTE_TEST_N(176,192),
    TEST_FUZZING_SET_BYTE_TEST_N(192,208),
    TEST_FUZZING_SET_BYTE_TEST_N(208,224),
    TEST_FUZZING_SET_BYTE_TEST_N(224,240),
    TEST_FUZZING_SET_BYTE_TEST_N(240,256),

    SVN_TEST_NULL
  };

SVN_TEST_MAIN
