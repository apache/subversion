/* txn.c : implementation of transaction functions
 *
 * ================================================================
 * Copyright (c) 2000 Collab.Net.  All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 * 
 * 1. Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 * 
 * 2. Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 * 
 * 3. The end-user documentation included with the redistribution, if
 * any, must include the following acknowlegement: "This product includes
 * software developed by Collab.Net (http://www.Collab.Net/)."
 * Alternately, this acknowlegement may appear in the software itself, if
 * and wherever such third-party acknowlegements normally appear.
 * 
 * 4. The hosted project names must not be used to endorse or promote
 * products derived from this software without prior written
 * permission. For written permission, please contact info@collab.net.
 * 
 * 5. Products derived from this software may not use the "Tigris" name
 * nor may "Tigris" appear in their names without prior written
 * permission of Collab.Net.
 * 
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESSED OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL COLLABNET OR ITS CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * ====================================================================
 * 
 * This software consists of voluntary contributions made by many
 * individuals on behalf of Collab.Net.
 */

#include <string.h>

#include "apr_strings.h"

#include "svn_fs.h"
#include "fs.h"
#include "txn.h"
#include "skel.h"
#include "convert-size.h"
#include "dbt.h"
#include "err.h"


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
  char *a_end, *b_end;

  /* Parse the two keys.  */
  a = svn_fs__getsize (ak->data, ak->size, &a_end, 10000000);
  b = svn_fs__getsize (bk->data, bk->size, &b_end, 10000000);

  /* Are they well-formed?  */
  if (a_end != (char *) ak->data + ak->size)
    a_end = 0;
  if (b_end != (char *) bk->data + bk->size)
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



/* Writing TRANSACTION skels to the database.  */

/* Store the skel TXN_SKEL in the `transactions' table under the
   transaction id ID.  If CREATE is non-zero, return an error if an
   entry for ID already exists in `transactions'.

   Do this as part of the Berkeley DB transaction DB_TXN; if DB_TXN is
   zero, make the change outside of any Berkeley DB transaction.

   Do any necessary temporary allocation in POOL.  */
static svn_error_t *
put_transaction_skel (svn_fs_t *fs, DB_TXN *db_txn,
		      char *id,
		      skel_t *txn_skel,
		      int create,
		      apr_pool_t *pool)
{
  DB *transactions = fs->transactions;
  svn_string_t *unparsed_txn;
  DBT key, value;

  unparsed_txn = svn_fs__unparse_skel (txn_skel, pool);
  svn_fs__set_dbt (&key, id, strlen (id));
  svn_fs__set_dbt (&value, unparsed_txn->data, unparsed_txn->len);
  SVN_ERR (DB_WRAP (fs, "storing transaction skel",
		    transactions->put (transactions, db_txn, &key, &value, 
				       create ? DB_NOOVERWRITE : 0)));

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
create_txn_body (svn_fs_txn_t *svn_txn,
		 DB_TXN *db_txn,
		 DBC **cursor_p)
{
  DB *transactions = svn_txn->fs->transactions;
  DBC *cursor;
  DBT key, value;
  int id;

  /* Jim, svn_fs__nodata_dbt keeps getting an `implicit declaration'
     warning.  It's helpful right now have a tree that compiles
     without warning unless something's wrong, so I've put this bogus
     declaration here to sidestep the warning.  -karl */
  void svn_fs__nodata_dbt (DBT *value);

  /* Create a cursor.  */
  SVN_ERR (DB_WRAP (svn_txn->fs, "creating transaction (allocating cursor)",
		    transactions->cursor (transactions, db_txn, cursor_p, 0)));
  cursor = *cursor_p;

  /* Use that cursor to get the ID of the last entry in the table.
     We only need to know the key; don't actually read any of the value.  */
  svn_fs__result_dbt (&key);
  svn_fs__nodata_dbt (&value);
  SVN_ERR (DB_WRAP (svn_txn->fs, "creating transaction (getting max id)",
		    cursor->c_get (cursor, &key, &value, DB_LAST)));

  /* Try to parse the key as a number.  */
  {
    char *end;
    id = svn_fs__getsize (key.data, key.size, &end, 10000000);

    /* If we didn't consume the entire key as the number, then it's a
       bogus key.  */
    if (end != (char *) key.data + key.size)
      return (svn_error_createf
	      (SVN_ERR_FS_CORRUPT, 0, 0, svn_txn->fs->pool,
	       "malformed ID in transaction table of filesystem `%s'",
	       svn_txn->fs->env_path));
  }

  /* Choose a new, distinct ID.  */
  id++;

  /* Write an initial record for the new transaction to the database.  */
  {
    /* An empty transaction skel, `(transaction 0 )', written out
       as a series of initialized `skel_t' objects.  */
    static skel_t new_txn_skel[] = {
      { 0, "", 0, &new_txn_skel[1], 0 },
      { 1, "transaction", 11, 0, &new_txn_skel[2] },
      { 1, "", 0, 0, 0 }
    };
    char id_text[200];
    int id_len = svn_fs__putsize (id_text, sizeof (id_text), id);

    /* Store the transaction skel in the database, under this ID.  */
    id_text[id_len] = 0;
    SVN_ERR (put_transaction_skel (svn_txn->fs, db_txn, id_text,
				   &new_txn_skel[0], 1, svn_txn->pool));

    /* Store the ID in the transaction object.  */
    svn_txn->id = apr_pstrdup (svn_txn->pool, id_text);
  }

  return 0;
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
  for (;;)
    {
      DB_TXN *db_txn;
      DBC *cursor = 0;
      svn_error_t *svn_err;
      
      SVN_ERR (DB_WRAP (fs, "creating transaction (beginning DB transaction)",
			txn_begin (fs->env, 0, &db_txn, 0)));
    
      svn_err = create_txn_body (txn, db_txn, &cursor);
      if (! svn_err)
	{
	  /* The transaction succeeded!  Commit it.  */
	  if (cursor)
	    SVN_ERR (DB_WRAP (fs, "creating transaction (closing cursor)",
			      cursor->c_close (cursor)));
	  SVN_ERR (DB_WRAP (fs,
			    "creating transaction (committing DB transaction)",
			    txn_commit (db_txn, 0)));
	  break;
	}

      /* Is this a real error, or do we just need to retry?  */
      if (svn_err->apr_err != SVN_ERR_BERKELEY_DB
	  || svn_err->src_err != DB_LOCK_DEADLOCK)
	{
	  /* Free the cursor and abort the transaction, but ignore any
	     error returns.  The first error is more valuable.  */
	  if (cursor)
	    cursor->c_close (cursor);
	  txn_abort (db_txn);
	  return svn_err;
	}

      /* We deadlocked.  Abort the transaction, and try again.  */
      if (cursor)
	cursor->c_close (cursor);
      SVN_ERR (DB_WRAP (fs, "creating transaction (aborting DB transaction)",
			txn_abort (db_txn)));
    }

  /* Add the transaction to the filesystem's table of open transactions.  */
  apr_hash_set (fs->open_txns, txn->id, 0, txn);

  *txn_p = txn;
  return 0;
}
