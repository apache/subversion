/*
 * adm_ops.c: routines for affecting working copy administrative
 *            information.  NOTE: this code doesn't know where the adm
 *            info is actually stored.  Instead, generic handles to
 *            adm data are requested via a reference to some PATH
 *            (PATH being a regular, non-administrative directory or
 *            file in the working copy).
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
#include "log.h"
#include "adm_files.h"
#include "adm_ops.h"


/*** Finishing updates and commits. ***/


/* The main recursive body of svn_wc__do_update_cleanup. */
static svn_error_t *
recursively_tweak_entries (svn_stringbuf_t *dirpath,
                           const svn_stringbuf_t *base_url,
                           const svn_revnum_t new_rev,
                           apr_pool_t *pool)
{
  apr_hash_t *entries;
  apr_hash_index_t *hi;
  apr_pool_t *subpool = svn_pool_create (pool);
  
  /* Read DIRPATH's entries. */
  SVN_ERR (svn_wc_entries_read (&entries, dirpath, subpool));

  /* Tweak "this_dir" */
  SVN_ERR (svn_wc__tweak_entry (entries, 
                                svn_stringbuf_create (SVN_WC_ENTRY_THIS_DIR,
                                                      subpool),
                                base_url, new_rev, subpool));

  /* Recursively loop over all children. */
  for (hi = apr_hash_first (subpool, entries); hi; hi = apr_hash_next (hi))
    {
      const void *key;
      apr_ssize_t keylen;
      void *val;
      const char *name;
      svn_wc_entry_t *current_entry;
      svn_stringbuf_t *child_url = NULL;

      apr_hash_this (hi, &key, &keylen, &val);
      name = (const char *) key;
      current_entry = (svn_wc_entry_t *) val;

      /* Ignore the "this dir" entry. */
      if (! strcmp (name, SVN_WC_ENTRY_THIS_DIR))
        continue;

      /* Derive the new URL for the current (child) entry */
      if (base_url)
        {
          child_url = svn_stringbuf_dup (base_url, subpool);
          svn_path_add_component_nts (child_url, name);
        }
      
      /* If a file, tweak the entry. */
      if (current_entry->kind == svn_node_file)
        SVN_ERR (svn_wc__tweak_entry (entries, 
                                      svn_stringbuf_create (name, subpool),
                                      child_url, new_rev, subpool));
      
      /* If a dir, recurse. */
      else if (current_entry->kind == svn_node_dir)
        {
          svn_stringbuf_t *child_path = svn_stringbuf_dup (dirpath, subpool);
          svn_path_add_component_nts (child_path, name);
          SVN_ERR (recursively_tweak_entries 
                   (child_path, child_url, new_rev, subpool));
        }
    }

  /* Write a shiny new entries file to disk. */
  SVN_ERR (svn_wc__entries_write (entries, dirpath, subpool));

  /* Cleanup */
  svn_pool_destroy (subpool);

  return SVN_NO_ERROR;
}



svn_error_t *
svn_wc__do_update_cleanup (svn_stringbuf_t *path,
                           const svn_boolean_t recursive,
                           const svn_stringbuf_t *base_url,
                           const svn_revnum_t new_revision,
                           apr_pool_t *pool)
{
  apr_hash_t *entries;
  svn_wc_entry_t *entry;

  SVN_ERR (svn_wc_entry (&entry, path, pool));
  if (entry == NULL)
    return SVN_NO_ERROR;

  if (entry->kind == svn_node_file)
    {
      svn_stringbuf_t *parent, *basename;
      svn_path_split (path, &parent, &basename, pool);
      SVN_ERR (svn_wc_entries_read (&entries, parent, pool));
      SVN_ERR (svn_wc__tweak_entry (entries, basename,
                                    base_url, new_revision, pool));
      SVN_ERR (svn_wc__entries_write (entries, parent, pool));
    }

  else if (entry->kind == svn_node_dir)
    {
      if (! recursive) 
        {
          SVN_ERR (svn_wc_entries_read (&entries, path, pool));
          SVN_ERR (svn_wc__tweak_entry (entries,
                                        svn_stringbuf_create 
                                          (SVN_WC_ENTRY_THIS_DIR,
                                           pool),
                                        base_url, new_revision, pool));
          SVN_ERR (svn_wc__entries_write (entries, path, pool));
        }
      else
        SVN_ERR (recursively_tweak_entries (path, base_url,
                                            new_revision, pool));
    }

  else
    return svn_error_createf (SVN_ERR_UNKNOWN_NODE_KIND, 0, NULL, pool,
                              "Unrecognized node kind: '%s'\n", path->data);

  return SVN_NO_ERROR;
}



