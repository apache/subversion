/* fs-fs-pack-test.c --- tests for the FSFS filesystem
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
#include "../../libsvn_fs/fs-loader.h"
#include "../../libsvn_fs_fs/fs.h"
#include "../../libsvn_fs_fs/fs_fs.h"
#include "../../libsvn_fs_fs/low_level.h"
#include "../../libsvn_fs_fs/pack.h"
#include "../../libsvn_fs_fs/util.h"

#include "svn_hash.h"
#include "svn_pools.h"
#include "svn_props.h"
#include "svn_fs.h"
#include "private/svn_string_private.h"

#include "../svn_test_fs.h"



/*** Helper Functions ***/

static void
ignore_fs_warnings(void *baton, svn_error_t *err)
{
#ifdef SVN_DEBUG
  SVN_DBG(("Ignoring FS warning %s\n",
           svn_error_symbolic_name(err ? err->apr_err : 0)));
#endif
  return;
}

/* Return the expected contents of "iota" in revision REV. */
static const char *
get_rev_contents(svn_revnum_t rev, apr_pool_t *pool)
{
  /* Toss in a bunch of magic numbers for spice. */
  apr_int64_t num = ((rev * 1234353 + 4358) * 4583 + ((rev % 4) << 1)) / 42;
  return apr_psprintf(pool, "%" APR_INT64_T_FMT "\n", num);
}

struct pack_notify_baton
{
  apr_int64_t expected_shard;
  svn_fs_pack_notify_action_t expected_action;
};

static svn_error_t *
pack_notify(void *baton,
            apr_int64_t shard,
            svn_fs_pack_notify_action_t action,
            apr_pool_t *pool)
{
  struct pack_notify_baton *pnb = baton;

  SVN_TEST_ASSERT(shard == pnb->expected_shard);
  SVN_TEST_ASSERT(action == pnb->expected_action);

  /* Update expectations. */
  switch (action)
    {
      case svn_fs_pack_notify_start:
        pnb->expected_action = svn_fs_pack_notify_end;
        break;

      case svn_fs_pack_notify_end:
        pnb->expected_action = svn_fs_pack_notify_start;
        pnb->expected_shard++;
        break;

      default:
        return svn_error_create(SVN_ERR_TEST_FAILED, NULL,
                                "Unknown notification action when packing");
    }

  return SVN_NO_ERROR;
}

#define R1_LOG_MSG "Let's serf"

/* Create a filesystem in DIR.  Set the shard size to SHARD_SIZE and create
   NUM_REVS number of revisions (in addition to r0).  Use POOL for
   allocations.  After this function successfully completes, the filesystem's
   youngest revision number will be NUM_REVS.  */
static svn_error_t *
create_non_packed_filesystem(const char *dir,
                             const svn_test_opts_t *opts,
                             svn_revnum_t num_revs,
                             int shard_size,
                             apr_pool_t *pool)
{
  svn_fs_t *fs;
  svn_fs_txn_t *txn;
  svn_fs_root_t *txn_root;
  const char *conflict;
  svn_revnum_t after_rev;
  apr_pool_t *subpool = svn_pool_create(pool);
  apr_pool_t *iterpool;
  apr_hash_t *fs_config;

  /* Bail (with success) on known-untestable scenarios */
  if (strcmp(opts->fs_type, "fsfs") != 0)
    return svn_error_create(SVN_ERR_TEST_SKIPPED, NULL,
                            "this will test FSFS repositories only");

  if (opts->server_minor_version && (opts->server_minor_version < 6))
    return svn_error_create(SVN_ERR_TEST_SKIPPED, NULL,
                            "pre-1.6 SVN doesn't support FSFS packing");

  fs_config = apr_hash_make(pool);
  svn_hash_sets(fs_config, SVN_FS_CONFIG_FSFS_SHARD_SIZE,
                apr_itoa(pool, shard_size));

  /* Create a filesystem. */
  SVN_ERR(svn_test__create_fs2(&fs, dir, opts, fs_config, subpool));

  /* Revision 1: the Greek tree */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, 0, subpool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, subpool));
  SVN_ERR(svn_test__create_greek_tree(txn_root, subpool));
  SVN_ERR(svn_fs_change_txn_prop(txn, SVN_PROP_REVISION_LOG,
                                 svn_string_create(R1_LOG_MSG, pool),
                                 pool));
  SVN_ERR(svn_fs_commit_txn(&conflict, &after_rev, txn, subpool));
  SVN_TEST_ASSERT(SVN_IS_VALID_REVNUM(after_rev));

  /* Revisions 2 thru NUM_REVS-1: content tweaks to "iota". */
  iterpool = svn_pool_create(subpool);
  while (after_rev < num_revs)
    {
      svn_pool_clear(iterpool);
      SVN_ERR(svn_fs_begin_txn(&txn, fs, after_rev, iterpool));
      SVN_ERR(svn_fs_txn_root(&txn_root, txn, iterpool));
      SVN_ERR(svn_test__set_file_contents(txn_root, "iota",
                                          get_rev_contents(after_rev + 1,
                                                           iterpool),
                                          iterpool));
      SVN_ERR(svn_fs_commit_txn(&conflict, &after_rev, txn, iterpool));
      SVN_TEST_ASSERT(SVN_IS_VALID_REVNUM(after_rev));
    }
  svn_pool_destroy(iterpool);
  svn_pool_destroy(subpool);

  /* Done */
  return SVN_NO_ERROR;
}

/* Create a packed filesystem in DIR.  Set the shard size to
   SHARD_SIZE and create NUM_REVS number of revisions (in addition to
   r0).  Use POOL for allocations.  After this function successfully
   completes, the filesystem's youngest revision number will be the
   same as NUM_REVS.  */
static svn_error_t *
create_packed_filesystem(const char *dir,
                         const svn_test_opts_t *opts,
                         svn_revnum_t num_revs,
                         int shard_size,
                         apr_pool_t *pool)
{
  struct pack_notify_baton pnb;

  /* Create the repo and fill it. */
  SVN_ERR(create_non_packed_filesystem(dir, opts, num_revs, shard_size,
                                       pool));

  /* Now pack the FS */
  pnb.expected_shard = 0;
  pnb.expected_action = svn_fs_pack_notify_start;
  return svn_fs_pack(dir, pack_notify, &pnb, NULL, NULL, pool);
}

/* Create a packed FSFS filesystem for revprop tests at REPO_NAME with
 * MAX_REV revisions and the given SHARD_SIZE and OPTS.  Return it in *FS.
 * Use POOL for allocations.
 */
static svn_error_t *
prepare_revprop_repo(svn_fs_t **fs,
                     const char *repo_name,
                     svn_revnum_t max_rev,
                     int shard_size,
                     const svn_test_opts_t *opts,
                     apr_pool_t *pool)
{
  svn_fs_txn_t *txn;
  svn_fs_root_t *txn_root;
  const char *conflict;
  svn_revnum_t after_rev;
  apr_pool_t *subpool;

  /* Create the packed FS and open it. */
  SVN_ERR(create_packed_filesystem(repo_name, opts, max_rev, shard_size, pool));
  SVN_ERR(svn_fs_open2(fs, repo_name, NULL, pool, pool));

  subpool = svn_pool_create(pool);
  /* Do a commit to trigger packing. */
  SVN_ERR(svn_fs_begin_txn(&txn, *fs, max_rev, subpool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, subpool));
  SVN_ERR(svn_test__set_file_contents(txn_root, "iota", "new-iota",  subpool));
  SVN_ERR(svn_fs_commit_txn(&conflict, &after_rev, txn, subpool));
  SVN_TEST_ASSERT(SVN_IS_VALID_REVNUM(after_rev));
  svn_pool_destroy(subpool);

  /* Pack the repository. */
  SVN_ERR(svn_fs_pack(repo_name, NULL, NULL, NULL, NULL, pool));

  return SVN_NO_ERROR;
}

/* For revision REV, return a short log message allocated in POOL.
 */
static svn_string_t *
default_log(svn_revnum_t rev, apr_pool_t *pool)
{
  return svn_string_createf(pool, "Default message for rev %ld", rev);
}

/* For revision REV, return a long log message allocated in POOL.
 */
static svn_string_t *
large_log(svn_revnum_t rev, apr_size_t length, apr_pool_t *pool)
{
  svn_stringbuf_t *temp = svn_stringbuf_create_ensure(100000, pool);
  int i, count = (int)(length - 50) / 6;

  svn_stringbuf_appendcstr(temp, "A ");
  for (i = 0; i < count; ++i)
    svn_stringbuf_appendcstr(temp, "very, ");

  svn_stringbuf_appendcstr(temp,
    apr_psprintf(pool, "very long message for rev %ld, indeed", rev));

  return svn_stringbuf__morph_into_string(temp);
}

/* For revision REV, return a long log message allocated in POOL.
 */
static svn_string_t *
huge_log(svn_revnum_t rev, apr_pool_t *pool)
{
  return large_log(rev, 90000, pool);
}


/*** Tests ***/

