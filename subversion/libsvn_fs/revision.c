/* revision.c --- functions for working with filesystem revisions
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

#include "db.h"

#include "svn_fs.h"
#include "fs.h"
#include "revision.h"
#include "err.h"
#include "convert-size.h"
#include "skel.h"
#include "dbt.h"


/* Building some often-used error objects.  */


static svn_error_t *
corrupt_revision (svn_fs_t *fs, svn_revnum_t v)
{
  return
    svn_error_createf (SVN_ERR_FS_CORRUPT, 0, 0, fs->pool,
                       "corrupt root data for revision %ld of filesystem `%s'",
                       v, fs->env_path);
}


static svn_error_t *
no_such_revision (svn_fs_t *fs, svn_revnum_t v)
{
  return
    svn_error_createf (SVN_ERR_FS_NO_SUCH_REVISION, 0, 0, fs->pool,
                       "filesystem `%s' has no revision number %ld",
                       fs->env_path, v);
}



/* Reading revisions.  */


/* Set *SKEL to the REVISION skel of revision V of the filesystem FS.
   SKEL and the data block it points into will both be freed when POOL
   is cleared.

   Beyond verifying that it's a syntactically valid skel, this doesn't
   validate the data returned at all.  */
static svn_error_t *
get_revision_skel (skel_t **skel,
                   svn_fs_t *fs,
                   svn_revnum_t v, 
                   apr_pool_t *pool)
{
  db_recno_t recno;
  DBT key, value;
  int db_err;
  skel_t *revision;

  SVN_ERR (svn_fs__check_fs (fs));

  /* Turn the revision number into a Berkeley DB record number.
     Revisions are numbered starting with zero; Berkeley DB record numbers
     begin with one.  */
  recno = v + 1;
  svn_fs__set_dbt (&key, &recno, sizeof (recno));

  svn_fs__result_dbt (&value);
  db_err = fs->revisions->get (fs->revisions, 0, /* no transaction */ 
                               &key, &value, 0);
  if (db_err == DB_NOTFOUND)
    return no_such_revision (fs, v);
  SVN_ERR (DB_WRAP (fs, "reading revision root from filesystem", db_err));
  svn_fs__track_dbt (&value, pool);

  revision = svn_fs__parse_skel (value.data, value.size, pool);
  if (! revision)
    return corrupt_revision (fs, v);
  *skel = revision;
  return SVN_NO_ERROR;
}


/* Set *ID to ID of the root of revision V of the filesystem FS.
   Allocate the ID in POOL.  */
svn_error_t *
svn_fs__revision_root (svn_fs_id_t **id_p,
                       svn_fs_t *fs,
                       svn_revnum_t v,
                       apr_pool_t *pool)
{
  apr_pool_t *subpool = svn_pool_create (pool);
  skel_t *revision, *id_skel;
  svn_fs_id_t *id;

  SVN_ERR (get_revision_skel (&revision, fs, v, subpool));
  if (svn_fs__list_length (revision) != 3
      || ! svn_fs__matches_atom (revision->children, "revision"))
    goto corrupt;

  id_skel = revision->children->next;
  if (! id_skel->is_atom)
    goto corrupt;

  id = svn_fs_parse_id (id_skel->data, id_skel->len, pool);
  if (! id)
    goto corrupt;

  *id_p = id;
  return SVN_NO_ERROR;

 corrupt:
  apr_pool_destroy (subpool);
  return corrupt_revision (fs, v);
}



/* Writing revisions.  */


/* Add SKEL as a new revision to FS's `revisions' table.  Set *V_P to
   the number of the new revision created.  Do this as part of the
   Berkeley DB transaction TXN; if TXN is zero, then make the change
   without transaction protection.

   Do any necessary temporary allocation in POOL.  */
static svn_error_t *
put_revision_skel (svn_revnum_t *v_p,
                  svn_fs_t *fs,
                  skel_t *skel,
                  DB_TXN *txn,
                  apr_pool_t *pool)
{
  db_recno_t recno;
  DB *revisions = fs->revisions;
  DBT key, value;

  /* Since we use the DB_APPEND flag, the `put' call sets recno to the record
     number of the new revision.  */
  recno = 0;
  svn_fs__clear_dbt (&key);
  key.data = &recno;
  key.size = key.ulen = sizeof (recno);
  key.flags |= DB_DBT_USERMEM;

  SVN_ERR (DB_WRAP (fs, "adding new revision",
                    revisions->put (revisions, txn,
                                    &key,
                                    svn_fs__skel_to_dbt (&value, skel, pool),
                                    DB_APPEND)));

  /* Turn the record number into a Subversion revision number.
     Revisions are numbered starting with zero; Berkeley DB record numbers
     begin with one.  */
  *v_p = recno - 1;
  return SVN_NO_ERROR;
}



/* Creating and opening a filesystem's `revisions' table.  */


/* Open / create FS's `revisions' table.  FS->env must already be open;
   this function initializes FS->revisions.  If CREATE is non-zero, assume
   we are creating the filesystem afresh; otherwise, assume we are
   simply opening an existing database.  */
static svn_error_t *
make_revisions (svn_fs_t *fs, int create)
{
  SVN_ERR (DB_WRAP (fs, "allocating `revisions' table object",
                    db_create (&fs->revisions, fs->env, 0)));
  SVN_ERR (DB_WRAP (fs,
                    (create
                     ? "creating `revisions' table"
                     : "opening `revisions' table"),
                    fs->revisions->open (fs->revisions, "revisions", 0,
                                         DB_RECNO,
                                         create ? (DB_CREATE | DB_EXCL) : 0,
                                         0666)));

  return SVN_NO_ERROR;
}


/* Create a new `revisions' table for the new filesystem FS.  FS->env
   must already be open; this sets FS->revisions.  */
svn_error_t *
svn_fs__create_revisions (svn_fs_t *fs)
{
  return make_revisions (fs, 1);
}


/* Open the existing `revisions' table for the filesystem FS.  FS->env
   must already be open; this sets FS->revisions.  */
svn_error_t *
svn_fs__open_revisions (svn_fs_t *fs)
{
  return make_revisions (fs, 0);
}



/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
