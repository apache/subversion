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
 *
 * This software consists of voluntary contributions made by many
 * individuals.  For exact contribution history, see the revision
 * history and logs, available at http://subversion.tigris.org/.
 * ====================================================================
 */



#include <string.h>
#include <apr_pools.h>
#include <apr_hash.h>
#include <apr_file_io.h>
#include <apr_time.h>
#include "svn_types.h"
#include "svn_pools.h"
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
svn_wc__ensure_wc (svn_stringbuf_t *path,
                   svn_stringbuf_t *ancestor_path,
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
svn_wc__ensure_uniform_revision (svn_stringbuf_t *dir_path,
                                 svn_revnum_t revision,
                                 svn_boolean_t recurse,
                                 apr_pool_t *pool)
{
  apr_hash_t *entries;
  apr_hash_index_t *hi;
  apr_pool_t *subpool = svn_pool_create (pool);

  struct svn_wc_close_commit_baton *cbaton =
    apr_pcalloc (subpool, sizeof (*cbaton));
  cbaton->pool = subpool;
  cbaton->prefix_path = svn_stringbuf_create ("", subpool);

  SVN_ERR (svn_wc_entries_read (&entries, dir_path, subpool));

  /* Loop over this directory's entries: */
  for (hi = apr_hash_first (subpool, entries); hi; hi = apr_hash_next (hi))
    {
      const void *key;
      const char *keystring;
      apr_ssize_t klen;
      void *val;
      svn_stringbuf_t *current_entry_name;
      svn_wc_entry_t *current_entry; 
      svn_stringbuf_t *full_entry_path;

      /* Get the next entry */
      apr_hash_this (hi, &key, &klen, &val);
      keystring = (const char *) key;
      current_entry = (svn_wc_entry_t *) val;

      /* Compute the name of the entry */
      if (! strcmp (keystring, SVN_WC_ENTRY_THIS_DIR))
        current_entry_name = NULL;
      else
        current_entry_name = svn_stringbuf_create (keystring, subpool);

      /* Compute the complete path of the entry */
      full_entry_path = svn_stringbuf_dup (dir_path, subpool);
      if (current_entry_name)
        svn_path_add_component (full_entry_path, current_entry_name,
                                svn_path_local_style);

      /* If the entry's existence is `deleted', remove the entry
         altogether.  (If, during the update, the repository didn't
         re-add this entry, then it must not exist in the new
         revision!) */
      if (current_entry->existence == svn_wc_existence_deleted)
        {
          svn_stringbuf_t *thisdir_str = 
            svn_stringbuf_create (SVN_WC_ENTRY_THIS_DIR, subpool);

          /* THIS_DIR is deleted. */
          if (! current_entry_name)
            {
              SVN_ERR(svn_wc_remove_from_revision_control
                      (dir_path, thisdir_str, TRUE, subpool));

              /* don't bother to finish this entries loop... the whole
                 directory is blasted! */
              break;
            }

          /* a child subdirectory is deleted. */
          if ((current_entry->kind == svn_node_dir) && current_entry_name)
            {
              SVN_ERR(svn_wc_remove_from_revision_control
                      (full_entry_path, thisdir_str, TRUE, subpool));
            }
          
          /* a child file is deleted */
          if (current_entry->kind == svn_node_file)
            {
              SVN_ERR(svn_wc_remove_from_revision_control
                      (dir_path, current_entry_name, TRUE, subpool));
            }
        }


      /* If the entry is a file or SVN_WC_ENTRY_THIS_DIR, and it has a
         different rev than REVISION, fix it.  (But ignore the entry
         if it's scheduled for addition.) */
      else if (((current_entry->kind == svn_node_file)
                || (! current_entry_name))
               && (current_entry->revision != revision)
               && (current_entry->schedule != svn_wc_schedule_add))
        SVN_ERR (svn_wc_set_revision (cbaton, full_entry_path, revision));
      
      /* If entry is a dir (and not `.', and not scheduled for
         addition), then recurse into it. */
      else if (recurse && (current_entry->kind == svn_node_dir)
               && current_entry_name
               && (current_entry->schedule != svn_wc_schedule_add))
        SVN_ERR (svn_wc__ensure_uniform_revision (full_entry_path,
                                                  revision, recurse, subpool));
    }

  /* We're done examining this dir's entries, so free them. */
  svn_pool_destroy (subpool);

  return SVN_NO_ERROR;
}



svn_error_t *
svn_wc_set_revision (void *baton,
                     svn_stringbuf_t *target,
                     svn_revnum_t new_revnum)
{
  svn_error_t *err;
  apr_status_t apr_err;
  svn_stringbuf_t *log_parent, *logtag, *basename;
  apr_file_t *log_fp = NULL;
  struct svn_wc_close_commit_baton *bumper =
    (struct svn_wc_close_commit_baton *) baton;
  apr_pool_t *pool = bumper->pool;  /* cute, eh? */
  char *revstr = apr_psprintf (pool, "%ld", new_revnum);

  /* Construct the -full- path */
  svn_stringbuf_t *path = svn_stringbuf_dup (bumper->prefix_path, pool);
  svn_path_add_component (path, target, svn_path_local_style);

  /* Write a log file in the adm dir of path. */

  /* (First, try to write a logfile directly in PATH.) */
  log_parent = path;
  basename = svn_stringbuf_create (SVN_WC_ENTRY_THIS_DIR, pool);
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
        svn_stringbuf_set (log_parent, ".");

      SVN_ERR (svn_wc__open_adm_file (&log_fp, log_parent, SVN_WC__ADM_LOG,
                                      (APR_WRITE|APR_APPEND|APR_CREATE),
                                      pool));
    }
  else
    {
      /* PATH must be a dir */
      svn_stringbuf_t *pdir;

      if (svn_path_is_empty (log_parent, svn_path_local_style))
        {
          /* We have an empty path.  Since there is no way to examine
             the parent of an empty path, we ensure that the parent
             directory is '.', and that we are looking at the "this
             dir" entry. */
          pdir = svn_stringbuf_create (".", pool);
        }
      else
        {
          /* We were given a directory, so we look at that dir's "this
             dir" entry. */
          pdir = log_parent;
        }

      SVN_ERR (svn_wc__entry_modify
               (pdir, 
                basename,
                SVN_WC__ENTRY_MODIFY_REVISION,
                new_revnum,
                svn_node_none, 
                svn_wc_schedule_normal,
                svn_wc_existence_normal,
                FALSE,
                0, 
                0, 
                NULL, NULL,
                pool, 
                NULL));
    }

  /* Regardless of whether it's a file or dir, the "main" logfile
     contains a command to bump the revision attribute (and
     timestamp.)  */
  logtag = svn_stringbuf_create ("", pool);
  svn_xml_make_open_tag (&logtag, pool, svn_xml_self_closing,
                         SVN_WC__LOG_COMMITTED,
                         SVN_WC__LOG_ATTR_NAME, basename,
                         SVN_WC__LOG_ATTR_REVISION, 
                         svn_stringbuf_create (revstr, pool),
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
                                 svn_stringbuf_t *target,
                                 svn_stringbuf_t *name,
                                 svn_stringbuf_t **value)
{
  struct svn_wc_close_commit_baton *ccb =
    (struct svn_wc_close_commit_baton *) baton;

  /* Prepend the baton's prefix to the target. */
  svn_stringbuf_t *path = svn_stringbuf_dup (ccb->prefix_path, ccb->pool);
  svn_path_add_component (path, target, svn_path_local_style);

  /* And use our public interface to get the property value. */
  SVN_ERR (svn_wc__wcprop_get (value, name, path, ccb->pool));

  return SVN_NO_ERROR;
}


svn_error_t *svn_wc_set_wc_prop (void *baton,
                                 svn_stringbuf_t *target,
                                 svn_stringbuf_t *name,
                                 svn_stringbuf_t *value)
{
  struct svn_wc_close_commit_baton *ccb =
    (struct svn_wc_close_commit_baton *) baton;

  /* Prepend the baton's prefix to the target. */
  svn_stringbuf_t *path = svn_stringbuf_dup (ccb->prefix_path, ccb->pool);
  svn_path_add_component (path, target, svn_path_local_style);

  /* And use our public interface to get the property value. */
  SVN_ERR (svn_wc__wcprop_set (name, value, path, ccb->pool));

  return SVN_NO_ERROR;
}



/* Remove FILE if it exists and is a file.  If it does not exist, do
   nothing.  If it is not a file, error. */
static svn_error_t *
remove_file_if_present (svn_stringbuf_t *file, apr_pool_t *pool)
{
  apr_status_t apr_err;
  enum svn_node_kind kind;
  
  SVN_ERR (svn_io_check_path (file, &kind, pool));

  if (kind == svn_node_none)
    return SVN_NO_ERROR;

  /* Else. */

  apr_err = apr_file_remove (file->data, pool);
  if (apr_err)
    return svn_error_createf
      (apr_err, 0, NULL, pool, "Unable to remove '%s'", file->data);

  return SVN_NO_ERROR;
}




/* kff todo: not all of these really belong in wc_adm.  Some may get
   broken out into other files later.  They're just here to satisfy
   the public header file that they exist. */

svn_error_t *
svn_wc_rename (svn_stringbuf_t *src, svn_stringbuf_t *dst, apr_pool_t *pool)
{
  /* kff todo */
  return SVN_NO_ERROR;
}



enum mark_tree_state {
  mark_tree_state_delete = 1,
};


/* Recursively mark a tree DIR for some STATE. */
static svn_error_t *
mark_tree (svn_stringbuf_t *dir, enum mark_tree_state state, apr_pool_t *pool)
{
  apr_pool_t *subpool = svn_pool_create (pool);
  apr_hash_t *entries;
  apr_hash_index_t *hi;
  svn_stringbuf_t *fullpath = svn_stringbuf_dup (dir, pool);
  svn_pool_feedback_t *fbtable = svn_pool_get_feedback_vtable (pool);

  /* Read the entries file for this directory. */
  SVN_ERR (svn_wc_entries_read (&entries, dir, pool));

  /* Mark each entry in the entries file. */
  for (hi = apr_hash_first (pool, entries); hi; hi = apr_hash_next (hi))
    {
      const void *key;
      apr_ssize_t klen;
      void *val;
      svn_stringbuf_t *basename;
      svn_wc_entry_t *entry; 

      /* Get the next entry */
      apr_hash_this (hi, &key, &klen, &val);
      entry = (svn_wc_entry_t *) val;

      /* Skip "this dir".  */
      if (! strcmp ((const char *)key, SVN_WC_ENTRY_THIS_DIR))
        continue;
          
      basename = svn_stringbuf_create ((const char *) key, subpool);
      svn_path_add_component (fullpath, basename, svn_path_local_style);

      /* If the entry's existence is `deleted', skip it. */
      if (entry->existence == svn_wc_existence_deleted)
        continue;

      /* If this is a directory, recurse. */
      if (entry->kind == svn_node_dir)
        SVN_ERR (mark_tree (fullpath, state, subpool));

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
                    FALSE, 0, 0, NULL, NULL, subpool, NULL));
          if (fbtable)
            {
              apr_status_t apr_err;

              apr_err = fbtable->report_deleted_item (fullpath->data, pool);
              if (apr_err)
                return svn_error_createf 
                  (apr_err, 0, NULL, pool,
                   "Error reporting deleted item `%s'", fullpath->data);
            }
          break;
        }

      /* Reset FULLPATH to just hold this dir's name. */
      svn_stringbuf_set (fullpath, dir->data);

      /* Clear our per-iteration pool. */
      svn_pool_clear (subpool);
    }

  /* Handle "this dir" for states that need it done post-recursion. */
  if (state == mark_tree_state_delete)
    SVN_ERR (svn_wc__entry_modify
             (dir, NULL, 
              SVN_WC__ENTRY_MODIFY_SCHEDULE,
              SVN_INVALID_REVNUM, svn_node_dir,
              svn_wc_schedule_delete,
              svn_wc_existence_normal,
              FALSE, 0, 0, NULL, NULL, pool, NULL));

  /* Destroy our per-iteration pool. */
  svn_pool_destroy (subpool);
  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc_delete (svn_stringbuf_t *path, apr_pool_t *pool)
{
  svn_stringbuf_t *dir, *basename;
  svn_wc_entry_t *entry;

  /* Get the entry for the path we are deleting. */
  SVN_ERR (svn_wc_entry (&entry, path, pool));
  if (! entry)
    return svn_error_createf
      (SVN_ERR_WC_ENTRY_NOT_FOUND, 0, NULL, pool,
       "'%s' does not appear to be under revision control", path->data);
    
  if (entry->existence == svn_wc_existence_deleted)
    return svn_error_createf
      (SVN_ERR_WC_ENTRY_NOT_FOUND, 0, NULL, pool,
       "entry '%s' has already been deleted", path->data);

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
    svn_stringbuf_set (dir, ".");
  
  SVN_ERR (svn_wc__entry_modify
           (dir, basename, 
            SVN_WC__ENTRY_MODIFY_SCHEDULE,
            SVN_INVALID_REVNUM, entry->kind,
            svn_wc_schedule_delete,
            svn_wc_existence_normal,
            FALSE, 0, 0, NULL, NULL, pool, NULL));

  /* Now, call our client feedback function. */
  {
    svn_pool_feedback_t *fbtable = svn_pool_get_feedback_vtable (pool);
    if (fbtable)
      {
        apr_status_t apr_err;

        apr_err = fbtable->report_deleted_item (path->data, pool);
        if (apr_err)
          return svn_error_createf 
            (apr_err, 0, NULL, pool,
             "Error reporting deleted item `%s'", path->data);
      }
  }

  return SVN_NO_ERROR;
}


