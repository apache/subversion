/* reps-table.c : operations on the `representations' table
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
#include "reps-table.h"
#include "strings-table.h"
#include "key-gen.h"



/*** Creating and opening the representations table. ***/

int
svn_fs__open_reps_table (DB **reps_p,
                         DB_ENV *env,
                         int create)
{
  DB *reps;

  DB_ERR (db_create (&reps, env, 0));
  DB_ERR (reps->open (reps, "representations", 0, DB_BTREE,
                      create ? (DB_CREATE | DB_EXCL) : 0,
                      0666));

  /* Create the `next-key' table entry.  */
  if (create)
  {
    DBT key, value;

    DB_ERR (reps->put
            (reps, 0,
             svn_fs__str_to_dbt (&key, (char *) svn_fs__next_key_key),
             svn_fs__str_to_dbt (&value, (char *) "0"),
             0));
  }

  *reps_p = reps;
  return 0;
}



/*** Storing and retrieving reps.  ***/

svn_error_t *
svn_fs__read_rep (skel_t **skel_p,
                  svn_fs_t *fs,
                  const char *key,
                  trail_t *trail)
{
  skel_t *skel;
  int db_err;
  DBT query, result;

  db_err = fs->representations->get
    (fs->representations,
     trail->db_txn,
     svn_fs__str_to_dbt (&query, (char *) key),
     svn_fs__result_dbt (&result), 0);

  svn_fs__track_dbt (&result, trail->pool);

  /* If there's no such node, return an appropriately specific error.  */
  if (db_err == DB_NOTFOUND)
    return svn_error_createf
      (SVN_ERR_FS_NO_SUCH_REPRESENTATION, 0, 0, fs->pool,
       "svn_fs__read_rep: no such representation `%s'", key);

  /* Handle any other error conditions.  */
  SVN_ERR (DB_WRAP (fs, "reading representation", db_err));

  /* Parse the REPRESENTATION skel.  */
  skel = svn_fs__parse_skel (result.data, result.size, trail->pool);
  *skel_p = skel;

  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs__write_rep (svn_fs_t *fs,
                   const char *key,
                   skel_t *skel,
                   trail_t *trail)
{
  DBT query, result;

  SVN_ERR (DB_WRAP (fs, "storing representation",
                    fs->representations->put
                    (fs->representations, trail->db_txn,
                     svn_fs__str_to_dbt (&query, (char *) key),
                     svn_fs__skel_to_dbt (&result, skel, trail->pool), 0)));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs__write_new_rep (const char **key,
                       svn_fs_t *fs,
                       skel_t *skel,
                       trail_t *trail)
{
  DBT query, result;
  int db_err;
  apr_size_t len;
  char next_key[200];    /* This will be a problem if the number of
                            representations in a filesystem ever
                            exceeds 1821797716821872825139468712408937
                            126733897152817476066745969754933395997209
                            053270030282678007662838673314795994559163
                            674524215744560596468010549540621501770423
                            499988699078859474399479617124840673097380
                            736524850563115569208508785942830080999927
                            310762507339484047393505519345657439796788
                            24151197232629947748581376.  Somebody warn
                            my grandchildren. */
  
  /* Get the current value associated with `next-key'.  */
  svn_fs__str_to_dbt (&query, (char *) svn_fs__next_key_key);
  SVN_ERR (DB_WRAP (fs, "allocating new representation (getting next-key)",
                    fs->representations->get (fs->representations,
                                              trail->db_txn,
                                              &query,
                                              svn_fs__result_dbt (&result),
                                              0)));

  svn_fs__track_dbt (&result, trail->pool);

  /* Store the new rep skel. */
  *key = apr_pstrndup (trail->pool, result.data, result.size);
  SVN_ERR (svn_fs__write_rep (fs, *key, skel, trail));

  /* Bump to future key. */
  len = result.size;
  svn_fs__next_key (result.data, &len, next_key);
  db_err = fs->representations->put
    (fs->representations, trail->db_txn,
     svn_fs__str_to_dbt (&query, (char *) svn_fs__next_key_key),
     svn_fs__str_to_dbt (&result, (char *) next_key),
     0);

  SVN_ERR (DB_WRAP (fs, "bumping next representation key", db_err));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs__delete_rep (svn_fs_t *fs, const char *key, trail_t *trail)
{
  int db_err;
  DBT query;

  db_err = fs->representations->del
    (fs->representations, trail->db_txn,
     svn_fs__str_to_dbt (&query, (char *) key), 0);

  /* If there's no such node, return an appropriately specific error.  */
  if (db_err == DB_NOTFOUND)
    return svn_error_createf
      (SVN_ERR_FS_NO_SUCH_REPRESENTATION, 0, 0, fs->pool,
       "svn_fs__delete_rep: no such representation `%s'", key);

  /* Handle any other error conditions.  */
  SVN_ERR (DB_WRAP (fs, "deleting representation", db_err));

  return SVN_NO_ERROR;
}



/* Helper function.  Is representation a `fulltext' type?  */
static int
rep_is_fulltext (skel_t *rep)
{
  return svn_fs__matches_atom (rep->children->children, "fulltext");
}


const char *
svn_fs__string_key_from_rep (skel_t *rep)
{
  if (rep_is_fulltext (rep))
    return (const char *) rep->children->next->data;
  else
    abort ();   /* ### we only know about fulltext right now */

  return NULL;
}


svn_error_t *
svn_fs__string_from_rep (svn_string_t *str,
                         svn_fs_t *fs,
                         skel_t *rep,
                         trail_t *trail)
{
  const char *strkey;
  char *data;

  strkey = svn_fs__string_key_from_rep (rep);
  SVN_ERR (svn_fs__string_size (&(str->len), fs, strkey, trail));
  data = apr_palloc (trail->pool, str->len);
  SVN_ERR (svn_fs__string_read (fs, strkey, 0, &(str->len), data, trail));
  str->data = data;
  return SVN_NO_ERROR;
}


int
svn_fs__rep_is_mutable (skel_t *rep)
{
  /* The node "header" is the first element of a rep skel. */
  skel_t *header = rep->children;
  
  /* The 2nd element of the header, IF it exists, is the header's
     first `flag'.  It could be NULL.  */
  skel_t *flag = header->children->next;
  
  while (flag)
    {
      if (svn_fs__matches_atom (flag, "mutable"))
        return TRUE;

      flag = flag->next;
    }
  
  /* Reached the end of the header skel, no mutable flag was found. */
  return FALSE;
}


/* Add the "mutable" flag to representation REP.  Allocate the flag in
   POOL; it is advisable that POOL be at least as long-lived as the
   pool REP is allocated in.  If the mutability flag is already set,
   this function does nothing.  */
static void
rep_set_mutable_flag (skel_t *rep, apr_pool_t *pool)
{
  if (! svn_fs__rep_is_mutable (rep))
    svn_fs__append (svn_fs__str_atom ("mutable", pool), rep->children);
    
  return;
}


svn_error_t *
svn_fs__get_mutable_rep (const char **new_key,
                         const char *key,
                         svn_fs_t *fs, 
                         trail_t *trail)
{
  skel_t *rep;

  /* Read the rep associated with KEY */
  SVN_ERR (svn_fs__read_rep (&rep, fs, key, trail));

  /* If REP is not mutable, we have to make a copy of it that is.
     This means making a deep copy of the string to which it refers
     as well! */
  if (! svn_fs__rep_is_mutable (rep))
    {
      if (rep_is_fulltext (rep))
        {
          const char *string_key, *new_string_key;

          /* Step 1:  Copy the string to which the rep refers. */
          string_key = svn_fs__string_key_from_rep (rep);
          SVN_ERR (svn_fs__string_copy (fs, &new_string_key,
                                        string_key, trail));
          
          /* Step 2:  Make this rep mutable. */
          rep_set_mutable_flag (rep, trail->pool);
                   
          /* Step 3:  Change the string key to which this rep points. */
          rep->children->next->data = new_string_key;
          rep->children->next->len = strlen (new_string_key);

          /* Step 4: Write the mutable version of this rep to the
             database, returning the newly created key to the
             caller. */
          SVN_ERR (svn_fs__write_new_rep (new_key, fs, rep, trail));
        }
      else
        abort (); /* Huh?  We only know about fulltext right now. */
    }
  else
    *new_key = key;

  return SVN_NO_ERROR;
}



/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */

