/*
 * copy.c:  wc 'copy' functionality.
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

/* ==================================================================== */



/*** Includes. ***/

#include <string.h>
#include "svn_wc.h"
#include "svn_string.h"
#include "svn_pools.h"
#include "svn_error.h"
#include "svn_path.h"

#include "wc.h"
#include "adm_files.h"
#include "props.h"


/*** Code. ***/

svn_error_t *
svn_wc__remove_wcprops (svn_wc_adm_access_t *adm_access, apr_pool_t *pool)
{
  apr_hash_t *entries;
  apr_hash_index_t *hi;
  const char *wcprop_path;
  apr_pool_t *subpool = svn_pool_create (pool);
  svn_error_t *err;

  /* Read PATH's entries. */
  SVN_ERR (svn_wc_entries_read (&entries, adm_access, FALSE, subpool));

  /* Remove this_dir's wcprops */
  SVN_ERR (svn_wc__wcprop_path (&wcprop_path,
                                svn_wc_adm_access_path (adm_access),
                                0, subpool));
  err = svn_io_remove_file (wcprop_path, subpool);
  if (err)
    svn_error_clear (err);

  /* Recursively loop over all children. */
  for (hi = apr_hash_first (subpool, entries); hi; hi = apr_hash_next (hi))
    {
      const void *key;
      void *val;
      const char *name;
      const svn_wc_entry_t *current_entry;
      const char *child_path;

      apr_hash_this (hi, &key, NULL, &val);
      name = key;
      current_entry = val;

      /* Ignore the "this dir" entry. */
      if (! strcmp (name, SVN_WC_ENTRY_THIS_DIR))
        continue;

      child_path = svn_path_join (svn_wc_adm_access_path (adm_access), name,
                                  subpool);

      /* If a file, remove it from wcprops. */
      if (current_entry->kind == svn_node_file)
        {
          SVN_ERR (svn_wc__wcprop_path (&wcprop_path, child_path, 0, subpool));
          err = svn_io_remove_file (wcprop_path, subpool);
          if (err)
            svn_error_clear (err);
          /* ignoring any error value from the removal; most likely,
             apr_file_remove will complain about trying to a remove a
             file that's not there.  But this more efficient than
             doing an independant stat for each file's existence
             before trying to remove it, no? */
        }        

      /* If a dir, recurse. */
      else if (current_entry->kind == svn_node_dir)
        {
          svn_wc_adm_access_t *child_access;
          SVN_ERR (svn_wc_adm_retrieve (&child_access, adm_access, child_path,
                                        subpool));
          SVN_ERR (svn_wc__remove_wcprops (child_access, subpool));
        }
    }

  /* Cleanup */
  svn_pool_destroy (subpool);

  return SVN_NO_ERROR;
}



/* This function effectively creates and schedules a file for
   addition, but does extra administrative things to allow it to
   function as a 'copy'.

   ASSUMPTIONS:

     - src_path points to a file under version control
     - dst_parent points to a dir under version control, in the same
                  working copy.
     - dst_basename will be the 'new' name of the copied file in dst_parent
 */
