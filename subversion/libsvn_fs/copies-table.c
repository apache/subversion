/* copies-table.c : operations on the `copies' table
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

#include <string.h>

#include "db.h"
#include "fs.h"
#include "err.h"
#include "key-gen.h"
#include "dbt.h"
#include "skel.h"
#include "fs_skels.h"
#include "copies-table.h"
#include "trail.h"
#include "validate.h"
#include "id.h"

static const char next_id_key[] = "next-id";


int
svn_fs__open_copies_table (DB **copies_p,
                           DB_ENV *env,
                           int create)
{
  DB *copies;

  DB_ERR (db_create (&copies, env, 0));
  DB_ERR (copies->open (copies, "copies", 0, DB_BTREE,
                        create ? (DB_CREATE | DB_EXCL) : 0,
                        0666));

  /* Create the initial copy id `0' and the `next-id' table entry.  */
  if (create)
  {
    DBT key, value;

    DB_ERR (copies->put (copies, 0,
                         svn_fs__str_to_dbt (&key, (char *) "0"),
                         svn_fs__str_to_dbt (&value, (char *) "0.0.0"),
                         0));
    DB_ERR (copies->put (copies, 0,
                         svn_fs__str_to_dbt (&key, (char *) next_id_key),
                         svn_fs__str_to_dbt (&value, (char *) "0"),
                         0));
  }

  *copies_p = copies;
  return 0;
}


/* Store COPY as a copy named COPY_ID in FS as part of TRAIL.  */
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
  SVN_ERR (DB_WRAP (fs, "storing copy record",
                    fs->copies->put (fs->copies, trail->db_txn,
                                     &key, &value, 0)));

  return SVN_NO_ERROR;
}


/* Allocate a Subversion copy ID in FS, as part of TRAIL.  Set ID_P to
   *the new copy ID, allocated in TRAIL->pool.  */
static svn_error_t *
allocate_copy_id (char **id_p,
                  svn_fs_t *fs,
                  trail_t *trail)
{
  DBT key, value;
  apr_size_t next_id;
  char *next_id_str;

  svn_fs__str_to_dbt (&key, (char *) next_id_key);

  /* Get the current value associated with the `next-id' key in the
     copies table.  */
  SVN_ERR (DB_WRAP (fs, "allocating new copy ID (getting `next-id')",
                    fs->copies->get (fs->copies, trail->db_txn,
                                     &key,
                                     svn_fs__result_dbt (&value),
                                     0)));
  svn_fs__track_dbt (&value, trail->pool);

  /* That's the value we want to return.  */
  next_id_str = apr_pstrndup (trail->pool, value.data, value.size);

  /* Try to parse the value.  */
  {
    const char *endptr;

    next_id = svn_fs__getsize (value.data, value.size, &endptr, 1000000);
    if (endptr != (const char *) value.data + value.size)
      return svn_fs__err_corrupt_next_id (fs, "copies");
  }

  /* Store the next value.  */
  {
    char buf[200];
    int buf_len;

    buf_len = svn_fs__putsize (buf, sizeof (buf), next_id + 1);
    SVN_ERR (DB_WRAP (fs, "allocating new copy ID (setting `next-id')",
                      fs->copies->put (fs->copies, trail->db_txn,
                                       &key,
                                       svn_fs__set_dbt (&value, buf, buf_len),
                                       0)));
  }

  *id_p = next_id_str;
  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs__create_copy (char **copy_id_p,
                     svn_fs_t *fs,
                     const svn_fs_id_t *dst_noderev_id,
                     trail_t *trail)
{
  char *copy_id;
  svn_fs__copy_t copy;

  SVN_ERR (allocate_copy_id (&copy_id, fs, trail));
  copy.dst_noderev_id = (svn_fs_id_t *) dst_noderev_id;
  SVN_ERR (put_copy (fs, &copy, copy_id, trail));

  *copy_id_p = copy_id;
  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs__delete_copy (svn_fs_t *fs,
                     const char *copy_id,
                     trail_t *trail)
{
  DBT key;

  svn_fs__str_to_dbt (&key, (char *) copy_id);
  return DB_WRAP (fs, "deleting entry from `copies' table",
                  fs->copies->del (fs->copies, trail->db_txn, &key, 0));
}


svn_error_t *
svn_fs__get_copy (svn_fs__copy_t **copy_p,
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
  SVN_ERR (DB_WRAP (fs, "reading copy", db_err));

  /* Unparse COPY skel */
  skel = svn_fs__parse_skel (value.data, value.size, trail->pool);
  if (! skel)
    return svn_fs__err_corrupt_copy (fs, copy_id);

  /* Convert skel to native type. */
  SVN_ERR (svn_fs__parse_copy_skel (&copy, skel, trail->pool));
  *copy_p = copy;
  return SVN_NO_ERROR;
}



/* 
 * local variables:
 * eval: (load-file "../../tools/dev/svn-dev.el")
 * end:
 */
