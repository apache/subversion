/* rev-table.c : working with the `revisions' table
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
#include "svn_fs.h"
#include "../fs.h"
#include "../err.h"
#include "../util/skel.h"
#include "../util/fs_skels.h"
#include "dbt.h"
#include "rev-table.h"


/* Opening/creating the `revisions' table.  */

int svn_fs__open_revisions_table (DB **revisions_p,
                                  DB_ENV *env,
                                  int create)
{
  const int open_flags = (create ? (DB_CREATE | DB_EXCL) : 0);
  DB *revisions;

  DB_ERR (svn_bdb__check_version());
  DB_ERR (db_create (&revisions, env, 0));
  DB_ERR (revisions->open (SVN_BDB_OPEN_PARAMS(revisions, NULL),
                           "revisions", 0, DB_RECNO,
                           open_flags | SVN_BDB_AUTO_COMMIT,
                           0666));

  *revisions_p = revisions;
  return 0;
}



/* Storing and retrieving filesystem revisions.  */


svn_error_t *
svn_fs__get_rev (svn_fs__revision_t **revision_p,
                 svn_fs_t *fs,
                 svn_revnum_t rev,
                 trail_t *trail)
{
  int db_err;
  DBT key, value;
  skel_t *skel;
  svn_fs__revision_t *revision;

  /* Turn the revision number into a Berkeley DB record number.
     Revisions are numbered starting with zero; Berkeley DB record
     numbers begin with one.  */
  db_recno_t recno = rev + 1;

  db_err = fs->revisions->get (fs->revisions, trail->db_txn,
                               svn_fs__set_dbt (&key, &recno, sizeof (recno)),
                               svn_fs__result_dbt (&value),
                               0);
  svn_fs__track_dbt (&value, trail->pool);

  /* If there's no such revision, return an appropriately specific error.  */
  if (db_err == DB_NOTFOUND)
    return svn_fs__err_dangling_rev (fs, rev);

  /* Handle any other error conditions.  */
  SVN_ERR (DB_WRAP (fs, "reading filesystem revision", db_err));

  /* Parse REVISION skel.  */
  skel = svn_fs__parse_skel (value.data, value.size, trail->pool);
  if (! skel)
    return svn_fs__err_corrupt_fs_revision (fs, rev);
    
  /* Convert skel to native type. */
  SVN_ERR (svn_fs__parse_revision_skel (&revision, skel, trail->pool));

  *revision_p = revision;
  return SVN_NO_ERROR;
}


/* Write REVISION to FS as part of TRAIL.  If *REV is a valid revision
   number, write this revision as one that corresponds to *REV, else
   write a new revision and return its newly created revision number
   in *REV.  */
svn_error_t *
svn_fs__put_rev (svn_revnum_t *rev,
                 svn_fs_t *fs,
                 const svn_fs__revision_t *revision,
                 trail_t *trail)
{
  int db_err;
  db_recno_t recno = 0;
  skel_t *skel;
  DBT key, value;

  /* Convert native type to skel. */
  SVN_ERR (svn_fs__unparse_revision_skel (&skel, revision, trail->pool));

  if (SVN_IS_VALID_REVNUM (*rev))
    {
      DBT query, result;
      
      /* Update the filesystem revision with the new skel. */
      recno = *rev + 1;
      db_err = fs->revisions->put 
        (fs->revisions, trail->db_txn,
         svn_fs__set_dbt (&query, &recno, sizeof (recno)),
         svn_fs__skel_to_dbt (&result, skel, trail->pool), 0);
      return DB_WRAP (fs, "updating filesystem revision", db_err);
    }
      
  db_err = fs->revisions->put (fs->revisions, trail->db_txn,
                               svn_fs__recno_dbt(&key, &recno),
                               svn_fs__skel_to_dbt (&value, skel, trail->pool),
                               DB_APPEND);
  SVN_ERR (DB_WRAP (fs, "storing filesystem revision", db_err));

  /* Turn the record number into a Subversion revision number.
     Revisions are numbered starting with zero; Berkeley DB record
     numbers begin with one.  */
  *rev = recno - 1;
  return SVN_NO_ERROR;
}



/* Getting the youngest revision.  */


svn_error_t *
svn_fs__youngest_rev (svn_revnum_t *youngest_p,
                      svn_fs_t *fs,
                      trail_t *trail)
{
  int db_err;
  DBC *cursor = 0;
  DBT key, value;
  db_recno_t recno;

  SVN_ERR (svn_fs__check_fs (fs));

  /* Create a database cursor.  */
  SVN_ERR (DB_WRAP (fs, "getting youngest revision (creating cursor)",
                    fs->revisions->cursor (fs->revisions, trail->db_txn,
                                           &cursor, 0)));

  /* Find the last entry in the `revisions' table.  */
  db_err = cursor->c_get (cursor,
                          svn_fs__recno_dbt (&key, &recno),
                          svn_fs__nodata_dbt (&value),
                          DB_LAST);

  if (db_err)
    {
      /* Free the cursor.  Ignore any error value --- the error above
         is more interesting.  */
      cursor->c_close (cursor);

      if (db_err == DB_NOTFOUND)
        /* The revision 0 should always be present, at least.  */
        return
          svn_error_createf
          (SVN_ERR_FS_CORRUPT, 0, 0,
           "revision 0 missing from `revisions' table, in filesystem `%s'",
           fs->path);
      
      SVN_ERR (DB_WRAP (fs, "getting youngest revision (finding last entry)",
                        db_err));
    }

  /* You can't commit a transaction with open cursors, because:
     1) key/value pairs don't get deleted until the cursors referring
     to them are closed, so closing a cursor can fail for various
     reasons, and txn_commit shouldn't fail that way, and 
     2) using a cursor after committing its transaction can cause
     undetectable database corruption.  */
  SVN_ERR (DB_WRAP (fs, "getting youngest revision (closing cursor)",
                    cursor->c_close (cursor)));

  /* Turn the record number into a Subversion revision number.
     Revisions are numbered starting with zero; Berkeley DB record
     numbers begin with one.  */
  *youngest_p = recno - 1;
  return SVN_NO_ERROR;
}



/* 
 * local variables:
 * eval: (load-file "../../../tools/dev/svn-dev.el")
 * end:
 */
