/* svn_test_fs.c --- test helpers for the filesystem
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

#include "svn_test.h"

#include "svn_string.h"
#include "svn_utf.h"
#include "svn_pools.h"
#include "svn_error.h"
#include "svn_fs.h"
#include "svn_path.h"
#include "svn_delta.h"
#include "svn_hash.h"

#include "svn_test_fs.h"


/*-------------------------------------------------------------------*/

/** Helper routines. **/


static void
fs_warning_handler(void *baton, svn_error_t *err)
{
  svn_handle_warning(stderr, err);
}

/* This is used only by bdb fs tests. */
svn_error_t *
svn_test__fs_new(svn_fs_t **fs_p, apr_pool_t *pool)
{
  apr_hash_t *fs_config = apr_hash_make(pool);
  apr_hash_set(fs_config, SVN_FS_CONFIG_BDB_TXN_NOSYNC,
               APR_HASH_KEY_STRING, "1");

  *fs_p = svn_fs_new(fs_config, pool);
  if (! *fs_p)
    return svn_error_create(SVN_ERR_FS_GENERAL, NULL,
                            "Couldn't alloc a new fs object.");

  /* Provide a warning function that just dumps the message to stderr.  */
  svn_fs_set_warning_func(*fs_p, fs_warning_handler, NULL);

  return SVN_NO_ERROR;
}


static apr_hash_t *
make_fs_config(const char *fs_type,
               int server_minor_version,
               apr_pool_t *pool)
{
  apr_hash_t *fs_config = apr_hash_make(pool);

  svn_hash_sets(fs_config, SVN_FS_CONFIG_BDB_TXN_NOSYNC, "1");
  svn_hash_sets(fs_config, SVN_FS_CONFIG_BDB_LOG_AUTOREMOVE, "1");
  svn_hash_sets(fs_config, SVN_FS_CONFIG_FS_TYPE, fs_type);
  if (server_minor_version)
    {
      svn_hash_sets(fs_config, SVN_FS_CONFIG_COMPATIBLE_VERSION,
                    apr_psprintf(pool, "1.%d.0", server_minor_version));
      if (server_minor_version == 6 || server_minor_version == 7)
        svn_hash_sets(fs_config, SVN_FS_CONFIG_PRE_1_8_COMPATIBLE, "1");
      else if (server_minor_version == 5)
        apr_hash_set(fs_config, SVN_FS_CONFIG_PRE_1_6_COMPATIBLE,
                     APR_HASH_KEY_STRING, "1");
      else if (server_minor_version == 4)
        apr_hash_set(fs_config, SVN_FS_CONFIG_PRE_1_5_COMPATIBLE,
                     APR_HASH_KEY_STRING, "1");
      else if (server_minor_version == 3)
        apr_hash_set(fs_config, SVN_FS_CONFIG_PRE_1_4_COMPATIBLE,
                     APR_HASH_KEY_STRING, "1");
    }
  return fs_config;
}


static svn_error_t *
create_fs(svn_fs_t **fs_p,
          const char *name,
          const char *fs_type,
          int server_minor_version,
          apr_hash_t *overlay_fs_config,
          apr_pool_t *pool)
{
  apr_hash_t *fs_config = make_fs_config(fs_type, server_minor_version, pool);

  if (overlay_fs_config)
    fs_config = apr_hash_overlay(pool, overlay_fs_config, fs_config);

  /* If there's already a repository named NAME, delete it.  Doing
     things this way means that repositories stick around after a
     failure for postmortem analysis, but also that tests can be
     re-run without cleaning out the repositories created by prior
     runs.  */
  SVN_ERR(svn_io_remove_dir2(name, TRUE, NULL, NULL, pool));

  SVN_ERR(svn_fs_create2(fs_p, name, fs_config, pool, pool));
  if (! *fs_p)
    return svn_error_create(SVN_ERR_FS_GENERAL, NULL,
                            "Couldn't alloc a new fs object.");

  /* Provide a warning function that just dumps the message to stderr.  */
  svn_fs_set_warning_func(*fs_p, fs_warning_handler, NULL);

  /* Register this fs for cleanup. */
  svn_test_add_dir_cleanup(name);

  return SVN_NO_ERROR;
}

/* If OPTS specifies a filesystem type of 'fsfs' and provides a config file,
 * copy that file into the filesystem FS and set *MUST_REOPEN to TRUE, else
 * set *MUST_REOPEN to FALSE. */