/* ------------------------------------------------------------------------ */
#define REPO_NAME "test-repo-fsfs-pack"
#define SHARD_SIZE 7
#define MAX_REV 53
static svn_error_t *
pack_filesystem(const svn_test_opts_t *opts,
                apr_pool_t *pool)
{
  int i;
  svn_node_kind_t kind;
  const char *path;
  char buf[80];
  apr_file_t *file;
  apr_size_t len;

  SVN_ERR(create_packed_filesystem(REPO_NAME, opts, MAX_REV, SHARD_SIZE,
                                   pool));

  /* Check to see that the pack files exist, and that the rev directories
     don't. */
  for (i = 0; i < (MAX_REV + 1) / SHARD_SIZE; i++)
    {
      path = svn_dirent_join_many(pool, REPO_NAME, "revs",
                                  apr_psprintf(pool, "%d.pack", i / SHARD_SIZE),
                                  "pack", SVN_VA_NULL);

      /* These files should exist. */
      SVN_ERR(svn_io_check_path(path, &kind, pool));
      if (kind != svn_node_file)
        return svn_error_createf(SVN_ERR_FS_GENERAL, NULL,
                                 "Expected pack file '%s' not found", path);

      if (opts->server_minor_version && (opts->server_minor_version < 9))
        {
          path = svn_dirent_join_many(pool, REPO_NAME, "revs",
                                      apr_psprintf(pool, "%d.pack", i / SHARD_SIZE),
                                      "manifest", SVN_VA_NULL);
          SVN_ERR(svn_io_check_path(path, &kind, pool));
          if (kind != svn_node_file)
            return svn_error_createf(SVN_ERR_FS_GENERAL, NULL,
                                     "Expected manifest file '%s' not found",
                                     path);
        }

      /* This directory should not exist. */
      path = svn_dirent_join_many(pool, REPO_NAME, "revs",
                                  apr_psprintf(pool, "%d", i / SHARD_SIZE),
                                  SVN_VA_NULL);
      SVN_ERR(svn_io_check_path(path, &kind, pool));
      if (kind != svn_node_none)
        return svn_error_createf(SVN_ERR_FS_GENERAL, NULL,
                                 "Unexpected directory '%s' found", path);
    }

  /* Ensure the min-unpacked-rev jives with the above operations. */
  SVN_ERR(svn_io_file_open(&file,
                           svn_dirent_join(REPO_NAME, PATH_MIN_UNPACKED_REV,
                                           pool),
                           APR_READ | APR_BUFFERED, APR_OS_DEFAULT, pool));
  len = sizeof(buf);
  SVN_ERR(svn_io_read_length_line(file, buf, &len, pool));
  SVN_ERR(svn_io_file_close(file, pool));
  if (SVN_STR_TO_REV(buf) != (MAX_REV / SHARD_SIZE) * SHARD_SIZE)
    return svn_error_createf(SVN_ERR_FS_GENERAL, NULL,
                             "Bad '%s' contents", PATH_MIN_UNPACKED_REV);

  /* Finally, make sure the final revision directory does exist. */
  path = svn_dirent_join_many(pool, REPO_NAME, "revs",
                              apr_psprintf(pool, "%d", (i / SHARD_SIZE) + 1),
                              SVN_VA_NULL);
  SVN_ERR(svn_io_check_path(path, &kind, pool));
  if (kind != svn_node_none)
    return svn_error_createf(SVN_ERR_FS_GENERAL, NULL,
                             "Expected directory '%s' not found", path);


  return SVN_NO_ERROR;
}
#undef REPO_NAME
#undef SHARD_SIZE
#undef MAX_REV

/* ------------------------------------------------------------------------ */
#define REPO_NAME "test-repo-fsfs-pack-even"
#define SHARD_SIZE 4
#define MAX_REV 11
static svn_error_t *
pack_even_filesystem(const svn_test_opts_t *opts,
                     apr_pool_t *pool)
{
  svn_node_kind_t kind;
  const char *path;

  SVN_ERR(create_packed_filesystem(REPO_NAME, opts, MAX_REV, SHARD_SIZE,
                                   pool));

  path = svn_dirent_join_many(pool, REPO_NAME, "revs", "2.pack", SVN_VA_NULL);
  SVN_ERR(svn_io_check_path(path, &kind, pool));
  if (kind != svn_node_dir)
    return svn_error_createf(SVN_ERR_FS_GENERAL, NULL,
                             "Packing did not complete as expected");

  return SVN_NO_ERROR;
}
#undef REPO_NAME
#undef SHARD_SIZE
#undef MAX_REV

/* ------------------------------------------------------------------------ */
#define REPO_NAME "test-repo-read-packed-fs"
#define SHARD_SIZE 5
#define MAX_REV 11
static svn_error_t *
read_packed_fs(const svn_test_opts_t *opts,
               apr_pool_t *pool)
{
  svn_fs_t *fs;
  svn_stream_t *rstream;
  svn_stringbuf_t *rstring;
  svn_revnum_t i;

  SVN_ERR(create_packed_filesystem(REPO_NAME, opts, MAX_REV, SHARD_SIZE, pool));
  SVN_ERR(svn_fs_open2(&fs, REPO_NAME, NULL, pool, pool));

  for (i = 1; i < (MAX_REV + 1); i++)
    {
      svn_fs_root_t *rev_root;
      svn_stringbuf_t *sb;

      SVN_ERR(svn_fs_revision_root(&rev_root, fs, i, pool));
      SVN_ERR(svn_fs_file_contents(&rstream, rev_root, "iota", pool));
      SVN_ERR(svn_test__stream_to_string(&rstring, rstream, pool));

      if (i == 1)
        sb = svn_stringbuf_create("This is the file 'iota'.\n", pool);
      else
        sb = svn_stringbuf_create(get_rev_contents(i, pool), pool);

      if (! svn_stringbuf_compare(rstring, sb))
        return svn_error_createf(SVN_ERR_FS_GENERAL, NULL,
                                 "Bad data in revision %ld.", i);
    }

  return SVN_NO_ERROR;
}
#undef REPO_NAME
#undef SHARD_SIZE
#undef MAX_REV

/* ------------------------------------------------------------------------ */
#define REPO_NAME "test-repo-commit-packed-fs"
#define SHARD_SIZE 5
#define MAX_REV 10
static svn_error_t *
commit_packed_fs(const svn_test_opts_t *opts,
                 apr_pool_t *pool)
{
  svn_fs_t *fs;
  svn_fs_txn_t *txn;
  svn_fs_root_t *txn_root;
  const char *conflict;
  svn_revnum_t after_rev;

  /* Create the packed FS and open it. */
  SVN_ERR(create_packed_filesystem(REPO_NAME, opts, MAX_REV, 5, pool));
  SVN_ERR(svn_fs_open2(&fs, REPO_NAME, NULL, pool, pool));

  /* Now do a commit. */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, MAX_REV, pool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, pool));
  SVN_ERR(svn_test__set_file_contents(txn_root, "iota",
          "How much better is it to get wisdom than gold! and to get "
          "understanding rather to be chosen than silver!", pool));
  SVN_ERR(svn_fs_commit_txn(&conflict, &after_rev, txn, pool));
  SVN_TEST_ASSERT(SVN_IS_VALID_REVNUM(after_rev));

  return SVN_NO_ERROR;
}
#undef REPO_NAME
#undef MAX_REV
#undef SHARD_SIZE

/* ------------------------------------------------------------------------ */
#define REPO_NAME "test-repo-get-set-revprop-packed-fs"
#define SHARD_SIZE 4
#define MAX_REV 10
static svn_error_t *
get_set_revprop_packed_fs(const svn_test_opts_t *opts,
                          apr_pool_t *pool)
{
  svn_fs_t *fs;
  svn_string_t *prop_value;

  /* Create the packed FS and open it. */
  SVN_ERR(prepare_revprop_repo(&fs, REPO_NAME, MAX_REV, SHARD_SIZE, opts,
                               pool));

  /* Try to get revprop for revision 0
   * (non-packed due to special handling). */
  SVN_ERR(svn_fs_revision_prop(&prop_value, fs, 0, SVN_PROP_REVISION_AUTHOR,
                               pool));

  /* Try to change revprop for revision 0
   * (non-packed due to special handling). */
  SVN_ERR(svn_fs_change_rev_prop(fs, 0, SVN_PROP_REVISION_AUTHOR,
                                 svn_string_create("tweaked-author", pool),
                                 pool));

  /* verify */
  SVN_ERR(svn_fs_revision_prop(&prop_value, fs, 0, SVN_PROP_REVISION_AUTHOR,
                               pool));
  SVN_TEST_STRING_ASSERT(prop_value->data, "tweaked-author");

  /* Try to get packed revprop for revision 5. */
  SVN_ERR(svn_fs_revision_prop(&prop_value, fs, 5, SVN_PROP_REVISION_AUTHOR,
                               pool));

  /* Try to change packed revprop for revision 5. */
  SVN_ERR(svn_fs_change_rev_prop(fs, 5, SVN_PROP_REVISION_AUTHOR,
                                 svn_string_create("tweaked-author2", pool),
                                 pool));

  /* verify */
  SVN_ERR(svn_fs_revision_prop(&prop_value, fs, 5, SVN_PROP_REVISION_AUTHOR,
                               pool));
  SVN_TEST_STRING_ASSERT(prop_value->data, "tweaked-author2");

  return SVN_NO_ERROR;
}
#undef REPO_NAME
#undef MAX_REV
#undef SHARD_SIZE

/* ------------------------------------------------------------------------ */
#define REPO_NAME "test-repo-get-set-large-revprop-packed-fs"
#define SHARD_SIZE 4
#define MAX_REV 11
static svn_error_t *
get_set_large_revprop_packed_fs(const svn_test_opts_t *opts,
                                apr_pool_t *pool)
{
  svn_fs_t *fs;
  svn_string_t *prop_value;
  svn_revnum_t rev;

  /* Create the packed FS and open it. */
  SVN_ERR(prepare_revprop_repo(&fs, REPO_NAME, MAX_REV, SHARD_SIZE, opts,
                               pool));

  /* Set commit messages to different, large values that fill the pack
   * files but do not exceed the pack size limit. */
  for (rev = 0; rev <= MAX_REV; ++rev)
    SVN_ERR(svn_fs_change_rev_prop(fs, rev, SVN_PROP_REVISION_LOG,
                                   large_log(rev, 1000, pool),
                                   pool));

  /* verify */
  for (rev = 0; rev <= MAX_REV; ++rev)
    {
      SVN_ERR(svn_fs_revision_prop(&prop_value, fs, rev,
                                   SVN_PROP_REVISION_LOG, pool));
      SVN_TEST_STRING_ASSERT(prop_value->data,
                             large_log(rev, 1000, pool)->data);
    }

  /* Put a larger revprop into the last, some middle and the first revision
   * of a pack.  This should cause the packs to split in the middle. */
  SVN_ERR(svn_fs_change_rev_prop(fs, 3, SVN_PROP_REVISION_LOG,
                                 /* rev 0 is not packed */
                                 large_log(3, 2400, pool),
                                 pool));
  SVN_ERR(svn_fs_change_rev_prop(fs, 5, SVN_PROP_REVISION_LOG,
                                 large_log(5, 1500, pool),
                                 pool));
  SVN_ERR(svn_fs_change_rev_prop(fs, 8, SVN_PROP_REVISION_LOG,
                                 large_log(8, 1500, pool),
                                 pool));

  /* verify */
  for (rev = 0; rev <= MAX_REV; ++rev)
    {
      SVN_ERR(svn_fs_revision_prop(&prop_value, fs, rev,
                                   SVN_PROP_REVISION_LOG, pool));

      if (rev == 3)
        SVN_TEST_STRING_ASSERT(prop_value->data,
                               large_log(rev, 2400, pool)->data);
      else if (rev == 5 || rev == 8)
        SVN_TEST_STRING_ASSERT(prop_value->data,
                               large_log(rev, 1500, pool)->data);
      else
        SVN_TEST_STRING_ASSERT(prop_value->data,
                               large_log(rev, 1000, pool)->data);
    }

  return SVN_NO_ERROR;
}
#undef REPO_NAME
#undef MAX_REV
#undef SHARD_SIZE

