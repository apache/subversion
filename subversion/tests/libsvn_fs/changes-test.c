/* changes-test.c --- test `changes' interfaces
 *
 * ====================================================================
 * Copyright (c) 2000-2002 CollabNet.  All rights reserved.
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
#include "svn_error.h"
#include "apr.h"
#include "../fs-helpers.h"
#include "../../libsvn_fs/util/skel.h"
#include "../../libsvn_fs/util/fs_skels.h"
#include "../../libsvn_fs/bdb/changes-table.h"



/* Helper functions/variables.  */
static const char *standard_txns[6]
  = { "0", "1", "2", "3", "4", "5" };
static const char *standard_changes[16][4] 
     /* KEY   PATH   NODEREVID  KIND      */
  = { { "0",  "foo",  "0.1.0",  "add"      },
      { "0",  "foo",  "0.1.0",  "text-mod" },
      { "0",  "bar",  "0.2.0",  "add"      },
      { "0",  "bar",  "0.2.0",  "text-mod" },
      { "0",  "bar",  "0.2.0",  "prop-mod" },
      { "0",  "baz",  "0.3.0",  "add"      },
      { "0",  "baz",  "0.3.0",  "text-mod" },
      { "1",  "foo",  "0.1.1",  "text-mod" },
      { "2",  "foo",  "0.1.2",  "prop-mod" },
      { "2",  "bar",  "0.2.2",  "text-mod" },
      { "3",  "baz",  "0.3.3",  "text-mod" },
      { "4",  "fob",  "0.4.4",  "add"      },
      { "4",  "fob",  "0.4.4",  "text-mod" },
      { "5",  "baz",  "0.3.3",  "delete"   },
      { "5",  "baz",  "0.5.5",  "add"      },
      { "5",  "baz",  "0.5.5",  "text-mod" } };


static svn_fs__change_kind_t string_to_kind (const char *str)
{
  if (strcmp (str, "add") == 0)
    return svn_fs__change_add;
  if (strcmp (str, "delete") == 0)
    return svn_fs__change_delete;
  if (strcmp (str, "replace") == 0)
    return svn_fs__change_replace;
  if (strcmp (str, "text-mod") == 0)
    return svn_fs__change_text_mod;
  if (strcmp (str, "prop-mod") == 0)
    return svn_fs__change_prop_mod;
  return 0;
}


/* Common args structure for several different txn_body_* functions. */
struct changes_args
{
  svn_fs_t *fs;
  const char *key;
  svn_fs__change_t *change;
  apr_array_header_t *changes;
};


static svn_error_t *
txn_body_changes_add (void *baton, trail_t *trail)
{
  struct changes_args *b = baton;
  return svn_fs__changes_add (b->fs, b->key, b->change, trail);
}


static svn_error_t *
add_standard_changes (svn_fs_t *fs,
                      apr_pool_t *pool)
{
  int i;
  struct changes_args args;
  int num_changes = sizeof (standard_changes) / sizeof (const char *) / 4;

  for (i = 0; i < num_changes; i++)
    {
      svn_fs__change_t change;

      /* Set up the current change item. */
      change.path = standard_changes[i][1];
      change.noderev_id = svn_fs_parse_id (standard_changes[i][2], 
                                           strlen (standard_changes[i][2]), 
                                           pool);
      change.kind = string_to_kind (standard_changes[i][3]);

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
txn_body_changes_fetch (void *baton, trail_t *trail)
{
  struct changes_args *b = baton;
  return svn_fs__changes_fetch (&(b->changes), b->fs, b->key, trail);
}


static svn_error_t *
txn_body_changes_delete (void *baton, trail_t *trail)
{
  struct changes_args *b = baton;
  return svn_fs__changes_delete (b->fs, b->key, trail);
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
changes_fetch (const char **msg, 
               svn_boolean_t msg_only,
               apr_pool_t *pool)
{
  svn_fs_t *fs;
  int i;
  int num_txns = sizeof (standard_txns) / sizeof (const char *);
  int cur_change_index = 0;
  struct changes_args args;

  *msg = "Fetch changes from the changes table.";

  if (msg_only)
    return SVN_NO_ERROR;

  /* Create a new fs and repos */
  SVN_ERR (svn_test__create_fs (&fs, "test-repo-changes-fetch", pool));

  /* First, verify that we can request changes for an arbitrary key
     without error. */
  args.fs = fs;
  args.key = "blahbliggityblah";
  SVN_ERR (svn_fs__retry_txn (args.fs, txn_body_changes_fetch, &args, pool));
  if ((! args.changes) || (args.changes->nelts))
    return svn_error_create (SVN_ERR_TEST_FAILED, 0, NULL, pool,
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
      SVN_ERR (svn_fs__retry_txn (args.fs, txn_body_changes_fetch, 
                                  &args, pool));
      if (! args.changes)
        return svn_error_createf (SVN_ERR_TEST_FAILED, 0, NULL, pool,
                                  "got no changes for key `%s'", txn_id);

      for (j = 0; j < args.changes->nelts; j++)
        {
          svn_string_t *noderev_id;
          svn_fs__change_kind_t kind;
          svn_fs__change_t *change 
            = APR_ARRAY_IDX (args.changes, j, svn_fs__change_t *);

          /* Verify that the TXN_ID matches. */
          if (strcmp (standard_changes[cur_change_index][0], txn_id))
            return svn_error_createf 
              (SVN_ERR_TEST_FAILED, 0, NULL, pool,
               "missing some changes for key `%s'", txn_id);
            
          /* Verify that the PATH matches. */
          if (strcmp (standard_changes[cur_change_index][1], change->path))
            return svn_error_createf 
              (SVN_ERR_TEST_FAILED, 0, NULL, pool,
               "paths differ in change for key `%s'", txn_id);

          /* Verify that the NODE-REV-ID matches. */
          noderev_id = svn_fs_unparse_id (change->noderev_id, pool);
          if (strcmp (standard_changes[cur_change_index][2], noderev_id->data))
            return svn_error_createf 
              (SVN_ERR_TEST_FAILED, 0, NULL, pool,
               "node revision ids differ in change for key `%s'", txn_id);

          /* Verify that the change KIND matches. */
          kind = string_to_kind (standard_changes[cur_change_index][3]);
          if (kind != change->kind)
            return svn_error_createf 
              (SVN_ERR_TEST_FAILED, 0, NULL, pool,
               "change kinds differ in change for key `%s'", txn_id);

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
      SVN_ERR (svn_fs__retry_txn (args.fs, txn_body_changes_fetch, 
                                  &args, pool));
      if ((! args.changes) || (args.changes->nelts))
        return svn_error_createf 
          (SVN_ERR_TEST_FAILED, 0, NULL, pool,
           "expected empty changes array for txn `%s'", args.key);
    }

  /* Close the filesystem. */
  SVN_ERR (svn_fs_close_fs (fs));

  return SVN_NO_ERROR;
}



/* The test table.  */

svn_error_t * (*test_funcs[]) (const char **msg,
                               svn_boolean_t msg_only,
                               apr_pool_t *pool) = {
  0,
  changes_add,
  changes_fetch,
  changes_delete,
  0
};



/* 
 * local variables:
 * eval: (load-file "../../../tools/dev/svn-dev.el")
 * end:
 */
