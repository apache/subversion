/* changes-table.c : operations on the `changes' table
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

#include "bdb_compat.h"

#include <apr_hash.h>
#include <apr_tables.h>

#include "svn_fs.h"
#include "svn_pools.h"
#include "../fs.h"
#include "../id.h"
#include "../err.h"
#include "../trail.h"
#include "../util/fs_skels.h"
#include "dbt.h"
#include "changes-table.h"



/*** Creating and opening the changes table. ***/

int
svn_fs__open_changes_table (DB **changes_p,
                            DB_ENV *env,
                            int create)
{
  const u_int32_t open_flags = (create ? (DB_CREATE | DB_EXCL) : 0);
  DB *changes;

  DB_ERR (svn_bdb__check_version());
  DB_ERR (db_create (&changes, env, 0));

  /* Enable duplicate keys. This allows us to store the changes
     one-per-row.  Note: this must occur before ->open().  */
  DB_ERR (changes->set_flags (changes, DB_DUP));

  DB_ERR (changes->open (SVN_BDB_OPEN_PARAMS(changes, NULL),
                         "changes", 0, DB_BTREE,
                         open_flags | SVN_BDB_AUTO_COMMIT,
                         0666));

  *changes_p = changes;
  return 0;
}



/*** Storing and retrieving changes.  ***/

