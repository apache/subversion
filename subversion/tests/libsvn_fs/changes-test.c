/* changes-test.c --- test `changes' interfaces
 *
 * ====================================================================
 * Copyright (c) 2000-2003 CollabNet.  All rights reserved.
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
#include <stdarg.h>
#include <string.h>
#include <stdio.h>

#include <apr.h>

#include "svn_error.h"
#include "svn_test.h"
#include "../fs-helpers.h"
#include "../../libsvn_fs/util/skel.h"
#include "../../libsvn_fs/util/fs_skels.h"
#include "../../libsvn_fs/bdb/changes-table.h"



/* Helper functions/variables.  */
static const char *standard_txns[6]
  = { "0", "1", "2", "3", "4", "5" };
static const char *standard_changes[19][6] 
     /* KEY   PATH   NODEREVID  KIND     TEXT PROP */
  = { { "0",  "/foo",  "1.0.0",  "add",     0,  0  },
      { "0",  "/foo",  "1.0.0",  "modify",  0, "1" },
      { "0",  "/bar",  "2.0.0",  "add",     0,  0  },
      { "0",  "/bar",  "2.0.0",  "modify", "1", 0  },
      { "0",  "/bar",  "2.0.0",  "modify",  0, "1" },
      { "0",  "/baz",  "3.0.0",  "add",     0,  0  },
      { "0",  "/baz",  "3.0.0",  "modify", "1", 0  },
      { "1",  "/foo",  "1.0.1",  "modify", "1", 0  },
      { "2",  "/foo",  "1.0.2",  "modify",  0, "1" },
      { "2",  "/bar",  "2.0.2",  "modify", "1", 0  },
      { "3",  "/baz",  "3.0.3",  "modify", "1", 0  },
      { "4",  "/fob",  "4.0.4",  "add",     0,  0  },
      { "4",  "/fob",  "4.0.4",  "modify", "1", 0  },
      { "5",  "/baz",  "3.0.3",  "delete",  0,  0  },
      { "5",  "/baz",  "5.0.5",  "add",     0, "1" },
      { "5",  "/baz",  "5.0.5",  "modify", "1", 0  },
      { "6",  "/fob",  "4.0.6",  "modify", "1", 0  },
      { "6",  "/fob",  "4.0.6",  "reset",   0,  0  },
      { "6",  "/fob",  "4.0.6",  "modify",  0, "1" } };


static svn_fs_path_change_kind_t string_to_kind (const char *str)
{
  if (strcmp (str, "add") == 0)
    return svn_fs_path_change_add;
  if (strcmp (str, "delete") == 0)
    return svn_fs_path_change_delete;
  if (strcmp (str, "replace") == 0)
    return svn_fs_path_change_replace;
  if (strcmp (str, "modify") == 0)
    return svn_fs_path_change_modify;
  if (strcmp (str, "reset") == 0)
    return svn_fs_path_change_reset;
  return 0;
}


/* Common args structure for several different txn_body_* functions. */
struct changes_args
{
  svn_fs_t *fs;
  const char *key;
  svn_fs__change_t *change;
  apr_array_header_t *raw_changes;
  apr_hash_t *changes;
};


static svn_error_t *
txn_body_changes_add (void *baton, trail_t *trail)
{
  struct changes_args *b = baton;
  return svn_fs__bdb_changes_add (b->fs, b->key, b->change, trail);
}


static svn_error_t *
add_standard_changes (svn_fs_t *fs,
                      apr_pool_t *pool)
{
  int i;
  struct changes_args args;
  int num_changes = sizeof (standard_changes) / sizeof (const char *) / 6;

  for (i = 0; i < num_changes; i++)
    {
      svn_fs__change_t change;

      /* Set up the current change item. */
      change.path = standard_changes[i][1];
      change.noderev_id = svn_fs_parse_id (standard_changes[i][2], 
                                           strlen (standard_changes[i][2]), 
                                           pool);
      change.kind = string_to_kind (standard_changes[i][3]);
      change.text_mod = standard_changes[i][4] ? 1 : 0;
      change.prop_mod = standard_changes[i][5] ? 1 : 0;

      /* Set up transaction baton. */
      args.fs = fs;
      args.key = standard_changes[i][0];
      args.change = &change;

      /* Write new changes to the changes table. */
      SVN_ERR (svn_fs__retry_txn (args.fs, txn_body_changes_add, &args, pool));
    }

  return SVN_NO_ERROR;
}