/* ### todo: Might just make more sense to expose the
   svn_wc_wcprop_get/set functions themselves than to have mindless
   wrappers around them. */
svn_error_t *
svn_wc_get_wc_prop (const char *path,
                    const char *name,
                    const svn_string_t **value,
                    apr_pool_t *pool)
{
  return svn_wc__wcprop_get (value, name, path, pool);
}

svn_error_t *
svn_wc_set_wc_prop (const char *path,
                    const char *name,
                    const svn_string_t *value,
                    apr_pool_t *pool)
{
  return svn_wc__wcprop_set (name, value, path, pool);
}



/* Process an absolute PATH that has just been successfully committed.
   
   Specifically, its working revision will be set to NEW_REVNUM;  if
   REV_DATE and REV_AUTHOR are both non-NULL, then three entry values
   will be set (overwritten):  'committed-rev', 'committed-date',
   'last-author'. 

   If RECURSE is true (assuming PATH is a directory), this post-commit
   processing will happen recursively down from PATH. 
*/
svn_error_t *
svn_wc_process_committed (svn_stringbuf_t *path,
                          svn_boolean_t recurse,
                          svn_revnum_t new_revnum,
                          const char *rev_date,
                          const char *rev_author,
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

      svn_error_clear_all (err);
      svn_path_split (path, &log_parent, &basename, pool);
      if (svn_path_is_empty (log_parent))
        svn_stringbuf_set (log_parent, ".");

      SVN_ERR (svn_wc__open_adm_file (&log_fp, log_parent, SVN_WC__ADM_LOG,
                                      (APR_WRITE|APR_APPEND|APR_CREATE),
                                      pool));

      /* Oh, and recursing at this point isn't really sensible. */
      recurse = FALSE;
    }
  else
    {
      /* PATH must be a dir */
      svn_stringbuf_t *pdir;
      svn_wc_entry_t tmp_entry;

      if (svn_path_is_empty (log_parent))
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

      tmp_entry.kind = svn_node_dir;
      tmp_entry.revision = new_revnum;
      SVN_ERR (svn_wc__entry_modify (pdir, basename, &tmp_entry, 
                                     SVN_WC__ENTRY_MODIFY_REVISION, pool));
    }

  logtag = svn_stringbuf_create ("", pool);

  /* Append a log command to set (overwrite) the 'committed-rev',
     'committed-date', and 'last-author' attributes in the entry.

     Note: it's important that this log command come *before* the
     LOG_COMMITTED command, because log_do_committed() might actually
     remove the entry! */
  if (rev_date && rev_author)
    svn_xml_make_open_tag (&logtag, pool, svn_xml_self_closing,
                           SVN_WC__LOG_MODIFY_ENTRY,
                           SVN_WC__LOG_ATTR_NAME, basename,
                           SVN_WC__ENTRY_ATTR_CMT_REV,
                           svn_stringbuf_create (revstr, pool),
                           SVN_WC__ENTRY_ATTR_CMT_DATE,
                           svn_stringbuf_create (rev_date, pool),
                           SVN_WC__ENTRY_ATTR_CMT_AUTHOR,
                           svn_stringbuf_create (rev_author, pool),
                           NULL);


  /* Regardless of whether it's a file or dir, the "main" logfile
     contains a command to bump the revision attribute (and
     timestamp.)  */
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
                                "process_committed: "
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
      apr_pool_t *subpool = svn_pool_create (pool);

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
          svn_path_add_component_nts (path, name);
          
          /* Recurse, but only allow further recursion if the child is
             a directory.  */
          SVN_ERR (svn_wc_process_committed 
                   (path, 
                    (current_entry->kind == svn_node_dir) ? TRUE : FALSE,
                    new_revnum, rev_date, rev_author, subpool));

          /* De-telescope the path. */
          svn_path_remove_component (path);
          
          svn_pool_clear (subpool);
        }

      svn_pool_destroy (subpool); 
   }

  return SVN_NO_ERROR;
}




