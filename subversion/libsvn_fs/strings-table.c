/* strings-table.c : operations on the `strings' table
 *
 * ====================================================================
 * Copyright (c) 2000-2001 CollabNet.  All rights reserved.
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

svn_error_t *
svn_fs__string_read (svn_fs_t *fs,
                     const char *key,
                     char *buf,
                     apr_off_t offset,
                     apr_size_t *len,
                     trail_t *trail)
{
  int db_err;
  DBT query, result;

  svn_fs__clear_dbt (&result);
  result.data = buf;
  result.ulen = *len;
  result.doff = offset;
  result.dlen = *len;
  result.flags |= (DB_DBT_USERMEM | DB_DBT_PARTIAL);

  db_err = fs->strings->get (fs->strings, trail->db_txn,
                             svn_fs__str_to_dbt (&query, (char *) key),
                             &result, 0);

  /* If there's no such node, return an appropriately specific error.  */
  if (db_err == DB_NOTFOUND)
    return svn_error_createf
      (SVN_ERR_FS_NO_SUCH_STRING, 0, 0, fs->pool,
       "svn_fs__string_read: no such string `%s'", key);

  /* Handle any other error conditions.  */
  SVN_ERR (DB_WRAP (fs, "reading string", db_err));

  {
    /* ### ugly hack!!  

       Thanks to what is believed to be a bug in Berkeley DB 3.2.9,
       reading off the end of this end (if it is stored in a
       B_OVERFLOW page, as BDB defines it) will not return the proper
       value in results.size, so for now we will hack around that by
       calculating the legal sizes for ourselves. 

       This section of code *should* just be:

           *len = result.size;
       
       and is certainly intended to someday return to that. */
    apr_size_t size;

    SVN_ERR (svn_fs__string_size (&size, fs, key, trail));
    if (offset >= size)
      *len = 0;
    else if (*len > (size - offset))
      *len = size - offset;
  }

  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs__string_append (svn_fs_t *fs,
                       const char **key,
                       apr_size_t len,
                       const char *buf,
                       trail_t *trail)
{
  DBT query, result;
  apr_size_t offset = 0;
  int db_err;

  /* If the passed-in key is NULL, we graciously generate a new string
     using the value of the `next-key' record in the strings table. */
  if (*key == NULL)
    {
      char next_key[200];
      apr_size_t key_len;

      /* ### todo: see issue #409 for why bumping the key as part of this
         trail is problematic. */

      /* Get the current value associated with `next-key'.  */
      svn_fs__str_to_dbt (&query, (char *) svn_fs__next_key_key);
      SVN_ERR (DB_WRAP (fs, "allocating new string (getting next-key)",
                        fs->strings->get (fs->strings,
                                          trail->db_txn,
                                          &query,
                                          svn_fs__result_dbt (&result),
                                          0)));

      svn_fs__track_dbt (&result, trail->pool);
      *key = apr_pstrndup (trail->pool, result.data, result.size);

      /* Bump to future key. */
      key_len = result.size;
      svn_fs__next_key (result.data, &key_len, next_key);
      db_err = fs->strings->put
        (fs->strings, trail->db_txn,
         svn_fs__str_to_dbt (&query, (char *) svn_fs__next_key_key),
         svn_fs__str_to_dbt (&result, (char *) next_key),
         0);

      SVN_ERR (DB_WRAP (fs, "bumping next string key", db_err));
    }
  else
    {
      /* Get the current size of the record. */
      SVN_ERR (svn_fs__string_size (&offset, fs, *key, trail));
    }

  /* Append to it. */
  svn_fs__clear_dbt (&result);
  result.data = (char *) buf;
  result.size = len;
  result.doff = offset;
  result.dlen = len;
  result.flags |= (DB_DBT_USERMEM | DB_DBT_PARTIAL);

  SVN_ERR (DB_WRAP (fs, "appending string",
                    fs->strings->put
                    (fs->strings, trail->db_txn,
                     svn_fs__str_to_dbt (&query, (char *) (*key)),
                     &result, 0)));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs__string_clear (svn_fs_t *fs,
                      const char *key,
                      trail_t *trail)
{
  int db_err;
  DBT query, result;

  svn_fs__clear_dbt (&result);
  result.data = 0;
  result.size = 0;
  result.flags |= DB_DBT_USERMEM;

  db_err = fs->strings->put (fs->strings, trail->db_txn,
                             svn_fs__str_to_dbt (&query, (char *) key),
                             &result, 0);

  /* If there's no such node, return an appropriately specific error.  */
  if (db_err == DB_NOTFOUND)
    return svn_error_createf
      (SVN_ERR_FS_NO_SUCH_STRING, 0, 0, fs->pool,
       "svn_fs__string_clear: no such string `%s'", key);

  /* Handle any other error conditions.  */
  SVN_ERR (DB_WRAP (fs, "clearing string", db_err));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs__string_size (apr_size_t *size,
                     svn_fs_t *fs,
                     const char *key,
                     trail_t *trail)
{
  int db_err;
  DBT query, result;

  svn_fs__clear_dbt (&result);
  result.ulen = 0;
  result.flags |= DB_DBT_USERMEM;

  db_err = fs->strings->get (fs->strings, trail->db_txn,
                             svn_fs__str_to_dbt (&query, (char *) key),
                             &result, 0);

  /* We don't need to svn_fs__track_dbt() the result, because nothing
     was allocated in it. */

  /* If there's no such node, return an appropriately specific error.  */
  if (db_err == DB_NOTFOUND)
    return svn_error_createf
      (SVN_ERR_FS_NO_SUCH_STRING, 0, 0, fs->pool,
       "svn_fs__string_size: no such string `%s'", key);

  if (db_err && db_err != ENOMEM)
    return DB_WRAP (fs, "reading string", db_err);

  /* kff todo: how can we know this cast is safe? */
  *size = (apr_size_t) result.size;

  return SVN_NO_ERROR;
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
  apr_size_t size, offset;
  char buf[10000];

  offset = 0;
  *new_key = NULL;
  while (1)
    {
      /* Reset size to maximum chunk size. */
      size = sizeof (buf);

      /* Read SIZE bytes from the record, starting at OFFSET. */
      SVN_ERR (svn_fs__string_read (fs, key, buf, offset, &size, trail));

      /* Now, write SIZE bytes into the new record. */
      SVN_ERR (svn_fs__string_append (fs, new_key, size, buf, trail));
      offset += size;

      /* If we didn't get the whole chunk, we must be finished. */
      if (size < sizeof (buf))
        break;
    }

  return SVN_NO_ERROR;
}



/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