/* ------------------------------------------------------------------------ */
#define REPO_NAME "test-repo-get-set-huge-revprop-packed-fs"
#define SHARD_SIZE 4
#define MAX_REV 10
static svn_error_t *
get_set_huge_revprop_packed_fs(const svn_test_opts_t *opts,
                               apr_pool_t *pool)
{
  svn_fs_t *fs;
  svn_string_t *prop_value;
  svn_revnum_t rev;

  /* Create the packed FS and open it. */
  SVN_ERR(prepare_revprop_repo(&fs, REPO_NAME, MAX_REV, SHARD_SIZE, opts,
                               pool));

  /* Set commit messages to different values */
  for (rev = 0; rev <= MAX_REV; ++rev)
    SVN_ERR(svn_fs_change_rev_prop(fs, rev, SVN_PROP_REVISION_LOG,
                                   default_log(rev, pool),
                                   pool));

  /* verify */
  for (rev = 0; rev <= MAX_REV; ++rev)
    {
      SVN_ERR(svn_fs_revision_prop(&prop_value, fs, rev,
                                   SVN_PROP_REVISION_LOG, pool));
      SVN_TEST_STRING_ASSERT(prop_value->data, default_log(rev, pool)->data);
    }

  /* Put a huge revprop into the last, some middle and the first revision
   * of a pack.  They will cause the pack files to split accordingly. */
  SVN_ERR(svn_fs_change_rev_prop(fs, 3, SVN_PROP_REVISION_LOG,
                                 huge_log(3, pool),
                                 pool));
  SVN_ERR(svn_fs_change_rev_prop(fs, 5, SVN_PROP_REVISION_LOG,
                                 huge_log(5, pool),
                                 pool));
  SVN_ERR(svn_fs_change_rev_prop(fs, 8, SVN_PROP_REVISION_LOG,
                                 huge_log(8, pool),
                                 pool));

  /* verify */
  for (rev = 0; rev <= MAX_REV; ++rev)
    {
      SVN_ERR(svn_fs_revision_prop(&prop_value, fs, rev,
                                   SVN_PROP_REVISION_LOG, pool));

      if (rev == 3 || rev == 5 || rev == 8)
        SVN_TEST_STRING_ASSERT(prop_value->data,
                               huge_log(rev, pool)->data);
      else
        SVN_TEST_STRING_ASSERT(prop_value->data,
                               default_log(rev, pool)->data);
    }

  return SVN_NO_ERROR;
}
#undef REPO_NAME
#undef MAX_REV
#undef SHARD_SIZE

/* ------------------------------------------------------------------------ */
/* Regression test for issue #3571 (fsfs 'svnadmin recover' expects
   youngest revprop to be outside revprops.db). */
#define REPO_NAME "test-repo-recover-fully-packed"
#define SHARD_SIZE 4
#define MAX_REV 7
static svn_error_t *
recover_fully_packed(const svn_test_opts_t *opts,
                     apr_pool_t *pool)
{
  apr_pool_t *subpool;
  svn_fs_t *fs;
  svn_fs_txn_t *txn;
  svn_fs_root_t *txn_root;
  const char *conflict;
  svn_revnum_t after_rev;
  svn_error_t *err;

  /* Create a packed FS for which every revision will live in a pack
     digest file, and then recover it. */
  SVN_ERR(create_packed_filesystem(REPO_NAME, opts, MAX_REV, SHARD_SIZE, pool));
  SVN_ERR(svn_fs_recover(REPO_NAME, NULL, NULL, pool));

  /* Add another revision, re-pack, re-recover. */
  subpool = svn_pool_create(pool);
  SVN_ERR(svn_fs_open2(&fs, REPO_NAME, NULL, subpool, subpool));
  SVN_ERR(svn_fs_begin_txn(&txn, fs, MAX_REV, subpool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, subpool));
  SVN_ERR(svn_test__set_file_contents(txn_root, "A/mu", "new-mu", subpool));
  SVN_ERR(svn_fs_commit_txn(&conflict, &after_rev, txn, subpool));
  SVN_TEST_ASSERT(SVN_IS_VALID_REVNUM(after_rev));
  svn_pool_destroy(subpool);
  SVN_ERR(svn_fs_pack(REPO_NAME, NULL, NULL, NULL, NULL, pool));
  SVN_ERR(svn_fs_recover(REPO_NAME, NULL, NULL, pool));

  /* Now, delete the youngest revprop file, and recover again.  This
     time we want to see an error! */
  SVN_ERR(svn_io_remove_file2(
              svn_dirent_join_many(pool, REPO_NAME, PATH_REVPROPS_DIR,
                                   apr_psprintf(pool, "%ld/%ld",
                                                after_rev / SHARD_SIZE,
                                                after_rev),
                                   SVN_VA_NULL),
              FALSE, pool));
  err = svn_fs_recover(REPO_NAME, NULL, NULL, pool);
  if (! err)
    return svn_error_create(SVN_ERR_TEST_FAILED, NULL,
                            "Expected SVN_ERR_FS_CORRUPT error; got none");
  if (err->apr_err != SVN_ERR_FS_CORRUPT)
    return svn_error_create(SVN_ERR_TEST_FAILED, err,
                            "Expected SVN_ERR_FS_CORRUPT error; got:");
  svn_error_clear(err);
  return SVN_NO_ERROR;
}
#undef REPO_NAME
#undef MAX_REV
#undef SHARD_SIZE

/* ------------------------------------------------------------------------ */
/* Regression test for issue #4320 (fsfs file-hinting fails when reading a rep
   from the transaction that is committing rev = SHARD_SIZE). */
#define REPO_NAME "test-repo-file-hint-at-shard-boundary"
#define SHARD_SIZE 4
#define MAX_REV (SHARD_SIZE - 1)
static svn_error_t *
file_hint_at_shard_boundary(const svn_test_opts_t *opts,
                            apr_pool_t *pool)
{
  apr_pool_t *subpool;
  svn_fs_t *fs;
  svn_fs_txn_t *txn;
  svn_fs_root_t *txn_root;
  const char *file_contents;
  svn_stringbuf_t *retrieved_contents;
  svn_error_t *err = SVN_NO_ERROR;

  /* Create a packed FS and MAX_REV revisions */
  SVN_ERR(create_packed_filesystem(REPO_NAME, opts, MAX_REV, SHARD_SIZE, pool));

  /* Reopen the filesystem */
  subpool = svn_pool_create(pool);
  SVN_ERR(svn_fs_open2(&fs, REPO_NAME, NULL, subpool, subpool));

  /* Revision = SHARD_SIZE */
  file_contents = get_rev_contents(SHARD_SIZE, subpool);
  SVN_ERR(svn_fs_begin_txn(&txn, fs, MAX_REV, subpool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, subpool));
  SVN_ERR(svn_test__set_file_contents(txn_root, "iota", file_contents,
                                      subpool));

  /* Retrieve the file. */
  SVN_ERR(svn_test__get_file_contents(txn_root, "iota", &retrieved_contents,
                                      subpool));
  if (strcmp(retrieved_contents->data, file_contents))
    {
      err = svn_error_create(SVN_ERR_TEST_FAILED, err,
                              "Retrieved incorrect contents from iota.");
    }

  /* Close the repo. */
  svn_pool_destroy(subpool);

  return err;
}
#undef REPO_NAME
#undef MAX_REV
#undef SHARD_SIZE

/* ------------------------------------------------------------------------ */
#define REPO_NAME "test-repo-fsfs-info"
#define SHARD_SIZE 3
#define MAX_REV 5
static svn_error_t *
test_info(const svn_test_opts_t *opts,
          apr_pool_t *pool)
{
  svn_fs_t *fs;
  const svn_fs_fsfs_info_t *fsfs_info;
  const svn_fs_info_placeholder_t *info;

  SVN_ERR(create_packed_filesystem(REPO_NAME, opts, MAX_REV, SHARD_SIZE,
                                   pool));

  SVN_ERR(svn_fs_open2(&fs, REPO_NAME, NULL, pool, pool));
  SVN_ERR(svn_fs_info(&info, fs, pool, pool));
  info = svn_fs_info_dup(info, pool, pool);

  SVN_TEST_STRING_ASSERT(opts->fs_type, info->fs_type);

  /* Bail (with success) on known-untestable scenarios */
  if (strcmp(opts->fs_type, "fsfs") != 0)
    return SVN_NO_ERROR;

  fsfs_info = (const void *)info;
  if (opts->server_minor_version && (opts->server_minor_version < 6))
    {
      SVN_TEST_ASSERT(fsfs_info->shard_size == 0);
      SVN_TEST_ASSERT(fsfs_info->min_unpacked_rev == 0);
    }
  else
    {
      SVN_TEST_ASSERT(fsfs_info->shard_size == SHARD_SIZE);
      SVN_TEST_ASSERT(fsfs_info->min_unpacked_rev
                      == (MAX_REV + 1) / SHARD_SIZE * SHARD_SIZE);
    }

  return SVN_NO_ERROR;
}
#undef REPO_NAME
#undef SHARD_SIZE
#undef MAX_REV

/* ------------------------------------------------------------------------ */
#define REPO_NAME "test-repo-fsfs-pack-shard-size-one"
#define SHARD_SIZE 1
#define MAX_REV 4
static svn_error_t *
pack_shard_size_one(const svn_test_opts_t *opts,
                     apr_pool_t *pool)
{
  svn_string_t *propval;
  svn_fs_t *fs;

  SVN_ERR(create_packed_filesystem(REPO_NAME, opts, MAX_REV, SHARD_SIZE,
                                   pool));
  SVN_ERR(svn_fs_open2(&fs, REPO_NAME, NULL, pool, pool));
  /* whitebox: revprop packing special-cases r0, which causes
     (start_rev==1, end_rev==0) in pack_revprops_shard().  So test that. */
  SVN_ERR(svn_fs_revision_prop(&propval, fs, 1, SVN_PROP_REVISION_LOG, pool));
  SVN_TEST_STRING_ASSERT(propval->data, R1_LOG_MSG);

  return SVN_NO_ERROR;
}
#undef REPO_NAME
#undef SHARD_SIZE
#undef MAX_REV
/* ------------------------------------------------------------------------ */
#define REPO_NAME "test-repo-get_set_multiple_huge_revprops_packed_fs"
#define SHARD_SIZE 4
#define MAX_REV 9
static svn_error_t *
get_set_multiple_huge_revprops_packed_fs(const svn_test_opts_t *opts,
                                         apr_pool_t *pool)
{
  svn_fs_t *fs;
  svn_string_t *prop_value;
  svn_revnum_t rev;

  /* Create the packed FS and open it. */
  SVN_ERR(prepare_revprop_repo(&fs, REPO_NAME, MAX_REV, SHARD_SIZE, opts,
                               pool));

  /* Set commit messages to different values */
  for (rev = 0; rev <= MAX_REV; ++rev)
    SVN_ERR(svn_fs_change_rev_prop(fs, rev, SVN_PROP_REVISION_LOG,
                                   default_log(rev, pool),
                                   pool));

  /* verify */
  for (rev = 0; rev <= MAX_REV; ++rev)
    {
      SVN_ERR(svn_fs_revision_prop(&prop_value, fs, rev,
                                   SVN_PROP_REVISION_LOG, pool));
      SVN_TEST_STRING_ASSERT(prop_value->data, default_log(rev, pool)->data);
    }

  /* Put a huge revprop into revision 1 and 2. */
  SVN_ERR(svn_fs_change_rev_prop(fs, 1, SVN_PROP_REVISION_LOG,
                                 huge_log(1, pool),
                                 pool));
  SVN_ERR(svn_fs_change_rev_prop(fs, 2, SVN_PROP_REVISION_LOG,
                                 huge_log(2, pool),
                                 pool));
  SVN_ERR(svn_fs_change_rev_prop(fs, 5, SVN_PROP_REVISION_LOG,
                                 huge_log(5, pool),
                                 pool));
  SVN_ERR(svn_fs_change_rev_prop(fs, 6, SVN_PROP_REVISION_LOG,
                                 huge_log(6, pool),
                                 pool));

  /* verify */
  for (rev = 0; rev <= MAX_REV; ++rev)
    {
      SVN_ERR(svn_fs_revision_prop(&prop_value, fs, rev,
                                   SVN_PROP_REVISION_LOG, pool));

      if (rev == 1 || rev == 2 || rev == 5 || rev == 6)
        SVN_TEST_STRING_ASSERT(prop_value->data,
                               huge_log(rev, pool)->data);
      else
        SVN_TEST_STRING_ASSERT(prop_value->data,
                               default_log(rev, pool)->data);
    }

  return SVN_NO_ERROR;
}
#undef REPO_NAME
#undef MAX_REV
#undef SHARD_SIZE

/* ------------------------------------------------------------------------ */
#define SHARD_SIZE 4
static svn_error_t *
upgrade_txns_to_log_addressing(const svn_test_opts_t *opts,
                               const char *repo_name,
                               svn_revnum_t max_rev,
                               svn_boolean_t upgrade_before_txns,
                               apr_pool_t *pool)
{
  svn_fs_t *fs;
  svn_revnum_t rev;
  apr_array_header_t *txns;
  apr_array_header_t *txn_names;
  int i, k;
  svn_test_opts_t temp_opts;
  svn_fs_root_t *root;
  apr_pool_t *iterpool = svn_pool_create(pool);

  static const char * const paths[SHARD_SIZE][2]
    = {
        { "A/mu",        "A/B/lambda" },
        { "A/B/E/alpha", "A/D/H/psi"  },
        { "A/D/gamma",   "A/B/E/beta" },
        { "A/D/G/pi",    "A/D/G/rho"  }
      };

  /* Bail (with success) on known-untestable scenarios */
  if ((strcmp(opts->fs_type, "fsfs") != 0)
      || (opts->server_minor_version && (opts->server_minor_version < 9)))
    return svn_error_create(SVN_ERR_TEST_SKIPPED, NULL,
                            "pre-1.9 SVN doesn't support log addressing");

  /* Create the packed FS in phys addressing format and open it. */
  temp_opts = *opts;
  temp_opts.server_minor_version = 8;
  SVN_ERR(prepare_revprop_repo(&fs, repo_name, max_rev, SHARD_SIZE,
                               &temp_opts, pool));

  if (upgrade_before_txns)
    {
      /* upgrade to final repo format (using log addressing) and re-open */
      SVN_ERR(svn_fs_upgrade2(repo_name, NULL, NULL, NULL, NULL, pool));
      SVN_ERR(svn_fs_open2(&fs, repo_name, svn_fs_config(fs, pool), pool,
                           pool));
    }

  /* Create 4 concurrent transactions */
  txns = apr_array_make(pool, SHARD_SIZE, sizeof(svn_fs_txn_t *));
  txn_names = apr_array_make(pool, SHARD_SIZE, sizeof(const char *));
  for (i = 0; i < SHARD_SIZE; ++i)
    {
      svn_fs_txn_t *txn;
      const char *txn_name;

      SVN_ERR(svn_fs_begin_txn(&txn, fs, max_rev, pool));
      APR_ARRAY_PUSH(txns, svn_fs_txn_t *) = txn;

      SVN_ERR(svn_fs_txn_name(&txn_name, txn, pool));
      APR_ARRAY_PUSH(txn_names, const char *) = txn_name;
    }

  /* Let all txns touch at least 2 files.
   * Thus, the addressing data of at least one representation in the txn
   * will differ between addressing modes. */
  for (i = 0; i < SHARD_SIZE; ++i)
    {
      svn_fs_txn_t *txn = APR_ARRAY_IDX(txns, i, svn_fs_txn_t *);
      SVN_ERR(svn_fs_txn_root(&root, txn, pool));

      for (k = 0; k < 2; ++k)
        {
          svn_stream_t *stream;
          const char *file_path = paths[i][k];
          svn_pool_clear(iterpool);

          SVN_ERR(svn_fs_apply_text(&stream, root, file_path, NULL, iterpool));
          SVN_ERR(svn_stream_printf(stream, iterpool,
                                    "This is file %s in txn %d",
                                    file_path, i));
          SVN_ERR(svn_stream_close(stream));
        }
    }

  if (!upgrade_before_txns)
    {
      /* upgrade to final repo format (using log addressing) and re-open */
      SVN_ERR(svn_fs_upgrade2(repo_name, NULL, NULL, NULL, NULL, pool));
      SVN_ERR(svn_fs_open2(&fs, repo_name, svn_fs_config(fs, pool), pool,
                           pool));
    }

  /* Commit all transactions
   * (in reverse order to make things more interesting) */
  for (i = SHARD_SIZE - 1; i >= 0; --i)
    {
      svn_fs_txn_t *txn;
      const char *txn_name = APR_ARRAY_IDX(txn_names, i, const char *);
      svn_pool_clear(iterpool);

      SVN_ERR(svn_fs_open_txn(&txn, fs, txn_name, iterpool));
      SVN_ERR(svn_fs_commit_txn(NULL, &rev, txn, iterpool));
    }

  /* Further changes to fill the shard */

  SVN_ERR(svn_fs_youngest_rev(&rev, fs, pool));
  SVN_TEST_ASSERT(rev == SHARD_SIZE + max_rev + 1);

  while ((rev + 1) % SHARD_SIZE)
    {
      svn_fs_txn_t *txn;
      if (rev % SHARD_SIZE == 0)
        break;

      svn_pool_clear(iterpool);

      SVN_ERR(svn_fs_begin_txn(&txn, fs, rev, iterpool));
      SVN_ERR(svn_fs_txn_root(&root, txn, iterpool));
      SVN_ERR(svn_test__set_file_contents(root, "iota",
                                          get_rev_contents(rev + 1, iterpool),
                                          iterpool));
      SVN_ERR(svn_fs_commit_txn(NULL, &rev, txn, iterpool));
    }

  /* Make sure to close all file handles etc. from the last iteration */

  svn_pool_clear(iterpool);

  /* Pack repo to verify that old and new shard get packed according to
     their respective addressing mode */

  SVN_ERR(svn_fs_pack(repo_name, NULL, NULL, NULL, NULL, pool));

  /* verify that our changes got in */

  SVN_ERR(svn_fs_revision_root(&root, fs, rev, pool));
  for (i = 0; i < SHARD_SIZE; ++i)
    {
      for (k = 0; k < 2; ++k)
        {
          svn_stream_t *stream;
          const char *file_path = paths[i][k];
          svn_string_t *string;
          const char *expected;

          svn_pool_clear(iterpool);

          SVN_ERR(svn_fs_file_contents(&stream, root, file_path, iterpool));
          SVN_ERR(svn_string_from_stream(&string, stream, iterpool, iterpool));

          expected = apr_psprintf(pool,"This is file %s in txn %d",
                                  file_path, i);
          SVN_TEST_STRING_ASSERT(string->data, expected);
        }
    }

  /* verify that the indexes are consistent, we calculated the correct
     low-level checksums etc. */
  SVN_ERR(svn_fs_verify(repo_name, NULL,
                        SVN_INVALID_REVNUM, SVN_INVALID_REVNUM,
                        NULL, NULL, NULL, NULL, pool));
  for (; rev >= 0; --rev)
    {
      svn_pool_clear(iterpool);
      SVN_ERR(svn_fs_revision_root(&root, fs, rev, iterpool));
      SVN_ERR(svn_fs_verify_root(root, iterpool));
    }

  return SVN_NO_ERROR;
}
#undef SHARD_SIZE

