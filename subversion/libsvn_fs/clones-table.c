/* clones-table.c : operations on the `clones' table
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


#include "clones-table.h"
#include "fs.h"
#include "err.h"
#include "dbt.h"
#include "skel.h"


int
svn_fs__open_clones_table (DB **clones_p,
                           DB_ENV *env,
                           int create)
{
  DB *clones;

  DB_ERR (db_create (&clones, env, 0));
  DB_ERR (clones->open (clones, "clones", 0, DB_BTREE,
                        create ? (DB_CREATE | DB_EXCL) : 0,
                        0666));

  *clones_p = clones;
  return 0;
}


static char *
make_clones_key (const char *svn_txn,
                 const char *base_path,
                 apr_pool_t *pool)
{
  return apr_pstrcat (pool, svn_txn, " ", base_path, 0);
}


static int
is_valid_clone (skel_t *skel)
{
  int len = svn_fs__list_length (skel);

  if (len >= 1)
    {
      if (svn_fs__is_atom (skel->children, "cloned")
          && len == 2
          && skel->children->next->is_atom)
        return 1;
      else if (svn_fs__is_atom (skel->children, "moved")
               && len == 3
               && skel->children->next->is_atom
               && skel->children->next->next->is_atom)
        return 1;
    }

  return 0;
}


svn_error_t *
svn_fs__check_clone (skel_t **clone_p,
                     svn_fs_t *fs,
                     const char *svn_txn,
                     const char *base_path,
                     trail_t *trail)
{
  DBT key, value;
  int db_err;
  skel_t *clone;

  /* Assemble the table key from the transaction ID and the base path.  */
  char *key_str = make_clones_key (svn_txn, base_path, trail->pool);

  /* Try to find an entry for that in the database.  */
  db_err = fs->clones->get (fs->clones, trail->db_txn,
                            svn_fs__str_to_dbt (&key, key_str),
                            svn_fs__result_dbt (&value),
                            0);

  /* If there's no such entry, the node hasn't been cloned.  */
  if (db_err == DB_NOTFOUND)
    {
      *clone_p = 0;
      return 0;
    }

  /* Handle any other error conditions.  */
  SVN_ERR (DB_WRAP (fs, "reading `clones' entry", db_err));

  /* Make sure the skel's contents get freed when TRAIL->pool is destroyed.  */
  svn_fs__track_dbt (&value, trail->pool);

  /* Parse and check the CLONE skel.  */
  clone = svn_fs__parse_skel (value.data, value.size, trail->pool);
  if (! clone
      || ! is_valid_clone (clone))
    return svn_fs__err_corrupt_clone (fs, svn_txn, base_path);

  *clone_p = clone;
  return 0;
}


int
svn_fs__is_cloned (skel_t **clone_id_p,
                   skel_t *clone)
{
  if (svn_fs__is_atom (clone->children, "cloned"))
    {
      *clone_id_p = clone->children->next;
      return 1;
    }

  return 0;
}


int
svn_fs__is_renamed (skel_t **parent_clone_id_p,
                    skel_t **entry_name_p,
                    skel_t *clone)
{
  if (svn_fs__is_atom (clone->children, "moved"))
    {
      *parent_clone_id_p = clone->children->next;
      *entry_name_p = clone->children->next->next;
      return 1;
    }

  return 0;
}


svn_error_t *
svn_fs__record_clone (svn_fs_t *fs,
                      const char *svn_txn,
                      const char *base_path,
                      const svn_fs_id_t *clone_id,
                      trail_t *trail)
{
  apr_pool_t *pool = trail->pool;
  char *key_str = make_clones_key (svn_txn, base_path, pool);
  svn_string_t *clone_id_string = svn_fs_unparse_id (clone_id, pool);
  skel_t *clone;
  DBT key, value;

  /* The logic here is wrong.  A `cloned' entry can override a `moved'
     entry, but not another `cloned' entry.  */
  abort ();

  /* Assemble the CLONE skel.  */
  clone = svn_fs__make_empty_list (pool);
  svn_fs__prepend (svn_fs__mem_atom (clone_id_string->data,
                                     clone_id_string->len,
                                     pool),
                   clone);
  svn_fs__prepend (svn_fs__str_atom ((char *) "cloned", pool), clone);

  SVN_ERR (DB_WRAP (fs, "recording clone creation",
                    fs->clones->put (fs->clones, trail->db_txn,
                                     svn_fs__str_to_dbt (&key, key_str),
                                     svn_fs__skel_to_dbt (&value, clone, pool),
                                     0)));


  return 0;
}


svn_error_t *
svn_fs__record_rename (svn_fs_t *fs,
                       const char *svn_txn,
                       const char *base_path,
                       const svn_fs_id_t *parent_id,
                       const char *entry_name,
                       trail_t *trail)
{
  apr_pool_t *pool = trail->pool;
  char *key_str = make_clones_key (svn_txn, base_path, pool);
  svn_string_t *parent_id_string = svn_fs_unparse_id (parent_id, pool);
  skel_t *clone;
  DBT key, value;

  /* Assemble the CLONE skel.  */
  clone = svn_fs__make_empty_list (pool);
  svn_fs__prepend (svn_fs__str_atom (apr_pstrdup (pool, entry_name), pool),
                   clone);
  svn_fs__prepend (svn_fs__mem_atom (parent_id_string->data,
                                     parent_id_string->len,
                                     pool),
                   clone);
  svn_fs__prepend (svn_fs__str_atom ((char *) "moved", pool), clone);

  SVN_ERR (DB_WRAP (fs, "recording clone reparenting",
                    fs->clones->put (fs->clones, trail->db_txn,
                                     svn_fs__str_to_dbt (&key, key_str),
                                     svn_fs__skel_to_dbt (&value, clone, pool),
                                     0)));

  /* The logic here is wrong.  A `moved' entry can override another
     `moved' entry, but not a `cloned' entry.  */
  abort ();

  return 0;
}



/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