static svn_error_t *
txn_body_changes_fetch_raw (void *baton, trail_t *trail)
{
  struct changes_args *b = baton;
  return svn_fs__bdb_changes_fetch_raw (&(b->raw_changes), b->fs, b->key, trail);
}


static svn_error_t *
txn_body_changes_fetch (void *baton, trail_t *trail)
{
  struct changes_args *b = baton;
  return svn_fs__bdb_changes_fetch (&(b->changes), b->fs, b->key, trail);
}


static svn_error_t *
txn_body_changes_delete (void *baton, trail_t *trail)
{
  struct changes_args *b = baton;
  return svn_fs__bdb_changes_delete (b->fs, b->key, trail);
}



/* The tests.  */

static svn_error_t *
changes_add (const char **msg, 
             svn_boolean_t msg_only,
             apr_pool_t *pool)
{
  svn_fs_t *fs;

  *msg = "Add changes to the changes table.";

  if (msg_only)
    return SVN_NO_ERROR;

  /* Create a new fs and repos */
  SVN_ERR (svn_test__create_fs (&fs, "test-repo-changes-add", pool));

  /* Add the standard slew of changes. */
  SVN_ERR (add_standard_changes (fs, pool));

  /* Close the filesystem. */
  SVN_ERR (svn_fs_close_fs (fs));

  return SVN_NO_ERROR;
}


static svn_error_t *
changes_fetch_raw (const char **msg, 
                   svn_boolean_t msg_only,
                   apr_pool_t *pool)
{
  svn_fs_t *fs;
  int i;
  int num_txns = sizeof (standard_txns) / sizeof (const char *);
  int cur_change_index = 0;
  struct changes_args args;

  *msg = "Fetch raw changes from the changes table.";

  if (msg_only)
    return SVN_NO_ERROR;

  /* Create a new fs and repos */
  SVN_ERR (svn_test__create_fs (&fs, "test-repo-changes-fetch", pool));

  /* First, verify that we can request changes for an arbitrary key
     without error. */
  args.fs = fs;
  args.key = "blahbliggityblah";
  SVN_ERR (svn_fs__retry_txn (args.fs, txn_body_changes_fetch_raw, 
                              &args, pool));
  if ((! args.raw_changes) || (args.raw_changes->nelts))
    return svn_error_create (SVN_ERR_TEST_FAILED, NULL,
                             "expected empty changes array");
  
  /* Add the standard slew of changes. */
  SVN_ERR (add_standard_changes (fs, pool));

  /* For each transaction, fetch that transaction's changes, and
     compare those changes against the standard changes list.  Order
     matters throughout all the changes code, so we shouldn't have to
     worry about ordering of the arrays.  */
  for (i = 0; i < num_txns; i++)
    {
      const char *txn_id = standard_txns[i];
      int j;

      /* Setup the trail baton. */
      args.fs = fs;
      args.key = txn_id;

      /* And get those changes. */
      SVN_ERR (svn_fs__retry_txn (args.fs, txn_body_changes_fetch_raw, 
                                  &args, pool));
      if (! args.raw_changes)
        return svn_error_createf (SVN_ERR_TEST_FAILED, NULL,
                                  "got no changes for key `%s'", txn_id);

      for (j = 0; j < args.raw_changes->nelts; j++)
        {
          svn_string_t *noderev_id;
          svn_fs_path_change_kind_t kind;
          svn_fs__change_t *change 
            = APR_ARRAY_IDX (args.raw_changes, j, svn_fs__change_t *);
          int mod_bit = 0;

          /* Verify that the TXN_ID matches. */
          if (strcmp (standard_changes[cur_change_index][0], txn_id))
            return svn_error_createf 
              (SVN_ERR_TEST_FAILED, NULL,
               "missing some changes for key `%s'", txn_id);
            
          /* Verify that the PATH matches. */
          if (strcmp (standard_changes[cur_change_index][1], change->path))
            return svn_error_createf 
              (SVN_ERR_TEST_FAILED, NULL,
               "paths differ in change for key `%s'", txn_id);

          /* Verify that the NODE-REV-ID matches. */
          noderev_id = svn_fs_unparse_id (change->noderev_id, pool);
          if (strcmp (standard_changes[cur_change_index][2], noderev_id->data))
            return svn_error_createf 
              (SVN_ERR_TEST_FAILED, NULL,
               "node revision ids differ in change for key `%s'", txn_id);

          /* Verify that the change KIND matches. */
          kind = string_to_kind (standard_changes[cur_change_index][3]);
          if (kind != change->kind)
            return svn_error_createf 
              (SVN_ERR_TEST_FAILED, NULL,
               "change kinds differ in change for key `%s'", txn_id);

          /* Verify that the change TEXT-MOD bit matches. */
          mod_bit = standard_changes[cur_change_index][4] ? 1 : 0;
          if (mod_bit != change->text_mod)
            return svn_error_createf 
              (SVN_ERR_TEST_FAILED, NULL,
               "change text-mod bits differ in change for key `%s'", txn_id);

          /* Verify that the change PROP-MOD bit matches. */
          mod_bit = standard_changes[cur_change_index][5] ? 1 : 0;
          if (mod_bit != change->prop_mod)
            return svn_error_createf 
              (SVN_ERR_TEST_FAILED, NULL,
               "change prop-mod bits differ in change for key `%s'", txn_id);

          cur_change_index++;
        }
    }

  /* Close the filesystem. */
  SVN_ERR (svn_fs_close_fs (fs));

  return SVN_NO_ERROR;
}


