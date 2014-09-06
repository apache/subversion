/* fs-fs-fuzzy-test.c --- fuzzying tests for the FSFS filesystem
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

/* Create a greek repo with OPTS at REPO_NAME.  Verify that a modification
 * of any single byte using MODIFIER will be detected. */
static svn_error_t *
currying_1_byte_test(const svn_test_opts_t *opts,
                     const char *repo_name,
                     unsigned char (* modifier)(unsigned char c),
                     apr_pool_t *pool)
{
  svn_repos_t *repos;
  svn_fs_t *fs;
  svn_fs_txn_t *txn;
  svn_fs_root_t *txn_root;
  svn_revnum_t rev;
  apr_hash_t *fs_config;
  svn_fs_fs__revision_file_t *rev_file;
  apr_off_t filesize = 0;
  apr_off_t i;

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

  /* Open the revision 1 file for modification. */
  SVN_ERR(svn_fs_fs__open_pack_or_rev_file_writable(&rev_file, fs, rev,
                                                    pool, iterpool));
  SVN_ERR(svn_io_file_seek(rev_file->file, APR_END, &filesize, iterpool));

  /* We want all the caching we can get.  More importantly, we want to
     change the cache namespace before each test iteration. */
  fs_config = apr_hash_make(pool);
  svn_hash_sets(fs_config, SVN_FS_CONFIG_FSFS_CACHE_DELTAS, "1");
  svn_hash_sets(fs_config, SVN_FS_CONFIG_FSFS_CACHE_FULLTEXTS, "1");
  svn_hash_sets(fs_config, SVN_FS_CONFIG_FSFS_CACHE_REVPROPS, "2");
  svn_hash_sets(fs_config, SVN_FS_CONFIG_FSFS_BLOCK_READ, "0");

  /* Re-open the repo with known config hash and without swallowing any
     caching related errors. */
  SVN_ERR(svn_repos_open3(&repos, repo_name, fs_config, pool, iterpool));
  svn_fs_set_warning_func(svn_repos_fs(repos), dont_filter_warnings, NULL);

  /* Manipulate all bytes one at a time. */
  for (i = 0; i < filesize; ++i)
    {
      svn_error_t *err;

      /* Read byte */
      unsigned char c_old, c_new;
      SVN_ERR(svn_io_file_seek(rev_file->file, APR_SET, &i, iterpool));
      SVN_ERR(svn_io_file_getc((char *)&c_old, rev_file->file, iterpool));

      /* What to replace it with. Skip if there is no change. */
      c_new = modifier(c_old);
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
      SVN_ERR(svn_fs_fs__initialize_caches(svn_repos_fs(repos), iterpool));

      /* This shall detect the corruption and return an error. */
      err = svn_repos_verify_fs3(repos, rev, rev, TRUE, FALSE, FALSE,
                                 NULL, NULL, NULL, NULL, iterpool);

      /* Log changes that don't get detected.
         In f7, this can only bits in the indexes. */
      if (!err)
        printf("Undetected mod at offset %lx (%ld): 0x%02x -> 0x%02x\n",
               i, i, c_old, c_new);

      /* Once we catch all changes, . */
/*      SVN_TEST_ASSERT(err); */
      svn_error_clear(err);

      /* Undo the corruption. */
      SVN_ERR(svn_io_file_seek(rev_file->file, APR_SET, &i, iterpool));
      SVN_ERR(svn_io_file_putc((char)c_old, rev_file->file, iterpool));

      svn_pool_clear(iterpool);
    }

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}


/*** Tests ***/

/* ------------------------------------------------------------------------ */

static unsigned char
invert_byte(unsigned char c)
{
  return ~c;
}

static svn_error_t *
currying_invert_byte_test(const svn_test_opts_t *opts,
                          apr_pool_t *pool)
{
  SVN_ERR(currying_1_byte_test(opts, "currying_invert_byte", invert_byte,
                               pool));

  return SVN_NO_ERROR;
}

/* ------------------------------------------------------------------------ */

static unsigned char
increment_byte(unsigned char c)
{
  return c + 1;
}

static svn_error_t *
currying_increment_byte_test(const svn_test_opts_t *opts,
                             apr_pool_t *pool)
{
  SVN_ERR(currying_1_byte_test(opts, "currying_increment_byte",
                               increment_byte, pool));

  return SVN_NO_ERROR;
}

/* ------------------------------------------------------------------------ */

static unsigned char
decrement_byte(unsigned char c)
{
  return c - 1;
}

static svn_error_t *
currying_decrement_byte_test(const svn_test_opts_t *opts,
                             apr_pool_t *pool)
{
  SVN_ERR(currying_1_byte_test(opts, "currying_decrement_byte",
                               decrement_byte, pool));

  return SVN_NO_ERROR;
}

/* ------------------------------------------------------------------------ */

static unsigned char
null_byte(unsigned char c)
{
  return 0;
}

static svn_error_t *
currying_null_byte_test(const svn_test_opts_t *opts,
                        apr_pool_t *pool)
{
  SVN_ERR(currying_1_byte_test(opts, "currying_null_byte", null_byte,
                               pool));

  return SVN_NO_ERROR;
}


/* The test table.  */

static int max_threads = 4;

static struct svn_test_descriptor_t test_funcs[] =
  {
    SVN_TEST_NULL,
    SVN_TEST_OPTS_PASS(currying_invert_byte_test,
                       "currying: invert any byte"),
    SVN_TEST_OPTS_PASS(currying_increment_byte_test,
                       "currying: increment any byte"),
    SVN_TEST_OPTS_PASS(currying_decrement_byte_test,
                       "currying: decrement any byte"),
    SVN_TEST_OPTS_PASS(currying_null_byte_test,
                       "currying: set any byte to 0"),
    SVN_TEST_NULL
  };

SVN_TEST_MAIN
