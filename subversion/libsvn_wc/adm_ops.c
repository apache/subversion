/*
 * adm_ops.c: routines for affecting working copy administrative
 *            information.  NOTE: this code doesn't know where the adm
 *            info is actually stored.  Instead, generic handles to
 *            adm data are requested via a reference to some PATH
 *            (PATH being a regular, non-administrative directory or
 *            file in the working copy).
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



#include <apr_pools.h>
#include <apr_hash.h>
#include <apr_file_io.h>
#include <apr_time.h>
#include "svn_types.h"
#include "svn_string.h"
#include "svn_error.h"
#include "svn_hash.h"
#include "svn_path.h"
#include "svn_wc.h"
#include "wc.h"



/*** adm area guarantees ***/

/* Make sure that PATH (a directory) contains a complete adm area,
 * based at REPOSITORY.
 *
 * Creates the adm area if none, in which case PATH starts out at
 * revision 0.
 *
 * Note: The adm area's lock-state is not changed by this function,
 * and if the adm area is created, it is left in an unlocked state.
 */
svn_error_t *
svn_wc__ensure_wc (svn_string_t *path,
                   svn_string_t *ancestor_path,
                   svn_revnum_t ancestor_revision,
                   apr_pool_t *pool)
{
  svn_error_t *err;

  err = svn_wc__ensure_adm (path,
                            ancestor_path,
                            ancestor_revision,
                            pool);
  if (err)
    return err;

  return SVN_NO_ERROR;
}



/*** Closing commits. ***/


svn_error_t *
svn_wc__ensure_uniform_revision (svn_string_t *dir_path,
                                 svn_revnum_t revision,
                                 apr_pool_t *pool)
{
  apr_hash_t *entries;
  apr_hash_index_t *hi;
  apr_pool_t *subpool = svn_pool_create (pool);

  struct svn_wc_close_commit_baton *cbaton =
    apr_pcalloc (subpool, sizeof (*cbaton));
  cbaton->pool = subpool;
  cbaton->prefix_path = svn_string_create ("", subpool);

  SVN_ERR (svn_wc_entries_read (&entries, dir_path, subpool));

  /* Loop over this directory's entries: */
  for (hi = apr_hash_first (entries); hi; hi = apr_hash_next (hi))
    {
      const void *key;
      const char *keystring;
      apr_size_t klen;
      void *val;
      svn_string_t *current_entry_name;
      svn_wc_entry_t *current_entry; 
      svn_string_t *full_entry_path;

      /* Get the next entry */
      apr_hash_this (hi, &key, &klen, &val);
      keystring = (const char *) key;
      current_entry = (svn_wc_entry_t *) val;

      /* Compute the name of the entry */
      if (! strcmp (keystring, SVN_WC_ENTRY_THIS_DIR))
        current_entry_name = NULL;
      else
        current_entry_name = svn_string_create (keystring, subpool);

      /* Compute the complete path of the entry */
      full_entry_path = svn_string_dup (dir_path, subpool);
      if (current_entry_name)
        svn_path_add_component (full_entry_path, current_entry_name,
                                svn_path_url_style);

      /* If the entry is a file or SVN_WC_ENTRY_THIS_DIR, and it has a
         different rev than REVISION, fix it. */
      if (((current_entry->kind == svn_node_file) || (! current_entry_name))
          && (current_entry->revision != revision))
        SVN_ERR (svn_wc_set_revision (cbaton, full_entry_path, revision));
      
      /* If entry is a dir (and not `.'), recurse. */
      if ((current_entry->kind == svn_node_dir) && current_entry_name)
        SVN_ERR (svn_wc__ensure_uniform_revision (full_entry_path,
                                                  revision, subpool));
    }

  /* We're done examining this dir's entries, so free them. */
  svn_pool_destroy (subpool);

  return SVN_NO_ERROR;
}