/* Remove FILE if it exists and is a file.  If it does not exist, do
   nothing.  If it is not a file, error. */
static svn_error_t *
remove_file_if_present (svn_stringbuf_t *file, apr_pool_t *pool)
{
  svn_node_kind_t kind;

  /* Does this file exist?  If not, get outta here. */
  SVN_ERR (svn_io_check_path (file->data, &kind, pool));
  if (kind == svn_node_none)
    return SVN_NO_ERROR;

  /* Else, remove the file. */
  return svn_io_remove_file (file->data, pool);
}




/* Recursively mark a tree DIR for with a SCHEDULE and/or EXISTENCE
   flag and/or COPIED flag, depending on the state of MODIFY_FLAGS. */
static svn_error_t *
mark_tree (svn_stringbuf_t *dir, 
           apr_uint32_t modify_flags,
           svn_wc_schedule_t schedule,
           svn_boolean_t copied,
           svn_wc_notify_func_t notify_func,
           void *notify_baton,
           apr_pool_t *pool)
{
  apr_pool_t *subpool = svn_pool_create (pool);
  apr_hash_t *entries;
  apr_hash_index_t *hi;
  svn_stringbuf_t *fullpath = svn_stringbuf_dup (dir, pool);
  svn_wc_entry_t *entry; 

  /* Read the entries file for this directory. */
  SVN_ERR (svn_wc_entries_read (&entries, dir, pool));

  /* Mark each entry in the entries file. */
  for (hi = apr_hash_first (pool, entries); hi; hi = apr_hash_next (hi))
    {
      const void *key;
      apr_ssize_t klen;
      void *val;
      svn_stringbuf_t *basename;

      /* Get the next entry */
      apr_hash_this (hi, &key, &klen, &val);
      entry = (svn_wc_entry_t *) val;

      /* Skip "this dir".  */
      if (! strcmp ((const char *)key, SVN_WC_ENTRY_THIS_DIR))
        continue;
          
      basename = svn_stringbuf_create ((const char *) key, subpool);
      svn_path_add_component (fullpath, basename);

      /* If this is a directory, recurse. */
      if (entry->kind == svn_node_dir)
        SVN_ERR (mark_tree (fullpath, modify_flags,
                            schedule, copied,
                            notify_func, notify_baton,
                            subpool));

      /* Mark this entry. */
      entry->schedule = schedule;
      entry->copied = copied; 
      SVN_ERR (svn_wc__entry_modify (dir, basename, entry, 
                                     modify_flags, pool));

      /* Tell someone what we've done. */
      if (schedule == svn_wc_schedule_delete && notify_func != NULL)
        (*notify_func) (notify_baton, svn_wc_notify_delete, fullpath->data);

      /* Reset FULLPATH to just hold this dir's name. */
      svn_stringbuf_set (fullpath, dir->data);

      /* Clear our per-iteration pool. */
      svn_pool_clear (subpool);
    }
  
  /* Handle "this dir" for states that need it done post-recursion. */
  entry = apr_hash_get (entries, SVN_WC_ENTRY_THIS_DIR, APR_HASH_KEY_STRING);
  entry->schedule = schedule;
  entry->copied = copied;
  SVN_ERR (svn_wc__entry_modify (dir, NULL, entry, modify_flags, pool));
  
  /* Destroy our per-iteration pool. */
  svn_pool_destroy (subpool);
  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc_delete (svn_stringbuf_t *path,
               svn_wc_notify_func_t notify_func,
               void *notify_baton,
               apr_pool_t *pool)
{
  svn_stringbuf_t *dir, *basename;
  svn_wc_entry_t *entry;
  svn_boolean_t dir_unadded = FALSE;

  /* Get the entry for the path we are deleting. */
  SVN_ERR (svn_wc_entry (&entry, path, pool));
  if (! entry)
    return svn_error_createf
      (SVN_ERR_ENTRY_NOT_FOUND, 0, NULL, pool,
       "'%s' does not appear to be under revision control", path->data);
    
  if (entry->kind == svn_node_dir)
    {
      /* Special case, delete of a newly added dir. */
      if (entry->schedule == svn_wc_schedule_add)
        dir_unadded = TRUE;
      else
        /* Recursively mark a whole tree for deletion. */
        SVN_ERR (mark_tree (path, SVN_WC__ENTRY_MODIFY_SCHEDULE,
                            svn_wc_schedule_delete, FALSE,
                            notify_func, notify_baton,
                            pool));
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
      svn_path_split (path, &dir, &basename, pool);
      if (svn_path_is_empty (dir))
        svn_stringbuf_set (dir, ".");
      
      entry->schedule = svn_wc_schedule_delete;
      SVN_ERR (svn_wc__entry_modify (dir, basename, entry,
                                     SVN_WC__ENTRY_MODIFY_SCHEDULE, pool));
    }

  /* Report the deletion to the caller. */
  if (notify_func != NULL)
    (*notify_func) (notify_baton, svn_wc_notify_delete, path->data);

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
            svn_wc_notify_func_t notify_func,
            void *notify_baton,
            apr_pool_t *pool)
{
  svn_stringbuf_t *parent_dir, *basename;
  svn_wc_entry_t *orig_entry, *parent_entry, tmp_entry;
  svn_boolean_t is_replace = FALSE;
  enum svn_node_kind kind;
  apr_uint32_t modify_flags = 0;
  
  /* Make sure something's there. */
  SVN_ERR (svn_io_check_path (path->data, &kind, pool));
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
      if ((! copyfrom_url) && (orig_entry->schedule != svn_wc_schedule_delete))
        {
          return svn_error_createf 
            (SVN_ERR_ENTRY_EXISTS, 0, NULL, pool,
             "'%s' is already under revision control", path->data);
        }
      else if (orig_entry->kind != kind)
        {
          /* ### todo: At some point, we obviously don't want to block
             replacements where the node kind changes.  When this
             happens, svn_wc_revert() needs to learn how to revert
             this situation.  */
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
  svn_path_split (path, &parent_dir, &basename, pool);
  if (svn_path_is_empty (parent_dir))
    parent_dir = svn_stringbuf_create (".", pool);

  /* Init the modify flags. */
  modify_flags = SVN_WC__ENTRY_MODIFY_SCHEDULE | SVN_WC__ENTRY_MODIFY_KIND;;
  if (! (is_replace || copyfrom_url))
    modify_flags |= SVN_WC__ENTRY_MODIFY_REVISION;

  /* If a copy ancestor was given, put the proper ancestry info in a hash. */
  if (copyfrom_url)
    {
      tmp_entry.copyfrom_url = copyfrom_url;
      tmp_entry.copyfrom_rev = copyfrom_rev;
      tmp_entry.copied = TRUE;
      modify_flags |= SVN_WC__ENTRY_MODIFY_COPYFROM_URL;
      modify_flags |= SVN_WC__ENTRY_MODIFY_COPYFROM_REV;
      modify_flags |= SVN_WC__ENTRY_MODIFY_COPIED;
    }

  tmp_entry.revision = 0;
  tmp_entry.kind = kind;
  tmp_entry.schedule = svn_wc_schedule_add;

  /* Now, add the entry for this item to the parent_dir's
     entries file, marking it for addition. */
  SVN_ERR (svn_wc__entry_modify (parent_dir, basename, &tmp_entry, 
                                 modify_flags, pool));


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
        {
          svn_string_t mt_str;
          mt_str.data = mimetype;
          mt_str.len = strlen(mimetype);
          SVN_ERR (svn_wc_prop_set (SVN_PROP_MIME_TYPE, &mt_str, path->data,
                                    pool));
        }
    }  
  else /* scheduling a directory for addition */
    {
      svn_wc_entry_t *p_entry;
      svn_stringbuf_t *p_path;

      /* Get the entry for this directory's parent.  We need to snatch
         the ancestor path out of there. */
      SVN_ERR (svn_wc_entry (&p_entry, parent_dir, pool));
  
      /* Derive the parent path for our new addition here. */
      p_path = svn_stringbuf_dup (p_entry->url, pool);
      svn_path_add_component (p_path, basename);
  
      /* Make sure this new directory has an admistrative subdirectory
         created inside of it */
      SVN_ERR (svn_wc__ensure_adm (path, p_path, 0, pool));
      
      /* We're making the same mods we made above, but this time we'll
         force the scheduling. */
      modify_flags |= SVN_WC__ENTRY_MODIFY_FORCE;
      tmp_entry.schedule = is_replace 
                           ? svn_wc_schedule_replace 
                           : svn_wc_schedule_add;
      SVN_ERR (svn_wc__entry_modify (path, NULL, &tmp_entry, 
                                     modify_flags, pool));

      if (copyfrom_url)
        {
          /* If this new directory has ancestry, it's not enough to
             schedule it for addition with copyfrom args.  We also
             need to rewrite its ancestor-url, and rewrite the
             ancestor-url of ALL its children!

             We're doing this because our current commit model (for
             hysterical raisins, presumably) assumes an entry's URL is
             correct before commit -- i.e. the URL is not tweaked in
             the post-commit bumping process.  We might want to change
             this model someday. */

          /* Figure out what the new url should be. */
          svn_stringbuf_t *url;
          SVN_ERR (svn_wc_entry (&parent_entry, parent_dir, pool));
          url = svn_stringbuf_dup (parent_entry->url, pool);
          svn_path_add_component (url, basename);

          /* Change the entry urls recursively (but not the working rev). */
          SVN_ERR (svn_wc__do_update_cleanup (path, TRUE, /* recursive */
                                              url, SVN_INVALID_REVNUM, pool));

          /* Recursively add the 'copied' existence flag as well!  */
          SVN_ERR (mark_tree (path, SVN_WC__ENTRY_MODIFY_COPIED,
                              svn_wc_schedule_normal, TRUE,
                              NULL, NULL, /* N/A cuz we aren't deleting */
                              pool));

          /* Clean out the now-obsolete wcprops. */
          SVN_ERR (svn_wc__remove_wcprops (path, pool));
        }
    }

  /* Report the addition to the caller. */
  if (notify_func != NULL)
    (*notify_func) (notify_baton, svn_wc_notify_add, path->data);

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


/* Return a new wrapping of error ERR regarding the revert subcommand,
   while doing VERB on PATH.  Use POOL for allocations.
*/
static svn_error_t *
revert_error (svn_error_t *err,
              svn_stringbuf_t *path,
              const char *verb,
              apr_pool_t *pool)
{
  return svn_error_quick_wrap 
    (err, apr_psprintf (pool, "revert: error %s for `%s'", verb, path->data));
}


/* Revert ENTRY for NAME in directory PARENT_DIR, altering
   *MODIFY_FLAGS to indicate what parts of the entry were reverted
   (for example, if property changes were reverted, then set the
   SVN_WC__ENTRY_MODIFY_PROP_TIME bit in MODIFY_FLAGS).

   Use POOL for any temporary allocations.*/
static svn_error_t *
revert_admin_things (svn_stringbuf_t *parent_dir,
                     svn_stringbuf_t *name,
                     svn_wc_entry_t *entry,
                     apr_uint32_t *modify_flags,
                     apr_pool_t *pool)
{
  svn_stringbuf_t *fullpath, *thing, *pthing;
  enum svn_node_kind kind;
  svn_boolean_t modified_p;
  svn_error_t *err;
  apr_time_t tstamp;

  /* Build the full path of the thing we're reverting. */
  fullpath = svn_stringbuf_dup (parent_dir, pool);
  if (name && (strcmp (name->data, SVN_WC_ENTRY_THIS_DIR)))
    svn_path_add_component (fullpath, name);

  /* Check for prop changes. */
  SVN_ERR (svn_wc_props_modified_p (&modified_p, fullpath, pool));  
  if (modified_p)
    {
      SVN_ERR (svn_wc__prop_path (&thing, fullpath, 0, pool)); 
      SVN_ERR (svn_wc__prop_base_path (&pthing, fullpath, 0, pool));

      /* If there is a pristing property file, copy it out as the
         working property file, else just remove the working property
         file. */
      SVN_ERR (svn_io_check_path (pthing->data, &kind, pool));
      if (kind == svn_node_file)
        {
          if ((err = svn_io_set_file_read_write (thing->data, FALSE, pool)))
            return revert_error (err, fullpath, "restoring props", pool);
          if ((err = svn_io_copy_file (pthing->data, thing->data, FALSE, pool)))
            return revert_error (err, fullpath, "restoring props", pool);
          SVN_ERR (svn_io_file_affected_time (&tstamp, thing, pool));
          entry->prop_time = tstamp;
        }
      else
        {
          if ((err = svn_io_set_file_read_write (thing->data, FALSE, pool)))
            return revert_error (err, fullpath, "removing props", pool);

          if ((err = svn_io_remove_file (thing->data, pool)))
            return revert_error (err, fullpath, "removing props", pool);
        }

      /* Modify our entry structure. */
      *modify_flags |= SVN_WC__ENTRY_MODIFY_PROP_TIME;
    }

  if (entry->kind == svn_node_file)
    {
      SVN_ERR (svn_io_check_path (fullpath->data, &kind, pool));
      SVN_ERR (svn_wc_text_modified_p (&modified_p, fullpath, pool));
      if ((modified_p) || (kind == svn_node_none))
        {
          /* If there are textual mods (or if the working file is
             missing altogether), copy the text-base out into
             the working copy, and update the timestamp in the entries
             file. */
          svn_wc_keywords_t *keywords;
          enum svn_wc__eol_style eol_style;
          const char *eol;
          pthing = svn_wc__text_base_path (fullpath, 0, pool);

          SVN_ERR (svn_wc__get_eol_style 
                   (&eol_style, &eol, fullpath->data, pool));
          SVN_ERR (svn_wc__get_keywords 
                   (&keywords, fullpath->data, NULL, pool));

          /* When copying the text-base out to the working copy, make
             sure to do any eol translations or keyword substitutions,
             as dictated by the property values.  If these properties
             are turned off, then this is just a normal copy. */
          if ((err = svn_wc_copy_and_translate (pthing->data,
                                                fullpath->data,
                                                eol, FALSE, /* don't repair */
                                                keywords,
                                                TRUE, /* expand keywords */
                                                pool)))
            return revert_error (err, fullpath, "restoring text", pool);

          /* Modify our entry structure. */
          SVN_ERR (svn_io_file_affected_time (&tstamp, fullpath, pool));
          *modify_flags |= SVN_WC__ENTRY_MODIFY_TEXT_TIME;
          entry->text_time = tstamp;
        }
    }

  /* Remove conflict state (and conflict files), if any. */
  if (entry->prejfile || entry->conflict_old 
      || entry->conflict_new || entry->conflict_wrk)
    {
      svn_stringbuf_t *rmfile;
    
      /* Handle the three possible text conflict files. */
      if (entry->conflict_old)
        {
          rmfile = svn_stringbuf_dup (parent_dir, pool);
          svn_path_add_component (rmfile, entry->conflict_old);
          SVN_ERR (remove_file_if_present (rmfile, pool));
          *modify_flags |= SVN_WC__ENTRY_MODIFY_CONFLICT_OLD;
        }
    
      if (entry->conflict_new)
        {
          rmfile = svn_stringbuf_dup (parent_dir, pool);
          svn_path_add_component (rmfile, entry->conflict_new);
          SVN_ERR (remove_file_if_present (rmfile, pool));
          *modify_flags |= SVN_WC__ENTRY_MODIFY_CONFLICT_NEW;
        }
    
      if (entry->conflict_wrk)
        {
          rmfile = svn_stringbuf_dup (parent_dir, pool);
          svn_path_add_component (rmfile, entry->conflict_wrk);
          SVN_ERR (remove_file_if_present (rmfile, pool));
          *modify_flags |= SVN_WC__ENTRY_MODIFY_CONFLICT_WRK;
        }
    
      /* Remove the prej-file if the entry lists one (and it exists) */
      if (entry->prejfile)
        {
          rmfile = svn_stringbuf_dup (parent_dir, pool);
          svn_path_add_component (rmfile, entry->prejfile);
          SVN_ERR (remove_file_if_present (rmfile, pool));
          *modify_flags |= SVN_WC__ENTRY_MODIFY_PREJFILE;
        }
    }

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc_revert (svn_stringbuf_t *path,
               svn_boolean_t recursive,
               svn_wc_notify_func_t notify_func,
               void *notify_baton,
               apr_pool_t *pool)
{
  enum svn_node_kind kind;
  svn_stringbuf_t *p_dir = NULL, *bname = NULL;
  svn_wc_entry_t *entry;
  svn_boolean_t wc_root, reverted = FALSE;
  apr_uint32_t modify_flags = 0;

  /* Safeguard 1:  is this a versioned resource? */
  SVN_ERR (svn_wc_entry (&entry, path, pool));
  if (! entry)
    return svn_error_createf 
      (SVN_ERR_ENTRY_NOT_FOUND, 0, NULL, pool,
       "Cannot revert '%s' -- not a versioned resource", path->data);

  /* Safeguard 2:  can we handle this node kind? */
  if ((entry->kind != svn_node_file) && (entry->kind != svn_node_dir))
    return svn_error_createf 
      (SVN_ERR_UNSUPPORTED_FEATURE, 0, NULL, pool,
       "Cannot revert '%s' -- unsupported entry node kind", path->data);

  /* Safeguard 3:  can we deal with the node kind of PATH current in
     the working copy? */
  SVN_ERR (svn_io_check_path (path->data, &kind, pool));
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
      svn_path_split (path, &p_dir, &bname, pool);
      if (svn_path_is_empty (p_dir))
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
      /* Force recursion on replaced directories. */
      if ((entry->kind == svn_node_dir)
          && (entry->schedule == svn_wc_schedule_replace))
        recursive = TRUE;

      /* Reset the schedule to normal. */
      entry->schedule = svn_wc_schedule_normal;
      entry->conflict_old = NULL;
      entry->conflict_new = NULL;
      entry->conflict_wrk = NULL;
      entry->prejfile = NULL;
      if (! wc_root)
        SVN_ERR (svn_wc__entry_modify (p_dir, bname, entry,
                                       modify_flags 
                                       | SVN_WC__ENTRY_MODIFY_FORCE,
                                       pool));

      /* For directories, reset the schedule to normal in the
         directory itself. */
      if (entry->kind == svn_node_dir) 
        SVN_ERR (svn_wc__entry_modify (path, NULL, entry,
                                       SVN_WC__ENTRY_MODIFY_SCHEDULE 
                                       | SVN_WC__ENTRY_MODIFY_PREJFILE
                                       | SVN_WC__ENTRY_MODIFY_FORCE,
                                       pool));

      /* Note that this was reverted. */
      reverted = TRUE;
    }

  /* If PATH was reverted, tell our client that. */
  if ((notify_func != NULL) && reverted)
    (*notify_func) (notify_baton, svn_wc_notify_revert, path->data);

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
          svn_path_add_component_nts (full_entry_path, keystring);

          /* Revert the entry. */
          SVN_ERR (svn_wc_revert (full_entry_path, TRUE,
                                  notify_func, notify_baton, pool));

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
      svn_path_add_component (full_path, name);

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
      svn_path_split (full_path, &parent_dir, &basename, pool);
      if (svn_path_is_empty (parent_dir))
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
                {
                  svn_error_clear_all (err);
                  left_a_file = TRUE;
                }
              else if (err)
                return err;
            }
          else if (current_entry_name && (current_entry->kind == svn_node_dir))
            {
              svn_stringbuf_t *this_dir = svn_stringbuf_create
                (SVN_WC_ENTRY_THIS_DIR, subpool);
              svn_stringbuf_t *entrypath = svn_stringbuf_dup (path, subpool);
              svn_path_add_component (entrypath, current_entry_name);
              err = svn_wc_remove_from_revision_control (entrypath, this_dir,
                                                         destroy_wf, subpool);
              if (err && (err->apr_err == SVN_ERR_WC_LEFT_LOCAL_MOD))
                {
                  svn_error_clear_all (err);
                  left_a_file = TRUE;
                }
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



/* Helper for svn_wc_resolve_conflict */
static svn_error_t *
attempt_deletion (svn_stringbuf_t *parent_dir,
                  svn_stringbuf_t *basename,
                  apr_pool_t *pool)
{
  svn_stringbuf_t *full_path = svn_stringbuf_dup (parent_dir, pool);
  svn_path_add_component (full_path, basename);
 
  return remove_file_if_present (full_path, pool);
}


svn_error_t *
svn_wc_resolve_conflict (svn_stringbuf_t *path,
                         svn_wc_notify_func_t notify_func,
                         void *notify_baton,
                         apr_pool_t *pool)
{
  svn_stringbuf_t *parent, *basename;
  svn_boolean_t text_conflict, prop_conflict;
  svn_wc_entry_t *entry = NULL;

  /* Feh, ignoring the return value here.  We just want to know
     whether we got the entry or not. */
  svn_wc_entry (&entry, path, pool);
  if (! entry)
    return svn_error_createf (SVN_ERR_ENTRY_NOT_FOUND, 0, NULL, pool,
                              "Not under version control: '%s'", path->data);

  svn_path_split (path, &parent, &basename, pool);

  /* Sanity check: see if libsvn_wc thinks this item is in a state of
     conflict at all.   If not, just go home.*/
  SVN_ERR (svn_wc_conflicted_p (&text_conflict, &prop_conflict,
                                parent, entry, pool));
  if ((! text_conflict) && (! prop_conflict))
    return SVN_NO_ERROR;

  /* Yes indeed, being able to map a function over a list would be nice. */
  if (entry->conflict_old)
    SVN_ERR (attempt_deletion (parent, entry->conflict_old, pool));
  if (entry->conflict_new)
    SVN_ERR (attempt_deletion (parent, entry->conflict_new, pool));
  if (entry->conflict_wrk)
    SVN_ERR (attempt_deletion (parent, entry->conflict_wrk, pool));
  if (entry->prejfile)
    SVN_ERR (attempt_deletion (parent, entry->prejfile, pool));

  if (notify_func)
    {
      /* Sanity check:  see if libsvn_wc *still* thinks this item is in a
         state of conflict.  If not, report the successful resolution.  */     
      SVN_ERR (svn_wc_conflicted_p (&text_conflict, &prop_conflict,
                                    parent, entry, pool));
      if ((! text_conflict) && (! prop_conflict))
        (*notify_func) (notify_baton, svn_wc_notify_resolve, path->data);
    }
          
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
                                      svn_stringbuf_create (basename, pool));

              SVN_ERR (svn_wc_set_auth_file (childpath, TRUE,
                                             filename, contents, pool));
            }
        }
    }


  return SVN_NO_ERROR;
}





/* 
 * local variables:
 * eval: (load-file "../../tools/dev/svn-dev.el")
 * end:
 */