static svn_error_t *
copy_file_administratively (const char *src_path, 
                            svn_wc_adm_access_t *src_access,
                            svn_wc_adm_access_t *dst_parent,
                            const char *dst_basename,
                            svn_wc_notify_func_t notify_copied,
                            void *notify_baton,
                            apr_pool_t *pool)
{
  svn_node_kind_t dst_kind;
  const svn_wc_entry_t *src_entry, *dst_entry;

  /* The 'dst_path' is simply dst_parent/dst_basename */
  const char *dst_path
    = svn_path_join (svn_wc_adm_access_path (dst_parent), dst_basename, pool);

  /* Sanity check:  if dst file exists already, don't allow overwrite. */
  SVN_ERR (svn_io_check_path (dst_path, &dst_kind, pool));
  if (dst_kind != svn_node_none)
    return svn_error_createf (SVN_ERR_ENTRY_EXISTS, NULL,
                              "'%s' already exists and is in the way.",
                              dst_path);

  /* Even if DST_PATH doesn't exist it may still be a versioned file; it
     may be scheduled for deletion, or the user may simply have removed the
     working copy.  Since we are going to write to DST_PATH text-base and
     prop-base we need to detect such cases and abort. */
  SVN_ERR (svn_wc_entry (&dst_entry, dst_path, dst_parent, FALSE, pool));
  if (dst_entry && dst_entry->kind == svn_node_file)
    {
      if (dst_entry->schedule == svn_wc_schedule_delete)
        return svn_error_createf (SVN_ERR_ENTRY_EXISTS, NULL,
                                  "'%s' is scheduled for deletion, it must"
                                  " be commited before being overwritten",
                                  dst_path);
      else
        return svn_error_createf (SVN_ERR_ENTRY_EXISTS, NULL,
                                  "There is already a versioned item '%s'",
                                  dst_path);
    }

  /* Sanity check:  you cannot make a copy of something that's not
     in the repository.  See comment at the bottom of this file for an
     explanation. */
  SVN_ERR (svn_wc_entry (&src_entry, src_path, src_access, FALSE, pool));
  if (! src_entry)
    {
      return svn_error_createf 
        (SVN_ERR_UNVERSIONED_RESOURCE, NULL,
         "Cannot copy or move '%s' -- it's not under revision control",
         src_path);
    }
  else if ((src_entry->schedule == svn_wc_schedule_add)
           || (! src_entry->url)
           || (src_entry->copied))
    {
      return svn_error_createf 
        (SVN_ERR_UNSUPPORTED_FEATURE, NULL,
         "Cannot copy or move '%s' -- it's not in the repository yet,\n"
         "perhaps because it is a copy or is inside a copied tree.\n"
         "Try committing first.",
         src_path);
    }

  /* Now, make an actual copy of the working file. */
  SVN_ERR (svn_io_copy_file (src_path, dst_path, TRUE, pool));

  /* Copy the pristine text-base over.  Why?  Because it's the *only*
     way we can detect any upcoming local mods on the copy.

     In other words, we're talking about the scenario where somebody
     makes local mods to 'foo.c', then does an 'svn cp foo.c bar.c'.
     In this case, bar.c should still be locally modified too.
     
     Why do we want the copy to have local mods?  Even though the user
     will only see an 'A' instead of an 'M', local mods means that the
     client doesn't have to send anything but a small delta during
     commit; the server can make efficient use of the copyfrom args. 

     As long as we're copying the text-base over, we should copy the
     working and pristine propfiles over too. */
  {
    svn_node_kind_t kind;
    const char *src_wprop, *src_bprop, *dst_wprop, *dst_bprop;

    /* Discover the paths to the two text-base files */
    const char *src_txtb = svn_wc__text_base_path (src_path, FALSE, pool);
    const char *dst_txtb = svn_wc__text_base_path (dst_path, FALSE, pool);

    /* Discover the paths to the four prop files */
    SVN_ERR (svn_wc__prop_path (&src_wprop, src_path, 0, pool));
    SVN_ERR (svn_wc__prop_base_path (&src_bprop, src_path, 0, pool));
    SVN_ERR (svn_wc__prop_path (&dst_wprop, dst_path, 0, pool));
    SVN_ERR (svn_wc__prop_base_path (&dst_bprop, dst_path, 0, pool));

    /* Copy the text-base over unconditionally. */
    SVN_ERR (svn_io_copy_file (src_txtb, dst_txtb, TRUE, pool));

    /* Copy the props over if they exist. */
    SVN_ERR (svn_io_check_path (src_wprop, &kind, pool));
    if (kind == svn_node_file)
      SVN_ERR (svn_io_copy_file (src_wprop, dst_wprop, TRUE, pool));
      
    /* Copy the base-props over if they exist */
    SVN_ERR (svn_io_check_path (src_bprop, &kind, pool));
    if (kind == svn_node_file)
      SVN_ERR (svn_io_copy_file (src_bprop, dst_bprop, TRUE, pool));
  }

  /* Schedule the new file for addition in its parent, WITH HISTORY. */
  {
    char *copyfrom_url;
    svn_revnum_t copyfrom_rev;

    SVN_ERR (svn_wc_get_ancestry (&copyfrom_url, &copyfrom_rev,
                                  src_path, src_access, pool));
    
    SVN_ERR (svn_wc_add (dst_path, dst_parent,
                         copyfrom_url, copyfrom_rev,
                         notify_copied, notify_baton, pool));
  }

  return SVN_NO_ERROR;
}


/* This function effectively creates and schedules a dir for
   addition, but does extra administrative things to allow it to
   function as a 'copy'.

   ASSUMPTIONS:

     - src_path points to a dir under version control
     - dst_parent points to a dir under version control, in the same
                  working copy.
     - dst_basename will be the 'new' name of the copied dir in dst_parent
 */
