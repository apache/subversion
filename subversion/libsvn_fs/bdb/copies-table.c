/* copies-table.c : operations on the `copies' table
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

#include <string.h>

#include "svn_pools.h"
#include "bdb_compat.h"
#include "../fs.h"
#include "../err.h"
#include "../key-gen.h"
#include "dbt.h"
#include "../util/skel.h"
#include "../util/fs_skels.h"
#include "../trail.h"
#include "../id.h"
#include "bdb-err.h"
#include "copies-table.h"
#include "rev-table.h"


int
svn_fs__bdb_open_copies_table (DB **copies_p,
                               DB_ENV *env,
                               int create)
{
  const u_int32_t open_flags = (create ? (DB_CREATE | DB_EXCL) : 0);
  DB *copies;

  BDB_ERR (svn_fs__bdb_check_version());
  BDB_ERR (db_create (&copies, env, 0));
  BDB_ERR (copies->open (SVN_BDB_OPEN_PARAMS(copies, NULL),
                        "copies", 0, DB_BTREE,
                        open_flags | SVN_BDB_AUTO_COMMIT,
                        0666));

  /* Create the initial `next-id' table entry.  */
  if (create)
  {
    DBT key, value;
    BDB_ERR (copies->put (copies, 0,
                         svn_fs__str_to_dbt (&key, 
                                             (char *) svn_fs__next_key_key),
                         svn_fs__str_to_dbt (&value, (char *) "0"),
                         SVN_BDB_AUTO_COMMIT));
  }

  *copies_p = copies;
  return 0;
}


