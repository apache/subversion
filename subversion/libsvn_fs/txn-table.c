/* txn-table.c : operations on the `transactions' table
 *
 * ====================================================================
 * Copyright (c) 2000 CollabNet.  All rights reserved.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution.  The terms
 * are also available at http://subversion.tigris.org/license-1.html.
 * If newer versions of this license are posted there, you may use a
 * newer version instead, at your option.
 * ====================================================================
 */

#include "db.h"
#include "fs.h"
#include "err.h"
#include "convert-size.h"
#include "dbt.h"
#include "txn-table.h"
#include "trail.h"


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
  {
    DBT key, value;

    DB_ERR (txns->put (txns, 0,
		       svn_fs__str_to_dbt (&key, "next-id"),
		       svn_fs__str_to_dbt (&value, "0"),
		       0));
  }

  *transactions_p = txns;
  return 0;
}


/* Allocate a Subversion transaction ID in FS, as part of TRAIL.  Set
   *ID_P to the new transaction ID.  */
static svn_error_t *
allocate_txn_id (char **id_p,
		 svn_fs_t *fs,
		 trail_t *trail)
{
  DBT key, value;
  apr_size_t next_id;
  char *next_id_str;

  svn_fs__str_to_dbt (&key, "next-id");

  /* Get the current value associated with the `next-id' key in the
     transactions table.  */
  SVN_ERR (DB_WRAP (fs, "allocating new transaction ID (getting `next-id')",
		    fs->transactions->get (fs->transactions, trail->db_txn,
					   &key,
					   svn_fs__result_dbt (&value),
					   0)));

  /* That's the value we want to return.  */
  next_id_str = apr_pstrndup (trail->pool, value.data, value.size);

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
  return 0;
}


svn_error_t *
svn_fs__create_txn (char **txn_id_p,
		    svn_fs_t *fs,
		    const svn_fs_id_t *root_id,
		    trail_t *trail)
{
  apr_pool_t *pool = trail->pool;
  char *svn_txn_id;
  skel_t *txn_skel;
  svn_string_t *unparsed_root_id = svn_fs_unparse_id (root_id, pool);
  DBT key, value;

  SVN_ERR (allocate_txn_id (&svn_txn_id, fs, trail));
  txn_skel = svn_fs__make_empty_list (pool);
  svn_fs__prepend (svn_fs__mem_atom (unparsed_root_id->data,
				     unparsed_root_id->len,
				     pool),
		   txn_skel);
  svn_fs__prepend (svn_fs__mem_atom (unparsed_root_id->data,
				     unparsed_root_id->len,
				     pool),
		   txn_skel);
  svn_fs__prepend (svn_fs__str_atom ("transaction", pool), txn_skel);

  svn_fs__str_to_dbt (&key, svn_txn_id);
  svn_fs__skel_to_dbt (&value, txn_skel, pool);
  SVN_ERR (DB_WRAP (fs, "creating transaction record",
		    fs->transactions->put (fs->transactions, trail->db_txn,
					   &key, &value, 0)));

  return 0;
}


svn_error_t *
svn_fs__get_txn (svn_fs_id_t **root_id_p,
		 svn_fs_id_t **base_root_id_p,
		 svn_fs_t *fs,
		 const char *svn_txn,
		 trail_t *trail)
{
  abort ();
}


svn_error_t *
svn_fs__set_txn_root (svn_fs_t *fs,
		      const char *svn_txn,
		      const svn_fs_id_t *root_id,
		      trail_t *trail)
{
  abort ();
}