svn_error_t *
svn_wc_set_revision (void *baton,
                     svn_string_t *target,
                     svn_revnum_t new_revnum)
{
  svn_error_t *err;
  apr_status_t apr_err;
  svn_string_t *log_parent, *logtag, *basename;
  apr_file_t *log_fp = NULL;
  struct svn_wc_close_commit_baton *bumper =
    (struct svn_wc_close_commit_baton *) baton;
  apr_pool_t *pool = bumper->pool;  /* cute, eh? */
  char *revstr = apr_psprintf (pool, "%ld", new_revnum);

  /* Construct the -full- path */
  svn_string_t *path = svn_string_dup (bumper->prefix_path, pool);
  svn_path_add_component (path, target, svn_path_local_style);

  /* Write a log file in the adm dir of path. */

  /* (First, try to write a logfile directly in PATH.) */
  log_parent = path;
  basename = svn_string_create (SVN_WC_ENTRY_THIS_DIR, pool);
  err = svn_wc__open_adm_file (&log_fp, log_parent, SVN_WC__ADM_LOG,
                               (APR_WRITE | APR_APPEND | APR_CREATE),
                               pool);
  if (err)
    {
      /* (Ah, PATH must be a file.  So create a logfile in its
         parent instead.) */      
      svn_path_split (path, &log_parent, &basename,
                      svn_path_local_style, pool);
      if (svn_path_is_empty (log_parent, svn_path_local_style))
        svn_string_set (log_parent, ".");

      SVN_ERR (svn_wc__open_adm_file (&log_fp, log_parent, SVN_WC__ADM_LOG,
                                      (APR_WRITE|APR_APPEND|APR_CREATE),
                                      pool));
    }
  else
    {
      /* PATH must be a dir */
      svn_string_t *pdir, *bname;

      if (svn_path_is_empty (log_parent, svn_path_local_style))
        {
          /* We have an empty path.  Since there is no way to examine
             the parent of an empty path, we ensure that the parent
             directory is '.', and that we are looking at the "this
             dir" entry. */
          pdir = svn_string_create (".", pool);
        }
      else
        {
          /* We were given a directory, so we look at that dir's "this
             dir" entry. */
          pdir = log_parent;
        }

      bname = svn_string_create (SVN_WC_ENTRY_THIS_DIR, pool);

      SVN_ERR (svn_wc__entry_modify
               (pdir, 
                bname, 
                (SVN_WC__ENTRY_MODIFY_REVISION 
                 | SVN_WC__ENTRY_MODIFY_SCHEDULE), 
                new_revnum,
                svn_node_none, 
                svn_wc_schedule_unadd,
                svn_wc_existence_normal,
                FALSE,
                0, 
                0, 
                NULL, 
                pool, 
                NULL));
    }

  /* Regardless of whether it's a file or dir, the "main" logfile
     contains a command to bump the revision attribute (and
     timestamp.)  */
  logtag = svn_string_create ("", pool);
  svn_xml_make_open_tag (&logtag, pool, svn_xml_self_closing,
                         SVN_WC__LOG_COMMITTED,
                         SVN_WC__LOG_ATTR_NAME, basename,
                         SVN_WC__LOG_ATTR_REVISION, 
                         svn_string_create (revstr, pool),
                         NULL);
      
  apr_err = apr_file_write_full (log_fp, logtag->data, logtag->len, NULL);
  if (apr_err)
    {
      apr_file_close (log_fp);
      return svn_error_createf (apr_err, 0, NULL, pool,
                                "svn_wc_set_revision: "
                                "error writing %s's log file", 
                                path->data);
    }
      
  SVN_ERR (svn_wc__close_adm_file (log_fp, log_parent, SVN_WC__ADM_LOG,
                                   TRUE, /* sync */
                                   pool));


  /* Run the log file we just created. */
  SVN_ERR (svn_wc__run_log (log_parent, pool));
            
  /* The client's commit routine will take care of removing all
     locks en masse. */

  return SVN_NO_ERROR;
}



svn_error_t *svn_wc_get_wc_prop (void *baton,
                                 svn_string_t *target,
                                 svn_string_t *name,
                                 svn_string_t **value)
{
  struct svn_wc_close_commit_baton *ccb =
    (struct svn_wc_close_commit_baton *) baton;

  /* Prepend the baton's prefix to the target. */
  svn_string_t *path = svn_string_dup (ccb->prefix_path, ccb->pool);
  svn_path_add_component (path, target, svn_path_local_style);

  /* And use our public interface to get the property value. */
  SVN_ERR (svn_wc__wcprop_get (value, name, path, ccb->pool));

  return SVN_NO_ERROR;
}


