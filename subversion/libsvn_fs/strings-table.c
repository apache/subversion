/* strings-table.c : operations on the `strings' table
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

#include "db.h"
#include "svn_fs.h"
#include "fs.h"
#include "err.h"
#include "dbt.h"
#include "trail.h"
#include "strings-table.h"
#include "key-gen.h"
#include "svn_pools.h"


/*** Creating and opening the strings table. ***/

int
svn_fs__open_strings_table (DB **strings_p,
                            DB_ENV *env,
                            int create)
{
  DB *strings;

  DB_ERR (db_create (&strings, env, 0));

  /* Enable duplicate keys. This allows the data to be spread out across
     multiple records. Note: this must occur before ->open().  */
  DB_ERR (strings->set_flags (strings, DB_DUP));

  DB_ERR (strings->open (strings, "strings", 0, DB_BTREE,
                       create ? (DB_CREATE | DB_EXCL) : 0,
                       0666));

  if (create)
  {
    DBT key, value;

    /* Create the `next-key' table entry.  */
    DB_ERR (strings->put
            (strings, 0,
             svn_fs__str_to_dbt (&key, (char *) svn_fs__next_key_key),
             svn_fs__str_to_dbt (&value, (char *) "0"),
             0));
  }
  
  *strings_p = strings;
  return 0;
}



/*** Storing and retrieving strings.  ***/

static svn_error_t *
locate_key (apr_size_t *length,
            DBC **cursor,
            DBT *query,
            svn_fs_t *fs,
            trail_t *trail)
{
  int db_err;
  DBT result;

  SVN_ERR (DB_WRAP (fs, "creating cursor for reading a string",
                    fs->strings->cursor (fs->strings, trail->db_txn,
                                         cursor, 0)));

  /* Set up the DBT for reading the length of the record. */
  svn_fs__clear_dbt (&result);
  result.ulen = 0;
  result.flags |= DB_DBT_USERMEM;

  /* Advance the cursor to the key that we're looking for. */
  db_err = (*cursor)->c_get (*cursor, query, &result, DB_SET);

  /* We don't need to svn_fs__track_dbt() the result, because nothing
     was allocated in it. */

  /* If there's no such node, return an appropriately specific error.  */
  if (db_err == DB_NOTFOUND)
    {
      (*cursor)->c_close (*cursor);
      return svn_error_createf
        (SVN_ERR_FS_NO_SUCH_STRING, 0, 0, fs->pool,
         "locate_key: no such string `%s'", (const char *)query->data);
    }
  if (db_err)
    {
      DBT rerun;

      if (db_err != ENOMEM)
        {
          (*cursor)->c_close (*cursor);
          return DB_WRAP (fs, "could not move cursor", db_err);
        }

      /* We got an ENOMEM (typical since we have a zero length buf), so
         we need to re-run the operation to make it happen. */
      svn_fs__clear_dbt (&rerun);
      rerun.flags |= DB_DBT_USERMEM | DB_DBT_PARTIAL;
      SVN_ERR (DB_WRAP (fs, "rerunning cursor move",
                        (*cursor)->c_get (*cursor, query, &rerun, DB_SET)));
    }

  /* ### this cast might not be safe? */
  *length = (apr_size_t) result.size;

  return SVN_NO_ERROR;
}

static int
get_next_length (apr_size_t *length, DBC *cursor, DBT *query)
{
  DBT result;
  int db_err;

  /* Set up the DBT for reading the length of the record. */
  svn_fs__clear_dbt (&result);
  result.ulen = 0;
  result.flags |= DB_DBT_USERMEM;

  /* Note: this may change the QUERY DBT, but that's okay: we're going
     to be sticking with the same key anyways.  */
  db_err = cursor->c_get (cursor, query, &result, DB_NEXT_DUP);

  /* Note that we exit on DB_NOTFOUND. The caller uses that to end a loop. */
  if (db_err)
    {
      DBT rerun;

      if (db_err != ENOMEM)
        {
          cursor->c_close (cursor);
          return db_err;
        }

      /* We got an ENOMEM (typical since we have a zero length buf), so
         we need to re-run the operation to make it happen. */
      svn_fs__clear_dbt (&rerun);
      rerun.flags |= DB_DBT_USERMEM | DB_DBT_PARTIAL;
      db_err = cursor->c_get (cursor, query, &rerun, DB_NEXT_DUP);
    }

  /* ### this cast might not be safe? */
  *length = (apr_size_t) result.size;
  return db_err;
}


/* Read *LEN bytes into BUF from OFFSET in string KEY in FS, as part
 * of TRAIL.
 * 
 * On return, *LEN is set to the number of bytes read. This value may
 * be less than the number requested.
 *
 * If OFFSET is past the end of the string, then *LEN will be set to
 * zero. Callers which are advancing OFFSET as they read portions of
 * the string can terminate their loop when *LEN is returned as zero
 * (which will occur when OFFSET == length(the string)).
 * 
 * If string KEY does not exist, the error SVN_ERR_FS_NO_SUCH_STRING
 * is returned.
 */