static svn_error_t *
maybe_install_fs_conf(svn_fs_t *fs,
                      const svn_test_opts_t *opts,
                      svn_boolean_t *must_reopen,
                      apr_pool_t *pool)
{
  *must_reopen = FALSE;
  if (! opts->config_file)
    return SVN_NO_ERROR;

  if (strcmp(opts->fs_type, "fsfs") == 0)
    {
      *must_reopen = TRUE;
      return svn_io_copy_file(opts->config_file,
                              svn_path_join(svn_fs_path(fs, pool),
                                            "fsfs.conf", pool),
                              FALSE /* copy_perms */,
                              pool);
    }

  if (strcmp(opts->fs_type, "fsx") == 0)
    {
      *must_reopen = TRUE;
      return svn_io_copy_file(opts->config_file,
                              svn_path_join(svn_fs_path(fs, pool),
                                            "fsx.conf", pool),
                              FALSE /* copy_perms */,
                              pool);
    }

  return SVN_NO_ERROR;
}


svn_error_t *
svn_test__create_bdb_fs(svn_fs_t **fs_p,
                        const char *name,
                        const svn_test_opts_t *opts,
                        apr_pool_t *pool)
{
  return create_fs(fs_p, name, "bdb", opts->server_minor_version, NULL, pool);
}