svn_error_t *svn_wc_set_wc_prop (void *baton,
                                 svn_string_t *target,
                                 svn_string_t *name,
                                 svn_string_t *value)
{
  struct svn_wc_close_commit_baton *ccb =
    (struct svn_wc_close_commit_baton *) baton;

  /* Prepend the baton's prefix to the target. */
  svn_string_t *path = svn_string_dup (ccb->prefix_path, ccb->pool);
  svn_path_add_component (path, target, svn_path_local_style);

  /* And use our public interface to get the property value. */
  SVN_ERR (svn_wc__wcprop_set (name, value, path, ccb->pool));

  return SVN_NO_ERROR;
}







/* kff todo: not all of these really belong in wc_adm.  Some may get
   broken out into other files later.  They're just here to satisfy
   the public header file that they exist. */

svn_error_t *
svn_wc_rename (svn_string_t *src, svn_string_t *dst, apr_pool_t *pool)
{
  /* kff todo */
  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc_copy (svn_string_t *src, svn_string_t *dst, apr_pool_t *pool)
{
  /* kff todo */
  return SVN_NO_ERROR;
}


enum mark_tree_state {
  mark_tree_state_delete = 1,
  mark_tree_state_unadd,
  mark_tree_state_undelete
};


/* Recursively mark a tree DIR for some STATE. */
static svn_error_t *
mark_tree (svn_string_t *dir, enum mark_tree_state state, apr_pool_t *pool)
{
  apr_pool_t *subpool = svn_pool_create (pool);
  apr_hash_t *entries;
  apr_hash_index_t *hi;
  svn_string_t *fullpath = svn_string_dup (dir, pool);

  /* Read the entries file for this directory. */
  SVN_ERR (svn_wc_entries_read (&entries, dir, pool));

  /* Mark each entry in the entries file. */
  for (hi = apr_hash_first (entries); hi; hi = apr_hash_next (hi))
    {
      const void *key;
      apr_size_t klen;
      void *val;
      svn_string_t *basename;
      svn_wc_entry_t *entry; 

      /* Get the next entry */
      apr_hash_this (hi, &key, &klen, &val);
      entry = (svn_wc_entry_t *) val;
      basename = svn_string_create ((const char *) key, subpool);

      if (entry->kind == svn_node_dir)
        {
          if (strcmp (basename->data, SVN_WC_ENTRY_THIS_DIR) != 0)
            {
              svn_path_add_component (fullpath, basename, 
                                      svn_path_local_style);
              SVN_ERR (mark_tree (fullpath, state, subpool));
            }
        }

      /* Mark this entry. */
      switch (state)
        {
        case mark_tree_state_delete:
          SVN_ERR (svn_wc__entry_modify
                   (dir, basename, 
                    SVN_WC__ENTRY_MODIFY_SCHEDULE,
                    SVN_INVALID_REVNUM, entry->kind,
                    svn_wc_schedule_delete,
                    svn_wc_existence_normal,
                    FALSE, 0, 0, NULL, pool, NULL));
          break;

        case mark_tree_state_unadd:
          SVN_ERR (svn_wc__entry_modify
                   (dir, basename, 
                    SVN_WC__ENTRY_MODIFY_SCHEDULE,
                    SVN_INVALID_REVNUM, entry->kind,
                    svn_wc_schedule_unadd,
                    svn_wc_existence_normal,
                    FALSE, 0, 0, NULL, pool, NULL));
          break;

        case mark_tree_state_undelete: 
          SVN_ERR (svn_wc__entry_modify
                   (dir, basename, 
                    SVN_WC__ENTRY_MODIFY_SCHEDULE,
                    SVN_INVALID_REVNUM, entry->kind,
                    svn_wc_schedule_undelete,
                    svn_wc_existence_normal,
                    FALSE, 0, 0, NULL, pool, NULL));
          break;
        }

      /* Reset FULLPATH to just hold this dir's name. */
      svn_string_set (fullpath, dir->data);

      /* Clear our per-iteration pool. */
      svn_pool_clear (subpool);
    }

  /* Destroy our per-iteration pool. */
  svn_pool_destroy (subpool);
  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc_delete (svn_string_t *path, apr_pool_t *pool)
{
  svn_string_t *dir, *basename;
  svn_wc_entry_t *entry;

  /* Get the entry for the path we are deleting. */
  SVN_ERR (svn_wc_entry (&entry, path, pool));
  if (entry->kind == svn_node_dir)
    {
      /* Recursively mark a whole tree for deletion. */
      SVN_ERR (mark_tree (path, mark_tree_state_delete, pool));
    }

  /* We need to mark this entry for deletion in its parent's entries
     file, so we split off basename from the parent path, then fold in
     the addition of a delete flag. */
  svn_path_split (path, &dir, &basename, svn_path_local_style, pool);
  if (svn_path_is_empty (dir, svn_path_local_style))
    svn_string_set (dir, ".");
  
  SVN_ERR (svn_wc__entry_modify
           (dir, basename, 
            SVN_WC__ENTRY_MODIFY_SCHEDULE,
            SVN_INVALID_REVNUM, entry->kind,
            svn_wc_schedule_delete,
            svn_wc_existence_normal,
            FALSE, 0, 0, NULL, pool, NULL));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc_add_directory (svn_string_t *dir, apr_pool_t *pool)
{
  svn_string_t *parent_dir, *basename;
  svn_wc_entry_t *orig_entry, *entry;
  svn_string_t *ancestor_path;

  /* Get the original entry for this directory if one exists (perhaps
     this is actually a replacement of a previously deleted thing). */
  if (svn_wc_entry (&orig_entry, dir, pool))
    orig_entry = NULL;

  /* You can only add something that is a) not in revision control, or
     b) slated for deletion from revision control. */
  if (orig_entry && orig_entry->schedule != svn_wc_schedule_delete)
    return svn_error_createf 
      (SVN_ERR_WC_ENTRY_EXISTS, 0, NULL, pool,
       "Directory '%s' has already been added to revision control",
       dir->data);

  /* Get the entry for this directory's parent.  We need to snatch the
     ancestor path out of there. */
  svn_path_split (dir, &parent_dir, &basename, svn_path_local_style, pool);
  if (svn_path_is_empty (parent_dir, svn_path_local_style))
    parent_dir = svn_string_create (".", pool);
  SVN_ERR (svn_wc_entry (&entry, parent_dir, pool));
  
  /* Derive the ancestor path for our new addition here. */
  ancestor_path = svn_string_dup (entry->ancestor, pool);
  svn_path_add_component (ancestor_path, basename, svn_path_repos_style);
  
  /* Make sure this new directory has an admistrative subdirectory
     created inside of it */
  SVN_ERR (svn_wc__ensure_adm (dir, ancestor_path, 0, pool));

  /* Now, add the entry for this directory to the parent_dir's entries
     file, marking it for addition. */
  SVN_ERR (svn_wc__entry_modify
           (parent_dir, basename, 
            (SVN_WC__ENTRY_MODIFY_SCHEDULE
             | SVN_WC__ENTRY_MODIFY_REVISION
             | SVN_WC__ENTRY_MODIFY_KIND),
            0, svn_node_dir,
            svn_wc_schedule_add,
            svn_wc_existence_normal,
            FALSE, 0, 0, NULL, pool, NULL));

  /* And finally, make sure this entry is marked for addition in its
     own administrative directory. */
  SVN_ERR (svn_wc__entry_modify
           (dir, NULL,
            (SVN_WC__ENTRY_MODIFY_SCHEDULE
             | SVN_WC__ENTRY_MODIFY_REVISION
             | SVN_WC__ENTRY_MODIFY_KIND
             | SVN_WC__ENTRY_MODIFY_FORCE),
            0, svn_node_dir,
            svn_wc_schedule_add,
            svn_wc_existence_normal,
            FALSE, 0, 0, NULL, pool, NULL));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc_add_file (svn_string_t *file, apr_pool_t *pool)
{
  svn_string_t *dir, *basename;
  svn_wc_entry_t *orig_entry;

  /* Get the original entry for this directory if one exists (perhaps
     this is actually a replacement of a previously deleted thing). */
  SVN_ERR (svn_wc_entry (&orig_entry, file, pool));

  /* You can only add something that is a) not in revision control, or
     b) slated for deletion from revision control. */
  if (orig_entry && orig_entry->schedule != svn_wc_schedule_delete)
    return svn_error_createf 
      (SVN_ERR_WC_ENTRY_EXISTS, 0, NULL, pool,
       "File '%s' has already been added to revision control",
       file->data);

  svn_path_split (file, &dir, &basename, svn_path_local_style, pool);

  SVN_ERR (svn_wc__entry_modify
           (dir, basename,
            (SVN_WC__ENTRY_MODIFY_SCHEDULE
             | SVN_WC__ENTRY_MODIFY_REVISION
             | SVN_WC__ENTRY_MODIFY_KIND),
            0, svn_node_file,
            svn_wc_schedule_add,
            svn_wc_existence_normal,
            FALSE, 0, 0, NULL, pool, NULL));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc_unadd (svn_string_t *path, 
              apr_pool_t *pool)
{
  svn_wc_entry_t *entry;
  svn_string_t *dir, *basename;

  /* Get the entry for PATH */
  SVN_ERR (svn_wc_entry (&entry, path, pool));
  if (entry->kind == svn_node_dir)
    {
      /* Recursively un-mark a whole tree for addition. */
      SVN_ERR (mark_tree (path, mark_tree_state_unadd, pool));
    }

  /* We need to un-mark this entry for addition in its parent's entries
     file, so we split off basename from the parent path, then fold in
     the addition of a delete flag. */
  svn_path_split (path, &dir, &basename, svn_path_local_style, pool);
  if (svn_path_is_empty (dir, svn_path_local_style))
    svn_string_set (dir, ".");
  
  SVN_ERR (svn_wc__entry_modify
           (dir, basename,
            SVN_WC__ENTRY_MODIFY_SCHEDULE,
            SVN_INVALID_REVNUM, svn_node_none,
            svn_wc_schedule_unadd,
            svn_wc_existence_normal,
            FALSE, 0, 0, NULL, pool, NULL));

  return SVN_NO_ERROR;
}


/* Un-mark a PATH for deletion.  If RECURSE is TRUE and PATH
   represents a directory, un-mark the entire tree under PATH for
   deletion.  */
svn_error_t *
svn_wc_undelete (svn_string_t *path, 
                 svn_boolean_t recursive,
                 apr_pool_t *pool)
{
  svn_wc_entry_t *entry;
  svn_string_t *dir, *basename;

  /* Get the entry for PATH */
  SVN_ERR (svn_wc_entry (&entry, path, pool));
  if (entry->kind == svn_node_dir)
    {
      /* Recursively un-mark a whole tree for addition. */
      SVN_ERR (mark_tree (path, mark_tree_state_undelete, pool));
    }

  /* We need to un-mark this entry for deletion in its parent's entries
     file, so we split off basename from the parent path, then fold in
     the addition of a delete flag. */
  svn_path_split (path, &dir, &basename, svn_path_local_style, pool);
  if (svn_path_is_empty (dir, svn_path_local_style))
    svn_string_set (dir, ".");
  
  SVN_ERR (svn_wc__entry_modify
           (dir, basename,
            SVN_WC__ENTRY_MODIFY_SCHEDULE,
            SVN_INVALID_REVNUM, svn_node_none,
            svn_wc_schedule_undelete,
            svn_wc_existence_normal,
            FALSE, 0, 0, NULL, pool, NULL));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc_get_pristine_copy_path (svn_string_t *path,
                               svn_string_t **pristine_path,
                               apr_pool_t *pool)
{
  *pristine_path = svn_wc__text_base_path (path, FALSE, pool);
  return SVN_NO_ERROR;
}



svn_error_t *
svn_wc_remove_from_revision_control (svn_string_t *path, 
                                     svn_string_t *name,
                                     svn_boolean_t destroy_wf,
                                     apr_pool_t *pool)
{
  apr_status_t apr_err;
  svn_error_t *err;
  svn_boolean_t is_file;
  svn_boolean_t left_a_file = FALSE;
  apr_pool_t *subpool = svn_pool_create (pool);
  apr_hash_t *entries = NULL;
  svn_string_t *full_path = svn_string_dup (path, pool);

  /* NAME is either a file's basename or SVN_WC_ENTRY_THIS_DIR. */
  is_file = (strcmp (name->data, SVN_WC_ENTRY_THIS_DIR)) ? TRUE : FALSE;
      
  if (is_file)
    {
      if (destroy_wf)
        {
          /* Check for local mods. */
          svn_boolean_t text_modified_p;
          svn_path_add_component (full_path, name, svn_path_local_style);
          SVN_ERR (svn_wc_text_modified_p (&text_modified_p, full_path,
                                           subpool));
          if (text_modified_p)  /* don't kill local mods */
            return svn_error_create (SVN_ERR_WC_LEFT_LOCAL_MOD, 0, NULL,
                                     subpool, "");
          else
            {
              /* Remove the actual working file. */
              apr_err = apr_file_remove (full_path->data, subpool);
              if (apr_err)
                return svn_error_createf (apr_err, 0, NULL, subpool,
                                          "Unable to remove file '%s'",
                                          full_path->data);
            }
        }

      /* Remove NAME from PATH's entries file: */
      svn_path_add_component (full_path, name, svn_path_local_style);
      SVN_ERR (svn_wc_entries_read (&entries, path, pool));
      svn_wc__entry_remove (entries, name);
      SVN_ERR (svn_wc__entries_write (entries, path, pool));

      /* Remove text-base/NAME, prop/NAME, prop-base/NAME, wcprops/NAME */
      {
        /* ### if they exist, remove them. */
      }

    }  /* done with file case */

  else /* looking at THIS_DIR */
    {
      svn_string_t *parent_dir, *basename;
      apr_hash_index_t *hi;
      /* ### sanity check:  check 2 places for DELETED flag? */

      /* Remove self from parent's entries file */
      svn_path_split (full_path, &parent_dir, &basename,
                      svn_path_local_style, pool);
      SVN_ERR (svn_wc_entries_read (&entries, parent_dir, pool));
      svn_wc__entry_remove (entries, basename);
      SVN_ERR (svn_wc__entries_write (entries, parent_dir, pool));      
      
      /* Recurse on each file and dir entry. */
      SVN_ERR (svn_wc_entries_read (&entries, path, subpool));
      
      for (hi = apr_hash_first (entries); hi;
           hi = apr_hash_next (hi))
        {
          const void *key;
          apr_size_t klen;
          void *val;
          svn_string_t *current_entry_name;
          svn_wc_entry_t *current_entry; 
          
          apr_hash_this (hi, &key, &klen, &val);
          current_entry = (svn_wc_entry_t *) val;
          if (! strcmp ((const char *)key, SVN_WC_ENTRY_THIS_DIR))
            current_entry_name = NULL;
          else
            current_entry_name = svn_string_create((const char *)key, subpool);

          if (current_entry->kind == svn_node_file)
            {
              err = svn_wc_remove_from_revision_control (path,
                                                         current_entry_name,
                                                         destroy_wf, subpool);
              if (err && (err->apr_err == SVN_ERR_WC_LEFT_LOCAL_MOD))
                left_a_file = TRUE;
              else if (err)
                return err;
            }
          else if (current_entry_name && (current_entry->kind == svn_node_dir))
            {
              svn_string_t *this_dir = svn_string_create
                (SVN_WC_ENTRY_THIS_DIR, subpool);
              svn_string_t *entrypath = svn_string_dup (path, subpool);
              svn_path_add_component (entrypath, current_entry_name,
                                      svn_path_local_style);
              err = svn_wc_remove_from_revision_control (entrypath, this_dir,
                                                         destroy_wf, subpool);
              if (err && (err->apr_err == SVN_ERR_WC_LEFT_LOCAL_MOD))
                left_a_file = TRUE;
              else if (err)
                return err;
            }
        }

      /* At this point, every directory below this one has been
         removed from revision control. */

      /* Remove the entire administrative SVN area, thereby removing
         _this_ dir from revision control too. */
      SVN_ERR (svn_wc__adm_destroy (path, subpool));
      
      /* If caller wants us to recursively nuke everything on disk, go
         ahead, provided that there are no dangling local-mod files
         below */
      if (destroy_wf && (! left_a_file))
        {
          apr_err = apr_dir_remove_recursively (path->data, subpool);
          if (apr_err)
            return svn_error_createf (apr_err, 0, NULL, subpool,
                                      "Can't recursively remove dir '%s'",
                                      path->data);
        }
    }  /* end of directory case */

  svn_pool_destroy (subpool);

  if (left_a_file)
    return svn_error_create (SVN_ERR_WC_LEFT_LOCAL_MOD, 0, NULL, pool, "");

  else
    return SVN_NO_ERROR;
}






/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