static svn_error_t *
string_read (svn_fs_t *fs,
             const char *key,
             char *buf,
             apr_off_t offset,
             apr_size_t *len,
             trail_t *trail)
{
  int db_err;
  DBT query, result;
  DBC *cursor;
  apr_size_t length;

  svn_fs__str_to_dbt (&query, (char *) key);

  SVN_ERR (locate_key (&length, &cursor, &query, fs, trail));

  /* Seek through the records for this key, trying to find the record that
     includes OFFSET. Note that we don't require reading from more than
     one record since we're allowed to return partial reads.  */
  while (length <= offset)
    {
      offset -= length;

      db_err = get_next_length (&length, cursor, &query);

      /* No more records? They tried to read past the end. */
      if (db_err == DB_NOTFOUND)
        {
          *len = 0;
          return SVN_NO_ERROR;
        }
      if (db_err)
        return DB_WRAP (fs, "reading string", db_err);
    }

  /* The current record contains OFFSET. Fetch the contents now. Note that
     OFFSET has been moved to be relative to this record. The length could
     quite easily extend past this record, but no big deal. We also keep
     the DB_DBT_PARTIAL to read little pieces at this location.  */
  svn_fs__clear_dbt (&result);
  result.data = buf;
  result.ulen = *len;
  result.doff = offset;
  result.dlen = *len;
  result.flags |= (DB_DBT_USERMEM | DB_DBT_PARTIAL);
  db_err = cursor->c_get (cursor, &query, &result, DB_CURRENT);
  if (db_err)
    goto cursor_error;

  /* Done with the cursor. */
  SVN_ERR (DB_WRAP (fs, "closing string-reading cursor",
                    cursor->c_close (cursor)));

  *len = result.size;
  return SVN_NO_ERROR;

cursor_error:
  /* An error occurred somewhere. Close the cursor and return the error. */
  cursor->c_close (cursor);
  return DB_WRAP (fs, "reading string", db_err);
}


svn_error_t *
svn_fs__string_read (svn_fs_t *fs,
                     const char *key,
                     char *buf,
                     apr_off_t offset,
                     apr_size_t *len,
                     trail_t *trail)
{
  apr_size_t amt_read = 0;

  while (1)
    {
      apr_size_t size = *len - amt_read;
      SVN_ERR (string_read (fs, key, buf + amt_read, offset + amt_read, 
                            &size, trail));
      amt_read += size;
      if ((size == 0) || (amt_read == *len))
        break;
    }
  
  *len = amt_read;
  return SVN_NO_ERROR;
}


/* Get the current 'next-key' value and bump the record. */
static svn_error_t *
get_key_and_bump (svn_fs_t *fs, const char **key, trail_t *trail)
{
  DBC *cursor;
  char next_key[200];
  apr_size_t key_len;
  int db_err;
  DBT query;
  DBT result;

  /* ### todo: see issue #409 for why bumping the key as part of this
     trail is problematic. */

  /* Open a cursor and move it to the 'next-key' value. We can then fetch
     the contents and use the cursor to overwrite those contents. Since
     this database allows duplicates, we can't do an arbitrary 'put' to
     write the new value -- that would append, not overwrite.  */

  SVN_ERR (DB_WRAP (fs, "creating cursor for reading a string",
                    fs->strings->cursor (fs->strings, trail->db_txn,
                                         &cursor, 0)));

  /* Advance the cursor to 'next-key' and read it. */

  db_err = cursor->c_get (cursor,
                          svn_fs__str_to_dbt (&query,
                                              (char *) svn_fs__next_key_key),
                          svn_fs__result_dbt (&result),
                          DB_SET);
  if (db_err)
    {
      cursor->c_close (cursor);
      return DB_WRAP (fs, "getting next-key value", db_err);
    }

  svn_fs__track_dbt (&result, trail->pool);
  *key = apr_pstrndup (trail->pool, result.data, result.size);

  /* Bump to future key. */
  key_len = result.size;
  svn_fs__next_key (result.data, &key_len, next_key);

  /* Shove the new key back into the database, at the cursor position. */
  db_err = cursor->c_put (cursor, &query,
                          svn_fs__str_to_dbt (&result, (char *) next_key),
                          DB_CURRENT);

  cursor->c_close (cursor);

  return DB_WRAP (fs, "bumping next string key", db_err);
}

