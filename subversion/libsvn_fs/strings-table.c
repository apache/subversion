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

svn_error_t *
svn_fs__read_string_stream (svn_stream_t **stream,
                            svn_fs_t *fs,
                            const char *key,
                            trail_t *trail)
{
  abort ();
  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs__write_string_stream (svn_stream_t **stream,
                             svn_fs_t *fs,
                             const char *key,
                             trail_t *trail)
{
  abort ();
  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs__append_string_stream (svn_stream_t **stream,
                              svn_fs_t *fs,
                              const char *key,
                              trail_t *trail)
{
  abort ();
  return SVN_NO_ERROR;
}



/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
