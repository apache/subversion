/* txn.c : implementation of transaction functions
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

#include <string.h>

#include "apr_strings.h"

#include "svn_fs.h"
#include "fs.h"
#include "txn.h"
#include "revision.h"
#include "node.h"
#include "skel.h"
#include "convert-size.h"
#include "dbt.h"
#include "err.h"
#include "id.h"


/* The private structure underlying the public svn_fs_txn_t typedef.  */

struct svn_fs_txn_t {

  /* This transaction's private pool, a subpool of fs->pool.

     Freeing this must completely clean up the transaction object,
     write back any buffered data, and release any database or system
     resources it holds.  (But don't confused the transaction object
     with the transaction it represents: freeing this does *not* abort
     the transaction.)  */
  apr_pool_t *pool;

  /* The filesystem to which this transaction belongs.  */
  svn_fs_t *fs;

  /* The ID of this transaction --- a null-terminated string.
     This is the key into the `transactions' table.  */
  char *id;

  /* The root directory for this transaction, or zero if the user
     hasn't called svn_fs_replace_root yet.  */
  svn_fs_id_t *root;
};



/* Building error objects.  */


static svn_error_t *
corrupt_txn (svn_fs_txn_t *txn)
{
  return svn_error_createf (SVN_ERR_FS_CORRUPT, 0, 0, txn->fs->pool,
			    "corrupt transaction `%s' in filesystem `%s'",
			    txn->id, txn->fs->env_path);
}


static svn_error_t *
dangling_txn_id (svn_fs_txn_t *txn)
{
  return svn_error_createf (SVN_ERR_FS_CORRUPT, 0, 0, txn->fs->pool,
			    "dangling transaction id `%s' in filesystem `%s'",
			    txn->id, txn->fs->env_path);
}


#if 0
static svn_error_t *
no_such_txn (svn_fs_txn_t *txn)
{
  return svn_error_createf (SVN_ERR_FS_NO_SUCH_TRANSACTION, 0, 0,
			    txn->fs->pool,
			    "no transaction `%s' in filesystem `%s'",
			    txn->id, txn->fs->env_path);
}
#endif


static svn_error_t *
bad_txn_root (svn_fs_txn_t *txn, svn_revnum_t revision)
{
  return
    svn_error_createf
    (SVN_ERR_FS_BAD_REPLACE_ROOT, 0, 0, txn->fs->pool,
     "the root directory of transaction `%s' is not a direct descendent\n"
     "of the root of revision `%ld', in filesystem `%s'",
     txn->id, revision, txn->fs->env_path);
}



/* Creating and opening the database's `transactions' table.  */


/* A comparison function for keys in the `transactions' table.  The
   transactions table is sorted by the numeric value of the key.  This
   means that we can simply seek to the last record in the table to
   find the highest ID currently in use.

   Strictly speaking, this function only needs to handle strings that
   we actually use as keys in the table.  However, if we happen to
   insert garbage keys, and this comparison function doesn't do
   something consistent with them (i.e., something transitive and
   reflexive), we can actually corrupt the btree structure.  Which
   seems unfriendly.

   So this function tries to act as a proper comparison for any two
   arbitrary byte strings.  Two well-formed transaction ID's compare
   by their numeric value; any malformed key comes before any
   well-formed key; and two malformed keys come in byte-by-byte order.  */
static int
compare_transactions_keys (const DBT *ak, const DBT *bk)
{
  int a, b;
  const char *a_end;
  const char *b_end;

  /* Parse the two keys.  */
  a = svn_fs__getsize (ak->data, ak->size, &a_end, 10000000);
  b = svn_fs__getsize (bk->data, bk->size, &b_end, 10000000);

  /* Are they well-formed?  */
  if (a_end != (const char *) ak->data + ak->size)
    a_end = 0;
  if (b_end != (const char *) bk->data + bk->size)
    b_end = 0;

  /* If both keys are well-formed, then compare them numerically.  */
  if (a_end && b_end)
    return a - b;

  /* Malformed keys come before well-formed keys.  */
  if (a_end)
    return 1;
  if (b_end)
    return -1;

  /* Two malformed keys appear in byte-by-byte order.  */
  return svn_fs__compare_dbt (ak, bk);
}


