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
 * ====================================================================
 */

#include "db.h"
#include "svn_fs.h"
#include "fs.h"
#include "err.h"
#include "dbt.h"
#include "trail.h"
#include "strings-table.h"



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

  *strings_p = strings;
  return 0;
}



/*** Storing and retrieving strings.  ***/

struct string_baton
{
  svn_fs_t *fs;           /* Which filesystem should we look in? */
  const char *key;        /* Which string in the `strings' table? */
  u_int32_t offset;       /* Where are we in the string? */
  trail_t *trail;         /* The database trail to work in. */
};


#if 0 /* left in for cannibalization */
static svn_error_t *
string_read (void *baton, char *buffer, apr_size_t *len)
{
  int db_err;
  DBT query, result;
  struct string_baton *sb = baton;

  svn_fs__clear_dbt (&result);
  result.data = buffer;
  result.ulen = *len;
  result.doff = sb->offset;
  result.dlen = *len;
  result.flags |= (DB_DBT_USERMEM | DB_DBT_PARTIAL);

  db_err = sb->fs->strings->get (sb->fs->strings, sb->trail->db_txn,
                                 svn_fs__str_to_dbt (&query, (char *) sb->key),
                                 &result, 0);

  /* If there's no such node, return an appropriately specific error.  */
  if (db_err == DB_NOTFOUND)
    return svn_error_createf
      (SVN_ERR_FS_NO_SUCH_STRING, 0, 0, sb->fs->pool,
       "string_read: no such string `%s'", sb->key);

  /* Handle any other error conditions.  */
  SVN_ERR (DB_WRAP (sb->fs, "reading string", db_err));

  sb->offset += result.size;
  *len = result.size;

  return SVN_NO_ERROR;
}
#endif /* 0 */


#if 0 /* left in for cannibalization */
static svn_error_t *
string_write (void *baton, const char *data, apr_size_t *len)
{
  DBT query, result;
  struct string_baton *sb = baton;

  svn_fs__clear_dbt (&result);
  result.data = (char *) data;
  result.size = *len;
  result.doff = sb->offset;
  result.dlen = *len;
  result.flags |= (DB_DBT_USERMEM | DB_DBT_PARTIAL);

  SVN_ERR (DB_WRAP (sb->fs, "storing string",
                    sb->fs->strings->put
                    (sb->fs->strings, sb->trail->db_txn,
                     svn_fs__str_to_dbt (&query, (char *) sb->key),
                     &result, 0)));

  *len = result.size;
  sb->offset += result.size;

  return SVN_NO_ERROR;
}
#endif /* 0 */


svn_error_t *
svn_fs__string_size (apr_size_t *size,
                     svn_fs_t *fs,
                     const char *key,
                     trail_t *trail)
{
  int db_err;
  DBT query, result;

  /* kff todo: is svn_fs__nodata_dbt() the right thing to use here? */
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

  /* Handle any other error conditions.  */
  SVN_ERR (DB_WRAP (fs, "reading string", db_err));

  /* kff todo: how can we know this cast is safe? */
  *size = (apr_size_t) result.size;

  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs__string_read (svn_fs_t *fs,
                     const char *key,
                     apr_off_t offset,
                     apr_size_t *len,
                     char *buf,
                     trail_t *trail)
{
  /* ### implement ### */
  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs__string_append (svn_fs_t *fs,
                       const char **key,
                       apr_size_t len,
                       const char *buf,
                       trail_t *trail)
{
  /* ### implement ### */
  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs__string_clear (svn_fs_t *fs,
                      const char *key,
                      trail_t *trail)
{
  /* ### implement ### */
  return SVN_NO_ERROR;
}


#if 0
/* Left this code to help with implementation of string_append() above. */
svn_error_t *
svn_fs__append_string_stream (svn_stream_t **stream,
                              svn_fs_t *fs,
                              const char *key,
                              trail_t *trail)
{
  struct string_baton *baton = apr_pcalloc (trail->pool, sizeof (*baton));
  svn_stream_t *s = svn_stream_create (baton, trail->pool);

  {
    /* Is there some other way to append to a record?  Without using
       DB_DBT_PARTIAL?  DB_APPEND isn't it; that's for appending to
       the end of a query or recno database, not appending to the end
       of an individual record value. */
    svn_error_t *err;
    apr_size_t size;
    err = svn_fs__string_size (&size, fs, key, trail);

    if (! err)
      baton->offset = size;
    else if (err->apr_err == SVN_ERR_FS_NO_SUCH_STRING)
      baton->offset = 0;
    else
      return err;
  }
  
  baton->fs     = fs;
  baton->key    = key;
  baton->trail  = trail;
  
  svn_stream_set_write (s, string_write);

  *stream = s;
  return SVN_NO_ERROR;
}
#endif /* 0 */


svn_error_t *
svn_fs__delete_string (svn_fs_t *fs,
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



/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