/* Store COPY as a copy named COPY_ID in FS as part of TRAIL.  */
/* ### only has one caller; might not need to be abstracted */
static svn_error_t *
put_copy (svn_fs_t *fs,
          const svn_fs__copy_t *copy,
          const char *copy_id,
          trail_t *trail)
{
  skel_t *copy_skel;
  DBT key, value;

  /* Convert native type to skel. */
  SVN_ERR (svn_fs__unparse_copy_skel (&copy_skel, copy, trail->pool));

  /* Only in the context of this function do we know that the DB call
     will not attempt to modify COPY_ID, so the cast belongs here.  */
  svn_fs__str_to_dbt (&key, (char *) copy_id);
  svn_fs__skel_to_dbt (&value, copy_skel, trail->pool);
  SVN_ERR (BDB_WRAP (fs, "storing copy record",
                    fs->copies->put (fs->copies, trail->db_txn,
                                     &key, &value, 0)));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs__bdb_reserve_copy_id (const char **id_p,
                             svn_fs_t *fs,
                             trail_t *trail)
{
  DBT query, result;
  apr_size_t len;
  char next_key[SVN_FS__MAX_KEY_SIZE];
  int db_err;

  svn_fs__str_to_dbt (&query, (char *) svn_fs__next_key_key);

  /* Get the current value associated with the `next-id' key in the
     copies table.  */
  SVN_ERR (BDB_WRAP (fs, "allocating new copy ID (getting `next-key')",
                    fs->copies->get (fs->copies, trail->db_txn,
                                     &query, svn_fs__result_dbt (&result), 
                                     0)));
  svn_fs__track_dbt (&result, trail->pool);

  /* Set our return value. */
  *id_p = apr_pstrmemdup (trail->pool, result.data, result.size);

  /* Bump to future key. */
  len = result.size;
  svn_fs__next_key (result.data, &len, next_key);
  db_err = fs->copies->put (fs->copies, trail->db_txn,
                            svn_fs__str_to_dbt (&query, 
                                                (char *) svn_fs__next_key_key),
                            svn_fs__str_to_dbt (&result, (char *) next_key), 
                            0);

  SVN_ERR (BDB_WRAP (fs, "bumping next copy key", db_err));
  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs__bdb_create_copy (const char *copy_id,
                         svn_fs_t *fs,
                         const char *src_path,
                         const char *src_txn_id,
                         const svn_fs_id_t *dst_noderev_id,
                         trail_t *trail)
{
  svn_fs__copy_t copy;
  copy.src_path = src_path;
  copy.src_txn_id = src_txn_id;
  copy.dst_noderev_id = (svn_fs_id_t *) dst_noderev_id;
  return put_copy (fs, &copy, copy_id, trail);
}


svn_error_t *
svn_fs__bdb_delete_copy (svn_fs_t *fs,
                         const char *copy_id,
                         trail_t *trail)
{
  DBT key;

  svn_fs__str_to_dbt (&key, (char *) copy_id);
  return BDB_WRAP (fs, "deleting entry from `copies' table",
                  fs->copies->del (fs->copies, trail->db_txn, &key, 0));
}


svn_error_t *
svn_fs__bdb_get_copy (svn_fs__copy_t **copy_p,
                      svn_fs_t *fs,
                      const char *copy_id,
                      trail_t *trail)
{
  DBT key, value;
  int db_err;
  skel_t *skel;
  svn_fs__copy_t *copy;

  /* Only in the context of this function do we know that the DB call
     will not attempt to modify copy_id, so the cast belongs here.  */
  db_err = fs->copies->get (fs->copies, trail->db_txn,
                            svn_fs__str_to_dbt (&key, (char *) copy_id),
                            svn_fs__result_dbt (&value),
                            0);
  svn_fs__track_dbt (&value, trail->pool);

  if (db_err == DB_NOTFOUND)
    return svn_fs__err_no_such_copy (fs, copy_id);
  SVN_ERR (BDB_WRAP (fs, "reading copy", db_err));

  /* Unparse COPY skel */
  skel = svn_fs__parse_skel (value.data, value.size, trail->pool);
  if (! skel)
    return svn_fs__err_corrupt_copy (fs, copy_id);

  /* Convert skel to native type. */
  SVN_ERR (svn_fs__parse_copy_skel (&copy, skel, trail->pool));
  *copy_p = copy;
  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs__bdb_walk_copies_reverse (svn_fs__bdb_copy_cb_func_t callback,
                                 void *baton,
                                 svn_fs_t *fs,
                                 const char *start_id,
                                 const char *end_id,
                                 trail_t *trail)
{
  apr_size_t const next_id_key_len = strlen (svn_fs__next_key_key);
  apr_pool_t *subpool = svn_pool_create (trail->pool);
  svn_fs__copy_t *copy;
  const char *cur_id = end_id;
  DBC *cursor;
  DBT key, value;
  int db_err, db_c_err;
  int track_key = 0;

  /* Create a database cursor to list the copy names. */
  SVN_ERR (BDB_WRAP (fs, "reading copy list (opening cursor)",
                     fs->copies->cursor (fs->copies, trail->db_txn, 
                                         &cursor, 0)));

  /* Position our cursor at the END_ID. */
  svn_fs__str_to_dbt (&key, (char *)end_id);

  /* Read backwards through the copies table, stopping on errors or
     when we reach our START_ID. */
  for (db_err = cursor->c_get (cursor, 
                               &key, 
                               svn_fs__result_dbt (&value), 
                               DB_SET);
       (db_err == 0) && (strcmp (cur_id, start_id) != 0);
       db_err = cursor->c_get (cursor,
                               svn_fs__result_dbt (&key),
                               svn_fs__result_dbt (&value),
                               DB_PREV))
    {
      skel_t *copy_skel;
      const char *copy_id;
      svn_error_t *err;

      /* Clear the per-iteration subpool */
      svn_pool_clear (subpool);

      /* Track the memory alloc'd for fetching the key and value here
         so that when the containing pool is cleared, this memory is
         freed.  The first time through this loop is an exception
         though -- in that loop, we only want to track the value
         because we created our own key.  */
      if (track_key)
        svn_fs__track_dbt (&key, subpool);
      svn_fs__track_dbt (&value, subpool);
      track_key = 1;

      /* Ignore the "next-id" key. */
      if (key.size == next_id_key_len
          && 0 == memcmp (key.data, svn_fs__next_key_key, next_id_key_len))
        continue;

      /* Get the COPY_ID, and parse COPY skel. */
      copy_id = apr_pstrmemdup (subpool, key.data, key.size);
      copy_skel = svn_fs__parse_skel (value.data, value.size, subpool);
      if (! copy_skel)
        {
          cursor->c_close (cursor);
          return svn_fs__err_corrupt_copy (fs, copy_id);
        }

      /* Convert skel to native type. */
      if ((err = svn_fs__parse_copy_skel (&copy, copy_skel, subpool)))
        {
          cursor->c_close (cursor);
          return err;
        }

      /* Call our callback function with this COPY. */
      if ((err = callback (baton, copy_id, copy, subpool)))
        {
          cursor->c_close (cursor);
          return err;
        }
    }

  /* Check for errors, but close the cursor first. */
  db_c_err = cursor->c_close (cursor);
  if (db_err != DB_NOTFOUND)
    {
      SVN_ERR (BDB_WRAP (fs, "reading copy list (listing keys)",
                         db_err));
    }
  SVN_ERR (BDB_WRAP (fs, "reading copy list (closing cursor)",
                     db_c_err));

  /* Destroy the per-iteration subpool */
  svn_pool_destroy (subpool);

  return SVN_NO_ERROR;
}
