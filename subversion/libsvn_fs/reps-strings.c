/* reps-strings.c : intepreting representations w.r.t. strings
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
#include "reps-strings.h"



/* Helper function.  Is representation a `fulltext' type?  */
static int
rep_is_fulltext (skel_t *rep)
{
  return svn_fs__matches_atom (rep->children->children, "fulltext");
}


const char *
svn_fs__string_key_from_rep (skel_t *rep, apr_pool_t *pool)
{
  if (rep_is_fulltext (rep))
    return apr_pstrndup (pool,
                         rep->children->next->data,
                         rep->children->next->len);
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

  strkey = svn_fs__string_key_from_rep (rep, trail->pool);
  SVN_ERR (svn_fs__string_size (&(str->len), fs, strkey, trail));
  data = apr_palloc (trail->pool, str->len);
  SVN_ERR (svn_fs__string_read (fs, strkey, data, 0, &(str->len), trail));
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
          string_key = svn_fs__string_key_from_rep (rep, trail->pool);
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


svn_error_t *
svn_fs__rep_read_range (svn_fs_t *fs,
                        skel_t *rep,
                        char *buf,
                        apr_size_t offset,
                        apr_size_t *len,
                        trail_t *trail)
{
  if (rep_is_fulltext (rep))
    {
      const char *string_key = apr_pstrndup (trail->pool,
                                             rep->children->next->data,
                                             rep->children->next->len);
      SVN_ERR (svn_fs__string_read (fs, string_key, buf, offset, len, trail));
    }
  else
    abort ();  /* We don't do undeltification yet. */

  return SVN_NO_ERROR;
}



/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */

