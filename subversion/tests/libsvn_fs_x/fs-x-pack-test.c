/* fs-x-pack-test.c --- tests for the FSX filesystem
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
#include "../../libsvn_fs_x/batch_fsync.h"
#include "../../libsvn_fs_x/fs.h"
#include "../../libsvn_fs_x/reps.h"

#include "svn_pools.h"
#include "svn_props.h"
#include "svn_fs.h"
#include "private/svn_string_private.h"

#include "../svn_test_fs.h"



/*** Helper Functions ***/

/* Write the format number and maximum number of files per directory
   to a new format file in PATH, overwriting a previously existing
   file.  Use POOL for temporary allocation.

   (This implementation is largely stolen from libsvn_fs_fs/fs_fs.c.) */
static svn_error_t *
write_format(const char *path,
             int format,
             int max_files_per_dir,
             apr_pool_t *pool)
{
  const char *contents;

  path = svn_dirent_join(path, "format", pool);
  SVN_TEST_ASSERT(max_files_per_dir > 0);

  contents = apr_psprintf(pool,
                          "%d\n"
                          "layout sharded %d\n",
                          format, max_files_per_dir);

  SVN_ERR(svn_io_write_atomic2(path, contents, strlen(contents),
                               NULL /* copy perms */, FALSE, pool));

  /* And set the perms to make it read only */
  return svn_io_set_file_read_only(path, FALSE, pool);
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

/* Create a packed filesystem in DIR.  Set the shard size to
   SHARD_SIZE and create NUM_REVS number of revisions (in addition to
   r0).  Use POOL for allocations.  After this function successfully
   completes, the filesystem's youngest revision number will be the
   same as NUM_REVS.  */
static svn_error_t *
create_packed_filesystem(const char *dir,
                         const svn_test_opts_t *opts,
                         int num_revs,
                         int shard_size,
                         apr_pool_t *pool)
{
  svn_fs_t *fs;
  svn_fs_txn_t *txn;
  svn_fs_root_t *txn_root;
  const char *conflict;
  svn_revnum_t after_rev;
  apr_pool_t *subpool = svn_pool_create(pool);
  struct pack_notify_baton pnb;
  apr_pool_t *iterpool;
  int version;

  /* Bail (with success) on known-untestable scenarios */
  if (strcmp(opts->fs_type, "fsx") != 0)
    return svn_error_create(SVN_ERR_TEST_SKIPPED, NULL,
                            "this will test FSX repositories only");

  if (opts->server_minor_version && (opts->server_minor_version < 9))
    return svn_error_create(SVN_ERR_TEST_SKIPPED, NULL,
                            "pre-1.9 SVN doesn't support FSX");

  /* Create a filesystem, then close it */
  SVN_ERR(svn_test__create_fs(&fs, dir, opts, subpool));
  svn_pool_destroy(subpool);

  subpool = svn_pool_create(pool);

  /* Rewrite the format file */
  SVN_ERR(svn_io_read_version_file(&version,
                                   svn_dirent_join(dir, "format", subpool),
                                   subpool));
  SVN_ERR(write_format(dir, version, shard_size, subpool));

  /* Reopen the filesystem */
  SVN_ERR(svn_fs_open2(&fs, dir, NULL, subpool, subpool));

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
                     int max_rev,
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
#define REPO_NAME "test-repo-fsx-pack"
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

      /* This file should exist. */
      SVN_ERR(svn_io_check_path(path, &kind, pool));
      if (kind != svn_node_file)
        return svn_error_createf(SVN_ERR_FS_GENERAL, NULL,
                                 "Expected pack file '%s' not found", path);

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
#define REPO_NAME "test-repo-fsx-pack-even"
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
                                   large_log(rev, 15000, pool),
                                   pool));

  /* verify */
  for (rev = 0; rev <= MAX_REV; ++rev)
    {
      SVN_ERR(svn_fs_revision_prop(&prop_value, fs, rev,
                                   SVN_PROP_REVISION_LOG, pool));
      SVN_TEST_STRING_ASSERT(prop_value->data,
                             large_log(rev, 15000, pool)->data);
    }

  /* Put a larger revprop into the last, some middle and the first revision
   * of a pack.  This should cause the packs to split in the middle. */
  SVN_ERR(svn_fs_change_rev_prop(fs, 3, SVN_PROP_REVISION_LOG,
                                 /* rev 0 is not packed */
                                 large_log(3, 37000, pool),
                                 pool));
  SVN_ERR(svn_fs_change_rev_prop(fs, 5, SVN_PROP_REVISION_LOG,
                                 large_log(5, 25000, pool),
                                 pool));
  SVN_ERR(svn_fs_change_rev_prop(fs, 8, SVN_PROP_REVISION_LOG,
                                 large_log(8, 25000, pool),
                                 pool));

  /* verify */
  for (rev = 0; rev <= MAX_REV; ++rev)
    {
      SVN_ERR(svn_fs_revision_prop(&prop_value, fs, rev,
                                   SVN_PROP_REVISION_LOG, pool));

      if (rev == 3)
        SVN_TEST_STRING_ASSERT(prop_value->data,
                               large_log(rev, 37000, pool)->data);
      else if (rev == 5 || rev == 8)
        SVN_TEST_STRING_ASSERT(prop_value->data,
                               large_log(rev, 25000, pool)->data);
      else
        SVN_TEST_STRING_ASSERT(prop_value->data,
                               large_log(rev, 15000, pool)->data);
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
              svn_dirent_join_many(pool, REPO_NAME, PATH_REVS_DIR,
                                   apr_psprintf(pool, "%ld/p%ld",
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
   from the transaction that is commiting rev = SHARD_SIZE). */
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
#define REPO_NAME "test-repo-fsx-info"
#define SHARD_SIZE 3
#define MAX_REV 5
static svn_error_t *
test_info(const svn_test_opts_t *opts,
          apr_pool_t *pool)
{
  svn_fs_t *fs;
  const svn_fs_fsx_info_t *fsx_info;
  const svn_fs_info_placeholder_t *info;

  SVN_ERR(create_packed_filesystem(REPO_NAME, opts, MAX_REV, SHARD_SIZE,
                                   pool));

  SVN_ERR(svn_fs_open2(&fs, REPO_NAME, NULL, pool, pool));
  SVN_ERR(svn_fs_info(&info, fs, pool, pool));
  info = svn_fs_info_dup(info, pool, pool);

  SVN_TEST_STRING_ASSERT(opts->fs_type, info->fs_type);

  /* Bail (with success) on known-untestable scenarios */
  if (strcmp(opts->fs_type, "fsx") != 0)
    return SVN_NO_ERROR;

  fsx_info = (const void *)info;
  SVN_TEST_ASSERT(fsx_info->shard_size == SHARD_SIZE);
  SVN_TEST_ASSERT(fsx_info->min_unpacked_rev
                  == (MAX_REV + 1) / SHARD_SIZE * SHARD_SIZE);

  return SVN_NO_ERROR;
}
#undef REPO_NAME
#undef SHARD_SIZE
#undef MAX_REV

/* ------------------------------------------------------------------------ */
#define REPO_NAME "test-repo-fsx-rev-container"
#define SHARD_SIZE 3
#define MAX_REV 5
static svn_error_t *
test_reps(const svn_test_opts_t *opts,
          apr_pool_t *pool)
{
  svn_fs_t *fs = NULL;
  svn_fs_x__reps_builder_t *builder;
  svn_fs_x__reps_t *container;
  svn_stringbuf_t *serialized;
  svn_stream_t *stream;
  svn_stringbuf_t *contents = svn_stringbuf_create_ensure(10000, pool);
  int i;

  for (i = 0; i < 10000; ++i)
    {
      int v, s = 0;
      for (v = i; v > 0; v /= 10)
        s += v % 10;

      svn_stringbuf_appendbyte(contents, (char)(s + ' '));
    }

  SVN_ERR(create_packed_filesystem(REPO_NAME, opts, MAX_REV, SHARD_SIZE,
                                   pool));

  SVN_ERR(svn_fs_open2(&fs, REPO_NAME, NULL, pool, pool));

  builder = svn_fs_x__reps_builder_create(fs, pool);
  for (i = 10000; i > 10; --i)
    {
      apr_size_t idx;
      svn_string_t string;
      string.data = contents->data;
      string.len = i;

      SVN_ERR(svn_fs_x__reps_add(&idx, builder, &string));
    }

  serialized = svn_stringbuf_create_empty(pool);
  stream = svn_stream_from_stringbuf(serialized, pool);
  SVN_ERR(svn_fs_x__write_reps_container(stream, builder, pool));

  SVN_ERR(svn_stream_reset(stream));
  SVN_ERR(svn_fs_x__read_reps_container(&container, stream, pool, pool));
  SVN_ERR(svn_stream_close(stream));

  return SVN_NO_ERROR;
}

#undef REPO_NAME
#undef SHARD_SIZE
#undef MAX_REV

/* ------------------------------------------------------------------------ */
#define REPO_NAME "test-repo-fsx-pack-shard-size-one"
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
#define REPO_NAME "test-repo-fsx-batch-fsync"
static svn_error_t *
test_batch_fsync(const svn_test_opts_t *opts,
                 apr_pool_t *pool)
{
  const char *abspath;
  svn_fs_x__batch_fsync_t *batch;
  int i;

  /* Disable this test for non FSX backends because it has no relevance to
   * them. */
  if (strcmp(opts->fs_type, "fsx") != 0)
      return svn_error_create(SVN_ERR_TEST_SKIPPED, NULL,
      "this will test FSX repositories only");

  /* Create an empty working directory and let it be cleaned up by the test
   * harness. */
  SVN_ERR(svn_dirent_get_absolute(&abspath, REPO_NAME, pool));

  SVN_ERR(svn_io_remove_dir2(abspath, TRUE, NULL, NULL, pool));
  SVN_ERR(svn_io_make_dir_recursively(abspath, pool));
  svn_test_add_dir_cleanup(abspath);

  /* Initialize infrastructure with a pool that lives as long as this
   * application. */
  SVN_ERR(svn_fs_x__batch_fsync_init(pool));

  /* We use and re-use the same batch object throughout this test. */
  SVN_ERR(svn_fs_x__batch_fsync_create(&batch, TRUE, pool));

  /* The working directory is new. */
  SVN_ERR(svn_fs_x__batch_fsync_new_path(batch, abspath, pool));

  /* 1st run: Has to fire up worker threads etc. */
  for (i = 0; i < 10; ++i)
    {
      apr_file_t *file;
      const char *path = svn_dirent_join(abspath,
                                         apr_psprintf(pool, "file%i", i),
                                         pool);
      apr_size_t len = strlen(path);

      SVN_ERR(svn_fs_x__batch_fsync_open_file(&file, batch, path, pool));

      SVN_ERR(svn_io_file_write(file, path, &len, pool));
    }

  SVN_ERR(svn_fs_x__batch_fsync_run(batch, pool));

  /* 2nd run: Running a batch must leave the container in an empty,
   * re-usable state. Hence, try to re-use it. */
  for (i = 0; i < 10; ++i)
    {
      apr_file_t *file;
      const char *path = svn_dirent_join(abspath,
                                         apr_psprintf(pool, "new%i", i),
                                         pool);
      apr_size_t len = strlen(path);

      SVN_ERR(svn_fs_x__batch_fsync_open_file(&file, batch, path, pool));

      SVN_ERR(svn_io_file_write(file, path, &len, pool));
    }

  SVN_ERR(svn_fs_x__batch_fsync_run(batch, pool));

  /* 3rd run: Schedule but don't execute. POOL cleanup shall not fail. */
  for (i = 0; i < 10; ++i)
    {
      apr_file_t *file;
      const char *path = svn_dirent_join(abspath,
                                         apr_psprintf(pool, "another%i", i),
                                         pool);
      apr_size_t len = strlen(path);

      SVN_ERR(svn_fs_x__batch_fsync_open_file(&file, batch, path, pool));

      SVN_ERR(svn_io_file_write(file, path, &len, pool));
    }

  return SVN_NO_ERROR;
}
#undef REPO_NAME
/* ------------------------------------------------------------------------ */

/* The test table.  */

static int max_threads = 4;

static struct svn_test_descriptor_t test_funcs[] =
  {
    SVN_TEST_NULL,
    SVN_TEST_OPTS_PASS(pack_filesystem,
                       "pack a FSX filesystem"),
    SVN_TEST_OPTS_PASS(pack_even_filesystem,
                       "pack FSX where revs % shard = 0"),
    SVN_TEST_OPTS_PASS(read_packed_fs,
                       "read from a packed FSX filesystem"),
    SVN_TEST_OPTS_PASS(commit_packed_fs,
                       "commit to a packed FSX filesystem"),
    SVN_TEST_OPTS_PASS(get_set_revprop_packed_fs,
                       "get/set revprop while packing FSX filesystem"),
    SVN_TEST_OPTS_PASS(get_set_large_revprop_packed_fs,
                       "get/set large packed revprops in FSX"),
    SVN_TEST_OPTS_PASS(get_set_huge_revprop_packed_fs,
                       "get/set huge packed revprops in FSX"),
    SVN_TEST_OPTS_PASS(recover_fully_packed,
                       "recover a fully packed filesystem"),
    SVN_TEST_OPTS_PASS(file_hint_at_shard_boundary,
                       "test file hint at shard boundary"),
    SVN_TEST_OPTS_PASS(test_info,
                       "test svn_fs_info"),
    SVN_TEST_OPTS_PASS(test_reps,
                       "test representations container"),
    SVN_TEST_OPTS_PASS(pack_shard_size_one,
                       "test packing with shard size = 1"),
    SVN_TEST_OPTS_PASS(test_batch_fsync,
                       "test batch fsync"),
    SVN_TEST_NULL
  };

SVN_TEST_MAIN
