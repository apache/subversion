/* rev-table.c : working with the `revisions' table
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

#include <db.h>
#include "svn_fs.h"
#include "fs.h"
#include "err.h"
#include "dbt.h"
#include "skel.h"
#include "validate.h"
#include "rev-table.h"


/* Opening/creating the `revisions' table.  */

int svn_fs__open_revisions_table (DB **revisions_p,
                                  DB_ENV *env,
                                  int create)
{
  DB *revisions;

  DB_ERR (db_create (&revisions, env, 0));
  DB_ERR (revisions->open (revisions, "revisions", 0, DB_RECNO,
                           create ? (DB_CREATE | DB_EXCL) : 0,
                           0666));

  *revisions_p = revisions;
  return 0;
}



/* Storing and retrieving filesystem revisions.  */


static int
is_valid_filesystem_revision (skel_t *skel)
{
  int len = svn_fs__list_length (skel);

  if (len == 3)
    {
      if (svn_fs__is_atom (skel->children, "revision")
          && skel->children->next != NULL
          && svn_fs__is_valid_proplist (skel->children->next->next))
        {
          skel_t *id = skel->children->next;
          if (id->is_atom
              && 0 == (1 & svn_fs__count_id_components (id->data, id->len)))
            return 1;
        }
    }

  return 0;
}


svn_error_t *
svn_fs__get_rev (skel_t **skel_p,
                 svn_fs_t *fs,
                 svn_revnum_t rev,
                 DB_TXN *db_txn,
                 apr_pool_t *pool)
{
  int db_err;
  DBT key, value;
  skel_t *skel;
  db_recno_t recno = rev + 1;

  db_err = fs->revisions->get (fs->revisions, db_txn,
                               svn_fs__set_dbt (&key, &recno, sizeof (recno)),
                               svn_fs__result_dbt (&value),
                               0);

  /* If there's no such revision, return an appropriately specific error.  */
  if (db_err == DB_NOTFOUND)
    return svn_fs__err_dangling_rev (fs, rev);

  /* Handle any other error conditions.  */
  SVN_ERR (DB_WRAP (fs, "reading filesystem revision", db_err));

  /* Make sure the skel's contents get freed when POOL is destroyed.  */
  svn_fs__track_dbt (&value, pool);

  /* Parse and check the REVISION skel.  */
  skel = svn_fs__parse_skel (value.data, value.size, pool);
  if (! skel
      || ! is_valid_filesystem_revision (skel))
    return svn_fs__err_corrupt_fs_revision (fs, rev);

  *skel_p = skel;
  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs__put_rev (svn_revnum_t *rev,
                 svn_fs_t *fs,
                 skel_t *skel,
                 DB_TXN *db_txn,
                 apr_pool_t *pool)
{
  int db_err;
  DBT key, value;
  db_recno_t recno = 0;

  /* xbc FIXME: Need a useful revision number here. */
  if (! is_valid_filesystem_revision (skel))
    return svn_fs__err_corrupt_fs_revision (fs, -1);

  db_err = fs->revisions->put (fs->revisions, db_txn,
                               svn_fs__set_dbt (&key, &recno, sizeof (recno)),
                               svn_fs__skel_to_dbt (&value, skel, pool),
                               DB_APPEND);
  SVN_ERR (DB_WRAP (fs, "storing filesystem revision", db_err));

  *rev = recno - 1;
  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs__rev_get_root (svn_fs_id_t **root_id_p,
                      svn_fs_t *fs,
                      svn_revnum_t rev,
                      trail_t *trail)
{
  skel_t *skel;
  svn_fs_id_t *id;

  SVN_ERR (svn_fs__get_rev (&skel, fs, rev,
                            trail->db_txn, trail->pool));

  id = svn_fs_parse_id (skel->children->next->data,
                        skel->children->next->len,
                        trail->pool);

  /* The skel validator doesn't check the ID format. */
  if (id == NULL)
    return svn_fs__err_corrupt_fs_revision (fs, -1);

  *root_id_p = id;
  return SVN_NO_ERROR;
}



/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
