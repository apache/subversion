/* txn-table.c : operations on the `transactions' table
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

#include <string.h>

#include "db.h"
#include "fs.h"
#include "err.h"
#include "key-gen.h"
#include "dbt.h"
#include "skel.h"
#include "fs_skels.h"
#include "txn-table.h"
#include "trail.h"
#include "validate.h"
#include "id.h"

static const char next_id_key[] = "next-id";


int
svn_fs__open_transactions_table (DB **transactions_p,
                                 DB_ENV *env,
                                 int create)
{
  DB *txns;

  DB_ERR (db_create (&txns, env, 0));
  DB_ERR (txns->open (txns, "transactions", 0, DB_BTREE,
                      create ? (DB_CREATE | DB_EXCL) : 0,
                      0666));

  /* Create the `next-id' table entry.  */
  if (create)
  {
    DBT key, value;

    DB_ERR (txns->put (txns, 0,
                       svn_fs__str_to_dbt (&key, (char *) next_id_key),
                       svn_fs__str_to_dbt (&value, (char *) "0"),
                       0));
  }

  *transactions_p = txns;
  return 0;
}


/* Store TXN as a transaction named TXN_NAME in FS as part of TRAIL.  */
static svn_error_t *
put_txn (svn_fs_t *fs,
         const svn_fs__transaction_t *txn,
         const char *txn_name,
         trail_t *trail)
{
  skel_t *txn_skel;
  DBT key, value;

  /* Convert native type to skel. */
  SVN_ERR (svn_fs__unparse_transaction_skel (&txn_skel, txn, trail->pool));

  /* Only in the context of this function do we know that the DB call
     will not attempt to modify txn_name, so the cast belongs here.  */
  svn_fs__str_to_dbt (&key, (char *) txn_name);
  svn_fs__skel_to_dbt (&value, txn_skel, trail->pool);
  SVN_ERR (DB_WRAP (fs, "storing transaction record",
                    fs->transactions->put (fs->transactions, trail->db_txn,
                                           &key, &value, 0)));

  return SVN_NO_ERROR;
}


/* Allocate a Subversion transaction ID in FS, as part of TRAIL.  Set
   *ID_P to the new transaction ID, allocated in TRAIL->pool.  */