svn_error_t *
svn_fs__string_append (svn_fs_t *fs,
                       const char **key,
                       apr_size_t len,
                       const char *buf,
                       trail_t *trail)
{
  DBT query, result;

  /* If the passed-in key is NULL, we graciously generate a new string
     using the value of the `next-key' record in the strings table. */
  if (*key == NULL)
    {
      SVN_ERR (get_key_and_bump (fs, key, trail));
    }

  /* Store a new record into the database. */
  SVN_ERR (DB_WRAP (fs, "appending string",
                    fs->strings->put
                    (fs->strings, trail->db_txn,
                     svn_fs__str_to_dbt (&query, (char *) (*key)),
                     svn_fs__set_dbt (&result, (void *) buf, len),
                     0)));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs__string_clear (svn_fs_t *fs,
                      const char *key,
                      trail_t *trail)
{
  int db_err;
  DBT query, result;

  svn_fs__str_to_dbt (&query, (char *)key);

  /* Torch the prior contents */
  db_err = fs->strings->del (fs->strings, trail->db_txn, &query, 0);

  /* If there's no such node, return an appropriately specific error.  */
  if (db_err == DB_NOTFOUND)
    return svn_error_createf
      (SVN_ERR_FS_NO_SUCH_STRING, 0, 0, fs->pool,
       "svn_fs__string_clear: no such string `%s'", key);

  /* Handle any other error conditions.  */
  SVN_ERR (DB_WRAP (fs, "clearing string", db_err));

  /* Shove empty data back in for this key. */
  svn_fs__clear_dbt (&result);
  result.data = 0;
  result.size = 0;
  result.flags |= DB_DBT_USERMEM;

  return DB_WRAP (fs, "storing empty contents",
                  fs->strings->put (fs->strings, trail->db_txn,
                                    &query, &result, 0));
}


svn_error_t *
svn_fs__string_size (apr_size_t *size,
                     svn_fs_t *fs,
                     const char *key,
                     trail_t *trail)
{
  int db_err;
  DBT query;
  DBC *cursor;
  apr_size_t length;
  apr_size_t total;

  svn_fs__str_to_dbt (&query, (char *) key);

  SVN_ERR (locate_key (&length, &cursor, &query, fs, trail));

  total = length;
  while (1)
    {
      db_err = get_next_length (&length, cursor, &query);

      /* No more records? Then return the total length. */
      if (db_err == DB_NOTFOUND)
        {
          *size = total;
          return SVN_NO_ERROR;
        }
      if (db_err)
        return DB_WRAP (fs, "fetching string length", db_err);

      total += length;
    }

  /* NOTREACHED */
}


svn_error_t *
svn_fs__string_delete (svn_fs_t *fs,
                       const char *key,
                       trail_t *trail)
{
  int db_err;
  DBT query;

  db_err = fs->strings->del (fs->strings, trail->db_txn,
                             svn_fs__str_to_dbt (&query, (char *) key), 0);

  /* If there's no such node, return an appropriately specific error.  */
  if (db_err == DB_NOTFOUND)
    return svn_error_createf
      (SVN_ERR_FS_NO_SUCH_STRING, 0, 0, fs->pool,
       "svn_fs__delete_string: no such string `%s'", key);

  /* Handle any other error conditions.  */
  SVN_ERR (DB_WRAP (fs, "deleting string", db_err));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs__string_copy (svn_fs_t *fs,
                     const char **new_key,
                     const char *key,
                     trail_t *trail)
{
  DBT query;
  DBT result;
  DBT copykey;
  DBC *cursor;
  int db_err;

  SVN_ERR (get_key_and_bump (fs, new_key, trail));

  SVN_ERR (DB_WRAP (fs, "creating cursor for reading a string",
                    fs->strings->cursor (fs->strings, trail->db_txn,
                                         &cursor, 0)));

  svn_fs__str_to_dbt (&query, (char *) key);
  svn_fs__str_to_dbt (&copykey, (char *) *new_key);

  svn_fs__clear_dbt (&result);

  /* Move to the first record and fetch its data (under BDB's mem mgmt). */
  db_err = cursor->c_get (cursor, &query, &result, DB_SET);
  if (db_err)
    {
      cursor->c_close (cursor);
      return DB_WRAP (fs, "getting next-key value", db_err);
    }

  while (1)
    {
      /* ### can we pass a BDB-provided buffer to another BDB function?
         ### they are supposed to have a duration up to certain points
         ### of calling back into BDB, but I'm not sure what the exact
         ### rules are. it is definitely nicer to use BDB buffers here
         ### to simplify things and reduce copies, but... hrm.
      */

      /* Write the data to the database */
      db_err = fs->strings->put (fs->strings, trail->db_txn,
                                 &copykey, &result, 0);
      if (db_err)
        {
          cursor->c_close (cursor);
          return DB_WRAP (fs, "writing copied data", db_err);
        }

      /* Read the next chunk. Terminate loop if we're done. */
      svn_fs__clear_dbt (&result);
      db_err = cursor->c_get (cursor, &query, &result, DB_NEXT_DUP);
      if (db_err == DB_NOTFOUND)
        break;
      if (db_err)
        {
          cursor->c_close (cursor);
          return DB_WRAP (fs, "fetching string data for a copy", db_err);
        }
    }

  cursor->c_close (cursor);

  return SVN_NO_ERROR;
}



/* 
 * local variables:
 * eval: (load-file "../../tools/dev/svn-dev.el")
 * end:
 */