#define REPO_NAME "test-repo-upgrade_new_txns_to_log_addressing"
#define MAX_REV 8
static svn_error_t *
upgrade_new_txns_to_log_addressing(const svn_test_opts_t *opts,
                                   apr_pool_t *pool)
{
  SVN_ERR(upgrade_txns_to_log_addressing(opts, REPO_NAME, MAX_REV, TRUE,
                                         pool));

  return SVN_NO_ERROR;
}
#undef REPO_NAME
#undef MAX_REV

/* ------------------------------------------------------------------------ */
#define REPO_NAME "test-repo-upgrade_old_txns_to_log_addressing"
#define MAX_REV 8
static svn_error_t *
upgrade_old_txns_to_log_addressing(const svn_test_opts_t *opts,
                                   apr_pool_t *pool)
{
  SVN_ERR(upgrade_txns_to_log_addressing(opts, REPO_NAME, MAX_REV, FALSE,
                                         pool));

  return SVN_NO_ERROR;
}

#undef REPO_NAME
#undef MAX_REV

/* ------------------------------------------------------------------------ */

#define REPO_NAME "test-repo-metadata_checksumming"
static svn_error_t *
metadata_checksumming(const svn_test_opts_t *opts,
                  apr_pool_t *pool)
{
  svn_fs_t *fs;
  const char *repo_path, *r0_path;
  apr_hash_t *fs_config = apr_hash_make(pool);
  svn_stringbuf_t *r0;
  svn_fs_root_t *root;
  apr_hash_t *dir;

  /* Skip this test unless we are FSFS f7+ */
  if ((strcmp(opts->fs_type, "fsfs") != 0)
      || (opts->server_minor_version && (opts->server_minor_version < 9)))
    return svn_error_create(SVN_ERR_TEST_SKIPPED, NULL,
                            "pre-1.9 SVN doesn't checksum metadata");

  /* Create the file system to fiddle with. */
  SVN_ERR(svn_test__create_fs(&fs, REPO_NAME, opts, pool));
  repo_path = svn_fs_path(fs, pool);

  /* Manipulate the data on disk.
   * (change id from '0.0.*' to '1.0.*') */
  r0_path = svn_dirent_join_many(pool, repo_path, "revs", "0", "0",
                                 SVN_VA_NULL);
  SVN_ERR(svn_stringbuf_from_file2(&r0, r0_path, pool));
  r0->data[21] = '1';
  SVN_ERR(svn_io_remove_file2(r0_path, FALSE, pool));
  SVN_ERR(svn_io_file_create_bytes(r0_path, r0->data, r0->len, pool));

  /* Reading the corrupted data on the normal code path triggers no error.
   * Use a separate namespace to avoid simply reading data from cache. */
  svn_hash_sets(fs_config, SVN_FS_CONFIG_FSFS_CACHE_NS,
                           svn_uuid_generate(pool));
  SVN_ERR(svn_fs_open2(&fs, repo_path, fs_config, pool, pool));
  SVN_ERR(svn_fs_revision_root(&root, fs, 0, pool));
  SVN_ERR(svn_fs_dir_entries(&dir, root, "/", pool));

  /* The block-read code path uses the P2L index information and compares
   * low-level checksums.  Again, separate cache namespace. */
  svn_hash_sets(fs_config, SVN_FS_CONFIG_FSFS_CACHE_NS,
                           svn_uuid_generate(pool));
  svn_hash_sets(fs_config, SVN_FS_CONFIG_FSFS_BLOCK_READ, "1");
  SVN_ERR(svn_fs_open2(&fs, repo_path, fs_config, pool, pool));
  SVN_ERR(svn_fs_revision_root(&root, fs, 0, pool));
  SVN_TEST_ASSERT_ERROR(svn_fs_dir_entries(&dir, root, "/", pool),
                        SVN_ERR_CHECKSUM_MISMATCH);

  return SVN_NO_ERROR;
}

#undef REPO_NAME

/* ------------------------------------------------------------------------ */

#define REPO_NAME "test-repo-revprop_caching_on_off"
static svn_error_t *
revprop_caching_on_off(const svn_test_opts_t *opts,
                       apr_pool_t *pool)
{
  svn_fs_t *fs1;
  svn_fs_t *fs2;
  apr_hash_t *fs_config;
  svn_string_t *value;
  const svn_string_t *another_value_for_avoiding_warnings_from_a_broken_api;
  const svn_string_t *new_value = svn_string_create("new", pool);

  if (strcmp(opts->fs_type, "fsfs") != 0)
    return svn_error_create(SVN_ERR_TEST_SKIPPED, NULL, NULL);

  /* Open two filesystem objects, enable revision property caching
   * in one of them. */
  SVN_ERR(svn_test__create_fs(&fs1, REPO_NAME, opts, pool));

  fs_config = apr_hash_make(pool);
  svn_hash_sets(fs_config, SVN_FS_CONFIG_FSFS_CACHE_REVPROPS, "1");

  SVN_ERR(svn_fs_open2(&fs2, svn_fs_path(fs1, pool), fs_config, pool, pool));

  /* With inefficient named atomics, the filesystem will output a warning
     and disable the revprop caching, but we still would like to test
     these cases.  Ignore the warning(s). */
  svn_fs_set_warning_func(fs2, ignore_fs_warnings, NULL);

  SVN_ERR(svn_fs_revision_prop(&value, fs2, 0, "svn:date", pool));
  another_value_for_avoiding_warnings_from_a_broken_api = value;
  SVN_ERR(svn_fs_change_rev_prop2(
              fs1, 0, "svn:date",
              &another_value_for_avoiding_warnings_from_a_broken_api,
              new_value, pool));

  /* Expect the change to be visible through both objects.*/
  SVN_ERR(svn_fs_revision_prop(&value, fs1, 0, "svn:date", pool));
  SVN_TEST_STRING_ASSERT(value->data, "new");

  SVN_ERR(svn_fs_revision_prop(&value, fs2, 0, "svn:date", pool));
  SVN_TEST_STRING_ASSERT(value->data, "new");

  return SVN_NO_ERROR;
}

#undef REPO_NAME

/* ------------------------------------------------------------------------ */

static svn_error_t *
id_parser_test(const svn_test_opts_t *opts,
               apr_pool_t *pool)
{
 #define LONG_MAX_STR #LONG_MAX

  /* Verify the revision number parser (e.g. first element of a txn ID) */
  svn_fs_fs__id_part_t id_part;
  SVN_ERR(svn_fs_fs__id_txn_parse(&id_part, "0-0"));

#if LONG_MAX == 2147483647L
  SVN_ERR(svn_fs_fs__id_txn_parse(&id_part, "2147483647-0"));

  /* Trigger all sorts of overflow conditions. */
  SVN_TEST_ASSERT_ERROR(svn_fs_fs__id_txn_parse(&id_part, "2147483648-0"),
                        SVN_ERR_FS_MALFORMED_TXN_ID);
  SVN_TEST_ASSERT_ERROR(svn_fs_fs__id_txn_parse(&id_part, "21474836470-0"),
                        SVN_ERR_FS_MALFORMED_TXN_ID);
  SVN_TEST_ASSERT_ERROR(svn_fs_fs__id_txn_parse(&id_part, "21474836479-0"),
                        SVN_ERR_FS_MALFORMED_TXN_ID);
  SVN_TEST_ASSERT_ERROR(svn_fs_fs__id_txn_parse(&id_part, "4294967295-0"),
                        SVN_ERR_FS_MALFORMED_TXN_ID);
  SVN_TEST_ASSERT_ERROR(svn_fs_fs__id_txn_parse(&id_part, "4294967296-0"),
                        SVN_ERR_FS_MALFORMED_TXN_ID);
  SVN_TEST_ASSERT_ERROR(svn_fs_fs__id_txn_parse(&id_part, "4294967304-0"),
                        SVN_ERR_FS_MALFORMED_TXN_ID);
  SVN_TEST_ASSERT_ERROR(svn_fs_fs__id_txn_parse(&id_part, "4294967305-0"),
                        SVN_ERR_FS_MALFORMED_TXN_ID);
  SVN_TEST_ASSERT_ERROR(svn_fs_fs__id_txn_parse(&id_part, "42949672950-0"),
                        SVN_ERR_FS_MALFORMED_TXN_ID);
  SVN_TEST_ASSERT_ERROR(svn_fs_fs__id_txn_parse(&id_part, "42949672959-0"),
                        SVN_ERR_FS_MALFORMED_TXN_ID);

  /* 0x120000000 = 4831838208.
   * 483183820 < 10*483183820 mod 2^32 = 536870904 */
  SVN_TEST_ASSERT_ERROR(svn_fs_fs__id_txn_parse(&id_part, "4831838208-0"),
                        SVN_ERR_FS_MALFORMED_TXN_ID);
#else
  SVN_ERR(svn_fs_fs__id_txn_parse(&id_part, "9223372036854775807-0"));

  /* Trigger all sorts of overflow conditions. */
  SVN_TEST_ASSERT_ERROR(svn_fs_fs__id_txn_parse(&id_part,
                                                "9223372036854775808-0"),
                        SVN_ERR_FS_MALFORMED_TXN_ID);
  SVN_TEST_ASSERT_ERROR(svn_fs_fs__id_txn_parse(&id_part,
                                                "92233720368547758070-0"),
                        SVN_ERR_FS_MALFORMED_TXN_ID);
  SVN_TEST_ASSERT_ERROR(svn_fs_fs__id_txn_parse(&id_part,
                                                "92233720368547758079-0"),
                        SVN_ERR_FS_MALFORMED_TXN_ID);
  SVN_TEST_ASSERT_ERROR(svn_fs_fs__id_txn_parse(&id_part,
                                                "18446744073709551615-0"),
                        SVN_ERR_FS_MALFORMED_TXN_ID);
  SVN_TEST_ASSERT_ERROR(svn_fs_fs__id_txn_parse(&id_part,
                                                "18446744073709551616-0"),
                        SVN_ERR_FS_MALFORMED_TXN_ID);
  SVN_TEST_ASSERT_ERROR(svn_fs_fs__id_txn_parse(&id_part,
                                                "18446744073709551624-0"),
                        SVN_ERR_FS_MALFORMED_TXN_ID);
  SVN_TEST_ASSERT_ERROR(svn_fs_fs__id_txn_parse(&id_part,
                                                "18446744073709551625-0"),
                        SVN_ERR_FS_MALFORMED_TXN_ID);
  SVN_TEST_ASSERT_ERROR(svn_fs_fs__id_txn_parse(&id_part,
                                                "184467440737095516150-0"),
                        SVN_ERR_FS_MALFORMED_TXN_ID);
  SVN_TEST_ASSERT_ERROR(svn_fs_fs__id_txn_parse(&id_part,
                                                "184467440737095516159-0"),
                        SVN_ERR_FS_MALFORMED_TXN_ID);

  /* 0x12000000000000000 = 20752587082923245568.
   * 2075258708292324556 < 10*2075258708292324556 mod 2^32 = 2305843009213693944 */
  SVN_TEST_ASSERT_ERROR(svn_fs_fs__id_txn_parse(&id_part,
                                                "20752587082923245568-0"),
                        SVN_ERR_FS_MALFORMED_TXN_ID);
#endif

  /* Invalid characters */
  SVN_TEST_ASSERT_ERROR(svn_fs_fs__id_txn_parse(&id_part, "2e4-0"),
                        SVN_ERR_FS_MALFORMED_TXN_ID);
  SVN_TEST_ASSERT_ERROR(svn_fs_fs__id_txn_parse(&id_part, "2-4-0"),
                        SVN_ERR_FS_MALFORMED_TXN_ID);

  return SVN_NO_ERROR;
}

