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
 * based on URL at REVISION.
 *
 * Creates the adm area if none, in which case PATH starts out at
 * revision 0.
 *
 * Note: The adm area's lock-state is not changed by this function,
 * and if the adm area is created, it is left in an unlocked state.
 */
svn_error_t *
svn_wc__ensure_wc (svn_stringbuf_t *path,
                   svn_stringbuf_t *url,
                   svn_revnum_t revision,
                   apr_pool_t *pool)
{
  svn_error_t *err;

  err = svn_wc__ensure_adm (path,
                            url,
                            revision,
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

      /* If the entry is a file or SVN_WC_ENTRY_THIS_DIR, and it has a
         different rev than REVISION, fix it.  (But ignore the entry
         if it's scheduled for addition or replacement.) */
      if (((current_entry->kind == svn_node_file)
           || (! current_entry_name))
          && (current_entry->revision != revision)
          && (current_entry->schedule != svn_wc_schedule_add)
          && (current_entry->schedule != svn_wc_schedule_replace))
        SVN_ERR (svn_wc_set_revision (cbaton, full_entry_path, FALSE,
                                      revision));
      
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



/* This function is the "real" meat of svn_wc_set_revision; it assumes
   that PATH is absolute.  */
static svn_error_t *
set_revision (svn_stringbuf_t *path,
              svn_boolean_t recurse,
              svn_revnum_t new_revnum,
              apr_pool_t *pool)
{
  svn_error_t *err;
  apr_status_t apr_err;
  svn_stringbuf_t *log_parent, *logtag, *basename;
  apr_file_t *log_fp = NULL;
  char *revstr = apr_psprintf (pool, "%ld", new_revnum);

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
                FALSE, FALSE,
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

  if (recurse)
    {
      apr_hash_t *entries;
      apr_hash_index_t *hi;

      /* Read PATH's entries;  this is the absolute path. */
      SVN_ERR (svn_wc_entries_read (&entries, path, pool));

      /* Recursively loop over all children. */
      for (hi = apr_hash_first (pool, entries); hi; hi = apr_hash_next (hi))
        {
          const void *key;
          void *val;
          const char *name;
          svn_wc_entry_t *current_entry;
          
          apr_hash_this (hi, &key, NULL, &val);
          name = (const char *) key;
          current_entry = (svn_wc_entry_t *) val;
          
          /* Ignore the "this dir" entry. */
          if (! strcmp (name, SVN_WC_ENTRY_THIS_DIR))
            continue;
          
          /* Create child path by telescoping the main path. */
          svn_path_add_component_nts (path, name, svn_path_local_style);
          
          /* Recurse, but only allow further recursion if the child is
             a directory.  */
          SVN_ERR (set_revision (path, 
                                 (current_entry->kind == svn_node_dir)
                                    ? TRUE : FALSE,
                                 new_revnum,
                                 pool));

          /* De-telescope the path. */
          svn_path_remove_component (path, svn_path_local_style);
        }
    }

  return SVN_NO_ERROR;
}


/* Public API for above */
svn_error_t *
svn_wc_set_revision (void *baton,
                     svn_stringbuf_t *target,
                     svn_boolean_t recurse,
                     svn_revnum_t new_revnum)
{
  struct svn_wc_close_commit_baton *bumper =
    (struct svn_wc_close_commit_baton *) baton;

  /* Construct the -full- path by using the baton */
  svn_stringbuf_t *path = svn_stringbuf_dup (bumper->prefix_path, 
                                             bumper->pool);
  svn_path_add_component (path, target, svn_path_local_style);

  /* Call the real function. */
  return set_revision (path, recurse, new_revnum, bumper->pool);
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




/* Recursively mark a tree DIR for with a SCHEDULE and/or EXISTENCE
   flag and/or COPIED flag, depending on the state of MODIFY_FLAGS. */
static svn_error_t *
mark_tree (svn_stringbuf_t *dir, 
           apr_uint16_t modify_flags,
           enum svn_wc_schedule_t schedule,
           svn_boolean_t copied,
           apr_pool_t *pool)
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

      /* If this is a directory, recurse. */
      if (entry->kind == svn_node_dir)
        SVN_ERR (mark_tree (fullpath, modify_flags,
                            schedule, copied, subpool));

      /* Mark this entry. */
      SVN_ERR (svn_wc__entry_modify
               (dir, basename, 
                modify_flags,
                SVN_INVALID_REVNUM, entry->kind,
                schedule,
                FALSE, TRUE, 0, 0, NULL, NULL, subpool, NULL));

      if (fbtable && (schedule == svn_wc_schedule_delete))
        {
          apr_status_t apr_err;
          
          apr_err = fbtable->report_deleted_item (fullpath->data, pool);
          if (apr_err)
            return svn_error_createf 
              (apr_err, 0, NULL, pool,
               "Error reporting deleted item `%s'", fullpath->data);
        }

      /* Reset FULLPATH to just hold this dir's name. */
      svn_stringbuf_set (fullpath, dir->data);

      /* Clear our per-iteration pool. */
      svn_pool_clear (subpool);
    }

  /* Handle "this dir" for states that need it done post-recursion. */
  SVN_ERR (svn_wc__entry_modify
           (dir, NULL, 
            modify_flags,
            SVN_INVALID_REVNUM, svn_node_dir,
            schedule,
            FALSE,
            TRUE,
            0, 0, NULL, NULL, pool, NULL));
  
  /* Destroy our per-iteration pool. */
  svn_pool_destroy (subpool);
  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc_delete (svn_stringbuf_t *path, apr_pool_t *pool)
{
  svn_stringbuf_t *dir, *basename;
  svn_wc_entry_t *entry;
  svn_boolean_t dir_unadded = FALSE;

  /* Get the entry for the path we are deleting. */
  SVN_ERR (svn_wc_entry (&entry, path, pool));
  if (! entry)
    return svn_error_createf
      (SVN_ERR_WC_ENTRY_NOT_FOUND, 0, NULL, pool,
       "'%s' does not appear to be under revision control", path->data);
    
  if (entry->kind == svn_node_dir)
    {
      /* Special case, delete of a newly added dir. */
      if (entry->schedule == svn_wc_schedule_add)
        dir_unadded = TRUE;
      else
        /* Recursively mark a whole tree for deletion. */
        SVN_ERR (mark_tree (path, SVN_WC__ENTRY_MODIFY_SCHEDULE,
                            svn_wc_schedule_delete, FALSE, pool));
    }

  /* Deleting a directory that has been added but not yet
     committed is easy, just remove the adminstrative dir. */
  if (dir_unadded)
    {
      svn_stringbuf_t *this_dir =
        svn_stringbuf_create (SVN_WC_ENTRY_THIS_DIR, pool);
      SVN_ERR (svn_wc_remove_from_revision_control (path,
                                                    this_dir,
                                                    FALSE, pool));
    }
  else
    {
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
                FALSE, FALSE, 0, 0, NULL, NULL, pool, NULL));
    }

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


svn_error_t *
svn_wc_get_ancestry (svn_stringbuf_t **url,
                     svn_revnum_t *rev,
                     svn_stringbuf_t *path,
                     apr_pool_t *pool)
{
  svn_wc_entry_t *ent;

  SVN_ERR (svn_wc_entry (&ent, path, pool));
  *url = svn_stringbuf_dup (ent->url, pool);
  *rev = ent->revision;

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc_add (svn_stringbuf_t *path,
            svn_stringbuf_t *copyfrom_url,
            svn_revnum_t copyfrom_rev,
            apr_pool_t *pool)
{
  svn_stringbuf_t *parent_dir, *basename;
  svn_wc_entry_t *orig_entry, *parent_entry;
  svn_pool_feedback_t *fbtable = svn_pool_get_feedback_vtable (pool);
  svn_boolean_t is_replace = FALSE;
  apr_status_t apr_err;
  apr_hash_t *atts = apr_hash_make (pool);
  enum svn_node_kind kind;

  /* Make sure something's there. */
  SVN_ERR (svn_io_check_path (path, &kind, pool));
  if (kind == svn_node_none)
    return svn_error_createf (SVN_ERR_WC_PATH_NOT_FOUND, 0, NULL, pool,
                              "'%s' not found", path->data);

  /* Get the original entry for this path if one exists (perhaps
     this is actually a replacement of a previously deleted thing). */
  if (svn_wc_entry (&orig_entry, path, pool))
    orig_entry = NULL;

  /* You can only add something that is not in revision control, or
     that is slated for deletion from revision control, unless, of
     course, you're specifying an addition with -history-; then it's
     okay for the object to be under version control already; it's not
     really new.  */
  if (orig_entry)
    {
      if ((! copyfrom_url)
          && (orig_entry->schedule != svn_wc_schedule_delete))
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
      if (orig_entry->schedule == svn_wc_schedule_delete)
        is_replace = TRUE;
    }
    
  /* Split off the basename from the parent directory. */
  svn_path_split (path, &parent_dir, &basename, svn_path_local_style, pool);
  if (svn_path_is_empty (parent_dir, svn_path_local_style))
    parent_dir = svn_stringbuf_create (".", pool);

  /* If a copy ancestor was given, put the proper ancestry info in a hash. */
  if (copyfrom_url)
    {
      apr_hash_set (atts, 
                    SVN_WC_ENTRY_ATTR_COPYFROM_URL, APR_HASH_KEY_STRING,
                    copyfrom_url);
      apr_hash_set (atts, 
                    SVN_WC_ENTRY_ATTR_COPYFROM_REV, APR_HASH_KEY_STRING,
                    svn_stringbuf_createf (pool, "%ld", copyfrom_rev));
    }

  /* Now, add the entry for this item to the parent_dir's
     entries file, marking it for addition. */
  SVN_ERR (svn_wc__entry_modify
           (parent_dir, basename,
            (SVN_WC__ENTRY_MODIFY_SCHEDULE
             | (copyfrom_url ? SVN_WC__ENTRY_MODIFY_COPIED : 0)
             | ((is_replace || copyfrom_url) ?
                  0 : SVN_WC__ENTRY_MODIFY_REVISION)
             | SVN_WC__ENTRY_MODIFY_KIND
             | SVN_WC__ENTRY_MODIFY_ATTRIBUTES),
            0, kind,
            svn_wc_schedule_add,
            FALSE,
            copyfrom_url ? TRUE : FALSE, 
            0, 0, NULL,
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
  else /* scheduling a directory for addition */
    {
      svn_wc_entry_t *p_entry;
      svn_stringbuf_t *p_path;
      apr_uint16_t flags;

      /* Get the entry for this directory's parent.  We need to snatch
         the ancestor path out of there. */
      SVN_ERR (svn_wc_entry (&p_entry, parent_dir, pool));
  
      /* Derive the parent path for our new addition here. */
      p_path = svn_stringbuf_dup (p_entry->url, pool);
      svn_path_add_component (p_path, basename, svn_path_url_style);
  
      /* Make sure this new directory has an admistrative subdirectory
         created inside of it */
      SVN_ERR (svn_wc__ensure_adm (path, p_path, 0, pool));
      
      /* Things we plan to change in this_dir. */
      flags = (SVN_WC__ENTRY_MODIFY_SCHEDULE
               | (copyfrom_url ? SVN_WC__ENTRY_MODIFY_COPIED : 0)
               | ((is_replace || copyfrom_url)
                     ? 0 : SVN_WC__ENTRY_MODIFY_REVISION)
               | SVN_WC__ENTRY_MODIFY_KIND
               | SVN_WC__ENTRY_MODIFY_ATTRIBUTES
               | SVN_WC__ENTRY_MODIFY_FORCE);

      /* And finally, make sure this entry is marked for addition in
         its own administrative directory. */
      SVN_ERR (svn_wc__entry_modify
               (path, NULL,
                flags,
                0, svn_node_dir,
                is_replace ? svn_wc_schedule_replace : svn_wc_schedule_add,
                FALSE,
                copyfrom_url ? TRUE : FALSE,
                0, 0,
                NULL,
                atts,  /* may or may not contain copyfrom args */
                pool, NULL));


      if (copyfrom_url)
        {
          /* If this new directory has ancestry, it's not enough to
             schedule it for addition with copyfrom args.  We also
             need to rewrite its ancestor-url, and rewrite the
             ancestor-url of ALL its children! */

          /* Figure out what the new url should be. */
          svn_stringbuf_t *url;
          SVN_ERR (svn_wc_entry (&parent_entry, parent_dir, pool));
          url = svn_stringbuf_dup (parent_entry->url, pool);
          svn_path_add_component (url, basename, svn_path_url_style);

          /* Change the entry urls recursively. */
          SVN_ERR (svn_wc__recursively_rewrite_urls (path, url, pool));

          /* Recursively add the 'copied' existence flag as well!  */
          SVN_ERR (mark_tree (path, SVN_WC__ENTRY_MODIFY_COPIED,
                              svn_wc_schedule_normal, TRUE, pool));

          /* Clean out the now-obsolete wcprops. */
          SVN_ERR (svn_wc__remove_wcprops (path, pool));
        }
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
revert_admin_things (svn_stringbuf_t *parent_dir,
                     svn_stringbuf_t *name,
                     svn_wc_entry_t *entry,
                     apr_uint64_t *modify_flags,
                     apr_pool_t *pool)
{
  svn_stringbuf_t *full_path, *thing, *pristine_thing;
  enum svn_node_kind kind;
  svn_boolean_t modified_p;
  svn_error_t *err;
  apr_time_t tstamp;

  full_path = svn_stringbuf_dup (parent_dir, pool);
  if (name && (strcmp (name->data, SVN_WC_ENTRY_THIS_DIR)))
    svn_path_add_component (full_path, name, svn_path_local_style);

  SVN_ERR (svn_wc_props_modified_p (&modified_p, full_path, pool));  
  if (modified_p)
    {
      SVN_ERR (svn_wc__prop_path (&thing, full_path, 0, pool)); 
      SVN_ERR (svn_wc__prop_base_path (&pristine_thing, full_path, 0, pool));
      err = svn_io_copy_file (pristine_thing, thing, pool);
      if (err)
        return svn_error_createf 
          (err->apr_err, 0, NULL, pool,
           "revert_admin_things:  Error restoring pristine props for '%s'", 
           full_path->data);
      SVN_ERR (svn_io_file_affected_time (&tstamp, thing, pool));

      /* Modify our entry structure. */
      *modify_flags |= SVN_WC__ENTRY_MODIFY_PROP_TIME;
      entry->prop_time = tstamp;
    }

  if (entry->kind == svn_node_file)
    {
      SVN_ERR (svn_io_check_path (full_path, &kind, pool));
      SVN_ERR (svn_wc_text_modified_p (&modified_p, full_path, pool));
      if ((modified_p) || (kind == svn_node_none))
        {
          /* If there are textual mods (or if the working file is
             missing altogether), copy the text-base out into
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

          /* Modify our entry structure. */
          *modify_flags |= SVN_WC__ENTRY_MODIFY_TEXT_TIME;
          entry->text_time = tstamp;
        }
    }

  if (entry->conflicted)
    {
      svn_stringbuf_t *rej_file = NULL, *prej_file = NULL, *rmfile;
      apr_status_t apr_err;

      /* Get the names of the reject files. */
      rej_file = apr_hash_get (entry->attributes, 
                               SVN_WC_ENTRY_ATTR_REJFILE,
                               APR_HASH_KEY_STRING);
      prej_file = apr_hash_get (entry->attributes, 
                                SVN_WC_ENTRY_ATTR_PREJFILE,
                                APR_HASH_KEY_STRING);

      /* Now blow them away. */
      if (rej_file)
        {
          rmfile = svn_stringbuf_dup (parent_dir, pool);
          svn_path_add_component (rmfile, rej_file, svn_path_local_style);
          apr_err = apr_file_remove (rmfile->data, pool);
          if (apr_err)
            return svn_error_createf
              (apr_err, 0, NULL, pool, 
               "Unable to remove '%s'", rmfile->data);
          *modify_flags |= SVN_WC__ENTRY_MODIFY_ATTRIBUTES;
        }
      if (prej_file)
        {
          rmfile = svn_stringbuf_dup (parent_dir, pool);
          svn_path_add_component (rmfile, prej_file, svn_path_local_style);
          apr_err = apr_file_remove (rmfile->data, pool);
          if (apr_err)
            return svn_error_createf
              (apr_err, 0, NULL, pool, 
               "Unable to remove '%s'", rmfile->data);
          *modify_flags |= SVN_WC__ENTRY_MODIFY_ATTRIBUTES;
        }

      /* Modify our entry structure. */
      *modify_flags |= SVN_WC__ENTRY_MODIFY_CONFLICTED;
      entry->conflicted = FALSE;
    }

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc_revert (svn_stringbuf_t *path,
               svn_boolean_t recursive,
               apr_pool_t *pool)
{
  enum svn_node_kind kind;
  svn_stringbuf_t *p_dir = NULL, *bname = NULL;
  svn_wc_entry_t *entry;
  svn_boolean_t wc_root, reverted = FALSE;
  apr_uint64_t modify_flags = 0;
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
      svn_path_split (path, &p_dir, &bname, svn_path_local_style, pool);
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
        SVN_ERR (svn_wc_remove_from_revision_control (p_dir, bname, 
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
      if (entry->kind == svn_node_file)
        SVN_ERR (revert_admin_things (p_dir, bname, entry, &modify_flags, 
                                      pool));
      if (entry->kind == svn_node_dir)
        SVN_ERR (revert_admin_things (path, NULL, entry, &modify_flags, 
                                      pool));
    }

  /* Deletions and replacements. */
  else if ((entry->schedule == svn_wc_schedule_delete) 
           || (entry->schedule == svn_wc_schedule_replace))
    {
      /* Revert the prop and text mods (if any). */
      if (entry->kind == svn_node_file)
        SVN_ERR (revert_admin_things (p_dir, bname, entry, &modify_flags,
                                      pool));
      if (entry->kind == svn_node_dir)
        SVN_ERR (revert_admin_things (path, NULL, entry, &modify_flags,
                                      pool));

      modify_flags |= SVN_WC__ENTRY_MODIFY_SCHEDULE;
    }

  /* All our disk modifications should be finished by now.  Let's
     update our entries files. */
  if (modify_flags)
    {
      const char *remove1 = NULL, *remove2 = NULL;

      /* Reset the schedule to normal. */
      if (! wc_root)
        {
          if (modify_flags & SVN_WC__ENTRY_MODIFY_ATTRIBUTES)
            {
              /* This *should* be the removal of the .rej and .prej
                 directives. */
              remove1 = SVN_WC_ENTRY_ATTR_REJFILE;
              remove2 = SVN_WC_ENTRY_ATTR_PREJFILE;
            }

          SVN_ERR (svn_wc__entry_modify
                   (p_dir,
                    bname,
                    modify_flags | SVN_WC__ENTRY_MODIFY_FORCE,
                    SVN_INVALID_REVNUM,
                    entry->kind,
                    svn_wc_schedule_normal,
                    entry->conflicted,
                    entry->copied,
                    entry->text_time,
                    entry->prop_time,
                    NULL, entry->attributes,
                    pool,
                    remove1,
                    remove2,
                    NULL));
        }

      /* For directories only. */
      if (entry->kind == svn_node_dir) 
        {
          /* Force recursion on replaced directories. */
          if (entry->schedule == svn_wc_schedule_replace)
            recursive = TRUE;

          if (modify_flags & SVN_WC__ENTRY_MODIFY_ATTRIBUTES)
            {
              /* This *should* be the removal of the .rej and .prej
                 directives. */
              remove1 = SVN_WC_ENTRY_ATTR_PREJFILE;
            }

          /* Reset the schedule to normal in the directory itself. */
          SVN_ERR (svn_wc__entry_modify
                   (path,
                    NULL,
                    SVN_WC__ENTRY_MODIFY_SCHEDULE 
                    | SVN_WC__ENTRY_MODIFY_CONFLICTED 
                    | SVN_WC__ENTRY_MODIFY_FORCE,
                    SVN_INVALID_REVNUM,
                    svn_node_none,
                    svn_wc_schedule_normal,
                    FALSE, FALSE,
                    0,
                    0,
                    NULL, NULL,
                    pool,
                    remove1,
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

      /* Remove text-base/NAME.svn-base, prop/NAME, prop-base/NAME.svn-base,
         wcprops/NAME */
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
      if (svn_path_is_empty (parent_dir, svn_path_local_style))
        svn_stringbuf_set (parent_dir, ".");

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

      /* Remove the entire administrative .svn area, thereby removing
         _this_ dir from revision control too. */
      SVN_ERR (svn_wc__adm_destroy (path, subpool));
      
      /* If caller wants us to recursively nuke everything on disk, go
         ahead, provided that there are no dangling local-mod files
         below */
      if (destroy_wf && (! left_a_file))
        {
          /* If the dir is *truly* empty (i.e. has no unversioned
             resources, all versioned files are gone, all .svn dirs are
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
  apr_file_t *file;
  svn_stringbuf_t *fname = svn_stringbuf_create(filename, pool);
  SVN_ERR (svn_wc__open_auth_file (&file, path, fname, APR_READ, pool));

  /* Read the file's contents into a stringbuf, allocated in POOL. */
  SVN_ERR (svn_string_from_aprfile (contents, file, pool));

  SVN_ERR (svn_wc__close_auth_file (file, path, fname,
                                    0 /* Don't sync */, pool));

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
     (In reality, this opens a file 'path/.svn/tmp/auth/filename'.) */
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