static svn_error_t *
copy_dir_administratively (const char *src_path, 
                           svn_wc_adm_access_t *src_access,
                           svn_wc_adm_access_t *dst_parent,
                           const char *dst_basename,
                           svn_wc_notify_func_t notify_copied,
                           void *notify_baton,
                           apr_pool_t *pool)
{
  const svn_wc_entry_t *src_entry;
  svn_wc_adm_access_t *adm_access;

  /* The 'dst_path' is simply dst_parent/dst_basename */
  const char *dst_path = svn_path_join (svn_wc_adm_access_path (dst_parent),
                                        dst_basename, pool);

  /* Sanity check:  you cannot make a copy of something that's not
     in the repository.  See comment at the bottom of this file for an
     explanation. */
  SVN_ERR (svn_wc_entry (&src_entry, src_path, src_access, FALSE, pool));
  if ((src_entry->schedule == svn_wc_schedule_add)
      || (! src_entry->url))
    return svn_error_createf 
      (SVN_ERR_UNSUPPORTED_FEATURE, NULL,
       "Not allowed to copy or move '%s' -- it's not in the repository yet.\n"
       "Try committing first.",
       src_path);

  /* Recursively copy the whole directory over.  This gets us all
     text-base, props, base-props, as well as entries, local mods,
     schedulings, existences, etc.

      ### Should we be copying unversioned items within the directory? */
  SVN_ERR (svn_io_copy_dir_recursively (src_path,
                                        svn_wc_adm_access_path (dst_parent),
                                        dst_basename,
                                        TRUE, pool));

  /* If this is part of a move, the copied directory will be locked,
     because the source directory was locked.  Running cleanup will remove
     the locks, even though this directory is not yet been added to the
     parent. */
  SVN_ERR (svn_wc_cleanup (dst_path, NULL, pool));

  /* Remove all wcprops in the directory, because they're all bogus now.
     After the commit, ra_dav should regenerate them and re-store them as
     an optimization.  Note we use the normal locking mechanism here, even
     though this directory has not yet been added to the parent. */
  SVN_ERR (svn_wc_adm_open (&adm_access, NULL, dst_path, TRUE, TRUE, pool));
  SVN_ERR (svn_wc__remove_wcprops (adm_access, pool));
  SVN_ERR (svn_wc_adm_close (adm_access));

  /* Schedule the directory for addition in both its parent and itself
     (this_dir) -- WITH HISTORY.  This function should leave the
     existing administrative dir untouched.  */
  {
    char *copyfrom_url;
    svn_revnum_t copyfrom_rev;
    
    SVN_ERR (svn_wc_get_ancestry (&copyfrom_url, &copyfrom_rev,
                                  src_path, src_access, pool));
    
    SVN_ERR (svn_wc_add (dst_path, dst_parent,
                         copyfrom_url, copyfrom_rev,
                         notify_copied, notify_baton, pool));
  }
 
  return SVN_NO_ERROR;
}



/* Public Interface */

svn_error_t *
svn_wc_copy (const char *src_path,
             svn_wc_adm_access_t *dst_parent,
             const char *dst_basename,
             svn_wc_notify_func_t notify_func,
             void *notify_baton,
             apr_pool_t *pool)
{
  svn_wc_adm_access_t *adm_access;
  svn_node_kind_t src_kind;

  SVN_ERR (svn_wc_adm_probe_open (&adm_access, NULL, src_path, FALSE, TRUE,
                                  pool));

  SVN_ERR (svn_io_check_path (src_path, &src_kind, pool));
  
  if (src_kind == svn_node_file)
    SVN_ERR (copy_file_administratively (src_path, adm_access,
                                         dst_parent, dst_basename,
                                         notify_func, notify_baton, pool));

  else if (src_kind == svn_node_dir)
    SVN_ERR (copy_dir_administratively (src_path, adm_access,
                                        dst_parent, dst_basename,
                                        notify_func, notify_baton, pool));

  SVN_ERR (svn_wc_adm_close (adm_access));


  return SVN_NO_ERROR;
}



/*
  Rabbinic Commentary


  Q:  Why can't we 'svn cp' something that we just copied?
      i.e.  'svn cp foo foo2;  svn cp foo2 foo3"

  A:  It leads to inconsistencies.

      In the example above, foo2 has no associated repository URL,
      because it hasn't been committed yet.  But suppose foo3 simply
      inherited foo's URL (i.e. foo3 'pointed' to foo as a copy
      ancestor by virtue of transitivity.)
 
      For one, this is not what the user would expect.  That's
      certainly not what the user typed!  Second, suppose that the
      user did a commit between the two 'svn cp' commands.  Now foo3
      really *would* point to foo2, but without that commit, it
      pointed to foo.  Ugly inconsistency, and the user has no idea
      that foo3's ancestor would be different in each case.

      And even if somehow we *could* make foo3 point to foo2 before
      foo2 existed in the repository... what's to prevent a user from
      committing foo3 first?  That would break.

*/