#undef REPO_NAME

/* ------------------------------------------------------------------------ */

#define REPO_NAME "test-repo-plain_0_length"

static svn_error_t *
receive_index(const svn_fs_fs__p2l_entry_t *entry,
              void *baton,
              apr_pool_t *scratch_pool)
{
  apr_array_header_t *entries = baton;
  APR_ARRAY_PUSH(entries, svn_fs_fs__p2l_entry_t *)
    = apr_pmemdup(entries->pool, entry, sizeof(*entry));

  return SVN_NO_ERROR;
}

static apr_size_t
stringbuf_find(svn_stringbuf_t *rev_contents,
               const char *substring)
{
  apr_size_t i;
  apr_size_t len = strlen(substring);

  for (i = 0; i < rev_contents->len - len + 1; ++i)
      if (!memcmp(rev_contents->data + i, substring, len))
        return i;

  return APR_SIZE_MAX;
}

static svn_error_t *
plain_0_length(const svn_test_opts_t *opts,
               apr_pool_t *pool)
{
  svn_fs_t *fs;
  fs_fs_data_t *ffd;
  svn_fs_txn_t *txn;
  svn_fs_root_t *root;
  svn_revnum_t rev;
  const char *rev_path;
  svn_stringbuf_t *rev_contents;
  apr_hash_t *fs_config;
  svn_filesize_t file_length;
  apr_size_t offset;

  if (strcmp(opts->fs_type, "fsfs") != 0)
    return svn_error_create(SVN_ERR_TEST_SKIPPED, NULL, NULL);

  /* Create a repo that does not deltify properties and does not share reps
     on its own - makes it easier to do that later by hand. */
  SVN_ERR(svn_test__create_fs(&fs, REPO_NAME, opts, pool));
  ffd = fs->fsap_data;
  ffd->deltify_properties = FALSE;
  ffd->rep_sharing_allowed = FALSE;

  /* Create one file node with matching contents and property reps. */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, 0, pool));
  SVN_ERR(svn_fs_txn_root(&root, txn, pool));
  SVN_ERR(svn_fs_make_file(root, "foo", pool));
  SVN_ERR(svn_test__set_file_contents(root, "foo", "END\n", pool));
  SVN_ERR(svn_fs_change_node_prop(root, "foo", "x", NULL, pool));
  SVN_ERR(svn_fs_commit_txn(NULL, &rev, txn, pool));

  /* Redirect text rep to props rep. */
  rev_path = svn_fs_fs__path_rev_absolute(fs, rev, pool);
  SVN_ERR(svn_stringbuf_from_file2(&rev_contents, rev_path, pool));

  offset = stringbuf_find(rev_contents, "id: ");
  if (offset != APR_SIZE_MAX)
    {
      node_revision_t *noderev;
      svn_stringbuf_t *noderev_str;

      /* Read the noderev. */
      svn_stream_t *stream = svn_stream_from_stringbuf(rev_contents, pool);
      SVN_ERR(svn_stream_skip(stream, offset));
      SVN_ERR(svn_fs_fs__read_noderev(&noderev, stream, pool, pool));
      SVN_ERR(svn_stream_close(stream));

      /* Tweak the DATA_REP. */
      noderev->data_rep->revision = noderev->prop_rep->revision;
      noderev->data_rep->item_index = noderev->prop_rep->item_index;
      noderev->data_rep->size = noderev->prop_rep->size;
      noderev->data_rep->expanded_size = 0;

      /* Serialize it back. */
      noderev_str = svn_stringbuf_create_empty(pool);
      stream = svn_stream_from_stringbuf(noderev_str, pool);
      SVN_ERR(svn_fs_fs__write_noderev(stream, noderev, ffd->format,
                                       svn_fs_fs__fs_supports_mergeinfo(fs),
                                       pool));
      SVN_ERR(svn_stream_close(stream));

      /* Patch the revision contents */
      memcpy(rev_contents->data + offset, noderev_str->data, noderev_str->len);
    }

  SVN_ERR(svn_io_write_atomic2(rev_path, rev_contents->data,
                               rev_contents->len, NULL, FALSE,
                               pool));

  if (svn_fs_fs__use_log_addressing(fs))
    {
      /* Refresh index data (checksums). */
      apr_array_header_t *entries = apr_array_make(pool, 4, sizeof(void *));
      SVN_ERR(svn_fs_fs__dump_index(fs, rev, receive_index, entries,
                                    NULL, NULL, pool));
      SVN_ERR(svn_fs_fs__load_index(fs, rev, entries, pool));
    }

  /* Create an independent FS instances with separate caches etc. */
  fs_config = apr_hash_make(pool);
  svn_hash_sets(fs_config, SVN_FS_CONFIG_FSFS_CACHE_NS,
                svn_uuid_generate(pool));
  SVN_ERR(svn_fs_open2(&fs, REPO_NAME, fs_config, pool, pool));

  /* Now, check that we get the correct file length. */
  SVN_ERR(svn_fs_revision_root(&root, fs, rev, pool));
  SVN_ERR(svn_fs_file_length(&file_length, root, "foo", pool));

  SVN_TEST_ASSERT(file_length == 4);

  return SVN_NO_ERROR;
}

#undef REPO_NAME

/* ------------------------------------------------------------------------ */

#define REPO_NAME "test-repo-rep_sharing_effectiveness"

static int
count_substring(svn_stringbuf_t *string,
                const char *needle)
{
  int count = 0;
  apr_size_t len = strlen(needle);
  apr_size_t pos;

  for (pos = 0; pos + len <= string->len; ++pos)
    if (memcmp(string->data + pos, needle, len) == 0)
      ++count;

  return count;
}

static svn_error_t *
count_representations(int *count,
                      svn_fs_t *fs,
                      svn_revnum_t revision,
                      apr_pool_t *pool)
{
  svn_stringbuf_t *rev_contents;
  const char *rev_path = svn_fs_fs__path_rev_absolute(fs, revision, pool);
  SVN_ERR(svn_stringbuf_from_file2(&rev_contents, rev_path, pool));

  *count = count_substring(rev_contents, "PLAIN")
         + count_substring(rev_contents, "DELTA");

  return SVN_NO_ERROR;
}

/* Repeat string S many times to make it big enough for deltification etc.
   to kick in. */
static const char*
multiply_string(const char *s,
                apr_pool_t *pool)
{
  svn_stringbuf_t *temp = svn_stringbuf_create(s, pool);

  int i;
  for (i = 0; i < 7; ++i)
    svn_stringbuf_insert(temp, temp->len, temp->data, temp->len);

  return temp->data;
}