static svn_error_t *
make_transactions (svn_fs_t *fs, int create)
{
  DB *transactions;

  SVN_ERR (DB_WRAP (fs, "allocating `transactions' table object",
		    db_create (&transactions, fs->env, 0)));
  SVN_ERR (DB_WRAP (fs, "setting `transactions' comparison function",
		    transactions->set_bt_compare (transactions,
						  compare_transactions_keys)));
  SVN_ERR (DB_WRAP (fs, 
		    (create
		     ? "creating `transactions' table"
		     : "opening `transactions' table"),
		    transactions->open (transactions,
					"transactions", 0, DB_BTREE,
					create ? (DB_CREATE | DB_EXCL) : 0,
					0666)));

  fs->transactions = transactions;
  return 0;
}


/* Create a new `transactions' table for the new filesystem FS.
   FS->env must already be open; this sets FS->nodes.  */
svn_error_t *
svn_fs__create_transactions (svn_fs_t *fs)
{
  return make_transactions (fs, 1);
}


/* Open the existing `transactions' table for the filesystem FS.
   FS->env must already be open; this sets FS->nodes.  */
svn_error_t *
svn_fs__open_transactions (svn_fs_t *fs)
{
  return make_transactions (fs, 0);
}



/* Storing and retrieving TRANSACTION skels.  */

/* Store the skel TXN_SKEL in the `transactions' table under the
   transaction id ID.  If CREATE is non-zero, return an error if an
   entry for ID already exists in `transactions'.

   Do this as part of the Berkeley DB transaction DB_TXN; if DB_TXN is
   zero, make the change outside of any Berkeley DB transaction.

   Do any necessary temporary allocation in POOL.  */
static svn_error_t *
put_transaction_skel (svn_fs_t *fs, DB_TXN *db_txn,
		      char *id,
		      const skel_t *txn_skel,
		      int create,
		      apr_pool_t *pool)
{
  DB *transactions = fs->transactions;
  DBT key, value;

  SVN_ERR (DB_WRAP (fs, "storing transaction skel",
		    transactions->put (transactions, db_txn,
				       svn_fs__str_to_dbt (&key, id),
				       svn_fs__skel_to_dbt (&value, txn_skel, 
							    pool),
				       create ? DB_NOOVERWRITE : 0)));

  return 0;
}

/* Set *SKEL_P to point to the TRANSACTION skel for SVN_TXN, as part
   of the Berkeley DB transaction DB_TXN.  Allocate the skel and the
   data it points to in POOL.

   Beyond verifying that it's a syntactically valid skel, this doesn't
   validate the data in *SKEL_P at all.  */
static svn_error_t *
get_transaction_skel (skel_t **skel_p,
		      svn_fs_txn_t *svn_txn,
		      DB_TXN *db_txn,
		      apr_pool_t *pool)
{
  svn_fs_t *fs = svn_txn->fs;
  DBT key, value;
  int db_err;
  skel_t *txn_skel;

  svn_fs__set_dbt (&key, svn_txn->id, strlen (svn_txn->id));
  svn_fs__result_dbt (&value);
  db_err = fs->transactions->get (fs->transactions, db_txn, &key, &value, 0);
  if (db_err == DB_NOTFOUND)
    return dangling_txn_id (svn_txn);
  SVN_ERR (DB_WRAP (fs, "reading transaction", db_err));
  svn_fs__track_dbt (&value, pool);

  txn_skel = svn_fs__parse_skel (value.data, value.size, pool);
  if (! txn_skel)
    return corrupt_txn (svn_txn);

  *skel_p = txn_skel;
  return 0;
}



/* Creating transactions.  */


/* The body of the "create a transaction" operation.

   Choose an ID for SVN_TXN, create an entry for it in the
   `transactions' table, and set SVN_TXN->id appropriately.  Set
   *CURSOR_P to the cursor we allocate.  Do all this as part of the
   Berkeley DB transaction DB_TXN.

   In general, transactions may deadlock, and need to be retried.
   This function is the body of code that needs to be retried.  If any
   kind of DB error occurs --- deadlock or a real error --- this
   function simply returns and reports the error.  It is the caller's
   responsibility to abort and retry the transaction.  It is also the
   caller's responsibility to free whatever resources the transaction
   body allocates --- in this case, a cursor.  */