static svn_error_t *
changes_delete (const char **msg, 
                svn_boolean_t msg_only,
                apr_pool_t *pool)
{
  svn_fs_t *fs;
  int i;
  int num_txns = sizeof (standard_txns) / sizeof (const char *);
  struct changes_args args;

  *msg = "Delete changes from the changes table.";

  if (msg_only)
    return SVN_NO_ERROR;

  /* Create a new fs and repos */
  SVN_ERR (svn_test__create_fs (&fs, "test-repo-changes-delete", pool));

  /* Add the standard slew of changes. */
  SVN_ERR (add_standard_changes (fs, pool));

  /* Now, delete all the changes we know about, verifying their removal. */
  for (i = 0; i < num_txns; i++)
    {
      args.fs = fs;
      args.key = standard_txns[i];
      SVN_ERR (svn_fs__retry_txn (args.fs, txn_body_changes_delete, 
                                  &args, pool));
      args.changes = 0;
      SVN_ERR (svn_fs__retry_txn (args.fs, txn_body_changes_fetch_raw, 
                                  &args, pool));
      if ((! args.raw_changes) || (args.raw_changes->nelts))
        return svn_error_createf 
          (SVN_ERR_TEST_FAILED, NULL,
           "expected empty changes array for txn `%s'", args.key);
    }

  /* Close the filesystem. */
  SVN_ERR (svn_fs_close_fs (fs));

  return SVN_NO_ERROR;
}