svn_error_t *
svn_test__create_fs2(svn_fs_t **fs_p,
                     const char *name,
                     const svn_test_opts_t *opts,
                     apr_hash_t *fs_config,
                     apr_pool_t *pool)
{
  svn_boolean_t must_reopen;

  SVN_ERR(create_fs(fs_p, name, opts->fs_type, opts->server_minor_version,
                    fs_config, pool));

  SVN_ERR(maybe_install_fs_conf(*fs_p, opts, &must_reopen, pool));
  if (must_reopen)
    {
      SVN_ERR(svn_fs_open2(fs_p, name, fs_config, pool, pool));
      svn_fs_set_warning_func(*fs_p, fs_warning_handler, NULL);
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_test__create_fs(svn_fs_t **fs_p,
                    const char *name,
                    const svn_test_opts_t *opts,
                    apr_pool_t *pool)
{
  return svn_test__create_fs2(fs_p, name, opts, NULL, pool);
}

svn_error_t *
svn_test__create_repos2(svn_repos_t **repos_p,
                        const char **repos_url,
                        const char **repos_dirent,
                        const char *name,
                        const svn_test_opts_t *opts,
                        apr_pool_t *result_pool,
                        apr_pool_t *scratch_pool)
{
  svn_repos_t *repos;
  svn_boolean_t must_reopen;
  const char *repos_abspath;
  apr_pool_t *repos_pool = repos_p ? result_pool : scratch_pool;
  svn_boolean_t init_svnserve = FALSE;
  apr_hash_t *fs_config = make_fs_config(opts->fs_type,
                                         opts->server_minor_version,
                                         repos_pool);

  if (repos_url && opts->repos_dir && opts->repos_url)
    {
      name = apr_psprintf(scratch_pool, "%s-%s", opts->prog_name,
                          svn_dirent_basename(name, NULL));

      repos_abspath = svn_dirent_join(opts->repos_dir, name, scratch_pool);

      SVN_ERR(svn_dirent_get_absolute(&repos_abspath, repos_abspath,
                                      scratch_pool));

      SVN_ERR(svn_io_make_dir_recursively(repos_abspath, scratch_pool));

      *repos_url = svn_path_url_add_component2(opts->repos_url, name,
                                               result_pool);

      if (strstr(opts->repos_url, "svn://"))
        init_svnserve = TRUE;
    }
  else
    {
      SVN_ERR(svn_dirent_get_absolute(&repos_abspath, name, scratch_pool));

      if (repos_url)
        SVN_ERR(svn_uri_get_file_url_from_dirent(repos_url, repos_abspath,
                                                 result_pool));
    }

  /* If there's already a repository named NAME, delete it.  Doing
     things this way means that repositories stick around after a
     failure for postmortem analysis, but also that tests can be
     re-run without cleaning out the repositories created by prior
     runs.  */
  SVN_ERR(svn_io_remove_dir2(repos_abspath, TRUE, NULL, NULL, scratch_pool));

  SVN_ERR(svn_repos_create(&repos, repos_abspath, NULL, NULL, NULL,
                           fs_config, repos_pool));

  /* Register this repo for cleanup. */
  svn_test_add_dir_cleanup(repos_abspath);

  SVN_ERR(maybe_install_fs_conf(svn_repos_fs(repos), opts, &must_reopen,
                                scratch_pool));
  if (must_reopen)
    {
      SVN_ERR(svn_repos_open3(&repos, repos_abspath, NULL, repos_pool,
                              scratch_pool));
    }

  svn_fs_set_warning_func(svn_repos_fs(repos), fs_warning_handler, NULL);

  if (init_svnserve)
    {
      const char *cfg;
      const char *pwd;

      cfg = svn_dirent_join(repos_abspath, "conf/svnserve.conf", scratch_pool);
      SVN_ERR(svn_io_remove_file2(cfg, FALSE, scratch_pool));
      SVN_ERR(svn_io_file_create(cfg,
                                 "[general]\n"
                                 "auth-access = write\n"
                                 "password-db = passwd\n",
                                 scratch_pool));

      pwd = svn_dirent_join(repos_abspath, "conf/passwd", scratch_pool);
      SVN_ERR(svn_io_remove_file2(pwd, FALSE, scratch_pool));
      SVN_ERR(svn_io_file_create(pwd,
                                 "[users]\n"
                                 "jrandom = rayjandom\n"
                                 "jconstant = rayjandom\n",
                                 scratch_pool));
    }

  if (repos_p)
    *repos_p = repos;
  if (repos_dirent)
    *repos_dirent = apr_pstrdup(result_pool, repos_abspath);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_test__create_repos(svn_repos_t **repos_p,
                       const char *name,
                       const svn_test_opts_t *opts,
                       apr_pool_t *pool)
{
  return svn_error_trace(
            svn_test__create_repos2(repos_p, NULL, NULL, name,
                                    opts, pool, pool));
}

svn_error_t *
svn_test__stream_to_string(svn_stringbuf_t **string,
                           svn_stream_t *stream,
                           apr_pool_t *pool)
{
  char buf[10]; /* Making this really small because a) hey, they're
                   just tests, not the prime place to beg for
                   optimization, and b) we've had repository
                   problems in the past that only showed up when
                   reading a file into a buffer that couldn't hold the
                   file's whole contents -- the kind of thing you'd
                   like to catch while testing.

                   ### cmpilato todo: Perhaps some day this size can
                   be passed in as a parameter.  Not high on my list
                   of priorities today, though. */

  apr_size_t len;
  svn_stringbuf_t *str = svn_stringbuf_create_empty(pool);

  do
    {
      len = sizeof(buf);
      SVN_ERR(svn_stream_read_full(stream, buf, &len));

      /* Now copy however many bytes were *actually* read into str. */
      svn_stringbuf_appendbytes(str, buf, len);

    } while (len);  /* Continue until we're told that no bytes were
                       read. */

  *string = str;
  return SVN_NO_ERROR;
}

svn_error_t *
svn_test__set_file_contents(svn_fs_root_t *root,
                            const char *path,
                            const char *contents,
                            apr_pool_t *pool)
{
  svn_txdelta_window_handler_t consumer_func;
  void *consumer_baton;
  svn_string_t string;
  apr_pool_t *subpool = svn_pool_create(pool);

  SVN_ERR(svn_fs_apply_textdelta(&consumer_func, &consumer_baton,
                                 root, path, NULL, NULL, subpool));

  string.data = contents;
  string.len = strlen(contents);
  SVN_ERR(svn_txdelta_send_string(&string, consumer_func,
                                  consumer_baton, subpool));

  svn_pool_destroy(subpool);
  return SVN_NO_ERROR;
}


svn_error_t *
svn_test__get_file_contents(svn_fs_root_t *root,
                            const char *path,
                            svn_stringbuf_t **str,
                            apr_pool_t *pool)
{
  svn_stream_t *stream;

  SVN_ERR(svn_fs_file_contents(&stream, root, path, pool));
  SVN_ERR(svn_test__stream_to_string(str, stream, pool));

  return SVN_NO_ERROR;
}


/* Read all the entries in directory PATH under transaction or
   revision root ROOT, copying their full paths into the TREE_ENTRIES
   hash, and recursing when those entries are directories */
static svn_error_t *
get_dir_entries(apr_hash_t *tree_entries,
                svn_fs_root_t *root,
                const char *path,
                apr_pool_t *scratch_pool)
{
  apr_hash_t *entries;
  apr_hash_index_t *hi;
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);
  apr_pool_t *result_pool = apr_hash_pool_get(tree_entries);

  SVN_ERR(svn_fs_dir_entries(&entries, root, path, scratch_pool));

  /* Copy this list to the master list with the path prepended to the
     names */
  for (hi = apr_hash_first(scratch_pool, entries); hi; hi = apr_hash_next(hi))
    {
      void *val;
      svn_fs_dirent_t *dirent;
      const char *full_path;
      svn_pool_clear(iterpool);

      apr_hash_this(hi, NULL, NULL, &val);
      dirent = val;

      /* Calculate the full path of this entry (by appending the name
         to the path thus far) */
      full_path = svn_path_join(path, dirent->name, result_pool);

      /* Now, copy this dirent to the master hash, but this time, use
         the full path for the key */
      apr_hash_set(tree_entries, full_path, APR_HASH_KEY_STRING, dirent);

      /* If this entry is a directory, recurse into the tree. */
      if (dirent->kind == svn_node_dir)
        SVN_ERR(get_dir_entries(tree_entries, root, full_path, iterpool));
    }

  return SVN_NO_ERROR;
}


/* Verify that PATH under ROOT is: a directory if contents is NULL;
   a file with contents CONTENTS otherwise. */
static svn_error_t *
validate_tree_entry(svn_fs_root_t *root,
                    const char *path,
                    const char *contents,
                    apr_pool_t *pool)
{
  svn_stream_t *rstream;
  svn_stringbuf_t *rstring;
  svn_node_kind_t kind;
  svn_boolean_t is_dir, is_file;

  /* Verify that node types are reported consistently. */
  SVN_ERR(svn_fs_check_path(&kind, root, path, pool));
  SVN_ERR(svn_fs_is_dir(&is_dir, root, path, pool));
  SVN_ERR(svn_fs_is_file(&is_file, root, path, pool));

  SVN_TEST_ASSERT(!is_dir || kind == svn_node_dir);
  SVN_TEST_ASSERT(!is_file || kind == svn_node_file);
  SVN_TEST_ASSERT(is_dir || is_file);

  /* Verify that this is the expected type of node */
  if ((!is_dir && !contents) || (is_dir && contents))
    return svn_error_createf
      (SVN_ERR_FS_GENERAL, NULL,
       "node '%s' in tree was of unexpected node type",
       path);

  /* Verify that the contents are as expected (files only) */
  if (! is_dir)
    {
      svn_stringbuf_t *expected = svn_stringbuf_create(contents, pool);

      /* File lengths. */
      svn_filesize_t length;
      SVN_ERR(svn_fs_file_length(&length, root, path, pool));
      SVN_TEST_ASSERT(expected->len == length);

      /* Text contents. */
      SVN_ERR(svn_fs_file_contents(&rstream, root, path, pool));
      SVN_ERR(svn_test__stream_to_string(&rstring, rstream, pool));
      if (! svn_stringbuf_compare(rstring, expected))
        return svn_error_createf
          (SVN_ERR_FS_GENERAL, NULL,
           "node '%s' in tree had unexpected contents",
           path);
    }

  return SVN_NO_ERROR;
}



/* Given a transaction or revision root (ROOT), check to see if the
   tree that grows from that root has all the path entries, and only
   those entries, passed in the array ENTRIES (which is an array of
   NUM_ENTRIES tree_test_entry_t's) */
svn_error_t *
svn_test__validate_tree(svn_fs_root_t *root,
                        svn_test__tree_entry_t *entries,
                        int num_entries,
                        apr_pool_t *pool)
{
  apr_hash_t *tree_entries, *expected_entries;
  apr_pool_t *subpool = svn_pool_create(pool);
  apr_pool_t *iterpool = svn_pool_create(pool);
  svn_stringbuf_t *extra_entries = NULL;
  svn_stringbuf_t *missing_entries = NULL;
  svn_stringbuf_t *corrupt_entries = NULL;
  apr_hash_index_t *hi;
  int i;

  /* There should be no entry with this name. */
  const char *na_name = "es-vee-en";

  /* Create our master hash for storing the entries */
  tree_entries = apr_hash_make(subpool);

  /* Recursively get the whole tree */
  SVN_ERR(get_dir_entries(tree_entries, root, "", iterpool));
  svn_pool_clear(iterpool);

  /* Create a hash for storing our expected entries */
  expected_entries = apr_hash_make(subpool);

  /* Copy our array of expected entries into a hash. */
  for (i = 0; i < num_entries; i++)
    apr_hash_set(expected_entries, entries[i].path,
                 APR_HASH_KEY_STRING, &(entries[i]));

  /* For each entry in our EXPECTED_ENTRIES hash, try to find that
     entry in the TREE_ENTRIES hash given us by the FS.  If we find
     that object, remove it from the TREE_ENTRIES.  If we don't find
     it, there's a problem to report! */
  for (hi = apr_hash_first(subpool, expected_entries);
       hi;
       hi = apr_hash_next(hi))
    {
      const void *key;
      apr_ssize_t keylen;
      void *val;
      svn_test__tree_entry_t *entry;

      svn_pool_clear(iterpool);
      apr_hash_this(hi, &key, &keylen, &val);
      entry = val;

      /* Verify that the entry exists in our full list of entries. */
      val = apr_hash_get(tree_entries, key, keylen);
      if (val)
        {
          svn_error_t *err;

          if ((err = validate_tree_entry(root, entry->path,
                                         entry->contents, iterpool)))
            {
              /* If we don't have a corrupt entries string, make one. */
              if (! corrupt_entries)
                corrupt_entries = svn_stringbuf_create_empty(subpool);

              /* Append this entry name to the list of corrupt entries. */
              svn_stringbuf_appendcstr(corrupt_entries, "   ");
              svn_stringbuf_appendbytes(corrupt_entries, (const char *)key,
                                        keylen);
              svn_stringbuf_appendcstr(corrupt_entries, "\n");
              svn_error_clear(err);
            }

          apr_hash_set(tree_entries, key, keylen, NULL);
        }
      else
        {
          /* If we don't have a missing entries string, make one. */
          if (! missing_entries)
            missing_entries = svn_stringbuf_create_empty(subpool);

          /* Append this entry name to the list of missing entries. */
          svn_stringbuf_appendcstr(missing_entries, "   ");
          svn_stringbuf_appendbytes(missing_entries, (const char *)key,
                                    keylen);
          svn_stringbuf_appendcstr(missing_entries, "\n");
        }
    }

  /* Any entries still left in TREE_ENTRIES are extra ones that are
     not expected to be present.  Assemble a string with their names. */
  for (hi = apr_hash_first(subpool, tree_entries);
       hi;
       hi = apr_hash_next(hi))
    {
      const void *key;
      apr_ssize_t keylen;

      apr_hash_this(hi, &key, &keylen, NULL);

      /* If we don't have an extra entries string, make one. */
      if (! extra_entries)
        extra_entries = svn_stringbuf_create_empty(subpool);

      /* Append this entry name to the list of missing entries. */
      svn_stringbuf_appendcstr(extra_entries, "   ");
      svn_stringbuf_appendbytes(extra_entries, (const char *)key, keylen);
      svn_stringbuf_appendcstr(extra_entries, "\n");
    }

  /* Test that non-existent paths will not be found.
   * Skip this test if somebody sneakily added NA_NAME. */
  if (!svn_hash_gets(expected_entries, na_name))
    {
      svn_node_kind_t kind;
      svn_boolean_t is_dir, is_file;

      /* Verify that the node is reported as "n/a". */
      SVN_ERR(svn_fs_check_path(&kind, root, na_name, subpool));
      SVN_ERR(svn_fs_is_dir(&is_dir, root, na_name, subpool));
      SVN_ERR(svn_fs_is_file(&is_file, root, na_name, subpool));

      SVN_TEST_ASSERT(kind == svn_node_none);
      SVN_TEST_ASSERT(!is_file);
      SVN_TEST_ASSERT(!is_dir);
    }

  if (missing_entries || extra_entries || corrupt_entries)
    {
      return svn_error_createf
        (SVN_ERR_FS_GENERAL, NULL,
         "Repository tree does not look as expected.\n"
         "Corrupt entries:\n%s"
         "Missing entries:\n%s"
         "Extra entries:\n%s",
         corrupt_entries ? corrupt_entries->data : "",
         missing_entries ? missing_entries->data : "",
         extra_entries ? extra_entries->data : "");
    }

  svn_pool_destroy(iterpool);
  svn_pool_destroy(subpool);
  return SVN_NO_ERROR;
}


svn_error_t *
svn_test__validate_changes(svn_fs_root_t *root,
                           apr_hash_t *expected,
                           apr_pool_t *pool)
{
  svn_fs_path_change_iterator_t *iter;
  apr_hash_t *actual;
  apr_hash_index_t *hi;
  svn_fs_path_change3_t *change;

  SVN_ERR(svn_fs_paths_changed3(&iter, root, pool, pool));
  SVN_ERR(svn_fs_path_change_get(&change, iter));

  /* We collect all changes b/c this is the easiest way to check for an
     exact match against EXPECTED. */
  actual = apr_hash_make(pool);
  while (change)
    {
      const char *path = apr_pstrmemdup(pool, change->path.data,
                                        change->path.len);
      /* No duplicates! */
      SVN_TEST_ASSERT(!apr_hash_get(actual, path, change->path.len));
      apr_hash_set(actual, path, change->path.len, path);

      SVN_ERR(svn_fs_path_change_get(&change, iter));
    }

#if 0
  /* Print ACTUAL and EXPECTED. */
  {
    int i;
    for (i=0, hi = apr_hash_first(pool, expected); hi; hi = apr_hash_next(hi))
      SVN_DBG(("expected[%d] = '%s'\n", i++, apr_hash_this_key(hi)));
    for (i=0, hi = apr_hash_first(pool, actual); hi; hi = apr_hash_next(hi))
      SVN_DBG(("actual[%d] = '%s'\n", i++, apr_hash_this_key(hi)));
  }
#endif

  for (hi = apr_hash_first(pool, expected); hi; hi = apr_hash_next(hi))
    if (NULL == svn_hash_gets(actual, apr_hash_this_key(hi)))
      return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                               "Path '%s' missing from actual changed-paths",
                               (const char *)apr_hash_this_key(hi));

  for (hi = apr_hash_first(pool, actual); hi; hi = apr_hash_next(hi))
    if (NULL == svn_hash_gets(expected, apr_hash_this_key(hi)))
      return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                               "Path '%s' missing from expected changed-paths",
                               (const char *)apr_hash_this_key(hi));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_test__txn_script_exec(svn_fs_root_t *txn_root,
                          svn_test__txn_script_command_t *script,
                          int num_edits,
                          apr_pool_t *pool)
{
  int i;
  apr_pool_t *iterpool = svn_pool_create(pool);

  /* Run through the list of edits, making the appropriate edit on
     that entry in the TXN_ROOT. */
  for (i = 0; i < num_edits; i++)
    {
      const char *path = script[i].path;
      const char *param1 = script[i].param1;
      int cmd = script[i].cmd;
      svn_boolean_t is_dir = (param1 == 0);

      svn_pool_clear(iterpool);
      switch (cmd)
        {
        case 'a':
          if (is_dir)
            {
              SVN_ERR(svn_fs_make_dir(txn_root, path, iterpool));
            }
          else
            {
              SVN_ERR(svn_fs_make_file(txn_root, path, iterpool));
              SVN_ERR(svn_test__set_file_contents(txn_root, path,
                                                  param1, iterpool));
            }
          break;

        case 'c':
          {
            svn_revnum_t youngest;
            svn_fs_root_t *rev_root;
            svn_fs_t *fs = svn_fs_root_fs(txn_root);

            SVN_ERR(svn_fs_youngest_rev(&youngest, fs, iterpool));
            SVN_ERR(svn_fs_revision_root(&rev_root, fs, youngest, iterpool));
            SVN_ERR(svn_fs_copy(rev_root, path, txn_root, param1, iterpool));
          }
          break;

        case 'd':
          SVN_ERR(svn_fs_delete(txn_root, path, iterpool));
          break;

        case 'e':
          if (! is_dir)
            {
              SVN_ERR(svn_test__set_file_contents(txn_root, path,
                                                  param1, iterpool));
            }
          break;

        default:
          break;
        }
    }

  svn_pool_destroy(iterpool);
  return SVN_NO_ERROR;
}


const struct svn_test__tree_entry_t svn_test__greek_tree_nodes[21] = {
  { "iota",         "This is the file 'iota'.\n" },
  { "A",            NULL },
  { "A/mu",         "This is the file 'mu'.\n" },
  { "A/B",          NULL },
  { "A/B/lambda",   "This is the file 'lambda'.\n" },
  { "A/B/E",        NULL },
  { "A/B/E/alpha",  "This is the file 'alpha'.\n" },
  { "A/B/E/beta",   "This is the file 'beta'.\n" },
  { "A/B/F",        NULL },
  { "A/C",          NULL },
  { "A/D",          NULL },
  { "A/D/gamma",    "This is the file 'gamma'.\n" },
  { "A/D/G",        NULL },
  { "A/D/G/pi",     "This is the file 'pi'.\n" },
  { "A/D/G/rho",    "This is the file 'rho'.\n" },
  { "A/D/G/tau",    "This is the file 'tau'.\n" },
  { "A/D/H",        NULL },
  { "A/D/H/chi",    "This is the file 'chi'.\n" },
  { "A/D/H/psi",    "This is the file 'psi'.\n" },
  { "A/D/H/omega",  "This is the file 'omega'.\n" },
  { NULL,           NULL },
};

svn_error_t *
svn_test__check_greek_tree(svn_fs_root_t *root,
                           apr_pool_t *pool)
{
  svn_stream_t *rstream;
  svn_stringbuf_t *rstring;
  svn_stringbuf_t *content;
  const struct svn_test__tree_entry_t *node;
  apr_pool_t *iterpool = svn_pool_create(pool);

  /* Loop through the list of files, checking for matching content. */
  for (node = svn_test__greek_tree_nodes; node->path; node++)
    {
      if (node->contents)
        {
          svn_pool_clear(iterpool);

          SVN_ERR(svn_fs_file_contents(&rstream, root, node->path, iterpool));
          SVN_ERR(svn_test__stream_to_string(&rstring, rstream, iterpool));
          content = svn_stringbuf_create(node->contents, iterpool);
          if (! svn_stringbuf_compare(rstring, content))
            return svn_error_createf(SVN_ERR_FS_GENERAL, NULL,
                                     "data read != data written in file '%s'.",
                                     node->path);
        }
    }

  svn_pool_destroy(iterpool);
  return SVN_NO_ERROR;
}

svn_error_t *
svn_test__create_greek_tree_at(svn_fs_root_t *txn_root,
                               const char *root_dir,
                               apr_pool_t *pool)
{
  const struct svn_test__tree_entry_t *node;
  apr_pool_t *iterpool = svn_pool_create(pool);

  for (node = svn_test__greek_tree_nodes; node->path; node++)
    {
      const char *path;
      svn_pool_clear(iterpool);

      path = svn_relpath_join(root_dir, node->path, iterpool);

      if (node->contents)
        {
          SVN_ERR(svn_fs_make_file(txn_root, path, iterpool));
          SVN_ERR(svn_test__set_file_contents(txn_root, path, node->contents,
                                              iterpool));
        }
      else
        {
          SVN_ERR(svn_fs_make_dir(txn_root, path, iterpool));
        }
    }

  svn_pool_destroy(iterpool);
  return SVN_NO_ERROR;
}

svn_error_t *
svn_test__create_greek_tree(svn_fs_root_t *txn_root,
                            apr_pool_t *pool)
{
  return svn_test__create_greek_tree_at(txn_root, "", pool);
}

svn_error_t *
svn_test__create_blame_repository(svn_repos_t **out_repos,
                                  const char *test_name,
                                  const svn_test_opts_t *opts,
                                  apr_pool_t *pool)
{
  svn_repos_t *repos;
  svn_fs_t *fs;
  svn_fs_txn_t *txn;
  svn_fs_root_t *txn_root, *revision_root;
  svn_revnum_t youngest_rev = 0;
  apr_pool_t *subpool = svn_pool_create(pool);

  /* Create a filesystem and repository. */
  SVN_ERR(svn_test__create_repos(&repos, test_name,
                                 opts, pool));
  *out_repos = repos;

  fs = svn_repos_fs(repos);

  /* Revision 1:  Add trunk, tags, branches. */
  SVN_ERR(svn_repos_fs_begin_txn_for_commit(&txn, repos, youngest_rev,
                                            "initial", "log msg", subpool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, subpool));
  SVN_ERR(svn_fs_make_dir(txn_root, "trunk", subpool));
  SVN_ERR(svn_fs_make_dir(txn_root, "tags", subpool));
  SVN_ERR(svn_fs_make_dir(txn_root, "branches", subpool));
  SVN_ERR(svn_repos_fs_commit_txn(NULL, repos, &youngest_rev, txn, subpool));
  SVN_TEST_ASSERT(SVN_IS_VALID_REVNUM(youngest_rev));
  svn_pool_clear(subpool);

  /* Revision 2:  Add the Greek tree on the trunk. */
  SVN_ERR(svn_repos_fs_begin_txn_for_commit(&txn, repos, youngest_rev,
                                            "initial", "log msg", subpool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, subpool));
  SVN_ERR(svn_test__create_greek_tree_at(txn_root, "trunk", subpool));
  SVN_ERR(svn_repos_fs_commit_txn(NULL, repos, &youngest_rev, txn, subpool));
  SVN_TEST_ASSERT(SVN_IS_VALID_REVNUM(youngest_rev));
  svn_pool_clear(subpool);

  /* Revision 3:  Tweak trunk/A/mu. */
  SVN_ERR(svn_repos_fs_begin_txn_for_commit(&txn, repos, youngest_rev,
                                            "user-trunk", "log msg", subpool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, subpool));
  SVN_ERR(svn_test__set_file_contents(txn_root, "trunk/A/mu",
                                      "A\nB\nC\nD\nE\nF\nG\nH\nI", subpool));
  SVN_ERR(svn_repos_fs_commit_txn(NULL, repos, &youngest_rev, txn, subpool));
  SVN_TEST_ASSERT(SVN_IS_VALID_REVNUM(youngest_rev));
  svn_pool_clear(subpool);

  /* Revision 4:  Copy trunk to branches/1.0.x. */
  SVN_ERR(svn_repos_fs_begin_txn_for_commit(&txn, repos, youngest_rev,
                                            "copy", "log msg", subpool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, subpool));
  SVN_ERR(svn_fs_revision_root(&revision_root, fs, youngest_rev, subpool));
  SVN_ERR(svn_fs_copy(revision_root, "trunk",
                      txn_root, "branches/1.0.x",
                      subpool));
  SVN_ERR(svn_repos_fs_commit_txn(NULL, repos, &youngest_rev, txn, subpool));
  SVN_TEST_ASSERT(SVN_IS_VALID_REVNUM(youngest_rev));
  svn_pool_clear(subpool);

  /* Revision 5:  Tweak trunk/A/mu. */
  SVN_ERR(svn_repos_fs_begin_txn_for_commit(&txn, repos, youngest_rev,
                                            "user-trunk", "log msg", subpool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, subpool));
  SVN_ERR(svn_test__set_file_contents(txn_root, "trunk/A/mu",
                                      "A\nB\nC -- trunk edit\nD\nE\nF\nG\nH\nI",
                                      subpool));
  SVN_ERR(svn_repos_fs_commit_txn(NULL, repos, &youngest_rev, txn, subpool));
  SVN_TEST_ASSERT(SVN_IS_VALID_REVNUM(youngest_rev));
  svn_pool_clear(subpool);

  /* Revision 6:  Tweak branches/1.0.x/A/mu. */
  SVN_ERR(svn_repos_fs_begin_txn_for_commit(&txn, repos, youngest_rev,
                                            "user-branch", "log msg", subpool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, subpool));
  SVN_ERR(svn_test__set_file_contents(txn_root, "branches/1.0.x/A/mu",
                                      "A\nB\nC\nD -- branch edit\nE\nF\nG\nH\nI",
                                      subpool));
  SVN_ERR(svn_repos_fs_commit_txn(NULL, repos, &youngest_rev, txn, subpool));
  SVN_TEST_ASSERT(SVN_IS_VALID_REVNUM(youngest_rev));
  svn_pool_clear(subpool);

  /* Revision 7:  Merge trunk to branch. */
  SVN_ERR(svn_repos_fs_begin_txn_for_commit(&txn, repos, youngest_rev,
                                            "user-merge1", "log msg", subpool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, subpool));
  SVN_ERR(svn_test__set_file_contents(txn_root, "branches/1.0.x/A/mu",
                                      "A\nB\nC -- trunk edit\nD -- branch edit"
                                      "\nE\nF\nG\nH\nI", subpool));
  SVN_ERR(svn_fs_change_node_prop(txn_root, "/branches/1.0.x", "svn:mergeinfo",
                                  svn_string_create("/trunk:4-6", subpool),
                                  subpool));
  SVN_ERR(svn_repos_fs_commit_txn(NULL, repos, &youngest_rev, txn, subpool));
  SVN_TEST_ASSERT(SVN_IS_VALID_REVNUM(youngest_rev));
  svn_pool_clear(subpool);

  /* Revision 8:  Merge branch to trunk. */
  SVN_ERR(svn_repos_fs_begin_txn_for_commit(&txn, repos, youngest_rev,
                                            "user-merge2", "log msg", subpool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, subpool));
  SVN_ERR(svn_test__set_file_contents(txn_root, "trunk/A/mu",
                                      "A\nB\nC -- trunk edit\nD -- branch edit\n"
                                      "E\nF\nG\nH\nI", subpool));
  SVN_ERR(svn_fs_change_node_prop(txn_root, "/trunk", "svn:mergeinfo",
                                  svn_string_create("/branches/1.0.x:4-7", subpool),
                                  subpool));
  SVN_ERR(svn_repos_fs_commit_txn(NULL, repos, &youngest_rev, txn, subpool));
  SVN_TEST_ASSERT(SVN_IS_VALID_REVNUM(youngest_rev));

  svn_pool_destroy(subpool);

  return SVN_NO_ERROR;
}