static svn_error_t *
add_to_revision_control (svn_stringbuf_t *path,
                         enum svn_node_kind kind,
                         svn_stringbuf_t *ancestor_path,
                         apr_pool_t *pool)
{
  svn_stringbuf_t *parent_dir, *basename;
  svn_stringbuf_t *copyfrom_url, *copyfrom_rev;
  svn_wc_entry_t *orig_entry, *anc_entry, *parent_entry;
  svn_pool_feedback_t *fbtable = svn_pool_get_feedback_vtable (pool);
  apr_status_t apr_err;
  apr_hash_t *atts = apr_hash_make (pool);
  svn_stringbuf_t *url = NULL;

  /* Get the original entry for this path if one exists (perhaps
     this is actually a replacement of a previously deleted thing). */
  if (svn_wc_entry (&orig_entry, path, pool))
    orig_entry = NULL;

  /* You can only add something that is a) not in revision control, or
     b) slated for deletion from revision control, or c) already
     `deleted' from revision control.   Unless, of course, you're
     specifying an addition with -history-;  then it's okay for the
     object to be under version control already;  it's not really new.  */
  if (orig_entry)
    {
      if ((! ancestor_path)
          && (orig_entry->schedule != svn_wc_schedule_delete)
          && (orig_entry->existence != svn_wc_existence_deleted))
        {
          return svn_error_createf 
            (SVN_ERR_WC_ENTRY_EXISTS, 0, NULL, pool,
             "'%s' is already under revision control",
             path->data);
        }
      else if (orig_entry->kind != kind)
        {
          /* ### todo:  At some point, we obviously don't want to
             block replacements where the node kind changes.  When
             this happens, svn_wc_revert() needs to learn how to
             revert this situation.  */
          return svn_error_createf 
            (SVN_ERR_UNSUPPORTED_FEATURE, 0, NULL, pool,
             "Could not replace '%s' with a node of a differing type"
             " -- try committing your deletion first and then re-adding '%s'",
             path->data, path->data);
        }
    }
    
  /* Split off the basename from the parent directory. */
  svn_path_split (path, &parent_dir, &basename, svn_path_local_style, pool);
  if (svn_path_is_empty (parent_dir, svn_path_local_style))
    parent_dir = svn_stringbuf_create (".", pool);

  /* If a copy ancestor was given, put the proper ancestry info in a hash. */
  if (ancestor_path)
    {
      /* Here's where we create and set the copyfrom_* args */
      SVN_ERR (svn_wc_entry (&anc_entry, ancestor_path, pool));
      copyfrom_url = svn_stringbuf_dup (anc_entry->ancestor, pool);
      copyfrom_rev = svn_stringbuf_createf (pool, "%ld", anc_entry->revision);
      apr_hash_set (atts, 
                    SVN_WC_ENTRY_ATTR_COPYFROM_URL, APR_HASH_KEY_STRING,
                    copyfrom_url);
      apr_hash_set (atts, 
                    SVN_WC_ENTRY_ATTR_COPYFROM_REV, APR_HASH_KEY_STRING,
                    copyfrom_rev);

      if (kind == svn_node_dir)
        {        
          /* Need to set SVN_WC_ENTRY_ATTR_ANCESTOR in the hash too,
             to reflect the copied directory's final url.  Normally,
             __ensure_adm() would create this url.  But because the
             copied directory already has an .svn area, the function
             doesn't touch it. */
          SVN_ERR (svn_wc_entry (&parent_entry, parent_dir, pool));
          url = svn_stringbuf_dup (parent_entry->ancestor, pool);
          svn_path_add_component (url, basename, svn_path_url_style);
        }
    }


  /* Now, add the entry for this item to the parent_dir's
     entries file, marking it for addition. */
  /* ### todo:  Should we NOT reset the revision if this is a replace? */
  SVN_ERR (svn_wc__entry_modify
           (parent_dir, basename,
            (SVN_WC__ENTRY_MODIFY_SCHEDULE
             | SVN_WC__ENTRY_MODIFY_REVISION
             | SVN_WC__ENTRY_MODIFY_KIND
             | SVN_WC__ENTRY_MODIFY_ATTRIBUTES),
            0, kind,
            svn_wc_schedule_add,
            svn_wc_existence_normal,
            FALSE, 0, 0, NULL,
            atts,  /* may or may not contain copyfrom args */
            pool, NULL));

  /* If this is a replacement, we need to reset the properties for
     PATH. */
  if (orig_entry)
    {
      svn_stringbuf_t *prop_path;
      SVN_ERR (svn_wc__prop_path (&prop_path, path, FALSE, pool));
      SVN_ERR (remove_file_if_present (prop_path, pool));
    }

  if (kind == svn_node_file)
    {
      const char *mimetype;

      /* Try to detect the mime-type of this new addition. */
      SVN_ERR (svn_io_detect_mimetype (&mimetype, path->data, pool));
      if (mimetype)
        SVN_ERR (svn_wc_prop_set 
                 (svn_stringbuf_create (SVN_PROP_MIME_TYPE, pool),
                  svn_stringbuf_create (mimetype, pool),
                  path,
                  pool));
    }  
  else
    {
      svn_wc_entry_t *p_entry;
      svn_stringbuf_t *p_path;
      apr_uint16_t flags;

      /* Get the entry for this directory's parent.  We need to snatch
         the ancestor path out of there. */
      SVN_ERR (svn_wc_entry (&p_entry, parent_dir, pool));
  
      /* Derive the parent path for our new addition here. */
      p_path = svn_stringbuf_dup (p_entry->ancestor, pool);
      svn_path_add_component (p_path, basename, svn_path_url_style);
  
      /* Make sure this new directory has an admistrative subdirectory
         created inside of it */
      SVN_ERR (svn_wc__ensure_adm (path, p_path, 0, pool));
      
      /* Things we plan to change in this_dir. */
      flags = (SVN_WC__ENTRY_MODIFY_SCHEDULE
               | SVN_WC__ENTRY_MODIFY_REVISION 
               | SVN_WC__ENTRY_MODIFY_KIND
               | SVN_WC__ENTRY_MODIFY_ATTRIBUTES
               | SVN_WC__ENTRY_MODIFY_FORCE);

      /* If we had to manually calculate a copied directory's ancestor
         url, add to the flags. */
      if (url)
        flags |= SVN_WC__ENTRY_MODIFY_ANCESTOR;

      /* And finally, make sure this entry is marked for addition in
         its own administrative directory. */
      SVN_ERR (svn_wc__entry_modify
               (path, NULL,
                flags,
                0, svn_node_dir,
                ((orig_entry 
                  && orig_entry->schedule == svn_wc_schedule_delete) 
                 ? svn_wc_schedule_replace 
                 : svn_wc_schedule_add),
                svn_wc_existence_normal,
                FALSE, 0, 0,
                url,   /* may or may not be null */
                atts,  /* may or may not contain copyfrom args */
                pool, NULL));
    }
  
  /* Now, call our client feedback function. */
  if (fbtable)
    {
      apr_err = fbtable->report_added_item (path->data, pool);
      if (apr_err)
        return svn_error_createf 
          (apr_err, 0, NULL, pool,
           "Error reporting added item `%s'", path->data);
    }

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc_add_directory (svn_stringbuf_t *dir,
                      svn_stringbuf_t *ancestor_path,
                      apr_pool_t *pool)
{
  return add_to_revision_control (dir, svn_node_dir, ancestor_path, pool);
}


svn_error_t *
svn_wc_add_file (svn_stringbuf_t *file,
                 svn_stringbuf_t *ancestor_path,
                 apr_pool_t *pool)
{
  return add_to_revision_control (file, svn_node_file, ancestor_path, pool);
}



/* Thoughts on Reversion. 

    What does is mean to revert a given PATH in a tree?  We'll
    consider things by their modifications.

    Adds

    - For files, svn_wc_remove_from_revision_control(), baby.

    - Added directories may contain nothing but added children, and
      reverting the addition of a directory necessary means reverting
      the addition of all the directory's children.  Again,
      svn_wc_remove_from_revision_control() should do the trick.

    Deletes

    - Restore properties to their unmodified state.

    - For files, restore the pristine contents, and reset the schedule
      to 'normal'.

    - For directories, reset the schedule to 'normal'.  All children
      of a directory marked for deletion must also be marked for
      deletion, but it's okay for those children to remain deleted even
      if their parent directory is restored.  That's what the
      recursive flag is for.

    Replaces

    - Restore properties to their unmodified state.

    - For files, restore the pristine contents, and reset the schedule
      to 'normal'.

    - For directories, reset the schedule to normal.  A replaced
      directory can have deleted children (left over from the initial
      deletion), replaced children (children of the initial deletion
      now re-added), and added children (new entries under the
      replaced directory).  Since this is technically an addition, it
      necessitates recursion.

    Modifications

    - Restore properties and, for files, contents to their unmodified
      state.

*/

/* Revert ENTRY in directory PARENT_DIR, trusting that it is of kind
   KIND, and using POOL for any necessary allocations.  Set REVERTED
   to TRUE if anything was modified, FALSE otherwise. */
static svn_error_t *
revert_admin_things (svn_boolean_t *reverted,
                     svn_stringbuf_t *parent_dir,
                     svn_stringbuf_t *entry,
                     enum svn_node_kind kind,
                     apr_pool_t *pool)
{
  svn_stringbuf_t *full_path, *thing, *pristine_thing;
  svn_error_t *err;
  svn_boolean_t text_modified_p = FALSE, prop_modified_p = FALSE;
  apr_time_t tstamp, pstamp;

  *reverted = FALSE;
  full_path = svn_stringbuf_dup (parent_dir, pool);
  if (entry)
    svn_path_add_component (full_path, entry, svn_path_local_style);

  /* ### todo:  WARNING WARNING WARNING!!  This is NOT crash-proof! */
  /* ### todo:  WARNING WARNING WARNING!!  This is NOT crash-proof! */
  /* ### todo:  WARNING WARNING WARNING!!  This is NOT crash-proof! */

  SVN_ERR (svn_wc_props_modified_p (&prop_modified_p, full_path, pool));  
  if (prop_modified_p)
    {
      SVN_ERR (svn_wc__prop_path (&thing, full_path, 0, pool)); 
      SVN_ERR (svn_wc__prop_base_path (&pristine_thing, full_path, 0, pool));
      err = svn_io_copy_file (pristine_thing, thing, pool);
      if (err)
        return svn_error_createf 
          (err->apr_err, 0, NULL, pool,
           "revert_admin_things:  Error restoring pristine props for '%s'", 
           full_path->data);
      SVN_ERR (svn_io_file_affected_time (&pstamp, thing, pool));
    }

  if (kind == svn_node_file)
    {
      SVN_ERR (svn_wc_text_modified_p (&text_modified_p, full_path, pool));
      if (text_modified_p)
        {
          /* If there are textual mods, copy the text-base out into
             the working copy, and update the timestamp in the entries
             file. */
          pristine_thing = svn_wc__text_base_path (full_path, 0, pool);
          err = svn_io_copy_file (pristine_thing, full_path, pool);
          if (err)
            return svn_error_createf 
              (err->apr_err, 0, NULL, pool,
               "revert_admin_things:  Error restoring pristine text for '%s'", 
               full_path->data);
          SVN_ERR (svn_io_file_affected_time (&tstamp, full_path, pool));
        }
    }

  
  if (text_modified_p || prop_modified_p)
    {
      apr_uint64_t modify_flags = 0;
      modify_flags |= (text_modified_p ? SVN_WC__ENTRY_MODIFY_TEXT_TIME : 0);
      modify_flags |= (prop_modified_p ? SVN_WC__ENTRY_MODIFY_PROP_TIME : 0);

      /* Update the entries file. */
      SVN_ERR (svn_wc__entry_modify
               (parent_dir, 
                entry,
                modify_flags,
                SVN_INVALID_REVNUM, 
                svn_node_none, 
                svn_wc_schedule_normal,
                svn_wc_existence_normal, 
                FALSE, 
                tstamp, 
                pstamp, 
                NULL, NULL,
                pool, 
                NULL));

      *reverted = TRUE;
    }

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc_revert (svn_stringbuf_t *path,
               svn_boolean_t recursive,
               apr_pool_t *pool)
{
  enum svn_node_kind kind;
  svn_stringbuf_t *p_dir = NULL, *basename = NULL;
  svn_wc_entry_t *entry;
  svn_boolean_t wc_root, reverted = FALSE;
  svn_pool_feedback_t *fbtable = svn_pool_get_feedback_vtable (pool);

  /* Safeguard 1:  is this a versioned resource? */
  SVN_ERR (svn_wc_entry (&entry, path, pool));
  if (! entry)
    return svn_error_createf 
      (SVN_ERR_WC_ENTRY_NOT_FOUND, 0, NULL, pool,
       "Cannot revert '%s' -- not a versioned resource", path->data);

  /* Safeguard 2:  can we handle this node kind? */
  if ((entry->kind != svn_node_file) && (entry->kind != svn_node_dir))
    return svn_error_createf 
      (SVN_ERR_UNSUPPORTED_FEATURE, 0, NULL, pool,
       "Cannot revert '%s' -- unsupported entry node kind", path->data);

  /* Safeguard 3:  can we deal with the node kind of PATH current in
     the working copy? */
  SVN_ERR (svn_io_check_path (path, &kind, pool));
  if ((kind != svn_node_none)
      && (kind != svn_node_file)
      && (kind != svn_node_dir))
    return svn_error_createf 
      (SVN_ERR_UNSUPPORTED_FEATURE, 0, NULL, pool,
       "Cannot revert '%s' -- unsupported node kind in working copy", 
       path->data);

  /* Determine if PATH is a WC root.  If PATH is a file, it should
     definitely NOT be a WC root. */
  SVN_ERR (svn_wc_is_wc_root (&wc_root, path, pool));
  if (! wc_root)
    {
      /* Split the basename from the parent path. */
      svn_path_split (path, &p_dir, &basename, svn_path_local_style, pool);
      if (svn_path_is_empty (p_dir, svn_path_local_style))
        p_dir = svn_stringbuf_create (".", pool);
    }

  /* Additions. */
  if (entry->schedule == svn_wc_schedule_add)
    {
      /* Remove the item from revision control. */
      if (entry->kind == svn_node_dir)
        SVN_ERR (svn_wc_remove_from_revision_control 
                 (path, 
                  svn_stringbuf_create (SVN_WC_ENTRY_THIS_DIR, pool),
                  FALSE, pool));
      else
        SVN_ERR (svn_wc_remove_from_revision_control (p_dir, basename, 
                                                      FALSE, pool));

      /* Recursivity is taken care of by svn_wc_remove_from_revision_control,
         and we've definitely reverted PATH at this point. */
      recursive = FALSE;
      reverted = TRUE;
    }

  /* Regular prop and text edit. */
  else if (entry->schedule == svn_wc_schedule_normal)
    {
      /* Revert the prop and text mods (if any). */
      if (entry->kind == svn_node_dir)
        SVN_ERR (revert_admin_things (&reverted, path, NULL, 
                                      entry->kind, pool));
      else
        SVN_ERR (revert_admin_things (&reverted, p_dir, basename, 
                                      entry->kind, pool));
    }

  /* Deletions and replacements. */
  else if ((entry->schedule == svn_wc_schedule_delete) 
           || (entry->schedule == svn_wc_schedule_replace))
    {
      /* Revert the prop and text mods (if any). */
      if (entry->kind == svn_node_dir)
        SVN_ERR (revert_admin_things (&reverted, path, NULL, 
                                      entry->kind, pool));
      else
        SVN_ERR (revert_admin_things (&reverted, p_dir, basename, 
                                      entry->kind, pool));

      /* Reset the schedule to normal. */
      if (! wc_root)
        {
          SVN_ERR (svn_wc__entry_modify
                   (p_dir,
                    basename,
                    SVN_WC__ENTRY_MODIFY_SCHEDULE | SVN_WC__ENTRY_MODIFY_FORCE,
                    SVN_INVALID_REVNUM,
                    svn_node_none,
                    svn_wc_schedule_normal,
                    svn_wc_existence_normal,
                    TRUE,
                    0,
                    0,
                    NULL, NULL,
                    pool,
                    NULL));
        }

      /* For directories only. */
      if (entry->kind == svn_node_dir) 
        {
          /* Force recursion on replaced directories. */
          if (entry->schedule == svn_wc_schedule_replace)
            recursive = TRUE;

          /* Reset the schedule to normal in the directory itself. */
          SVN_ERR (svn_wc__entry_modify
                   (path,
                    NULL,
                    SVN_WC__ENTRY_MODIFY_SCHEDULE | SVN_WC__ENTRY_MODIFY_FORCE,
                    SVN_INVALID_REVNUM,
                    svn_node_none,
                    svn_wc_schedule_normal,
                    svn_wc_existence_normal,
                    TRUE,
                    0,
                    0,
                    NULL, NULL,
                    pool,
                    NULL));
        }

      /* Note that this was reverted. */
      reverted = TRUE;
    }

  /* If PATH was reverted, tell our client that. */
  if ((fbtable) && (reverted))
    {
      apr_status_t apr_err = fbtable->report_reversion (path->data, pool);
      if (apr_err)
        return svn_error_createf 
          (apr_err, 0, NULL, pool,
           "Error reporting reversion of `%s'", path->data);
    }

  /* Finally, recurse if requested. */
  if (recursive && (entry->kind == svn_node_dir))
    {
      apr_hash_t *entries;
      apr_hash_index_t *hi;
      svn_stringbuf_t *full_entry_path = svn_stringbuf_dup (path, pool);

      SVN_ERR (svn_wc_entries_read (&entries, path, pool));
      for (hi = apr_hash_first (pool, entries); hi; hi = apr_hash_next (hi))
        {
          const void *key;
          const char *keystring;
          apr_ssize_t klen;
          void *val;
          
          /* Get the next entry */
          apr_hash_this (hi, &key, &klen, &val);
          keystring = (const char *) key;

          /* Skip "this dir" */
          if (! strcmp (keystring, SVN_WC_ENTRY_THIS_DIR))
            continue;

          /* Add the entry name to FULL_ENTRY_PATH. */
          svn_path_add_component_nts (full_entry_path,
                                      keystring,
                                      svn_path_local_style);

          /* Revert the entry. */
          SVN_ERR (svn_wc_revert (full_entry_path, TRUE, pool));

          /* Return FULL_ENTRY_PATH to its pre-appended state. */
          svn_stringbuf_set (full_entry_path, path->data);
        }
    }
  
  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc_get_pristine_copy_path (svn_stringbuf_t *path,
                               svn_stringbuf_t **pristine_path,
                               apr_pool_t *pool)
{
  *pristine_path = svn_wc__text_base_path (path, FALSE, pool);
  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc_remove_from_revision_control (svn_stringbuf_t *path, 
                                     svn_stringbuf_t *name,
                                     svn_boolean_t destroy_wf,
                                     apr_pool_t *pool)
{
  apr_status_t apr_err;
  svn_error_t *err;
  svn_boolean_t is_file;
  svn_boolean_t left_a_file = FALSE;
  apr_pool_t *subpool = svn_pool_create (pool);
  apr_hash_t *entries = NULL;
  svn_stringbuf_t *full_path = svn_stringbuf_dup (path, pool);

  /* NAME is either a file's basename or SVN_WC_ENTRY_THIS_DIR. */
  is_file = (strcmp (name->data, SVN_WC_ENTRY_THIS_DIR)) ? TRUE : FALSE;
      
  if (is_file)
    {
      svn_path_add_component (full_path, name, svn_path_local_style);

      if (destroy_wf)
        {
          /* Check for local mods. */
          svn_boolean_t text_modified_p;
          SVN_ERR (svn_wc_text_modified_p (&text_modified_p, full_path,
                                           subpool));
          if (text_modified_p)  /* don't kill local mods */
            {
              return svn_error_create (SVN_ERR_WC_LEFT_LOCAL_MOD,
                                       0, NULL, subpool, "");
            }
          else
            {
              /* The working file is still present; remove it. */
              SVN_ERR (remove_file_if_present (full_path, subpool));
            }
        }

      /* Remove NAME from PATH's entries file: */
      SVN_ERR (svn_wc_entries_read (&entries, path, pool));
      svn_wc__entry_remove (entries, name);
      SVN_ERR (svn_wc__entries_write (entries, path, pool));

      /* Remove text-base/NAME, prop/NAME, prop-base/NAME, wcprops/NAME */
      {
        svn_stringbuf_t *svn_thang;

        /* Text base. */
        svn_thang = svn_wc__text_base_path (full_path, 0, subpool);
        SVN_ERR (remove_file_if_present (svn_thang, subpool));

        /* Working prop file. */
        SVN_ERR (svn_wc__prop_path (&svn_thang, full_path, 0, subpool));
        SVN_ERR (remove_file_if_present (svn_thang, subpool));

        /* Prop base file. */
        SVN_ERR (svn_wc__prop_base_path (&svn_thang, full_path, 0, subpool));
        SVN_ERR (remove_file_if_present (svn_thang, subpool));

        /* wc-prop file. */
        SVN_ERR (svn_wc__wcprop_path (&svn_thang, full_path, 0, subpool));
        SVN_ERR (remove_file_if_present (svn_thang, subpool));
      }

    }  /* done with file case */

  else /* looking at THIS_DIR */
    {
      svn_stringbuf_t *parent_dir, *basename;
      apr_hash_index_t *hi;
      /* ### sanity check:  check 2 places for DELETED flag? */

      /* Remove self from parent's entries file */
      svn_path_split (full_path, &parent_dir, &basename,
                      svn_path_local_style, pool);
      /* ### sanity check:  is parent_dir even a working copy?
         if not, it should not be a fatal error.  we're just removing
         the top of the wc. */
      SVN_ERR (svn_wc_entries_read (&entries, parent_dir, pool));
      svn_wc__entry_remove (entries, basename);
      SVN_ERR (svn_wc__entries_write (entries, parent_dir, pool));      
      
      /* Recurse on each file and dir entry. */
      SVN_ERR (svn_wc_entries_read (&entries, path, subpool));
      
      for (hi = apr_hash_first (subpool, entries); 
           hi;
           hi = apr_hash_next (hi))
        {
          const void *key;
          apr_ssize_t klen;
          void *val;
          svn_stringbuf_t *current_entry_name;
          svn_wc_entry_t *current_entry; 
          
          apr_hash_this (hi, &key, &klen, &val);
          current_entry = (svn_wc_entry_t *) val;
          if (! strcmp ((const char *)key, SVN_WC_ENTRY_THIS_DIR))
            current_entry_name = NULL;
          else
            current_entry_name = svn_stringbuf_create((const char *)key, 
                                                      subpool);

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
              svn_stringbuf_t *this_dir = svn_stringbuf_create
                (SVN_WC_ENTRY_THIS_DIR, subpool);
              svn_stringbuf_t *entrypath = svn_stringbuf_dup (path, subpool);
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
          /* If the dir is *truly* empty (i.e. has no unversioned
             resources, all versioned files are gone, all SVN dirs are
             gone, and contains nothing but empty dirs), then a
             *non*-recursive dir_remove should work.  If it doesn't,
             no big deal.  Just assume there are unversioned items in
             there and set "left_a_file" */
          apr_err = apr_dir_remove (path->data, subpool);
          if (apr_err)
            left_a_file = TRUE;
        }
    }  /* end of directory case */

  svn_pool_destroy (subpool);

  if (left_a_file)
    return svn_error_create (SVN_ERR_WC_LEFT_LOCAL_MOD, 0, NULL, pool, "");

  else
    return SVN_NO_ERROR;
}



svn_error_t *
svn_wc_get_auth_file (svn_stringbuf_t *path,
                      const char *filename,
                      svn_stringbuf_t **contents,
                      apr_pool_t *pool)
{
  svn_stringbuf_t *full_path_to_file =
    svn_wc__adm_path (path, 0, pool, SVN_WC__ADM_AUTH_DIR, filename, NULL);
  
  /* Sanity check */
  if (! svn_wc__adm_path_exists (path, 0, pool,
                                 SVN_WC__ADM_AUTH_DIR, filename, NULL))
    return 
      svn_error_createf (SVN_ERR_WC_PATH_NOT_FOUND, 0, NULL, pool,
                         "auth file '%s' not found in adm area of '%s'",
                         filename, path->data);

  /* Read the file's contents into a stringbuf, allocated in POOL. */
  SVN_ERR (svn_string_from_file (contents, full_path_to_file->data, pool));

  return SVN_NO_ERROR;
}



svn_error_t *
svn_wc_set_auth_file (svn_stringbuf_t *path,
                      svn_boolean_t recurse,
                      const char *filename,
                      svn_stringbuf_t *contents,
                      apr_pool_t *pool)
{
  apr_status_t status;
  apr_file_t *fp;
  apr_size_t sz;

  svn_stringbuf_t *file = svn_stringbuf_create (filename, pool);

  /* Create/overwrite the file in PATH's administrative area.
     (In reality, this opens a file 'path/SVN/tmp/auth/filename'.) */
  SVN_ERR (svn_wc__open_auth_file (&fp, path, file,
                                   (APR_WRITE | APR_CREATE | APR_TRUNCATE),
                                   pool));

  status = apr_file_write_full (fp, contents->data, contents->len, &sz);
  if (status) 
    return svn_error_createf (status, 0, NULL, pool,
                              "error writing to auth file '%s' in '%s'",
                              filename, path->data);

  SVN_ERR (svn_wc__close_auth_file (fp, path, file, TRUE /* sync */, pool));
  
  if (recurse)
    {
      /* Loop over PATH's entries, and recurse into directories. */
      apr_hash_index_t *hi;
      apr_hash_t *entries;
      const char *basename;
      svn_wc_entry_t *entry;

      SVN_ERR (svn_wc_entries_read (&entries, path, pool));

      for (hi = apr_hash_first (pool, entries); hi; hi = apr_hash_next (hi))
        {
          const void *key;
          apr_ssize_t keylen;
          void *val;

          apr_hash_this (hi, &key, &keylen, &val);
          basename = (const char *) key;          
          entry = (svn_wc_entry_t *) val;

          if ((entry->kind == svn_node_dir)
              && (strcmp (basename, SVN_WC_ENTRY_THIS_DIR)))
            {              
              svn_stringbuf_t *childpath; 
              
              /* If the entry's existence is `deleted', skip it. */
              if ((entry->existence == svn_wc_existence_deleted)
                  && (entry->schedule != svn_wc_schedule_add))
                continue;

              childpath = svn_stringbuf_dup (path, pool);
              svn_path_add_component (childpath, 
                                      svn_stringbuf_create (basename, pool),
                                      svn_path_local_style);

              SVN_ERR (svn_wc_set_auth_file (childpath, TRUE,
                                             filename, contents, pool));
            }
        }
    }


  return SVN_NO_ERROR;
}





/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