static apr_hash_t *
get_ideal_changes (const char *txn_id,
                   apr_pool_t *pool)
{
  apr_hash_t *ideal = apr_hash_make (pool);
  svn_fs_path_change_t *change;
  if (strcmp (txn_id, "0") == 0)
    {
      change = apr_palloc (pool, sizeof (*change));
      change->node_rev_id = svn_fs_parse_id ("1.0.0", 5, pool);
      change->change_kind = svn_fs_path_change_add;
      change->text_mod = 0;
      change->prop_mod = 1;
      apr_hash_set (ideal, "/foo", APR_HASH_KEY_STRING, change);

      change = apr_palloc (pool, sizeof (*change));
      change->node_rev_id = svn_fs_parse_id ("2.0.0", 5, pool);
      change->change_kind = svn_fs_path_change_add;
      change->text_mod = 1;
      change->prop_mod = 1;
      apr_hash_set (ideal, "/bar", APR_HASH_KEY_STRING, change);

      change = apr_palloc (pool, sizeof (*change));
      change->node_rev_id = svn_fs_parse_id ("3.0.0", 5, pool);
      change->change_kind = svn_fs_path_change_add;
      change->text_mod = 1;
      change->prop_mod = 0;
      apr_hash_set (ideal, "/baz", APR_HASH_KEY_STRING, change);
    }
  if (strcmp (txn_id, "1") == 0)
    {
      change = apr_palloc (pool, sizeof (*change));
      change->node_rev_id = svn_fs_parse_id ("1.0.1", 5, pool);
      change->change_kind = svn_fs_path_change_modify;
      change->text_mod = 1;
      change->prop_mod = 0;
      apr_hash_set (ideal, "/foo", APR_HASH_KEY_STRING, change);
    }
  if (strcmp (txn_id, "2") == 0)
    {
      change = apr_palloc (pool, sizeof (*change));
      change->node_rev_id = svn_fs_parse_id ("1.0.2", 5, pool);
      change->change_kind = svn_fs_path_change_modify;
      change->text_mod = 0;
      change->prop_mod = 1;
      apr_hash_set (ideal, "/foo", APR_HASH_KEY_STRING, change);

      change = apr_palloc (pool, sizeof (*change));
      change->node_rev_id = svn_fs_parse_id ("2.0.2", 5, pool);
      change->change_kind = svn_fs_path_change_modify;
      change->text_mod = 1;
      change->prop_mod = 0;
      apr_hash_set (ideal, "/bar", APR_HASH_KEY_STRING, change);
    }
  if (strcmp (txn_id, "3") == 0)
    {
      change = apr_palloc (pool, sizeof (*change));
      change->node_rev_id = svn_fs_parse_id ("3.0.3", 5, pool);
      change->change_kind = svn_fs_path_change_modify;
      change->text_mod = 1;
      change->prop_mod = 0;
      apr_hash_set (ideal, "/baz", APR_HASH_KEY_STRING, change);
    }
  if (strcmp (txn_id, "4") == 0)
    {
      change = apr_palloc (pool, sizeof (*change));
      change->node_rev_id = svn_fs_parse_id ("4.0.4", 5, pool);
      change->change_kind = svn_fs_path_change_add;
      change->text_mod = 1;
      change->prop_mod = 0;
      apr_hash_set (ideal, "/fob", APR_HASH_KEY_STRING, change);
    }
  if (strcmp (txn_id, "5") == 0)
    {
      change = apr_palloc (pool, sizeof (*change));
      change->node_rev_id = svn_fs_parse_id ("5.0.5", 5, pool);
      change->change_kind = svn_fs_path_change_replace;
      change->text_mod = 1;
      change->prop_mod = 1;
      apr_hash_set (ideal, "/baz", APR_HASH_KEY_STRING, change);
    }
  if (strcmp (txn_id, "6") == 0)
    {
      change = apr_palloc (pool, sizeof (*change));
      change->node_rev_id = svn_fs_parse_id ("4.0.6", 5, pool);
      change->change_kind = svn_fs_path_change_modify;
      change->text_mod = 0;
      change->prop_mod = 1;
      apr_hash_set (ideal, "/fob", APR_HASH_KEY_STRING, change);
    }
  return ideal;
}