static svn_error_t *
rep_sharing_effectiveness(const svn_test_opts_t *opts,
                          apr_pool_t *pool)
{
  svn_fs_t *fs;
  fs_fs_data_t *ffd;
  svn_fs_txn_t *txn;
  svn_fs_root_t *root;
  svn_revnum_t rev;
  const char *hello_str = multiply_string("Hello, ", pool);
  const char *world_str = multiply_string("World!", pool);
  const char *goodbye_str = multiply_string("Goodbye!", pool);

  if (strcmp(opts->fs_type, "fsfs") != 0)
    return svn_error_create(SVN_ERR_TEST_SKIPPED, NULL, NULL);

  /* Create a repo that and explicitly enable rep sharing. */
  SVN_ERR(svn_test__create_fs(&fs, REPO_NAME, opts, pool));

  ffd = fs->fsap_data;
  if (ffd->format < SVN_FS_FS__MIN_REP_SHARING_FORMAT)
    return svn_error_create(SVN_ERR_TEST_SKIPPED, NULL, NULL);

  ffd->rep_sharing_allowed = TRUE;

  /* Revision 1: create 2 files with different content. */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, 0, pool));
  SVN_ERR(svn_fs_txn_root(&root, txn, pool));
  SVN_ERR(svn_fs_make_file(root, "foo", pool));
  SVN_ERR(svn_test__set_file_contents(root, "foo", hello_str, pool));
  SVN_ERR(svn_fs_make_file(root, "bar", pool));
  SVN_ERR(svn_test__set_file_contents(root, "bar", world_str, pool));
  SVN_ERR(svn_fs_commit_txn(NULL, &rev, txn, pool));

  /* Revision 2: modify a file to match another file's r1 content and
                 add another with the same content.
                 (classic rep-sharing). */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, rev, pool));
  SVN_ERR(svn_fs_txn_root(&root, txn, pool));
  SVN_ERR(svn_test__set_file_contents(root, "foo", world_str, pool));
  SVN_ERR(svn_fs_make_file(root, "baz", pool));
  SVN_ERR(svn_test__set_file_contents(root, "baz", hello_str, pool));
  SVN_ERR(svn_fs_commit_txn(NULL, &rev, txn, pool));

  /* Revision 3: modify all files to some new, identical content and add
                 another with the same content.
                 (in-revision rep-sharing). */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, rev, pool));
  SVN_ERR(svn_fs_txn_root(&root, txn, pool));
  SVN_ERR(svn_test__set_file_contents(root, "foo", goodbye_str, pool));
  SVN_ERR(svn_test__set_file_contents(root, "bar", goodbye_str, pool));
  SVN_ERR(svn_test__set_file_contents(root, "baz", goodbye_str, pool));
  SVN_ERR(svn_fs_make_file(root, "qux", pool));
  SVN_ERR(svn_test__set_file_contents(root, "qux", goodbye_str, pool));
  SVN_ERR(svn_fs_commit_txn(NULL, &rev, txn, pool));

  /* Verify revision contents. */
  {
    const struct {
      svn_revnum_t revision;
      const char *file;
      const char *contents;
    } expected[] = {
      { 1, "foo", "Hello, " },
      { 1, "bar", "World!" },
      { 2, "foo", "World!" },
      { 2, "bar", "World!" },
      { 2, "baz", "Hello, " },
      { 3, "foo", "Goodbye!" },
      { 3, "bar", "Goodbye!" },
      { 3, "baz", "Goodbye!" },
      { 3, "qux", "Goodbye!" },
      { SVN_INVALID_REVNUM, NULL, NULL }
    };

    int i;
    apr_pool_t *iterpool = svn_pool_create(pool);
    for (i = 0; SVN_IS_VALID_REVNUM(expected[i].revision); ++i)
      {
        svn_stringbuf_t *str;

        SVN_ERR(svn_fs_revision_root(&root, fs, expected[i].revision,
                                     iterpool));
        SVN_ERR(svn_test__get_file_contents(root, expected[i].file, &str,
                                            iterpool));

        SVN_TEST_STRING_ASSERT(str->data,
                               multiply_string(expected[i].contents,
                                               iterpool));
      }

    svn_pool_destroy(iterpool);
  }

  /* Verify that rep sharing eliminated most reps. */
  {
    /* Number of expected representations (including the root directory). */
    const int expected[] = { 1, 3, 1, 2 } ;

    svn_revnum_t i;
    apr_pool_t *iterpool = svn_pool_create(pool);
    for (i = 0; i <= rev; ++i)
      {
        int count;
        SVN_ERR(count_representations(&count, fs, i, iterpool));
        SVN_TEST_ASSERT(count == expected[i]);
      }

    svn_pool_destroy(iterpool);
  }

  return SVN_NO_ERROR;
}

#undef REPO_NAME

/* ------------------------------------------------------------------------ */

#define REPO_NAME "test-repo-delta_chain_with_plain"

static svn_error_t *
delta_chain_with_plain(const svn_test_opts_t *opts,
                       apr_pool_t *pool)
{
  svn_fs_t *fs;
  fs_fs_data_t *ffd;
  svn_fs_txn_t *txn;
  svn_fs_root_t *root;
  svn_revnum_t rev;
  svn_stringbuf_t *prop_value, *contents, *contents2, *hash_rep;
  int i;
  apr_hash_t *fs_config, *props;

  if (strcmp(opts->fs_type, "fsfs") != 0)
    return svn_error_create(SVN_ERR_TEST_SKIPPED, NULL, NULL);

  /* Reproducing issue #4577 without the r1676667 fix is much harder in 1.9+
   * than it was in 1.8.  The reason is that 1.9+ won't deltify small reps
   * nor against small reps.  So, we must construct relatively large PLAIN
   * and DELTA reps.
   *
   * The idea is to construct a PLAIN prop rep, make a file share that as
   * its text rep, grow the file considerably (to make the PLAIN rep later
   * read beyond EOF) and then replace it entirely with another longish
   * contents.
   */

  /* Create a repo that and explicitly enable rep sharing. */
  SVN_ERR(svn_test__create_fs(&fs, REPO_NAME, opts, pool));

  ffd = fs->fsap_data;
  if (ffd->format < SVN_FS_FS__MIN_REP_SHARING_FORMAT)
    return svn_error_create(SVN_ERR_TEST_SKIPPED, NULL, NULL);

  ffd->rep_sharing_allowed = TRUE;

  /* Make sure all props are stored as PLAIN reps. */
  ffd->deltify_properties = FALSE;

  /* Construct various content strings.
   * Note that props need to be shorter than the file contents. */
  prop_value = svn_stringbuf_create("prop", pool);
  for (i = 0; i < 10; ++i)
    svn_stringbuf_appendstr(prop_value, prop_value);

  contents = svn_stringbuf_create("Some text.", pool);
  for (i = 0; i < 10; ++i)
    svn_stringbuf_appendstr(contents, contents);

  contents2 = svn_stringbuf_create("Totally new!", pool);
  for (i = 0; i < 10; ++i)
    svn_stringbuf_appendstr(contents2, contents2);

  /* Revision 1: create a property rep. */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, 0, pool));
  SVN_ERR(svn_fs_txn_root(&root, txn, pool));
  SVN_ERR(svn_fs_change_node_prop(root, "/", "p",
                                  svn_string_create(prop_value->data, pool),
                                  pool));
  SVN_ERR(svn_fs_commit_txn(NULL, &rev, txn, pool));

  /* Revision 2: create a file that shares the text rep with the PLAIN
   * property rep from r1. */
  props = apr_hash_make(pool);
  svn_hash_sets(props, "p", svn_string_create(prop_value->data, pool));

  hash_rep = svn_stringbuf_create_empty(pool);
  svn_hash_write2(props, svn_stream_from_stringbuf(hash_rep, pool), "END",
                  pool);

  SVN_ERR(svn_fs_begin_txn(&txn, fs, rev, pool));
  SVN_ERR(svn_fs_txn_root(&root, txn, pool));
  SVN_ERR(svn_fs_make_file(root, "foo", pool));
  SVN_ERR(svn_test__set_file_contents(root, "foo", hash_rep->data, pool));
  SVN_ERR(svn_fs_commit_txn(NULL, &rev, txn, pool));

  /* Revision 3: modify the file contents to a long-ish full text
   * (~10kByte, longer than the r1 revision file). */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, rev, pool));
  SVN_ERR(svn_fs_txn_root(&root, txn, pool));
  SVN_ERR(svn_test__set_file_contents(root, "foo", contents->data, pool));
  SVN_ERR(svn_fs_commit_txn(NULL, &rev, txn, pool));

  /* Revision 4: replace file contents to something disjoint from r3. */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, rev, pool));
  SVN_ERR(svn_fs_txn_root(&root, txn, pool));
  SVN_ERR(svn_test__set_file_contents(root, "foo", contents2->data, pool));
  SVN_ERR(svn_fs_commit_txn(NULL, &rev, txn, pool));

  /* Getting foo@4 must work.  To make sure we actually read from disk,
   * use a new FS instance with disjoint caches. */
  fs_config = apr_hash_make(pool);
  svn_hash_sets(fs_config, SVN_FS_CONFIG_FSFS_CACHE_NS,
                           svn_uuid_generate(pool));
  SVN_ERR(svn_fs_open2(&fs, REPO_NAME, fs_config, pool, pool));

  SVN_ERR(svn_fs_revision_root(&root, fs, rev, pool));
  SVN_ERR(svn_test__get_file_contents(root, "foo", &contents, pool));
  SVN_TEST_STRING_ASSERT(contents->data, contents2->data);

  return SVN_NO_ERROR;
}

#undef REPO_NAME

/* ------------------------------------------------------------------------ */

#define REPO_NAME "test-repo-compare_0_length_rep"