static svn_error_t *
begin_txn_body (void *baton,
		DB_TXN *db_txn)
{
  svn_fs_txn_t *svn_txn = baton;
  svn_error_t *svn_err;
  DB *transactions = svn_txn->fs->transactions;
  DBC *cursor = 0;
  DBT key, value;
  int id;

  /* Create a cursor.  */
  SVN_ERR (DB_WRAP (svn_txn->fs, "creating transaction (allocating cursor)",
		    transactions->cursor (transactions, db_txn, &cursor, 0)));

  /* Use that cursor to get the ID of the last entry in the table.
     We only need to know the key; don't actually read any of the value.  */
  svn_err = DB_WRAP (svn_txn->fs, "creating transaction (getting max id)",
		     cursor->c_get (cursor,
				    svn_fs__result_dbt (&key),
				    svn_fs__nodata_dbt (&value),
				    DB_LAST));
  if (svn_err)
    goto error;

  /* Try to parse the key as a number.  */
  {
    const char *end;
    id = svn_fs__getsize (key.data, key.size, &end, 10000000);

    /* If we didn't consume the entire key as the number, then it's a
       bogus key.  */
    if (end != (const char *) key.data + key.size)
      {
	svn_err = (svn_error_createf
		   (SVN_ERR_FS_CORRUPT, 0, 0, svn_txn->fs->pool,
		    "malformed ID in transaction table of filesystem `%s'",
		    svn_txn->fs->env_path));
	goto error;
      }
  }

  /* Choose a new, distinct ID.  */
  id++;

  /* Write an initial record for the new transaction to the database.  */
  {
    /* An empty transaction skel, `(transaction 0 )', written out
       as a series of initialized `skel_t' objects.  */
    static const skel_t new_txn_skel[] = {
      { 0, "", 0, (skel_t *)&new_txn_skel[1], 0 },
      { 1, "transaction", 11, 0, (skel_t *)&new_txn_skel[2] },
      { 1, "", 0, 0, 0 }
    };
    char id_text[200];
    int id_len = svn_fs__putsize (id_text, sizeof (id_text), id);

    /* Store the transaction skel in the database, under this ID.  */
    id_text[id_len] = 0;
    svn_err = put_transaction_skel (svn_txn->fs, db_txn, id_text,
				    &new_txn_skel[0], 1, svn_txn->pool);
    if (svn_err)
      goto error;

    /* Store the ID in the transaction object.  */
    svn_txn->id = apr_pstrdup (svn_txn->pool, id_text);
  }

  SVN_ERR (DB_WRAP (svn_txn->fs, "creating transaction (closing cursor)",
		    cursor->c_close (cursor)));
  return 0;

 error:
  if (cursor)
    cursor->c_close (cursor);
  return svn_err;
}


svn_error_t *
svn_fs_begin_txn (svn_fs_txn_t **txn_p,
		  svn_fs_t *fs)
{
  apr_pool_t *pool;
  svn_fs_txn_t *txn;

  SVN_ERR (svn_fs__check_fs (fs));

  pool = svn_pool_create (fs->pool);
  txn = NEW (pool, svn_fs_txn_t);
  memset (txn, 0, sizeof (*txn));
  txn->fs = fs;
  txn->pool = pool;

  /* Choose an id for this transaction, and create the transaction
     record in the database.  */
  SVN_ERR (svn_fs__retry_txn (fs, begin_txn_body, txn));

  /* Add the transaction to the filesystem's table of open transactions.  */
  apr_hash_set (fs->open_txns, txn->id, 0, txn);

  *txn_p = txn;
  return 0;
}



/* Creating a new root directory for a transaction.  */


struct replace_root_args {
  svn_fs_dir_t **root_p;
  svn_fs_txn_t *svn_txn;
  svn_revnum_t revision;
};


