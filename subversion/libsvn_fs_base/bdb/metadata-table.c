/* metadata-table.c : operations on the `metadata' table
 *
 * ====================================================================
 * Copyright (c) 2008 CollabNet.  All rights reserved.
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
#include <assert.h>
#include "bdb_compat.h"

#include "svn_pools.h"
#include "dbt.h"
#include "../err.h"
#include "../fs.h"
#include "../trail.h"
#include "../../libsvn_fs/fs-loader.h"
#include "bdb-err.h"
#include "metadata-table.h"

#include "private/svn_fs_util.h"


int
svn_fs_bdb__open_metadata_table(DB **metadata_p,
                               DB_ENV *env,
                               svn_boolean_t create)
{
  const u_int32_t open_flags = (create ? (DB_CREATE | DB_EXCL) : 0);
  DB *metadata;
  int error;

  BDB_ERR(svn_fs_bdb__check_version());
  BDB_ERR(db_create(&metadata, env, 0));
  error = (metadata->open)(SVN_BDB_OPEN_PARAMS(metadata, NULL),
                           "metadata", 0, DB_BTREE,
                           open_flags, 0666);

  /* Create the table if it doesn't yet exist.  This is a form of
     automagical repository upgrading. */
  if (error == ENOENT && (! create))
    {
      BDB_ERR(metadata->close(metadata, 0));
      return svn_fs_bdb__open_metadata_table(metadata_p, env, TRUE);
    }
  BDB_ERR(error);

  /* If we're creating the table from scratch (not upgrading), record the
     upgrade rev as 0. */
  if (create)
    {
      DBT key, value;

      BDB_ERR(metadata->put(metadata, 0,
                            svn_fs_base__str_to_dbt(&key,
                                   SVN_FS_BASE__METADATA_FORWARD_DELTA_UPGRADE),
                            svn_fs_base__str_to_dbt(&value, "0"), 0));
    }

  *metadata_p = metadata;
  return 0;
}


svn_error_t *
svn_fs_bdb__metadata_set(svn_fs_t *fs,
                         const char *key_str,
                         const char *val,
                         trail_t *trail,
                         apr_pool_t *pool)
{
  base_fs_data_t *bfd = fs->fsap_data;
  DBT key, value;

  svn_fs_base__str_to_dbt(&key, key_str);
  if (val == NULL)
    {
      svn_fs_base__trail_debug(trail, "metadata", "del");
      return BDB_WRAP(fs, "deleting metadata from 'metadata' table",
                      bfd->metadata->del(bfd->metadata, trail->db_txn,
                                         &key, 0));
    }
  else
    {
      svn_fs_base__str_to_dbt(&value, val);
      svn_fs_base__trail_debug(trail, "metadata", "add");
      return BDB_WRAP(fs, "storing metadata record",
                      bfd->metadata->put(bfd->metadata, trail->db_txn,
                                         &key, &value, 0));
    }
}


svn_error_t *
svn_fs_bdb__metadata_get(const char **val,
                         svn_fs_t *fs,
                         const char *key_str,
                         trail_t *trail,
                         apr_pool_t *pool)
{
  base_fs_data_t *bfd = fs->fsap_data;
  DBT key, value;
  int db_err;

  svn_fs_base__trail_debug(trail, "metadata", "get");
  db_err = bfd->metadata->get(bfd->metadata, trail->db_txn,
                              svn_fs_base__str_to_dbt(&key, key_str),
                              svn_fs_base__result_dbt(&value), 0);
  svn_fs_base__track_dbt(&value, pool);

  if (db_err == DB_NOTFOUND)
    return svn_fs_base__err_no_such_metadata(fs, key_str);

  *val = apr_pstrmemdup(pool, value.data, value.size);
  return SVN_NO_ERROR;
}