static svn_error_t *
compare_0_length_rep(const svn_test_opts_t *opts,
                     apr_pool_t *pool)
{
  svn_fs_t *fs;
  svn_fs_txn_t *txn;
  svn_fs_root_t *root;
  svn_revnum_t rev;
  int i, k;
  apr_hash_t *fs_config;

  /* Test expectations. */
#define no_rep_file      "no-rep"
#define empty_plain_file "empty-plain"
#define plain_file       "plain"
#define empty_delta_file "empty-delta"
#define delta_file       "delta"

  enum { COUNT = 5 };
  const char *file_names[COUNT] = { no_rep_file,
                                    empty_plain_file, 
                                    plain_file,
                                    empty_delta_file,
                                    delta_file };

  int equal[COUNT][COUNT] = { { 1, 1, 0, 1, 0 },
                              { 1, 1, 0, 1, 0 },
                              { 0, 0, 1, 0, 1 },
                              { 1, 1, 0, 1, 0 },
                              { 0, 0, 1, 0, 1 } };

  /* Well, this club is FSFS only ... */
  if (strcmp(opts->fs_type, "fsfs") != 0)
    return svn_error_create(SVN_ERR_TEST_SKIPPED, NULL, NULL);

  /* We want to check that whether NULL reps, empty PLAIN reps and empty
   * DELTA reps are all considered equal, yet different from non-empty reps.
   *
   * Because we can't create empty PLAIN reps with recent formats anymore,
   * some format selection & upgrade gymnastics is needed. */

  /* Create a format 1 repository.
   * This one does not support DELTA reps, so all is PLAIN. */
  fs_config = apr_hash_make(pool);
  svn_hash_sets(fs_config, SVN_FS_CONFIG_PRE_1_4_COMPATIBLE, "x");
  SVN_ERR(svn_test__create_fs2(&fs, REPO_NAME, opts, fs_config, pool));

  /* Revision 1, create 3 files:
   * One with no rep, one with an empty rep and a non-empty one. */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, 0, pool));
  SVN_ERR(svn_fs_txn_root(&root, txn, pool));
  SVN_ERR(svn_fs_make_file(root, no_rep_file, pool));
  SVN_ERR(svn_fs_make_file(root, empty_plain_file, pool));
  SVN_ERR(svn_test__set_file_contents(root, empty_plain_file, "", pool));
  SVN_ERR(svn_fs_make_file(root, plain_file, pool));
  SVN_ERR(svn_test__set_file_contents(root, plain_file, "x", pool));
  SVN_ERR(svn_fs_commit_txn(NULL, &rev, txn, pool));

  /* Upgrade the file system format. */
  SVN_ERR(svn_fs_upgrade2(REPO_NAME, NULL, NULL, NULL, NULL, pool));
  SVN_ERR(svn_fs_open2(&fs, REPO_NAME, NULL, pool, pool));

  /* Revision 2, create two more files:
   * a file with an empty DELTA rep and a non-empty one. */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, rev, pool));
  SVN_ERR(svn_fs_txn_root(&root, txn, pool));
  SVN_ERR(svn_fs_make_file(root, empty_delta_file, pool));
  SVN_ERR(svn_test__set_file_contents(root, empty_delta_file, "", pool));
  SVN_ERR(svn_fs_make_file(root, delta_file, pool));
  SVN_ERR(svn_test__set_file_contents(root, delta_file, "x", pool));
  SVN_ERR(svn_fs_commit_txn(NULL, &rev, txn, pool));

  /* Now compare. */
  SVN_ERR(svn_fs_revision_root(&root, fs, rev, pool));
  for (i = 0; i < COUNT; ++i)
    for (k = 0; k < COUNT; ++k)
      {
        svn_boolean_t different;
        SVN_ERR(svn_fs_contents_different(&different, root, file_names[i],
                                          root, file_names[k], pool));
        SVN_TEST_ASSERT(different != equal[i][k]);
      }

  return SVN_NO_ERROR;
}

#undef REPO_NAME

/* ------------------------------------------------------------------------ */
/* Verify that the format 7 pack logic works even if we can't fit all index
   metadata into memory. */
#define REPO_NAME "test-repo-pack-with-limited-memory"
#define SHARD_SIZE 4
#define MAX_REV (2 * SHARD_SIZE - 1)
static svn_error_t *
pack_with_limited_memory(const svn_test_opts_t *opts,
                         apr_pool_t *pool)
{
  apr_size_t max_mem;
  apr_pool_t *iterpool = svn_pool_create(pool);

  /* Bail (with success) on known-untestable scenarios */
  if (opts->server_minor_version && (opts->server_minor_version < 9))
    return svn_error_create(SVN_ERR_TEST_SKIPPED, NULL,
                            "pre-1.9 SVN doesn't support reordering packs");

  /* Run with an increasing memory allowance such that we cover all
     splitting scenarios. */
  for (max_mem = 350; max_mem < 8000; max_mem += max_mem / 2)
    {
      const char *dir;
      svn_fs_t *fs;

      svn_pool_clear(iterpool);

      /* Create a filesystem. */
      dir = apr_psprintf(iterpool, "%s-%d", REPO_NAME, (int)max_mem);
      SVN_ERR(create_non_packed_filesystem(dir, opts, MAX_REV, SHARD_SIZE,
                                           iterpool));

      /* Pack it with a narrow memory budget. */
      SVN_ERR(svn_fs_open2(&fs, dir, NULL, iterpool, iterpool));
      SVN_ERR(svn_fs_fs__pack(fs, max_mem, NULL, NULL, NULL, NULL,
                              iterpool));

      /* To be sure: Verify that we didn't break the repo. */
      SVN_ERR(svn_fs_verify(dir, NULL, 0, MAX_REV, NULL, NULL, NULL, NULL,
                            iterpool));
    }

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}
#undef REPO_NAME
#undef MAX_REV
#undef SHARD_SIZE

/* ------------------------------------------------------------------------ */

#define REPO_NAME "test-repo-large_delta_against_plain"

static svn_error_t *
large_delta_against_plain(const svn_test_opts_t *opts,
                          apr_pool_t *pool)
{
  svn_fs_t *fs;
  fs_fs_data_t *ffd;
  svn_fs_txn_t *txn;
  svn_fs_root_t *root;
  svn_revnum_t rev;
  svn_stringbuf_t *prop_value;
  svn_string_t *prop_read;
  int i;
  apr_hash_t *fs_config;

  if (strcmp(opts->fs_type, "fsfs") != 0)
    return svn_error_create(SVN_ERR_TEST_SKIPPED, NULL, NULL);

  /* Create a repo that and explicitly enable rep sharing. */
  SVN_ERR(svn_test__create_fs(&fs, REPO_NAME, opts, pool));
  ffd = fs->fsap_data;

  /* Make sure all props are stored as PLAIN reps. */
  ffd->deltify_properties = FALSE;

  /* Construct a property larger than 2 txdelta windows. */
  prop_value = svn_stringbuf_create("prop", pool);
  while (prop_value->len <= 2 * 102400)
    svn_stringbuf_appendstr(prop_value, prop_value);

  /* Revision 1: create a property rep. */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, 0, pool));
  SVN_ERR(svn_fs_txn_root(&root, txn, pool));
  SVN_ERR(svn_fs_change_node_prop(root, "/", "p",
                                  svn_string_create(prop_value->data, pool),
                                  pool));
  SVN_ERR(svn_fs_commit_txn(NULL, &rev, txn, pool));

  /* Now, store them as DELTA reps. */
  ffd->deltify_properties = TRUE;

  /* Construct a property larger than 2 txdelta windows, distinct from the
   * previous one but with a matching "tail". */
  prop_value = svn_stringbuf_create("blob", pool);
  while (prop_value->len <= 2 * 102400)
    svn_stringbuf_appendstr(prop_value, prop_value);
  for (i = 0; i < 100; ++i)
    svn_stringbuf_appendcstr(prop_value, "prop");

  /* Revision 2: modify the property. */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, 1, pool));
  SVN_ERR(svn_fs_txn_root(&root, txn, pool));
  SVN_ERR(svn_fs_change_node_prop(root, "/", "p",
                                  svn_string_create(prop_value->data, pool),
                                  pool));
  SVN_ERR(svn_fs_commit_txn(NULL, &rev, txn, pool));

  /* Reconstructing the property deltified must work.  To make sure we
   * actually read from disk, use a new FS instance with disjoint caches. */
  fs_config = apr_hash_make(pool);
  svn_hash_sets(fs_config, SVN_FS_CONFIG_FSFS_CACHE_NS,
                           svn_uuid_generate(pool));
  SVN_ERR(svn_fs_open2(&fs, REPO_NAME, fs_config, pool, pool));

  SVN_ERR(svn_fs_revision_root(&root, fs, rev, pool));
  SVN_ERR(svn_fs_node_prop(&prop_read, root, "/", "p", pool));
  SVN_TEST_STRING_ASSERT(prop_read->data, prop_value->data);

  return SVN_NO_ERROR;
}

#undef REPO_NAME



/* The test table.  */

static int max_threads = 4;

static struct svn_test_descriptor_t test_funcs[] =
  {
    SVN_TEST_NULL,
    SVN_TEST_OPTS_PASS(pack_filesystem,
                       "pack a FSFS filesystem"),
    SVN_TEST_OPTS_PASS(pack_even_filesystem,
                       "pack FSFS where revs % shard = 0"),
    SVN_TEST_OPTS_PASS(read_packed_fs,
                       "read from a packed FSFS filesystem"),
    SVN_TEST_OPTS_PASS(commit_packed_fs,
                       "commit to a packed FSFS filesystem"),
    SVN_TEST_OPTS_PASS(get_set_revprop_packed_fs,
                       "get/set revprop while packing FSFS filesystem"),
    SVN_TEST_OPTS_PASS(get_set_large_revprop_packed_fs,
                       "get/set large packed revprops in FSFS"),
    SVN_TEST_OPTS_PASS(get_set_huge_revprop_packed_fs,
                       "get/set huge packed revprops in FSFS"),
    SVN_TEST_OPTS_PASS(recover_fully_packed,
                       "recover a fully packed filesystem"),
    SVN_TEST_OPTS_PASS(file_hint_at_shard_boundary,
                       "test file hint at shard boundary"),
    SVN_TEST_OPTS_PASS(test_info,
                       "test svn_fs_info"),
    SVN_TEST_OPTS_PASS(pack_shard_size_one,
                       "test packing with shard size = 1"),
    SVN_TEST_OPTS_PASS(get_set_multiple_huge_revprops_packed_fs,
                       "set multiple huge revprops in packed FSFS"),
    SVN_TEST_OPTS_PASS(upgrade_new_txns_to_log_addressing,
                       "upgrade txns to log addressing in shared FSFS"),
    SVN_TEST_OPTS_PASS(upgrade_old_txns_to_log_addressing,
                       "upgrade txns started before svnadmin upgrade"),
    SVN_TEST_OPTS_PASS(metadata_checksumming,
                       "metadata checksums being checked"),
    SVN_TEST_OPTS_PASS(revprop_caching_on_off,
                       "change revprops with enabled and disabled caching"),
    SVN_TEST_OPTS_PASS(id_parser_test,
                       "id parser test"),
    SVN_TEST_OPTS_PASS(plain_0_length,
                       "file with 0 expanded-length, issue #4554"),
    SVN_TEST_OPTS_PASS(rep_sharing_effectiveness,
                       "rep-sharing effectiveness"),
    SVN_TEST_OPTS_PASS(delta_chain_with_plain,
                       "delta chains starting with PLAIN, issue #4577"),
    SVN_TEST_OPTS_PASS(compare_0_length_rep,
                       "compare empty PLAIN and non-existent reps"),
    SVN_TEST_OPTS_PASS(pack_with_limited_memory,
                       "pack with limited memory for metadata"),
    SVN_TEST_OPTS_PASS(large_delta_against_plain,
                       "large deltas against PLAIN, issue #4658"),
    SVN_TEST_NULL
  };

SVN_TEST_MAIN
