/* uuids-table.c : operations on the `uuids' table
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

#include <apr_uuid.h>

#include "bdb_compat.h"
#include "svn_fs.h"
#include "../fs.h"
#include "../err.h"
#include "dbt.h"
#include "../trail.h"
#include "bdb-err.h"
#include "uuids-table.h"


/*** Creating and opening the uuids table.
     When the table is created, the repository's uuid is
     is generated and stored as record #1. ***/

int
svn_fs__bdb_open_uuids_table (DB **uuids_p,
                              DB_ENV *env,
                              int create)
{
  const u_int32_t open_flags = (create ? (DB_CREATE | DB_EXCL) : 0);
  char buffer[APR_UUID_FORMATTED_LENGTH + 1];
  DB *uuids;
  int error;

  BDB_ERR (svn_fs__bdb_check_version());
  BDB_ERR (db_create (&uuids, env, 0));
  BDB_ERR (uuids->set_re_len (uuids, sizeof (buffer) - 1));
  
  error = uuids->open (SVN_BDB_OPEN_PARAMS (uuids, NULL),
                       "uuids", 0, DB_RECNO,
                       open_flags | SVN_BDB_AUTO_COMMIT,
                       0666);
  
  /* This is a temporary compatibility check; it creates the
     UUIDs table if one does not already exist. */
  if (error == ENOENT && create == 0)
    {
      BDB_ERR (uuids->close (uuids, 0));
      return svn_fs__bdb_open_uuids_table (uuids_p, env, 1);
    }

  BDB_ERR (error);    

  if (create)
    {
      DBT key, value;
      apr_uuid_t uuid;
      int recno = 0;

      svn_fs__clear_dbt (&key);
      key.data = &recno;
      key.size = sizeof (recno);

      svn_fs__clear_dbt (&value);
      value.data = buffer;
      value.size = sizeof (buffer) - 1;

      apr_uuid_get (&uuid);
      apr_uuid_format (buffer, &uuid);

      BDB_ERR (uuids->put (uuids, 0, &key, &value, 
                           DB_APPEND | SVN_BDB_AUTO_COMMIT));
    }
  
  *uuids_p = uuids;
  return 0;
}

svn_error_t *svn_fs__bdb_get_uuid (svn_fs_t *fs,
                                   int idx,
                                   const char **uuid,
                                   trail_t *trail)
{
    char buffer[APR_UUID_FORMATTED_LENGTH + 1];
    DB *uuids = fs->uuids;
    DBT key;
    DBT value;

    svn_fs__clear_dbt (&key);
    key.data = &idx;
    key.size = sizeof (idx);

    svn_fs__clear_dbt (&value);
    value.data = buffer;
    value.size = sizeof (buffer) - 1;

    SVN_ERR (BDB_WRAP (fs, "get repository uuid",
                       uuids->get (uuids, trail->db_txn, &key, &value, 0)));

    *uuid = apr_pstrmemdup (trail->pool, value.data, value.size + 1);

    return SVN_NO_ERROR;
}

svn_error_t *svn_fs__bdb_set_uuid (svn_fs_t *fs,
                                   int idx,
                                   const char *uuid,
                                   trail_t *trail)
{
    DB *uuids = fs->uuids;
    DBT key;
    DBT value;

    svn_fs__clear_dbt (&key);
    key.data = &idx;
    key.size = sizeof (idx);

    svn_fs__clear_dbt (&value);
    value.size = strlen (uuid);
    value.data = apr_pstrmemdup (trail->pool, uuid, value.size + 1);
     
    SVN_ERR (BDB_WRAP (fs,
                       "set repository uuid",
                       uuids->put (uuids, trail->db_txn, &key, &value, 0)));

    return SVN_NO_ERROR;
}
