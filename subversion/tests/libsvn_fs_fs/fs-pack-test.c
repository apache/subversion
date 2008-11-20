/* fs-pack-test.c --- tests for the filesystem
 *
 * ====================================================================
 * Copyright (c) 2008 CollabNet.  All rights reserved.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution.  The terms
 * are also available at http://subversion.tigris.org/license-1.html.
 * If newer versions of this license are posted there, you may use a
 * newer version instead, at your option.
 *
 * This software consists of voluntary contributions made by many
 * individuals.  For exact contribution history, see the revision
 * history and logs, available at http://subversion.tigris.org/.
 * ====================================================================
 */

#include <stdlib.h>
#include <string.h>
#include <apr_pools.h>

#include "../svn_test.h"
#include "../../libsvn_fs_fs/fs.h"

#include "svn_pools.h"
#include "svn_fs.h"

#include "../svn_test_fs.h"


/*-----------------------------------------------------------------*/

/** The actual fs-tests called by `make check` **/

/* Write the format number and maximum number of files per directory
   to a new format file in PATH, overwriting a previously existing file.

   Use POOL for temporary allocation.

   This implementation is largely stolen from libsvn_fs_fs/fs_fs.c. */
static svn_error_t *
write_format(const char *path,
             int format,
             int max_files_per_dir,
             apr_pool_t *pool)
{
  const char *contents;

  path = svn_path_join(path, "format", pool);

  if (format >= SVN_FS_FS__MIN_LAYOUT_FORMAT_OPTION_FORMAT)
    {
      if (max_files_per_dir)
        contents = apr_psprintf(pool,
                                "%d\n"
                                "layout sharded %d\n",
                                format, max_files_per_dir);
      else
        contents = apr_psprintf(pool,
                                "%d\n"
                                "layout linear",
                                format);
    }
  else
    {
      contents = apr_psprintf(pool, "%d\n", format);
    }

    {
      const char *path_tmp;

      SVN_ERR(svn_io_write_unique(&path_tmp,
                                  svn_path_dirname(path, pool),
                                  contents, strlen(contents),
                                  svn_io_file_del_none, pool));

#ifdef WIN32
      /* make the destination writable, but only on Windows, because
         Windows does not let us replace read-only files. */
      SVN_ERR(svn_io_set_file_read_write(path, TRUE, pool));
#endif /* WIN32 */

      /* rename the temp file as the real destination */
      SVN_ERR(svn_io_file_rename(path_tmp, path, pool));
    }

  /* And set the perms to make it read only */
  return svn_io_set_file_read_only(path, FALSE, pool);
}

/* Pack a filesystem.  */
#define REPO_NAME "test-repo-fsfs-pack"
#define SHARD_SIZE 7
#define MAX_REV 53
static svn_error_t *
pack_filesystem(const char **msg,
                svn_boolean_t msg_only,
                svn_test_opts_t *opts,
                apr_pool_t *pool)
{
  svn_fs_t *fs;
  svn_fs_txn_t *txn;
  svn_fs_root_t *txn_root;
  const char *conflict;
  svn_revnum_t after_rev;
  int i;
  svn_node_kind_t kind;
  const char *pack_path;
  apr_pool_t *subpool = svn_pool_create(pool);

  *msg = "pack a FSFS filesystem";

  if (msg_only)
    return SVN_NO_ERROR;

  /* Create a filesystem, then close it */
  SVN_ERR(svn_test__create_fs(&fs, REPO_NAME, opts, subpool));
  svn_pool_destroy(subpool);

  subpool = svn_pool_create(pool);

  /* Rewrite the format file */
  SVN_ERR(write_format(REPO_NAME, SVN_FS_FS__MIN_PACKED_FORMAT,
                       SHARD_SIZE, subpool));

  /* Reopen the filesystem */
  SVN_ERR(svn_fs_open(&fs, REPO_NAME, NULL, subpool));

  /* Revision 1: the Greek tree */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, 0, subpool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, subpool));
  SVN_ERR(svn_test__create_greek_tree(txn_root, subpool));
  SVN_ERR(svn_fs_commit_txn(&conflict, &after_rev, txn, subpool));

  /* Revisions 2-11: A bunch of random changes. */
  while (after_rev < MAX_REV + 1)
    {
      /* Toss in a bunch of magic numbers for spice. */
      apr_int64_t num = ((after_rev * 1234353 + 4358) 
                                * 4583 + ((after_rev % 4) << 1)) / 42;
      const char *str = apr_psprintf(pool, "%" APR_INT64_T_FMT "\n", num);

      SVN_ERR(svn_fs_begin_txn(&txn, fs, after_rev, subpool));
      SVN_ERR(svn_fs_txn_root(&txn_root, txn, subpool));
      SVN_ERR(svn_test__set_file_contents(txn_root, "iota", str, subpool));
      SVN_ERR(svn_fs_commit_txn(&conflict, &after_rev, txn, subpool));
    }
  svn_pool_destroy(subpool);

  /* Now pack the FS */
  SVN_ERR(svn_fs_pack(REPO_NAME, NULL, NULL, pool));

  /* Check to see that the pack files exist, and that the rev directories
     don't. */
  for (i = 0; i < (MAX_REV + 1) / SHARD_SIZE; i++)
    {
      pack_path = svn_path_join_many(
            pool, REPO_NAME, "revs",
            apr_psprintf(pool, "%d.pack", i / SHARD_SIZE), NULL);

      /* This file should exist. */
      SVN_ERR(svn_io_check_path(pack_path, &kind, pool));
      if (kind != svn_node_file)
        return svn_error_createf(SVN_ERR_FS_GENERAL, NULL,
                                 "Expected pack file '%s' not found",
                                 pack_path);

      /* This directory should not exist. */
      pack_path = svn_path_join_many(
            pool, REPO_NAME, "revs",
            apr_psprintf(pool, "%d", i / SHARD_SIZE), NULL);
      SVN_ERR(svn_io_check_path(pack_path, &kind, pool));
      if (kind != svn_node_none)
        return svn_error_createf(SVN_ERR_FS_GENERAL, NULL,
                                 "Unexpected directory '%s' found",
                                 pack_path);
    }

  /* Finally, make sure the final revision directory does exist. */
  pack_path = svn_path_join_many(
        pool, REPO_NAME, "revs",
        apr_psprintf(pool, "%d", (i / SHARD_SIZE) + 1), NULL);
  SVN_ERR(svn_io_check_path(pack_path, &kind, pool));
  if (kind != svn_node_none)
    return svn_error_createf(SVN_ERR_FS_GENERAL, NULL,
                             "Expected directory '%s' not found",
                             pack_path);


  return SVN_NO_ERROR;
}
#undef REPO_NAME


/* ------------------------------------------------------------------------ */

/* The test table.  */

struct svn_test_descriptor_t test_funcs[] =
  {
    SVN_TEST_NULL,
    SVN_TEST_PASS(pack_filesystem),
    SVN_TEST_NULL
  };