static svn_error_t *
replace_root_body (void *baton,
		   DB_TXN *db_txn)
{
  /* Unpack the true arguments, passed through svn_fs__retry_txn.  */
  struct replace_root_args *args = baton;
  svn_fs_dir_t **root_p = args->root_p;
  svn_fs_txn_t *svn_txn = args->svn_txn;
  svn_revnum_t revision = args->revision;

  svn_fs_id_t *revision_root_id;
  svn_fs_node_t *txn_root;

  /* The TRANSACTION skel for SVN_TXN, and the sub-skel that holds its
     root directory ID.  These get read in *only* if we don't
     have the root directory cached in the SVN_TXN object already.  */
  skel_t *txn_skel, *root_skel;

  /* Find the root of REVISION in the transaction's filesystem.  */
  SVN_ERR (svn_fs__revision_root (&revision_root_id, svn_txn->fs, revision,
                                  svn_txn->pool));

  /* Have we cached the transaction's root directory ID?  */
  if (! svn_txn->root)
    {
      /* Read in SVN_TXN's TRANSACTION skel, and try to find the root
	 directory ID there.  */
      SVN_ERR (get_transaction_skel (&txn_skel, svn_txn, db_txn, 
				     svn_txn->pool));
      if (svn_fs__list_length (txn_skel) != 2
	  || ! txn_skel->children->is_atom
	  || ! txn_skel->children->next->is_atom)
	return corrupt_txn (svn_txn);
      root_skel = txn_skel->children->next;

      /* If there is a node ID, try to parse it.  */
      if (root_skel->len > 0)
	{
	  svn_txn->root = svn_fs_parse_id (root_skel->data, root_skel->len,
					   svn_txn->pool);
	  if (! svn_txn->root)
	    return corrupt_txn (svn_txn);
	}
      else
	svn_txn->root = 0;
    }
      
  /* At this point, the cache svn_txn->root is up-to-date: it is zero
     iff the transaction has no root directory yet.  */
  if (svn_txn->root)
    {
      /* Yes, we have a root directory.  Make sure it's a direct
         ancestor of the root revision.  */
      if (! svn_fs__is_parent (revision_root_id, svn_txn->root))
	return bad_txn_root (svn_txn, revision);

      /* The root directory ID looks reasonable, so open the actual node.  */
      SVN_ERR (svn_fs__open_node_by_id (&txn_root, svn_txn->fs,
					svn_txn->root, db_txn));
    }
  else
    {
      /* No, this transaction has no root directory yet.  */
      svn_error_t *svn_err;
      svn_fs_node_t *revision_root;

      /* Open REVISION's root directory, create an immediate successor
         to it, and establish that as SVN_TXN's root.  */
      SVN_ERR (svn_fs__open_node_by_id (&revision_root,
					svn_txn->fs, revision_root_id,
					db_txn));
      svn_err = svn_fs__create_successor (&txn_root, revision_root,
					  svn_txn, db_txn);
      svn_fs_close_node (revision_root);
      if (svn_err)
	return svn_err;

      /* Record this transaction's new root directory ID.  We know that 
	 txn_skel has been read in, and root_skel set, because svn_txn had
	 no root directory when we began.  */
      {
	svn_string_t *unparsed_txn_root_id
	  = svn_fs_unparse_id (svn_fs__node_id (txn_root), svn_txn->pool);
	root_skel->data = unparsed_txn_root_id->data;
	root_skel->len = unparsed_txn_root_id->len;

	svn_err = put_transaction_skel (svn_txn->fs, db_txn,
					svn_txn->id, txn_skel, 0,
					svn_txn->pool);
	if (svn_err)
	  {
	    svn_fs_close_node (txn_root);
	    return svn_err;
	  }
      }
    }

  /* Make sure it's a mutable directory, as it must be.  */
  if (! svn_fs_node_is_dir (txn_root)
      || ! svn_fs_node_is_mutable (txn_root))
    {
      svn_fs_close_node (txn_root);
      return
	svn_error_createf
	(SVN_ERR_FS_CORRUPT, 0, 0, svn_txn->fs->pool,
	 "the root of transaction `%s' in filesystem `%s' is not a"
	 " mutable directory",
	 svn_txn->id, svn_txn->fs->env_path);
    }

  *root_p = svn_fs_node_to_dir (txn_root);
  return 0;
}


svn_error_t *
svn_fs_replace_root (svn_fs_dir_t **root_p,
		     svn_fs_txn_t *txn,
		     svn_revnum_t revision)
{
  struct replace_root_args args;

  args.root_p = root_p;
  args.svn_txn = txn;
  args.revision = revision;

  return svn_fs__retry_txn (txn->fs, replace_root_body, &args);
}



/* Miscellaneous trivial transaction functions.  */

char *
svn_fs__txn_id (svn_fs_txn_t *txn)
{
  return txn->id;
}


svn_error_t *
svn_fs_txn_name (svn_string_t **name_p,
		 svn_fs_txn_t *txn,
		 apr_pool_t *pool)
{
  *name_p = svn_string_ncreate (txn->id, strlen (txn->id), pool);
  return 0;
}