svn_error_t *
svn_fs__changes_add (svn_fs_t *fs,
                     const char *key,
                     svn_fs__change_t *change,
                     trail_t *trail)
{
  DBT query, value;
  skel_t *skel;

  /* Convert native type to skel. */
  SVN_ERR (svn_fs__unparse_change_skel (&skel, change, trail->pool));

  /* Store a new record into the database. */
  svn_fs__str_to_dbt (&query, (char *) key);
  svn_fs__skel_to_dbt (&value, skel, trail->pool);
  SVN_ERR (DB_WRAP (fs, "creating change", 
                    fs->changes->put (fs->changes, trail->db_txn,
                                      &query, &value, 0)));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs__changes_delete (svn_fs_t *fs,
                        const char *key,
                        trail_t *trail)
{
  int db_err;
  DBT query;
  
  db_err = fs->changes->del (fs->changes, trail->db_txn,
                             svn_fs__str_to_dbt (&query, (char *) key), 0);

  /* If there're no changes for KEY, that is acceptable.  Any other
     error should be propogated to the caller, though.  */
  if ((db_err) && (db_err != DB_NOTFOUND))
    {
      SVN_ERR (DB_WRAP (fs, "deleting changes", db_err));
    }

  return SVN_NO_ERROR;
}


/* Make a public change structure from an internal one, allocating the
   structure, and copies of necessary members, POOL. */
static svn_fs_path_change_t *
make_change (svn_fs__change_t *change, 
             apr_pool_t *pool)
{
  svn_fs_path_change_t *new_one = apr_palloc (pool, sizeof (*new_one));
  new_one->node_rev_id = svn_fs__id_copy (change->noderev_id, pool);
  new_one->change_kind = change->kind;
  new_one->text_mod = change->text_mod;
  new_one->prop_mod = change->prop_mod;
  return new_one;
}


/* Merge the internal-use-only CHANGE into a hash of public-FS
   svn_fs_path_change_t CHANGES, collapsing multiple changes into a
   single summarical (is that real word?) change per path. */
static svn_error_t *
fold_change (apr_hash_t *changes,
             svn_fs__change_t *change)
{
  apr_pool_t *pool = apr_hash_pool_get (changes);
  svn_fs_path_change_t *old_change, *new_change;
  const char *path;

  if ((old_change = apr_hash_get (changes, change->path, APR_HASH_KEY_STRING)))
    {
      /* This path already exists in the hash, so we have to merge
         this change into the already existing one. */
      
      /* Since the path already exists in the hash, we don't have to
         dup the allocation for the path itself. */
      path = change->path;

      /* Sanity check:  only allow NULL node revision ID in the
         `reset' case. */
      if ((! change->noderev_id) && (change->kind != svn_fs_path_change_reset))
        return svn_error_create 
          (SVN_ERR_FS_CORRUPT, 0, NULL,
           "Invalid change: missing required node revision ID");
        
      /* Sanity check:  we should be talking about the same node
         revision ID as our last change except where the last change
         was a deletion. */
      if (change->noderev_id
          && (! svn_fs__id_eq (old_change->node_rev_id, change->noderev_id))
          && (old_change->change_kind != svn_fs_path_change_delete))
        return svn_error_create 
          (SVN_ERR_FS_CORRUPT, 0, NULL,
           "Invalid change ordering: new node revision ID without delete");

      /* Sanity check: an add, replacement, or reset must be the first
         thing to follow a deletion. */
      if ((old_change->change_kind == svn_fs_path_change_delete)
          && (! ((change->kind == svn_fs_path_change_replace) 
                 || (change->kind == svn_fs_path_change_reset)
                 || (change->kind == svn_fs_path_change_add))))
        return svn_error_create 
          (SVN_ERR_FS_CORRUPT, 0, NULL,
           "Invalid change ordering: non-add change on deleted path");

      /* Now, merge that change in. */
      switch (change->kind)
        {
        case svn_fs_path_change_reset:
          /* A reset here will simply remove the path change from the
             hash. */
          old_change = NULL;
          break;

        case svn_fs_path_change_delete:
          if ((old_change->change_kind == svn_fs_path_change_replace)
              || (old_change->change_kind == svn_fs_path_change_add))
            {
              /* If the path was introduced in this transaction via an
                 add or replace, and we are deleting it, just remove
                 the path altogether.  */
              old_change = NULL;
            }
          else
            {
              /* A deletion overrules all previous changes. */
              old_change->change_kind = svn_fs_path_change_delete;
              old_change->text_mod = change->text_mod;
              old_change->prop_mod = change->prop_mod;
            }
          break;

        case svn_fs_path_change_add:
        case svn_fs_path_change_replace:
          /* An add at this point must be following a previous delete,
             so treat it just like a replace. */
          old_change->change_kind = svn_fs_path_change_replace;
          old_change->node_rev_id = svn_fs__id_copy (change->noderev_id, pool);
          old_change->text_mod = change->text_mod;
          old_change->prop_mod = change->prop_mod;
          break;

        case svn_fs_path_change_modify:
        default:
          if (change->text_mod)
            old_change->text_mod = 1;
          if (change->prop_mod)
            old_change->prop_mod = 1;
          break;
        }

      /* Point our new_change to our (possibly modified) old_change. */
      new_change = old_change;
    }
  else
    {
      /* This change is new to the hash, so make a new public change
         structure from the internal one (in the hash's pool), and dup
         the path into the hash's pool, too. */
      new_change = make_change (change, pool);
      path = apr_pstrdup (pool, change->path);
    }

  /* Add (or update) this path, removing any leading slash that might exist. */
  apr_hash_set (changes, path, APR_HASH_KEY_STRING, new_change);

  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs__changes_fetch (apr_hash_t **changes_p,
                       svn_fs_t *fs,
                       const char *key,
                       trail_t *trail)
{
  DBC *cursor;
  DBT query, result;
  int db_err = 0, db_c_err = 0;
  svn_error_t *err = SVN_NO_ERROR;
  apr_hash_t *changes = apr_hash_make (trail->pool);
  apr_pool_t *subpool = svn_pool_create (trail->pool);

  /* Get a cursor on the first record matching KEY, and then loop over
     the records, adding them to the return array. */
  SVN_ERR (DB_WRAP (fs, "creating cursor for reading changes",
                    fs->changes->cursor (fs->changes, trail->db_txn,
                                         &cursor, 0)));

  /* Advance the cursor to the key that we're looking for. */
  svn_fs__str_to_dbt (&query, (char *) key);
  svn_fs__result_dbt (&result);
  db_err = cursor->c_get (cursor, &query, &result, DB_SET);
  if (! db_err)
    svn_fs__track_dbt (&result, trail->pool);

  while (! db_err)
    {
      svn_fs__change_t *change;
      skel_t *result_skel;
      
      /* RESULT now contains a change record associated with KEY.  We
         need to parse that skel into an svn_fs__change_t structure ...  */
      result_skel = svn_fs__parse_skel (result.data, result.size, subpool);
      if (! result_skel)
        {
          err = svn_error_createf (SVN_ERR_FS_CORRUPT, 0, NULL,
                                   "error reading changes for key `%s'", key);
          goto cleanup;
        }
      err = svn_fs__parse_change_skel (&change, result_skel, subpool);
      if (err)
        goto cleanup;
      
      /* ... and merge it with our return hash.  */
      SVN_ERR (fold_change (changes, change));

      /* Advance the cursor to the next record with this same KEY, and
         fetch that record. */
      svn_fs__result_dbt (&result);
      db_err = cursor->c_get (cursor, &query, &result, DB_NEXT_DUP);
      if (! db_err)
        svn_fs__track_dbt (&result, trail->pool);
      
      /* Clear the per-iteration subpool. */
      svn_pool_clear (subpool);
    }

  /* Destroy the per-iteration subpool. */
  svn_pool_destroy (subpool);

  /* If there are no (more) change records for this KEY, we're
     finished.  Just return the (possibly empty) array.  Any other
     error, however, needs to get handled appropriately.  */
  if (db_err && (db_err != DB_NOTFOUND))
    err = DB_WRAP (fs, "fetching changes", db_err);

 cleanup:
  /* Close the cursor. */
  db_c_err = cursor->c_close (cursor);

  /* If we had an error prior to closing the cursor, return the error. */
  if (err)
    return err;
  
  /* If our only error thus far was when we closed the cursor, return
     that error. */
  if (db_c_err)
    SVN_ERR (DB_WRAP (fs, "closing changes cursor", db_c_err));

  /* Finally, set our return variable and get outta here. */
  *changes_p = changes;
  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs__changes_fetch_raw (apr_array_header_t **changes_p,
                           svn_fs_t *fs,
                           const char *key,
                           trail_t *trail)
{
  DBC *cursor;
  DBT query, result;
  int db_err = 0, db_c_err = 0;
  svn_error_t *err = SVN_NO_ERROR;
  svn_fs__change_t *change;
  apr_array_header_t *changes = apr_array_make (trail->pool, 4, 
                                                sizeof (change));

  /* Get a cursor on the first record matching KEY, and then loop over
     the records, adding them to the return array. */
  SVN_ERR (DB_WRAP (fs, "creating cursor for reading changes",
                    fs->changes->cursor (fs->changes, trail->db_txn,
                                         &cursor, 0)));

  /* Advance the cursor to the key that we're looking for. */
  svn_fs__str_to_dbt (&query, (char *) key);
  svn_fs__result_dbt (&result);
  db_err = cursor->c_get (cursor, &query, &result, DB_SET);
  if (! db_err)
    svn_fs__track_dbt (&result, trail->pool);

  while (! db_err)
    {
      skel_t *result_skel;
      
      /* RESULT now contains a change record associated with KEY.  We
         need to parse that skel into an svn_fs__change_t structure ...  */
      result_skel = svn_fs__parse_skel (result.data, result.size, trail->pool);
      if (! result_skel)
        {
          err = svn_error_createf (SVN_ERR_FS_CORRUPT, 0, NULL,
                                   "error reading changes for key `%s'", key);
          goto cleanup;
        }
      err = svn_fs__parse_change_skel (&change, result_skel, trail->pool);
      if (err)
        goto cleanup;
      
      /* ... and add it to our return array.  */
      (*((svn_fs__change_t **) apr_array_push (changes))) = change;

      /* Advance the cursor to the next record with this same KEY, and
         fetch that record. */
      svn_fs__result_dbt (&result);
      db_err = cursor->c_get (cursor, &query, &result, DB_NEXT_DUP);
      if (! db_err)
        svn_fs__track_dbt (&result, trail->pool);
    }

  /* If there are no (more) change records for this KEY, we're
     finished.  Just return the (possibly empty) array.  Any other
     error, however, needs to get handled appropriately.  */
  if (db_err && (db_err != DB_NOTFOUND))
    err = DB_WRAP (fs, "fetching changes", db_err);

 cleanup:
  /* Close the cursor. */
  db_c_err = cursor->c_close (cursor);

  /* If we had an error prior to closing the cursor, return the error. */
  if (err)
    return err;
  
  /* If our only error thus far was when we closed the cursor, return
     that error. */
  if (db_c_err)
    SVN_ERR (DB_WRAP (fs, "closing changes cursor", db_c_err));

  /* Finally, set our return variable and get outta here. */
  *changes_p = changes;
  return SVN_NO_ERROR;
}