static svn_error_t *
compare_changes (apr_hash_t *ideals,
                 apr_hash_t *changes,
                 const char *txn_id,
                 apr_pool_t *pool)
{
  apr_hash_index_t *hi;
  
  for (hi = apr_hash_first (pool, ideals); hi; hi = apr_hash_next (hi))
    {
      const void *key;
      void *val;
      svn_fs_path_change_t *ideal_change, *change;
      const char *path;

      /* KEY will be the path, VAL the change. */
      apr_hash_this (hi, &key, NULL, &val);
      path = (const char *) key;
      ideal_change = val;

      /* Now get the change that refers to PATH in the actual
         changes hash. */
      change = apr_hash_get (changes, path, APR_HASH_KEY_STRING);
      if (! change)
        return svn_error_createf 
          (SVN_ERR_TEST_FAILED, NULL,
           "missing expected change for path `%s' in txn_id `%s'", 
           path, txn_id);
            
      /* Verify that the NODE-REV-ID matches. */
      if (svn_fs_compare_ids (change->node_rev_id, 
                              ideal_change->node_rev_id))
        return svn_error_createf 
          (SVN_ERR_TEST_FAILED, NULL,
           "node revision ids differ in change for key `%s'", txn_id);

      /* Verify that the change KIND matches. */
      if (change->change_kind != ideal_change->change_kind)
        return svn_error_createf 
          (SVN_ERR_TEST_FAILED, NULL,
           "change kinds differ in change for key `%s'", txn_id);

      /* Verify that the change TEXT-MOD bit matches. */
      if (change->text_mod != ideal_change->text_mod)
        return svn_error_createf 
          (SVN_ERR_TEST_FAILED, NULL,
           "change text-mod bits differ in change for key `%s'", txn_id);

      /* Verify that the change PROP-MOD bit matches. */
      if (change->prop_mod != ideal_change->prop_mod)
        return svn_error_createf 
          (SVN_ERR_TEST_FAILED, NULL,
           "change prop-mod bits differ in change for key `%s'", txn_id);
    }

  return SVN_NO_ERROR;
}


static svn_error_t *
changes_fetch (const char **msg, 
               svn_boolean_t msg_only,
               apr_pool_t *pool)
{
  svn_fs_t *fs;
  int i;
  int num_txns = sizeof (standard_txns) / sizeof (const char *);
  struct changes_args args;
  *msg = "Fetch compressed changes from the changes table.";

  if (msg_only)
    return SVN_NO_ERROR;

  /* Create a new fs and repos */
  SVN_ERR (svn_test__create_fs (&fs, "test-repo-changes-fetch", pool));

  /* First, verify that we can request changes for an arbitrary key
     without error. */
  args.fs = fs;
  args.key = "blahbliggityblah";
  SVN_ERR (svn_fs__retry_txn (fs, txn_body_changes_fetch, &args, pool));
  if ((! args.changes) || (apr_hash_count (args.changes)))
    return svn_error_create (SVN_ERR_TEST_FAILED, NULL,
                             "expected empty changes hash");
  
  /* Add the standard slew of changes. */
  SVN_ERR (add_standard_changes (fs, pool));

  /* For each transaction, fetch that transaction's changes, and
     compare those changes against our ideal compressed changes
     hash. */
  for (i = 0; i < num_txns; i++)
    {
      const char *txn_id = standard_txns[i];
      apr_hash_t *ideals;

      /* Get the ideal changes hash. */
      ideals = get_ideal_changes (txn_id, pool);

      /* Setup the trail baton. */
      args.fs = fs;
      args.key = txn_id;

      /* And get those changes via in the internal interface, and
         verify that they are accurate. */
      SVN_ERR (svn_fs__retry_txn (fs, txn_body_changes_fetch, &args, pool));
      if (! args.changes)
        return svn_error_createf 
          (SVN_ERR_TEST_FAILED, NULL,
           "got no changes for key `%s'", txn_id);
      if (apr_hash_count (ideals) != apr_hash_count (args.changes))
        return svn_error_createf 
          (SVN_ERR_TEST_FAILED, NULL,
           "unexpected number of changes for key `%s'", txn_id);
      SVN_ERR (compare_changes (ideals, args.changes, txn_id, pool));
    }

  /* Close the filesystem. */
  SVN_ERR (svn_fs_close_fs (fs));

  return SVN_NO_ERROR;
}



/* The test table.  */

struct svn_test_descriptor_t test_funcs[] =
  {
    SVN_TEST_NULL,
    SVN_TEST_PASS (changes_add),
    SVN_TEST_PASS (changes_fetch_raw),
    SVN_TEST_PASS (changes_delete),
    SVN_TEST_PASS (changes_fetch),
    SVN_TEST_NULL
  };
