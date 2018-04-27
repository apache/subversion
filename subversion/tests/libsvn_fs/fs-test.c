/* fs-test.c --- tests for the filesystem
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
#include <apr_thread_proc.h>
#include <apr_poll.h>
#include <assert.h>

#include "../svn_test.h"

#include "svn_hash.h"
#include "svn_pools.h"
#include "svn_time.h"
#include "svn_string.h"
#include "svn_fs.h"
#include "svn_checksum.h"
#include "svn_mergeinfo.h"
#include "svn_props.h"
#include "svn_version.h"

#include "svn_private_config.h"
#include "private/svn_cache.h"
#include "private/svn_fs_util.h"
#include "private/svn_fs_private.h"
#include "private/svn_fspath.h"
#include "private/svn_sqlite.h"

#include "../svn_test_fs.h"

#include "../../libsvn_delta/delta.h"
#include "../../libsvn_fs/fs-loader.h"

#define SET_STR(ps, s) ((ps)->data = (s), (ps)->len = strlen(s))


/*-----------------------------------------------------------------*/

/** The actual fs-tests called by `make check` **/

/* Helper:  commit TXN, expecting either success or failure:
 *
 * If EXPECTED_CONFLICT is null, then the commit is expected to
 * succeed.  If it does succeed, set *NEW_REV to the new revision;
 * else return error.
 *
 * If EXPECTED_CONFLICT is non-null, it is either the empty string or
 * the expected path of the conflict.  If it is the empty string, any
 * conflict is acceptable.  If it is a non-empty string, the commit
 * must fail due to conflict, and the conflict path must match
 * EXPECTED_CONFLICT.  If they don't match, return error.
 *
 * If a conflict is expected but the commit succeeds anyway, return
 * error.  If the commit fails but does not provide an error, return
 * error.
 */
static svn_error_t *
test_commit_txn(svn_revnum_t *new_rev,
                svn_fs_txn_t *txn,
                const char *expected_conflict,
                apr_pool_t *pool)
{
  const char *conflict;
  svn_error_t *err;

  err = svn_fs_commit_txn(&conflict, new_rev, txn, pool);

  if (err && (err->apr_err == SVN_ERR_FS_CONFLICT))
    {
      svn_error_clear(err);
      if (! expected_conflict)
        {
          return svn_error_createf
            (SVN_ERR_FS_CONFLICT, NULL,
             "commit conflicted at '%s', but no conflict expected",
             conflict ? conflict : "(missing conflict info!)");
        }
      else if (conflict == NULL)
        {
          return svn_error_createf
            (SVN_ERR_FS_CONFLICT, NULL,
             "commit conflicted as expected, "
             "but no conflict path was returned ('%s' expected)",
             expected_conflict);
        }
      else if ((strcmp(expected_conflict, "") != 0)
               && (strcmp(conflict, expected_conflict) != 0))
        {
          return svn_error_createf
            (SVN_ERR_FS_CONFLICT, NULL,
             "commit conflicted at '%s', but expected conflict at '%s')",
             conflict, expected_conflict);
        }

      /* The svn_fs_commit_txn() API promises to set *NEW_REV to an
         invalid revision number in the case of a conflict.  */
      if (SVN_IS_VALID_REVNUM(*new_rev))
        {
          return svn_error_createf
            (SVN_ERR_FS_GENERAL, NULL,
             "conflicting commit returned valid new revision");
        }
    }
  else if (err)   /* commit may have succeeded, but always report an error */
    {
      if (SVN_IS_VALID_REVNUM(*new_rev))
        return svn_error_quick_wrap
          (err, "commit succeeded but something else failed");
      else
        return svn_error_quick_wrap
          (err, "commit failed due to something other than a conflict");
    }
  else            /* err == NULL, commit should have succeeded */
    {
      if (! SVN_IS_VALID_REVNUM(*new_rev))
        {
          return svn_error_create
            (SVN_ERR_FS_GENERAL, NULL,
             "commit failed but no error was returned");
        }

      if (expected_conflict)
        {
          return svn_error_createf
            (SVN_ERR_FS_GENERAL, NULL,
             "commit succeeded that was expected to fail at '%s'",
             expected_conflict);
        }
    }

  return SVN_NO_ERROR;
}



/* Begin a txn, check its name, then close it */
static svn_error_t *
trivial_transaction(const svn_test_opts_t *opts,
                    apr_pool_t *pool)
{
  svn_fs_t *fs;
  svn_fs_txn_t *txn;
  const char *txn_name;
  int is_invalid_char[256];
  int i;
  const char *p;

  SVN_ERR(svn_test__create_fs(&fs, "test-repo-trivial-txn",
                              opts, pool));

  /* Begin a new transaction that is based on revision 0.  */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, 0, pool));

  /* Test that the txn name is non-null. */
  SVN_ERR(svn_fs_txn_name(&txn_name, txn, pool));

  if (! txn_name)
    return svn_error_create(SVN_ERR_FS_GENERAL, NULL,
                            "Got a NULL txn name.");

  /* Test that the txn name contains only valid characters.  See
     svn_fs.h for the list of valid characters. */
  for (i = 0; i < sizeof(is_invalid_char)/sizeof(*is_invalid_char); ++i)
    is_invalid_char[i] = 1;
  for (i = '0'; i <= '9'; ++i)
    is_invalid_char[i] = 0;
  for (i = 'a'; i <= 'z'; ++i)
    is_invalid_char[i] = 0;
  for (i = 'A'; i <= 'Z'; ++i)
    is_invalid_char[i] = 0;
  for (p = "-."; *p; ++p)
    is_invalid_char[(unsigned char) *p] = 0;

  for (p = txn_name; *p; ++p)
    {
      if (is_invalid_char[(unsigned char) *p])
        return svn_error_createf(SVN_ERR_FS_GENERAL, NULL,
                                 "The txn name '%s' contains an illegal '%c' "
                                 "character", txn_name, *p);
    }

  return SVN_NO_ERROR;
}



/* Open an existing transaction by name. */
static svn_error_t *
reopen_trivial_transaction(const svn_test_opts_t *opts,
                           apr_pool_t *pool)
{
  svn_fs_t *fs;
  svn_fs_txn_t *txn;
  svn_fs_root_t *root;
  const char *txn_name;
  apr_pool_t *subpool = svn_pool_create(pool);

  SVN_ERR(svn_test__create_fs(&fs, "test-repo-reopen-trivial-txn",
                              opts, pool));

  /* Create a first transaction - we don't want that one to reopen. */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, 0, subpool));

  /* Begin a second transaction that is based on revision 0.  */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, 0, subpool));

  /* Don't use the subpool, txn_name must persist beyond the current txn */
  SVN_ERR(svn_fs_txn_name(&txn_name, txn, pool));

  SVN_TEST_ASSERT(svn_fs_txn_base_revision(txn) == 0);

  /* Create a third transaction - we don't want that one to reopen. */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, 0, subpool));

  /* Close the transaction. */
  svn_pool_clear(subpool);

  /* Reopen the transaction by name */
  SVN_ERR(svn_fs_open_txn(&txn, fs, txn_name, subpool));

  /* Does it have the same name? */
  SVN_ERR(svn_fs_txn_root(&root, txn, subpool));
  SVN_TEST_STRING_ASSERT(svn_fs_txn_root_name(root, subpool), txn_name);

  SVN_TEST_ASSERT(svn_fs_txn_base_revision(txn) == 0);

  {
    const char *conflict;
    svn_revnum_t new_rev;
    SVN_ERR(svn_fs_commit_txn(&conflict, &new_rev, txn, subpool));
    SVN_TEST_STRING_ASSERT(conflict, NULL);
    SVN_TEST_ASSERT(new_rev == 1);
  }

  /* Close the transaction ... again. */
  svn_pool_clear(subpool);

  /* Begin another transaction that is based on revision 1.  */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, 1, subpool));

  /* Don't use the subpool, txn_name must persist beyond the current txn */
  SVN_ERR(svn_fs_txn_name(&txn_name, txn, pool));

  SVN_TEST_ASSERT(svn_fs_txn_base_revision(txn) == 1);

  /* Keep the txn name in pool */
  SVN_ERR(svn_fs_txn_name(&txn_name, txn, pool));

  /* Close the transaction ... again. */
  svn_pool_clear(subpool);

  /* Reopen the transaction by name ... again */
  SVN_ERR(svn_fs_open_txn(&txn, fs, txn_name, subpool));

  /* Does it have the same name? ... */
  SVN_ERR(svn_fs_txn_root(&root, txn, subpool));
  SVN_TEST_STRING_ASSERT(svn_fs_txn_root_name(root, subpool), txn_name);

  /* And the same base revision? */
  SVN_TEST_ASSERT(svn_fs_txn_base_revision(txn) == 1);

  svn_pool_destroy(subpool);

  return SVN_NO_ERROR;
}



/* Create a file! */
static svn_error_t *
create_file_transaction(const svn_test_opts_t *opts,
                        apr_pool_t *pool)
{
  svn_fs_t *fs;
  svn_fs_txn_t *txn;
  svn_fs_root_t *txn_root;

  SVN_ERR(svn_test__create_fs(&fs, "test-repo-create-file-txn",
                              opts, pool));

  /* Begin a new transaction that is based on revision 0.  */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, 0, pool));

  /* Get the txn root */
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, pool));

  /* Create a new file in the root directory. */
  SVN_ERR(svn_fs_make_file(txn_root, "beer.txt", pool));

  return SVN_NO_ERROR;
}


/* Make sure we get txn lists correctly. */
static svn_error_t *
verify_txn_list(const svn_test_opts_t *opts,
                apr_pool_t *pool)
{
  svn_fs_t *fs;
  apr_pool_t *subpool;
  svn_fs_txn_t *txn1, *txn2;
  const char *name1, *name2;
  apr_array_header_t *txn_list;

  SVN_ERR(svn_test__create_fs(&fs, "test-repo-verify-txn-list",
                              opts, pool));

  /* Begin a new transaction, get its name (in the top pool), close it.  */
  subpool = svn_pool_create(pool);
  SVN_ERR(svn_fs_begin_txn(&txn1, fs, 0, subpool));
  SVN_ERR(svn_fs_txn_name(&name1, txn1, pool));
  svn_pool_destroy(subpool);

  /* Begin *another* transaction, get its name (in the top pool), close it.  */
  subpool = svn_pool_create(pool);
  SVN_ERR(svn_fs_begin_txn(&txn2, fs, 0, subpool));
  SVN_ERR(svn_fs_txn_name(&name2, txn2, pool));
  svn_pool_destroy(subpool);

  /* Get the list of active transactions from the fs. */
  SVN_ERR(svn_fs_list_transactions(&txn_list, fs, pool));

  /* Check the list. It should have *exactly* two entries. */
  if (txn_list->nelts != 2)
    goto all_bad;

  /* We should be able to find our 2 txn names in the list, in some
     order. */
  if ((! strcmp(name1, APR_ARRAY_IDX(txn_list, 0, const char *)))
      && (! strcmp(name2, APR_ARRAY_IDX(txn_list, 1, const char *))))
    goto all_good;

  else if ((! strcmp(name2, APR_ARRAY_IDX(txn_list, 0, const char *)))
           && (! strcmp(name1, APR_ARRAY_IDX(txn_list, 1, const char *))))
    goto all_good;

 all_bad:

  return svn_error_create(SVN_ERR_FS_GENERAL, NULL,
                          "Got a bogus txn list.");
 all_good:

  return SVN_NO_ERROR;
}


/* Generate N consecutive transactions, then abort them all.  Return
   the list of transaction names. */
static svn_error_t *
txn_names_are_not_reused_helper1(apr_hash_t **txn_names,
                                 svn_fs_t *fs,
                                 apr_pool_t *pool)
{
  apr_hash_index_t *hi;
  const int N = 10;
  int i;
  apr_pool_t *subpool = svn_pool_create(pool);

  *txn_names = apr_hash_make(pool);

  /* Create the transactions and store in a hash table the transaction
     name as the key and the svn_fs_txn_t * as the value. */
  for (i = 0; i < N; ++i)
    {
      svn_fs_txn_t *txn;
      const char *name;

      SVN_ERR(svn_fs_begin_txn(&txn, fs, 0, subpool));
      SVN_ERR(svn_fs_txn_name(&name, txn, pool));
      if (apr_hash_get(*txn_names, name, APR_HASH_KEY_STRING) != NULL)
        return svn_error_createf(SVN_ERR_FS_GENERAL, NULL,
                                 "beginning a new transaction used an "
                                 "existing transaction name '%s'",
                                 name);
      apr_hash_set(*txn_names, name, APR_HASH_KEY_STRING, txn);
    }

  i = 0;
  for (hi = apr_hash_first(pool, *txn_names); hi; hi = apr_hash_next(hi))
    {
      void *val;
      apr_hash_this(hi, NULL, NULL, &val);
      SVN_ERR(svn_fs_abort_txn((svn_fs_txn_t *)val, pool));
      ++i;
    }

  if (i != N)
    return svn_error_createf(SVN_ERR_FS_GENERAL, NULL,
                             "created %d transactions, but only aborted %d",
                             N, i);

  svn_pool_destroy(subpool);
  return SVN_NO_ERROR;
}

/* Compare two hash tables and ensure that no keys in the first hash
   table appear in the second hash table. */
static svn_error_t *
txn_names_are_not_reused_helper2(apr_hash_t *ht1,
                                 apr_hash_t *ht2,
                                 apr_pool_t *pool)
{
  apr_hash_index_t *hi;

  for (hi = apr_hash_first(pool, ht1); hi; hi = apr_hash_next(hi))
    {
      const void *key;
      const char *key_string;
      apr_hash_this(hi, &key, NULL, NULL);
      key_string = key;
      if (apr_hash_get(ht2, key, APR_HASH_KEY_STRING) != NULL)
        return svn_error_createf(SVN_ERR_FS_GENERAL, NULL,
                                 "the transaction name '%s' was reused",
                                 key_string);
    }

  return SVN_NO_ERROR;
}

/* Make sure that transaction names are not reused. */
static svn_error_t *
txn_names_are_not_reused(const svn_test_opts_t *opts,
                         apr_pool_t *pool)
{
  svn_fs_t *fs;
  apr_pool_t *subpool;
  apr_hash_t *txn_names1, *txn_names2;

  /* Bail (with success) on known-untestable scenarios */
  if ((strcmp(opts->fs_type, "fsfs") == 0)
      && (opts->server_minor_version && (opts->server_minor_version < 5)))
    return SVN_NO_ERROR;

  SVN_ERR(svn_test__create_fs(&fs, "test-repo-txn-names-are-not-reused",
                              opts, pool));

  subpool = svn_pool_create(pool);

  /* Create N transactions, abort them all, and collect the generated
     transaction names.  Do this twice. */
  SVN_ERR(txn_names_are_not_reused_helper1(&txn_names1, fs, subpool));
  SVN_ERR(txn_names_are_not_reused_helper1(&txn_names2, fs, subpool));

  /* Check that no transaction names appear in both hash tables. */
  SVN_ERR(txn_names_are_not_reused_helper2(txn_names1, txn_names2, subpool));
  SVN_ERR(txn_names_are_not_reused_helper2(txn_names2, txn_names1, subpool));

  svn_pool_destroy(subpool);

  return SVN_NO_ERROR;
}



/* Test writing & reading a file's contents. */
static svn_error_t *
write_and_read_file(const svn_test_opts_t *opts,
                    apr_pool_t *pool)
{
  svn_fs_t *fs;
  svn_fs_txn_t *txn;
  svn_fs_root_t *txn_root;
  svn_stream_t *rstream;
  svn_stringbuf_t *rstring;
  svn_stringbuf_t *wstring;

  wstring = svn_stringbuf_create("Wicki wild, wicki wicki wild.", pool);
  SVN_ERR(svn_test__create_fs(&fs, "test-repo-read-and-write-file",
                              opts, pool));
  SVN_ERR(svn_fs_begin_txn(&txn, fs, 0, pool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, pool));

  /* Add an empty file. */
  SVN_ERR(svn_fs_make_file(txn_root, "beer.txt", pool));

  /* And write some data into this file. */
  SVN_ERR(svn_test__set_file_contents(txn_root, "beer.txt",
                                      wstring->data, pool));

  /* Now let's read the data back from the file. */
  SVN_ERR(svn_fs_file_contents(&rstream, txn_root, "beer.txt", pool));
  SVN_ERR(svn_test__stream_to_string(&rstring, rstream, pool));

  /* Compare what was read to what was written. */
  if (! svn_stringbuf_compare(rstring, wstring))
    return svn_error_create(SVN_ERR_FS_GENERAL, NULL,
                            "data read != data written.");

  return SVN_NO_ERROR;
}



/* Create a file, a directory, and a file in that directory! */
static svn_error_t *
create_mini_tree_transaction(const svn_test_opts_t *opts,
                             apr_pool_t *pool)
{
  svn_fs_t *fs;
  svn_fs_txn_t *txn;
  svn_fs_root_t *txn_root;

  SVN_ERR(svn_test__create_fs(&fs, "test-repo-create-mini-tree-txn",
                              opts, pool));

  /* Begin a new transaction that is based on revision 0.  */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, 0, pool));

  /* Get the txn root */
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, pool));

  /* Create a new file in the root directory. */
  SVN_ERR(svn_fs_make_file(txn_root, "wine.txt", pool));

  /* Create a new directory in the root directory. */
  SVN_ERR(svn_fs_make_dir(txn_root, "keg", pool));

  /* Now, create a file in our new directory. */
  SVN_ERR(svn_fs_make_file(txn_root, "keg/beer.txt", pool));

  return SVN_NO_ERROR;
}


/* Create a file, a directory, and a file in that directory! */
static svn_error_t *
create_greek_tree_transaction(const svn_test_opts_t *opts,
                              apr_pool_t *pool)
{
  svn_fs_t *fs;
  svn_fs_txn_t *txn;
  svn_fs_root_t *txn_root;

  /* Prepare a txn to receive the greek tree. */
  SVN_ERR(svn_test__create_fs(&fs, "test-repo-create-greek-tree-txn",
                              opts, pool));
  SVN_ERR(svn_fs_begin_txn(&txn, fs, 0, pool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, pool));

  /* Create and verify the greek tree. */
  SVN_ERR(svn_test__create_greek_tree(txn_root, pool));

  return SVN_NO_ERROR;
}


/* Verify that entry KEY is present in ENTRIES, and that its value is
   an svn_fs_dirent_t whose name and id are not null. */
static svn_error_t *
verify_entry(apr_hash_t *entries, const char *key)
{
  svn_fs_dirent_t *ent = apr_hash_get(entries, key,
                                      APR_HASH_KEY_STRING);

  if (ent == NULL)
    return svn_error_createf
      (SVN_ERR_FS_GENERAL, NULL,
       "didn't find dir entry for \"%s\"", key);

  if ((ent->name == NULL) && (ent->id == NULL))
    return svn_error_createf
      (SVN_ERR_FS_GENERAL, NULL,
       "dir entry for \"%s\" has null name and null id", key);

  if (ent->name == NULL)
    return svn_error_createf
      (SVN_ERR_FS_GENERAL, NULL,
       "dir entry for \"%s\" has null name", key);

  if (ent->id == NULL)
    return svn_error_createf
      (SVN_ERR_FS_GENERAL, NULL,
       "dir entry for \"%s\" has null id", key);

  if (strcmp(ent->name, key) != 0)
     return svn_error_createf
     (SVN_ERR_FS_GENERAL, NULL,
      "dir entry for \"%s\" contains wrong name (\"%s\")", key, ent->name);

  return SVN_NO_ERROR;
}


static svn_error_t *
list_directory(const svn_test_opts_t *opts,
               apr_pool_t *pool)
{
  svn_fs_t *fs;
  svn_fs_txn_t *txn;
  svn_fs_root_t *txn_root;
  apr_hash_t *entries;

  SVN_ERR(svn_test__create_fs(&fs, "test-repo-list-dir",
                              opts, pool));
  SVN_ERR(svn_fs_begin_txn(&txn, fs, 0, pool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, pool));

  /* We create this tree
   *
   *         /q
   *         /A/x
   *         /A/y
   *         /A/z
   *         /B/m
   *         /B/n
   *         /B/o
   *
   * then list dir A.  It should have 3 files: "x", "y", and "z", no
   * more, no less.
   */

  /* Create the tree. */
  SVN_ERR(svn_fs_make_file(txn_root, "q", pool));
  SVN_ERR(svn_fs_make_dir(txn_root, "A", pool));
  SVN_ERR(svn_fs_make_file(txn_root, "A/x", pool));
  SVN_ERR(svn_fs_make_file(txn_root, "A/y", pool));
  SVN_ERR(svn_fs_make_file(txn_root, "A/z", pool));
  SVN_ERR(svn_fs_make_dir(txn_root, "B", pool));
  SVN_ERR(svn_fs_make_file(txn_root, "B/m", pool));
  SVN_ERR(svn_fs_make_file(txn_root, "B/n", pool));
  SVN_ERR(svn_fs_make_file(txn_root, "B/o", pool));

  /* Get A's entries. */
  SVN_ERR(svn_fs_dir_entries(&entries, txn_root, "A", pool));

  /* Make sure exactly the right set of entries is present. */
  if (apr_hash_count(entries) != 3)
    {
      return svn_error_create(SVN_ERR_FS_GENERAL, NULL,
                              "unexpected number of entries in dir");
    }
  else
    {
      SVN_ERR(verify_entry(entries, "x"));
      SVN_ERR(verify_entry(entries, "y"));
      SVN_ERR(verify_entry(entries, "z"));
    }

  return SVN_NO_ERROR;
}


/* If EXPR raises SVN_ERR_FS_PROP_BASEVALUE_MISMATCH, continue; else, fail
 * the test. */
#define FAILS_WITH_BOV(expr) \
  do { \
      svn_error_t *__err = (expr); \
      if (!__err || __err->apr_err != SVN_ERR_FS_PROP_BASEVALUE_MISMATCH) \
        return svn_error_create(SVN_ERR_TEST_FAILED, __err, \
                                "svn_fs_change_rev_prop2() failed to " \
                                "detect unexpected old value"); \
      else \
        svn_error_clear(__err); \
  } while (0)

static svn_error_t *
revision_props(const svn_test_opts_t *opts,
               apr_pool_t *pool)
{
  svn_fs_t *fs;
  apr_hash_t *proplist;
  svn_string_t *value;
  int i;
  svn_string_t s1;

  const char *initial_props[4][2] = {
    { "color", "red" },
    { "size", "XXL" },
    { "favorite saturday morning cartoon", "looney tunes" },
    { "auto", "Green 1997 Saturn SL1" }
    };

  const char *final_props[4][2] = {
    { "color", "violet" },
    { "flower", "violet" },
    { "favorite saturday morning cartoon", "looney tunes" },
    { "auto", "Red 2000 Chevrolet Blazer" }
    };

  /* Open the fs */
  SVN_ERR(svn_test__create_fs(&fs, "test-repo-rev-props",
                              opts, pool));

  /* Set some properties on the revision. */
  for (i = 0; i < 4; i++)
    {
      SET_STR(&s1, initial_props[i][1]);
      SVN_ERR(svn_fs_change_rev_prop(fs, 0, initial_props[i][0], &s1, pool));
    }

  /* Change some of the above properties. */
  SET_STR(&s1, "violet");
  SVN_ERR(svn_fs_change_rev_prop(fs, 0, "color", &s1, pool));

  SET_STR(&s1, "Red 2000 Chevrolet Blazer");
  SVN_ERR(svn_fs_change_rev_prop(fs, 0, "auto", &s1, pool));

  /* Remove a property altogether */
  SVN_ERR(svn_fs_change_rev_prop(fs, 0, "size", NULL, pool));

  /* Copy a property's value into a new property. */
  SVN_ERR(svn_fs_revision_prop(&value, fs, 0, "color", pool));
  SVN_TEST_ASSERT(value);

  s1.data = value->data;
  s1.len = value->len;
  SVN_ERR(svn_fs_change_rev_prop(fs, 0, "flower", &s1, pool));

  /* Test svn_fs_change_rev_prop2().  If the whole block goes through, then
   * it is a no-op (it undoes all changes it makes). */
    {
      const svn_string_t s2 = { "wrong value", 11 };
      const svn_string_t *s2_p = &s2;
      const svn_string_t *s1_p = &s1;
      const svn_string_t *unset = NULL;
      const svn_string_t *s1_dup;

      /* Value of "flower" is 's1'. */

      FAILS_WITH_BOV(svn_fs_change_rev_prop2(fs, 0, "flower", &s2_p, s1_p, pool));
      s1_dup = svn_string_dup(&s1, pool);
      SVN_ERR(svn_fs_change_rev_prop2(fs, 0, "flower", &s1_dup, s2_p, pool));

      /* Value of "flower" is 's2'. */

      FAILS_WITH_BOV(svn_fs_change_rev_prop2(fs, 0, "flower", &s1_p, NULL, pool));
      SVN_ERR(svn_fs_change_rev_prop2(fs, 0, "flower", &s2_p, NULL, pool));

      /* Value of "flower" is <not set>. */

      FAILS_WITH_BOV(svn_fs_change_rev_prop2(fs, 0, "flower", &s2_p, s1_p, pool));
      SVN_ERR(svn_fs_change_rev_prop2(fs, 0, "flower", &unset, s1_p, pool));

      /* Value of "flower" is 's1'. */
    }

  /* Obtain a list of all current properties, and make sure it matches
     the expected values. */
  SVN_ERR(svn_fs_revision_proplist(&proplist, fs, 0, pool));
  SVN_TEST_ASSERT(proplist);
  {
    svn_string_t *prop_value;

    if (apr_hash_count(proplist) < 4 )
      return svn_error_createf
        (SVN_ERR_FS_GENERAL, NULL,
         "too few revision properties found");

    /* Loop through our list of expected revision property name/value
       pairs. */
    for (i = 0; i < 4; i++)
      {
        /* For each expected property: */

        /* Step 1.  Find it by name in the hash of all rev. props
           returned to us by svn_fs_revision_proplist.  If it can't be
           found, return an error. */
        prop_value = apr_hash_get(proplist,
                                  final_props[i][0],
                                  APR_HASH_KEY_STRING);
        if (! prop_value)
          return svn_error_createf
            (SVN_ERR_FS_GENERAL, NULL,
             "unable to find expected revision property");

        /* Step 2.  Make sure the value associated with it is the same
           as what was expected, else return an error. */
        if (strcmp(prop_value->data, final_props[i][1]))
          return svn_error_createf
            (SVN_ERR_FS_GENERAL, NULL,
             "revision property had an unexpected value");
      }
  }

  return SVN_NO_ERROR;
}


static svn_error_t *
transaction_props(const svn_test_opts_t *opts,
                  apr_pool_t *pool)
{
  svn_fs_t *fs;
  svn_fs_txn_t *txn;
  apr_hash_t *proplist;
  svn_string_t *value;
  svn_revnum_t after_rev;
  int i;
  svn_string_t s1;

  const char *initial_props[4][2] = {
    { "color", "red" },
    { "size", "XXL" },
    { "favorite saturday morning cartoon", "looney tunes" },
    { "auto", "Green 1997 Saturn SL1" }
    };

  const char *final_props[5][2] = {
    { "color", "violet" },
    { "flower", "violet" },
    { "favorite saturday morning cartoon", "looney tunes" },
    { "auto", "Red 2000 Chevrolet Blazer" },
    { SVN_PROP_REVISION_DATE, "<some datestamp value>" }
    };

  /* Open the fs */
  SVN_ERR(svn_test__create_fs(&fs, "test-repo-txn-props",
                              opts, pool));
  SVN_ERR(svn_fs_begin_txn(&txn, fs, 0, pool));

  /* Set some properties on the revision. */
  for (i = 0; i < 4; i++)
    {
      SET_STR(&s1, initial_props[i][1]);
      SVN_ERR(svn_fs_change_txn_prop(txn, initial_props[i][0], &s1, pool));
    }

  /* Change some of the above properties. */
  SET_STR(&s1, "violet");
  SVN_ERR(svn_fs_change_txn_prop(txn, "color", &s1, pool));

  SET_STR(&s1, "Red 2000 Chevrolet Blazer");
  SVN_ERR(svn_fs_change_txn_prop(txn, "auto", &s1, pool));

  /* Remove a property altogether */
  SVN_ERR(svn_fs_change_txn_prop(txn, "size", NULL, pool));

  /* Copy a property's value into a new property. */
  SVN_ERR(svn_fs_txn_prop(&value, txn, "color", pool));

  s1.data = value->data;
  s1.len = value->len;
  SVN_ERR(svn_fs_change_txn_prop(txn, "flower", &s1, pool));

  /* Obtain a list of all current properties, and make sure it matches
     the expected values. */
  SVN_ERR(svn_fs_txn_proplist(&proplist, txn, pool));
  {
    svn_string_t *prop_value;

    /* All transactions get a datestamp property at their inception,
       so we expect *5*, not 4 properties. */
    if (apr_hash_count(proplist) != 5 )
      return svn_error_createf
        (SVN_ERR_FS_GENERAL, NULL,
         "unexpected number of transaction properties were found");

    /* Loop through our list of expected revision property name/value
       pairs. */
    for (i = 0; i < 5; i++)
      {
        /* For each expected property: */

        /* Step 1.  Find it by name in the hash of all rev. props
           returned to us by svn_fs_revision_proplist.  If it can't be
           found, return an error. */
        prop_value = apr_hash_get(proplist,
                                  final_props[i][0],
                                  APR_HASH_KEY_STRING);
        if (! prop_value)
          return svn_error_createf
            (SVN_ERR_FS_GENERAL, NULL,
             "unable to find expected transaction property");

        /* Step 2.  Make sure the value associated with it is the same
           as what was expected, else return an error. */
        if (strcmp(final_props[i][0], SVN_PROP_REVISION_DATE))
          if (strcmp(prop_value->data, final_props[i][1]))
            return svn_error_createf
              (SVN_ERR_FS_GENERAL, NULL,
               "transaction property had an unexpected value");
      }
  }

  /* Commit the transaction. */
  SVN_ERR(test_commit_txn(&after_rev, txn, NULL, pool));
  if (after_rev != 1)
    return svn_error_createf
      (SVN_ERR_FS_GENERAL, NULL,
       "committed transaction got wrong revision number");

  /* Obtain a list of all properties on the new revision, and make
     sure it matches the expected values.  If you're wondering, the
     expected values should be the exact same set of properties that
     existed on the transaction just prior to its being committed. */
  SVN_ERR(svn_fs_revision_proplist(&proplist, fs, after_rev, pool));
  {
    svn_string_t *prop_value;

    if (apr_hash_count(proplist) < 5 )
      return svn_error_createf
        (SVN_ERR_FS_GENERAL, NULL,
         "unexpected number of revision properties were found");

    /* Loop through our list of expected revision property name/value
       pairs. */
    for (i = 0; i < 5; i++)
      {
        /* For each expected property: */

        /* Step 1.  Find it by name in the hash of all rev. props
           returned to us by svn_fs_revision_proplist.  If it can't be
           found, return an error. */
        prop_value = apr_hash_get(proplist,
                                  final_props[i][0],
                                  APR_HASH_KEY_STRING);
        if (! prop_value)
          return svn_error_createf
            (SVN_ERR_FS_GENERAL, NULL,
             "unable to find expected revision property");

        /* Step 2.  Make sure the value associated with it is the same
           as what was expected, else return an error. */
        if (strcmp(final_props[i][0], SVN_PROP_REVISION_DATE))
          if (strcmp(prop_value->data, final_props[i][1]))
            return svn_error_createf
              (SVN_ERR_FS_GENERAL, NULL,
               "revision property had an unexpected value");
      }
  }

  return SVN_NO_ERROR;
}


static svn_error_t *
node_props(const svn_test_opts_t *opts,
           apr_pool_t *pool)
{
  svn_fs_t *fs;
  svn_fs_txn_t *txn;
  svn_fs_root_t *txn_root;
  apr_hash_t *proplist;
  svn_string_t *value;
  int i;
  svn_string_t s1;

  const char *initial_props[4][2] = {
    { "Best Rock Artist", "Creed" },
    { "Best Rap Artist", "Eminem" },
    { "Best Country Artist", "(null)" },
    { "Best Sound Designer", "Pluessman" }
    };

  const char *final_props[4][2] = {
    { "Best Rock Artist", "P.O.D." },
    { "Best Rap Artist", "Busta Rhymes" },
    { "Best Sound Designer", "Pluessman" },
    { "Biggest Cakewalk Fanatic", "Pluessman" }
    };

  /* Open the fs and transaction */
  SVN_ERR(svn_test__create_fs(&fs, "test-repo-node-props",
                              opts, pool));
  SVN_ERR(svn_fs_begin_txn(&txn, fs, 0, pool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, pool));

  /* Make a node to put some properties into */
  SVN_ERR(svn_fs_make_file(txn_root, "music.txt", pool));

  /* Set some properties on the nodes. */
  for (i = 0; i < 4; i++)
    {
      SET_STR(&s1, initial_props[i][1]);
      SVN_ERR(svn_fs_change_node_prop
              (txn_root, "music.txt", initial_props[i][0], &s1, pool));
    }

  /* Change some of the above properties. */
  SET_STR(&s1, "P.O.D.");
  SVN_ERR(svn_fs_change_node_prop(txn_root, "music.txt", "Best Rock Artist",
                                  &s1, pool));

  SET_STR(&s1, "Busta Rhymes");
  SVN_ERR(svn_fs_change_node_prop(txn_root, "music.txt", "Best Rap Artist",
                                  &s1, pool));

  /* Remove a property altogether */
  SVN_ERR(svn_fs_change_node_prop(txn_root, "music.txt",
                                  "Best Country Artist", NULL, pool));

  /* Copy a property's value into a new property. */
  SVN_ERR(svn_fs_node_prop(&value, txn_root, "music.txt",
                           "Best Sound Designer", pool));

  s1.data = value->data;
  s1.len = value->len;
  SVN_ERR(svn_fs_change_node_prop(txn_root, "music.txt",
                                  "Biggest Cakewalk Fanatic", &s1, pool));

  /* Obtain a list of all current properties, and make sure it matches
     the expected values. */
  SVN_ERR(svn_fs_node_proplist(&proplist, txn_root, "music.txt", pool));
  {
    svn_string_t *prop_value;

    if (apr_hash_count(proplist) != 4 )
      return svn_error_createf
        (SVN_ERR_FS_GENERAL, NULL,
         "unexpected number of node properties were found");

    /* Loop through our list of expected node property name/value
       pairs. */
    for (i = 0; i < 4; i++)
      {
        /* For each expected property: */

        /* Step 1.  Find it by name in the hash of all node props
           returned to us by svn_fs_node_proplist.  If it can't be
           found, return an error. */
        prop_value = apr_hash_get(proplist,
                                  final_props[i][0],
                                  APR_HASH_KEY_STRING);
        if (! prop_value)
          return svn_error_createf
            (SVN_ERR_FS_GENERAL, NULL,
             "unable to find expected node property");

        /* Step 2.  Make sure the value associated with it is the same
           as what was expected, else return an error. */
        if (strcmp(prop_value->data, final_props[i][1]))
          return svn_error_createf
            (SVN_ERR_FS_GENERAL, NULL,
             "node property had an unexpected value");
      }
  }

  return SVN_NO_ERROR;
}



/* Set *PRESENT to true if entry NAME is present in directory PATH
   under ROOT, else set *PRESENT to false. */
static svn_error_t *
check_entry(svn_fs_root_t *root,
            const char *path,
            const char *name,
            svn_boolean_t *present,
            apr_pool_t *pool)
{
  apr_hash_t *entries;
  svn_fs_dirent_t *ent;
  apr_pool_t *subpool = svn_pool_create(pool);

  SVN_ERR(svn_fs_dir_entries(&entries, root, path, subpool));
  ent = apr_hash_get(entries, name, APR_HASH_KEY_STRING);

  if (ent)
    *present = TRUE;
  else
    *present = FALSE;

  svn_pool_destroy(subpool);
  return SVN_NO_ERROR;
}


/* Return an error if entry NAME is absent in directory PATH under ROOT. */
static svn_error_t *
check_entry_present(svn_fs_root_t *root, const char *path,
                    const char *name, apr_pool_t *pool)
{
  svn_boolean_t present = FALSE;
  SVN_ERR(check_entry(root, path, name, &present, pool));

  if (! present)
    return svn_error_createf
      (SVN_ERR_FS_GENERAL, NULL,
       "entry \"%s\" absent when it should be present", name);

  return SVN_NO_ERROR;
}


/* Return an error if entry NAME is present in directory PATH under ROOT. */
static svn_error_t *
check_entry_absent(svn_fs_root_t *root, const char *path,
                   const char *name, apr_pool_t *pool)
{
  svn_boolean_t present = TRUE;
  SVN_ERR(check_entry(root, path, name, &present, pool));

  if (present)
    return svn_error_createf
      (SVN_ERR_FS_GENERAL, NULL,
       "entry \"%s\" present when it should be absent", name);

  return SVN_NO_ERROR;
}


/* Fetch the youngest revision from a repos. */
static svn_error_t *
fetch_youngest_rev(const svn_test_opts_t *opts,
                   apr_pool_t *pool)
{
  svn_fs_t *fs;
  svn_fs_txn_t *txn;
  svn_fs_root_t *txn_root;
  svn_revnum_t new_rev;
  svn_revnum_t youngest_rev, new_youngest_rev;

  SVN_ERR(svn_test__create_fs(&fs, "test-repo-youngest-rev",
                              opts, pool));

  /* Get youngest revision of brand spankin' new filesystem. */
  SVN_ERR(svn_fs_youngest_rev(&youngest_rev, fs, pool));

  /* Prepare a txn to receive the greek tree. */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, 0, pool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, pool));

  /* Create the greek tree. */
  SVN_ERR(svn_test__create_greek_tree(txn_root, pool));

  /* Commit it. */
  SVN_ERR(test_commit_txn(&new_rev, txn, NULL, pool));

  /* Get the new youngest revision. */
  SVN_ERR(svn_fs_youngest_rev(&new_youngest_rev, fs, pool));

  if (youngest_rev == new_rev)
    return svn_error_create(SVN_ERR_FS_GENERAL, NULL,
                            "commit didn't bump up revision number");

  if (new_youngest_rev != new_rev)
    return svn_error_create(SVN_ERR_FS_GENERAL, NULL,
                            "couldn't fetch youngest revision");

  return SVN_NO_ERROR;
}


/* Test committing against an empty repository.
   todo: also test committing against youngest? */
static svn_error_t *
basic_commit(const svn_test_opts_t *opts,
             apr_pool_t *pool)
{
  svn_fs_t *fs;
  svn_fs_txn_t *txn;
  svn_fs_root_t *txn_root, *revision_root;
  svn_revnum_t before_rev, after_rev;
  const char *conflict;

  /* Prepare a filesystem. */
  SVN_ERR(svn_test__create_fs(&fs, "test-repo-basic-commit",
                              opts, pool));

  /* Save the current youngest revision. */
  SVN_ERR(svn_fs_youngest_rev(&before_rev, fs, pool));

  /* Prepare a txn to receive the greek tree. */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, 0, pool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, pool));

  /* Paranoidly check that the current youngest rev is unchanged. */
  SVN_ERR(svn_fs_youngest_rev(&after_rev, fs, pool));
  if (after_rev != before_rev)
    return svn_error_create
      (SVN_ERR_FS_GENERAL, NULL,
       "youngest revision changed unexpectedly");

  /* Create the greek tree. */
  SVN_ERR(svn_test__create_greek_tree(txn_root, pool));
  SVN_TEST_ASSERT(svn_fs_is_txn_root(txn_root));
  SVN_TEST_ASSERT(!svn_fs_is_revision_root(txn_root));

  /* Commit it. */
  SVN_ERR(svn_fs_commit_txn(&conflict, &after_rev, txn, pool));
  SVN_TEST_ASSERT(SVN_IS_VALID_REVNUM(after_rev));

  /* Make sure it's a different revision than before. */
  if (after_rev == before_rev)
    return svn_error_create
      (SVN_ERR_FS_GENERAL, NULL,
       "youngest revision failed to change");

  /* Get root of the revision */
  SVN_ERR(svn_fs_revision_root(&revision_root, fs, after_rev, pool));
  SVN_TEST_ASSERT(!svn_fs_is_txn_root(revision_root));
  SVN_TEST_ASSERT(svn_fs_is_revision_root(revision_root));

  /* Check the tree. */
  SVN_ERR(svn_test__check_greek_tree(revision_root, pool));

  return SVN_NO_ERROR;
}



static svn_error_t *
test_tree_node_validation(const svn_test_opts_t *opts,
                          apr_pool_t *pool)
{
  svn_fs_t *fs;
  svn_fs_txn_t *txn;
  svn_fs_root_t *txn_root, *revision_root;
  svn_revnum_t after_rev;
  const char *conflict;
  apr_pool_t *subpool;

  /* Prepare a filesystem. */
  SVN_ERR(svn_test__create_fs(&fs, "test-repo-validate-tree-entries",
                              opts, pool));

  /* In a txn, create the greek tree. */
  subpool = svn_pool_create(pool);
  {
    static svn_test__tree_entry_t expected_entries[] = {
      /* path, contents (0 = dir) */
      { "iota",        "This is the file 'iota'.\n" },
      { "A",           0 },
      { "A/mu",        "This is the file 'mu'.\n" },
      { "A/B",         0 },
      { "A/B/lambda",  "This is the file 'lambda'.\n" },
      { "A/B/E",       0 },
      { "A/B/E/alpha", "This is the file 'alpha'.\n" },
      { "A/B/E/beta",  "This is the file 'beta'.\n" },
      { "A/B/F",       0 },
      { "A/C",         0 },
      { "A/D",         0 },
      { "A/D/gamma",   "This is the file 'gamma'.\n" },
      { "A/D/G",       0 },
      { "A/D/G/pi",    "This is the file 'pi'.\n" },
      { "A/D/G/rho",   "This is the file 'rho'.\n" },
      { "A/D/G/tau",   "This is the file 'tau'.\n" },
      { "A/D/H",       0 },
      { "A/D/H/chi",   "This is the file 'chi'.\n" },
      { "A/D/H/psi",   "This is the file 'psi'.\n" },
      { "A/D/H/omega", "This is the file 'omega'.\n" }
    };
    SVN_ERR(svn_fs_begin_txn(&txn, fs, 0, subpool));
    SVN_ERR(svn_fs_txn_root(&txn_root, txn, subpool));
    SVN_ERR(svn_test__create_greek_tree(txn_root, subpool));

    /* Carefully validate that tree in the transaction. */
    SVN_ERR(svn_test__validate_tree(txn_root, expected_entries, 20,
                                    subpool));

    /* Go ahead and commit the tree, and destroy the txn object.  */
    SVN_ERR(svn_fs_commit_txn(&conflict, &after_rev, txn, subpool));
    SVN_TEST_ASSERT(SVN_IS_VALID_REVNUM(after_rev));

    /* Carefully validate that tree in the new revision, now. */
    SVN_ERR(svn_fs_revision_root(&revision_root, fs, after_rev, subpool));
    SVN_ERR(svn_test__validate_tree(revision_root, expected_entries, 20,
                                    subpool));
  }
  svn_pool_destroy(subpool);

  /* In a new txn, modify the greek tree. */
  subpool = svn_pool_create(pool);
  {
    static svn_test__tree_entry_t expected_entries[] = {
      /* path, contents (0 = dir) */
      { "iota",          "This is a new version of 'iota'.\n" },
      { "A",             0 },
      { "A/B",           0 },
      { "A/B/lambda",    "This is the file 'lambda'.\n" },
      { "A/B/E",         0 },
      { "A/B/E/alpha",   "This is the file 'alpha'.\n" },
      { "A/B/E/beta",    "This is the file 'beta'.\n" },
      { "A/B/F",         0 },
      { "A/C",           0 },
      { "A/C/kappa",     "This is the file 'kappa'.\n" },
      { "A/D",           0 },
      { "A/D/gamma",     "This is the file 'gamma'.\n" },
      { "A/D/H",         0 },
      { "A/D/H/chi",     "This is the file 'chi'.\n" },
      { "A/D/H/psi",     "This is the file 'psi'.\n" },
      { "A/D/H/omega",   "This is the file 'omega'.\n" },
      { "A/D/I",         0 },
      { "A/D/I/delta",   "This is the file 'delta'.\n" },
      { "A/D/I/epsilon", "This is the file 'epsilon'.\n" }
    };

    SVN_ERR(svn_fs_begin_txn(&txn, fs, after_rev, subpool));
    SVN_ERR(svn_fs_txn_root(&txn_root, txn, subpool));
    SVN_ERR(svn_test__set_file_contents
            (txn_root, "iota", "This is a new version of 'iota'.\n",
             subpool));
    SVN_ERR(svn_fs_delete(txn_root, "A/mu", subpool));
    SVN_ERR(svn_fs_delete(txn_root, "A/D/G", subpool));
    SVN_ERR(svn_fs_make_dir(txn_root, "A/D/I", subpool));
    SVN_ERR(svn_fs_make_file(txn_root, "A/D/I/delta", subpool));
    SVN_ERR(svn_test__set_file_contents
            (txn_root, "A/D/I/delta", "This is the file 'delta'.\n",
             subpool));
    SVN_ERR(svn_fs_make_file(txn_root, "A/D/I/epsilon", subpool));
    SVN_ERR(svn_test__set_file_contents
            (txn_root, "A/D/I/epsilon", "This is the file 'epsilon'.\n",
             subpool));
    SVN_ERR(svn_fs_make_file(txn_root, "A/C/kappa", subpool));
    SVN_ERR(svn_test__set_file_contents
            (txn_root, "A/C/kappa", "This is the file 'kappa'.\n",
             subpool));

    /* Carefully validate that tree in the transaction. */
    SVN_ERR(svn_test__validate_tree(txn_root, expected_entries, 19,
                                    subpool));

    /* Go ahead and commit the tree, and destroy the txn object.  */
    SVN_ERR(svn_fs_commit_txn(&conflict, &after_rev, txn, subpool));
    SVN_TEST_ASSERT(SVN_IS_VALID_REVNUM(after_rev));

    /* Carefully validate that tree in the new revision, now. */
    SVN_ERR(svn_fs_revision_root(&revision_root, fs, after_rev, subpool));
    SVN_ERR(svn_test__validate_tree(revision_root, expected_entries,
                                    19, subpool));
  }
  svn_pool_destroy(subpool);

  return SVN_NO_ERROR;
}


/* Commit with merging (committing against non-youngest). */
static svn_error_t *
merging_commit(const svn_test_opts_t *opts,
               apr_pool_t *pool)
{
  svn_fs_t *fs;
  svn_fs_txn_t *txn;
  svn_fs_root_t *txn_root, *revision_root;
  svn_revnum_t after_rev;
  svn_revnum_t revisions[24];
  apr_size_t i;
  svn_revnum_t revision_count;

  /* Prepare a filesystem. */
  SVN_ERR(svn_test__create_fs(&fs, "test-repo-merging-commit",
                              opts, pool));

  /* Initialize our revision number stuffs. */
  for (i = 0;
       i < ((sizeof(revisions)) / (sizeof(svn_revnum_t)));
       i++)
    revisions[i] = SVN_INVALID_REVNUM;
  revision_count = 0;
  revisions[revision_count++] = 0; /* the brand spankin' new revision */

  /***********************************************************************/
  /* REVISION 0 */
  /***********************************************************************/

  /* In one txn, create and commit the greek tree. */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, 0, pool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, pool));
  SVN_ERR(svn_test__create_greek_tree(txn_root, pool));
  SVN_ERR(test_commit_txn(&after_rev, txn, NULL, pool));

  /***********************************************************************/
  /* REVISION 1 */
  /***********************************************************************/
  {
    static svn_test__tree_entry_t expected_entries[] = {
      /* path, contents (0 = dir) */
      { "iota",        "This is the file 'iota'.\n" },
      { "A",           0 },
      { "A/mu",        "This is the file 'mu'.\n" },
      { "A/B",         0 },
      { "A/B/lambda",  "This is the file 'lambda'.\n" },
      { "A/B/E",       0 },
      { "A/B/E/alpha", "This is the file 'alpha'.\n" },
      { "A/B/E/beta",  "This is the file 'beta'.\n" },
      { "A/B/F",       0 },
      { "A/C",         0 },
      { "A/D",         0 },
      { "A/D/gamma",   "This is the file 'gamma'.\n" },
      { "A/D/G",       0 },
      { "A/D/G/pi",    "This is the file 'pi'.\n" },
      { "A/D/G/rho",   "This is the file 'rho'.\n" },
      { "A/D/G/tau",   "This is the file 'tau'.\n" },
      { "A/D/H",       0 },
      { "A/D/H/chi",   "This is the file 'chi'.\n" },
      { "A/D/H/psi",   "This is the file 'psi'.\n" },
      { "A/D/H/omega", "This is the file 'omega'.\n" }
    };
    SVN_ERR(svn_fs_revision_root(&revision_root, fs, after_rev, pool));
    SVN_ERR(svn_test__validate_tree(revision_root, expected_entries,
                                    20, pool));
  }
  revisions[revision_count++] = after_rev;

  /* Let's add a directory and some files to the tree, and delete
     'iota' */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, revisions[revision_count-1], pool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, pool));
  SVN_ERR(svn_fs_make_dir(txn_root, "A/D/I", pool));
  SVN_ERR(svn_fs_make_file(txn_root, "A/D/I/delta", pool));
  SVN_ERR(svn_test__set_file_contents
          (txn_root, "A/D/I/delta", "This is the file 'delta'.\n", pool));
  SVN_ERR(svn_fs_make_file(txn_root, "A/D/I/epsilon", pool));
  SVN_ERR(svn_test__set_file_contents
          (txn_root, "A/D/I/epsilon", "This is the file 'epsilon'.\n", pool));
  SVN_ERR(svn_fs_make_file(txn_root, "A/C/kappa", pool));
  SVN_ERR(svn_test__set_file_contents
          (txn_root, "A/C/kappa", "This is the file 'kappa'.\n", pool));
  SVN_ERR(svn_fs_delete(txn_root, "iota", pool));
  SVN_ERR(test_commit_txn(&after_rev, txn, NULL, pool));

  /***********************************************************************/
  /* REVISION 2 */
  /***********************************************************************/
  {
    static svn_test__tree_entry_t expected_entries[] = {
      /* path, contents (0 = dir) */
      { "A",             0 },
      { "A/mu",          "This is the file 'mu'.\n" },
      { "A/B",           0 },
      { "A/B/lambda",    "This is the file 'lambda'.\n" },
      { "A/B/E",         0 },
      { "A/B/E/alpha",   "This is the file 'alpha'.\n" },
      { "A/B/E/beta",    "This is the file 'beta'.\n" },
      { "A/B/F",         0 },
      { "A/C",           0 },
      { "A/C/kappa",     "This is the file 'kappa'.\n" },
      { "A/D",           0 },
      { "A/D/gamma",     "This is the file 'gamma'.\n" },
      { "A/D/G",         0 },
      { "A/D/G/pi",      "This is the file 'pi'.\n" },
      { "A/D/G/rho",     "This is the file 'rho'.\n" },
      { "A/D/G/tau",     "This is the file 'tau'.\n" },
      { "A/D/H",         0 },
      { "A/D/H/chi",     "This is the file 'chi'.\n" },
      { "A/D/H/psi",     "This is the file 'psi'.\n" },
      { "A/D/H/omega",   "This is the file 'omega'.\n" },
      { "A/D/I",         0 },
      { "A/D/I/delta",   "This is the file 'delta'.\n" },
      { "A/D/I/epsilon", "This is the file 'epsilon'.\n" }
    };
    SVN_ERR(svn_fs_revision_root(&revision_root, fs, after_rev, pool));
    SVN_ERR(svn_test__validate_tree(revision_root, expected_entries,
                                    23, pool));
  }
  revisions[revision_count++] = after_rev;

  /* We don't think the A/D/H directory is pulling its weight...let's
     knock it off.  Oh, and let's re-add iota, too. */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, revisions[revision_count-1], pool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, pool));
  SVN_ERR(svn_fs_delete(txn_root, "A/D/H", pool));
  SVN_ERR(svn_fs_make_file(txn_root, "iota", pool));
  SVN_ERR(svn_test__set_file_contents
          (txn_root, "iota", "This is the new file 'iota'.\n", pool));
  SVN_ERR(test_commit_txn(&after_rev, txn, NULL, pool));

  /***********************************************************************/
  /* REVISION 3 */
  /***********************************************************************/
  {
    static svn_test__tree_entry_t expected_entries[] = {
      /* path, contents (0 = dir) */
      { "iota",          "This is the new file 'iota'.\n" },
      { "A",             0 },
      { "A/mu",          "This is the file 'mu'.\n" },
      { "A/B",           0 },
      { "A/B/lambda",    "This is the file 'lambda'.\n" },
      { "A/B/E",         0 },
      { "A/B/E/alpha",   "This is the file 'alpha'.\n" },
      { "A/B/E/beta",    "This is the file 'beta'.\n" },
      { "A/B/F",         0 },
      { "A/C",           0 },
      { "A/C/kappa",     "This is the file 'kappa'.\n" },
      { "A/D",           0 },
      { "A/D/gamma",     "This is the file 'gamma'.\n" },
      { "A/D/G",         0 },
      { "A/D/G/pi",      "This is the file 'pi'.\n" },
      { "A/D/G/rho",     "This is the file 'rho'.\n" },
      { "A/D/G/tau",     "This is the file 'tau'.\n" },
      { "A/D/I",         0 },
      { "A/D/I/delta",   "This is the file 'delta'.\n" },
      { "A/D/I/epsilon", "This is the file 'epsilon'.\n" }
    };
    SVN_ERR(svn_fs_revision_root(&revision_root, fs, after_rev, pool));
    SVN_ERR(svn_test__validate_tree(revision_root, expected_entries,
                                    20, pool));
  }
  revisions[revision_count++] = after_rev;

  /* Delete iota (yet again). */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, revisions[revision_count-1], pool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, pool));
  SVN_ERR(svn_fs_delete(txn_root, "iota", pool));
  SVN_ERR(test_commit_txn(&after_rev, txn, NULL, pool));

  /***********************************************************************/
  /* REVISION 4 */
  /***********************************************************************/
  {
    static svn_test__tree_entry_t expected_entries[] = {
      /* path, contents (0 = dir) */
      { "A",             0 },
      { "A/mu",          "This is the file 'mu'.\n" },
      { "A/B",           0 },
      { "A/B/lambda",    "This is the file 'lambda'.\n" },
      { "A/B/E",         0 },
      { "A/B/E/alpha",   "This is the file 'alpha'.\n" },
      { "A/B/E/beta",    "This is the file 'beta'.\n" },
      { "A/B/F",         0 },
      { "A/C",           0 },
      { "A/C/kappa",     "This is the file 'kappa'.\n" },
      { "A/D",           0 },
      { "A/D/gamma",     "This is the file 'gamma'.\n" },
      { "A/D/G",         0 },
      { "A/D/G/pi",      "This is the file 'pi'.\n" },
      { "A/D/G/rho",     "This is the file 'rho'.\n" },
      { "A/D/G/tau",     "This is the file 'tau'.\n" },
      { "A/D/I",         0 },
      { "A/D/I/delta",   "This is the file 'delta'.\n" },
      { "A/D/I/epsilon", "This is the file 'epsilon'.\n" }
    };
    SVN_ERR(svn_fs_revision_root(&revision_root, fs, after_rev, pool));
    SVN_ERR(svn_test__validate_tree(revision_root, expected_entries,
                                    19, pool));
  }
  revisions[revision_count++] = after_rev;

  /***********************************************************************/
  /* GIVEN:  A and B, with common ancestor ANCESTOR, where A and B
     directories, and E, an entry in either A, B, or ANCESTOR.

     For every E, the following cases exist:
      - E exists in neither ANCESTOR nor A.
      - E doesn't exist in ANCESTOR, and has been added to A.
      - E exists in ANCESTOR, but has been deleted from A.
      - E exists in both ANCESTOR and A ...
        - but refers to different node revisions.
        - and refers to the same node revision.

     The same set of possible relationships with ANCESTOR holds for B,
     so there are thirty-six combinations.  The matrix is symmetrical
     with A and B reversed, so we only have to describe one triangular
     half, including the diagonal --- 21 combinations.

     Our goal here is to test all the possible scenarios that can
     occur given the above boolean logic table, and to make sure that
     the results we get are as expected.

     The test cases below have the following features:

     - They run straight through the scenarios as described in the
       `structure' document at this time.

     - In each case, a txn is begun based on some revision (ANCESTOR),
       is modified into a new tree (B), and then is attempted to be
       committed (which happens against the head of the tree, A).

     - If the commit is successful (and is *expected* to be such),
       that new revision (which exists now as a result of the
       successful commit) is thoroughly tested for accuracy of tree
       entries, and in the case of files, for their contents.  It is
       important to realize that these successful commits are
       advancing the head of the tree, and each one effective becomes
       the new `A' described in further test cases.
  */
  /***********************************************************************/

  /* (6) E exists in neither ANCESTOR nor A. */
  {
    /* (1) E exists in neither ANCESTOR nor B.  Can't occur, by
       assumption that E exists in either A, B, or ancestor. */

    /* (1) E has been added to B.  Add E in the merged result. */
    SVN_ERR(svn_fs_begin_txn(&txn, fs, revisions[0], pool));
    SVN_ERR(svn_fs_txn_root(&txn_root, txn, pool));
    SVN_ERR(svn_fs_make_file(txn_root, "theta", pool));
    SVN_ERR(svn_test__set_file_contents
            (txn_root, "theta", "This is the file 'theta'.\n", pool));
    SVN_ERR(test_commit_txn(&after_rev, txn, NULL, pool));

    /*********************************************************************/
    /* REVISION 5 */
    /*********************************************************************/
    {
      static svn_test__tree_entry_t expected_entries[] = {
        /* path, contents (0 = dir) */
        { "theta",         "This is the file 'theta'.\n" },
        { "A",             0 },
        { "A/mu",          "This is the file 'mu'.\n" },
        { "A/B",           0 },
        { "A/B/lambda",    "This is the file 'lambda'.\n" },
        { "A/B/E",         0 },
        { "A/B/E/alpha",   "This is the file 'alpha'.\n" },
        { "A/B/E/beta",    "This is the file 'beta'.\n" },
        { "A/B/F",         0 },
        { "A/C",           0 },
        { "A/C/kappa",     "This is the file 'kappa'.\n" },
        { "A/D",           0 },
        { "A/D/gamma",     "This is the file 'gamma'.\n" },
        { "A/D/G",         0 },
        { "A/D/G/pi",      "This is the file 'pi'.\n" },
        { "A/D/G/rho",     "This is the file 'rho'.\n" },
        { "A/D/G/tau",     "This is the file 'tau'.\n" },
        { "A/D/I",         0 },
        { "A/D/I/delta",   "This is the file 'delta'.\n" },
        { "A/D/I/epsilon", "This is the file 'epsilon'.\n" }
      };
      SVN_ERR(svn_fs_revision_root(&revision_root, fs, after_rev, pool));
      SVN_ERR(svn_test__validate_tree(revision_root,
                                      expected_entries,
                                      20, pool));
    }
    revisions[revision_count++] = after_rev;

    /* (1) E has been deleted from B.  Can't occur, by assumption that
       E doesn't exist in ANCESTOR. */

    /* (3) E exists in both ANCESTOR and B.  Can't occur, by
       assumption that E doesn't exist in ancestor. */
  }

  /* (5) E doesn't exist in ANCESTOR, and has been added to A. */
  {
    svn_revnum_t failed_rev;
    /* (1) E doesn't exist in ANCESTOR, and has been added to B.
       Conflict. */
    SVN_ERR(svn_fs_begin_txn(&txn, fs, revisions[4], pool));
    SVN_ERR(svn_fs_txn_root(&txn_root, txn, pool));
    SVN_ERR(svn_fs_make_file(txn_root, "theta", pool));
    SVN_ERR(svn_test__set_file_contents
            (txn_root, "theta", "This is another file 'theta'.\n", pool));

    /* TXN must actually be based upon revisions[4] (instead of HEAD). */
    SVN_TEST_ASSERT(svn_fs_txn_base_revision(txn) == revisions[4]);

    SVN_ERR(test_commit_txn(&failed_rev, txn, "/theta", pool));
    SVN_ERR(svn_fs_abort_txn(txn, pool));

    /* (1) E exists in ANCESTOR, but has been deleted from B.  Can't
       occur, by assumption that E doesn't exist in ANCESTOR. */

    /* (3) E exists in both ANCESTOR and B.  Can't occur, by assumption
       that E doesn't exist in ANCESTOR. */

    SVN_TEST_ASSERT(failed_rev == SVN_INVALID_REVNUM);
  }

  /* (4) E exists in ANCESTOR, but has been deleted from A */
  {
    /* (1) E exists in ANCESTOR, but has been deleted from B.  If
       neither delete was a result of a rename, then omit E from the
       merged tree.  Otherwise, conflict. */
    /* ### cmpilato todo: the rename case isn't actually handled by
       merge yet, so we know we won't get a conflict here. */
    SVN_ERR(svn_fs_begin_txn(&txn, fs, revisions[1], pool));
    SVN_ERR(svn_fs_txn_root(&txn_root, txn, pool));
    SVN_ERR(svn_fs_delete(txn_root, "A/D/H", pool));

    /* TXN must actually be based upon revisions[1] (instead of HEAD). */
    SVN_TEST_ASSERT(svn_fs_txn_base_revision(txn) == revisions[1]);

    /* We used to create the revision like this before fixing issue
       #2751 -- Directory prop mods reverted in overlapping commits scenario.

       But we now expect that to fail as out of date */
    {
      svn_revnum_t failed_rev;
      SVN_ERR(test_commit_txn(&failed_rev, txn, "/A/D/H", pool));

      SVN_TEST_ASSERT(failed_rev == SVN_INVALID_REVNUM);
    }
    /*********************************************************************/
    /* REVISION 6 */
    /*********************************************************************/
    {
      static svn_test__tree_entry_t expected_entries[] = {
        /* path, contents (0 = dir) */
        { "theta",         "This is the file 'theta'.\n" },
        { "A",             0 },
        { "A/mu",          "This is the file 'mu'.\n" },
        { "A/B",           0 },
        { "A/B/lambda",    "This is the file 'lambda'.\n" },
        { "A/B/E",         0 },
        { "A/B/E/alpha",   "This is the file 'alpha'.\n" },
        { "A/B/E/beta",    "This is the file 'beta'.\n" },
        { "A/B/F",         0 },
        { "A/C",           0 },
        { "A/C/kappa",     "This is the file 'kappa'.\n" },
        { "A/D",           0 },
        { "A/D/gamma",     "This is the file 'gamma'.\n" },
        { "A/D/G",         0 },
        { "A/D/G/pi",      "This is the file 'pi'.\n" },
        { "A/D/G/rho",     "This is the file 'rho'.\n" },
        { "A/D/G/tau",     "This is the file 'tau'.\n" },
        { "A/D/I",         0 },
        { "A/D/I/delta",   "This is the file 'delta'.\n" },
        { "A/D/I/epsilon", "This is the file 'epsilon'.\n" }
      };
      SVN_ERR(svn_fs_revision_root(&revision_root, fs, after_rev, pool));
      SVN_ERR(svn_test__validate_tree(revision_root,
                                      expected_entries,
                                      20, pool));
    }
    revisions[revision_count++] = after_rev;

    /* Try deleting a file F inside a subtree S where S does not exist
       in the most recent revision, but does exist in the ancestor
       tree.  This should conflict. */
    {
      svn_revnum_t failed_rev;
      SVN_ERR(svn_fs_begin_txn(&txn, fs, revisions[1], pool));
      SVN_ERR(svn_fs_txn_root(&txn_root, txn, pool));
      SVN_ERR(svn_fs_delete(txn_root, "A/D/H/omega", pool));
      SVN_ERR(test_commit_txn(&failed_rev, txn, "/A/D/H", pool));
      SVN_ERR(svn_fs_abort_txn(txn, pool));

      SVN_TEST_ASSERT(failed_rev == SVN_INVALID_REVNUM);
    }

    /* E exists in both ANCESTOR and B ... */
    {
      /* (1) but refers to different nodes.  Conflict. */
      SVN_ERR(svn_fs_begin_txn(&txn, fs, after_rev, pool));
      SVN_ERR(svn_fs_txn_root(&txn_root, txn, pool));
      SVN_ERR(svn_fs_make_dir(txn_root, "A/D/H", pool));
      SVN_ERR(test_commit_txn(&after_rev, txn, NULL, pool));
      revisions[revision_count++] = after_rev;

      /*********************************************************************/
      /* REVISION 7 */
      /*********************************************************************/

      /* Re-remove A/D/H because future tests expect it to be absent. */
      {
        SVN_ERR(svn_fs_begin_txn
                (&txn, fs, revisions[revision_count - 1], pool));
        SVN_ERR(svn_fs_txn_root(&txn_root, txn, pool));
        SVN_ERR(svn_fs_delete(txn_root, "A/D/H", pool));
        SVN_ERR(test_commit_txn(&after_rev, txn, NULL, pool));
        revisions[revision_count++] = after_rev;
      }

      /*********************************************************************/
      /* REVISION 8 (looks exactly like revision 6, we hope) */
      /*********************************************************************/

      /* (1) but refers to different revisions of the same node.
         Conflict. */
      SVN_ERR(svn_fs_begin_txn(&txn, fs, revisions[1], pool));
      SVN_ERR(svn_fs_txn_root(&txn_root, txn, pool));
      SVN_ERR(svn_fs_make_file(txn_root, "A/D/H/zeta", pool));
      SVN_ERR(test_commit_txn(&after_rev, txn, "/A/D/H", pool));
      SVN_ERR(svn_fs_abort_txn(txn, pool));

      /* (1) and refers to the same node revision.  Omit E from the
         merged tree.  This is already tested in Merge-Test 3
         (A/D/H/chi, A/D/H/psi, e.g.), but we'll test it here again
         anyway.  A little paranoia never hurt anyone.  */
      SVN_ERR(svn_fs_begin_txn(&txn, fs, revisions[1], pool));
      SVN_ERR(svn_fs_txn_root(&txn_root, txn, pool));
      SVN_ERR(svn_fs_delete(txn_root, "A/mu", pool)); /* unrelated change */
      SVN_ERR(test_commit_txn(&after_rev, txn, NULL, pool));

      /*********************************************************************/
      /* REVISION 9 */
      /*********************************************************************/
      {
        static svn_test__tree_entry_t expected_entries[] = {
          /* path, contents (0 = dir) */
          { "theta",         "This is the file 'theta'.\n" },
          { "A",             0 },
          { "A/B",           0 },
          { "A/B/lambda",    "This is the file 'lambda'.\n" },
          { "A/B/E",         0 },
          { "A/B/E/alpha",   "This is the file 'alpha'.\n" },
          { "A/B/E/beta",    "This is the file 'beta'.\n" },
          { "A/B/F",         0 },
          { "A/C",           0 },
          { "A/C/kappa",     "This is the file 'kappa'.\n" },
          { "A/D",           0 },
          { "A/D/gamma",     "This is the file 'gamma'.\n" },
          { "A/D/G",         0 },
          { "A/D/G/pi",      "This is the file 'pi'.\n" },
          { "A/D/G/rho",     "This is the file 'rho'.\n" },
          { "A/D/G/tau",     "This is the file 'tau'.\n" },
          { "A/D/I",         0 },
          { "A/D/I/delta",   "This is the file 'delta'.\n" },
          { "A/D/I/epsilon", "This is the file 'epsilon'.\n" }
        };
        SVN_ERR(svn_fs_revision_root(&revision_root, fs, after_rev, pool));
        SVN_ERR(svn_test__validate_tree(revision_root,
                                        expected_entries,
                                        19, pool));
      }
      revisions[revision_count++] = after_rev;
    }
  }

  /* Preparation for upcoming tests.
     We make a new head revision, with A/mu restored, but containing
     slightly different contents than its first incarnation. */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, revisions[revision_count-1], pool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, pool));
  SVN_ERR(svn_fs_make_file(txn_root, "A/mu", pool));
  SVN_ERR(svn_test__set_file_contents
          (txn_root, "A/mu", "A new file 'mu'.\n", pool));
  SVN_ERR(svn_fs_make_file(txn_root, "A/D/G/xi", pool));
  SVN_ERR(svn_test__set_file_contents
          (txn_root, "A/D/G/xi", "This is the file 'xi'.\n", pool));
  SVN_ERR(test_commit_txn(&after_rev, txn, NULL, pool));
  /*********************************************************************/
  /* REVISION 10 */
  /*********************************************************************/
  {
    static svn_test__tree_entry_t expected_entries[] = {
      /* path, contents (0 = dir) */
      { "theta",         "This is the file 'theta'.\n" },
      { "A",             0 },
      { "A/mu",          "A new file 'mu'.\n" },
      { "A/B",           0 },
      { "A/B/lambda",    "This is the file 'lambda'.\n" },
      { "A/B/E",         0 },
      { "A/B/E/alpha",   "This is the file 'alpha'.\n" },
      { "A/B/E/beta",    "This is the file 'beta'.\n" },
      { "A/B/F",         0 },
      { "A/C",           0 },
      { "A/C/kappa",     "This is the file 'kappa'.\n" },
      { "A/D",           0 },
      { "A/D/gamma",     "This is the file 'gamma'.\n" },
      { "A/D/G",         0 },
      { "A/D/G/pi",      "This is the file 'pi'.\n" },
      { "A/D/G/rho",     "This is the file 'rho'.\n" },
      { "A/D/G/tau",     "This is the file 'tau'.\n" },
      { "A/D/G/xi",      "This is the file 'xi'.\n" },
      { "A/D/I",         0 },
      { "A/D/I/delta",   "This is the file 'delta'.\n" },
      { "A/D/I/epsilon", "This is the file 'epsilon'.\n" }
    };
    SVN_ERR(svn_fs_revision_root(&revision_root, fs, after_rev, pool));
    SVN_ERR(svn_test__validate_tree(revision_root, expected_entries,
                                    21, pool));
  }
  revisions[revision_count++] = after_rev;

  /* (3) E exists in both ANCESTOR and A, but refers to different
     nodes. */
  {
    /* (1) E exists in both ANCESTOR and B, but refers to different
       nodes, and not all nodes are directories.  Conflict. */

    /* ### kff todo: A/mu's contents will be exactly the same.
       If the fs ever starts optimizing this case, these tests may
       start to fail. */
    SVN_ERR(svn_fs_begin_txn(&txn, fs, revisions[1], pool));
    SVN_ERR(svn_fs_txn_root(&txn_root, txn, pool));
    SVN_ERR(svn_fs_delete(txn_root, "A/mu", pool));
    SVN_ERR(svn_fs_make_file(txn_root, "A/mu", pool));
    SVN_ERR(svn_test__set_file_contents
            (txn_root, "A/mu", "This is the file 'mu'.\n", pool));
    SVN_ERR(test_commit_txn(&after_rev, txn, "/A/mu", pool));
    SVN_ERR(svn_fs_abort_txn(txn, pool));

    /* (1) E exists in both ANCESTOR and B, but refers to different
       revisions of the same node.  Conflict. */
    SVN_ERR(svn_fs_begin_txn(&txn, fs, revisions[1], pool));
    SVN_ERR(svn_fs_txn_root(&txn_root, txn, pool));
    SVN_ERR(svn_test__set_file_contents
            (txn_root, "A/mu", "A change to file 'mu'.\n", pool));
    SVN_ERR(test_commit_txn(&after_rev, txn, "/A/mu", pool));
    SVN_ERR(svn_fs_abort_txn(txn, pool));

    /* (1) E exists in both ANCESTOR and B, and refers to the same
       node revision.  Replace E with A's node revision.  */
    {
      svn_stringbuf_t *old_mu_contents;
      SVN_ERR(svn_fs_begin_txn(&txn, fs, revisions[1], pool));
      SVN_ERR(svn_fs_txn_root(&txn_root, txn, pool));
      SVN_ERR(svn_test__get_file_contents
              (txn_root, "A/mu", &old_mu_contents, pool));
      if ((! old_mu_contents) || (strcmp(old_mu_contents->data,
                                         "This is the file 'mu'.\n") != 0))
        {
          return svn_error_create
            (SVN_ERR_FS_GENERAL, NULL,
             "got wrong contents from an old revision tree");
        }
      SVN_ERR(svn_fs_make_file(txn_root, "A/sigma", pool));
      SVN_ERR(svn_test__set_file_contents  /* unrelated change */
              (txn_root, "A/sigma", "This is the file 'sigma'.\n", pool));
      SVN_ERR(test_commit_txn(&after_rev, txn, NULL, pool));
      /*********************************************************************/
      /* REVISION 11 */
      /*********************************************************************/
      {
        static svn_test__tree_entry_t expected_entries[] = {
          /* path, contents (0 = dir) */
          { "theta",         "This is the file 'theta'.\n" },
          { "A",             0 },
          { "A/mu",          "A new file 'mu'.\n" },
          { "A/sigma",       "This is the file 'sigma'.\n" },
          { "A/B",           0 },
          { "A/B/lambda",    "This is the file 'lambda'.\n" },
          { "A/B/E",         0 },
          { "A/B/E/alpha",   "This is the file 'alpha'.\n" },
          { "A/B/E/beta",    "This is the file 'beta'.\n" },
          { "A/B/F",         0 },
          { "A/C",           0 },
          { "A/C/kappa",     "This is the file 'kappa'.\n" },
          { "A/D",           0 },
          { "A/D/gamma",     "This is the file 'gamma'.\n" },
          { "A/D/G",         0 },
          { "A/D/G/pi",      "This is the file 'pi'.\n" },
          { "A/D/G/rho",     "This is the file 'rho'.\n" },
          { "A/D/G/tau",     "This is the file 'tau'.\n" },
          { "A/D/G/xi",      "This is the file 'xi'.\n" },
          { "A/D/I",         0 },
          { "A/D/I/delta",   "This is the file 'delta'.\n" },
          { "A/D/I/epsilon", "This is the file 'epsilon'.\n" }
        };
        SVN_ERR(svn_fs_revision_root(&revision_root, fs, after_rev, pool));
        SVN_ERR(svn_test__validate_tree(revision_root,
                                        expected_entries,
                                        22, pool));
      }
      revisions[revision_count++] = after_rev;
    }
  }

  /* Preparation for upcoming tests.
     We make a new head revision.  There are two changes in the new
     revision: A/B/lambda has been modified.  We will also use the
     recent addition of A/D/G/xi, treated as a modification to
     A/D/G. */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, revisions[revision_count-1], pool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, pool));
  SVN_ERR(svn_test__set_file_contents
          (txn_root, "A/B/lambda", "Change to file 'lambda'.\n", pool));
  SVN_ERR(test_commit_txn(&after_rev, txn, NULL, pool));
  /*********************************************************************/
  /* REVISION 12 */
  /*********************************************************************/
  {
    static svn_test__tree_entry_t expected_entries[] = {
      /* path, contents (0 = dir) */
      { "theta",         "This is the file 'theta'.\n" },
      { "A",             0 },
      { "A/mu",          "A new file 'mu'.\n" },
      { "A/sigma",       "This is the file 'sigma'.\n" },
      { "A/B",           0 },
      { "A/B/lambda",    "Change to file 'lambda'.\n" },
      { "A/B/E",         0 },
      { "A/B/E/alpha",   "This is the file 'alpha'.\n" },
      { "A/B/E/beta",    "This is the file 'beta'.\n" },
      { "A/B/F",         0 },
      { "A/C",           0 },
      { "A/C/kappa",     "This is the file 'kappa'.\n" },
      { "A/D",           0 },
      { "A/D/gamma",     "This is the file 'gamma'.\n" },
      { "A/D/G",         0 },
      { "A/D/G/pi",      "This is the file 'pi'.\n" },
      { "A/D/G/rho",     "This is the file 'rho'.\n" },
      { "A/D/G/tau",     "This is the file 'tau'.\n" },
      { "A/D/G/xi",      "This is the file 'xi'.\n" },
      { "A/D/I",         0 },
      { "A/D/I/delta",   "This is the file 'delta'.\n" },
      { "A/D/I/epsilon", "This is the file 'epsilon'.\n" }
    };
    SVN_ERR(svn_fs_revision_root(&revision_root, fs, after_rev, pool));
    SVN_ERR(svn_test__validate_tree(revision_root, expected_entries,
                                    22, pool));
  }
  revisions[revision_count++] = after_rev;

  /* (2) E exists in both ANCESTOR and A, but refers to different
     revisions of the same node. */
  {
    /* (1a) E exists in both ANCESTOR and B, but refers to different
       revisions of the same file node.  Conflict. */
    SVN_ERR(svn_fs_begin_txn(&txn, fs, revisions[1], pool));
    SVN_ERR(svn_fs_txn_root(&txn_root, txn, pool));
    SVN_ERR(svn_test__set_file_contents
            (txn_root, "A/B/lambda", "A different change to 'lambda'.\n",
             pool));
    SVN_ERR(test_commit_txn(&after_rev, txn, "/A/B/lambda", pool));
    SVN_ERR(svn_fs_abort_txn(txn, pool));

    /* (1b) E exists in both ANCESTOR and B, but refers to different
       revisions of the same directory node.  Merge A/E and B/E,
       recursively.  Succeed, because no conflict beneath E. */
    SVN_ERR(svn_fs_begin_txn(&txn, fs, revisions[1], pool));
    SVN_ERR(svn_fs_txn_root(&txn_root, txn, pool));
    SVN_ERR(svn_fs_make_file(txn_root, "A/D/G/nu", pool));
    SVN_ERR(svn_test__set_file_contents
            (txn_root, "A/D/G/nu", "This is the file 'nu'.\n", pool));
    SVN_ERR(test_commit_txn(&after_rev, txn, NULL, pool));
    /*********************************************************************/
    /* REVISION 13 */
    /*********************************************************************/
    {
      static svn_test__tree_entry_t expected_entries[] = {
        /* path, contents (0 = dir) */
        { "theta",         "This is the file 'theta'.\n" },
        { "A",             0 },
        { "A/mu",          "A new file 'mu'.\n" },
        { "A/sigma",       "This is the file 'sigma'.\n" },
        { "A/B",           0 },
        { "A/B/lambda",    "Change to file 'lambda'.\n" },
        { "A/B/E",         0 },
        { "A/B/E/alpha",   "This is the file 'alpha'.\n" },
        { "A/B/E/beta",    "This is the file 'beta'.\n" },
        { "A/B/F",         0 },
        { "A/C",           0 },
        { "A/C/kappa",     "This is the file 'kappa'.\n" },
        { "A/D",           0 },
        { "A/D/gamma",     "This is the file 'gamma'.\n" },
        { "A/D/G",         0 },
        { "A/D/G/pi",      "This is the file 'pi'.\n" },
        { "A/D/G/rho",     "This is the file 'rho'.\n" },
        { "A/D/G/tau",     "This is the file 'tau'.\n" },
        { "A/D/G/xi",      "This is the file 'xi'.\n" },
        { "A/D/G/nu",      "This is the file 'nu'.\n" },
        { "A/D/I",         0 },
        { "A/D/I/delta",   "This is the file 'delta'.\n" },
        { "A/D/I/epsilon", "This is the file 'epsilon'.\n" }
      };
      SVN_ERR(svn_fs_revision_root(&revision_root, fs, after_rev, pool));
      SVN_ERR(svn_test__validate_tree(revision_root,
                                      expected_entries,
                                      23, pool));
    }
    revisions[revision_count++] = after_rev;

    /* (1c) E exists in both ANCESTOR and B, but refers to different
       revisions of the same directory node.  Merge A/E and B/E,
       recursively.  Fail, because conflict beneath E. */
    SVN_ERR(svn_fs_begin_txn(&txn, fs, revisions[1], pool));
    SVN_ERR(svn_fs_txn_root(&txn_root, txn, pool));
    SVN_ERR(svn_fs_make_file(txn_root, "A/D/G/xi", pool));
    SVN_ERR(svn_test__set_file_contents
            (txn_root, "A/D/G/xi", "This is a different file 'xi'.\n", pool));
    SVN_ERR(test_commit_txn(&after_rev, txn, "/A/D/G/xi", pool));
    SVN_ERR(svn_fs_abort_txn(txn, pool));

    /* (1) E exists in both ANCESTOR and B, and refers to the same node
       revision.  Replace E with A's node revision.  */
    {
      svn_stringbuf_t *old_lambda_ctnts;
      SVN_ERR(svn_fs_begin_txn(&txn, fs, revisions[1], pool));
      SVN_ERR(svn_fs_txn_root(&txn_root, txn, pool));
      SVN_ERR(svn_test__get_file_contents
              (txn_root, "A/B/lambda", &old_lambda_ctnts, pool));
      if ((! old_lambda_ctnts)
          || (strcmp(old_lambda_ctnts->data,
                     "This is the file 'lambda'.\n") != 0))
        {
          return svn_error_create
            (SVN_ERR_FS_GENERAL, NULL,
             "got wrong contents from an old revision tree");
        }
      SVN_ERR(svn_test__set_file_contents
              (txn_root, "A/D/G/rho",
               "This is an irrelevant change to 'rho'.\n", pool));
      SVN_ERR(test_commit_txn(&after_rev, txn, NULL, pool));
      /*********************************************************************/
      /* REVISION 14 */
      /*********************************************************************/
      {
        static svn_test__tree_entry_t expected_entries[] = {
          /* path, contents (0 = dir) */
          { "theta",         "This is the file 'theta'.\n" },
          { "A",             0 },
          { "A/mu",          "A new file 'mu'.\n" },
          { "A/sigma",       "This is the file 'sigma'.\n" },
          { "A/B",           0 },
          { "A/B/lambda",    "Change to file 'lambda'.\n" },
          { "A/B/E",         0 },
          { "A/B/E/alpha",   "This is the file 'alpha'.\n" },
          { "A/B/E/beta",    "This is the file 'beta'.\n" },
          { "A/B/F",         0 },
          { "A/C",           0 },
          { "A/C/kappa",     "This is the file 'kappa'.\n" },
          { "A/D",           0 },
          { "A/D/gamma",     "This is the file 'gamma'.\n" },
          { "A/D/G",         0 },
          { "A/D/G/pi",      "This is the file 'pi'.\n" },
          { "A/D/G/rho",     "This is an irrelevant change to 'rho'.\n" },
          { "A/D/G/tau",     "This is the file 'tau'.\n" },
          { "A/D/G/xi",      "This is the file 'xi'.\n" },
          { "A/D/G/nu",      "This is the file 'nu'.\n"},
          { "A/D/I",         0 },
          { "A/D/I/delta",   "This is the file 'delta'.\n" },
          { "A/D/I/epsilon", "This is the file 'epsilon'.\n" }
        };
        SVN_ERR(svn_fs_revision_root(&revision_root, fs, after_rev, pool));
        SVN_ERR(svn_test__validate_tree(revision_root,
                                        expected_entries,
                                        23, pool));
      }
      revisions[revision_count++] = after_rev;
    }
  }

  /* (1) E exists in both ANCESTOR and A, and refers to the same node
     revision. */
  {
    /* (1) E exists in both ANCESTOR and B, and refers to the same
       node revision.  Nothing has happened to ANCESTOR/E, so no
       change is necessary. */

    /* This has now been tested about fifty-four trillion times.  We
       don't need to test it again here. */
  }

  /* E exists in ANCESTOR, but has been deleted from A.  E exists in
     both ANCESTOR and B but refers to different revisions of the same
     node.  Conflict.  */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, revisions[1], pool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, pool));
  SVN_ERR(svn_test__set_file_contents
          (txn_root, "iota", "New contents for 'iota'.\n", pool));
  SVN_ERR(test_commit_txn(&after_rev, txn, "/iota", pool));
  SVN_ERR(svn_fs_abort_txn(txn, pool));

  return SVN_NO_ERROR;
}


static svn_error_t *
copy_test(const svn_test_opts_t *opts,
          apr_pool_t *pool)
{
  svn_fs_t *fs;
  svn_fs_txn_t *txn;
  svn_fs_root_t *txn_root, *rev_root;
  svn_revnum_t after_rev;

  /* Prepare a filesystem. */
  SVN_ERR(svn_test__create_fs(&fs, "test-repo-copy",
                              opts, pool));

  /* In first txn, create and commit the greek tree. */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, 0, pool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, pool));
  SVN_ERR(svn_test__create_greek_tree(txn_root, pool));
  SVN_ERR(test_commit_txn(&after_rev, txn, NULL, pool));

  /* In second txn, copy the file A/D/G/pi into the subtree A/D/H as
     pi2.  Change that file's contents to state its new name.  Along
     the way, test that the copy history was preserved both during the
     transaction and after the commit. */

  SVN_ERR(svn_fs_revision_root(&rev_root, fs, after_rev, pool));
  SVN_ERR(svn_fs_begin_txn(&txn, fs, after_rev, pool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, pool));
  SVN_ERR(svn_fs_copy(rev_root, "A/D/G/pi",
                      txn_root, "A/D/H/pi2",
                      pool));
  { /* Check that copy history was preserved. */
    svn_revnum_t rev;
    const char *path;

    SVN_ERR(svn_fs_copied_from(&rev, &path, txn_root,
                               "A/D/H/pi2", pool));

    if (rev != after_rev)
      return svn_error_create
        (SVN_ERR_FS_GENERAL, NULL,
         "pre-commit copy history not preserved (rev lost) for A/D/H/pi2");

    if (strcmp(path, "/A/D/G/pi") != 0)
      return svn_error_create
        (SVN_ERR_FS_GENERAL, NULL,
         "pre-commit copy history not preserved (path lost) for A/D/H/pi2");
  }
  SVN_ERR(svn_test__set_file_contents
          (txn_root, "A/D/H/pi2", "This is the file 'pi2'.\n", pool));
  SVN_ERR(test_commit_txn(&after_rev, txn, NULL, pool));

  { /* Check that copy history is still preserved _after_ the commit. */
    svn_fs_root_t *root;
    svn_revnum_t rev;
    const char *path;

    SVN_ERR(svn_fs_revision_root(&root, fs, after_rev, pool));
    SVN_ERR(svn_fs_copied_from(&rev, &path, root, "A/D/H/pi2", pool));

    if (rev != (after_rev - 1))
      return svn_error_create
        (SVN_ERR_FS_GENERAL, NULL,
         "post-commit copy history wrong (rev) for A/D/H/pi2");

    if (strcmp(path, "/A/D/G/pi") != 0)
      return svn_error_create
        (SVN_ERR_FS_GENERAL, NULL,
         "post-commit copy history wrong (path) for A/D/H/pi2");
  }

  /* Let's copy the copy we just made, to make sure copy history gets
     chained correctly. */
  SVN_ERR(svn_fs_revision_root(&rev_root, fs, after_rev, pool));
  SVN_ERR(svn_fs_begin_txn(&txn, fs, after_rev, pool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, pool));
  SVN_ERR(svn_fs_copy(rev_root, "A/D/H/pi2", txn_root, "A/D/H/pi3", pool));
  SVN_ERR(test_commit_txn(&after_rev, txn, NULL, pool));
  { /* Check the copy history. */
    svn_fs_root_t *root;
    svn_revnum_t rev;
    const char *path;

    /* Check that the original copy still has its old history. */
    SVN_ERR(svn_fs_revision_root(&root, fs, (after_rev - 1), pool));
    SVN_ERR(svn_fs_copied_from(&rev, &path, root, "A/D/H/pi2", pool));

    if (rev != (after_rev - 2))
      return svn_error_create
        (SVN_ERR_FS_GENERAL, NULL,
         "first copy history wrong (rev) for A/D/H/pi2");

    if (strcmp(path, "/A/D/G/pi") != 0)
      return svn_error_create
        (SVN_ERR_FS_GENERAL, NULL,
         "first copy history wrong (path) for A/D/H/pi2");

    /* Check that the copy of the copy has the right history. */
    SVN_ERR(svn_fs_revision_root(&root, fs, after_rev, pool));
    SVN_ERR(svn_fs_copied_from(&rev, &path, root, "A/D/H/pi3", pool));

    if (rev != (after_rev - 1))
      return svn_error_create
        (SVN_ERR_FS_GENERAL, NULL,
         "second copy history wrong (rev) for A/D/H/pi3");

    if (strcmp(path, "/A/D/H/pi2") != 0)
      return svn_error_create
        (SVN_ERR_FS_GENERAL, NULL,
         "second copy history wrong (path) for A/D/H/pi3");
  }

  /* Commit a regular change to a copy, make sure the copy history
     isn't inherited. */
  SVN_ERR(svn_fs_revision_root(&rev_root, fs, after_rev, pool));
  SVN_ERR(svn_fs_begin_txn(&txn, fs, after_rev, pool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, pool));
  SVN_ERR(svn_test__set_file_contents
          (txn_root, "A/D/H/pi3", "This is the file 'pi3'.\n", pool));
  SVN_ERR(test_commit_txn(&after_rev, txn, NULL, pool));
  { /* Check the copy history. */
    svn_fs_root_t *root;
    svn_revnum_t rev;
    const char *path;

    /* Check that the copy still has its history. */
    SVN_ERR(svn_fs_revision_root(&root, fs, (after_rev - 1), pool));
    SVN_ERR(svn_fs_copied_from(&rev, &path, root, "A/D/H/pi3", pool));

    if (rev != (after_rev - 2))
      return svn_error_create
        (SVN_ERR_FS_GENERAL, NULL,
         "copy history wrong (rev) for A/D/H/pi3");

    if (strcmp(path, "/A/D/H/pi2") != 0)
      return svn_error_create
        (SVN_ERR_FS_GENERAL, NULL,
         "copy history wrong (path) for A/D/H/pi3");

    /* Check that the next revision after the copy has no copy history. */
    SVN_ERR(svn_fs_revision_root(&root, fs, after_rev, pool));
    SVN_ERR(svn_fs_copied_from(&rev, &path, root, "A/D/H/pi3", pool));

    if (rev != SVN_INVALID_REVNUM)
      return svn_error_create
        (SVN_ERR_FS_GENERAL, NULL,
         "copy history wrong (rev) for A/D/H/pi3");

    if (path != NULL)
      return svn_error_create
        (SVN_ERR_FS_GENERAL, NULL,
         "copy history wrong (path) for A/D/H/pi3");
  }

  /* Then, as if that wasn't fun enough, copy the whole subtree A/D/H
     into the root directory as H2! */
  SVN_ERR(svn_fs_revision_root(&rev_root, fs, after_rev, pool));
  SVN_ERR(svn_fs_begin_txn(&txn, fs, after_rev, pool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, pool));
  SVN_ERR(svn_fs_copy(rev_root, "A/D/H", txn_root, "H2", pool));
  SVN_ERR(test_commit_txn(&after_rev, txn, NULL, pool));
  { /* Check the copy history. */
    svn_fs_root_t *root;
    svn_revnum_t rev;
    const char *path;

    /* Check that the top of the copy has history. */
    SVN_ERR(svn_fs_revision_root(&root, fs, after_rev, pool));
    SVN_ERR(svn_fs_copied_from(&rev, &path, root, "H2", pool));

    if (rev != (after_rev - 1))
      return svn_error_create
        (SVN_ERR_FS_GENERAL, NULL,
         "copy history wrong (rev) for H2");

    if (strcmp(path, "/A/D/H") != 0)
      return svn_error_create
        (SVN_ERR_FS_GENERAL, NULL,
         "copy history wrong (path) for H2");

    /* Check that a random file under H2 reports no copy history. */
    SVN_ERR(svn_fs_copied_from(&rev, &path, root, "H2/omega", pool));

    if (rev != SVN_INVALID_REVNUM)
      return svn_error_create
        (SVN_ERR_FS_GENERAL, NULL,
         "copy history wrong (rev) for H2/omega");

    if (path != NULL)
      return svn_error_create
        (SVN_ERR_FS_GENERAL, NULL,
         "copy history wrong (path) for H2/omega");

    /* Note that H2/pi2 still has copy history, though.  See the doc
       string for svn_fs_copied_from() for more on this. */
  }

  /* Let's live dangerously.  What happens if we copy a path into one
     of its own children.  Looping filesystem?  Cyclic ancestry?
     Another West Virginia family tree with no branches?  We certainly
     hope that's not the case. */
  SVN_ERR(svn_fs_revision_root(&rev_root, fs, after_rev, pool));
  SVN_ERR(svn_fs_begin_txn(&txn, fs, after_rev, pool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, pool));
  SVN_ERR(svn_fs_copy(rev_root, "A/B", txn_root, "A/B/E/B", pool));
  SVN_ERR(test_commit_txn(&after_rev, txn, NULL, pool));
  { /* Check the copy history. */
    svn_fs_root_t *root;
    svn_revnum_t rev;
    const char *path;

    /* Check that the copy has history. */
    SVN_ERR(svn_fs_revision_root(&root, fs, after_rev, pool));
    SVN_ERR(svn_fs_copied_from(&rev, &path, root, "A/B/E/B", pool));

    if (rev != (after_rev - 1))
      return svn_error_create
        (SVN_ERR_FS_GENERAL, NULL,
         "copy history wrong (rev) for A/B/E/B");

    if (strcmp(path, "/A/B") != 0)
      return svn_error_create
        (SVN_ERR_FS_GENERAL, NULL,
         "copy history wrong (path) for A/B/E/B");

    /* Check that the original does not have copy history. */
    SVN_ERR(svn_fs_revision_root(&root, fs, after_rev, pool));
    SVN_ERR(svn_fs_copied_from(&rev, &path, root, "A/B", pool));

    if (rev != SVN_INVALID_REVNUM)
      return svn_error_create
        (SVN_ERR_FS_GENERAL, NULL,
         "copy history wrong (rev) for A/B");

    if (path != NULL)
      return svn_error_create
        (SVN_ERR_FS_GENERAL, NULL,
         "copy history wrong (path) for A/B");
  }

  /* After all these changes, let's see if the filesystem looks as we
     would expect it to. */
  {
    static svn_test__tree_entry_t expected_entries[] = {
      /* path, contents (0 = dir) */
      { "iota",        "This is the file 'iota'.\n" },
      { "H2",          0 },
      { "H2/chi",      "This is the file 'chi'.\n" },
      { "H2/pi2",      "This is the file 'pi2'.\n" },
      { "H2/pi3",      "This is the file 'pi3'.\n" },
      { "H2/psi",      "This is the file 'psi'.\n" },
      { "H2/omega",    "This is the file 'omega'.\n" },
      { "A",           0 },
      { "A/mu",        "This is the file 'mu'.\n" },
      { "A/B",         0 },
      { "A/B/lambda",  "This is the file 'lambda'.\n" },
      { "A/B/E",       0 },
      { "A/B/E/alpha", "This is the file 'alpha'.\n" },
      { "A/B/E/beta",  "This is the file 'beta'.\n" },
      { "A/B/E/B",         0 },
      { "A/B/E/B/lambda",  "This is the file 'lambda'.\n" },
      { "A/B/E/B/E",       0 },
      { "A/B/E/B/E/alpha", "This is the file 'alpha'.\n" },
      { "A/B/E/B/E/beta",  "This is the file 'beta'.\n" },
      { "A/B/E/B/F",       0 },
      { "A/B/F",       0 },
      { "A/C",         0 },
      { "A/D",         0 },
      { "A/D/gamma",   "This is the file 'gamma'.\n" },
      { "A/D/G",       0 },
      { "A/D/G/pi",    "This is the file 'pi'.\n" },
      { "A/D/G/rho",   "This is the file 'rho'.\n" },
      { "A/D/G/tau",   "This is the file 'tau'.\n" },
      { "A/D/H",       0 },
      { "A/D/H/chi",   "This is the file 'chi'.\n" },
      { "A/D/H/pi2",   "This is the file 'pi2'.\n" },
      { "A/D/H/pi3",   "This is the file 'pi3'.\n" },
      { "A/D/H/psi",   "This is the file 'psi'.\n" },
      { "A/D/H/omega", "This is the file 'omega'.\n" }
    };
    SVN_ERR(svn_fs_revision_root(&rev_root, fs, after_rev, pool));
    SVN_ERR(svn_test__validate_tree(rev_root, expected_entries,
                                    34, pool));
  }

  return SVN_NO_ERROR;
}


/* This tests deleting of mutable nodes.  We build a tree in a
 * transaction, then try to delete various items in the tree.  We
 * never commit the tree, so every entry being deleted points to a
 * mutable node.
 *
 * ### todo: this test was written before commits worked.  It might
 * now be worthwhile to combine it with delete().
 */
static svn_error_t *
delete_mutables(const svn_test_opts_t *opts,
                apr_pool_t *pool)
{
  svn_fs_t *fs;
  svn_fs_txn_t *txn;
  svn_fs_root_t *txn_root;
  svn_error_t *err;

  /* Prepare a txn to receive the greek tree. */
  SVN_ERR(svn_test__create_fs(&fs, "test-repo-del-from-dir",
                              opts, pool));
  SVN_ERR(svn_fs_begin_txn(&txn, fs, 0, pool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, pool));

  /* Create the greek tree. */
  SVN_ERR(svn_test__create_greek_tree(txn_root, pool));

  /* Baby, it's time to test like you've never tested before.  We do
   * the following, in this order:
   *
   *    1. Delete a single file somewhere, succeed.
   *    2. Delete two files of three, then make sure the third remains.
   *    3. Delete the third and last file.
   *    4. Try again to delete the dir, succeed.
   *    5. Delete one of the natively empty dirs, succeed.
   *    6. Try to delete root, fail.
   *    7. Try to delete a top-level file, succeed.
   *
   * Specifically, that's:
   *
   *    1. Delete A/D/gamma.
   *    2. Delete A/D/G/pi, A/D/G/rho.
   *    3. Delete A/D/G/tau.
   *    4. Try again to delete A/D/G, succeed.
   *    5. Delete A/C.
   *    6. Try to delete /, fail.
   *    7. Try to delete iota, succeed.
   *
   * Before and after each deletion or attempted deletion, we probe
   * the affected directory, to make sure everything is as it should
   * be.
   */

  /* 1 */
  {
    const svn_fs_id_t *gamma_id;
    SVN_ERR(svn_fs_node_id(&gamma_id, txn_root, "A/D/gamma", pool));
    SVN_ERR(check_entry_present(txn_root, "A/D", "gamma", pool));
    SVN_ERR(svn_fs_delete(txn_root, "A/D/gamma", pool));
    SVN_ERR(check_entry_absent(txn_root, "A/D", "gamma", pool));
  }

  /* 2 */
  {
    const svn_fs_id_t *pi_id, *rho_id, *tau_id;
    SVN_ERR(svn_fs_node_id(&pi_id, txn_root, "A/D/G/pi", pool));
    SVN_ERR(svn_fs_node_id(&rho_id, txn_root, "A/D/G/rho", pool));
    SVN_ERR(svn_fs_node_id(&tau_id, txn_root, "A/D/G/tau", pool));
    SVN_ERR(check_entry_present(txn_root, "A/D/G", "pi", pool));
    SVN_ERR(check_entry_present(txn_root, "A/D/G", "rho", pool));
    SVN_ERR(check_entry_present(txn_root, "A/D/G", "tau", pool));
    SVN_ERR(svn_fs_delete(txn_root, "A/D/G/pi", pool));
    SVN_ERR(check_entry_absent(txn_root, "A/D/G", "pi", pool));
    SVN_ERR(check_entry_present(txn_root, "A/D/G", "rho", pool));
    SVN_ERR(check_entry_present(txn_root, "A/D/G", "tau", pool));
    SVN_ERR(svn_fs_delete(txn_root, "A/D/G/rho", pool));
    SVN_ERR(check_entry_absent(txn_root, "A/D/G", "pi", pool));
    SVN_ERR(check_entry_absent(txn_root, "A/D/G", "rho", pool));
    SVN_ERR(check_entry_present(txn_root, "A/D/G", "tau", pool));
  }

  /* 3 */
  {
    const svn_fs_id_t *tau_id;
    SVN_ERR(svn_fs_node_id(&tau_id, txn_root, "A/D/G/tau", pool));
    SVN_ERR(check_entry_present(txn_root, "A/D/G", "tau", pool));
    SVN_ERR(svn_fs_delete(txn_root, "A/D/G/tau", pool));
    SVN_ERR(check_entry_absent(txn_root, "A/D/G", "tau", pool));
  }

  /* 4 */
  {
    const svn_fs_id_t *G_id;
    SVN_ERR(svn_fs_node_id(&G_id, txn_root, "A/D/G", pool));
    SVN_ERR(check_entry_present(txn_root, "A/D", "G", pool));
    SVN_ERR(svn_fs_delete(txn_root, "A/D/G", pool));        /* succeed */
    SVN_ERR(check_entry_absent(txn_root, "A/D", "G", pool));
  }

  /* 5 */
  {
    const svn_fs_id_t *C_id;
    SVN_ERR(svn_fs_node_id(&C_id, txn_root, "A/C", pool));
    SVN_ERR(check_entry_present(txn_root, "A", "C", pool));
    SVN_ERR(svn_fs_delete(txn_root, "A/C", pool));
    SVN_ERR(check_entry_absent(txn_root, "A", "C", pool));
  }

  /* 6 */
  {
    const svn_fs_id_t *root_id;
    SVN_ERR(svn_fs_node_id(&root_id, txn_root, "", pool));

    err = svn_fs_delete(txn_root, "", pool);

    if (err && (err->apr_err != SVN_ERR_FS_ROOT_DIR))
      {
        return svn_error_createf
          (SVN_ERR_FS_GENERAL, NULL,
           "deleting root directory got wrong error");
      }
    else if (! err)
      {
        return svn_error_createf
          (SVN_ERR_FS_GENERAL, NULL,
           "deleting root directory failed to get error");
      }
    svn_error_clear(err);

  }

  /* 7 */
  {
    const svn_fs_id_t *iota_id;
    SVN_ERR(svn_fs_node_id(&iota_id, txn_root, "iota", pool));
    SVN_ERR(check_entry_present(txn_root, "", "iota", pool));
    SVN_ERR(svn_fs_delete(txn_root, "iota", pool));
    SVN_ERR(check_entry_absent(txn_root, "", "iota", pool));
  }

  return SVN_NO_ERROR;
}


/* This tests deleting in general.
 *
 * ### todo: this test was written after (and independently of)
 * delete_mutables().  It might be worthwhile to combine them.
 */
static svn_error_t *
delete(const svn_test_opts_t *opts,
       apr_pool_t *pool)
{
  svn_fs_t *fs;
  svn_fs_txn_t *txn;
  svn_fs_root_t *txn_root;
  svn_revnum_t new_rev;

  /* This function tests 5 cases:
   *
   * 1. Delete mutable file.
   * 2. Delete mutable directory.
   * 3. Delete mutable directory with immutable nodes.
   * 4. Delete immutable file.
   * 5. Delete immutable directory.
   */

  /* Prepare a txn to receive the greek tree. */
  SVN_ERR(svn_test__create_fs(&fs, "test-repo-del-tree",
                              opts, pool));
  SVN_ERR(svn_fs_begin_txn(&txn, fs, 0, pool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, pool));

  /* Create the greek tree. */
  SVN_ERR(svn_test__create_greek_tree(txn_root, pool));

  /* 1. Delete mutable file. */
  {
    const svn_fs_id_t *iota_id, *gamma_id;
    static svn_test__tree_entry_t expected_entries[] = {
      /* path, contents (0 = dir) */
      { "A",           0 },
      { "A/mu",        "This is the file 'mu'.\n" },
      { "A/B",         0 },
      { "A/B/lambda",  "This is the file 'lambda'.\n" },
      { "A/B/E",       0 },
      { "A/B/E/alpha", "This is the file 'alpha'.\n" },
      { "A/B/E/beta",  "This is the file 'beta'.\n" },
      { "A/C",         0 },
      { "A/B/F",       0 },
      { "A/D",         0 },
      { "A/D/G",       0 },
      { "A/D/G/pi",    "This is the file 'pi'.\n" },
      { "A/D/G/rho",   "This is the file 'rho'.\n" },
      { "A/D/G/tau",   "This is the file 'tau'.\n" },
      { "A/D/H",       0 },
      { "A/D/H/chi",   "This is the file 'chi'.\n" },
      { "A/D/H/psi",   "This is the file 'psi'.\n" },
      { "A/D/H/omega", "This is the file 'omega'.\n" }
    };

    /* Check nodes revision ID is gone.  */
    SVN_ERR(svn_fs_node_id(&iota_id, txn_root, "iota", pool));
    SVN_ERR(svn_fs_node_id(&gamma_id, txn_root, "A/D/gamma", pool));

    SVN_ERR(check_entry_present(txn_root, "", "iota", pool));

    /* Try deleting mutable files. */
    SVN_ERR(svn_fs_delete(txn_root, "iota", pool));
    SVN_ERR(svn_fs_delete(txn_root, "A/D/gamma", pool));
    SVN_ERR(check_entry_absent(txn_root, "", "iota", pool));
    SVN_ERR(check_entry_absent(txn_root, "A/D", "gamma", pool));

    /* Validate the tree.  */
    SVN_ERR(svn_test__validate_tree(txn_root, expected_entries, 18, pool));
  }
  /* Abort transaction.  */
  SVN_ERR(svn_fs_abort_txn(txn, pool));

  /* 2. Delete mutable directory. */

  /* Prepare a txn to receive the greek tree. */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, 0, pool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, pool));

  /* Create the greek tree. */
  SVN_ERR(svn_test__create_greek_tree(txn_root, pool));

  {
    const svn_fs_id_t *A_id, *mu_id, *B_id, *lambda_id, *E_id, *alpha_id,
      *beta_id, *F_id, *C_id, *D_id, *gamma_id, *H_id, *chi_id,
      *psi_id, *omega_id, *G_id, *pi_id, *rho_id, *tau_id;

    /* Check nodes revision ID is gone.  */
    SVN_ERR(svn_fs_node_id(&A_id, txn_root, "/A", pool));
    SVN_ERR(check_entry_present(txn_root, "", "A", pool));
    SVN_ERR(svn_fs_node_id(&mu_id, txn_root, "/A/mu", pool));
    SVN_ERR(check_entry_present(txn_root, "A", "mu", pool));
    SVN_ERR(svn_fs_node_id(&B_id, txn_root, "/A/B", pool));
    SVN_ERR(check_entry_present(txn_root, "A", "B", pool));
    SVN_ERR(svn_fs_node_id(&lambda_id, txn_root, "/A/B/lambda", pool));
    SVN_ERR(check_entry_present(txn_root, "A/B", "lambda", pool));
    SVN_ERR(svn_fs_node_id(&E_id, txn_root, "/A/B/E", pool));
    SVN_ERR(check_entry_present(txn_root, "A/B", "E", pool));
    SVN_ERR(svn_fs_node_id(&alpha_id, txn_root, "/A/B/E/alpha", pool));
    SVN_ERR(check_entry_present(txn_root, "A/B/E", "alpha", pool));
    SVN_ERR(svn_fs_node_id(&beta_id, txn_root, "/A/B/E/beta", pool));
    SVN_ERR(check_entry_present(txn_root, "A/B/E", "beta", pool));
    SVN_ERR(svn_fs_node_id(&F_id, txn_root, "/A/B/F", pool));
    SVN_ERR(check_entry_present(txn_root, "A/B", "F", pool));
    SVN_ERR(svn_fs_node_id(&C_id, txn_root, "/A/C", pool));
    SVN_ERR(check_entry_present(txn_root, "A", "C", pool));
    SVN_ERR(svn_fs_node_id(&D_id, txn_root, "/A/D", pool));
    SVN_ERR(check_entry_present(txn_root, "A", "D", pool));
    SVN_ERR(svn_fs_node_id(&gamma_id, txn_root, "/A/D/gamma", pool));
    SVN_ERR(check_entry_present(txn_root, "A/D", "gamma", pool));
    SVN_ERR(svn_fs_node_id(&H_id, txn_root, "/A/D/H", pool));
    SVN_ERR(check_entry_present(txn_root, "A/D", "H", pool));
    SVN_ERR(svn_fs_node_id(&chi_id, txn_root, "/A/D/H/chi", pool));
    SVN_ERR(check_entry_present(txn_root, "A/D/H", "chi", pool));
    SVN_ERR(svn_fs_node_id(&psi_id, txn_root, "/A/D/H/psi", pool));
    SVN_ERR(check_entry_present(txn_root, "A/D/H", "psi", pool));
    SVN_ERR(svn_fs_node_id(&omega_id, txn_root, "/A/D/H/omega", pool));
    SVN_ERR(check_entry_present(txn_root, "A/D/H", "omega", pool));
    SVN_ERR(svn_fs_node_id(&G_id, txn_root, "/A/D/G", pool));
    SVN_ERR(check_entry_present(txn_root, "A/D", "G", pool));
    SVN_ERR(svn_fs_node_id(&pi_id, txn_root, "/A/D/G/pi", pool));
    SVN_ERR(check_entry_present(txn_root, "A/D/G", "pi", pool));
    SVN_ERR(svn_fs_node_id(&rho_id, txn_root, "/A/D/G/rho", pool));
    SVN_ERR(check_entry_present(txn_root, "A/D/G", "rho", pool));
    SVN_ERR(svn_fs_node_id(&tau_id, txn_root, "/A/D/G/tau", pool));
    SVN_ERR(check_entry_present(txn_root, "A/D/G", "tau", pool));

    /* Try deleting a mutable empty dir. */
    SVN_ERR(svn_fs_delete(txn_root, "A/C", pool));
    SVN_ERR(svn_fs_delete(txn_root, "A/B/F", pool));
    SVN_ERR(check_entry_absent(txn_root, "A", "C", pool));
    SVN_ERR(check_entry_absent(txn_root, "A/B", "F", pool));

    /* Now delete a mutable non-empty dir. */
    SVN_ERR(svn_fs_delete(txn_root, "A", pool));
    SVN_ERR(check_entry_absent(txn_root, "", "A", pool));

    /* Validate the tree.  */
    {
      static svn_test__tree_entry_t expected_entries[] = {
        /* path, contents (0 = dir) */
        { "iota",        "This is the file 'iota'.\n" } };
      SVN_ERR(svn_test__validate_tree(txn_root, expected_entries, 1, pool));
    }
  }

  /* Abort transaction.  */
  SVN_ERR(svn_fs_abort_txn(txn, pool));

  /* 3. Delete mutable directory with immutable nodes. */

  /* Prepare a txn to receive the greek tree. */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, 0, pool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, pool));

  /* Create the greek tree. */
  SVN_ERR(svn_test__create_greek_tree(txn_root, pool));

  /* Commit the greek tree. */
  SVN_ERR(svn_fs_commit_txn(NULL, &new_rev, txn, pool));
  SVN_TEST_ASSERT(SVN_IS_VALID_REVNUM(new_rev));

  /* Create new transaction. */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, new_rev, pool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, pool));

  {
    const svn_fs_id_t *A_id, *mu_id, *B_id, *lambda_id, *E_id, *alpha_id,
      *beta_id, *F_id, *C_id, *D_id, *gamma_id, *H_id, *chi_id,
      *psi_id, *omega_id, *G_id, *pi_id, *rho_id, *tau_id, *sigma_id;

    /* Create A/D/G/sigma.  This makes all components of A/D/G
       mutable.  */
    SVN_ERR(svn_fs_make_file(txn_root, "A/D/G/sigma", pool));
    SVN_ERR(svn_test__set_file_contents(txn_root, "A/D/G/sigma",
                                        "This is another file 'sigma'.\n", pool));

    /* Check that mutable node-revision-IDs are removed and immutable
       ones still exist.  */
    SVN_ERR(svn_fs_node_id(&A_id, txn_root, "/A", pool));
    SVN_ERR(check_entry_present(txn_root, "", "A", pool));
    SVN_ERR(svn_fs_node_id(&mu_id, txn_root, "/A/mu", pool));
    SVN_ERR(check_entry_present(txn_root, "A", "mu", pool));
    SVN_ERR(svn_fs_node_id(&B_id, txn_root, "/A/B", pool));
    SVN_ERR(check_entry_present(txn_root, "A", "B", pool));
    SVN_ERR(svn_fs_node_id(&lambda_id, txn_root, "/A/B/lambda", pool));
    SVN_ERR(check_entry_present(txn_root, "A/B", "lambda", pool));
    SVN_ERR(svn_fs_node_id(&E_id, txn_root, "/A/B/E", pool));
    SVN_ERR(check_entry_present(txn_root, "A/B", "E", pool));
    SVN_ERR(svn_fs_node_id(&alpha_id, txn_root, "/A/B/E/alpha", pool));
    SVN_ERR(check_entry_present(txn_root, "A/B/E", "alpha", pool));
    SVN_ERR(svn_fs_node_id(&beta_id, txn_root, "/A/B/E/beta", pool));
    SVN_ERR(check_entry_present(txn_root, "A/B/E", "beta", pool));
    SVN_ERR(svn_fs_node_id(&F_id, txn_root, "/A/B/F", pool));
    SVN_ERR(check_entry_present(txn_root, "A/B", "F", pool));
    SVN_ERR(svn_fs_node_id(&C_id, txn_root, "/A/C", pool));
    SVN_ERR(check_entry_present(txn_root, "A", "C", pool));
    SVN_ERR(svn_fs_node_id(&D_id, txn_root, "/A/D", pool));
    SVN_ERR(check_entry_present(txn_root, "A", "D", pool));
    SVN_ERR(svn_fs_node_id(&gamma_id, txn_root, "/A/D/gamma", pool));
    SVN_ERR(check_entry_present(txn_root, "A/D", "gamma", pool));
    SVN_ERR(svn_fs_node_id(&H_id, txn_root, "/A/D/H", pool));
    SVN_ERR(check_entry_present(txn_root, "A/D", "H", pool));
    SVN_ERR(svn_fs_node_id(&chi_id, txn_root, "/A/D/H/chi", pool));
    SVN_ERR(check_entry_present(txn_root, "A/D/H", "chi", pool));
    SVN_ERR(svn_fs_node_id(&psi_id, txn_root, "/A/D/H/psi", pool));
    SVN_ERR(check_entry_present(txn_root, "A/D/H", "psi", pool));
    SVN_ERR(svn_fs_node_id(&omega_id, txn_root, "/A/D/H/omega", pool));
    SVN_ERR(check_entry_present(txn_root, "A/D/H", "omega", pool));
    SVN_ERR(svn_fs_node_id(&G_id, txn_root, "/A/D/G", pool));
    SVN_ERR(check_entry_present(txn_root, "A/D", "G", pool));
    SVN_ERR(svn_fs_node_id(&pi_id, txn_root, "/A/D/G/pi", pool));
    SVN_ERR(check_entry_present(txn_root, "A/D/G", "pi", pool));
    SVN_ERR(svn_fs_node_id(&rho_id, txn_root, "/A/D/G/rho", pool));
    SVN_ERR(check_entry_present(txn_root, "A/D/G", "rho", pool));
    SVN_ERR(svn_fs_node_id(&tau_id, txn_root, "/A/D/G/tau", pool));
    SVN_ERR(check_entry_present(txn_root, "A/D/G", "tau", pool));
    SVN_ERR(svn_fs_node_id(&sigma_id, txn_root, "/A/D/G/sigma", pool));
    SVN_ERR(check_entry_present(txn_root, "A/D/G", "sigma", pool));

    /* Delete "A" */
    SVN_ERR(svn_fs_delete(txn_root, "A", pool));
    SVN_ERR(check_entry_absent(txn_root, "", "A", pool));

    /* Validate the tree.  */
    {
      static svn_test__tree_entry_t expected_entries[] = {
        /* path, contents (0 = dir) */
        { "iota",        "This is the file 'iota'.\n" }
      };

      SVN_ERR(svn_test__validate_tree(txn_root, expected_entries, 1, pool));
    }
  }

  /* Abort transaction.  */
  SVN_ERR(svn_fs_abort_txn(txn, pool));

  /* 4. Delete immutable file. */

  /* Create new transaction. */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, new_rev, pool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, pool));

  {
    const svn_fs_id_t *iota_id, *gamma_id;

    /* Check nodes revision ID is present.  */
    SVN_ERR(svn_fs_node_id(&iota_id, txn_root, "iota", pool));
    SVN_ERR(svn_fs_node_id(&gamma_id, txn_root, "A/D/gamma", pool));
    SVN_ERR(check_entry_present(txn_root, "", "iota", pool));
    SVN_ERR(check_entry_present(txn_root, "A/D", "gamma", pool));

    /* Delete some files. */
    SVN_ERR(svn_fs_delete(txn_root, "iota", pool));
    SVN_ERR(svn_fs_delete(txn_root, "A/D/gamma", pool));
    SVN_ERR(check_entry_absent(txn_root, "", "iota", pool));
    SVN_ERR(check_entry_absent(txn_root, "A/D", "iota", pool));

    /* Validate the tree.  */
    {
      static svn_test__tree_entry_t expected_entries[] = {
        /* path, contents (0 = dir) */
        { "A",           0 },
        { "A/mu",        "This is the file 'mu'.\n" },
        { "A/B",         0 },
        { "A/B/lambda",  "This is the file 'lambda'.\n" },
        { "A/B/E",       0 },
        { "A/B/E/alpha", "This is the file 'alpha'.\n" },
        { "A/B/E/beta",  "This is the file 'beta'.\n" },
        { "A/B/F",       0 },
        { "A/C",         0 },
        { "A/D",         0 },
        { "A/D/G",       0 },
        { "A/D/G/pi",    "This is the file 'pi'.\n" },
        { "A/D/G/rho",   "This is the file 'rho'.\n" },
        { "A/D/G/tau",   "This is the file 'tau'.\n" },
        { "A/D/H",       0 },
        { "A/D/H/chi",   "This is the file 'chi'.\n" },
        { "A/D/H/psi",   "This is the file 'psi'.\n" },
        { "A/D/H/omega", "This is the file 'omega'.\n" }
      };
      SVN_ERR(svn_test__validate_tree(txn_root, expected_entries, 18, pool));
    }
  }

  /* Abort transaction.  */
  SVN_ERR(svn_fs_abort_txn(txn, pool));

  /* 5. Delete immutable directory. */

  /* Create new transaction. */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, new_rev, pool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, pool));

  {
    const svn_fs_id_t *A_id, *mu_id, *B_id, *lambda_id, *E_id, *alpha_id,
      *beta_id, *F_id, *C_id, *D_id, *gamma_id, *H_id, *chi_id,
      *psi_id, *omega_id, *G_id, *pi_id, *rho_id, *tau_id;

    /* Check nodes revision ID is present.  */
    SVN_ERR(svn_fs_node_id(&A_id, txn_root, "/A", pool));
    SVN_ERR(check_entry_present(txn_root, "", "A", pool));
    SVN_ERR(svn_fs_node_id(&mu_id, txn_root, "/A/mu", pool));
    SVN_ERR(check_entry_present(txn_root, "A", "mu", pool));
    SVN_ERR(svn_fs_node_id(&B_id, txn_root, "/A/B", pool));
    SVN_ERR(check_entry_present(txn_root, "A", "B", pool));
    SVN_ERR(svn_fs_node_id(&lambda_id, txn_root, "/A/B/lambda", pool));
    SVN_ERR(check_entry_present(txn_root, "A/B", "lambda", pool));
    SVN_ERR(svn_fs_node_id(&E_id, txn_root, "/A/B/E", pool));
    SVN_ERR(check_entry_present(txn_root, "A/B", "E", pool));
    SVN_ERR(svn_fs_node_id(&alpha_id, txn_root, "/A/B/E/alpha", pool));
    SVN_ERR(check_entry_present(txn_root, "A/B/E", "alpha", pool));
    SVN_ERR(svn_fs_node_id(&beta_id, txn_root, "/A/B/E/beta", pool));
    SVN_ERR(check_entry_present(txn_root, "A/B/E", "beta", pool));
    SVN_ERR(svn_fs_node_id(&F_id, txn_root, "/A/B/F", pool));
    SVN_ERR(check_entry_present(txn_root, "A/B", "F", pool));
    SVN_ERR(svn_fs_node_id(&C_id, txn_root, "/A/C", pool));
    SVN_ERR(check_entry_present(txn_root, "A", "C", pool));
    SVN_ERR(svn_fs_node_id(&D_id, txn_root, "/A/D", pool));
    SVN_ERR(check_entry_present(txn_root, "A", "D", pool));
    SVN_ERR(svn_fs_node_id(&gamma_id, txn_root, "/A/D/gamma", pool));
    SVN_ERR(check_entry_present(txn_root, "A/D", "gamma", pool));
    SVN_ERR(svn_fs_node_id(&H_id, txn_root, "/A/D/H", pool));
    SVN_ERR(check_entry_present(txn_root, "A/D", "H", pool));
    SVN_ERR(svn_fs_node_id(&chi_id, txn_root, "/A/D/H/chi", pool));
    SVN_ERR(check_entry_present(txn_root, "A/D/H", "chi", pool));
    SVN_ERR(svn_fs_node_id(&psi_id, txn_root, "/A/D/H/psi", pool));
    SVN_ERR(check_entry_present(txn_root, "A/D/H", "psi", pool));
    SVN_ERR(svn_fs_node_id(&omega_id, txn_root, "/A/D/H/omega", pool));
    SVN_ERR(check_entry_present(txn_root, "A/D/H", "omega", pool));
    SVN_ERR(svn_fs_node_id(&G_id, txn_root, "/A/D/G", pool));
    SVN_ERR(check_entry_present(txn_root, "A/D", "G", pool));
    SVN_ERR(svn_fs_node_id(&pi_id, txn_root, "/A/D/G/pi", pool));
    SVN_ERR(check_entry_present(txn_root, "A/D/G", "pi", pool));
    SVN_ERR(svn_fs_node_id(&rho_id, txn_root, "/A/D/G/rho", pool));
    SVN_ERR(check_entry_present(txn_root, "A/D/G", "rho", pool));
    SVN_ERR(svn_fs_node_id(&tau_id, txn_root, "/A/D/G/tau", pool));
    SVN_ERR(check_entry_present(txn_root, "A/D/G", "tau", pool));

    /* Delete "A" */
    SVN_ERR(svn_fs_delete(txn_root, "A", pool));
    SVN_ERR(check_entry_absent(txn_root, "", "A", pool));

    /* Validate the tree.  */
    {
      static svn_test__tree_entry_t expected_entries[] = {
        /* path, contents (0 = dir) */
        { "iota",        "This is the file 'iota'.\n" }
      };
      SVN_ERR(svn_test__validate_tree(txn_root, expected_entries, 1, pool));
    }
  }

  return SVN_NO_ERROR;
}



/* Test the datestamps on commits. */
static svn_error_t *
commit_date(const svn_test_opts_t *opts,
            apr_pool_t *pool)
{
  svn_fs_t *fs;
  svn_fs_txn_t *txn;
  svn_fs_root_t *txn_root;
  svn_revnum_t rev;
  svn_string_t *datestamp;
  apr_time_t before_commit, at_commit, after_commit;

  /* Prepare a filesystem. */
  SVN_ERR(svn_test__create_fs(&fs, "test-repo-commit-date",
                              opts, pool));

  before_commit = apr_time_now();

  /* Commit a greek tree. */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, 0, pool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, pool));
  SVN_ERR(svn_test__create_greek_tree(txn_root, pool));
  SVN_ERR(svn_fs_commit_txn(NULL, &rev, txn, pool));
  SVN_TEST_ASSERT(SVN_IS_VALID_REVNUM(rev));

  after_commit = apr_time_now();

  /* Get the datestamp of the commit. */
  SVN_ERR(svn_fs_revision_prop(&datestamp, fs, rev, SVN_PROP_REVISION_DATE,
                               pool));

  if (datestamp == NULL)
    return svn_error_create
      (SVN_ERR_FS_GENERAL, NULL,
       "failed to get datestamp of committed revision");

  SVN_ERR(svn_time_from_cstring(&at_commit, datestamp->data, pool));

  if (at_commit < before_commit)
    return svn_error_create
      (SVN_ERR_FS_GENERAL, NULL,
       "datestamp too early");

  if (at_commit > after_commit)
    return svn_error_create
      (SVN_ERR_FS_GENERAL, NULL,
       "datestamp too late");

  return SVN_NO_ERROR;
}


static svn_error_t *
check_old_revisions(const svn_test_opts_t *opts,
                    apr_pool_t *pool)
{
  svn_fs_t *fs;
  svn_fs_txn_t *txn;
  svn_fs_root_t *txn_root;
  svn_revnum_t rev;
  apr_pool_t *subpool = svn_pool_create(pool);

  /* Prepare a filesystem. */
  SVN_ERR(svn_test__create_fs(&fs, "test-repo-check-old-revisions",
                              opts, pool));

  /* Commit a greek tree. */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, 0, subpool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, subpool));
  SVN_ERR(svn_test__create_greek_tree(txn_root, subpool));
  SVN_ERR(svn_fs_commit_txn(NULL, &rev, txn, subpool));
  SVN_TEST_ASSERT(SVN_IS_VALID_REVNUM(rev));
  svn_pool_clear(subpool);

  /* Modify and commit iota a few times, then test to see if we can
     retrieve all the committed revisions. */
  {
    /* right-side numbers match revision numbers */
#define iota_contents_1 "This is the file 'iota'.\n"

    /* Add a char to the front. */
#define iota_contents_2 "XThis is the file 'iota'.\n"

    /* Add a char to the end. */
#define iota_contents_3 "XThis is the file 'iota'.\nX"

    /* Add a couple of chars in the middle. */
#define iota_contents_4 "XThis is the X file 'iota'.\nX"

    /* Randomly add and delete chars all over. */
#define iota_contents_5 \
    "XTYhQis is ACK, PHHHT! no longer 'ioZZZZZta'.blarf\nbye"

    /* Reassure iota that it will live for quite some time. */
#define iota_contents_6 "Matthew 5:18 (Revised Standard Version) --\n\
For truly, I say to you, till heaven and earth pass away, not an iota,\n\
not a dot, will pass from the law until all is accomplished."

    /* Revert to the original contents. */
#define iota_contents_7 "This is the file 'iota'.\n"

    /* Revision 2. */
    SVN_ERR(svn_fs_begin_txn(&txn, fs, rev, subpool));
    SVN_ERR(svn_fs_txn_root(&txn_root, txn, subpool));
    SVN_ERR(svn_test__set_file_contents
            (txn_root, "iota", iota_contents_2, subpool));
    SVN_ERR(svn_fs_commit_txn(NULL, &rev, txn, subpool));
    SVN_TEST_ASSERT(SVN_IS_VALID_REVNUM(rev));
    svn_pool_clear(subpool);

    /* Revision 3. */
    SVN_ERR(svn_fs_begin_txn(&txn, fs, rev, subpool));
    SVN_ERR(svn_fs_txn_root(&txn_root, txn, subpool));
    SVN_ERR(svn_test__set_file_contents
            (txn_root, "iota", iota_contents_3, subpool));
    SVN_ERR(svn_fs_commit_txn(NULL, &rev, txn, subpool));
    SVN_TEST_ASSERT(SVN_IS_VALID_REVNUM(rev));
    svn_pool_clear(subpool);

    /* Revision 4. */
    SVN_ERR(svn_fs_begin_txn(&txn, fs, rev, subpool));
    SVN_ERR(svn_fs_txn_root(&txn_root, txn, subpool));
    SVN_ERR(svn_test__set_file_contents
            (txn_root, "iota", iota_contents_4, subpool));
    SVN_ERR(svn_fs_commit_txn(NULL, &rev, txn, subpool));
    SVN_TEST_ASSERT(SVN_IS_VALID_REVNUM(rev));
    svn_pool_clear(subpool);

    /* Revision 5. */
    SVN_ERR(svn_fs_begin_txn(&txn, fs, rev, subpool));
    SVN_ERR(svn_fs_txn_root(&txn_root, txn, subpool));
    SVN_ERR(svn_test__set_file_contents
            (txn_root, "iota", iota_contents_5, subpool));
    SVN_ERR(svn_fs_commit_txn(NULL, &rev, txn, subpool));
    SVN_TEST_ASSERT(SVN_IS_VALID_REVNUM(rev));
    svn_pool_clear(subpool);

    /* Revision 6. */
    SVN_ERR(svn_fs_begin_txn(&txn, fs, rev, subpool));
    SVN_ERR(svn_fs_txn_root(&txn_root, txn, subpool));
    SVN_ERR(svn_test__set_file_contents
            (txn_root, "iota", iota_contents_6, subpool));
    SVN_ERR(svn_fs_commit_txn(NULL, &rev, txn, subpool));
    SVN_TEST_ASSERT(SVN_IS_VALID_REVNUM(rev));
    svn_pool_clear(subpool);

    /* Revision 7. */
    SVN_ERR(svn_fs_begin_txn(&txn, fs, rev, subpool));
    SVN_ERR(svn_fs_txn_root(&txn_root, txn, subpool));
    SVN_ERR(svn_test__set_file_contents
            (txn_root, "iota", iota_contents_7, subpool));
    SVN_ERR(svn_fs_commit_txn(NULL, &rev, txn, subpool));
    SVN_TEST_ASSERT(SVN_IS_VALID_REVNUM(rev));
    svn_pool_clear(subpool);

    /** Now check the full Greek Tree in all of those revisions,
        adjusting `iota' for each one. ***/

    /* Validate revision 1.  */
    {
      svn_fs_root_t *root;
      static svn_test__tree_entry_t expected_entries[] = {
        /* path, contents (0 = dir) */
        { "iota",        iota_contents_1 },
        { "A",           0 },
        { "A/mu",        "This is the file 'mu'.\n" },
        { "A/B",         0 },
        { "A/B/lambda",  "This is the file 'lambda'.\n" },
        { "A/B/E",       0 },
        { "A/B/E/alpha", "This is the file 'alpha'.\n" },
        { "A/B/E/beta",  "This is the file 'beta'.\n" },
        { "A/B/F",       0 },
        { "A/C",         0 },
        { "A/D",         0 },
        { "A/D/gamma",   "This is the file 'gamma'.\n" },
        { "A/D/G",       0 },
        { "A/D/G/pi",    "This is the file 'pi'.\n" },
        { "A/D/G/rho",   "This is the file 'rho'.\n" },
        { "A/D/G/tau",   "This is the file 'tau'.\n" },
        { "A/D/H",       0 },
        { "A/D/H/chi",   "This is the file 'chi'.\n" },
        { "A/D/H/psi",   "This is the file 'psi'.\n" },
        { "A/D/H/omega", "This is the file 'omega'.\n" }
      };

      SVN_ERR(svn_fs_revision_root(&root, fs, 1, pool));
      SVN_ERR(svn_test__validate_tree(root, expected_entries, 20, pool));
    }

    /* Validate revision 2.  */
    {
      svn_fs_root_t *root;
      static svn_test__tree_entry_t expected_entries[] = {
        /* path, contents (0 = dir) */
        { "iota",        iota_contents_2 },
        { "A",           0 },
        { "A/mu",        "This is the file 'mu'.\n" },
        { "A/B",         0 },
        { "A/B/lambda",  "This is the file 'lambda'.\n" },
        { "A/B/E",       0 },
        { "A/B/E/alpha", "This is the file 'alpha'.\n" },
        { "A/B/E/beta",  "This is the file 'beta'.\n" },
        { "A/B/F",       0 },
        { "A/C",         0 },
        { "A/D",         0 },
        { "A/D/gamma",   "This is the file 'gamma'.\n" },
        { "A/D/G",       0 },
        { "A/D/G/pi",    "This is the file 'pi'.\n" },
        { "A/D/G/rho",   "This is the file 'rho'.\n" },
        { "A/D/G/tau",   "This is the file 'tau'.\n" },
        { "A/D/H",       0 },
        { "A/D/H/chi",   "This is the file 'chi'.\n" },
        { "A/D/H/psi",   "This is the file 'psi'.\n" },
        { "A/D/H/omega", "This is the file 'omega'.\n" }
      };

      SVN_ERR(svn_fs_revision_root(&root, fs, 2, pool));
      SVN_ERR(svn_test__validate_tree(root, expected_entries, 20, pool));
    }

    /* Validate revision 3.  */
    {
      svn_fs_root_t *root;
      static svn_test__tree_entry_t expected_entries[] = {
        /* path, contents (0 = dir) */
        { "iota",        iota_contents_3 },
        { "A",           0 },
        { "A/mu",        "This is the file 'mu'.\n" },
        { "A/B",         0 },
        { "A/B/lambda",  "This is the file 'lambda'.\n" },
        { "A/B/E",       0 },
        { "A/B/E/alpha", "This is the file 'alpha'.\n" },
        { "A/B/E/beta",  "This is the file 'beta'.\n" },
        { "A/B/F",       0 },
        { "A/C",         0 },
        { "A/D",         0 },
        { "A/D/gamma",   "This is the file 'gamma'.\n" },
        { "A/D/G",       0 },
        { "A/D/G/pi",    "This is the file 'pi'.\n" },
        { "A/D/G/rho",   "This is the file 'rho'.\n" },
        { "A/D/G/tau",   "This is the file 'tau'.\n" },
        { "A/D/H",       0 },
        { "A/D/H/chi",   "This is the file 'chi'.\n" },
        { "A/D/H/psi",   "This is the file 'psi'.\n" },
        { "A/D/H/omega", "This is the file 'omega'.\n" }
      };

      SVN_ERR(svn_fs_revision_root(&root, fs, 3, pool));
      SVN_ERR(svn_test__validate_tree(root, expected_entries, 20, pool));
    }

    /* Validate revision 4.  */
    {
      svn_fs_root_t *root;
      static svn_test__tree_entry_t expected_entries[] = {
        /* path, contents (0 = dir) */
        { "iota",        iota_contents_4 },
        { "A",           0 },
        { "A/mu",        "This is the file 'mu'.\n" },
        { "A/B",         0 },
        { "A/B/lambda",  "This is the file 'lambda'.\n" },
        { "A/B/E",       0 },
        { "A/B/E/alpha", "This is the file 'alpha'.\n" },
        { "A/B/E/beta",  "This is the file 'beta'.\n" },
        { "A/B/F",       0 },
        { "A/C",         0 },
        { "A/D",         0 },
        { "A/D/gamma",   "This is the file 'gamma'.\n" },
        { "A/D/G",       0 },
        { "A/D/G/pi",    "This is the file 'pi'.\n" },
        { "A/D/G/rho",   "This is the file 'rho'.\n" },
        { "A/D/G/tau",   "This is the file 'tau'.\n" },
        { "A/D/H",       0 },
        { "A/D/H/chi",   "This is the file 'chi'.\n" },
        { "A/D/H/psi",   "This is the file 'psi'.\n" },
        { "A/D/H/omega", "This is the file 'omega'.\n" }
      };

      SVN_ERR(svn_fs_revision_root(&root, fs, 4, pool));
      SVN_ERR(svn_test__validate_tree(root, expected_entries, 20, pool));
    }

    /* Validate revision 5.  */
    {
      svn_fs_root_t *root;
      static svn_test__tree_entry_t expected_entries[] = {
        /* path, contents (0 = dir) */
        { "iota",        iota_contents_5 },
        { "A",           0 },
        { "A/mu",        "This is the file 'mu'.\n" },
        { "A/B",         0 },
        { "A/B/lambda",  "This is the file 'lambda'.\n" },
        { "A/B/E",       0 },
        { "A/B/E/alpha", "This is the file 'alpha'.\n" },
        { "A/B/E/beta",  "This is the file 'beta'.\n" },
        { "A/B/F",       0 },
        { "A/C",         0 },
        { "A/D",         0 },
        { "A/D/G",       0 },
        { "A/D/gamma",   "This is the file 'gamma'.\n" },
        { "A/D/G/pi",    "This is the file 'pi'.\n" },
        { "A/D/G/rho",   "This is the file 'rho'.\n" },
        { "A/D/G/tau",   "This is the file 'tau'.\n" },
        { "A/D/H",       0 },
        { "A/D/H/chi",   "This is the file 'chi'.\n" },
        { "A/D/H/psi",   "This is the file 'psi'.\n" },
        { "A/D/H/omega", "This is the file 'omega'.\n" }
      };

      SVN_ERR(svn_fs_revision_root(&root, fs, 5, pool));
      SVN_ERR(svn_test__validate_tree(root, expected_entries, 20, pool));
    }

    /* Validate revision 6.  */
    {
      svn_fs_root_t *root;
      static svn_test__tree_entry_t expected_entries[] = {
        /* path, contents (0 = dir) */
        { "iota",        iota_contents_6 },
        { "A",           0 },
        { "A/mu",        "This is the file 'mu'.\n" },
        { "A/B",         0 },
        { "A/B/lambda",  "This is the file 'lambda'.\n" },
        { "A/B/E",       0 },
        { "A/B/E/alpha", "This is the file 'alpha'.\n" },
        { "A/B/E/beta",  "This is the file 'beta'.\n" },
        { "A/B/F",       0 },
        { "A/C",         0 },
        { "A/D",         0 },
        { "A/D/gamma",   "This is the file 'gamma'.\n" },
        { "A/D/G",       0 },
        { "A/D/G/pi",    "This is the file 'pi'.\n" },
        { "A/D/G/rho",   "This is the file 'rho'.\n" },
        { "A/D/G/tau",   "This is the file 'tau'.\n" },
        { "A/D/H",       0 },
        { "A/D/H/chi",   "This is the file 'chi'.\n" },
        { "A/D/H/psi",   "This is the file 'psi'.\n" },
        { "A/D/H/omega", "This is the file 'omega'.\n" }
      };

      SVN_ERR(svn_fs_revision_root(&root, fs, 6, pool));
      SVN_ERR(svn_test__validate_tree(root, expected_entries, 20, pool));
    }

    /* Validate revision 7.  */
    {
      svn_fs_root_t *root;
      static svn_test__tree_entry_t expected_entries[] = {
        /* path, contents (0 = dir) */
        { "iota",        iota_contents_7 },
        { "A",           0 },
        { "A/mu",        "This is the file 'mu'.\n" },
        { "A/B",         0 },
        { "A/B/lambda",  "This is the file 'lambda'.\n" },
        { "A/B/E",       0 },
        { "A/B/E/alpha", "This is the file 'alpha'.\n" },
        { "A/B/E/beta",  "This is the file 'beta'.\n" },
        { "A/B/F",       0 },
        { "A/C",         0 },
        { "A/D",         0 },
        { "A/D/gamma",   "This is the file 'gamma'.\n" },
        { "A/D/G",       0 },
        { "A/D/G/pi",    "This is the file 'pi'.\n" },
        { "A/D/G/rho",   "This is the file 'rho'.\n" },
        { "A/D/G/tau",   "This is the file 'tau'.\n" },
        { "A/D/H",       0 },
        { "A/D/H/chi",   "This is the file 'chi'.\n" },
        { "A/D/H/psi",   "This is the file 'psi'.\n" },
        { "A/D/H/omega", "This is the file 'omega'.\n" }
      };

      SVN_ERR(svn_fs_revision_root(&root, fs, 7, pool));
      SVN_ERR(svn_test__validate_tree(root, expected_entries, 20, pool));
    }
  }

  svn_pool_destroy(subpool);
  return SVN_NO_ERROR;
}


/* For each revision R in FS, from 0 to MAX_REV, check that it
   matches the tree in EXPECTED_TREES[R].  Use POOL for any
   allocations.  This is a helper function for check_all_revisions. */
static svn_error_t *
validate_revisions(svn_fs_t *fs,
                   svn_test__tree_t *expected_trees,
                   svn_revnum_t max_rev,
                   apr_pool_t *pool)
{
  svn_fs_root_t *revision_root;
  svn_revnum_t i;
  svn_error_t *err;
  apr_pool_t *subpool = svn_pool_create(pool);

  /* Validate all revisions up to the current one. */
  for (i = 0; i <= max_rev; i++)
    {
      SVN_ERR(svn_fs_revision_root(&revision_root, fs,
                                   (svn_revnum_t)i, subpool));
      err = svn_test__validate_tree(revision_root,
                                    expected_trees[i].entries,
                                    expected_trees[i].num_entries,
                                    subpool);
      if (err)
        return svn_error_createf
          (SVN_ERR_FS_GENERAL, err,
           "Error validating revision %ld (youngest is %ld)", i, max_rev);
      svn_pool_clear(subpool);
    }

  svn_pool_destroy(subpool);
  return SVN_NO_ERROR;
}


static svn_error_t *
check_all_revisions(const svn_test_opts_t *opts,
                    apr_pool_t *pool)
{
  svn_fs_t *fs;
  svn_fs_txn_t *txn;
  svn_fs_root_t *txn_root;
  svn_revnum_t youngest_rev;
  svn_test__tree_t expected_trees[5]; /* one tree per commit, please */
  svn_revnum_t revision_count = 0;
  apr_pool_t *subpool = svn_pool_create(pool);

  /* Create a filesystem and repository. */
  SVN_ERR(svn_test__create_fs(&fs, "test-repo-check-all-revisions",
                              opts, pool));

  /***********************************************************************/
  /* REVISION 0 */
  /***********************************************************************/
  {
    expected_trees[revision_count].num_entries = 0;
    expected_trees[revision_count].entries = 0;
    SVN_ERR(validate_revisions(fs, expected_trees, revision_count, subpool));
    revision_count++;
  }
  svn_pool_clear(subpool);

  /* Create and commit the greek tree. */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, 0, subpool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, subpool));
  SVN_ERR(svn_test__create_greek_tree(txn_root, subpool));
  SVN_ERR(svn_fs_commit_txn(NULL, &youngest_rev, txn, subpool));
  SVN_TEST_ASSERT(SVN_IS_VALID_REVNUM(youngest_rev));

  /***********************************************************************/
  /* REVISION 1 */
  /***********************************************************************/
  {
    static svn_test__tree_entry_t expected_entries[] = {
      /* path, contents (0 = dir) */
      { "iota",        "This is the file 'iota'.\n" },
      { "A",           0 },
      { "A/mu",        "This is the file 'mu'.\n" },
      { "A/B",         0 },
      { "A/B/lambda",  "This is the file 'lambda'.\n" },
      { "A/B/E",       0 },
      { "A/B/E/alpha", "This is the file 'alpha'.\n" },
      { "A/B/E/beta",  "This is the file 'beta'.\n" },
      { "A/B/F",       0 },
      { "A/C",         0 },
      { "A/D",         0 },
      { "A/D/gamma",   "This is the file 'gamma'.\n" },
      { "A/D/G",       0 },
      { "A/D/G/pi",    "This is the file 'pi'.\n" },
      { "A/D/G/rho",   "This is the file 'rho'.\n" },
      { "A/D/G/tau",   "This is the file 'tau'.\n" },
      { "A/D/H",       0 },
      { "A/D/H/chi",   "This is the file 'chi'.\n" },
      { "A/D/H/psi",   "This is the file 'psi'.\n" },
      { "A/D/H/omega", "This is the file 'omega'.\n" }
    };
    expected_trees[revision_count].entries = expected_entries;
    expected_trees[revision_count].num_entries = 20;
    SVN_ERR(validate_revisions(fs, expected_trees, revision_count, subpool));
    revision_count++;
  }
  svn_pool_clear(subpool);

  /* Make a new txn based on the youngest revision, make some changes,
     and commit those changes (which makes a new youngest
     revision). */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, youngest_rev, subpool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, subpool));
  {
    static svn_test__txn_script_command_t script_entries[] = {
      { 'a', "A/delta",     "This is the file 'delta'.\n" },
      { 'a', "A/epsilon",   "This is the file 'epsilon'.\n" },
      { 'a', "A/B/Z",       0 },
      { 'a', "A/B/Z/zeta",  "This is the file 'zeta'.\n" },
      { 'd', "A/C",         0 },
      { 'd', "A/mu",        "" },
      { 'd', "A/D/G/tau",   "" },
      { 'd', "A/D/H/omega", "" },
      { 'e', "iota",        "Changed file 'iota'.\n" },
      { 'e', "A/D/G/rho",   "Changed file 'rho'.\n" }
    };
    SVN_ERR(svn_test__txn_script_exec(txn_root, script_entries, 10,
                                      subpool));
  }
  SVN_ERR(svn_fs_commit_txn(NULL, &youngest_rev, txn, subpool));
  SVN_TEST_ASSERT(SVN_IS_VALID_REVNUM(youngest_rev));

  /***********************************************************************/
  /* REVISION 2 */
  /***********************************************************************/
  {
    static svn_test__tree_entry_t expected_entries[] = {
      /* path, contents (0 = dir) */
      { "iota",        "Changed file 'iota'.\n" },
      { "A",           0 },
      { "A/delta",     "This is the file 'delta'.\n" },
      { "A/epsilon",   "This is the file 'epsilon'.\n" },
      { "A/B",         0 },
      { "A/B/lambda",  "This is the file 'lambda'.\n" },
      { "A/B/E",       0 },
      { "A/B/E/alpha", "This is the file 'alpha'.\n" },
      { "A/B/E/beta",  "This is the file 'beta'.\n" },
      { "A/B/F",       0 },
      { "A/B/Z",       0 },
      { "A/B/Z/zeta",  "This is the file 'zeta'.\n" },
      { "A/D",         0 },
      { "A/D/gamma",   "This is the file 'gamma'.\n" },
      { "A/D/G",       0 },
      { "A/D/G/pi",    "This is the file 'pi'.\n" },
      { "A/D/G/rho",   "Changed file 'rho'.\n" },
      { "A/D/H",       0 },
      { "A/D/H/chi",   "This is the file 'chi'.\n" },
      { "A/D/H/psi",   "This is the file 'psi'.\n" }
    };
    expected_trees[revision_count].entries = expected_entries;
    expected_trees[revision_count].num_entries = 20;
    SVN_ERR(validate_revisions(fs, expected_trees, revision_count, subpool));
    revision_count++;
  }
  svn_pool_clear(subpool);

  /* Make a new txn based on the youngest revision, make some changes,
     and commit those changes (which makes a new youngest
     revision). */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, youngest_rev, subpool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, subpool));
  {
    static svn_test__txn_script_command_t script_entries[] = {
      { 'a', "A/mu",        "Re-added file 'mu'.\n" },
      { 'a', "A/D/H/omega", 0 }, /* re-add omega as directory! */
      { 'd', "iota",        "" },
      { 'e', "A/delta",     "This is the file 'delta'.\nLine 2.\n" }
    };
    SVN_ERR(svn_test__txn_script_exec(txn_root, script_entries, 4,
                                      subpool));
  }
  SVN_ERR(svn_fs_commit_txn(NULL, &youngest_rev, txn, subpool));
  SVN_TEST_ASSERT(SVN_IS_VALID_REVNUM(youngest_rev));

  /***********************************************************************/
  /* REVISION 3 */
  /***********************************************************************/
  {
    static svn_test__tree_entry_t expected_entries[] = {
      /* path, contents (0 = dir) */
      { "A",           0 },
      { "A/delta",     "This is the file 'delta'.\nLine 2.\n" },
      { "A/epsilon",   "This is the file 'epsilon'.\n" },
      { "A/mu",        "Re-added file 'mu'.\n" },
      { "A/B",         0 },
      { "A/B/lambda",  "This is the file 'lambda'.\n" },
      { "A/B/E",       0 },
      { "A/B/E/alpha", "This is the file 'alpha'.\n" },
      { "A/B/E/beta",  "This is the file 'beta'.\n" },
      { "A/B/F",       0 },
      { "A/B/Z",       0 },
      { "A/B/Z/zeta",  "This is the file 'zeta'.\n" },
      { "A/D",         0 },
      { "A/D/gamma",   "This is the file 'gamma'.\n" },
      { "A/D/G",       0 },
      { "A/D/G/pi",    "This is the file 'pi'.\n" },
      { "A/D/G/rho",   "Changed file 'rho'.\n" },
      { "A/D/H",       0 },
      { "A/D/H/chi",   "This is the file 'chi'.\n" },
      { "A/D/H/psi",   "This is the file 'psi'.\n" },
      { "A/D/H/omega", 0 }
    };
    expected_trees[revision_count].entries = expected_entries;
    expected_trees[revision_count].num_entries = 21;
    SVN_ERR(validate_revisions(fs, expected_trees, revision_count, subpool));
    revision_count++;
  }
  svn_pool_clear(subpool);

  /* Make a new txn based on the youngest revision, make some changes,
     and commit those changes (which makes a new youngest
     revision). */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, youngest_rev, subpool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, subpool));
  {
    static svn_test__txn_script_command_t script_entries[] = {
      { 'c', "A/D/G",        "A/D/G2" },
      { 'c', "A/epsilon",    "A/B/epsilon" },
    };
    SVN_ERR(svn_test__txn_script_exec(txn_root, script_entries, 2, subpool));
  }
  SVN_ERR(svn_fs_commit_txn(NULL, &youngest_rev, txn, subpool));
  SVN_TEST_ASSERT(SVN_IS_VALID_REVNUM(youngest_rev));

  /***********************************************************************/
  /* REVISION 4 */
  /***********************************************************************/
  {
    static svn_test__tree_entry_t expected_entries[] = {
      /* path, contents (0 = dir) */
      { "A",           0 },
      { "A/delta",     "This is the file 'delta'.\nLine 2.\n" },
      { "A/epsilon",   "This is the file 'epsilon'.\n" },
      { "A/mu",        "Re-added file 'mu'.\n" },
      { "A/B",         0 },
      { "A/B/epsilon", "This is the file 'epsilon'.\n" },
      { "A/B/lambda",  "This is the file 'lambda'.\n" },
      { "A/B/E",       0 },
      { "A/B/E/alpha", "This is the file 'alpha'.\n" },
      { "A/B/E/beta",  "This is the file 'beta'.\n" },
      { "A/B/F",       0 },
      { "A/B/Z",       0 },
      { "A/B/Z/zeta",  "This is the file 'zeta'.\n" },
      { "A/D",         0 },
      { "A/D/gamma",   "This is the file 'gamma'.\n" },
      { "A/D/G",       0 },
      { "A/D/G/pi",    "This is the file 'pi'.\n" },
      { "A/D/G/rho",   "Changed file 'rho'.\n" },
      { "A/D/G2",      0 },
      { "A/D/G2/pi",   "This is the file 'pi'.\n" },
      { "A/D/G2/rho",  "Changed file 'rho'.\n" },
      { "A/D/H",       0 },
      { "A/D/H/chi",   "This is the file 'chi'.\n" },
      { "A/D/H/psi",   "This is the file 'psi'.\n" },
      { "A/D/H/omega", 0 }
    };
    expected_trees[revision_count].entries = expected_entries;
    expected_trees[revision_count].num_entries = 25;
    SVN_ERR(validate_revisions(fs, expected_trees, revision_count, subpool));
    revision_count++;
  }
  svn_pool_destroy(subpool);

  return SVN_NO_ERROR;
}


/* Helper function for large_file_integrity().  Given a ROOT and PATH
   to a file, set *CHECKSUM to the checksum of kind CHECKSUM_KIND for the
   contents of the file. */
static svn_error_t *
get_file_checksum(svn_checksum_t **checksum,
                  svn_checksum_kind_t checksum_kind,
                  svn_fs_root_t *root,
                  const char *path,
                  apr_pool_t *pool)
{
  svn_stream_t *stream;

  /* Get a stream for the file contents. */
  SVN_ERR(svn_fs_file_contents(&stream, root, path, pool));
  SVN_ERR(svn_stream_contents_checksum(checksum, stream, checksum_kind,
                                       pool, pool));

  return SVN_NO_ERROR;
}


/* Return a pseudo-random number in the range [0,SCALAR) i.e. return
   a number N such that 0 <= N < SCALAR */
static int my_rand(apr_uint64_t scalar, apr_uint32_t *seed)
{
  static const apr_uint32_t TEST_RAND_MAX = 0xffffffffUL;
  /* Assumes TEST_RAND_MAX+1 can be exactly represented in a double */
  apr_uint32_t r = svn_test_rand(seed);
  return (int)(((double)r
                / ((double)TEST_RAND_MAX+1.0))
               * (double)scalar);
}


/* Put pseudo-random bytes in buffer BUF (which is LEN bytes long).
   If FULL is TRUE, simply replace every byte in BUF with a
   pseudo-random byte, else, replace a pseudo-random collection of
   bytes with pseudo-random data. */
static void
random_data_to_buffer(char *buf,
                      apr_size_t buf_len,
                      svn_boolean_t full,
                      apr_uint32_t *seed)
{
  apr_size_t i;
  apr_size_t num_bytes;
  apr_size_t offset;

  int ds_off = 0;
  const char *dataset = "0123456789";
  apr_size_t dataset_size = strlen(dataset);

  if (full)
    {
      for (i = 0; i < buf_len; i++)
        {
          ds_off = my_rand(dataset_size, seed);
          buf[i] = dataset[ds_off];
        }

      return;
    }

  num_bytes = my_rand(buf_len / 100, seed) + 1;
  for (i = 0; i < num_bytes; i++)
    {
      offset = my_rand(buf_len - 1, seed);
      ds_off = my_rand(dataset_size, seed);
      buf[offset] = dataset[ds_off];
    }

  return;
}


static svn_error_t *
file_integrity_helper(apr_size_t filesize, apr_uint32_t *seed,
                      const svn_test_opts_t *opts, const char *fs_name,
                      apr_pool_t *pool)
{
  svn_fs_t *fs;
  svn_fs_txn_t *txn;
  svn_fs_root_t *txn_root, *rev_root;
  svn_revnum_t youngest_rev = 0;
  apr_pool_t *subpool = svn_pool_create(pool);
  svn_string_t contents;
  char *content_buffer;
  svn_checksum_t *checksum;
  svn_checksum_kind_t checksum_kind = svn_checksum_md5;
  svn_checksum_t *checksum_list[100];
  svn_txdelta_window_handler_t wh_func;
  void *wh_baton;
  svn_revnum_t j;

  /* Create a filesystem and repository. */
  SVN_ERR(svn_test__create_fs(&fs, fs_name, opts, pool));

  /* Set up our file contents string buffer. */
  content_buffer = apr_palloc(pool, filesize);

  contents.data = content_buffer;
  contents.len = filesize;

  /* THE PLAN:

     The plan here is simple.  We have a very large file (FILESIZE
     bytes) that we initialize with pseudo-random data and commit.
     Then we make pseudo-random modifications to that file's contents,
     committing after each mod.  Prior to each commit, we generate an
     MD5 checksum for the contents of the file, storing each of those
     checksums in an array.  After we've made a whole bunch of edits
     and commits, we'll re-check that file's contents as of each
     revision in the repository, recalculate a checksum for those
     contents, and make sure the "before" and "after" checksums
     match.  */

  /* Create a big, ugly, pseudo-random-filled file and commit it.  */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, youngest_rev, subpool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, subpool));
  SVN_ERR(svn_fs_make_file(txn_root, "bigfile", subpool));
  random_data_to_buffer(content_buffer, filesize, TRUE, seed);
  SVN_ERR(svn_checksum(&checksum, checksum_kind, contents.data, contents.len,
                       pool));
  SVN_ERR(svn_fs_apply_textdelta
          (&wh_func, &wh_baton, txn_root, "bigfile", NULL, NULL, subpool));
  SVN_ERR(svn_txdelta_send_string(&contents, wh_func, wh_baton, subpool));
  SVN_ERR(svn_fs_commit_txn(NULL, &youngest_rev, txn, subpool));
  SVN_TEST_ASSERT(SVN_IS_VALID_REVNUM(youngest_rev));
  SVN_ERR(svn_fs_deltify_revision(fs, youngest_rev, subpool));
  checksum_list[youngest_rev] = checksum;
  svn_pool_clear(subpool);

  /* Now, let's make some edits to the beginning of our file, and
     commit those. */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, youngest_rev, subpool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, subpool));
  random_data_to_buffer(content_buffer, 20, TRUE, seed);
  SVN_ERR(svn_checksum(&checksum, checksum_kind, contents.data, contents.len,
                       pool));
  SVN_ERR(svn_fs_apply_textdelta
          (&wh_func, &wh_baton, txn_root, "bigfile", NULL, NULL, subpool));
  SVN_ERR(svn_txdelta_send_string(&contents, wh_func, wh_baton, subpool));
  SVN_ERR(svn_fs_commit_txn(NULL, &youngest_rev, txn, subpool));
  SVN_TEST_ASSERT(SVN_IS_VALID_REVNUM(youngest_rev));
  SVN_ERR(svn_fs_deltify_revision(fs, youngest_rev, subpool));
  checksum_list[youngest_rev] = checksum;
  svn_pool_clear(subpool);

  /* Now, let's make some edits to the end of our file. */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, youngest_rev, subpool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, subpool));
  random_data_to_buffer(content_buffer + (filesize - 20), 20, TRUE, seed);
  SVN_ERR(svn_checksum(&checksum, checksum_kind, contents.data, contents.len,
                       pool));
  SVN_ERR(svn_fs_apply_textdelta
          (&wh_func, &wh_baton, txn_root, "bigfile", NULL, NULL, subpool));
  SVN_ERR(svn_txdelta_send_string(&contents, wh_func, wh_baton, subpool));
  SVN_ERR(svn_fs_commit_txn(NULL, &youngest_rev, txn, subpool));
  SVN_TEST_ASSERT(SVN_IS_VALID_REVNUM(youngest_rev));
  SVN_ERR(svn_fs_deltify_revision(fs, youngest_rev, subpool));
  checksum_list[youngest_rev] = checksum;
  svn_pool_clear(subpool);

  /* How about some edits to both the beginning and the end of the
     file? */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, youngest_rev, subpool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, subpool));
  random_data_to_buffer(content_buffer, 20, TRUE, seed);
  random_data_to_buffer(content_buffer + (filesize - 20), 20, TRUE, seed);
  SVN_ERR(svn_checksum(&checksum, checksum_kind, contents.data, contents.len,
                       pool));
  SVN_ERR(svn_fs_apply_textdelta
          (&wh_func, &wh_baton, txn_root, "bigfile", NULL, NULL, subpool));
  SVN_ERR(svn_txdelta_send_string(&contents, wh_func, wh_baton, subpool));
  SVN_ERR(svn_fs_commit_txn(NULL, &youngest_rev, txn, subpool));
  SVN_TEST_ASSERT(SVN_IS_VALID_REVNUM(youngest_rev));
  SVN_ERR(svn_fs_deltify_revision(fs, youngest_rev, subpool));
  checksum_list[youngest_rev] = checksum;
  svn_pool_clear(subpool);

  /* Alright, now we're just going to go crazy.  Let's make many more
     edits -- pseudo-random numbers and offsets of bytes changed to
     more pseudo-random values.  */
  for (j = youngest_rev; j < 30; j = youngest_rev)
    {
      SVN_ERR(svn_fs_begin_txn(&txn, fs, youngest_rev, subpool));
      SVN_ERR(svn_fs_txn_root(&txn_root, txn, subpool));
      random_data_to_buffer(content_buffer, filesize, FALSE, seed);
      SVN_ERR(svn_checksum(&checksum, checksum_kind, contents.data,
                           contents.len, pool));
      SVN_ERR(svn_fs_apply_textdelta(&wh_func, &wh_baton, txn_root,
                                     "bigfile", NULL, NULL, subpool));
      SVN_ERR(svn_txdelta_send_string
              (&contents, wh_func, wh_baton, subpool));
      SVN_ERR(svn_fs_commit_txn(NULL, &youngest_rev, txn, subpool));
      SVN_TEST_ASSERT(SVN_IS_VALID_REVNUM(youngest_rev));
      SVN_ERR(svn_fs_deltify_revision(fs, youngest_rev, subpool));
      checksum_list[youngest_rev] = checksum;
      svn_pool_clear(subpool);
    }

  /* Now, calculate an MD5 digest for the contents of our big ugly
     file in each revision currently in existence, and make the sure
     the checksum matches the checksum of the data prior to its
     commit. */
  for (j = youngest_rev; j > 0; j--)
    {
      SVN_ERR(svn_fs_revision_root(&rev_root, fs, j, subpool));
      SVN_ERR(get_file_checksum(&checksum, checksum_kind, rev_root, "bigfile",
                                subpool));
      if (!svn_checksum_match(checksum, checksum_list[j]))
        return svn_error_createf
          (SVN_ERR_FS_GENERAL, NULL,
           "verify-checksum: checksum mismatch, revision %ld:\n"
           "   expected:  %s\n"
           "     actual:  %s\n", j,
        svn_checksum_to_cstring(checksum_list[j], pool),
        svn_checksum_to_cstring(checksum, pool));

      svn_pool_clear(subpool);
    }

  svn_pool_destroy(subpool);
  return SVN_NO_ERROR;
}


static svn_error_t *
small_file_integrity(const svn_test_opts_t *opts,
                     apr_pool_t *pool)
{
  apr_uint32_t seed = (apr_uint32_t) apr_time_now();

  /* Just use a really small file size... */
  return file_integrity_helper(20, &seed, opts,
                               "test-repo-small-file-integrity", pool);
}


static svn_error_t *
almostmedium_file_integrity(const svn_test_opts_t *opts,
                            apr_pool_t *pool)
{
  apr_uint32_t seed = (apr_uint32_t) apr_time_now();

  return file_integrity_helper(SVN_DELTA_WINDOW_SIZE - 1, &seed, opts,
                               "test-repo-almostmedium-file-integrity", pool);
}


static svn_error_t *
medium_file_integrity(const svn_test_opts_t *opts,
                      apr_pool_t *pool)
{
  apr_uint32_t seed = (apr_uint32_t) apr_time_now();

  /* Being no larger than the standard delta window size affects
     deltification internally, so test that. */
  return file_integrity_helper(SVN_DELTA_WINDOW_SIZE, &seed, opts,
                               "test-repo-medium-file-integrity", pool);
}


static svn_error_t *
large_file_integrity(const svn_test_opts_t *opts,
                     apr_pool_t *pool)
{
  apr_uint32_t seed = (apr_uint32_t) apr_time_now();

  /* Being larger than the standard delta window size affects
     deltification internally, so test that. */
  return file_integrity_helper(SVN_DELTA_WINDOW_SIZE + 1, &seed, opts,
                               "test-repo-large-file-integrity", pool);
}


static svn_error_t *
check_root_revision(const svn_test_opts_t *opts,
                    apr_pool_t *pool)
{
  svn_fs_t *fs;
  svn_fs_txn_t *txn;
  svn_fs_root_t *txn_root, *rev_root;
  svn_revnum_t youngest_rev, test_rev;
  apr_pool_t *subpool = svn_pool_create(pool);
  int i;

  /* Create a filesystem and repository. */
  SVN_ERR(svn_test__create_fs(&fs, "test-repo-check-root-revision",
                              opts, pool));

  /* Create and commit the greek tree. */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, 0, subpool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, subpool));
  SVN_ERR(svn_test__create_greek_tree(txn_root, subpool));
  SVN_ERR(svn_fs_commit_txn(NULL, &youngest_rev, txn, subpool));
  SVN_TEST_ASSERT(SVN_IS_VALID_REVNUM(youngest_rev));

  /* Root node's revision should be the same as YOUNGEST_REV. */
  SVN_ERR(svn_fs_revision_root(&rev_root, fs, youngest_rev, subpool));
  SVN_ERR(svn_fs_node_created_rev(&test_rev, rev_root, "", subpool));
  if (test_rev != youngest_rev)
    return svn_error_createf
      (SVN_ERR_FS_GENERAL, NULL,
       "Root node in revision %ld has unexpected stored revision %ld",
       youngest_rev, test_rev);
  svn_pool_clear(subpool);

  for (i = 0; i < 10; i++)
    {
      /* Create and commit the greek tree. */
      SVN_ERR(svn_fs_begin_txn(&txn, fs, youngest_rev, subpool));
      SVN_ERR(svn_fs_txn_root(&txn_root, txn, subpool));
      SVN_ERR(svn_test__set_file_contents
              (txn_root, "iota",
               apr_psprintf(subpool, "iota version %d", i + 2), subpool));

      SVN_ERR(svn_fs_commit_txn(NULL, &youngest_rev, txn, subpool));
      SVN_TEST_ASSERT(SVN_IS_VALID_REVNUM(youngest_rev));

      /* Root node's revision should be the same as YOUNGEST_REV. */
      SVN_ERR(svn_fs_revision_root(&rev_root, fs, youngest_rev, subpool));
      SVN_ERR(svn_fs_node_created_rev(&test_rev, rev_root, "", subpool));
      if (test_rev != youngest_rev)
        return svn_error_createf
          (SVN_ERR_FS_GENERAL, NULL,
           "Root node in revision %ld has unexpected stored revision %ld",
           youngest_rev, test_rev);
      svn_pool_clear(subpool);
    }

  svn_pool_destroy(subpool);
  return SVN_NO_ERROR;
}


struct node_created_rev_args {
  const char *path;
  svn_revnum_t rev;
};


static svn_error_t *
verify_path_revs(svn_fs_root_t *root,
                 struct node_created_rev_args *args,
                 int num_path_revs,
                 apr_pool_t *pool)
{
  apr_pool_t *subpool = svn_pool_create(pool);
  int i;
  svn_revnum_t rev;

  for (i = 0; i < num_path_revs; i++)
    {
      svn_pool_clear(subpool);
      SVN_ERR(svn_fs_node_created_rev(&rev, root, args[i].path, subpool));
      if (rev != args[i].rev)
        return svn_error_createf
          (SVN_ERR_FS_GENERAL, NULL,
           "verify_path_revs: '%s' has created rev '%ld' "
           "(expected '%ld')",
           args[i].path, rev, args[i].rev);
    }

  svn_pool_destroy(subpool);
  return SVN_NO_ERROR;
}


static svn_error_t *
test_node_created_rev(const svn_test_opts_t *opts,
                      apr_pool_t *pool)
{
  apr_pool_t *subpool = svn_pool_create(pool);
  svn_fs_t *fs;
  svn_fs_txn_t *txn;
  svn_fs_root_t *txn_root, *rev_root;
  svn_revnum_t youngest_rev = 0;
  int i;
  struct node_created_rev_args path_revs[21];
  const char *greek_paths[21] = {
    /*  0 */ "",
    /*  1 */ "iota",
    /*  2 */ "A",
    /*  3 */ "A/mu",
    /*  4 */ "A/B",
    /*  5 */ "A/B/lambda",
    /*  6 */ "A/B/E",
    /*  7 */ "A/B/E/alpha",
    /*  8 */ "A/B/E/beta",
    /*  9 */ "A/B/F",
    /* 10 */ "A/C",
    /* 11 */ "A/D",
    /* 12 */ "A/D/gamma",
    /* 13 */ "A/D/G",
    /* 14 */ "A/D/G/pi",
    /* 15 */ "A/D/G/rho",
    /* 16 */ "A/D/G/tau",
    /* 17 */ "A/D/H",
    /* 18 */ "A/D/H/chi",
    /* 19 */ "A/D/H/psi",
    /* 20 */ "A/D/H/omega",
  };

  /* Initialize the paths in our args list. */
  for (i = 0; i < 20; i++)
    path_revs[i].path = greek_paths[i];

  /* Create a filesystem and repository. */
  SVN_ERR(svn_test__create_fs(&fs, "test-repo-node-created-rev",
                              opts, pool));

  /* Created the greek tree in revision 1. */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, youngest_rev, subpool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, subpool));
  SVN_ERR(svn_test__create_greek_tree(txn_root, subpool));

  /* Now, prior to committing, all these nodes should have an invalid
     created rev.  After all, the rev has been created yet.  Verify
     this. */
  for (i = 0; i < 20; i++)
    path_revs[i].rev = SVN_INVALID_REVNUM;
  SVN_ERR(verify_path_revs(txn_root, path_revs, 20, subpool));

  /* Now commit the transaction. */
  SVN_ERR(svn_fs_commit_txn(NULL, &youngest_rev, txn, subpool));
  SVN_TEST_ASSERT(SVN_IS_VALID_REVNUM(youngest_rev));

  /* Now, we have a new revision, and all paths in it should have a
     created rev of 1.  Verify this. */
  SVN_ERR(svn_fs_revision_root(&rev_root, fs, youngest_rev, subpool));
  for (i = 0; i < 20; i++)
    path_revs[i].rev = 1;
  SVN_ERR(verify_path_revs(rev_root, path_revs, 20, subpool));

  /*** Let's make some changes/commits here and there, and make sure
       we can keep this whole created rev thing in good standing.  The
       general rule here is that prior to commit, mutable things have
       an invalid created rev, immutable things have their original
       created rev.  After the commit, those things which had invalid
       created revs in the transaction now have the youngest revision
       as their created rev.

       ### NOTE: Bubble-up currently affect the created revisions for
       directory nodes.  I'm not sure if this is the behavior we've
       settled on as desired.
  */

  /*** clear the per-commit pool */
  svn_pool_clear(subpool);
  /* begin a new transaction */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, youngest_rev, subpool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, subpool));
  /* The created revs on a txn root should be the same as on the rev
     root it came from, if we haven't made changes yet.  (See issue
     #2608.) */
  SVN_ERR(verify_path_revs(txn_root, path_revs, 20, subpool));
  /* make mods */
  SVN_ERR(svn_test__set_file_contents
          (txn_root, "iota", "pointless mod here", subpool));
  /* verify created revs */
  path_revs[0].rev = SVN_INVALID_REVNUM; /* (root) */
  path_revs[1].rev = SVN_INVALID_REVNUM; /* iota */
  SVN_ERR(verify_path_revs(txn_root, path_revs, 20, subpool));
  /* commit transaction */
  SVN_ERR(svn_fs_commit_txn(NULL, &youngest_rev, txn, subpool));
  SVN_TEST_ASSERT(SVN_IS_VALID_REVNUM(youngest_rev));
  /* get a revision root for the new revision */
  SVN_ERR(svn_fs_revision_root(&rev_root, fs, youngest_rev, subpool));
  /* verify created revs */
  path_revs[0].rev = 2; /* (root) */
  path_revs[1].rev = 2; /* iota */
  SVN_ERR(verify_path_revs(rev_root, path_revs, 20, subpool));

  /*** clear the per-commit pool */
  svn_pool_clear(subpool);
  /* begin a new transaction */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, youngest_rev, subpool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, subpool));
  /* make mods */
  SVN_ERR(svn_test__set_file_contents
          (txn_root, "A/D/H/omega", "pointless mod here", subpool));
  /* verify created revs */
  path_revs[0].rev  = SVN_INVALID_REVNUM; /* (root) */
  path_revs[2].rev  = SVN_INVALID_REVNUM; /* A */
  path_revs[11].rev = SVN_INVALID_REVNUM; /* D */
  path_revs[17].rev = SVN_INVALID_REVNUM; /* H */
  path_revs[20].rev = SVN_INVALID_REVNUM; /* omega */
  SVN_ERR(verify_path_revs(txn_root, path_revs, 20, subpool));
  /* commit transaction */
  SVN_ERR(svn_fs_commit_txn(NULL, &youngest_rev, txn, subpool));
  SVN_TEST_ASSERT(SVN_IS_VALID_REVNUM(youngest_rev));
  /* get a revision root for the new revision */
  SVN_ERR(svn_fs_revision_root(&rev_root, fs, youngest_rev, subpool));
  /* verify created revs */
  path_revs[0].rev  = 3; /* (root) */
  path_revs[2].rev  = 3; /* A */
  path_revs[11].rev = 3; /* D */
  path_revs[17].rev = 3; /* H */
  path_revs[20].rev = 3; /* omega */
  SVN_ERR(verify_path_revs(rev_root, path_revs, 20, subpool));

  /* Destroy the per-commit subpool. */
  svn_pool_destroy(subpool);

  return SVN_NO_ERROR;
}


static svn_error_t *
check_related(const svn_test_opts_t *opts,
              apr_pool_t *pool)
{
  apr_pool_t *subpool = svn_pool_create(pool);
  svn_fs_t *fs;
  svn_fs_txn_t *txn;
  svn_fs_root_t *txn_root, *rev_root;
  svn_revnum_t youngest_rev = 0;

  /* Create a filesystem and repository. */
  SVN_ERR(svn_test__create_fs(&fs, "test-repo-check-related",
                              opts, pool));

  /*** Step I: Build up some state in our repository through a series
       of commits */

  /* Using files because bubble-up complicates the testing.  However,
     the algorithm itself is ambivalent about what type of node is
     being examined.

     - New files show up in this order (through time): A,B,C,D,E,F
     - Number following filename is the revision.
     - Vertical motion shows revision history
     - Horizontal motion show copy history.

     A1---------C4         E7
     |          |          |
     A2         C5         E8---F9
     |          |               |
     A3---B4    C6              F10
     |    |
     A4   B5----------D6
          |           |
          B6          D7
  */
  /* Revision 1 */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, youngest_rev, subpool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, subpool));
  SVN_ERR(svn_fs_make_file(txn_root, "A", subpool));
  SVN_ERR(svn_test__set_file_contents(txn_root, "A", "1", subpool));
  SVN_ERR(svn_fs_commit_txn(NULL, &youngest_rev, txn, subpool));
  SVN_TEST_ASSERT(SVN_IS_VALID_REVNUM(youngest_rev));
  svn_pool_clear(subpool);
  /* Revision 2 */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, youngest_rev, subpool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, subpool));
  SVN_ERR(svn_test__set_file_contents(txn_root, "A", "2", subpool));
  SVN_ERR(svn_fs_commit_txn(NULL, &youngest_rev, txn, subpool));
  SVN_TEST_ASSERT(SVN_IS_VALID_REVNUM(youngest_rev));
  svn_pool_clear(subpool);
  /* Revision 3 */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, youngest_rev, subpool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, subpool));
  SVN_ERR(svn_test__set_file_contents(txn_root, "A", "3", subpool));
  SVN_ERR(svn_fs_commit_txn(NULL, &youngest_rev, txn, subpool));
  SVN_TEST_ASSERT(SVN_IS_VALID_REVNUM(youngest_rev));
  svn_pool_clear(subpool);
  /* Revision 4 */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, youngest_rev, subpool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, subpool));
  SVN_ERR(svn_test__set_file_contents(txn_root, "A", "4", subpool));
  SVN_ERR(svn_fs_revision_root(&rev_root, fs, 3, subpool));
  SVN_ERR(svn_fs_copy(rev_root, "A", txn_root, "B", subpool));
  SVN_ERR(svn_test__set_file_contents(txn_root, "B", "4", subpool));
  SVN_ERR(svn_fs_revision_root(&rev_root, fs, 1, subpool));
  SVN_ERR(svn_fs_copy(rev_root, "A", txn_root, "C", subpool));
  SVN_ERR(svn_test__set_file_contents(txn_root, "C", "4", subpool));
  SVN_ERR(svn_fs_commit_txn(NULL, &youngest_rev, txn, subpool));
  SVN_TEST_ASSERT(SVN_IS_VALID_REVNUM(youngest_rev));
  svn_pool_clear(subpool);
  /* Revision 5 */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, youngest_rev, subpool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, subpool));
  SVN_ERR(svn_test__set_file_contents(txn_root, "B", "5", subpool));
  SVN_ERR(svn_test__set_file_contents(txn_root, "C", "5", subpool));
  SVN_ERR(svn_fs_commit_txn(NULL, &youngest_rev, txn, subpool));
  SVN_TEST_ASSERT(SVN_IS_VALID_REVNUM(youngest_rev));
  svn_pool_clear(subpool);
  /* Revision 6 */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, youngest_rev, subpool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, subpool));
  SVN_ERR(svn_test__set_file_contents(txn_root, "B", "6", subpool));
  SVN_ERR(svn_test__set_file_contents(txn_root, "C", "6", subpool));
  SVN_ERR(svn_fs_revision_root(&rev_root, fs, 5, subpool));
  SVN_ERR(svn_fs_copy(rev_root, "B", txn_root, "D", subpool));
  SVN_ERR(svn_test__set_file_contents(txn_root, "D", "5", subpool));
  SVN_ERR(svn_fs_commit_txn(NULL, &youngest_rev, txn, subpool));
  SVN_TEST_ASSERT(SVN_IS_VALID_REVNUM(youngest_rev));
  svn_pool_clear(subpool);
  /* Revision 7 */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, youngest_rev, subpool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, subpool));
  SVN_ERR(svn_test__set_file_contents(txn_root, "D", "7", subpool));
  SVN_ERR(svn_fs_make_file(txn_root, "E", subpool));
  SVN_ERR(svn_test__set_file_contents(txn_root, "E", "7", subpool));
  SVN_ERR(svn_fs_commit_txn(NULL, &youngest_rev, txn, subpool));
  SVN_TEST_ASSERT(SVN_IS_VALID_REVNUM(youngest_rev));
  svn_pool_clear(subpool);
  /* Revision 8 */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, youngest_rev, subpool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, subpool));
  SVN_ERR(svn_test__set_file_contents(txn_root, "E", "8", subpool));
  SVN_ERR(svn_fs_commit_txn(NULL, &youngest_rev, txn, subpool));
  SVN_TEST_ASSERT(SVN_IS_VALID_REVNUM(youngest_rev));
  svn_pool_clear(subpool);
  /* Revision 9 */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, youngest_rev, subpool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, subpool));
  SVN_ERR(svn_fs_revision_root(&rev_root, fs, 8, subpool));
  SVN_ERR(svn_fs_copy(rev_root, "E", txn_root, "F", subpool));
  SVN_ERR(svn_test__set_file_contents(txn_root, "F", "9", subpool));
  SVN_ERR(svn_fs_commit_txn(NULL, &youngest_rev, txn, subpool));
  SVN_TEST_ASSERT(SVN_IS_VALID_REVNUM(youngest_rev));
  svn_pool_clear(subpool);
  /* Revision 10 */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, youngest_rev, subpool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, subpool));
  SVN_ERR(svn_test__set_file_contents(txn_root, "F", "10", subpool));
  SVN_ERR(svn_fs_commit_txn(NULL, &youngest_rev, txn, subpool));
  SVN_TEST_ASSERT(SVN_IS_VALID_REVNUM(youngest_rev));
  svn_pool_clear(subpool);

  /*** Step II: Exhaustively verify relationship between all nodes in
       existence. */
  {
    int i, j;

    struct path_rev_t
    {
      const char *path;
      svn_revnum_t rev;
    };

    /* Our 16 existing files/revisions. */
    struct path_rev_t path_revs[16] = {
      { "A", 1 }, { "A", 2 }, { "A", 3 }, { "A", 4 },
      { "B", 4 }, { "B", 5 }, { "B", 6 }, { "C", 4 },
      { "C", 5 }, { "C", 6 }, { "D", 6 }, { "D", 7 },
      { "E", 7 }, { "E", 8 }, { "F", 9 }, { "F", 10 }
    };

    /* Latest revision that touched the respective path. */
    struct path_rev_t latest_changes[6] = {
      { "A", 4 }, { "B", 6 }, { "C", 6 },
      { "D", 7 }, { "E", 8 }, { "F", 10 }
    };

    int related_matrix[16][16] = {
      /* A1 ... F10 across the top here*/
      { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0 }, /* A1 */
      { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0 }, /* A2 */
      { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0 }, /* A3 */
      { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0 }, /* A4 */
      { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0 }, /* B4 */
      { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0 }, /* B5 */
      { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0 }, /* B6 */
      { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0 }, /* C4 */
      { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0 }, /* C5 */
      { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0 }, /* C6 */
      { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0 }, /* D6 */
      { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0 }, /* D7 */
      { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1 }, /* E7 */
      { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1 }, /* E8 */
      { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1 }, /* F9 */
      { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1 }  /* F10 */
    };

    /* Here's the fun part.  Running the tests. */
    for (i = 0; i < 16; i++)
      {
        for (j = 0; j < 16; j++)
          {
            struct path_rev_t pr1 = path_revs[i];
            struct path_rev_t pr2 = path_revs[j];
            const svn_fs_id_t *id1, *id2;
            int related = 0;
            svn_fs_node_relation_t relation;
            svn_fs_root_t *rev_root1, *rev_root2;

            /* Get the ID for the first path/revision combination. */
            SVN_ERR(svn_fs_revision_root(&rev_root1, fs, pr1.rev, subpool));
            SVN_ERR(svn_fs_node_id(&id1, rev_root1, pr1.path, subpool));

            /* Get the ID for the second path/revision combination. */
            SVN_ERR(svn_fs_revision_root(&rev_root2, fs, pr2.rev, subpool));
            SVN_ERR(svn_fs_node_id(&id2, rev_root2, pr2.path, subpool));

            /* <exciting> Now, run the relationship check! </exciting> */
            related = svn_fs_check_related(id1, id2) ? 1 : 0;
            if (related == related_matrix[i][j])
              {
                /* xlnt! */
              }
            else if (related && (! related_matrix[i][j]))
              {
                return svn_error_createf
                  (SVN_ERR_TEST_FAILED, NULL,
                   "expected '%s:%d' to be related to '%s:%d'; it was not",
                   pr1.path, (int)pr1.rev, pr2.path, (int)pr2.rev);
              }
            else if ((! related) && related_matrix[i][j])
              {
                return svn_error_createf
                  (SVN_ERR_TEST_FAILED, NULL,
                   "expected '%s:%d' to not be related to '%s:%d'; it was",
                   pr1.path, (int)pr1.rev, pr2.path, (int)pr2.rev);
              }

            /* Asking directly, i.e. without involving the noderev IDs as
             * an intermediate, should yield the same results. */
            SVN_ERR(svn_fs_node_relation(&relation, rev_root1, pr1.path,
                                         rev_root2, pr2.path, subpool));
            if (i == j)
              {
                /* Identical note. */
                if (!related || relation != svn_fs_node_unchanged)
                  {
                    return svn_error_createf
                      (SVN_ERR_TEST_FAILED, NULL,
                      "expected '%s:%d' to be the same as '%s:%d';"
                      " it was not",
                      pr1.path, (int)pr1.rev, pr2.path, (int)pr2.rev);
                  }
              }
            else if (related && relation != svn_fs_node_common_ancestor)
              {
                return svn_error_createf
                  (SVN_ERR_TEST_FAILED, NULL,
                   "expected '%s:%d' to have a common ancestor with '%s:%d';"
                   " it had not",
                   pr1.path, (int)pr1.rev, pr2.path, (int)pr2.rev);
              }
            else if (!related && relation != svn_fs_node_unrelated)
              {
                return svn_error_createf
                  (SVN_ERR_TEST_FAILED, NULL,
                   "expected '%s:%d' to not be related to '%s:%d'; it was",
                   pr1.path, (int)pr1.rev, pr2.path, (int)pr2.rev);
              }

            svn_pool_clear(subpool);
          } /* for ... */
      } /* for ... */

    /* Verify that the noderevs stay the same after their last change. */
    for (i = 0; i < 6; ++i)
      {
        const char *path = latest_changes[i].path;
        svn_revnum_t latest = latest_changes[i].rev;
        svn_fs_root_t *latest_root;
        svn_revnum_t rev;
        svn_fs_node_relation_t relation;

        /* FS root of the latest change. */
        svn_pool_clear(subpool);
        SVN_ERR(svn_fs_revision_root(&latest_root, fs, latest, subpool));

        /* All future revisions. */
        for (rev = latest + 1; rev <= 10; ++rev)
          {
            /* Query their noderev relationship to the latest change. */
            SVN_ERR(svn_fs_revision_root(&rev_root, fs, rev, subpool));
            SVN_ERR(svn_fs_node_relation(&relation, latest_root, path,
                                         rev_root, path, subpool));

            /* They shall use the same noderevs */
            if (relation != svn_fs_node_unchanged)
              {
                return svn_error_createf
                  (SVN_ERR_TEST_FAILED, NULL,
                  "expected '%s:%d' to be the same as '%s:%d';"
                  " it was not",
                  path, (int)latest, path, (int)rev);
              }
          } /* for ... */
      } /* for ... */
  }

  /* Destroy the subpool. */
  svn_pool_destroy(subpool);

  return SVN_NO_ERROR;
}


static svn_error_t *
check_txn_related(const svn_test_opts_t *opts,
                  apr_pool_t *pool)
{
  apr_pool_t *subpool = svn_pool_create(pool);
  svn_fs_t *fs;
  svn_fs_txn_t *txn[3];
  svn_fs_root_t *root[3];
  svn_revnum_t youngest_rev = 0;

  /* Create a filesystem and repository. */
  SVN_ERR(svn_test__create_fs(&fs, "test-repo-check-txn-related",
                              opts, pool));

  /*** Step I: Build up some state in our repository through a series
       of commits */

  /* This is the node graph we are testing.  It contains one revision (r1)
     and two transactions, T1 and T2 - yet uncommitted.

     A is a file that exists in r1 (A-0) and gets modified in both txns.
     C is a copy of A-0 made in both txns.
     B is a new node created in both txns
     D is a file that exists in r1 (D-0) and never gets modified.
     / is the root folder, touched in r0, r1 and both txns (root-0)
     R is a copy of the root-0 made in both txns.

     The edges in the graph connect related noderevs:

                 +--A-0--+                D-0           +-root-0-+
                 |       |                              |        |
           +-----+       +-----+                 +------+        +------+
           |     |       |     |                 |      |        |      |
     B-1   C-1   A-1     A-2   C-2   B-2         R-1    root-1   root-2 R-2
  */
  /* Revision 1 */
  SVN_ERR(svn_fs_begin_txn(&txn[0], fs, youngest_rev, subpool));
  SVN_ERR(svn_fs_txn_root(&root[0], txn[0], subpool));
  SVN_ERR(svn_fs_make_file(root[0], "A", subpool));
  SVN_ERR(svn_test__set_file_contents(root[0], "A", "1", subpool));
  SVN_ERR(svn_fs_make_file(root[0], "D", subpool));
  SVN_ERR(svn_fs_commit_txn(NULL, &youngest_rev, txn[0], subpool));
  SVN_TEST_ASSERT(SVN_IS_VALID_REVNUM(youngest_rev));
  svn_pool_clear(subpool);
  SVN_ERR(svn_fs_revision_root(&root[0], fs, youngest_rev, pool));

  /* Transaction 1 */
  SVN_ERR(svn_fs_begin_txn(&txn[1], fs, youngest_rev, pool));
  SVN_ERR(svn_fs_txn_root(&root[1], txn[1], pool));
  SVN_ERR(svn_test__set_file_contents(root[1], "A", "2", pool));
  SVN_ERR(svn_fs_copy(root[0], "A", root[1], "C", pool));
  SVN_ERR(svn_fs_copy(root[0], "", root[1], "R", pool));
  SVN_ERR(svn_fs_make_file(root[1], "B", pool));

  /* Transaction 2 */
  SVN_ERR(svn_fs_begin_txn(&txn[2], fs, youngest_rev, pool));
  SVN_ERR(svn_fs_txn_root(&root[2], txn[2], pool));
  SVN_ERR(svn_test__set_file_contents(root[2], "A", "2", pool));
  SVN_ERR(svn_fs_copy(root[0], "A", root[2], "C", pool));
  SVN_ERR(svn_fs_copy(root[0], "", root[2], "R", pool));
  SVN_ERR(svn_fs_make_file(root[2], "B", pool));

  /*** Step II: Exhaustively verify relationship between all nodes in
       existence. */
  {
    enum { NODE_COUNT = 13 };
    int i, j;

    struct path_rev_t
    {
      const char *path;
      int root;
    };

    /* Our 16 existing files/revisions. */
    struct path_rev_t path_revs[NODE_COUNT] = {
      { "A", 0 }, { "A", 1 }, { "A", 2 },
      { "B", 1 }, { "B", 2 },
      { "C", 1 }, { "C", 2 },
      { "D", 0 },
      { "/", 0 }, { "/", 1 }, { "/", 2 },
      { "R", 1 }, { "R", 2 }
    };

    int related_matrix[NODE_COUNT][NODE_COUNT] = {
      /* A-0 ... R-2 across the top here*/
      { 1, 1, 1, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0 }, /* A-0 */
      { 1, 1, 1, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0 }, /* A-1 */
      { 1, 1, 1, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0 }, /* A-2 */
      { 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0 }, /* B-1 */
      { 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0 }, /* B-2 */
      { 1, 1, 1, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0 }, /* C-1 */
      { 1, 1, 1, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0 }, /* C-2 */
      { 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0 }, /* D-0 */
      { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1 }, /* root-0 */
      { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1 }, /* root-1 */
      { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1 }, /* root-2 */
      { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1 }, /* R-1 */
      { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1 }, /* R-2 */
    };

    /* Here's the fun part.  Running the tests. */
    for (i = 0; i < NODE_COUNT; i++)
      {
        for (j = 0; j < NODE_COUNT; j++)
          {
            struct path_rev_t pr1 = path_revs[i];
            struct path_rev_t pr2 = path_revs[j];
            const svn_fs_id_t *id1, *id2;
            int related = 0;
            svn_fs_node_relation_t relation;

            svn_pool_clear(subpool);

            /* Get the ID for the first path/revision combination. */
            SVN_ERR(svn_fs_node_id(&id1, root[pr1.root], pr1.path, subpool));

            /* Get the ID for the second path/revision combination. */
            SVN_ERR(svn_fs_node_id(&id2, root[pr2.root], pr2.path, subpool));

            /* <exciting> Now, run the relationship check! </exciting> */
            related = svn_fs_check_related(id1, id2) ? 1 : 0;
            if (related == related_matrix[i][j])
              {
                /* xlnt! */
              }
            else if ((! related) && related_matrix[i][j])
              {
                return svn_error_createf
                  (SVN_ERR_TEST_FAILED, NULL,
                   "expected '%s-%d' to be related to '%s-%d'; it was not",
                   pr1.path, pr1.root, pr2.path, pr2.root);
              }
            else if (related && (! related_matrix[i][j]))
              {
                return svn_error_createf
                  (SVN_ERR_TEST_FAILED, NULL,
                   "expected '%s-%d' to not be related to '%s-%d'; it was",
                   pr1.path, pr1.root, pr2.path, pr2.root);
              }

            /* Asking directly, i.e. without involving the noderev IDs as
             * an intermediate, should yield the same results. */
            SVN_ERR(svn_fs_node_relation(&relation, root[pr1.root], pr1.path,
                                         root[pr2.root], pr2.path, subpool));
            if (i == j)
              {
                /* Identical noderev. */
                if (!related || relation != svn_fs_node_unchanged)
                  {
                    return svn_error_createf
                      (SVN_ERR_TEST_FAILED, NULL,
                      "expected '%s-%d' to be the same as '%s-%d';"
                      " it was not",
                      pr1.path, pr1.root, pr2.path, pr2.root);
                  }
              }
            else if (related && relation != svn_fs_node_common_ancestor)
              {
                return svn_error_createf
                  (SVN_ERR_TEST_FAILED, NULL,
                   "expected '%s-%d' to have a common ancestor with '%s-%d';"
                   " it had not",
                   pr1.path, pr1.root, pr2.path, pr2.root);
              }
            else if (!related && relation != svn_fs_node_unrelated)
              {
                return svn_error_createf
                  (SVN_ERR_TEST_FAILED, NULL,
                   "expected '%s-%d' to not be related to '%s-%d'; it was",
                   pr1.path, pr1.root, pr2.path, pr2.root);
              }
          } /* for ... */
      } /* for ... */

    /* Verify that the noderevs stay the same after their last change.
       There is only D that is not changed. */
    for (i = 1; i <= 2; ++i)
      {
        svn_fs_node_relation_t relation;
        svn_pool_clear(subpool);

        /* Query their noderev relationship to the latest change. */
        SVN_ERR(svn_fs_node_relation(&relation, root[i], "D",
                                     root[0], "D", subpool));

        /* They shall use the same noderevs */
        if (relation != svn_fs_node_unchanged)
          {
            return svn_error_createf
              (SVN_ERR_TEST_FAILED, NULL,
              "expected 'D-%d' to be the same as 'D-0'; it was not", i);
          }
      } /* for ... */
  }

  /* Destroy the subpool. */
  svn_pool_destroy(subpool);

  return SVN_NO_ERROR;
}


static svn_error_t *
branch_test(const svn_test_opts_t *opts,
            apr_pool_t *pool)
{
  apr_pool_t *spool = svn_pool_create(pool);
  svn_fs_t *fs;
  svn_fs_txn_t *txn;
  svn_fs_root_t *txn_root, *rev_root;
  svn_revnum_t youngest_rev = 0;

  /* Create a filesystem and repository. */
  SVN_ERR(svn_test__create_fs(&fs, "test-repo-branch",
                              opts, pool));

  /*** Revision 1:  Create the greek tree in revision.  ***/
  SVN_ERR(svn_fs_begin_txn(&txn, fs, youngest_rev, spool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, spool));
  SVN_ERR(svn_test__create_greek_tree(txn_root, spool));
  SVN_ERR(svn_fs_commit_txn(NULL, &youngest_rev, txn, spool));
  SVN_TEST_ASSERT(SVN_IS_VALID_REVNUM(youngest_rev));
  svn_pool_clear(spool);

  /*** Revision 2:  Copy A/D/G/rho to A/D/G/rho2.  ***/
  SVN_ERR(svn_fs_begin_txn(&txn, fs, youngest_rev, spool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, spool));
  SVN_ERR(svn_fs_revision_root(&rev_root, fs, youngest_rev, spool));
  SVN_ERR(svn_fs_copy(rev_root, "A/D/G/rho", txn_root, "A/D/G/rho2", spool));
  SVN_ERR(svn_fs_commit_txn(NULL, &youngest_rev, txn, spool));
  SVN_TEST_ASSERT(SVN_IS_VALID_REVNUM(youngest_rev));
  svn_pool_clear(spool);

  /*** Revision 3:  Copy A/D/G to A/D/G2.  ***/
  SVN_ERR(svn_fs_begin_txn(&txn, fs, youngest_rev, spool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, spool));
  SVN_ERR(svn_fs_revision_root(&rev_root, fs, youngest_rev, spool));
  SVN_ERR(svn_fs_copy(rev_root, "A/D/G", txn_root, "A/D/G2", spool));
  SVN_ERR(svn_fs_commit_txn(NULL, &youngest_rev, txn, spool));
  SVN_TEST_ASSERT(SVN_IS_VALID_REVNUM(youngest_rev));
  svn_pool_clear(spool);

  /*** Revision 4:  Copy A/D to A/D2.  ***/
  SVN_ERR(svn_fs_begin_txn(&txn, fs, youngest_rev, spool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, spool));
  SVN_ERR(svn_fs_revision_root(&rev_root, fs, youngest_rev, spool));
  SVN_ERR(svn_fs_copy(rev_root, "A/D", txn_root, "A/D2", spool));
  SVN_ERR(svn_fs_commit_txn(NULL, &youngest_rev, txn, spool));
  SVN_TEST_ASSERT(SVN_IS_VALID_REVNUM(youngest_rev));
  svn_pool_clear(spool);

  /*** Revision 5:  Edit all the rho's! ***/
  SVN_ERR(svn_fs_begin_txn(&txn, fs, youngest_rev, spool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, spool));
  SVN_ERR(svn_fs_revision_root(&rev_root, fs, youngest_rev, spool));
  SVN_ERR(svn_test__set_file_contents(txn_root, "A/D/G/rho",
                                      "Edited text.", spool));
  SVN_ERR(svn_test__set_file_contents(txn_root, "A/D/G/rho2",
                                      "Edited text.", spool));
  SVN_ERR(svn_test__set_file_contents(txn_root, "A/D/G2/rho",
                                      "Edited text.", spool));
  SVN_ERR(svn_test__set_file_contents(txn_root, "A/D/G2/rho2",
                                      "Edited text.", spool));
  SVN_ERR(svn_test__set_file_contents(txn_root, "A/D2/G/rho",
                                      "Edited text.", spool));
  SVN_ERR(svn_test__set_file_contents(txn_root, "A/D2/G/rho2",
                                      "Edited text.", spool));
  SVN_ERR(svn_test__set_file_contents(txn_root, "A/D2/G2/rho",
                                      "Edited text.", spool));
  SVN_ERR(svn_test__set_file_contents(txn_root, "A/D2/G2/rho2",
                                      "Edited text.", spool));
  SVN_ERR(svn_fs_commit_txn(NULL, &youngest_rev, txn, spool));
  SVN_TEST_ASSERT(SVN_IS_VALID_REVNUM(youngest_rev));

  svn_pool_destroy(spool);

  return SVN_NO_ERROR;
}


/* Verify that file FILENAME under ROOT has the same contents checksum
 * as CONTENTS when comparing the checksums of the given TYPE.
 * Use POOL for temporary allocations. */
static svn_error_t *
verify_file_checksum(svn_stringbuf_t *contents,
                     svn_fs_root_t *root,
                     const char *filename,
                     svn_checksum_kind_t type,
                     apr_pool_t *pool)
{
  svn_checksum_t *expected_checksum, *actual_checksum;

  /* Write a file, compare the repository's idea of its checksum
     against our idea of its checksum.  They should be the same. */
  SVN_ERR(svn_checksum(&expected_checksum, type, contents->data,
                       contents->len, pool));
  SVN_ERR(svn_fs_file_checksum(&actual_checksum, type, root, filename, TRUE,
                               pool));
  if (!svn_checksum_match(expected_checksum, actual_checksum))
    return svn_error_createf
      (SVN_ERR_FS_GENERAL, NULL,
       "verify-checksum: checksum mismatch:\n"
       "   expected:  %s\n"
       "     actual:  %s\n",
       svn_checksum_to_cstring(expected_checksum, pool),
       svn_checksum_to_cstring(actual_checksum, pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
verify_checksum(const svn_test_opts_t *opts,
                apr_pool_t *pool)
{
  svn_fs_t *fs;
  svn_fs_txn_t *txn;
  svn_fs_root_t *txn_root, *rev_root;
  svn_stringbuf_t *str;
  svn_revnum_t rev;

  /* Write a file, compare the repository's idea of its checksum
     against our idea of its checksum.  They should be the same. */
  str = svn_stringbuf_create("My text editor charges me rent.", pool);

  SVN_ERR(svn_test__create_fs(&fs, "test-repo-verify-checksum",
                              opts, pool));
  SVN_ERR(svn_fs_begin_txn(&txn, fs, 0, pool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, pool));
  SVN_ERR(svn_fs_make_file(txn_root, "fact", pool));
  SVN_ERR(svn_test__set_file_contents(txn_root, "fact", str->data, pool));

  /* Do it for the txn. */
  SVN_ERR(verify_file_checksum(str, txn_root, "fact", svn_checksum_md5,
                               pool));
  SVN_ERR(verify_file_checksum(str, txn_root, "fact", svn_checksum_sha1,
                               pool));

  /* Do it again - this time for the revision. */
  SVN_ERR(svn_fs_commit_txn(NULL, &rev, txn, pool));
  SVN_ERR(svn_fs_revision_root(&rev_root, fs, rev, pool));
  SVN_ERR(verify_file_checksum(str, rev_root, "fact", svn_checksum_md5,
                               pool));
  SVN_ERR(verify_file_checksum(str, rev_root, "fact", svn_checksum_sha1,
                               pool));

  return SVN_NO_ERROR;
}


/* Helper for closest_copy_test().  Verify that CLOSEST_PATH and the
   revision associated with CLOSEST_ROOT match the EXPECTED_PATH and
   EXPECTED_REVISION, respectively. */
static svn_error_t *
test_closest_copy_pair(svn_fs_root_t *closest_root,
                       const char *closest_path,
                       svn_revnum_t expected_revision,
                       const char *expected_path)
{
  svn_revnum_t closest_rev = SVN_INVALID_REVNUM;

  /* Callers must pass valid -- EXPECTED_PATH and EXPECTED_REVISION
     come as a both-or-nothing pair. */
  assert(((! expected_path) && (! SVN_IS_VALID_REVNUM(expected_revision)))
         || (expected_path && SVN_IS_VALID_REVNUM(expected_revision)));

  /* CLOSEST_PATH and CLOSEST_ROOT come as a both-or-nothing pair, too. */
  if (closest_path && (! closest_root))
    return svn_error_create(SVN_ERR_FS_GENERAL, NULL,
                            "got closest path but no closest root");
  if ((! closest_path) && closest_root)
    return svn_error_create(SVN_ERR_FS_GENERAL, NULL,
                            "got closest root but no closest path");

  /* Now that our pairs are known sane, we can compare them. */
  if (closest_path && (! expected_path))
    return svn_error_createf(SVN_ERR_FS_GENERAL, NULL,
                             "got closest path ('%s') when none expected",
                             closest_path);
  if ((! closest_path) && expected_path)
    return svn_error_createf(SVN_ERR_FS_GENERAL, NULL,
                             "got no closest path; expected '%s'",
                             expected_path);
  if (closest_path && (strcmp(closest_path, expected_path) != 0))
    return svn_error_createf(SVN_ERR_FS_GENERAL, NULL,
                             "got a different closest path than expected:\n"
                             "   expected:  %s\n"
                             "     actual:  %s",
                             expected_path, closest_path);
  if (closest_root)
    closest_rev = svn_fs_revision_root_revision(closest_root);
  if (closest_rev != expected_revision)
    return svn_error_createf(SVN_ERR_FS_GENERAL, NULL,
                             "got a different closest rev than expected:\n"
                             "   expected:  %ld\n"
                             "     actual:  %ld",
                             expected_revision, closest_rev);

  return SVN_NO_ERROR;
}


static svn_error_t *
closest_copy_test(const svn_test_opts_t *opts,
                  apr_pool_t *pool)
{
  svn_fs_t *fs;
  svn_fs_txn_t *txn;
  svn_fs_root_t *txn_root, *rev_root, *croot;
  svn_revnum_t after_rev;
  const char *cpath;
  apr_pool_t *spool = svn_pool_create(pool);

  /* Prepare a filesystem. */
  SVN_ERR(svn_test__create_fs(&fs, "test-repo-closest-copy",
                              opts, pool));

  /* In first txn, create and commit the greek tree. */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, 0, spool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, spool));
  SVN_ERR(svn_test__create_greek_tree(txn_root, spool));
  SVN_ERR(test_commit_txn(&after_rev, txn, NULL, spool));
  svn_pool_clear(spool);
  SVN_ERR(svn_fs_revision_root(&rev_root, fs, after_rev, spool));

  /* Copy A to Z, and commit. */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, after_rev, spool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, spool));
  SVN_ERR(svn_fs_copy(rev_root, "A", txn_root, "Z", spool));
  SVN_ERR(test_commit_txn(&after_rev, txn, NULL, spool));
  svn_pool_clear(spool);
  SVN_ERR(svn_fs_revision_root(&rev_root, fs, after_rev, spool));

  /* Anything under Z should have a closest copy pair of ("/Z", 2), so
     we'll pick some spots to test.  Stuff under A should have no
     relevant closest copy. */
  SVN_ERR(svn_fs_closest_copy(&croot, &cpath, rev_root, "Z", spool));
  SVN_ERR(test_closest_copy_pair(croot, cpath, 2, "/Z"));
  SVN_ERR(svn_fs_closest_copy(&croot, &cpath, rev_root, "Z/D/G", spool));
  SVN_ERR(test_closest_copy_pair(croot, cpath, 2, "/Z"));
  SVN_ERR(svn_fs_closest_copy(&croot, &cpath, rev_root, "Z/mu", spool));
  SVN_ERR(test_closest_copy_pair(croot, cpath, 2, "/Z"));
  SVN_ERR(svn_fs_closest_copy(&croot, &cpath, rev_root, "Z/B/E/beta", spool));
  SVN_ERR(test_closest_copy_pair(croot, cpath, 2, "/Z"));
  SVN_ERR(svn_fs_closest_copy(&croot, &cpath, rev_root, "A", spool));
  SVN_ERR(test_closest_copy_pair(croot, cpath, SVN_INVALID_REVNUM, NULL));
  SVN_ERR(svn_fs_closest_copy(&croot, &cpath, rev_root, "A/D/G", spool));
  SVN_ERR(test_closest_copy_pair(croot, cpath, SVN_INVALID_REVNUM, NULL));
  SVN_ERR(svn_fs_closest_copy(&croot, &cpath, rev_root, "A/mu", spool));
  SVN_ERR(test_closest_copy_pair(croot, cpath, SVN_INVALID_REVNUM, NULL));
  SVN_ERR(svn_fs_closest_copy(&croot, &cpath, rev_root, "A/B/E/beta", spool));
  SVN_ERR(test_closest_copy_pair(croot, cpath, SVN_INVALID_REVNUM, NULL));

  /* Okay, so let's do some more stuff.  We'll edit Z/mu, copy A to
     Z2, copy A/D/H to Z2/D/H2, and edit Z2/D/H2/chi.  We'll also make
     new Z/t and Z2/D/H2/t files. */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, after_rev, spool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, spool));
  SVN_ERR(svn_test__set_file_contents(txn_root, "Z/mu",
                                      "Edited text.", spool));
  SVN_ERR(svn_fs_copy(rev_root, "A", txn_root, "Z2", spool));
  SVN_ERR(svn_fs_copy(rev_root, "A/D/H", txn_root, "Z2/D/H2", spool));
  SVN_ERR(svn_test__set_file_contents(txn_root, "Z2/D/H2/chi",
                                      "Edited text.", spool));
  SVN_ERR(svn_fs_make_file(txn_root, "Z/t", pool));
  SVN_ERR(svn_fs_make_file(txn_root, "Z2/D/H2/t", pool));
  SVN_ERR(test_commit_txn(&after_rev, txn, NULL, spool));
  svn_pool_clear(spool);
  SVN_ERR(svn_fs_revision_root(&rev_root, fs, after_rev, spool));

  /* Okay, just for kicks, let's modify Z2/D/H2/t.  Shouldn't affect
     its closest-copy-ness, right?  */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, after_rev, spool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, spool));
  SVN_ERR(svn_test__set_file_contents(txn_root, "Z2/D/H2/t",
                                      "Edited text.", spool));
  SVN_ERR(test_commit_txn(&after_rev, txn, NULL, spool));
  svn_pool_clear(spool);
  SVN_ERR(svn_fs_revision_root(&rev_root, fs, after_rev, spool));

  /* Now, we expect Z2/D/H2 to have a closest copy of ("/Z2/D/H2", 3)
     because of the deepest path rule.  We expected Z2/D to have a
     closest copy of ("/Z2", 3).  Z/mu should still have a closest
     copy of ("/Z", 2).  As for the two new files (Z/t and Z2/D/H2/t),
     neither should have a closest copy. */
  SVN_ERR(svn_fs_closest_copy(&croot, &cpath, rev_root, "A/mu", spool));
  SVN_ERR(test_closest_copy_pair(croot, cpath, SVN_INVALID_REVNUM, NULL));
  SVN_ERR(svn_fs_closest_copy(&croot, &cpath, rev_root, "Z/mu", spool));
  SVN_ERR(test_closest_copy_pair(croot, cpath, 2, "/Z"));
  SVN_ERR(svn_fs_closest_copy(&croot, &cpath, rev_root, "Z2/D/H2", spool));
  SVN_ERR(test_closest_copy_pair(croot, cpath, 3, "/Z2/D/H2"));
  SVN_ERR(svn_fs_closest_copy(&croot, &cpath, rev_root, "Z2/D", spool));
  SVN_ERR(test_closest_copy_pair(croot, cpath, 3, "/Z2"));
  SVN_ERR(svn_fs_closest_copy(&croot, &cpath, rev_root, "Z/t", spool));
  SVN_ERR(test_closest_copy_pair(croot, cpath, SVN_INVALID_REVNUM, NULL));
  SVN_ERR(svn_fs_closest_copy(&croot, &cpath, rev_root, "Z2/D/H2/t", spool));
  SVN_ERR(test_closest_copy_pair(croot, cpath, SVN_INVALID_REVNUM, NULL));

  return SVN_NO_ERROR;
}

static svn_error_t *
root_revisions(const svn_test_opts_t *opts,
               apr_pool_t *pool)
{
  svn_fs_t *fs;
  svn_fs_txn_t *txn;
  svn_fs_root_t *txn_root, *rev_root;
  svn_revnum_t after_rev, fetched_rev;
  apr_pool_t *spool = svn_pool_create(pool);

  /* Prepare a filesystem. */
  SVN_ERR(svn_test__create_fs(&fs, "test-repo-root-revisions",
                              opts, pool));

  /* In first txn, create and commit the greek tree. */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, 0, spool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, spool));
  SVN_ERR(svn_test__create_greek_tree(txn_root, spool));
  SVN_ERR(test_commit_txn(&after_rev, txn, NULL, spool));

  /* First, verify that a revision root based on our new revision
     reports the correct associated revision. */
  SVN_ERR(svn_fs_revision_root(&rev_root, fs, after_rev, spool));
  fetched_rev = svn_fs_revision_root_revision(rev_root);
  if (after_rev != fetched_rev)
    return svn_error_createf
      (SVN_ERR_TEST_FAILED, NULL,
       "expected revision '%d'; "
       "got '%d' from svn_fs_revision_root_revision(rev_root)",
       (int)after_rev, (int)fetched_rev);

  /* Then verify that we can't ask about the txn-base-rev from a
     revision root. */
  fetched_rev = svn_fs_txn_root_base_revision(rev_root);
  if (fetched_rev != SVN_INVALID_REVNUM)
    return svn_error_createf
      (SVN_ERR_TEST_FAILED, NULL,
       "expected SVN_INVALID_REVNUM; "
       "got '%d' from svn_fs_txn_root_base_revision(rev_root)",
       (int)fetched_rev);

  /* Now, create a second txn based on AFTER_REV. */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, after_rev, spool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, spool));

  /* Verify that it reports the right base revision. */
  fetched_rev = svn_fs_txn_root_base_revision(txn_root);
  if (after_rev != fetched_rev)
    return svn_error_createf
      (SVN_ERR_TEST_FAILED, NULL,
       "expected '%d'; "
       "got '%d' from svn_fs_txn_root_base_revision(txn_root)",
       (int)after_rev, (int)fetched_rev);

  /* Then verify that we can't ask about the rev-root-rev from a
     txn root. */
  fetched_rev = svn_fs_revision_root_revision(txn_root);
  if (fetched_rev != SVN_INVALID_REVNUM)
    return svn_error_createf
      (SVN_ERR_TEST_FAILED, NULL,
       "expected SVN_INVALID_REVNUM; "
       "got '%d' from svn_fs_revision_root_revision(txn_root)",
       (int)fetched_rev);

  return SVN_NO_ERROR;
}


static svn_error_t *
unordered_txn_dirprops(const svn_test_opts_t *opts,
                       apr_pool_t *pool)
{
  svn_fs_t *fs;
  svn_fs_txn_t *txn, *txn2;
  svn_fs_root_t *txn_root, *txn_root2;
  svn_string_t pval;
  svn_revnum_t new_rev, not_rev;
  svn_boolean_t is_bdb = strcmp(opts->fs_type, SVN_FS_TYPE_BDB) == 0;

  /* This is a regression test for issue #2751. */

  /* Prepare a filesystem. */
  SVN_ERR(svn_test__create_fs(&fs, "test-repo-unordered-txn-dirprops",
                              opts, pool));

  /* Create and commit the greek tree. */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, 0, pool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, pool));
  SVN_ERR(svn_test__create_greek_tree(txn_root, pool));
  SVN_ERR(test_commit_txn(&new_rev, txn, NULL, pool));

  /* Open two transactions */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, new_rev, pool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, pool));
  SVN_ERR(svn_fs_begin_txn(&txn2, fs, new_rev, pool));
  SVN_ERR(svn_fs_txn_root(&txn_root2, txn2, pool));

  /* Change a child file in one. */
  SVN_ERR(svn_test__set_file_contents(txn_root, "/A/B/E/alpha",
                                      "New contents", pool));

  /* Change dir props in the other.  (We're using svn:mergeinfo
     property just to make sure special handling logic for that
     property doesn't croak.) */
  SET_STR(&pval, "/A/C:1");
  SVN_ERR(svn_fs_change_node_prop(txn_root2, "/A/B", "svn:mergeinfo",
                                  &pval, pool));

  /* Commit the second one first. */
  SVN_ERR(test_commit_txn(&new_rev, txn2, NULL, pool));

  /* Then commit the first -- but expect a conflict due to the
     propchanges made by the other txn. */
  SVN_ERR(test_commit_txn(&not_rev, txn, "/A/B", pool));
  SVN_ERR(svn_fs_abort_txn(txn, pool));

  /* Now, let's try those in reverse.  Open two transactions */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, new_rev, pool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, pool));
  SVN_ERR(svn_fs_begin_txn(&txn2, fs, new_rev, pool));
  SVN_ERR(svn_fs_txn_root(&txn_root2, txn2, pool));

  /* Change a child file in one. */
  SVN_ERR(svn_test__set_file_contents(txn_root, "/A/B/E/alpha",
                                      "New contents", pool));

  /* Change dir props in the other. */
  SET_STR(&pval, "/A/C:1");
  SVN_ERR(svn_fs_change_node_prop(txn_root2, "/A/B", "svn:mergeinfo",
                                  &pval, pool));

  /* Commit the first one first. */
  SVN_ERR(test_commit_txn(&new_rev, txn, NULL, pool));

  /* Some backends are cleverer than others. */
  if (is_bdb)
    {
      /* Then commit the second -- but expect an conflict because the
         directory wasn't up-to-date, which is required for propchanges. */
      SVN_ERR(test_commit_txn(&not_rev, txn2, "/A/B", pool));
      SVN_ERR(svn_fs_abort_txn(txn2, pool));
    }
  else
    {
      /* Then commit the second -- there will be no conflict despite the
         directory being out-of-data because the properties as well as the
         directory structure (list of nodes) was up-to-date. */
      SVN_ERR(test_commit_txn(&not_rev, txn2, NULL, pool));
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
set_uuid(const svn_test_opts_t *opts,
         apr_pool_t *pool)
{
  svn_fs_t *fs;
  const char *fixed_uuid = svn_uuid_generate(pool);
  const char *fetched_uuid;

  /* Prepare a filesystem. */
  SVN_ERR(svn_test__create_fs(&fs, "test-repo-set-uuid",
                              opts, pool));

  /* Set the repository UUID to something fixed. */
  SVN_ERR(svn_fs_set_uuid(fs, fixed_uuid, pool));

  /* Make sure we get back what we set. */
  SVN_ERR(svn_fs_get_uuid(fs, &fetched_uuid, pool));
  if (strcmp(fixed_uuid, fetched_uuid) != 0)
    return svn_error_createf
      (SVN_ERR_TEST_FAILED, NULL, "expected UUID '%s'; got '%s'",
       fixed_uuid, fetched_uuid);

  /* Set the repository UUID to something new (and unknown). */
  SVN_ERR(svn_fs_set_uuid(fs, NULL, pool));

  /* Make sure we *don't* get back what we previously set (after all,
     this stuff is supposed to be universally unique!). */
  SVN_ERR(svn_fs_get_uuid(fs, &fetched_uuid, pool));
  if (strcmp(fixed_uuid, fetched_uuid) == 0)
    return svn_error_createf
      (SVN_ERR_TEST_FAILED, NULL,
       "expected something other than UUID '%s', but got that one",
       fixed_uuid);

  return SVN_NO_ERROR;
}

static svn_error_t *
node_origin_rev(const svn_test_opts_t *opts,
                apr_pool_t *pool)
{
  apr_pool_t *subpool = svn_pool_create(pool);
  svn_fs_t *fs;
  svn_fs_txn_t *txn;
  svn_fs_root_t *txn_root, *root;
  svn_revnum_t youngest_rev = 0;
  int i;

  struct path_rev_t {
    const char *path;
    svn_revnum_t rev;
  };

  /* Create the repository. */
  SVN_ERR(svn_test__create_fs(&fs, "test-repo-node-origin-rev",
                              opts, pool));

  /* Revision 1: Create the Greek tree.  */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, 0, subpool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, subpool));
  SVN_ERR(svn_test__create_greek_tree(txn_root, subpool));
  SVN_ERR(svn_fs_commit_txn(NULL, &youngest_rev, txn, subpool));
  SVN_TEST_ASSERT(SVN_IS_VALID_REVNUM(youngest_rev));
  svn_pool_clear(subpool);

  /* Revision 2: Modify A/D/H/chi and A/B/E/alpha.  */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, youngest_rev, subpool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, subpool));
  SVN_ERR(svn_test__set_file_contents(txn_root, "A/D/H/chi", "2", subpool));
  SVN_ERR(svn_test__set_file_contents(txn_root, "A/B/E/alpha", "2", subpool));
  SVN_ERR(svn_fs_commit_txn(NULL, &youngest_rev, txn, subpool));
  SVN_TEST_ASSERT(SVN_IS_VALID_REVNUM(youngest_rev));
  svn_pool_clear(subpool);

  /* Revision 3: Copy A/D to A/D2, and create A/D2/floop new.  */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, youngest_rev, subpool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, subpool));
  SVN_ERR(svn_fs_revision_root(&root, fs, youngest_rev, subpool));
  SVN_ERR(svn_fs_copy(root, "A/D", txn_root, "A/D2", subpool));
  SVN_ERR(svn_fs_make_file(txn_root, "A/D2/floop", subpool));
  SVN_ERR(svn_fs_commit_txn(NULL, &youngest_rev, txn, subpool));
  SVN_TEST_ASSERT(SVN_IS_VALID_REVNUM(youngest_rev));
  svn_pool_clear(subpool);

  /* Revision 4: Modify A/D/H/chi and A/D2/H/chi.  */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, youngest_rev, subpool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, subpool));
  SVN_ERR(svn_test__set_file_contents(txn_root, "A/D/H/chi", "4", subpool));
  SVN_ERR(svn_test__set_file_contents(txn_root, "A/D2/H/chi", "4", subpool));
  SVN_ERR(svn_fs_commit_txn(NULL, &youngest_rev, txn, subpool));
  SVN_TEST_ASSERT(SVN_IS_VALID_REVNUM(youngest_rev));
  svn_pool_clear(subpool);

  /* Revision 5: Delete A/D2/G, add A/B/E/alfalfa.  */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, youngest_rev, subpool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, subpool));
  SVN_ERR(svn_fs_delete(txn_root, "A/D2/G", subpool));
  SVN_ERR(svn_fs_make_file(txn_root, "A/B/E/alfalfa", subpool));
  SVN_ERR(svn_fs_commit_txn(NULL, &youngest_rev, txn, subpool));
  SVN_TEST_ASSERT(SVN_IS_VALID_REVNUM(youngest_rev));
  svn_pool_clear(subpool);

  /* Revision 6: Restore A/D2/G (from version 4).  */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, youngest_rev, subpool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, subpool));
  SVN_ERR(svn_fs_revision_root(&root, fs, 4, subpool));
  SVN_ERR(svn_fs_copy(root, "A/D2/G", txn_root, "A/D2/G", subpool));
  SVN_ERR(svn_fs_commit_txn(NULL, &youngest_rev, txn, subpool));
  SVN_TEST_ASSERT(SVN_IS_VALID_REVNUM(youngest_rev));
  svn_pool_clear(subpool);

  /* Revision 7: Move A/D2 to A/D (replacing it), Add a new file A/D2,
     and tweak A/D/floop.  */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, youngest_rev, subpool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, subpool));
  SVN_ERR(svn_fs_revision_root(&root, fs, youngest_rev, subpool));
  SVN_ERR(svn_fs_delete(txn_root, "A/D", subpool));
  SVN_ERR(svn_fs_copy(root, "A/D2", txn_root, "A/D", subpool));
  SVN_ERR(svn_fs_delete(txn_root, "A/D2", subpool));
  SVN_ERR(svn_fs_make_file(txn_root, "A/D2", subpool));
  SVN_ERR(svn_test__set_file_contents(txn_root, "A/D/floop", "7", subpool));
  SVN_ERR(svn_fs_commit_txn(NULL, &youngest_rev, txn, subpool));
  SVN_TEST_ASSERT(SVN_IS_VALID_REVNUM(youngest_rev));
  svn_pool_clear(subpool);

  /* Now test some origin revisions. */
  {
    struct path_rev_t pathrevs[5] = { { "A/D",             1 },
                                      { "A/D/floop",       3 },
                                      { "A/D2",            7 },
                                      { "iota",            1 },
                                      { "A/B/E/alfalfa",   5 } };

    SVN_ERR(svn_fs_revision_root(&root, fs, youngest_rev, pool));
    for (i = 0; i < (sizeof(pathrevs) / sizeof(struct path_rev_t)); i++)
      {
        struct path_rev_t path_rev = pathrevs[i];
        svn_revnum_t revision;
        SVN_ERR(svn_fs_node_origin_rev(&revision, root, path_rev.path, pool));
        if (path_rev.rev != revision)
          return svn_error_createf
            (SVN_ERR_TEST_FAILED, NULL,
             "expected origin revision of '%ld' for '%s'; got '%ld'",
             path_rev.rev, path_rev.path, revision);
      }
  }

  /* Also, we'll check a couple of queries into a transaction root. */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, youngest_rev, subpool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, subpool));
  SVN_ERR(svn_fs_make_file(txn_root, "bloop", subpool));
  SVN_ERR(svn_fs_make_dir(txn_root, "A/D/blarp", subpool));

  {
    struct path_rev_t pathrevs[6] = { { "A/D",             1 },
                                      { "A/D/floop",       3 },
                                      { "bloop",          -1 },
                                      { "A/D/blarp",      -1 },
                                      { "iota",            1 },
                                      { "A/B/E/alfalfa",   5 } };

    root = txn_root;
    for (i = 0; i < (sizeof(pathrevs) / sizeof(struct path_rev_t)); i++)
      {
        struct path_rev_t path_rev = pathrevs[i];
        svn_revnum_t revision;
        SVN_ERR(svn_fs_node_origin_rev(&revision, root, path_rev.path, pool));
        if (! SVN_IS_VALID_REVNUM(revision))
          revision = -1;
        if (path_rev.rev != revision)
          return svn_error_createf
            (SVN_ERR_TEST_FAILED, NULL,
             "expected origin revision of '%ld' for '%s'; got '%ld'",
             path_rev.rev, path_rev.path, revision);
      }
  }

  return SVN_NO_ERROR;
}


/* Helper: call svn_fs_history_location() and check the results. */
static svn_error_t *
check_history_location(const char *expected_path,
                       svn_revnum_t expected_revision,
                       svn_fs_history_t *history,
                       apr_pool_t *pool)
{
  const char *actual_path;
  svn_revnum_t actual_revision;

  SVN_ERR(svn_fs_history_location(&actual_path, &actual_revision,
                                  history, pool));

  /* Validate the location against our expectations. */
  if (actual_revision != expected_revision
      || (actual_path && expected_path && strcmp(actual_path, expected_path))
      || (actual_path != NULL) != (expected_path != NULL))
    {
      return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                               "svn_fs_history_location() failed:\n"
                               "  expected '%s@%ld'\n"
                               "     found '%s@%ld",
                               expected_path ? expected_path : "(null)",
                               expected_revision,
                               actual_path ? actual_path : "(null)",
                               actual_revision);
    }

  return SVN_NO_ERROR;
}

/* Test svn_fs_history_*(). */
static svn_error_t *
node_history(const svn_test_opts_t *opts,
             apr_pool_t *pool)
{
  svn_fs_t *fs;
  svn_fs_txn_t *txn;
  svn_fs_root_t *txn_root;
  svn_revnum_t after_rev;

  /* Prepare a txn to receive the greek tree. */
  SVN_ERR(svn_test__create_fs(&fs, "test-repo-node-history",
                              opts, pool));
  SVN_ERR(svn_fs_begin_txn(&txn, fs, 0, pool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, pool));

  /* Create and verify the greek tree. */
  SVN_ERR(svn_test__create_greek_tree(txn_root, pool));
  SVN_ERR(test_commit_txn(&after_rev, txn, NULL, pool));

  /* Make some changes, following copy_test() above. */

  /* r2: copy pi to pi2, with textmods. */
  {
    svn_fs_root_t *rev_root;

    SVN_ERR(svn_fs_revision_root(&rev_root, fs, after_rev, pool));
    SVN_ERR(svn_fs_begin_txn(&txn, fs, after_rev, pool));
    SVN_ERR(svn_fs_txn_root(&txn_root, txn, pool));
    SVN_ERR(svn_fs_copy(rev_root, "A/D/G/pi",
                        txn_root, "A/D/H/pi2",
                        pool));
    SVN_ERR(svn_test__set_file_contents
            (txn_root, "A/D/H/pi2", "This is the file 'pi2'.\n", pool));
    SVN_ERR(test_commit_txn(&after_rev, txn, NULL, pool));
  }

  /* Go back in history: pi2@r2 -> pi@r1 */
  {
    svn_fs_history_t *history;
    svn_fs_root_t *rev_root;

    SVN_ERR(svn_fs_revision_root(&rev_root, fs, after_rev, pool));

    /* Fetch a history object, and walk it until its start. */

    SVN_ERR(svn_fs_node_history(&history, rev_root, "A/D/H/pi2", pool));
    SVN_ERR(check_history_location("/A/D/H/pi2", 2, history, pool));

    SVN_ERR(svn_fs_history_prev(&history, history, TRUE, pool));
    SVN_ERR(check_history_location("/A/D/H/pi2", 2, history, pool));

    SVN_ERR(svn_fs_history_prev(&history, history, TRUE, pool));
    SVN_ERR(check_history_location("/A/D/G/pi", 1, history, pool));

    SVN_ERR(svn_fs_history_prev(&history, history, TRUE, pool));
    SVN_TEST_ASSERT(history == NULL);
  }

  return SVN_NO_ERROR;
}

/* Test svn_fs_delete_fs(). */
static svn_error_t *
delete_fs(const svn_test_opts_t *opts,
             apr_pool_t *pool)
{
  const char *path;
  svn_node_kind_t kind;

  /* We have to use a subpool to close the svn_fs_t before calling
     svn_fs_delete_fs.  See issue 4264. */
  {
    svn_fs_t *fs;
    apr_pool_t *subpool = svn_pool_create(pool);
    SVN_ERR(svn_test__create_fs(&fs, "test-repo-delete-fs", opts, subpool));
    path = svn_fs_path(fs, pool);
    svn_pool_destroy(subpool);
  }

  SVN_ERR(svn_io_check_path(path, &kind, pool));
  SVN_TEST_ASSERT(kind != svn_node_none);
  SVN_ERR(svn_fs_delete_fs(path, pool));
  SVN_ERR(svn_io_check_path(path, &kind, pool));
  SVN_TEST_ASSERT(kind == svn_node_none);

  /* Recreate dir so that test cleanup doesn't fail. */
  SVN_ERR(svn_io_dir_make(path, APR_OS_DEFAULT, pool));

  return SVN_NO_ERROR;
}

/* Issue 4340, "filenames containing \n corrupt FSFS repositories" */
static svn_error_t *
filename_trailing_newline(const svn_test_opts_t *opts,
                          apr_pool_t *pool)
{
  apr_pool_t *subpool = svn_pool_create(pool);
  svn_fs_t *fs;
  svn_fs_txn_t *txn;
  svn_fs_root_t *txn_root, *root;
  svn_revnum_t youngest_rev = 0;
  svn_error_t *err;

  /* The FS API wants \n to be permitted, but FSFS never implemented that.
   * Moreover, formats like svn:mergeinfo and svn:externals don't support
   * it either.  So, we can't have newlines in file names in any FS.
   */
  SVN_ERR(svn_test__create_fs(&fs, "test-repo-filename-trailing-newline",
                              opts, pool));

  /* Revision 1:  Add a directory /foo  */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, youngest_rev, subpool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, subpool));
  SVN_ERR(svn_fs_make_dir(txn_root, "/foo", subpool));
  SVN_ERR(svn_fs_commit_txn(NULL, &youngest_rev, txn, subpool));
  SVN_TEST_ASSERT(SVN_IS_VALID_REVNUM(youngest_rev));
  svn_pool_clear(subpool);

  /* Attempt to copy /foo to "/bar\n". This should fail. */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, youngest_rev, subpool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, subpool));
  SVN_ERR(svn_fs_revision_root(&root, fs, youngest_rev, subpool));
  err = svn_fs_copy(root, "/foo", txn_root, "/bar\n", subpool);
  SVN_TEST_ASSERT_ERROR(err, SVN_ERR_FS_PATH_SYNTAX);

  /* Attempt to create a file /foo/baz\n. This should fail. */
  err = svn_fs_make_file(txn_root, "/foo/baz\n", subpool);
  SVN_TEST_ASSERT_ERROR(err, SVN_ERR_FS_PATH_SYNTAX);

  /* Attempt to create a directory /foo/bang\n. This should fail. */
  err = svn_fs_make_dir(txn_root, "/foo/bang\n", subpool);
  SVN_TEST_ASSERT_ERROR(err, SVN_ERR_FS_PATH_SYNTAX);

  return SVN_NO_ERROR;
}

static svn_error_t *
test_fs_info_format(const svn_test_opts_t *opts,
                    apr_pool_t *pool)
{
  svn_fs_t *fs;
  int fs_format;
  svn_version_t *supports_version;
  svn_version_t v1_5_0 = {1, 5, 0, ""};
  svn_version_t v1_10_0 = {1, 10, 0, ""};
  svn_test_opts_t opts2;
  svn_boolean_t is_fsx = strcmp(opts->fs_type, "fsx") == 0;

  opts2 = *opts;
  opts2.server_minor_version = is_fsx ? 10 : 5;

  SVN_ERR(svn_test__create_fs(&fs, "test-repo-fs-format-info", &opts2, pool));
  SVN_ERR(svn_fs_info_format(&fs_format, &supports_version, fs, pool, pool));

  if (is_fsx)
    {
      SVN_TEST_ASSERT(fs_format == 2);
      SVN_TEST_ASSERT(svn_ver_equal(supports_version, &v1_10_0));
    }
  else
    {
       /* happens to be the same for FSFS and BDB */
      SVN_TEST_ASSERT(fs_format == 3);
      SVN_TEST_ASSERT(svn_ver_equal(supports_version, &v1_5_0));
    }

  return SVN_NO_ERROR;
}

/* Sleeps until apr_time_now() value changes. */
static void sleep_for_timestamps(void)
{
  apr_time_t start = apr_time_now();

  while (start == apr_time_now())
    {
      apr_sleep(APR_USEC_PER_SEC / 1000);
    }
}

static svn_error_t *
commit_timestamp(const svn_test_opts_t *opts,
                 apr_pool_t *pool)
{
  svn_fs_t *fs;
  svn_fs_txn_t *txn;
  svn_fs_root_t *txn_root;
  svn_string_t *date = svn_string_create("Yesterday", pool);
  svn_revnum_t rev = 0;
  apr_hash_t *proplist;
  svn_string_t *svn_date;
  svn_string_t *txn_svn_date;

  SVN_ERR(svn_test__create_fs(&fs, "test-repo-fs-commit-timestamp",
                              opts, pool));

  /* Commit with a specified svn:date. */
  SVN_ERR(svn_fs_begin_txn2(&txn, fs, rev, SVN_FS_TXN_CLIENT_DATE, pool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, pool));
  SVN_ERR(svn_fs_make_dir(txn_root, "/foo", pool));
  SVN_ERR(svn_fs_change_txn_prop(txn, SVN_PROP_REVISION_DATE, date, pool));
  SVN_ERR(svn_fs_commit_txn(NULL, &rev, txn, pool));

  SVN_ERR(svn_fs_revision_proplist(&proplist, fs, rev, pool));
  svn_date = apr_hash_get(proplist, SVN_PROP_REVISION_DATE,
                          APR_HASH_KEY_STRING);
  SVN_TEST_ASSERT(svn_date && !strcmp(svn_date->data, date->data));

  /* Commit that overwrites the specified svn:date. */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, rev, pool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, pool));
  SVN_ERR(svn_fs_make_dir(txn_root, "/bar", pool));
  SVN_ERR(svn_fs_change_txn_prop(txn, SVN_PROP_REVISION_DATE, date, pool));
  SVN_ERR(svn_fs_commit_txn(NULL, &rev, txn, pool));

  SVN_ERR(svn_fs_revision_proplist(&proplist, fs, rev, pool));
  svn_date = apr_hash_get(proplist, SVN_PROP_REVISION_DATE,
                          APR_HASH_KEY_STRING);
  SVN_TEST_ASSERT(svn_date && strcmp(svn_date->data, date->data));

  /* Commit with a missing svn:date. */
  SVN_ERR(svn_fs_begin_txn2(&txn, fs, rev, SVN_FS_TXN_CLIENT_DATE, pool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, pool));
  SVN_ERR(svn_fs_make_dir(txn_root, "/zag", pool));
  SVN_ERR(svn_fs_change_txn_prop(txn, SVN_PROP_REVISION_DATE, NULL, pool));
  SVN_ERR(svn_fs_txn_prop(&svn_date, txn, SVN_PROP_REVISION_DATE, pool));
  SVN_TEST_ASSERT(!svn_date);
  SVN_ERR(svn_fs_commit_txn(NULL, &rev, txn, pool));

  SVN_ERR(svn_fs_revision_proplist(&proplist, fs, rev, pool));
  svn_date = apr_hash_get(proplist, SVN_PROP_REVISION_DATE,
                          APR_HASH_KEY_STRING);
  SVN_TEST_ASSERT(!svn_date);

  /* Commit that overwites a missing svn:date. */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, rev, pool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, pool));
  SVN_ERR(svn_fs_make_dir(txn_root, "/zig", pool));
  SVN_ERR(svn_fs_change_txn_prop(txn, SVN_PROP_REVISION_DATE, NULL, pool));
  SVN_ERR(svn_fs_txn_prop(&svn_date, txn, SVN_PROP_REVISION_DATE, pool));
  SVN_TEST_ASSERT(!svn_date);
  SVN_ERR(svn_fs_commit_txn(NULL, &rev, txn, pool));

  SVN_ERR(svn_fs_revision_proplist(&proplist, fs, rev, pool));
  svn_date = apr_hash_get(proplist, SVN_PROP_REVISION_DATE,
                          APR_HASH_KEY_STRING);
  SVN_TEST_ASSERT(svn_date);

  /* Commit that doesn't do anything special about svn:date. */
  SVN_ERR(svn_fs_begin_txn2(&txn, fs, rev, 0, pool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, pool));
  SVN_ERR(svn_fs_make_dir(txn_root, "/zig/foo", pool));
  SVN_ERR(svn_fs_txn_prop(&txn_svn_date, txn, SVN_PROP_REVISION_DATE, pool));
  SVN_TEST_ASSERT(txn_svn_date);
  sleep_for_timestamps();
  SVN_ERR(svn_fs_commit_txn(NULL, &rev, txn, pool));

  SVN_ERR(svn_fs_revision_proplist(&proplist, fs, rev, pool));
  svn_date = apr_hash_get(proplist, SVN_PROP_REVISION_DATE,
                          APR_HASH_KEY_STRING);
  SVN_TEST_ASSERT(svn_date);
  SVN_TEST_ASSERT(!svn_string_compare(svn_date, txn_svn_date));

  /* Commit that instructs the backend to use a specific svn:date, but
   * doesn't provide one.  This used to fail with BDB prior to r1663697. */
  SVN_ERR(svn_fs_begin_txn2(&txn, fs, rev, SVN_FS_TXN_CLIENT_DATE, pool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, pool));
  SVN_ERR(svn_fs_make_dir(txn_root, "/zig/bar", pool));
  SVN_ERR(svn_fs_txn_prop(&txn_svn_date, txn, SVN_PROP_REVISION_DATE, pool));
  SVN_TEST_ASSERT(txn_svn_date);
  sleep_for_timestamps();
  SVN_ERR(svn_fs_commit_txn(NULL, &rev, txn, pool));

  SVN_ERR(svn_fs_revision_proplist(&proplist, fs, rev, pool));
  svn_date = apr_hash_get(proplist, SVN_PROP_REVISION_DATE,
                          APR_HASH_KEY_STRING);
  SVN_TEST_ASSERT(svn_date);
  SVN_TEST_ASSERT(!svn_string_compare(svn_date, txn_svn_date));

  return SVN_NO_ERROR;
}

static svn_error_t *
test_compat_version(const svn_test_opts_t *opts,
                    apr_pool_t *pool)
{
  svn_version_t *compatible_version;
  apr_hash_t *config = apr_hash_make(pool);

  svn_version_t vcurrent = {SVN_VER_MAJOR, SVN_VER_MINOR, 0, ""};
  svn_version_t v1_2_0 = {1, 2, 0, ""};
  svn_version_t v1_3_0 = {1, 3, 0, ""};
  svn_version_t v1_5_0 = {1, 5, 0, ""};

  /* no version specified -> default to the current one */
  SVN_ERR(svn_fs__compatible_version(&compatible_version, config, pool));
  SVN_TEST_ASSERT(svn_ver_equal(compatible_version, &vcurrent));

  /* test specific compat option */
  svn_hash_sets(config, SVN_FS_CONFIG_PRE_1_6_COMPATIBLE, "1");
  SVN_ERR(svn_fs__compatible_version(&compatible_version, config, pool));
  SVN_TEST_ASSERT(svn_ver_equal(compatible_version, &v1_5_0));

  /* test precedence amongst compat options */
  svn_hash_sets(config, SVN_FS_CONFIG_PRE_1_8_COMPATIBLE, "1");
  SVN_ERR(svn_fs__compatible_version(&compatible_version, config, pool));
  SVN_TEST_ASSERT(svn_ver_equal(compatible_version, &v1_5_0));

  svn_hash_sets(config, SVN_FS_CONFIG_PRE_1_4_COMPATIBLE, "1");
  SVN_ERR(svn_fs__compatible_version(&compatible_version, config, pool));
  SVN_TEST_ASSERT(svn_ver_equal(compatible_version, &v1_3_0));

  /* precedence should work with the generic option as well */
  svn_hash_sets(config, SVN_FS_CONFIG_COMPATIBLE_VERSION, "1.4.17-??");
  SVN_ERR(svn_fs__compatible_version(&compatible_version, config, pool));
  SVN_TEST_ASSERT(svn_ver_equal(compatible_version, &v1_3_0));

  svn_hash_sets(config, SVN_FS_CONFIG_COMPATIBLE_VERSION, "1.2.3-no!");
  SVN_ERR(svn_fs__compatible_version(&compatible_version, config, pool));
  SVN_TEST_ASSERT(svn_ver_equal(compatible_version, &v1_2_0));

  /* test generic option alone */
  config = apr_hash_make(pool);
  svn_hash_sets(config, SVN_FS_CONFIG_COMPATIBLE_VERSION, "1.2.3-no!");
  SVN_ERR(svn_fs__compatible_version(&compatible_version, config, pool));
  SVN_TEST_ASSERT(svn_ver_equal(compatible_version, &v1_2_0));

  /* out of range values should be caped by the current tool version */
  svn_hash_sets(config, SVN_FS_CONFIG_COMPATIBLE_VERSION, "2.3.4-x");
  SVN_ERR(svn_fs__compatible_version(&compatible_version, config, pool));
  SVN_TEST_ASSERT(svn_ver_equal(compatible_version, &vcurrent));

  return SVN_NO_ERROR;
}

static svn_error_t *
dir_prop_merge(const svn_test_opts_t *opts,
               apr_pool_t *pool)
{
  svn_fs_t *fs;
  svn_revnum_t head_rev;
  svn_fs_root_t *root;
  svn_fs_txn_t *txn, *mid_txn, *top_txn, *sub_txn, *c_txn;
  svn_boolean_t is_bdb = strcmp(opts->fs_type, SVN_FS_TYPE_BDB) == 0;

  /* Create test repository. */
  SVN_ERR(svn_test__create_fs(&fs, "test-repo-fs-dir_prop-merge", opts,
                              pool));

  SVN_ERR(svn_fs_begin_txn(&txn, fs, 0, pool));
  SVN_ERR(svn_fs_txn_root(&root, txn, pool));

  /* Create and verify the greek tree. */
  SVN_ERR(svn_test__create_greek_tree(root, pool));
  SVN_ERR(test_commit_txn(&head_rev, txn, NULL, pool));

  /* Start concurrent transactions */

  /* 1st: modify a mid-level directory */
  SVN_ERR(svn_fs_begin_txn2(&mid_txn, fs, head_rev, 0, pool));
  SVN_ERR(svn_fs_txn_root(&root, mid_txn, pool));
  SVN_ERR(svn_fs_change_node_prop(root, "A/D", "test-prop",
                                  svn_string_create("val1", pool), pool));
  svn_fs_close_root(root);

  /* 2st: modify a top-level directory */
  SVN_ERR(svn_fs_begin_txn2(&top_txn, fs, head_rev, 0, pool));
  SVN_ERR(svn_fs_txn_root(&root, top_txn, pool));
  SVN_ERR(svn_fs_change_node_prop(root, "A", "test-prop",
                                  svn_string_create("val2", pool), pool));
  svn_fs_close_root(root);

  SVN_ERR(svn_fs_begin_txn2(&sub_txn, fs, head_rev, 0, pool));
  SVN_ERR(svn_fs_txn_root(&root, sub_txn, pool));
  SVN_ERR(svn_fs_change_node_prop(root, "A/D/G", "test-prop",
                                  svn_string_create("val3", pool), pool));
  svn_fs_close_root(root);

  /* 3rd: modify a conflicting change to the mid-level directory */
  SVN_ERR(svn_fs_begin_txn2(&c_txn, fs, head_rev, 0, pool));
  SVN_ERR(svn_fs_txn_root(&root, c_txn, pool));
  SVN_ERR(svn_fs_change_node_prop(root, "A/D", "test-prop",
                                  svn_string_create("valX", pool), pool));
  svn_fs_close_root(root);

  /* Prop changes to the same node should conflict */
  SVN_ERR(test_commit_txn(&head_rev, mid_txn, NULL, pool));
  SVN_ERR(test_commit_txn(&head_rev, c_txn, "/A/D", pool));
  SVN_ERR(svn_fs_abort_txn(c_txn, pool));

  /* Changes in a sub-tree should not conflict with prop changes to some
     parent directory but some backends are cleverer than others. */
  if (is_bdb)
    {
      SVN_ERR(test_commit_txn(&head_rev, top_txn, "/A", pool));
      SVN_ERR(svn_fs_abort_txn(top_txn, pool));
    }
  else
    {
      SVN_ERR(test_commit_txn(&head_rev, top_txn, NULL, pool));
    }

  /* The inverted case is not that trivial to handle.  Hence, conflict.
     Depending on the checking order, the reported conflict path differs. */
  SVN_ERR(test_commit_txn(&head_rev, sub_txn, is_bdb ? "/A/D" : "/A", pool));
  SVN_ERR(svn_fs_abort_txn(sub_txn, pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
upgrade_while_committing(const svn_test_opts_t *opts,
                         apr_pool_t *pool)
{
  svn_fs_t *fs;
  svn_revnum_t head_rev = 0;
  svn_fs_root_t *root;
  svn_fs_txn_t *txn1, *txn2;
  const char *fs_path;
  apr_hash_t *fs_config = apr_hash_make(pool);

  /* Bail (with success) on known-untestable scenarios */
  if (strcmp(opts->fs_type, "fsfs") != 0)
    return svn_error_create(SVN_ERR_TEST_SKIPPED, NULL,
                            "this will test FSFS repositories only");

  if (opts->server_minor_version && (opts->server_minor_version < 6))
    return svn_error_create(SVN_ERR_TEST_SKIPPED, NULL,
                            "pre-1.6 SVN doesn't support FSFS packing");

  /* Create test repository with greek tree. */
  fs_path = "test-repo-upgrade-while-committing";

  svn_hash_sets(fs_config, SVN_FS_CONFIG_COMPATIBLE_VERSION, "1.7");
  svn_hash_sets(fs_config, SVN_FS_CONFIG_FSFS_SHARD_SIZE, "2");
  SVN_ERR(svn_test__create_fs2(&fs, fs_path, opts, fs_config, pool));

  SVN_ERR(svn_fs_begin_txn(&txn1, fs, head_rev, pool));
  SVN_ERR(svn_fs_txn_root(&root, txn1, pool));
  SVN_ERR(svn_test__create_greek_tree(root, pool));
  SVN_ERR(test_commit_txn(&head_rev, txn1, NULL, pool));

  /* Create txn with changes. */
  SVN_ERR(svn_fs_begin_txn(&txn1, fs, head_rev, pool));
  SVN_ERR(svn_fs_txn_root(&root, txn1, pool));
  SVN_ERR(svn_fs_make_dir(root, "/foo", pool));

  /* Upgrade filesystem, but keep existing svn_fs_t object. */
  SVN_ERR(svn_fs_upgrade(fs_path, pool));

  /* Creating a new txn for the old svn_fs_t should not fail. */
  SVN_ERR(svn_fs_begin_txn(&txn2, fs, head_rev, pool));

  /* Committing the already existing txn should not fail. */
  SVN_ERR(test_commit_txn(&head_rev, txn1, NULL, pool));

  /* Verify filesystem content. */
  SVN_ERR(svn_fs_verify(fs_path, NULL, 0, SVN_INVALID_REVNUM, NULL, NULL,
                        NULL, NULL, pool));

  return SVN_NO_ERROR;
}

/* Utility method for test_paths_changed. Verify that REV in FS changes
 * exactly one path and that that change is a property change.  Expect
 * the MERGEINFO_MOD flag of the change to have the given value.
 */
static svn_error_t *
verify_root_prop_change(svn_fs_t *fs,
                        svn_revnum_t rev,
                        svn_tristate_t mergeinfo_mod,
                        apr_pool_t *pool)
{
  svn_fs_path_change2_t *change;
  svn_fs_root_t *root;
  apr_hash_t *changes;

  SVN_ERR(svn_fs_revision_root(&root, fs, rev, pool));
  SVN_ERR(svn_fs_paths_changed2(&changes, root, pool));
  SVN_TEST_ASSERT(apr_hash_count(changes) == 1);
  change = svn_hash_gets(changes, "/");

  SVN_TEST_ASSERT(change->node_rev_id);
  SVN_TEST_ASSERT(change->change_kind == svn_fs_path_change_modify);
  SVN_TEST_ASSERT(   change->node_kind == svn_node_dir
                  || change->node_kind == svn_node_unknown);
  SVN_TEST_ASSERT(change->text_mod == FALSE);
  SVN_TEST_ASSERT(change->prop_mod == TRUE);

  if (change->copyfrom_known)
    {
      SVN_TEST_ASSERT(change->copyfrom_rev == SVN_INVALID_REVNUM);
      SVN_TEST_ASSERT(change->copyfrom_path == NULL);
    }

  SVN_TEST_ASSERT(change->mergeinfo_mod == mergeinfo_mod);

  return SVN_NO_ERROR;
}

static svn_error_t *
test_paths_changed(const svn_test_opts_t *opts,
                   apr_pool_t *pool)
{
  svn_fs_t *fs;
  svn_revnum_t head_rev = 0;
  svn_fs_root_t *root;
  svn_fs_txn_t *txn;
  const char *fs_path;
  apr_hash_t *changes;
  svn_boolean_t has_mergeinfo_mod = FALSE;
  apr_hash_index_t *hi;
  int i;

  /* The "mergeinfo_mod flag will say "unknown" until recently. */
  if (   strcmp(opts->fs_type, SVN_FS_TYPE_BDB) != 0
      && (!opts->server_minor_version || (opts->server_minor_version >= 9)))
    has_mergeinfo_mod = TRUE;

  /* Create test repository with greek tree. */
  fs_path = "test-repo-paths-changed";

  SVN_ERR(svn_test__create_fs2(&fs, fs_path, opts, NULL, pool));

  SVN_ERR(svn_fs_begin_txn(&txn, fs, head_rev, pool));
  SVN_ERR(svn_fs_txn_root(&root, txn, pool));
  SVN_ERR(svn_test__create_greek_tree(root, pool));
  SVN_ERR(test_commit_txn(&head_rev, txn, NULL, pool));

  /* Create txns with various prop changes. */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, head_rev, pool));
  SVN_ERR(svn_fs_txn_root(&root, txn, pool));
  SVN_ERR(svn_fs_change_node_prop(root, "/", "propname",
                                  svn_string_create("propval", pool), pool));
  SVN_ERR(test_commit_txn(&head_rev, txn, NULL, pool));

  SVN_ERR(svn_fs_begin_txn(&txn, fs, head_rev, pool));
  SVN_ERR(svn_fs_txn_root(&root, txn, pool));
  SVN_ERR(svn_fs_change_node_prop(root, "/", "svn:mergeinfo",
                                  svn_string_create("/: 1\n", pool), pool));
  SVN_ERR(test_commit_txn(&head_rev, txn, NULL, pool));

  /* Verify changed path lists. */

  /* Greek tree creation rev. */
  SVN_ERR(svn_fs_revision_root(&root, fs, head_rev - 2, pool));
  SVN_ERR(svn_fs_paths_changed2(&changes, root, pool));

  /* Reports all paths? */
  for (i = 0; svn_test__greek_tree_nodes[i].path; ++i)
    {
      const char *path
        = svn_fspath__canonicalize(svn_test__greek_tree_nodes[i].path, pool);

      SVN_TEST_ASSERT(svn_hash_gets(changes, path));
    }

  SVN_TEST_ASSERT(apr_hash_count(changes) == i);

  /* Verify per-path info. */
  for (hi = apr_hash_first(pool, changes); hi; hi = apr_hash_next(hi))
    {
      svn_fs_path_change2_t *change = apr_hash_this_val(hi);

      SVN_TEST_ASSERT(change->node_rev_id);
      SVN_TEST_ASSERT(change->change_kind == svn_fs_path_change_add);
      SVN_TEST_ASSERT(   change->node_kind == svn_node_file
                      || change->node_kind == svn_node_dir
                      || change->node_kind == svn_node_unknown);

      if (change->node_kind != svn_node_unknown)
        SVN_TEST_ASSERT(change->text_mod == (   change->node_kind
                                             == svn_node_file));

      SVN_TEST_ASSERT(change->prop_mod == FALSE);

      if (change->copyfrom_known)
        {
          SVN_TEST_ASSERT(change->copyfrom_rev == SVN_INVALID_REVNUM);
          SVN_TEST_ASSERT(change->copyfrom_path == NULL);
        }

      if (has_mergeinfo_mod)
        SVN_TEST_ASSERT(change->mergeinfo_mod == svn_tristate_false);
      else
        SVN_TEST_ASSERT(change->mergeinfo_mod == svn_tristate_unknown);
    }

  /* Propset rev. */
  SVN_ERR(verify_root_prop_change(fs, head_rev - 1,
                                  has_mergeinfo_mod ? svn_tristate_false
                                                    : svn_tristate_unknown,
                                  pool));

  /* Mergeinfo set rev. */
  SVN_ERR(verify_root_prop_change(fs, head_rev,
                                  has_mergeinfo_mod ? svn_tristate_true
                                                    : svn_tristate_unknown,
                                  pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
test_delete_replaced_paths_changed(const svn_test_opts_t *opts,
                                   apr_pool_t *pool)
{
  svn_fs_t *fs;
  svn_revnum_t head_rev = 0;
  svn_fs_root_t *root;
  svn_fs_txn_t *txn;
  const char *fs_path;
  apr_hash_t *changes;
  svn_fs_path_change2_t *change;
  const svn_fs_id_t *file_id;

  /* Create test repository with greek tree. */
  fs_path = "test-repo-delete-replace-paths-changed";

  SVN_ERR(svn_test__create_fs2(&fs, fs_path, opts, NULL, pool));

  SVN_ERR(svn_fs_begin_txn(&txn, fs, head_rev, pool));
  SVN_ERR(svn_fs_txn_root(&root, txn, pool));
  SVN_ERR(svn_test__create_greek_tree(root, pool));
  SVN_ERR(test_commit_txn(&head_rev, txn, NULL, pool));

  /* Create that replaces a file with a folder and then deletes that
   * replacement.  Start with the deletion. */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, head_rev, pool));
  SVN_ERR(svn_fs_txn_root(&root, txn, pool));
  SVN_ERR(svn_fs_delete(root, "/iota", pool));

  /* The change list should now report a deleted file. */
  SVN_ERR(svn_fs_paths_changed2(&changes, root, pool));
  change = svn_hash_gets(changes, "/iota");
  file_id = change->node_rev_id;
  SVN_TEST_ASSERT(   change->node_kind == svn_node_file
                  || change->node_kind == svn_node_unknown);
  SVN_TEST_ASSERT(change->change_kind == svn_fs_path_change_delete);

  /* Add a replacement. */
  SVN_ERR(svn_fs_make_dir(root, "/iota", pool));

  /* The change list now reports a replacement by a directory. */
  SVN_ERR(svn_fs_paths_changed2(&changes, root, pool));
  change = svn_hash_gets(changes, "/iota");
  SVN_TEST_ASSERT(   change->node_kind == svn_node_dir
                  || change->node_kind == svn_node_unknown);
  SVN_TEST_ASSERT(change->change_kind == svn_fs_path_change_replace);
  SVN_TEST_ASSERT(svn_fs_compare_ids(change->node_rev_id, file_id) != 0);

  /* Delete the replacement again. */
  SVN_ERR(svn_fs_delete(root, "/iota", pool));

  /* The change list should now be reported as a deleted file again. */
  SVN_ERR(svn_fs_paths_changed2(&changes, root, pool));
  change = svn_hash_gets(changes, "/iota");
  SVN_TEST_ASSERT(   change->node_kind == svn_node_file
                  || change->node_kind == svn_node_unknown);
  SVN_TEST_ASSERT(change->change_kind == svn_fs_path_change_delete);
  SVN_TEST_ASSERT(svn_fs_compare_ids(change->node_rev_id, file_id) == 0);

  /* Finally, commit the change. */
  SVN_ERR(test_commit_txn(&head_rev, txn, NULL, pool));

  /* The committed revision should still report the same change. */
  SVN_ERR(svn_fs_revision_root(&root, fs, head_rev, pool));
  SVN_ERR(svn_fs_paths_changed2(&changes, root, pool));
  change = svn_hash_gets(changes, "/iota");
  SVN_TEST_ASSERT(   change->node_kind == svn_node_file
                  || change->node_kind == svn_node_unknown);
  SVN_TEST_ASSERT(change->change_kind == svn_fs_path_change_delete);

  return SVN_NO_ERROR;
}

/* Get rid of transaction NAME in FS.  This function deals with backend-
 * specific behavior as permitted by the API. */
static svn_error_t *
cleanup_txn(svn_fs_t *fs,
            const char *name,
            apr_pool_t *scratch_pool)
{
  /* Get rid of the txns one at a time. */
  svn_error_t *err = svn_fs_purge_txn(fs, name, scratch_pool);

  /* Some backends (BDB) don't support purging transactions that have never
   * seen an abort or commit attempt.   Simply abort those txns. */
  if (err && err->apr_err == SVN_ERR_FS_TRANSACTION_NOT_DEAD)
    {
      svn_fs_txn_t *txn;
      svn_error_clear(err);
      err = SVN_NO_ERROR;

      SVN_ERR(svn_fs_open_txn(&txn, fs, name, scratch_pool));
      SVN_ERR(svn_fs_abort_txn(txn, scratch_pool));

      /* Should be gone now ... */
      SVN_TEST_ASSERT_ERROR(svn_fs_open_txn(&txn, fs, name, scratch_pool),
                            SVN_ERR_FS_NO_SUCH_TRANSACTION);
    }

  return svn_error_trace(err);
}

/* Make sure we get txn lists correctly. */
static svn_error_t *
purge_txn_test(const svn_test_opts_t *opts,
               apr_pool_t *pool)
{
  svn_fs_t *fs;
  svn_fs_txn_t *txn;
  const char *name1, *name2;
  apr_array_header_t *txn_list;
  apr_pool_t *subpool = svn_pool_create(pool);

  SVN_ERR(svn_test__create_fs(&fs, "test-repo-purge-txn",
                              opts, pool));

  /* Begin a new transaction, get its name (in the top pool), close it.  */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, 0, subpool));
  SVN_ERR(svn_fs_txn_name(&name1, txn, pool));

  /* Begin *another* transaction, get its name (in the top pool), close it.  */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, 0, subpool));
  SVN_ERR(svn_fs_txn_name(&name2, txn, pool));
  svn_pool_clear(subpool);

  /* Get rid of the txns one at a time. */
  SVN_ERR(cleanup_txn(fs, name1, pool));

  /* There should be exactly one left. */
  SVN_ERR(svn_fs_list_transactions(&txn_list, fs, pool));

  /* Check the list. It should have *exactly* one entry. */
  SVN_TEST_ASSERT(   txn_list->nelts == 1
                  && !strcmp(name2, APR_ARRAY_IDX(txn_list, 0, const char *)));

  /* Get rid of the other txn as well. */
  SVN_ERR(cleanup_txn(fs, name2, pool));

  /* There should be exactly one left. */
  SVN_ERR(svn_fs_list_transactions(&txn_list, fs, pool));

  /* Check the list. It should have no entries. */
  SVN_TEST_ASSERT(txn_list->nelts == 0);

  return SVN_NO_ERROR;
}

/* Test svn_fs_{contents,props}_{different,changed}().
 * ### This currently only tests them on revision roots, not on txn roots.
 */
static svn_error_t *
compare_contents(const svn_test_opts_t *opts,
                 apr_pool_t *pool)
{
  svn_fs_t *fs;
  svn_fs_txn_t *txn;
  svn_fs_root_t *txn_root, *root1, *root2;
  const char *original = "original contents";
  svn_revnum_t rev;
  int i;
  apr_pool_t *iterpool = svn_pool_create(pool);
  svn_boolean_t changed;

  /* Two similar but different texts that yield the same MD5 digest. */
  const char *evil_text1
    = "\xd1\x31\xdd\x02\xc5\xe6\xee\xc4\x69\x3d\x9a\x06\x98\xaf\xf9\x5c"
      "\x2f\xca\xb5\x87\x12\x46\x7e\xab\x40\x04\x58\x3e\xb8\xfb\x7f\x89"
      "\x55\xad\x34\x06\x09\xf4\xb3\x02\x83\xe4\x88\x83\x25\x71\x41\x5a"
      "\x08\x51\x25\xe8\xf7\xcd\xc9\x9f\xd9\x1d\xbd\xf2\x80\x37\x3c\x5b"
      "\xd8\x82\x3e\x31\x56\x34\x8f\x5b\xae\x6d\xac\xd4\x36\xc9\x19\xc6"
      "\xdd\x53\xe2\xb4\x87\xda\x03\xfd\x02\x39\x63\x06\xd2\x48\xcd\xa0"
      "\xe9\x9f\x33\x42\x0f\x57\x7e\xe8\xce\x54\xb6\x70\x80\xa8\x0d\x1e"
      "\xc6\x98\x21\xbc\xb6\xa8\x83\x93\x96\xf9\x65\x2b\x6f\xf7\x2a\x70";
  const char *evil_text2
    = "\xd1\x31\xdd\x02\xc5\xe6\xee\xc4\x69\x3d\x9a\x06\x98\xaf\xf9\x5c"
      "\x2f\xca\xb5\x07\x12\x46\x7e\xab\x40\x04\x58\x3e\xb8\xfb\x7f\x89"
      "\x55\xad\x34\x06\x09\xf4\xb3\x02\x83\xe4\x88\x83\x25\xf1\x41\x5a"
      "\x08\x51\x25\xe8\xf7\xcd\xc9\x9f\xd9\x1d\xbd\x72\x80\x37\x3c\x5b"
      "\xd8\x82\x3e\x31\x56\x34\x8f\x5b\xae\x6d\xac\xd4\x36\xc9\x19\xc6"
      "\xdd\x53\xe2\x34\x87\xda\x03\xfd\x02\x39\x63\x06\xd2\x48\xcd\xa0"
      "\xe9\x9f\x33\x42\x0f\x57\x7e\xe8\xce\x54\xb6\x70\x80\x28\x0d\x1e"
      "\xc6\x98\x21\xbc\xb6\xa8\x83\x93\x96\xf9\x65\xab\x6f\xf7\x2a\x70";
  svn_checksum_t *checksum1, *checksum2;

  /* (path, rev) pairs to compare plus the expected API return values */
  struct
    {
      svn_revnum_t rev1;
      const char *path1;
      svn_revnum_t rev2;
      const char *path2;

      svn_boolean_t different;  /* result of svn_fs_*_different */
      svn_tristate_t changed;   /* result of svn_fs_*_changed */
    } to_compare[] =
    {
      /* same representation */
      { 1, "foo", 2, "foo", FALSE, svn_tristate_false },
      { 1, "foo", 2, "bar", FALSE, svn_tristate_false },
      { 2, "foo", 2, "bar", FALSE, svn_tristate_false },

      /* different content but MD5 check is not reliable */
      { 3, "foo", 3, "bar", TRUE, svn_tristate_true },

      /* different contents */
      { 1, "foo", 3, "bar", TRUE, svn_tristate_true },
      { 1, "foo", 3, "foo", TRUE, svn_tristate_true },
      { 3, "foo", 4, "bar", TRUE, svn_tristate_true },
      { 3, "foo", 4, "bar", TRUE, svn_tristate_true },
      { 2, "bar", 3, "bar", TRUE, svn_tristate_true },
      { 3, "bar", 4, "bar", TRUE, svn_tristate_true },

      /* variations on the same theme: same content, possibly different rep */
      { 4, "foo", 4, "bar", FALSE, svn_tristate_unknown },
      { 1, "foo", 4, "bar", FALSE, svn_tristate_unknown },
      { 2, "foo", 4, "bar", FALSE, svn_tristate_unknown },
      { 1, "foo", 4, "foo", FALSE, svn_tristate_unknown },
      { 2, "foo", 4, "foo", FALSE, svn_tristate_unknown },
      { 2, "bar", 4, "bar", FALSE, svn_tristate_unknown },

      /* EOL */
      { 0 },
    };

  /* Same same, but different.
   * Just checking that we actually have an MD5 collision. */
  SVN_ERR(svn_checksum(&checksum1, svn_checksum_md5, evil_text1,
                       strlen(evil_text1), pool));
  SVN_ERR(svn_checksum(&checksum2, svn_checksum_md5, evil_text2,
                       strlen(evil_text2), pool));
  SVN_TEST_ASSERT(svn_checksum_match(checksum1, checksum1));
  SVN_TEST_ASSERT(strcmp(evil_text1, evil_text2));

  /* Now, build up our test repo. */
  SVN_ERR(svn_test__create_fs(&fs, "test-repo-compare-contents",
                              opts, pool));

  /* Rev 1: create a file. */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, 0, iterpool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, iterpool));
  SVN_ERR(svn_fs_make_file(txn_root, "foo", iterpool));
  SVN_ERR(svn_test__set_file_contents(txn_root, "foo", original, iterpool));
  SVN_ERR(svn_fs_change_node_prop(txn_root, "foo", "prop",
                                  svn_string_create(original, iterpool),
                                  iterpool));
  SVN_ERR(svn_fs_commit_txn(NULL, &rev, txn, iterpool));
  SVN_TEST_ASSERT(rev == 1);
  svn_pool_clear(iterpool);

  /* Rev 2: copy that file. */
  SVN_ERR(svn_fs_revision_root(&root1, fs, rev, iterpool));
  SVN_ERR(svn_fs_begin_txn(&txn, fs, 0, iterpool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, iterpool));
  SVN_ERR(svn_fs_copy(root1, "foo", txn_root, "bar", iterpool));
  SVN_ERR(svn_fs_commit_txn(NULL, &rev, txn, iterpool));
  SVN_TEST_ASSERT(rev == 2);
  svn_pool_clear(iterpool);

  /* Rev 3: modify both files.
   * The new contents differs for both files but has the same length and MD5.
   */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, rev, iterpool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, iterpool));
  SVN_ERR(svn_test__set_file_contents(txn_root, "foo", evil_text1, iterpool));
  SVN_ERR(svn_test__set_file_contents(txn_root, "bar", evil_text2, iterpool));
  SVN_ERR(svn_fs_change_node_prop(txn_root, "foo", "prop",
                                  svn_string_create(evil_text1, iterpool),
                                  iterpool));
  SVN_ERR(svn_fs_change_node_prop(txn_root, "bar", "prop",
                                  svn_string_create(evil_text2, iterpool),
                                  iterpool));
  SVN_ERR(svn_fs_commit_txn(NULL, &rev, txn, iterpool));
  SVN_TEST_ASSERT(rev == 3);
  svn_pool_clear(iterpool);

  /* Rev 4: revert both file contents.
   */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, rev, iterpool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, iterpool));
  SVN_ERR(svn_test__set_file_contents(txn_root, "foo", original, iterpool));
  SVN_ERR(svn_test__set_file_contents(txn_root, "bar", original, iterpool));
  SVN_ERR(svn_fs_change_node_prop(txn_root, "foo", "prop",
                                  svn_string_create(original, iterpool),
                                  iterpool));
  SVN_ERR(svn_fs_change_node_prop(txn_root, "bar", "prop",
                                  svn_string_create(original, iterpool),
                                  iterpool));
  SVN_ERR(svn_fs_commit_txn(NULL, &rev, txn, iterpool));
  SVN_TEST_ASSERT(rev == 4);
  svn_pool_clear(iterpool);

  /* Perform all comparisons listed in TO_COMPARE. */
  for (i = 0; to_compare[i].rev1 > 0; ++i)
    {
      svn_boolean_t text_different;
      svn_boolean_t text_changed;
      svn_boolean_t props_different;
      svn_boolean_t props_changed;

      svn_pool_clear(iterpool);
      SVN_ERR(svn_fs_revision_root(&root1, fs, to_compare[i].rev1, iterpool));
      SVN_ERR(svn_fs_revision_root(&root2, fs, to_compare[i].rev2, iterpool));

      /* Compare node texts. */
      SVN_ERR(svn_fs_contents_different(&text_different,
                                        root1, to_compare[i].path1,
                                        root2, to_compare[i].path2,
                                        iterpool));
      SVN_ERR(svn_fs_contents_changed(&text_changed,
                                      root1, to_compare[i].path1,
                                      root2, to_compare[i].path2,
                                      iterpool));

      /* Compare properties. */
      SVN_ERR(svn_fs_props_different(&props_different,
                                     root1, to_compare[i].path1,
                                     root2, to_compare[i].path2,
                                     iterpool));
      SVN_ERR(svn_fs_props_changed(&props_changed,
                                   root1, to_compare[i].path1,
                                   root2, to_compare[i].path2,
                                   iterpool));

      /* Check results. */
      SVN_TEST_ASSERT(text_different == to_compare[i].different);
      SVN_TEST_ASSERT(props_different == to_compare[i].different);

      switch (to_compare[i].changed)
        {
        case svn_tristate_true:
          SVN_TEST_ASSERT(text_changed);
          SVN_TEST_ASSERT(props_changed);
          break;

        case svn_tristate_false:
          SVN_TEST_ASSERT(!text_changed);
          SVN_TEST_ASSERT(!props_changed);
          break;

        default:
          break;
        }
    }

  /* Check how svn_fs_contents_different() and svn_fs_contents_changed()
     handles invalid path.*/
  SVN_ERR(svn_fs_revision_root(&root1, fs, 1, iterpool));
  SVN_TEST_ASSERT_ANY_ERROR(
    svn_fs_contents_changed(&changed, root1, "/", root1, "/", iterpool));
  SVN_TEST_ASSERT_ANY_ERROR(
    svn_fs_contents_different(&changed, root1, "/", root1, "/", iterpool));

  SVN_TEST_ASSERT_ANY_ERROR(
    svn_fs_contents_changed(&changed, root1, "/non-existent", root1,
                            "/non-existent", iterpool));
  SVN_TEST_ASSERT_ANY_ERROR(
    svn_fs_contents_different(&changed, root1, "/non-existent", root1,
                              "/non-existent", iterpool));

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

static svn_error_t *
test_path_change_create(const svn_test_opts_t *opts,
                        apr_pool_t *pool)
{
  svn_fs_t *fs;
  svn_fs_root_t *root;
  const svn_fs_id_t *id;
  svn_fs_path_change2_t *change;

  /* Build an empty test repo ... */
  SVN_ERR(svn_test__create_fs(&fs, "test-repo-path-change-create",
                              opts, pool));

  /* ... just to give us a valid ID. */
  SVN_ERR(svn_fs_revision_root(&root, fs, 0, pool));
  SVN_ERR(svn_fs_node_id(&id, root, "", pool));

  /* Do what we came here for. */
  change = svn_fs_path_change2_create(id, svn_fs_path_change_replace, pool);

  SVN_TEST_ASSERT(change);
  SVN_TEST_ASSERT(change->node_rev_id == id);
  SVN_TEST_ASSERT(change->change_kind == svn_fs_path_change_replace);

  /* All other fields should be "empty" / "unused". */
  SVN_TEST_ASSERT(change->node_kind == svn_node_none);

  SVN_TEST_ASSERT(change->text_mod == FALSE);
  SVN_TEST_ASSERT(change->prop_mod == FALSE);
  SVN_TEST_ASSERT(change->mergeinfo_mod == svn_tristate_unknown);

  SVN_TEST_ASSERT(change->copyfrom_known == FALSE);
  SVN_TEST_ASSERT(change->copyfrom_rev == SVN_INVALID_REVNUM);
  SVN_TEST_ASSERT(change->copyfrom_path == NULL);

  return SVN_NO_ERROR;
}

static svn_error_t *
test_node_created_info(const svn_test_opts_t *opts,
                       apr_pool_t *pool)
{
  svn_fs_t *fs;
  svn_fs_txn_t *txn;
  svn_fs_root_t *txn_root, *root;
  svn_revnum_t rev;
  int i;
  apr_pool_t *iterpool = svn_pool_create(pool);

  /* Test vectors. */
  struct
    {
      svn_revnum_t rev;
      const char *path;
      svn_revnum_t crev;
      const char *cpath;
    } to_check[] =
    {
      /* New noderev only upon modification. */
      { 1, "A/B/E/beta",  1, "/A/B/E/beta" },
      { 2, "A/B/E/beta",  1, "/A/B/E/beta" },
      { 3, "A/B/E/beta",  3, "/A/B/E/beta" },
      { 4, "A/B/E/beta",  3, "/A/B/E/beta" },

      /* Lazily copied node. */
      { 2, "Z/B/E/beta",  1, "/A/B/E/beta" },
      { 3, "Z/B/E/beta",  1, "/A/B/E/beta" },
      { 4, "Z/B/E/beta",  4, "/Z/B/E/beta" },

      /* Bubble-up upon sub-tree change. */
      { 2, "Z",  2, "/Z" },
      { 3, "Z",  2, "/Z" },
      { 4, "Z",  4, "/Z" },

      { 0 }
    };

  /* Start with a new repo and the greek tree in rev 1. */
  SVN_ERR(svn_test__create_fs(&fs, "test-repo-node-created-path",
                              opts, pool));

  SVN_ERR(svn_fs_begin_txn(&txn, fs, 0, iterpool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, iterpool));
  SVN_ERR(svn_test__create_greek_tree(txn_root, iterpool));
  SVN_ERR(test_commit_txn(&rev, txn, NULL, iterpool));
  svn_pool_clear(iterpool);

  /* r2: copy a subtree */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, rev, iterpool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, iterpool));
  SVN_ERR(svn_fs_revision_root(&root, fs, rev, iterpool));
  SVN_ERR(svn_fs_copy(root, "A", txn_root, "Z", iterpool));
  SVN_ERR(test_commit_txn(&rev, txn, NULL, iterpool));
  svn_pool_clear(iterpool);

  /* r3: touch node in copy source */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, rev, iterpool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, iterpool));
  SVN_ERR(svn_test__set_file_contents(txn_root, "A/B/E/beta", "new", iterpool));
  SVN_ERR(test_commit_txn(&rev, txn, NULL, iterpool));
  svn_pool_clear(iterpool);

  /* r4: touch same relative node in copy target */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, rev, iterpool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, iterpool));
  SVN_ERR(svn_test__set_file_contents(txn_root, "Z/B/E/beta", "new", iterpool));
  SVN_ERR(test_commit_txn(&rev, txn, NULL, iterpool));
  svn_pool_clear(iterpool);

  /* Now ask for some 'node created' info. */
  for (i = 0; to_check[i].rev > 0; ++i)
    {
      svn_revnum_t crev;
      const char *cpath;

      svn_pool_clear(iterpool);

      /* Get created path and rev. */
      SVN_ERR(svn_fs_revision_root(&root, fs, to_check[i].rev, iterpool));
      SVN_ERR(svn_fs_node_created_path(&cpath, root, to_check[i].path,
                                       iterpool));
      SVN_ERR(svn_fs_node_created_rev(&crev, root, to_check[i].path,
                                      iterpool));

      /* Compare the results with our expectations. */
      SVN_TEST_STRING_ASSERT(cpath, to_check[i].cpath);

      if (crev != to_check[i].crev)
        return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                                 "created rev mismatch for %s@%ld:\n"
                                 "  expected '%ld'\n"
                                 "     found '%ld",
                                 to_check[i].path,
                                 to_check[i].rev,
                                 to_check[i].crev,
                                 crev);
    }

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

static svn_error_t *
test_print_modules(const svn_test_opts_t *opts,
                   apr_pool_t *pool)
{
  const char *expected, *module_name;
  svn_stringbuf_t *modules = svn_stringbuf_create_empty(pool);

  /* Name of the providing module */
  if (strcmp(opts->fs_type, SVN_FS_TYPE_FSX) == 0)
    module_name = "fs_x";
  else if (strcmp(opts->fs_type, SVN_FS_TYPE_FSFS) == 0)
    module_name = "fs_fs";
  else if (strcmp(opts->fs_type, SVN_FS_TYPE_BDB) == 0)
    module_name = "fs_base";
  else
    return svn_error_createf(SVN_ERR_TEST_SKIPPED, NULL,
                             "don't know the module name for %s",
                             opts->fs_type);

  SVN_ERR(svn_fs_print_modules(modules, pool));

  /* The requested FS type must be listed amongst the available modules. */
  expected = apr_psprintf(pool, "* %s : ", module_name);
  SVN_TEST_ASSERT(strstr(modules->data, expected));

  return SVN_NO_ERROR;
}

/* Baton to be used with process_file_contents. */
typedef struct process_file_contents_baton_t
{
  const char *contents;
  svn_boolean_t processed;
} process_file_contents_baton_t;

/* Implements svn_fs_process_contents_func_t.
 * We flag the BATON as "processed" and compare the CONTENTS we've got to
 * what we expect through the BATON.
 */
static svn_error_t *
process_file_contents(const unsigned char *contents,
                      apr_size_t len,
                      void *baton,
                      apr_pool_t *scratch_pool)
{
  process_file_contents_baton_t *b = baton;

  SVN_TEST_ASSERT(strlen(b->contents) == len);
  SVN_TEST_ASSERT(memcmp(b->contents, contents, len) == 0);
  b->processed = TRUE;

  return SVN_NO_ERROR;
}

static svn_error_t *
test_zero_copy_processsing(const svn_test_opts_t *opts,
                           apr_pool_t *pool)
{
  svn_fs_t *fs;
  svn_fs_txn_t *txn;
  svn_fs_root_t *txn_root, *root;
  svn_revnum_t rev;
  const struct svn_test__tree_entry_t *node;
  apr_pool_t *iterpool = svn_pool_create(pool);

  /* Start with a new repo and the greek tree in rev 1. */
  SVN_ERR(svn_test__create_fs(&fs, "test-repo-zero-copy-processing",
                              opts, pool));

  SVN_ERR(svn_fs_begin_txn(&txn, fs, 0, iterpool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, iterpool));
  SVN_ERR(svn_test__create_greek_tree(txn_root, iterpool));
  SVN_ERR(test_commit_txn(&rev, txn, NULL, iterpool));
  svn_pool_clear(iterpool);

  SVN_ERR(svn_fs_revision_root(&root, fs, rev, pool));

  /* Prime the full-text cache by reading all file contents. */
  for (node = svn_test__greek_tree_nodes; node->path; node++)
    if (node->contents)
      {
        svn_stream_t *stream;
        svn_pool_clear(iterpool);

        SVN_ERR(svn_fs_file_contents(&stream, root, node->path, iterpool));
        SVN_ERR(svn_stream_copy3(stream, svn_stream_buffered(iterpool),
                                NULL, NULL, iterpool));
      }

  /* Now, try to get the data directly from cache
   * (if the backend has caches). */
  for (node = svn_test__greek_tree_nodes; node->path; node++)
    if (node->contents)
      {
        svn_boolean_t success;

        process_file_contents_baton_t baton;
        baton.contents = node->contents;
        baton.processed = FALSE;

        svn_pool_clear(iterpool);

        SVN_ERR(svn_fs_try_process_file_contents(&success, root, node->path,
                                                process_file_contents, &baton,
                                                iterpool));
        SVN_TEST_ASSERT(success == baton.processed);
      }

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

static svn_error_t *
test_dir_optimal_order(const svn_test_opts_t *opts,
                       apr_pool_t *pool)
{
  svn_fs_t *fs;
  svn_fs_txn_t *txn;
  svn_fs_root_t *txn_root, *root;
  svn_revnum_t rev;
  apr_hash_t *unordered;
  apr_array_header_t *ordered;
  int i;
  apr_hash_index_t *hi;

  /* Create a new repo and the greek tree in rev 1. */
  SVN_ERR(svn_test__create_fs(&fs, "test-repo-dir-optimal-order",
                              opts, pool));

  SVN_ERR(svn_fs_begin_txn(&txn, fs, 0, pool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, pool));
  SVN_ERR(svn_test__create_greek_tree(txn_root, pool));
  SVN_ERR(test_commit_txn(&rev, txn, NULL, pool));

  SVN_ERR(svn_fs_revision_root(&root, fs, rev, pool));

  /* Call the API function we are interested in. */
  SVN_ERR(svn_fs_dir_entries(&unordered, root, "A", pool));
  SVN_ERR(svn_fs_dir_optimal_order(&ordered, root, unordered, pool, pool));

  /* Verify that all entries are returned. */
  SVN_TEST_ASSERT(ordered->nelts == apr_hash_count(unordered));
  for (hi = apr_hash_first(pool, unordered); hi; hi = apr_hash_next(hi))
    {
      svn_boolean_t found = FALSE;
      const char *name = apr_hash_this_key(hi);

      /* Compare hash -> array because the array might contain the same
       * entry more than once.  Since that can't happen in the hash, doing
       * it in this direction ensures ORDERED won't contain duplicates.
       */
      for (i = 0; !found && i < ordered->nelts; ++i)
        {
          svn_fs_dirent_t *item = APR_ARRAY_IDX(ordered, i, svn_fs_dirent_t*);
          if (strcmp(item->name, name) == 0)
            {
              found = TRUE;
              SVN_TEST_ASSERT(item == apr_hash_this_val(hi));
            }
        }

      SVN_TEST_ASSERT(found);
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
test_config_files(const svn_test_opts_t *opts,
                  apr_pool_t *pool)
{
  svn_fs_t *fs;
  apr_array_header_t *files;
  int i;
  const char *repo_name = "test-repo-config-files";

  /* Create a empty and get its config files. */
  SVN_ERR(svn_test__create_fs(&fs, repo_name, opts, pool));
  SVN_ERR(svn_fs_info_config_files(&files, fs, pool, pool));

  /* All files should exist and be below the repo. */
  for (i = 0; i < files->nelts; ++i)
    {
      svn_node_kind_t kind;
      const char *path = APR_ARRAY_IDX(files, i, const char*);

      SVN_ERR(svn_io_check_path(path, &kind, pool));

      SVN_TEST_ASSERT(kind == svn_node_file);
      SVN_TEST_ASSERT(svn_dirent_is_ancestor(repo_name, path));
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
test_delta_file_stream(const svn_test_opts_t *opts,
                       apr_pool_t *pool)
{
  svn_fs_t *fs;
  svn_fs_txn_t *txn;
  svn_fs_root_t *txn_root, *root1, *root2;
  svn_revnum_t rev;
  apr_pool_t *subpool = svn_pool_create(pool);

  const char *old_content = "some content";
  const char *new_content = "some more content";
  svn_txdelta_window_handler_t delta_handler;
  void *delta_baton;
  svn_txdelta_stream_t *delta_stream;
  svn_stringbuf_t *source = svn_stringbuf_create_empty(pool);
  svn_stringbuf_t *dest = svn_stringbuf_create_empty(pool);

  /* Create a new repo. */
  SVN_ERR(svn_test__create_fs(&fs, "test-repo-delta-file-stream",
                              opts, pool));

  /* Revision 1: create a file. */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, 0, pool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, pool));
  SVN_ERR(svn_fs_make_file(txn_root, "foo", pool));
  SVN_ERR(svn_test__set_file_contents(txn_root, "foo", old_content, pool));
  SVN_ERR(test_commit_txn(&rev, txn, NULL, pool));

  /* Revision 2: create a file. */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, rev, pool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, pool));
  SVN_ERR(svn_test__set_file_contents(txn_root, "foo", new_content, pool));
  SVN_ERR(test_commit_txn(&rev, txn, NULL, pool));

  SVN_ERR(svn_fs_revision_root(&root1, fs, 1, pool));
  SVN_ERR(svn_fs_revision_root(&root2, fs, 2, pool));

  /* Test 1: Get delta against empty target. */
  SVN_ERR(svn_fs_get_file_delta_stream(&delta_stream,
                                       NULL, NULL, root1, "foo", subpool));

  svn_stringbuf_setempty(source);
  svn_stringbuf_setempty(dest);

  svn_txdelta_apply(svn_stream_from_stringbuf(source, subpool),
                    svn_stream_from_stringbuf(dest, subpool),
                    NULL, NULL, subpool, &delta_handler, &delta_baton);
  SVN_ERR(svn_txdelta_send_txstream(delta_stream,
                                    delta_handler,
                                    delta_baton,
                                    subpool));
  SVN_TEST_STRING_ASSERT(old_content, dest->data);
  svn_pool_clear(subpool);

  /* Test 2: Get delta against previous version. */
  SVN_ERR(svn_fs_get_file_delta_stream(&delta_stream,
                                       root1, "foo", root2, "foo", subpool));

  svn_stringbuf_set(source, old_content);
  svn_stringbuf_setempty(dest);

  svn_txdelta_apply(svn_stream_from_stringbuf(source, subpool),
                    svn_stream_from_stringbuf(dest, subpool),
                    NULL, NULL, subpool, &delta_handler, &delta_baton);
  SVN_ERR(svn_txdelta_send_txstream(delta_stream,
                                    delta_handler,
                                    delta_baton,
                                    subpool));
  SVN_TEST_STRING_ASSERT(new_content, dest->data);
  svn_pool_clear(subpool);

  /* Test 3: Get reverse delta. */
  SVN_ERR(svn_fs_get_file_delta_stream(&delta_stream,
                                       root2, "foo", root1, "foo", subpool));

  svn_stringbuf_set(source, new_content);
  svn_stringbuf_setempty(dest);

  svn_txdelta_apply(svn_stream_from_stringbuf(source, subpool),
                    svn_stream_from_stringbuf(dest, subpool),
                    NULL, NULL, subpool, &delta_handler, &delta_baton);
  SVN_ERR(svn_txdelta_send_txstream(delta_stream,
                                    delta_handler,
                                    delta_baton,
                                    subpool));
  SVN_TEST_STRING_ASSERT(old_content, dest->data);

  svn_pool_destroy(subpool);

  return SVN_NO_ERROR;
}

static svn_error_t *
test_fs_merge(const svn_test_opts_t *opts,
              apr_pool_t *pool)
{
  svn_fs_t *fs;
  svn_fs_txn_t *txn;
  svn_fs_root_t *txn_root, *root0, *root1;
  svn_revnum_t rev;

  /* Very basic test for svn_fs_merge because all the other interesting
   * cases get tested implicitly with concurrent txn / commit tests.
   * This API is just a thin layer around the internal merge function
   * and we simply check that the plumbing between them works.
   */

  /* Create a new repo. */
  SVN_ERR(svn_test__create_fs(&fs, "test-repo-fs-merge",
                              opts, pool));
  SVN_ERR(svn_fs_revision_root(&root0, fs, 0, pool));

  /* Revision 1: create a file. */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, 0, pool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, pool));
  SVN_ERR(svn_fs_make_file(txn_root, "foo", pool));
  SVN_ERR(test_commit_txn(&rev, txn, NULL, pool));
  SVN_ERR(svn_fs_revision_root(&root1, fs, rev, pool));

  /* Merge-able txn: create another file. */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, 0, pool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, pool));
  SVN_ERR(svn_fs_make_file(txn_root, "bar", pool));

  SVN_ERR(svn_fs_merge(NULL, root1, "/", txn_root, "/", root0, "/", pool));

  /* Not merge-able: create the same file file. */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, 0, pool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, pool));
  SVN_ERR(svn_fs_make_file(txn_root, "foo", pool));

  SVN_TEST_ASSERT_ERROR(svn_fs_merge(NULL, root1, "/", txn_root, "/", root0,
                                     "/", pool), SVN_ERR_FS_CONFLICT);

  return SVN_NO_ERROR;
}

static svn_error_t *
test_fsfs_config_opts(const svn_test_opts_t *opts,
                      apr_pool_t *pool)
{
  apr_hash_t *fs_config;
  svn_fs_t *fs;
  const svn_fs_info_placeholder_t *fs_info;
  const svn_fs_fsfs_info_t *fsfs_info;
  const char *dir_name = "test-repo-fsfs-config-opts";
  const char *repo_name_default = "test-repo-fsfs-config-opts/default";
  const char *repo_name_custom = "test-repo-fsfs-config-opts/custom";

  /* Bail (with SKIP) on known-untestable scenarios */
  if (strcmp(opts->fs_type, SVN_FS_TYPE_FSFS) != 0)
    return svn_error_create(SVN_ERR_TEST_SKIPPED, NULL,
                            "this will test FSFS repositories only");

  /* Remove the test directory from previous runs. */
  SVN_ERR(svn_io_remove_dir2(dir_name, TRUE, NULL, NULL, pool));

  /* Create the test directory and add it to the test cleanup list. */
  SVN_ERR(svn_io_dir_make(dir_name, APR_OS_DEFAULT, pool));
  svn_test_add_dir_cleanup(dir_name);

  /* Create an FSFS filesystem with default config.*/
  fs_config = apr_hash_make(pool);
  svn_hash_sets(fs_config, SVN_FS_CONFIG_FS_TYPE, SVN_FS_TYPE_FSFS);
  SVN_ERR(svn_fs_create(&fs, repo_name_default, fs_config, pool));

  /* Re-open FS to test the data on disk. */
  SVN_ERR(svn_fs_open2(&fs, repo_name_default, NULL, pool, pool));

  SVN_ERR(svn_fs_info(&fs_info, fs, pool, pool));
  SVN_TEST_STRING_ASSERT(fs_info->fs_type, SVN_FS_TYPE_FSFS);
  fsfs_info = (const void *) fs_info;

  /* Check FSFS specific info. Don't check the SHARD_SIZE, because it depends
   * on a compile-time constant and may be overridden. */
  SVN_TEST_ASSERT(fsfs_info->log_addressing);
  SVN_TEST_ASSERT(fsfs_info->min_unpacked_rev == 0);

  /* Create an FSFS filesystem with custom settings: disabled log-addressing
   * and custom shard size (123). */
  fs_config = apr_hash_make(pool);
  svn_hash_sets(fs_config, SVN_FS_CONFIG_FS_TYPE, SVN_FS_TYPE_FSFS);
  svn_hash_sets(fs_config, SVN_FS_CONFIG_FSFS_LOG_ADDRESSING, "false");
  svn_hash_sets(fs_config, SVN_FS_CONFIG_FSFS_SHARD_SIZE, "123");
  SVN_ERR(svn_fs_create(&fs, repo_name_custom, fs_config, pool));

  /* Re-open FS to test the data on disk. */
  SVN_ERR(svn_fs_open2(&fs, repo_name_custom, NULL, pool, pool));

  SVN_ERR(svn_fs_info(&fs_info, fs, pool, pool));
  SVN_TEST_STRING_ASSERT(fs_info->fs_type, SVN_FS_TYPE_FSFS);
  fsfs_info = (const void *) fs_info;

  /* Check FSFS specific info, including the SHARD_SIZE. */
  SVN_TEST_ASSERT(fsfs_info->log_addressing == FALSE);
  SVN_TEST_ASSERT(fsfs_info->shard_size == 123);
  SVN_TEST_ASSERT(fsfs_info->min_unpacked_rev == 0);

  return SVN_NO_ERROR;
}

static svn_error_t *
test_txn_pool_lifetime(const svn_test_opts_t *opts,
                       apr_pool_t *pool)
{
  /* Technically, the FS API makes no assumption on the lifetime of logically
   * dependent objects.  In particular, a txn root object may get destroyed
   * after the FS object that it has been built upon.  Actual data access is
   * implied to be invalid without a valid svn_fs_t.
   *
   * This test verifies that at least the destruction order of those two
   * objects is arbitrary.
   */
  svn_fs_t *fs;
  svn_fs_txn_t *txn;
  svn_fs_root_t *txn_root;

  /* We will allocate FS in FS_POOL.  Using a separate allocator makes
   * sure that we actually free the memory when destroying the pool.
   */
  apr_allocator_t *fs_allocator = svn_pool_create_allocator(FALSE);
  apr_pool_t *fs_pool = apr_allocator_owner_get(fs_allocator);

  /* Create a new repo. */
  SVN_ERR(svn_test__create_fs(&fs, "test-repo-pool-lifetime",
                              opts, fs_pool));

  /* Create a TXN_ROOT referencing FS. */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, 0, pool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, pool));

  /* Destroy FS.  Depending on the actual allocator implementation,
   * these memory pages becomes inaccessible. */
  svn_pool_destroy(fs_pool);

  /* Unclean implementations will try to access FS and may segfault here. */
  svn_fs_close_root(txn_root);

  return SVN_NO_ERROR;
}

static svn_error_t *
test_modify_txn_being_written(const svn_test_opts_t *opts,
                              apr_pool_t *pool)
{
  /* FSFS has a limitation (and check) that only one file can be
   * modified in TXN at time: see r861812 and svn_fs_apply_text() docstring.
   * This is regression test for this behavior. */
  svn_fs_t *fs;
  svn_fs_txn_t *txn;
  const char *txn_name;
  svn_fs_root_t *txn_root;
  svn_stream_t *foo_contents;
  svn_stream_t *bar_contents;

  /* Bail (with success) on known-untestable scenarios */
  if (strcmp(opts->fs_type, SVN_FS_TYPE_BDB) == 0)
    return svn_error_create(SVN_ERR_TEST_SKIPPED, NULL,
                            "this will not test BDB repositories");

  /* Create a new repo. */
  SVN_ERR(svn_test__create_fs(&fs, "test-repo-modify-txn-being-written",
                              opts, pool));

  /* Create a TXN_ROOT referencing FS. */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, 0, pool));
  SVN_ERR(svn_fs_txn_name(&txn_name, txn, pool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, pool));

  /* Make file /foo and open for writing.*/
  SVN_ERR(svn_fs_make_file(txn_root, "/foo", pool));
  SVN_ERR(svn_fs_apply_text(&foo_contents, txn_root, "/foo", NULL, pool));

  /* Attempt to modify another file '/bar' -- FSFS doesn't allow this. */
  SVN_ERR(svn_fs_make_file(txn_root, "/bar", pool));
  SVN_TEST_ASSERT_ERROR(
      svn_fs_apply_text(&bar_contents, txn_root, "/bar", NULL, pool),
      SVN_ERR_FS_REP_BEING_WRITTEN);

  /* *Reopen TXN. */
  SVN_ERR(svn_fs_open_txn(&txn, fs, txn_name, pool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, pool));

  /* Check that file '/bar' still cannot be modified */
  SVN_TEST_ASSERT_ERROR(
      svn_fs_apply_text(&bar_contents, txn_root, "/bar", NULL, pool),
      SVN_ERR_FS_REP_BEING_WRITTEN);

  /* Close file '/foo'. */
  SVN_ERR(svn_stream_close(foo_contents));

  /* Now file '/bar' can be modified. */
  SVN_ERR(svn_fs_apply_text(&bar_contents, txn_root, "/bar", NULL, pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
test_prop_and_text_rep_sharing_collision(const svn_test_opts_t *opts,
                                         apr_pool_t *pool)
{
  /* Regression test for issue 4554: Wrong file length with PLAIN
   * representations in FSFS. */
  svn_fs_t *fs;
  svn_fs_txn_t *txn;
  svn_fs_root_t *txn_root;
  svn_fs_root_t *rev_root;
  svn_revnum_t new_rev;
  svn_filesize_t length;
  const char *testdir = "test-repo-prop-and-text-rep-sharing-collision";

  /* Create a new repo. */
  SVN_ERR(svn_test__create_fs(&fs, testdir, opts, pool));

  SVN_ERR(svn_fs_begin_txn(&txn, fs, 0, pool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, pool));
  /* Set node property for the root. */
  SVN_ERR(svn_fs_change_node_prop(txn_root, "/", "prop",
                                  svn_string_create("value", pool),
                                  pool));

  /* Commit revision r1. */
  SVN_ERR(test_commit_txn(&new_rev, txn, NULL, pool));

  SVN_ERR(svn_fs_begin_txn(&txn, fs, 1, pool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, pool));

  /* Create file with same contents as property representation. */
  SVN_ERR(svn_fs_make_file(txn_root, "/foo", pool));
  SVN_ERR(svn_test__set_file_contents(txn_root, "/foo",
                                      "K 4\n"
                                      "prop\n"
                                      "V 5\n"
                                      "value\n"
                                      "END\n", pool));

  /* Commit revision r2. */
  SVN_ERR(test_commit_txn(&new_rev, txn, NULL, pool));

  /* Check that FS reports correct length for the file (23). */
  SVN_ERR(svn_fs_revision_root(&rev_root, fs, 2, pool));
  SVN_ERR(svn_fs_file_length(&length, rev_root, "/foo", pool));

  SVN_TEST_ASSERT(length == 23);
  return SVN_NO_ERROR;
}

static svn_error_t *
test_internal_txn_props(const svn_test_opts_t *opts,
                        apr_pool_t *pool)
{
  svn_fs_t *fs;
  svn_fs_txn_t *txn;
  svn_string_t *val;
  svn_prop_t prop;
  svn_prop_t internal_prop;
  apr_array_header_t *props;
  apr_hash_t *proplist;
  svn_error_t *err;

  SVN_ERR(svn_test__create_fs(&fs, "test-repo-internal-txn-props",
                              opts, pool));
  SVN_ERR(svn_fs_begin_txn2(&txn, fs, 0,
                            SVN_FS_TXN_CHECK_LOCKS |
                            SVN_FS_TXN_CHECK_OOD |
                            SVN_FS_TXN_CLIENT_DATE, pool));

  /* Ensure that we cannot read internal transaction properties. */
  SVN_ERR(svn_fs_txn_prop(&val, txn, SVN_FS__PROP_TXN_CHECK_LOCKS, pool));
  SVN_TEST_ASSERT(!val);
  SVN_ERR(svn_fs_txn_prop(&val, txn, SVN_FS__PROP_TXN_CHECK_OOD, pool));
  SVN_TEST_ASSERT(!val);
  SVN_ERR(svn_fs_txn_prop(&val, txn, SVN_FS__PROP_TXN_CLIENT_DATE, pool));
  SVN_TEST_ASSERT(!val);

  SVN_ERR(svn_fs_txn_proplist(&proplist, txn, pool));
  SVN_TEST_ASSERT(apr_hash_count(proplist) == 1);
  val = svn_hash_gets(proplist, SVN_PROP_REVISION_DATE);
  SVN_TEST_ASSERT(val);

  /* We also cannot change or discard them. */
  val = svn_string_create("Ooops!", pool);

  err = svn_fs_change_txn_prop(txn, SVN_FS__PROP_TXN_CHECK_LOCKS, val, pool);
  SVN_TEST_ASSERT_ERROR(err, SVN_ERR_INCORRECT_PARAMS);
  err = svn_fs_change_txn_prop(txn, SVN_FS__PROP_TXN_CHECK_LOCKS, NULL, pool);
  SVN_TEST_ASSERT_ERROR(err, SVN_ERR_INCORRECT_PARAMS);
  err = svn_fs_change_txn_prop(txn, SVN_FS__PROP_TXN_CHECK_OOD, val, pool);
  SVN_TEST_ASSERT_ERROR(err, SVN_ERR_INCORRECT_PARAMS);
  err = svn_fs_change_txn_prop(txn, SVN_FS__PROP_TXN_CHECK_OOD, NULL, pool);
  SVN_TEST_ASSERT_ERROR(err, SVN_ERR_INCORRECT_PARAMS);
  err = svn_fs_change_txn_prop(txn, SVN_FS__PROP_TXN_CLIENT_DATE, val, pool);
  SVN_TEST_ASSERT_ERROR(err, SVN_ERR_INCORRECT_PARAMS);
  err = svn_fs_change_txn_prop(txn, SVN_FS__PROP_TXN_CLIENT_DATE, NULL, pool);
  SVN_TEST_ASSERT_ERROR(err, SVN_ERR_INCORRECT_PARAMS);

  prop.name = "foo";
  prop.value = svn_string_create("bar", pool);
  internal_prop.name = SVN_FS__PROP_TXN_CHECK_LOCKS;
  internal_prop.value = svn_string_create("Ooops!", pool);

  props = apr_array_make(pool, 2, sizeof(svn_prop_t));
  APR_ARRAY_PUSH(props, svn_prop_t) = prop;
  APR_ARRAY_PUSH(props, svn_prop_t) = internal_prop;

  err = svn_fs_change_txn_props(txn, props, pool);
  SVN_TEST_ASSERT_ERROR(err, SVN_ERR_INCORRECT_PARAMS);

  return SVN_NO_ERROR;
}

/* A freeze function that expects an 'svn_error_t *' baton, and returns it. */
/* This function implements svn_fs_freeze_func_t. */
static svn_error_t *
freeze_func(void *baton, apr_pool_t *pool)
{
  return baton;
}

static svn_error_t *
freeze_and_commit(const svn_test_opts_t *opts,
                  apr_pool_t *pool)
{
  svn_fs_t *fs;
  svn_fs_txn_t *txn;
  svn_fs_root_t *txn_root;
  svn_revnum_t new_rev = 0;
  apr_pool_t *subpool = svn_pool_create(pool);
  const char *repo_name = "test-repo-freeze-and-commit";

  if (!strcmp(opts->fs_type, "bdb"))
    return svn_error_create(SVN_ERR_TEST_SKIPPED, NULL,
                            "this will not test BDB repositories");

  SVN_ERR(svn_test__create_fs(&fs, repo_name, opts, subpool));

  /* This test used to FAIL with an SQLite error since svn_fs_freeze()
   * wouldn't unlock rep-cache.db.  Therefore, part of the role of creating
   * the Greek tree is to create a rep-cache.db, in order to test that
   * svn_fs_freeze() unlocks it. */

  /* r1: Commit the Greek tree. */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, new_rev, subpool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, subpool));
  SVN_ERR(svn_test__create_greek_tree(txn_root, subpool));
  SVN_ERR(test_commit_txn(&new_rev, txn, NULL, subpool));

  /* Freeze and unfreeze. */
  SVN_ERR(svn_fs_freeze(fs, freeze_func, SVN_NO_ERROR, pool));

  /* Freeze again, but have freeze_func fail. */
    {
      svn_error_t *err = svn_error_create(APR_EGENERAL, NULL, NULL);
      SVN_TEST_ASSERT_ERROR(svn_fs_freeze(fs, freeze_func, err, pool),
                            err->apr_err);
    }

  /* Make some commit using same FS instance. */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, new_rev, pool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, pool));
  SVN_ERR(svn_fs_change_node_prop(txn_root, "", "temperature",
                                  svn_string_create("310.05", pool),
                                  pool));
  SVN_ERR(test_commit_txn(&new_rev, txn, NULL, pool));

  /* Re-open FS and make another commit. */
  SVN_ERR(svn_fs_open(&fs, repo_name, NULL, subpool));
  SVN_ERR(svn_fs_begin_txn(&txn, fs, new_rev, pool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, pool));
  SVN_ERR(svn_fs_change_node_prop(txn_root, "/", "temperature",
                                  svn_string_create("451", pool),
                                  pool));
  SVN_ERR(test_commit_txn(&new_rev, txn, NULL, pool));

  return SVN_NO_ERROR;
}

/* Number of changes in a revision.
 * Should be > 100 to span multiple blocks. */
#define CHANGES_COUNT 1017

/* Check that REVISION in FS reports the expected changes. */
static svn_error_t *
verify_added_files_list(svn_fs_t *fs,
                        svn_revnum_t revision,
                        apr_pool_t *scratch_pool)
{
  int i;
  svn_fs_root_t *root;
  apr_hash_t *changed_paths;
  svn_fs_path_change_iterator_t *iterator;
  svn_fs_path_change3_t *change;
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);

  /* Collect changes and test that no path gets reported twice. */
  SVN_ERR(svn_fs_revision_root(&root, fs, revision, scratch_pool));
  SVN_ERR(svn_fs_paths_changed3(&iterator, root, scratch_pool, scratch_pool));

  changed_paths = apr_hash_make(scratch_pool);
  SVN_ERR(svn_fs_path_change_get(&change, iterator));
  while (change)
    {
      const char *path = apr_pstrmemdup(scratch_pool, change->path.data,
                                        change->path.len);
      SVN_TEST_ASSERT(change->change_kind == svn_fs_path_change_add);
      SVN_TEST_ASSERT(!apr_hash_get(changed_paths, path, change->path.len));

      apr_hash_set(changed_paths, path, change->path.len, path);
      SVN_ERR(svn_fs_path_change_get(&change, iterator));
    }

  /* Verify that we've got exactly all paths that we added. */
  SVN_TEST_ASSERT(CHANGES_COUNT == apr_hash_count(changed_paths));
  for (i = 0; i < CHANGES_COUNT; ++i)
    {
      const char *file_name;
      svn_pool_clear(iterpool);

      file_name = apr_psprintf(iterpool, "/file-%d", i);
      SVN_TEST_ASSERT(svn_hash_gets(changed_paths, file_name));
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
test_large_changed_paths_list(const svn_test_opts_t *opts,
                              apr_pool_t *pool)
{
  svn_fs_t *fs;
  svn_fs_txn_t *txn;
  svn_fs_root_t *txn_root;
  int i;
  svn_revnum_t rev = 0;
  apr_pool_t *iterpool = svn_pool_create(pool);
  const char *repo_name = "test-repo-changed-paths-list";

  SVN_ERR(svn_test__create_fs(&fs, repo_name, opts, pool));

  /* r1: Add many empty files - just to amass a long list of changes. */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, rev, pool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, pool));

  for (i = 0; i < CHANGES_COUNT; ++i)
    {
      const char *file_name;
      svn_pool_clear(iterpool);

      file_name = apr_psprintf(iterpool, "/file-%d", i);
      SVN_ERR(svn_fs_make_file(txn_root, file_name, iterpool));
    }

  SVN_ERR(test_commit_txn(&rev, txn, NULL, pool));

  /* Now, read the change list.
   * Do it twice to cover cached data as well. */
  svn_pool_clear(iterpool);
  SVN_ERR(verify_added_files_list(fs, rev, iterpool));
  svn_pool_clear(iterpool);
  SVN_ERR(verify_added_files_list(fs, rev, iterpool));
  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

#undef CHANGES_COUNT

static svn_error_t *
commit_with_locked_rep_cache(const svn_test_opts_t *opts,
                             apr_pool_t *pool)
{
  svn_fs_t *fs;
  svn_fs_txn_t *txn;
  svn_fs_root_t *txn_root;
  svn_revnum_t new_rev;
  svn_sqlite__db_t *sdb;
  svn_error_t *err;
  const char *fs_path;
  const char *statements[] = { "SELECT MAX(revision) FROM rep_cache", NULL };

  if (strcmp(opts->fs_type, SVN_FS_TYPE_BDB) == 0)
    return svn_error_create(SVN_ERR_TEST_SKIPPED, NULL,
                            "this will not test BDB repositories");

  if (opts->server_minor_version && (opts->server_minor_version < 6))
    return svn_error_create(SVN_ERR_TEST_SKIPPED, NULL,
                            "pre-1.6 SVN doesn't support FSFS rep-sharing");

  fs_path = "test-repo-commit-with-locked-rep-cache";
  SVN_ERR(svn_test__create_fs(&fs, fs_path, opts, pool));

  /* r1: Add a file. */
  SVN_ERR(svn_fs_begin_txn2(&txn, fs, 0, 0, pool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, pool));
  SVN_ERR(svn_fs_make_file(txn_root, "/foo", pool));
  SVN_ERR(svn_test__set_file_contents(txn_root, "/foo", "a", pool));
  SVN_ERR(test_commit_txn(&new_rev, txn, NULL, pool));
  SVN_TEST_INT_ASSERT(new_rev, 1);

  /* Begin a new transaction based on r1. */
  SVN_ERR(svn_fs_begin_txn2(&txn, fs, 1, 0, pool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, pool));
  SVN_ERR(svn_test__set_file_contents(txn_root, "/foo", "b", pool));

  /* Obtain a shared lock on the rep-cache.db by starting a new read
   * transaction. */
  SVN_ERR(svn_sqlite__open(&sdb,
                           svn_dirent_join(fs_path, "rep-cache.db", pool),
                           svn_sqlite__mode_readonly, statements, 0, NULL,
                           0, pool, pool));
  SVN_ERR(svn_sqlite__begin_transaction(sdb));
  SVN_ERR(svn_sqlite__exec_statements(sdb, 0));

  /* Attempt to commit fs transaction.  This should result in a commit
   * post-processing error due to us still holding the shared lock on the
   * rep-cache.db. */
  err = svn_fs_commit_txn(NULL, &new_rev, txn, pool);
  SVN_TEST_ASSERT_ERROR(err, SVN_ERR_SQLITE_BUSY);
  SVN_TEST_INT_ASSERT(new_rev, 2);

  /* Release the shared lock. */
  SVN_ERR(svn_sqlite__finish_transaction(sdb, SVN_NO_ERROR));
  SVN_ERR(svn_sqlite__close(sdb));

  /* Try an operation that reads from rep-cache.db.
   *
   * XFAIL: Around r1740802, this call was producing an error due to the
   * svn_fs_t keeping an unusable db connection (and associated file
   * locks) within it.
   */
  SVN_ERR(svn_fs_verify(fs_path, NULL, 0, SVN_INVALID_REVNUM, NULL, NULL,
                        NULL, NULL, pool));

  return SVN_NO_ERROR;
}


static svn_error_t *
test_cache_clear_during_stream(const svn_test_opts_t *opts,
                               apr_pool_t *pool)
{
  svn_fs_t *fs;
  svn_fs_txn_t *txn;
  svn_fs_root_t *txn_root, *rev_root;
  svn_revnum_t new_rev;
  const char *fs_path;
  apr_pool_t *iterpool = svn_pool_create(pool);
  apr_pool_t *subpool = svn_pool_create(pool);
  svn_txdelta_window_handler_t consumer_func;
  void *consumer_baton;
  int i;
  svn_stream_t *stream;
  svn_stringbuf_t *buf;


  fs_path = "test-repo-cache_clear_during_stream";
  SVN_ERR(svn_test__create_fs(&fs, fs_path, opts, pool));

  /* r1: Add a file. */
  SVN_ERR(svn_fs_begin_txn2(&txn, fs, 0, 0, pool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, pool));
  SVN_ERR(svn_fs_make_file(txn_root, "/foo", pool));

  /* Make the file large enough to span multiple txdelta windows.
   * Just to be sure, make it not too uniform to keep self-txdelta at bay. */
  SVN_ERR(svn_fs_apply_textdelta(&consumer_func, &consumer_baton,
                                 txn_root, "/foo", NULL, NULL, subpool));
  stream = svn_txdelta_target_push(consumer_func, consumer_baton, 
                                   svn_stream_empty(subpool), subpool);
  for (i = 0; i < 10000; ++ i)
    {
      svn_string_t *text;

      svn_pool_clear(iterpool);
      text = svn_string_createf(iterpool, "some dummy text - %d\n", i);
      SVN_ERR(svn_stream_write(stream, text->data, &text->len));
    }

  SVN_ERR(svn_stream_close(stream));
  svn_pool_destroy(subpool);

  SVN_ERR(test_commit_txn(&new_rev, txn, NULL, pool));
  SVN_TEST_INT_ASSERT(new_rev, 1);

  /* Read the file once to populate the fulltext cache. */
  SVN_ERR(svn_fs_revision_root(&rev_root, fs, 1, pool));
  SVN_ERR(svn_fs_file_contents(&stream, rev_root, "/foo", pool));
  SVN_ERR(svn_test__stream_to_string(&buf, stream, pool));

  /* Start reading it again from cache, clear the cache and continue.
   * Make sure we read more than one txdelta window before clearing
   * the cache.  That gives the FS backend a chance to skip windows
   * when continuing the read from disk. */
  SVN_ERR(svn_fs_file_contents(&stream, rev_root, "/foo", pool));
  buf->len = 2 * SVN_STREAM_CHUNK_SIZE;
  SVN_ERR(svn_stream_read_full(stream, buf->data, &buf->len));
  SVN_ERR(svn_cache__membuffer_clear(svn_cache__get_global_membuffer_cache()));
  SVN_ERR(svn_test__stream_to_string(&buf, stream, pool));

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

static svn_error_t *
test_rep_sharing_strict_content_check(const svn_test_opts_t *opts,
                                      apr_pool_t *pool)
{
  svn_fs_t *fs;
  svn_fs_txn_t *txn;
  svn_fs_root_t *txn_root;
  svn_revnum_t new_rev;
  const char *fs_path, *fs_path2;
  apr_pool_t *subpool = svn_pool_create(pool);
  svn_error_t *err;

  /* Bail (with success) on known-untestable scenarios */
  if (strcmp(opts->fs_type, SVN_FS_TYPE_BDB) == 0)
    return svn_error_create(SVN_ERR_TEST_SKIPPED, NULL,
                            "BDB repositories don't support rep-sharing");

  /* Create 2 repos with same structure & size but different contents */
  fs_path = "test-rep-sharing-strict-content-check1";
  fs_path2 = "test-rep-sharing-strict-content-check2";

  SVN_ERR(svn_test__create_fs(&fs, fs_path, opts, subpool));

  SVN_ERR(svn_fs_begin_txn2(&txn, fs, 0, 0, subpool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, subpool));
  SVN_ERR(svn_fs_make_file(txn_root, "/foo", subpool));
  SVN_ERR(svn_test__set_file_contents(txn_root, "foo", "quite bad", subpool));
  SVN_ERR(test_commit_txn(&new_rev, txn, NULL, subpool));
  SVN_TEST_INT_ASSERT(new_rev, 1);

  SVN_ERR(svn_test__create_fs(&fs, fs_path2, opts, subpool));

  SVN_ERR(svn_fs_begin_txn2(&txn, fs, 0, 0, subpool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, subpool));
  SVN_ERR(svn_fs_make_file(txn_root, "foo", subpool));
  SVN_ERR(svn_test__set_file_contents(txn_root, "foo", "very good", subpool));
  SVN_ERR(test_commit_txn(&new_rev, txn, NULL, subpool));
  SVN_TEST_INT_ASSERT(new_rev, 1);

  /* Close both repositories. */
  svn_pool_clear(subpool);

  /* Doctor the first repo such that it uses the wrong rep-cache. */
  SVN_ERR(svn_io_copy_file(svn_relpath_join(fs_path2, "rep-cache.db", pool),
                           svn_relpath_join(fs_path, "rep-cache.db", pool),
                           FALSE, pool));

  /* Changing the file contents such that rep-sharing would kick in if
     the file contents was not properly compared. */
  SVN_ERR(svn_fs_open2(&fs, fs_path, NULL, subpool, subpool));

  SVN_ERR(svn_fs_begin_txn2(&txn, fs, 1, 0, subpool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, subpool));
  err = svn_test__set_file_contents(txn_root, "foo", "very good", subpool);
  SVN_TEST_ASSERT_ERROR(err, SVN_ERR_FS_AMBIGUOUS_CHECKSUM_REP);

  svn_pool_destroy(subpool);

  return SVN_NO_ERROR;
}

static svn_error_t *
closest_copy_test_svn_4677(const svn_test_opts_t *opts,
                           apr_pool_t *pool)
{
  svn_fs_t *fs;
  svn_fs_txn_t *txn;
  svn_fs_root_t *txn_root, *rev_root, *croot;
  svn_revnum_t after_rev;
  const char *cpath;
  apr_pool_t *spool = svn_pool_create(pool);

  /* Prepare a filesystem. */
  SVN_ERR(svn_test__create_fs(&fs, "test-repo-svn-4677",
                              opts, pool));

  /* In first txn, create file A/foo. */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, 0, spool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, spool));
  SVN_ERR(svn_fs_make_dir(txn_root, "A", spool));
  SVN_ERR(svn_fs_make_file(txn_root, "A/foo", spool));
  SVN_ERR(test_commit_txn(&after_rev, txn, NULL, spool));
  svn_pool_clear(spool);
  SVN_ERR(svn_fs_revision_root(&rev_root, fs, after_rev, spool));

  /* Move A to B, and commit. */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, after_rev, spool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, spool));
  SVN_ERR(svn_fs_copy(rev_root, "A", txn_root, "B", spool));
  SVN_ERR(svn_fs_delete(txn_root, "A", spool));
  SVN_ERR(test_commit_txn(&after_rev, txn, NULL, spool));
  svn_pool_clear(spool);
  SVN_ERR(svn_fs_revision_root(&rev_root, fs, after_rev, spool));

  /* Replace file B/foo with directory B/foo, add B/foo/bar, and commit. */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, after_rev, spool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, spool));
  SVN_ERR(svn_fs_delete(txn_root, "B/foo", spool));
  SVN_ERR(svn_fs_make_dir(txn_root, "B/foo", spool));
  SVN_ERR(svn_fs_make_file(txn_root, "B/foo/bar", spool));
  SVN_ERR(test_commit_txn(&after_rev, txn, NULL, spool));
  svn_pool_clear(spool);
  SVN_ERR(svn_fs_revision_root(&rev_root, fs, after_rev, spool));

  /* B/foo/bar has been copied.
     Issue 4677 was caused by returning an error in this situation. */
  SVN_ERR(svn_fs_closest_copy(&croot, &cpath, rev_root, "B/foo/bar", spool));
  SVN_TEST_ASSERT(cpath == NULL);
  SVN_TEST_ASSERT(croot == NULL);

  return SVN_NO_ERROR;
}

/* ------------------------------------------------------------------------ */

/* The test table.  */

static int max_threads = 8;

static struct svn_test_descriptor_t test_funcs[] =
  {
    SVN_TEST_NULL,
    SVN_TEST_OPTS_PASS(trivial_transaction,
                       "begin a txn, check its name, then close it"),
    SVN_TEST_OPTS_PASS(reopen_trivial_transaction,
                       "open an existing transaction by name"),
    SVN_TEST_OPTS_PASS(create_file_transaction,
                       "begin a txn, get the txn root, and add a file"),
    SVN_TEST_OPTS_PASS(verify_txn_list,
                       "create 2 txns, list them, and verify the list"),
    SVN_TEST_OPTS_PASS(txn_names_are_not_reused,
                       "check that transaction names are not reused"),
    SVN_TEST_OPTS_PASS(write_and_read_file,
                       "write and read a file's contents"),
    SVN_TEST_OPTS_PASS(almostmedium_file_integrity,
                       "create and modify almostmedium file"),
    SVN_TEST_OPTS_PASS(medium_file_integrity,
                       "create and modify medium file"),
    SVN_TEST_OPTS_PASS(large_file_integrity,
                       "create and modify large file"),
    SVN_TEST_OPTS_PASS(create_mini_tree_transaction,
                       "test basic file and subdirectory creation"),
    SVN_TEST_OPTS_PASS(create_greek_tree_transaction,
                       "make The Official Subversion Test Tree"),
    SVN_TEST_OPTS_PASS(list_directory,
                       "fill a directory, then list it"),
    SVN_TEST_OPTS_PASS(revision_props,
                       "set and get some revision properties"),
    SVN_TEST_OPTS_PASS(transaction_props,
                       "set/get txn props, commit, validate new rev props"),
    SVN_TEST_OPTS_PASS(node_props,
                       "set and get some node properties"),
    SVN_TEST_OPTS_PASS(delete_mutables,
                       "delete mutable nodes from directories"),
    SVN_TEST_OPTS_PASS(delete,
                       "delete nodes tree"),
    SVN_TEST_OPTS_PASS(fetch_youngest_rev,
                       "fetch the youngest revision from a filesystem"),
    SVN_TEST_OPTS_PASS(basic_commit,
                       "basic commit"),
    SVN_TEST_OPTS_PASS(test_tree_node_validation,
                       "testing tree validation helper"),
    SVN_TEST_OPTS_PASS(merging_commit, "merging commit"),
    SVN_TEST_OPTS_PASS(copy_test,
                       "copying and tracking copy history"),
    SVN_TEST_OPTS_PASS(commit_date,
                       "commit datestamps"),
    SVN_TEST_OPTS_PASS(check_old_revisions,
                       "check old revisions"),
    SVN_TEST_OPTS_PASS(check_all_revisions,
                       "after each commit, check all revisions"),
    SVN_TEST_OPTS_PASS(check_root_revision,
                       "ensure accurate storage of root node"),
    SVN_TEST_OPTS_PASS(test_node_created_rev,
                       "svn_fs_node_created_rev test"),
    SVN_TEST_OPTS_PASS(check_related,
                       "test svn_fs_check_related"),
    SVN_TEST_OPTS_PASS(branch_test,
                       "test complex copies (branches)"),
    SVN_TEST_OPTS_PASS(verify_checksum,
                       "test checksums"),
    SVN_TEST_OPTS_PASS(closest_copy_test,
                       "calculating closest history-affecting copies"),
    SVN_TEST_OPTS_PASS(root_revisions,
                       "svn_fs_root_t (base) revisions"),
    SVN_TEST_OPTS_PASS(unordered_txn_dirprops,
                       "test dir prop preservation in unordered txns"),
    SVN_TEST_OPTS_PASS(set_uuid,
                       "test svn_fs_set_uuid"),
    SVN_TEST_OPTS_PASS(node_origin_rev,
                       "test svn_fs_node_origin_rev"),
    SVN_TEST_OPTS_PASS(small_file_integrity,
                       "create and modify small file"),
    SVN_TEST_OPTS_PASS(node_history,
                       "test svn_fs_node_history"),
    SVN_TEST_OPTS_PASS(delete_fs,
                       "test svn_fs_delete_fs"),
    SVN_TEST_OPTS_PASS(filename_trailing_newline,
                       "filenames with trailing \\n might be rejected"),
    SVN_TEST_OPTS_PASS(test_fs_info_format,
                       "test svn_fs_info_format"),
    SVN_TEST_OPTS_PASS(commit_timestamp,
                       "commit timestamp"),
    SVN_TEST_OPTS_PASS(test_compat_version,
                       "test svn_fs__compatible_version"),
    SVN_TEST_OPTS_PASS(dir_prop_merge,
                       "test merge directory properties"),
    SVN_TEST_OPTS_PASS(upgrade_while_committing,
                       "upgrade while committing"),
    SVN_TEST_OPTS_PASS(test_paths_changed,
                       "test svn_fs_paths_changed"),
    SVN_TEST_OPTS_PASS(test_delete_replaced_paths_changed,
                       "test deletion after replace in changed paths list"),
    SVN_TEST_OPTS_PASS(purge_txn_test,
                       "test purging transactions"),
    SVN_TEST_OPTS_PASS(compare_contents,
                       "compare contents of different nodes"),
    SVN_TEST_OPTS_PASS(test_path_change_create,
                       "test svn_fs_path_change2_create"),
    SVN_TEST_OPTS_PASS(test_node_created_info,
                       "test FS 'node created' info"),
    SVN_TEST_OPTS_PASS(test_print_modules,
                       "test FS module listing"),
    SVN_TEST_OPTS_PASS(test_zero_copy_processsing,
                       "test zero copy file processing"),
    SVN_TEST_OPTS_PASS(test_dir_optimal_order,
                       "test svn_fs_dir_optimal_order"),
    SVN_TEST_OPTS_PASS(test_config_files,
                       "get configuration files"),
    SVN_TEST_OPTS_PASS(test_delta_file_stream,
                       "get a delta stream on a file"),
    SVN_TEST_OPTS_PASS(test_fs_merge,
                       "get merging txns with newer revisions"),
    SVN_TEST_OPTS_PASS(test_fsfs_config_opts,
                       "test creating FSFS repository with different opts"),
    SVN_TEST_OPTS_PASS(test_txn_pool_lifetime,
                       "test pool lifetime dependencies with txn roots"),
    SVN_TEST_OPTS_PASS(test_modify_txn_being_written,
                       "test modify txn being written"),
    SVN_TEST_OPTS_PASS(test_prop_and_text_rep_sharing_collision,
                       "test property and text rep-sharing collision"),
    SVN_TEST_OPTS_PASS(test_internal_txn_props,
                       "test setting and getting internal txn props"),
    SVN_TEST_OPTS_PASS(check_txn_related,
                       "test svn_fs_check_related for transactions"),
    SVN_TEST_OPTS_PASS(freeze_and_commit,
                       "freeze and commit"),
    SVN_TEST_OPTS_PASS(test_large_changed_paths_list,
                       "test reading a large changed paths list"),
    SVN_TEST_OPTS_PASS(commit_with_locked_rep_cache,
                       "test commit with locked rep-cache"),
    SVN_TEST_OPTS_PASS(test_cache_clear_during_stream,
                       "test clearing the cache while streaming a rep"),
    SVN_TEST_OPTS_PASS(test_rep_sharing_strict_content_check,
                       "test rep-sharing on content rather than SHA1"),
    SVN_TEST_OPTS_PASS(closest_copy_test_svn_4677,
                       "test issue SVN-4677 regression"),
    SVN_TEST_NULL
  };

SVN_TEST_MAIN