static svn_error_t *
allocate_txn_id (char **id_p,
                 svn_fs_t *fs,
                 trail_t *trail)
{
  DBT key, value;
  apr_size_t next_id;
  char *next_id_str;

  svn_fs__str_to_dbt (&key, (char *) next_id_key);

  /* Get the current value associated with the `next-id' key in the
     transactions table.  */
  SVN_ERR (DB_WRAP (fs, "allocating new transaction ID (getting `next-id')",
                    fs->transactions->get (fs->transactions, trail->db_txn,
                                           &key,
                                           svn_fs__result_dbt (&value),
                                           0)));
  svn_fs__track_dbt (&value, trail->pool);

  /* That's the value we want to return.  */
  next_id_str = apr_pstrmemdup (trail->pool, value.data, value.size);

  /* Try to parse the value.  */
  {
    const char *endptr;

    next_id = svn_fs__getsize (value.data, value.size, &endptr, 1000000);
    if (endptr != (const char *) value.data + value.size)
      return svn_fs__err_corrupt_next_txn_id (fs);
  }

  /* Store the next value.  */
  {
    char buf[200];
    int buf_len;

    buf_len = svn_fs__putsize (buf, sizeof (buf), next_id + 1);
    SVN_ERR (DB_WRAP (fs, "allocating new transaction ID (setting `next-id')",
                      fs->transactions->put (fs->transactions, trail->db_txn,
                                             &key,
                                             svn_fs__set_dbt (&value, 
                                                              buf, buf_len),
                                             0)));
  }

  *id_p = next_id_str;
  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs__create_txn (char **txn_name_p,
                    svn_fs_t *fs,
                    const svn_fs_id_t *root_id,
                    trail_t *trail)
{
  char *txn_name;
  svn_fs__transaction_t txn;

  SVN_ERR (allocate_txn_id (&txn_name, fs, trail));
  txn.root_id = root_id;
  txn.base_root_id = root_id;
  txn.proplist = NULL;
  SVN_ERR (put_txn (fs, &txn, txn_name, trail));

  *txn_name_p = txn_name; 
  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs__delete_txn (svn_fs_t *fs,
                    const char *txn_name,
                    trail_t *trail)
{
  DBT key;

  svn_fs__str_to_dbt (&key, (char *) txn_name);
  SVN_ERR (DB_WRAP (fs, "deleting entry from `transactions' table",
                    fs->transactions->del (fs->transactions,
                                           trail->db_txn, &key, 0)));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs__get_txn (svn_fs__transaction_t **txn_p,
                 svn_fs_t *fs,
                 const char *txn_name,
                 trail_t *trail)
{
  DBT key, value;
  int db_err;
  skel_t *skel;
  svn_fs__transaction_t *transaction;

  /* Only in the context of this function do we know that the DB call
     will not attempt to modify txn_name, so the cast belongs here.  */
  db_err = fs->transactions->get (fs->transactions, trail->db_txn,
                                  svn_fs__str_to_dbt (&key, (char *) txn_name),
                                  svn_fs__result_dbt (&value),
                                  0);
  svn_fs__track_dbt (&value, trail->pool);

  if (db_err == DB_NOTFOUND)
    return svn_fs__err_no_such_txn (fs, txn_name);
  SVN_ERR (DB_WRAP (fs, "reading transaction", db_err));

  /* Parse TRANSACTION skel */
  skel = svn_fs__parse_skel (value.data, value.size, trail->pool);
  if (! skel)
    return svn_fs__err_corrupt_txn (fs, txn_name);

  /* Convert skel to native type. */
  SVN_ERR (svn_fs__parse_transaction_skel (&transaction, skel, trail->pool));
  *txn_p = transaction;
  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs__get_txn_ids (const svn_fs_id_t **root_id_p,
                     const svn_fs_id_t **base_root_id_p,
                     svn_fs_t *fs,
                     const char *txn_name,
                     trail_t *trail)
{
  svn_fs__transaction_t *txn;
  
  SVN_ERR (svn_fs__get_txn (&txn, fs, txn_name, trail));
  *root_id_p = txn->root_id;
  *base_root_id_p = txn->base_root_id;
  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs__set_txn_root (svn_fs_t *fs,
                      const char *txn_name,
                      const svn_fs_id_t *new_id,
                      trail_t *trail)
{
  svn_fs__transaction_t *txn;

  SVN_ERR (svn_fs__get_txn (&txn, fs, txn_name, trail));
  if (! svn_fs__id_eq (txn->root_id, new_id))
    {
      txn->root_id = new_id;
      SVN_ERR (put_txn (fs, txn, txn_name, trail));
    }
  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs__set_txn_base (svn_fs_t *fs,
                      const char *txn_name,
                      const svn_fs_id_t *new_id,
                      trail_t *trail)
{
  svn_fs__transaction_t *txn;

  SVN_ERR (svn_fs__get_txn (&txn, fs, txn_name, trail));
  if (! svn_fs__id_eq (txn->base_root_id, new_id))
    {
      txn->base_root_id = new_id;
      SVN_ERR (put_txn (fs, txn, txn_name, trail));
    }
  return SVN_NO_ERROR;
}


svn_error_t *svn_fs__get_txn_list (char ***names_p,
                                   svn_fs_t *fs,
                                   apr_pool_t *pool,
                                   trail_t *trail)
{
  apr_size_t const next_id_key_len = strlen (next_id_key);

  char **names;
  apr_size_t names_count = 0;
  apr_size_t names_size = 4;

  DBC *cursor;
  DBT key, value;
  int db_err, db_c_err;

  /* Allocate the initial names array */
  names = apr_pcalloc (pool, names_size * sizeof (*names));

  /* Create a database cursor to list the transaction names. */
  SVN_ERR (DB_WRAP (fs, "reading transaction list (opening cursor)",
                    fs->transactions->cursor (fs->transactions, trail->db_txn,
                                              &cursor, 0)));

  /* Build a null-terminated array of keys in the transactions table. */
  for (db_err = cursor->c_get (cursor,
                               svn_fs__result_dbt (&key),
                               svn_fs__nodata_dbt (&value),
                               DB_FIRST);
       db_err == 0;
       db_err = cursor->c_get (cursor,
                               svn_fs__result_dbt (&key),
                               svn_fs__nodata_dbt (&value),
                               DB_NEXT))
    {
      svn_fs__track_dbt (&key, trail->pool);

      /* Ignore the "next-id" key. */
      if (key.size == next_id_key_len
          && 0 == memcmp (key.data, next_id_key, next_id_key_len))
        continue;

      /* Make sure there's enough space in the names array. */
      if (names_count == names_size - 1)
        {
          char **tmp;

          names_size *= 2;
          tmp = apr_pcalloc (pool, names_size * sizeof (*tmp));
          memcpy (tmp, names, names_count * sizeof (*tmp));
          names = tmp;
        }

      names[names_count++] = apr_pstrmemdup(pool, key.data, key.size);
    }

  names[names_count] = NULL;

  /* Check for errors, but close the cursor first. */
  db_c_err = cursor->c_close (cursor);
  if (db_err != DB_NOTFOUND)
    {
      SVN_ERR (DB_WRAP (fs, "reading transaction list (listing keys)",
                        db_err));
    }
  SVN_ERR (DB_WRAP (fs, "reading transaction list (closing cursor)",
                    db_c_err));

  *names_p = names;
  return SVN_NO_ERROR;
}



/* Generic transaction operations.  */


struct txn_prop_args {
  svn_string_t **value_p;
  svn_fs_t *fs;
  const char *id;
  const char *propname;
};


static svn_error_t *
txn_body_txn_prop (void *baton,
                   trail_t *trail)
{
  struct txn_prop_args *args = baton;
  svn_fs__transaction_t *txn;
  
  SVN_ERR (svn_fs__get_txn (&txn, args->fs, args->id, trail)); 
  *(args->value_p) = NULL;
  if (txn->proplist)
    *(args->value_p) = apr_hash_get (txn->proplist, 
                                     args->propname, APR_HASH_KEY_STRING);
  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_txn_prop (svn_string_t **value_p,
                 svn_fs_txn_t *txn,
                 const char *propname,
                 apr_pool_t *pool)
{
  struct txn_prop_args args;
  svn_string_t *value;
  svn_fs_t *fs = svn_fs_txn_fs (txn);

  SVN_ERR (svn_fs__check_fs (fs));

  args.value_p = &value;
  args.fs = fs;
  svn_fs_txn_name (&args.id, txn, pool);
  args.propname = propname;
  SVN_ERR (svn_fs__retry_txn (fs, txn_body_txn_prop, &args, pool));

  *value_p = value;
  return SVN_NO_ERROR;
}


struct txn_proplist_args {
  apr_hash_t **table_p;
  svn_fs_t *fs;
  const char *id;
  svn_revnum_t rev;
};


static svn_error_t *
txn_body_txn_proplist (void *baton, trail_t *trail)
{
  svn_fs__transaction_t *txn;
  struct txn_proplist_args *args = baton;

  SVN_ERR (svn_fs__get_txn (&txn, args->fs, args->id, trail));
  *(args->table_p) = txn->proplist 
                     ? txn->proplist : apr_hash_make (trail->pool);
  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_txn_proplist (apr_hash_t **table_p,
                     svn_fs_txn_t *txn,
                     apr_pool_t *pool)
{
  struct txn_proplist_args args;
  apr_hash_t *table;
  svn_fs_t *fs = svn_fs_txn_fs (txn);

  SVN_ERR (svn_fs__check_fs (fs));

  args.table_p = &table;
  args.fs = fs;
  svn_fs_txn_name (&args.id, txn, pool);
  SVN_ERR (svn_fs__retry_txn (fs, txn_body_txn_proplist, &args, pool));

  *table_p = table;
  return SVN_NO_ERROR;
}


struct change_txn_prop_args {
  svn_fs_t *fs;
  const char *id;
  const char *name;
  const svn_string_t *value;
};


svn_error_t *
svn_fs__set_txn_prop (svn_fs_t *fs,
                      const char *txn_name,
                      const char *name,
                      const svn_string_t *value,
                      trail_t *trail)
{
  svn_fs__transaction_t *txn;

  SVN_ERR (svn_fs__get_txn (&txn, fs, txn_name, trail));

  /* If there's no proplist, but we're just deleting a property, exit now. */
  if ((! txn->proplist) && (! value))
    return SVN_NO_ERROR;

  /* Now, if there's no proplist, we know we need to make one. */
  if (! txn->proplist)
    txn->proplist = apr_hash_make (trail->pool);

  /* Set the property. */
  apr_hash_set (txn->proplist, name, APR_HASH_KEY_STRING, value);

  /* Now overwrite the transaction. */
  return put_txn (fs, txn, txn_name, trail);
}


static svn_error_t *
txn_body_change_txn_prop (void *baton, trail_t *trail)
{
  struct change_txn_prop_args *args = baton;
  return svn_fs__set_txn_prop (args->fs, args->id, args->name, 
                               args->value, trail);
}


svn_error_t *
svn_fs_change_txn_prop (svn_fs_txn_t *txn,
                        const char *name,
                        const svn_string_t *value,
                        apr_pool_t *pool)
{
  struct change_txn_prop_args args;
  svn_fs_t *fs = svn_fs_txn_fs (txn);

  SVN_ERR (svn_fs__check_fs (fs));

  args.fs = fs;
  svn_fs_txn_name (&args.id, txn, pool);
  args.name = name;
  args.value = value;
  SVN_ERR (svn_fs__retry_txn (fs, txn_body_change_txn_prop, &args, pool));

  return SVN_NO_ERROR;
}


/* 
 * local variables:
 * eval: (load-file "../../tools/dev/svn-dev.el")
 * end:
 */
