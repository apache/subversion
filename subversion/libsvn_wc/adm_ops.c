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
#include "svn_io.h"

#include "wc.h"
#include "log.h"
#include "adm_files.h"
#include "adm_ops.h"
#include "entries.h"
#include "props.h"
#include "translate.h"


/*** Finishing updates and commits. ***/


/* The main recursive body of svn_wc__do_update_cleanup. */
static svn_error_t *
recursively_tweak_entries (svn_wc_adm_access_t *dirpath,
                           const char *base_url,
                           const svn_revnum_t new_rev,
                           apr_pool_t *pool)
{
  apr_hash_t *entries;
  apr_hash_index_t *hi;
  apr_pool_t *subpool = svn_pool_create (pool);
  
  /* Read DIRPATH's entries. */
  SVN_ERR (svn_wc_entries_read (&entries, dirpath, TRUE, subpool));

  /* Tweak "this_dir" */
  SVN_ERR (svn_wc__tweak_entry (entries, SVN_WC_ENTRY_THIS_DIR,
                                base_url, new_rev,
                                svn_wc_adm_access_pool (dirpath)));

  /* Recursively loop over all children. */
  for (hi = apr_hash_first (subpool, entries); hi; hi = apr_hash_next (hi))
    {
      const void *key;
      void *val;
      const char *name;
      svn_wc_entry_t *current_entry;
      const char *child_url = NULL;

      apr_hash_this (hi, &key, NULL, &val);
      name = key;
      current_entry = val;

      /* Ignore the "this dir" entry. */
      if (! strcmp (name, SVN_WC_ENTRY_THIS_DIR))
        continue;

      /* Derive the new URL for the current (child) entry */
      if (base_url)
        child_url = svn_path_url_add_component (base_url, name, subpool);
      
      /* If a file (or deleted dir), tweak the entry. */
      if ((current_entry->kind == svn_node_file)
          || (current_entry->deleted))
        SVN_ERR (svn_wc__tweak_entry (entries, name,
                                      child_url, new_rev,
                                      svn_wc_adm_access_pool (dirpath)));
      
      /* If a dir, recurse. */
      else if (current_entry->kind == svn_node_dir)        
        {
          svn_wc_adm_access_t *child_access;
          const char *child_path
            = svn_path_join (svn_wc_adm_access_path (dirpath), name, subpool);
          SVN_ERR (svn_wc_adm_retrieve (&child_access, dirpath, child_path,
                                        subpool));
          SVN_ERR (recursively_tweak_entries 
                   (child_access, child_url, new_rev, subpool));
        }
    }

  /* Write a shiny new entries file to disk. */
  SVN_ERR (svn_wc__entries_write (entries, dirpath, subpool));

  /* Cleanup */
  svn_pool_destroy (subpool);

  return SVN_NO_ERROR;
}



svn_error_t *
svn_wc__do_update_cleanup (const char *path,
                           svn_wc_adm_access_t *adm_access,
                           const svn_boolean_t recursive,
                           const char *base_url,
                           const svn_revnum_t new_revision,
                           apr_pool_t *pool)
{
  apr_hash_t *entries;
  const svn_wc_entry_t *entry;

  SVN_ERR (svn_wc_entry (&entry, path, adm_access, TRUE, pool));
  if (entry == NULL)
    return SVN_NO_ERROR;

  if (entry->kind == svn_node_file)
    {
      const char *parent, *base_name;
      svn_wc_adm_access_t *dir_access;
      svn_path_split_nts (path, &parent, &base_name, pool);
      SVN_ERR (svn_wc_adm_retrieve (&dir_access, adm_access, parent, pool));
      SVN_ERR (svn_wc_entries_read (&entries, dir_access, TRUE, pool));
      SVN_ERR (svn_wc__tweak_entry (entries, base_name,
                                    base_url, new_revision,
                                    svn_wc_adm_access_pool (dir_access)));
      SVN_ERR (svn_wc__entries_write (entries, dir_access, pool));
    }

  else if (entry->kind == svn_node_dir)
    {
      svn_wc_adm_access_t *dir_access;
      SVN_ERR (svn_wc_adm_retrieve (&dir_access, adm_access, path, pool));

      if (! recursive) 
        {
          SVN_ERR (svn_wc_entries_read (&entries, dir_access, TRUE, pool));
          SVN_ERR (svn_wc__tweak_entry (entries, SVN_WC_ENTRY_THIS_DIR,
                                        base_url, new_revision,
                                        svn_wc_adm_access_pool (dir_access)));
          SVN_ERR (svn_wc__entries_write (entries, adm_access, pool));
        }
      else
        SVN_ERR (recursively_tweak_entries (dir_access, base_url,
                                            new_revision, pool));
    }

  else
    return svn_error_createf (SVN_ERR_NODE_UNKNOWN_KIND, 0, NULL,
                              "Unrecognized node kind: '%s'\n", path);

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc_process_committed (const char *path,
                          svn_wc_adm_access_t *adm_access,
                          svn_boolean_t recurse,
                          svn_revnum_t new_revnum,
                          const char *rev_date,
                          const char *rev_author,
                          apr_array_header_t *wcprop_changes,
                          apr_pool_t *pool)
{
  apr_status_t apr_err;
  const char *base_name;
  svn_stringbuf_t *logtags;
  apr_file_t *log_fp = NULL;
  char *revstr = apr_psprintf (pool, "%" SVN_REVNUM_T_FMT, new_revnum);
  svn_stringbuf_t *checksum = NULL;

  SVN_ERR (svn_wc_adm_write_check (adm_access));

  /* Set PATH's working revision to NEW_REVNUM; if REV_DATE and
     REV_AUTHOR are both non-NULL, then set the 'committed-rev',
     'committed-date', and 'last-author' entry values; and set the
     checksum if a file. */

  /* Open a log file in the administrative directory */
  SVN_ERR (svn_wc__open_adm_file (&log_fp,
                                  svn_wc_adm_access_path (adm_access),
                                  SVN_WC__ADM_LOG,
                                  (APR_WRITE | APR_APPEND | APR_CREATE),
                                  pool));

  base_name = svn_path_is_child (svn_wc_adm_access_path (adm_access), path,
                                 pool);
  if (base_name)
    {
      /* PATH must be some sort of file */
      const char *tmp_text_base;

      /* We know that the new text base is sitting in the adm tmp area
         by now, because the commit succeeded. */
      tmp_text_base = svn_wc__text_base_path (path, TRUE, pool);

      /* It would be more efficient to compute the checksum as part of
         some other operation that has to process all the bytes anyway
         (such as copying or translation).  But that would make a lot
         of other code more complex, since the relevant copy and/or
         translation operations happened elsewhere, a long time ago.
         If we were to obtain the checksum then/there, we'd still have
         to somehow preserve it until now/here, which would result in
         unexpected and hard-to-maintain dependencies.  Ick.

         So instead we just do the checksum from scratch.  Ick. */
      svn_io_file_checksum (&checksum, tmp_text_base, pool);

      /* Oh, and recursing at this point isn't really sensible. */
      recurse = FALSE;
    }
  else
    {
      /* PATH must be a dir */
      svn_wc_entry_t tmp_entry;

      base_name = SVN_WC_ENTRY_THIS_DIR;
      tmp_entry.kind = svn_node_dir;
      tmp_entry.revision = new_revnum;
      SVN_ERR (svn_wc__entry_modify (adm_access, base_name, &tmp_entry, 
                                     SVN_WC__ENTRY_MODIFY_REVISION, pool));
    }

  logtags = svn_stringbuf_create ("", pool);

  /* Append a log command to set (overwrite) the 'committed-rev',
     'committed-date', 'last-author', and possibly `checksum'
     attributes in the entry.

     Note: it's important that this log command come *before* the
     LOG_COMMITTED command, because log_do_committed() might actually
     remove the entry! */
  if (rev_date && rev_author)
    svn_xml_make_open_tag (&logtags, pool, svn_xml_self_closing,
                           SVN_WC__LOG_MODIFY_ENTRY,
                           SVN_WC__LOG_ATTR_NAME, base_name,
                           SVN_WC__ENTRY_ATTR_CMT_REV,
                           revstr,
                           SVN_WC__ENTRY_ATTR_CMT_DATE,
                           rev_date,
                           SVN_WC__ENTRY_ATTR_CMT_AUTHOR,
                           rev_author,
                           NULL);

  if (checksum)
    svn_xml_make_open_tag (&logtags, pool, svn_xml_self_closing,
                           SVN_WC__LOG_MODIFY_ENTRY,
                           SVN_WC__LOG_ATTR_NAME, base_name,
                           SVN_WC__ENTRY_ATTR_CHECKSUM,
                           checksum->data,
                           NULL);

  /* Regardless of whether it's a file or dir, the "main" logfile
     contains a command to bump the revision attribute (and
     timestamp.)  */
  svn_xml_make_open_tag (&logtags, pool, svn_xml_self_closing,
                         SVN_WC__LOG_COMMITTED,
                         SVN_WC__LOG_ATTR_NAME, base_name,
                         SVN_WC__LOG_ATTR_REVISION, 
                         revstr,
                         NULL);


  /* Do wcprops in the same log txn as revision, etc. */
  if (wcprop_changes && (wcprop_changes->nelts > 0))
    {
      int i;

      for (i = 0; i < wcprop_changes->nelts; i++)
        {
          svn_prop_t *prop = APR_ARRAY_IDX (wcprop_changes, i, svn_prop_t *);

          svn_xml_make_open_tag (&logtags, pool, svn_xml_self_closing,
                                 SVN_WC__LOG_MODIFY_WCPROP,
                                 SVN_WC__LOG_ATTR_NAME,
                                 base_name,
                                 SVN_WC__LOG_ATTR_PROPNAME,
                                 prop->name,
                                 prop->value ? SVN_WC__LOG_ATTR_PROPVAL : NULL,
                                 prop->value ? prop->value->data : NULL,
                                 NULL);
        }
    }

  apr_err = apr_file_write_full (log_fp, logtags->data, logtags->len, NULL);
  if (apr_err)
    {
      apr_file_close (log_fp);
      return svn_error_createf (apr_err, 0, NULL,
                                "process_committed: "
                                "error writing %s's log file", 
                                path);
    }
      
  SVN_ERR (svn_wc__close_adm_file (log_fp, svn_wc_adm_access_path (adm_access),
                                   SVN_WC__ADM_LOG,
                                   TRUE, /* sync */
                                   pool));


  /* Run the log file we just created. */
  SVN_ERR (svn_wc__run_log (adm_access, pool));
            
  if (recurse)
    {
      apr_hash_t *entries;
      apr_hash_index_t *hi;
      apr_pool_t *subpool = svn_pool_create (pool);

      /* Read PATH's entries;  this is the absolute path. */
      SVN_ERR (svn_wc_entries_read (&entries, adm_access, TRUE, pool));

      /* Recursively loop over all children. */
      for (hi = apr_hash_first (pool, entries); hi; hi = apr_hash_next (hi))
        {
          const void *key;
          void *val;
          const char *name;
          const svn_wc_entry_t *current_entry;
          const char *this_path;
          svn_wc_adm_access_t *child_access;

          apr_hash_this (hi, &key, NULL, &val);
          name = key;
          current_entry = val;
          
          /* Ignore the "this dir" entry. */
          if (! strcmp (name, SVN_WC_ENTRY_THIS_DIR))
            continue;
          
          /* Create child path by telescoping the main path. */
          this_path = svn_path_join (path, name, subpool);

          if (current_entry->kind == svn_node_dir)
             SVN_ERR (svn_wc_adm_retrieve (&child_access, adm_access, this_path,
                                           subpool));
          else
             child_access = adm_access;
          
          /* Recurse, but only allow further recursion if the child is
             a directory.  Pass null for wcprop_changes, because the
             ones present in the current call are only applicable to
             this one committed item. */
          SVN_ERR (svn_wc_process_committed 
                   (this_path, child_access,
                    (current_entry->kind == svn_node_dir) ? TRUE : FALSE,
                    new_revnum, rev_date, rev_author, NULL, subpool));

          svn_pool_clear (subpool);
        }

      svn_pool_destroy (subpool); 
   }

  return SVN_NO_ERROR;
}




/* Remove FILE if it exists and is a file.  If it does not exist, do
   nothing.  If it is not a file, error. */
static svn_error_t *
remove_file_if_present (const char *file, apr_pool_t *pool)
{
  svn_node_kind_t kind;

  /* Does this file exist?  If not, get outta here. */
  SVN_ERR (svn_io_check_path (file, &kind, pool));
  if (kind == svn_node_none)
    return SVN_NO_ERROR;

  /* Else, remove the file. */
  return svn_io_remove_file (file, pool);
}




/* Recursively mark a tree ADM_ACCESS for with a SCHEDULE and/or EXISTENCE
   flag and/or COPIED flag, depending on the state of MODIFY_FLAGS. */
static svn_error_t *
mark_tree (svn_wc_adm_access_t *adm_access, 
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
  svn_wc_entry_t *entry; 

  /* Read the entries file for this directory. */
  SVN_ERR (svn_wc_entries_read (&entries, adm_access, FALSE, pool));

  /* Mark each entry in the entries file. */
  for (hi = apr_hash_first (pool, entries); hi; hi = apr_hash_next (hi))
    {
      const char *fullpath;
      const void *key;
      void *val;
      const char *base_name;

      /* Get the next entry */
      apr_hash_this (hi, &key, NULL, &val);
      entry = val;

      /* Skip "this dir".  */
      if (! strcmp ((const char *)key, SVN_WC_ENTRY_THIS_DIR))
        continue;
          
      base_name = key;
      fullpath = svn_path_join (svn_wc_adm_access_path (adm_access), base_name,
                                subpool);

      /* If this is a directory, recurse. */
      if (entry->kind == svn_node_dir)
        {
          svn_wc_adm_access_t *child_access;
          SVN_ERR (svn_wc_adm_retrieve (&child_access, adm_access, fullpath,
                                        subpool));
          SVN_ERR (mark_tree (child_access, modify_flags,
                              schedule, copied,
                              notify_func, notify_baton,
                              subpool));
        }

      /* Mark this entry.  This modifys the cache, but it's OK we are about
         to write it out again. */
      entry->schedule = schedule;
      entry->copied = copied; 
      SVN_ERR (svn_wc__entry_modify (adm_access, base_name, entry, 
                                     modify_flags, subpool));

      /* Tell someone what we've done. */
      if (schedule == svn_wc_schedule_delete && notify_func != NULL)
        (*notify_func) (notify_baton, fullpath, svn_wc_notify_delete,
                        svn_node_unknown,
                        NULL,
                        svn_wc_notify_state_unknown,
                        svn_wc_notify_state_unknown,
                        SVN_INVALID_REVNUM);

      /* Clear our per-iteration pool. */
      svn_pool_clear (subpool);
    }
  
  /* Handle "this dir" for states that need it done post-recursion. */
  entry = apr_hash_get (entries, SVN_WC_ENTRY_THIS_DIR, APR_HASH_KEY_STRING);

  /* Uncommitted directories (schedule add) that are to be scheduled for
     deletion are a special case, they don't need to be changed as they
     will be removed from their parent's entry list. */
  if (! (entry->schedule == svn_wc_schedule_add
         && schedule == svn_wc_schedule_delete))
  {
    if (modify_flags & SVN_WC__ENTRY_MODIFY_SCHEDULE)
      entry->schedule = schedule;
    if (modify_flags & SVN_WC__ENTRY_MODIFY_COPIED)
      entry->copied = copied;
    SVN_ERR (svn_wc__entry_modify (adm_access, NULL, entry, modify_flags,
                                   subpool));
  }
  
  /* Destroy our per-iteration pool. */
  svn_pool_destroy (subpool);
  return SVN_NO_ERROR;
}

/* Remove/erase PATH from the working copy. This involves deleting PATH
 * from the physical filesystem. PATH is assumed to be an unversioned file
 * or directory.
 */
static svn_error_t *
erase_unversioned_from_wc (const char *path,
                           apr_pool_t *pool)
{
  svn_node_kind_t kind;

  SVN_ERR (svn_io_check_path (path, &kind, pool));
  switch (kind)
    {
    case svn_node_none:
      /* Nothing to do. */
      break;

    default:
      /* ### TODO: what do we do here? To handle Unix symlinks we
         fallthrough to svn_node_file... gulp! */

    case svn_node_file:
      SVN_ERR (svn_io_remove_file (path, pool));
      break;

    case svn_node_dir:
      SVN_ERR (svn_io_remove_dir (path, pool));
      break;
    }

  return SVN_NO_ERROR;
}

/* Remove/erase PATH from the working copy. For files this involves
 * deletion from the physical filesystem.  For directories it involves the
 * deletion from the filesystem of all unversioned children, and all
 * versioned children that are files. By the time we get here, added but
 * not committed items will have been scheduled for detetion which means
 * they have become unversioned.
 *
 * The result is that all that remains are versioned directories, each with
 * its .svn directory and .svn contents.
 *
 * KIND is the node kind appropriate for PATH
 */
static svn_error_t *
erase_from_wc (const char *path,
               svn_wc_adm_access_t *adm_access,
               svn_node_kind_t kind,
               apr_pool_t *pool)
{
  /* Check that the item exists in the wc. */
  svn_node_kind_t wc_kind;
  SVN_ERR (svn_io_check_path (path, &wc_kind, pool));
  if (wc_kind == svn_node_none)
    return SVN_NO_ERROR;

  switch (kind)
    {
    default:
      /* ### TODO: what do we do here? */
      break;

    case svn_node_file:
      SVN_ERR (svn_io_remove_file (path, pool));
      break;

    case svn_node_dir:
      {
        apr_hash_t *ver, *unver;
        apr_hash_index_t *hi;
        svn_wc_adm_access_t *dir_access;

        /* ### Suspect that an iteration or recursion subpool would be
           good here. */

        /* First handle the versioned items, this is better (probably) than
           simply using svn_io_get_dirents for everything as it avoids the
           need to do svn_io_check_path on each versioned item */
        SVN_ERR (svn_wc_adm_retrieve (&dir_access, adm_access, path, pool));
        SVN_ERR (svn_wc_entries_read (&ver, dir_access, FALSE, pool));
        for (hi = apr_hash_first (pool, ver); hi; hi = apr_hash_next (hi))
          {
            const void *key;
            void *val;
            const char *name;
            const svn_wc_entry_t *entry;
            const char *down_path;

            apr_hash_this (hi, &key, NULL, &val);
            name = key;
            entry = val;

            if (!strcmp (name, SVN_WC_ENTRY_THIS_DIR))
              continue;

            down_path = svn_path_join (path, name, pool);
            SVN_ERR (erase_from_wc (down_path, dir_access, entry->kind, pool));
          }

        /* Now handle any remaining unversioned items */
        SVN_ERR (svn_io_get_dirents (&unver, path, pool));
        for (hi = apr_hash_first (pool, unver); hi; hi = apr_hash_next (hi))
          {
            const void *key;
            const char *name;
            const char *down_path;

            apr_hash_this (hi, &key, NULL, NULL);
            name = key;

            /* The admin directory will show up, we don't want to delete it */
            if (!strcmp (name, SVN_WC_ADM_DIR_NAME))
              continue;

            /* Versioned directories will show up, don't delete those either */
            if (apr_hash_get (ver, name, APR_HASH_KEY_STRING))
              continue;

            down_path = svn_path_join (path, name, pool);
            SVN_ERR (erase_unversioned_from_wc (down_path, pool));
          }
      }
      /* ### TODO: move this dir into parent's .svn area */
      break;
    }

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc_delete (const char *path,
               svn_wc_adm_access_t *adm_access,
               svn_wc_notify_func_t notify_func,
               void *notify_baton,
               apr_pool_t *pool)
{
  svn_wc_adm_access_t *dir_access;
  const svn_wc_entry_t *entry;
  const char *parent, *base_name;
  svn_boolean_t was_schedule_add;
  svn_node_kind_t was_kind;
  svn_boolean_t was_deleted;

  /* ### do we really need to retrieve? */
  SVN_ERR (svn_wc_adm_probe_retrieve (&dir_access, adm_access, path, pool));

  SVN_ERR (svn_wc_entry (&entry, path, dir_access, FALSE, pool));
  if (!entry)
    return erase_unversioned_from_wc (path, pool);
    
  /* Note: Entries caching?  What happens to this entry when the entries
     file is updated?  Lets play safe and copy the values */
  was_schedule_add = entry->schedule == svn_wc_schedule_add;
  was_kind = entry->kind;

  svn_path_split_nts (path, &parent, &base_name, pool);

  if (was_kind == svn_node_dir)
    {
      svn_wc_adm_access_t *parent_access;
      apr_hash_t *entries;
      const svn_wc_entry_t *entry_in_parent;

      /* The deleted state is only avaliable in the entry in parent's
         entries file */
      SVN_ERR (svn_wc_adm_retrieve (&parent_access, adm_access, parent, pool));
      SVN_ERR (svn_wc_entries_read (&entries, parent_access, TRUE, pool));
      entry_in_parent = apr_hash_get (entries, base_name, APR_HASH_KEY_STRING);
      was_deleted = entry_in_parent->deleted;
      
      if (was_schedule_add && !was_deleted)
        {
          /* Deleting a directory that has been added but not yet
             committed is easy, just remove the adminstrative dir. */

          SVN_ERR (svn_wc_remove_from_revision_control
                   (dir_access, SVN_WC_ENTRY_THIS_DIR, FALSE, pool));
        }
      else
        {
          /* Recursively mark a whole tree for deletion. */
          SVN_ERR (mark_tree (dir_access, SVN_WC__ENTRY_MODIFY_SCHEDULE,
                              svn_wc_schedule_delete, FALSE,
                              notify_func, notify_baton,
                              pool));
        }
    }
  
  if (!(was_kind == svn_node_dir && was_schedule_add && !was_deleted))
    {
      /* We need to mark this entry for deletion in its parent's entries
         file, so we split off base_name from the parent path, then fold in
         the addition of a delete flag. */
      svn_wc_entry_t tmp_entry;
      
      tmp_entry.schedule = svn_wc_schedule_delete;
      SVN_ERR (svn_wc__entry_modify (adm_access, base_name, &tmp_entry,
                                     SVN_WC__ENTRY_MODIFY_SCHEDULE, pool));
    }

  /* Report the deletion to the caller. */
  if (notify_func != NULL)
    (*notify_func) (notify_baton, path, svn_wc_notify_delete,
                    svn_node_unknown,
                    NULL,
                    svn_wc_notify_state_unknown,
                    svn_wc_notify_state_unknown,
                    SVN_INVALID_REVNUM);

  /* By the time we get here, anything that was scheduled to be added has
     become unversioned */
  if (was_schedule_add)
    SVN_ERR (erase_unversioned_from_wc (path, pool));
  else
    SVN_ERR (erase_from_wc (path, adm_access, was_kind, pool));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc_get_ancestry (char **url,
                     svn_revnum_t *rev,
                     const char *path,
                     svn_wc_adm_access_t *adm_access,
                     apr_pool_t *pool)
{
  const svn_wc_entry_t *ent;

  SVN_ERR (svn_wc_entry (&ent, path, adm_access, FALSE, pool));
  *url = apr_pstrdup (pool, ent->url);
  *rev = ent->revision;

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc_add (const char *path,
            svn_wc_adm_access_t *parent_access,
            const char *copyfrom_url,
            svn_revnum_t copyfrom_rev,
            svn_wc_notify_func_t notify_func,
            void *notify_baton,
            apr_pool_t *pool)
{
  const char *parent_dir, *base_name;
  const svn_wc_entry_t *orig_entry, *parent_entry;
  svn_wc_entry_t tmp_entry;
  svn_boolean_t is_replace = FALSE;
  svn_node_kind_t kind;
  apr_uint32_t modify_flags = 0;
  const char *mimetype = NULL;
  svn_boolean_t executable = FALSE;
  svn_error_t *err;
  svn_wc_adm_access_t *adm_access;
  
  /* Make sure something's there. */
  SVN_ERR (svn_io_check_path (path, &kind, pool));
  if (kind == svn_node_none)
    return svn_error_createf (SVN_ERR_WC_PATH_NOT_FOUND, 0, NULL,
                              "'%s' not found", path);

  /* Get the original entry for this path if one exists (perhaps
     this is actually a replacement of a previously deleted thing).
     
     Note that this is one of the few functions that is allowed to see
    'deleted' entries;  it's totally fine to have an entry that is
     scheduled for addition and still previously 'deleted'.  */
  err = svn_wc_adm_probe_retrieve (&adm_access, parent_access, path, pool);
  if (! err)
    err = svn_wc_entry (&orig_entry, path, parent_access, TRUE, pool);
  if (err)
    {
      svn_error_clear (err);
      orig_entry = NULL;
    }

  /* You can only add something that is not in revision control, or
     that is slated for deletion from revision control, or has been
     previously 'deleted', unless, of course, you're specifying an
     addition with -history-; then it's okay for the object to be
     under version control already; it's not really new.  */
  if (orig_entry)
    {
      if ((! copyfrom_url) 
          && (orig_entry->schedule != svn_wc_schedule_delete)
          && (! orig_entry->deleted))
        {
          return svn_error_createf 
            (SVN_ERR_ENTRY_EXISTS, 0, NULL,
             "'%s' is already under revision control", path);
        }
      else if (orig_entry->kind != kind)
        {
          /* ### todo: At some point, we obviously don't want to block
             replacements where the node kind changes.  When this
             happens, svn_wc_revert() needs to learn how to revert
             this situation.  At present we are using a specific node-change
             error so that clients can detect it. */
          return svn_error_createf 
            (SVN_ERR_WC_NODE_KIND_CHANGE, 0, NULL,
             "Could not replace '%s' with a node of a differing type"
             " -- commit the deletion, update the parent, and then add '%s'",
             path, path);
        }
      if (orig_entry->schedule == svn_wc_schedule_delete)
        is_replace = TRUE;
    }

  /* Split off the base_name from the parent directory. */
  svn_path_split_nts (path, &parent_dir, &base_name, pool);
  SVN_ERR (svn_wc_entry (&parent_entry, parent_dir, parent_access, FALSE,
                         pool));
  if (! parent_entry)
    return svn_error_createf 
      (SVN_ERR_ENTRY_NOT_FOUND, 0, NULL,
       "Could not find parent directory's entry while trying to add '%s'",
       path);
  if (parent_entry->schedule == svn_wc_schedule_delete)
    return svn_error_createf 
      (SVN_ERR_WC_SCHEDULE_CONFLICT, 0, NULL,
       "Can not add '%s' to a parent directory scheduled for deletion",
       path);

  /* Init the modify flags. */
  modify_flags = SVN_WC__ENTRY_MODIFY_SCHEDULE | SVN_WC__ENTRY_MODIFY_KIND;
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
  SVN_ERR (svn_wc__entry_modify (parent_access, base_name, &tmp_entry, 
                                 modify_flags, pool));


  /* If this is a replacement, we need to reset the properties for
     PATH. */
  if (orig_entry)
    {
      const char *prop_path;
      SVN_ERR (svn_wc__prop_path (&prop_path, path, FALSE, pool));
      SVN_ERR (remove_file_if_present (prop_path, pool));
    }

  if (kind == svn_node_file)
    {
      /* If this is a new file being added instead of a file copy,
         then try to detect the mime-type of this file and set
         svn:executable if the file is executable.  Otherwise, use the
         values in the original file. */
      if (! copyfrom_url)
        {
          SVN_ERR (svn_io_detect_mimetype (&mimetype, path, pool));
          if (mimetype)
            {
              svn_string_t mt_str;
              mt_str.data = mimetype;
              mt_str.len = strlen(mimetype);
              SVN_ERR (svn_wc_prop_set (SVN_PROP_MIME_TYPE, &mt_str, path,
                                        parent_access, pool));
            }

          /* Set svn:executable if the new addition is executable. */
          SVN_ERR (svn_io_is_file_executable (&executable, path, pool));
          if (executable)
            {
              svn_string_t emptystr;
              emptystr.data = "";
              emptystr.len = 0;
              SVN_ERR (svn_wc_prop_set (SVN_PROP_EXECUTABLE, &emptystr, path,
                                        parent_access, pool));
            }
        }
    }
  else /* scheduling a directory for addition */
    {
      if (! copyfrom_url)
        {
          const svn_wc_entry_t *p_entry; /* ### why not use parent_entry? */
          const char *new_url;

          /* Get the entry for this directory's parent.  We need to snatch
             the ancestor path out of there. */
          SVN_ERR (svn_wc_entry (&p_entry, parent_dir, parent_access, FALSE,
                                 pool));
  
          /* Derive the parent path for our new addition here. */
          new_url = svn_path_url_add_component (p_entry->url, base_name, pool);
  
          /* Make sure this new directory has an admistrative subdirectory
             created inside of it */
          SVN_ERR (svn_wc__ensure_adm (path, new_url, 0, pool));
        }
      else
        {
          /* When we are called with the copyfrom arguments set and with
             the admin directory already in existance, then the dir will
             contain the copyfrom settings.  So we need to pass the
             copyfrom arguments to the ensure call. */
          SVN_ERR (svn_wc__ensure_adm (path, copyfrom_url, 
                                       copyfrom_rev, pool));
        }
      
      /* We want the locks to persist, so use the access baton's pool */
      if (! orig_entry || orig_entry->deleted)
        SVN_ERR (svn_wc_adm_open (&adm_access, parent_access, path, TRUE, TRUE,
                                  svn_wc_adm_access_pool (parent_access)));

      /* We're making the same mods we made above, but this time we'll
         force the scheduling. */
      modify_flags |= SVN_WC__ENTRY_MODIFY_FORCE;
      tmp_entry.schedule = is_replace 
                           ? svn_wc_schedule_replace 
                           : svn_wc_schedule_add;
      SVN_ERR (svn_wc__entry_modify (adm_access, NULL, &tmp_entry, 
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
          const char *new_url 
            = svn_path_join (parent_entry->url, 
                             svn_path_uri_encode (base_name, pool),
                             pool);

          /* Change the entry urls recursively (but not the working rev). */
          SVN_ERR (svn_wc__do_update_cleanup (path, adm_access, TRUE, new_url, 
                                              SVN_INVALID_REVNUM, pool));

          /* Recursively add the 'copied' existence flag as well!  */
          SVN_ERR (mark_tree (adm_access, SVN_WC__ENTRY_MODIFY_COPIED,
                              svn_wc_schedule_normal, TRUE,
                              NULL, NULL, /* N/A cuz we aren't deleting */
                              pool));

          /* Clean out the now-obsolete wcprops. */
          SVN_ERR (svn_wc__remove_wcprops (adm_access, pool));
        }
    }

  /* Report the addition to the caller. */
  if (notify_func != NULL)
    (*notify_func) (notify_baton, path, svn_wc_notify_add,
                    kind,
                    mimetype,
                    svn_wc_notify_state_unknown,
                    svn_wc_notify_state_unknown,
                    SVN_INVALID_REVNUM);

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
              const char *path,
              const char *verb,
              apr_pool_t *pool)
{
  return svn_error_quick_wrap 
    (err, apr_psprintf (pool, "revert: error %s for `%s'", verb, path));
}


/* Revert ENTRY for NAME in directory PARENT_DIR, altering
   *MODIFY_FLAGS to indicate what parts of the entry were reverted
   (for example, if property changes were reverted, then set the
   SVN_WC__ENTRY_MODIFY_PROP_TIME bit in MODIFY_FLAGS).

   Use POOL for any temporary allocations.*/
static svn_error_t *
revert_admin_things (svn_wc_adm_access_t *adm_access,
                     const char *name,
                     svn_wc_entry_t *entry,
                     apr_uint32_t *modify_flags,
                     apr_pool_t *pool)
{
  const char *fullpath, *thing, *base_thing;
  svn_node_kind_t kind;
  svn_boolean_t modified_p;
  svn_error_t *err;
  apr_time_t tstamp;

  /* Build the full path of the thing we're reverting. */
  fullpath = svn_wc_adm_access_path (adm_access);
  if (name && (strcmp (name, SVN_WC_ENTRY_THIS_DIR) != 0))
    fullpath = svn_path_join (fullpath, name, pool);

  /* Check for prop changes. */
  SVN_ERR (svn_wc_props_modified_p (&modified_p, fullpath, adm_access, pool));  
  if (modified_p)
    {
      svn_node_kind_t working_props_kind;

      SVN_ERR (svn_wc__prop_path (&thing, fullpath, 0, pool)); 
      SVN_ERR (svn_wc__prop_base_path (&base_thing, fullpath, 0, pool));

      /* There may be a base props file but no working props file, if
         the mod was that the working file was `R'eplaced by a new
         file with no props. */
      SVN_ERR (svn_io_check_path (thing, &working_props_kind, pool));

      /* If there is a pristing property file, copy it out as the
         working property file, else just remove the working property
         file. */
      SVN_ERR (svn_io_check_path (base_thing, &kind, pool));
      if (kind == svn_node_file)
        {
          if ((working_props_kind == svn_node_file)
              && (err = svn_io_set_file_read_write (thing, FALSE, pool)))
            return revert_error (err, fullpath, "restoring props", pool);

          if ((err = svn_io_copy_file (base_thing, thing, FALSE, pool)))
            return revert_error (err, fullpath, "restoring props", pool);

          SVN_ERR (svn_io_file_affected_time (&tstamp, thing, pool));
          entry->prop_time = tstamp;
        }
      else if (working_props_kind == svn_node_file)
        {
          if ((err = svn_io_set_file_read_write (thing, FALSE, pool)))
            return revert_error (err, fullpath, "removing props", pool);

          if ((err = svn_io_remove_file (thing, pool)))
            return revert_error (err, fullpath, "removing props", pool);
        }

      /* Modify our entry structure. */
      *modify_flags |= SVN_WC__ENTRY_MODIFY_PROP_TIME;
    }
  else if (entry->schedule == svn_wc_schedule_replace)
    {
      /* Edge case: we're reverting a replacement, and
         svn_wc_props_modified_p thinks there's no property mods.
         However, because of the scheduled replacement,
         svn_wc_props_modified_p is deliberately ignoring the
         base-props; it's "no" answer simply means that there are no
         working props.  It's *still* possible that the base-props
         exist, however, from the original replaced file.  If they do,
         then we need to restore them. */
      SVN_ERR (svn_wc__prop_path (&thing, fullpath, 0, pool)); 
      SVN_ERR (svn_wc__prop_base_path (&base_thing, fullpath, 0, pool));
      SVN_ERR (svn_io_check_path (base_thing, &kind, pool));

      if ((err = svn_io_copy_file (base_thing, thing, FALSE, pool)))
        return revert_error (err, fullpath, "restoring props", pool);
      
      SVN_ERR (svn_io_file_affected_time (&tstamp, thing, pool));
      entry->prop_time = tstamp;
      *modify_flags |= SVN_WC__ENTRY_MODIFY_PROP_TIME;
    }

  if (entry->kind == svn_node_file)
    {
      SVN_ERR (svn_io_check_path (fullpath, &kind, pool));
      SVN_ERR (svn_wc_text_modified_p (&modified_p, fullpath, adm_access, 
                                       pool));
      if ((modified_p) || (kind == svn_node_none))
        {
          /* If there are textual mods (or if the working file is
             missing altogether), copy the text-base out into
             the working copy, and update the timestamp in the entries
             file. */
          svn_wc_keywords_t *keywords;
          const char *eol;
          base_thing = svn_wc__text_base_path (fullpath, 0, pool);

          SVN_ERR (svn_wc__get_eol_style (NULL, &eol, fullpath, pool));
          SVN_ERR (svn_wc__get_keywords (&keywords, fullpath, adm_access, NULL,
                                         pool));

          /* When copying the text-base out to the working copy, make
             sure to do any eol translations or keyword substitutions,
             as dictated by the property values.  If these properties
             are turned off, then this is just a normal copy. */
          if ((err = svn_wc_copy_and_translate (base_thing,
                                                fullpath,
                                                eol, FALSE, /* don't repair */
                                                keywords,
                                                TRUE, /* expand keywords */
                                                pool)))
            return revert_error (err, fullpath, "restoring text", pool);

          /* If necessary, tweak the new working file's executable bit. */
          SVN_ERR (svn_wc__maybe_set_executable (NULL, fullpath, pool));

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
      const char *rmfile;
    
      /* Handle the three possible text conflict files. */
      if (entry->conflict_old)
        {
          rmfile = svn_path_join (svn_wc_adm_access_path (adm_access),
                                  entry->conflict_old, pool);
          SVN_ERR (remove_file_if_present (rmfile, pool));
          *modify_flags |= SVN_WC__ENTRY_MODIFY_CONFLICT_OLD;
        }
    
      if (entry->conflict_new)
        {
          rmfile = svn_path_join (svn_wc_adm_access_path (adm_access),
                                  entry->conflict_new, pool);
          SVN_ERR (remove_file_if_present (rmfile, pool));
          *modify_flags |= SVN_WC__ENTRY_MODIFY_CONFLICT_NEW;
        }
    
      if (entry->conflict_wrk)
        {
          rmfile = svn_path_join (svn_wc_adm_access_path (adm_access),
                                  entry->conflict_wrk, pool);
          SVN_ERR (remove_file_if_present (rmfile, pool));
          *modify_flags |= SVN_WC__ENTRY_MODIFY_CONFLICT_WRK;
        }
    
      /* Remove the prej-file if the entry lists one (and it exists) */
      if (entry->prejfile)
        {
          rmfile = svn_path_join (svn_wc_adm_access_path (adm_access),
                                  entry->prejfile, pool);
          SVN_ERR (remove_file_if_present (rmfile, pool));
          *modify_flags |= SVN_WC__ENTRY_MODIFY_PREJFILE;
        }
    }

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc_revert (const char *path,
               svn_wc_adm_access_t *parent_access,
               svn_boolean_t recursive,
               svn_wc_notify_func_t notify_func,
               void *notify_baton,
               apr_pool_t *pool)
{
  svn_node_kind_t kind;
  const char *p_dir = NULL, *bname = NULL;
  const svn_wc_entry_t *entry;
  svn_wc_entry_t *tmp_entry;
  svn_boolean_t wc_root = FALSE, reverted = FALSE;
  apr_uint32_t modify_flags = 0;
  svn_wc_adm_access_t *dir_access;

  SVN_ERR (svn_wc_adm_probe_retrieve (&dir_access, parent_access, path, pool));

  /* Safeguard 1:  is this a versioned resource? */
  SVN_ERR (svn_wc_entry (&entry, path, dir_access, FALSE, pool));
  if (! entry)
    return svn_error_createf 
      (SVN_ERR_ENTRY_NOT_FOUND, 0, NULL,
       "Cannot revert '%s' -- not a versioned resource", path);

  /* Safeguard 2:  can we handle this node kind? */
  if ((entry->kind != svn_node_file) && (entry->kind != svn_node_dir))
    return svn_error_createf 
      (SVN_ERR_UNSUPPORTED_FEATURE, 0, NULL,
       "Cannot revert '%s' -- unsupported entry node kind", path);

  /* Safeguard 3:  can we deal with the node kind of PATH current in
     the working copy? */
  SVN_ERR (svn_io_check_path (path, &kind, pool));
  if ((kind != svn_node_none)
      && (kind != svn_node_file)
      && (kind != svn_node_dir))
    return svn_error_createf 
      (SVN_ERR_UNSUPPORTED_FEATURE, 0, NULL,
       "Cannot revert '%s' -- unsupported node kind in working copy", path);

  /* For directories, determine if PATH is a WC root so that we can
     tell if it is safe to split PATH into a parent directory and
     basename.  For files, we always do this split.  */
  if (kind == svn_node_dir)
    SVN_ERR (svn_wc_is_wc_root (&wc_root, path, dir_access, pool));
  if (! wc_root)
    {
      /* Split the base_name from the parent path. */
      svn_path_split_nts (path, &p_dir, &bname, pool);
    }

  tmp_entry = svn_wc_entry_dup (entry, pool);

  /* Additions. */
  if (entry->schedule == svn_wc_schedule_add)
    {
      /* Before removing item from revision control, notice if the
         entry is in a 'deleted' state; this is critical for
         directories, where this state only exists in its parent's
         entry. */
      svn_boolean_t was_deleted = FALSE;
      const char *parent, *basey;

      svn_path_split_nts (path, &parent, &basey, pool);
      if (entry->kind == svn_node_file)
        {
          was_deleted = entry->deleted;
          SVN_ERR (svn_wc_remove_from_revision_control (parent_access, bname,
                                                        FALSE, pool));
        }
      else if (entry->kind == svn_node_dir)
        {
          apr_hash_t *entries;
          const svn_wc_entry_t *parents_entry;
          SVN_ERR (svn_wc_entries_read (&entries, parent_access, TRUE, pool));
          parents_entry = apr_hash_get (entries, basey, APR_HASH_KEY_STRING);
          if (parents_entry)
            was_deleted = parents_entry->deleted;
          if (kind == svn_node_none)
            {
              /* Schedule add but missing, just remove the entry */
              svn_wc__entry_remove (entries, basey);
              SVN_ERR (svn_wc__entries_write (entries, parent_access, pool));
            }
          else
            SVN_ERR (svn_wc_remove_from_revision_control 
                     (dir_access, SVN_WC_ENTRY_THIS_DIR, FALSE, pool));
        }
      else  /* Else it's `none', or something exotic like a symlink... */
        return svn_error_createf
          (SVN_ERR_NODE_UNKNOWN_KIND, 0, NULL,
           "Unknown or unexpected kind for path %s", path);

      /* Recursivity is taken care of by svn_wc_remove_from_revision_control, 
         and we've definitely reverted PATH at this point. */
      recursive = FALSE;
      reverted = TRUE;

      /* If the removed item was *also* in a 'deleted' state, make
         sure we leave just a plain old 'deleted' entry behind in the
         parent. */
      if (was_deleted)
        {
          svn_wc_entry_t *tmpentry;
          tmpentry = apr_pcalloc (pool, sizeof(*tmpentry));
          tmpentry->kind = entry->kind;
          tmpentry->deleted = TRUE;

          if (entry->kind == svn_node_dir)
            SVN_ERR (svn_wc__entry_modify (parent_access, basey, tmpentry,
                                           SVN_WC__ENTRY_MODIFY_KIND
                                           | SVN_WC__ENTRY_MODIFY_DELETED,
                                           pool));
          else
              SVN_ERR (svn_wc__entry_modify (parent_access, bname, tmpentry,
                                             SVN_WC__ENTRY_MODIFY_KIND
                                             | SVN_WC__ENTRY_MODIFY_DELETED,
                                             pool));
        }
    }

  /* Regular prop and text edit. */
  else if (entry->schedule == svn_wc_schedule_normal)
    {
      /* Revert the prop and text mods (if any). */
      if (entry->kind == svn_node_file)
        SVN_ERR (revert_admin_things (parent_access, bname, tmp_entry,
                                      &modify_flags, pool));
      if (entry->kind == svn_node_dir)
        SVN_ERR (revert_admin_things (dir_access, NULL, tmp_entry,
                                      &modify_flags, pool));
    }

  /* Deletions and replacements. */
  else if ((entry->schedule == svn_wc_schedule_delete) 
           || (entry->schedule == svn_wc_schedule_replace))
    {
      /* Revert the prop and text mods (if any). */
      if (entry->kind == svn_node_file)
        SVN_ERR (revert_admin_things (parent_access, bname, tmp_entry,
                                      &modify_flags, pool));
      if (entry->kind == svn_node_dir)
        SVN_ERR (revert_admin_things (dir_access, NULL, tmp_entry,
                                      &modify_flags, pool));

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
      tmp_entry->schedule = svn_wc_schedule_normal;
      tmp_entry->conflict_old = NULL;
      tmp_entry->conflict_new = NULL;
      tmp_entry->conflict_wrk = NULL;
      tmp_entry->prejfile = NULL;
      if (! wc_root)
        SVN_ERR (svn_wc__entry_modify (parent_access, bname, tmp_entry,
                                       modify_flags 
                                       | SVN_WC__ENTRY_MODIFY_FORCE,
                                       pool));

      /* For directories, reset the schedule to normal in the
         directory itself. */
      if (entry->kind == svn_node_dir) 
        {
          SVN_ERR (svn_wc__entry_modify (dir_access, NULL, tmp_entry,
                                         SVN_WC__ENTRY_MODIFY_SCHEDULE 
                                         | SVN_WC__ENTRY_MODIFY_PREJFILE
                                         | SVN_WC__ENTRY_MODIFY_FORCE,
                                         pool));
        }

      /* Note that this was reverted. */
      reverted = TRUE;
    }

  /* If PATH was reverted, tell our client that. */
  if ((notify_func != NULL) && reverted)
    (*notify_func) (notify_baton, path, svn_wc_notify_revert,
                    svn_node_unknown,
                    NULL,  /* ### any way to get the mime type? */
                    svn_wc_notify_state_unknown,
                    svn_wc_notify_state_unknown,
                    SVN_INVALID_REVNUM);
 
  /* Finally, recurse if requested. */
  if (recursive && (entry->kind == svn_node_dir))
    {
      apr_hash_t *entries;
      apr_hash_index_t *hi;
      apr_pool_t *subpool = svn_pool_create (pool);

      SVN_ERR (svn_wc_entries_read (&entries, dir_access, FALSE, pool));
      for (hi = apr_hash_first (pool, entries); hi; hi = apr_hash_next (hi))
        {
          const void *key;
          const char *keystring;
          const char *full_entry_path;
          
          /* Get the next entry */
          apr_hash_this (hi, &key, NULL, NULL);
          keystring = key;

          /* Skip "this dir" */
          if (! strcmp (keystring, SVN_WC_ENTRY_THIS_DIR))
            continue;

          /* Add the entry name to FULL_ENTRY_PATH. */
          full_entry_path = svn_path_join (path, keystring, subpool);

          /* Revert the entry. */
          SVN_ERR (svn_wc_revert (full_entry_path, dir_access, TRUE,
                                  notify_func, notify_baton, subpool));

          svn_pool_clear (subpool);
        }

        svn_pool_destroy (subpool);
    }
  
  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc_get_pristine_copy_path (const char *path,
                               const char **pristine_path,
                               apr_pool_t *pool)
{
  *pristine_path = svn_wc__text_base_path (path, FALSE, pool);
  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc_remove_from_revision_control (svn_wc_adm_access_t *adm_access,
                                     const char *name,
                                     svn_boolean_t destroy_wf,
                                     apr_pool_t *pool)
{
  svn_error_t *err;
  svn_boolean_t is_file;
  svn_boolean_t left_something = FALSE;
  apr_pool_t *subpool = svn_pool_create (pool);
  apr_hash_t *entries = NULL;
  const char *full_path = apr_pstrdup (pool,
                                       svn_wc_adm_access_path (adm_access));

  /* NAME is either a file's basename or SVN_WC_ENTRY_THIS_DIR. */
  is_file = (strcmp (name, SVN_WC_ENTRY_THIS_DIR)) ? TRUE : FALSE;
      
  if (is_file)
    {
      svn_boolean_t text_modified_p;
      full_path = svn_path_join (full_path, name, pool);

      if (destroy_wf)
        /* Check for local mods. before removing entry */
        SVN_ERR (svn_wc_text_modified_p (&text_modified_p, full_path,
                                         adm_access, subpool));

      /* Remove NAME from PATH's entries file: */
      SVN_ERR (svn_wc_entries_read (&entries, adm_access, FALSE, pool));
      svn_wc__entry_remove (entries, name);
      SVN_ERR (svn_wc__entries_write (entries, adm_access, pool));

      /* Remove text-base/NAME.svn-base, prop/NAME, prop-base/NAME.svn-base,
         wcprops/NAME */
      {
        const char *svn_thang;

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

      /* If we were asked to destory the working file, do so unless
         it has local mods. */
      if (destroy_wf)
        {
          if (text_modified_p)  /* don't kill local mods */
            {
              return svn_error_create (SVN_ERR_WC_LEFT_LOCAL_MOD,
                                       0, NULL, "");
            }
          else
            {
              /* The working file is still present; remove it. */
              SVN_ERR (remove_file_if_present (full_path, subpool));
            }
        }

    }  /* done with file case */

  else /* looking at THIS_DIR */
    {
      apr_hash_index_t *hi;
      /* ### sanity check:  check 2 places for DELETED flag? */

      /* Remove self from parent's entries file, but only if parent is
         a working copy.  If it's not, that's fine, we just move on. */
      {
        const char *parent_dir, *base_name;
        svn_boolean_t is_root;

        SVN_ERR (svn_wc_is_wc_root (&is_root, full_path, adm_access, pool));

        /* If full_path is not the top of a wc, then its parent
           directory is also a working copy and has an entry for
           full_path.  We need to remove that entry: */
        if (! is_root)
          {
            svn_wc_adm_access_t *parent_access;

            svn_path_split_nts (full_path, &parent_dir, &base_name, pool);
            
            SVN_ERR (svn_wc_adm_retrieve (&parent_access, adm_access,
                                          parent_dir, pool));
            SVN_ERR (svn_wc_entries_read (&entries, parent_access, FALSE,
                                          pool));
            svn_wc__entry_remove (entries, base_name);
            SVN_ERR (svn_wc__entries_write (entries, parent_access, pool));
          }
      }
      
      /* Recurse on each file and dir entry. */
      SVN_ERR (svn_wc_entries_read (&entries, adm_access, FALSE, subpool));
      
      for (hi = apr_hash_first (subpool, entries); 
           hi;
           hi = apr_hash_next (hi))
        {
          const void *key;
          void *val;
          const char *current_entry_name;
          const svn_wc_entry_t *current_entry; 
          
          apr_hash_this (hi, &key, NULL, &val);
          current_entry = val;
          if (! strcmp (key, SVN_WC_ENTRY_THIS_DIR))
            current_entry_name = NULL;
          else
            current_entry_name = key;

          if (current_entry->kind == svn_node_file)
            {
              err = svn_wc_remove_from_revision_control
                (adm_access, current_entry_name, destroy_wf, subpool);

              if (err && (err->apr_err == SVN_ERR_WC_LEFT_LOCAL_MOD))
                {
                  svn_error_clear (err);
                  left_something = TRUE;
                }
              else if (err)
                return err;
            }
          else if (current_entry_name && (current_entry->kind == svn_node_dir))
            {
              svn_wc_adm_access_t *entry_access;
              const char *entrypath
                = svn_path_join (svn_wc_adm_access_path(adm_access),
                                 current_entry_name,
                                 subpool);
              SVN_ERR (svn_wc_adm_retrieve (&entry_access, adm_access,
                                            entrypath, pool));
              err = svn_wc_remove_from_revision_control (entry_access,
                                                         SVN_WC_ENTRY_THIS_DIR,
                                                         destroy_wf, subpool);
              if (err && (err->apr_err == SVN_ERR_WC_LEFT_LOCAL_MOD))
                {
                  svn_error_clear (err);
                  left_something = TRUE;
                }
              else if (err)
                return err;
            }
        }

      /* At this point, every directory below this one has been
         removed from revision control. */

      /* Remove the entire administrative .svn area, thereby removing
         _this_ dir from revision control too.  */
      SVN_ERR (svn_wc__adm_destroy (adm_access, subpool));
      
      /* If caller wants us to recursively nuke everything on disk, go
         ahead, provided that there are no dangling local-mod files
         below */
      if (destroy_wf && (! left_something))
        {
          /* If the dir is *truly* empty (i.e. has no unversioned
             resources, all versioned files are gone, all .svn dirs are
             gone, and contains nothing but empty dirs), then a
             *non*-recursive dir_remove should work.  If it doesn't,
             no big deal.  Just assume there are unversioned items in
             there and set "left_something" */
          err = svn_io_dir_remove_nonrecursive
            (svn_wc_adm_access_path (adm_access), subpool);
          if (err)
            left_something = TRUE;
        }
    }  /* end of directory case */

  svn_pool_destroy (subpool);

  if (left_something)
    return svn_error_create (SVN_ERR_WC_LEFT_LOCAL_MOD, 0, NULL, "");

  else
    return SVN_NO_ERROR;
}



/*** Resolving a conflict automatically ***/


/* Helper for resolve_conflict_on_entry */
static svn_error_t *
attempt_deletion (const char *parent_dir,
                  const char *base_name,
                  apr_pool_t *pool)
{
  const char *full_path = svn_path_join (parent_dir, base_name, pool);
  return remove_file_if_present (full_path, pool);
}


/* Does the main resolution work: */
static svn_error_t *
resolve_conflict_on_entry (const char *path,
                           const svn_wc_entry_t *orig_entry,
                           const char *conflict_dir,
                           const char *base_name,
                           svn_boolean_t resolve_text,
                           svn_boolean_t resolve_props,
                           svn_wc_notify_func_t notify_func,
                           void *notify_baton,
                           apr_pool_t *pool)
{
  svn_boolean_t text_conflict, prop_conflict;
  apr_uint32_t modify_flags = 0;
  svn_wc_entry_t *entry = svn_wc_entry_dup (orig_entry, pool);
#if 0  /* See below */
  svn_wc_adm_access_t *adm_access;
#endif

  /* Sanity check: see if libsvn_wc thinks this item is in a state of
     conflict that we have asked to resolve.   If not, just go home.*/
  SVN_ERR (svn_wc_conflicted_p (&text_conflict, &prop_conflict,
                                conflict_dir, entry, pool));
  if (!(resolve_text && text_conflict) && !(resolve_props && prop_conflict))
    return SVN_NO_ERROR;

  /* Yes indeed, being able to map a function over a list would be nice. */
  if (resolve_text && text_conflict && entry->conflict_old)
    {
      SVN_ERR (attempt_deletion (conflict_dir, entry->conflict_old, pool));
      modify_flags |= SVN_WC__ENTRY_MODIFY_CONFLICT_OLD;
      entry->conflict_old = NULL;
    }
  if (resolve_text && text_conflict && entry->conflict_new)
    {
      SVN_ERR (attempt_deletion (conflict_dir, entry->conflict_new, pool));
      modify_flags |= SVN_WC__ENTRY_MODIFY_CONFLICT_NEW;
      entry->conflict_new = NULL;
    }
  if (resolve_text && text_conflict && entry->conflict_wrk)
    {
      SVN_ERR (attempt_deletion (conflict_dir, entry->conflict_wrk, pool));
      modify_flags |= SVN_WC__ENTRY_MODIFY_CONFLICT_WRK;
      entry->conflict_wrk = NULL;
    }
  if (resolve_props && prop_conflict && entry->prejfile)
    {
      SVN_ERR (attempt_deletion (conflict_dir, entry->prejfile, pool));
      modify_flags |= SVN_WC__ENTRY_MODIFY_PREJFILE;
      entry->prejfile = NULL;
    }

#if 0
  /* ### FIXME: Now that update has locked the working copy we cannot get a
     ### lock here.  We either need to get the access baton passed in or we
     ### need to do this somewhere else.  Should this be using log files?

     Although removing the files is sufficient to indicate that the
     conflict is resolved, if we update the entry as well future checks
     for conflict state will be more efficient. */
  SVN_ERR (svn_wc_adm_open (&adm_access, NULL, conflict_dir, TRUE, FALSE,
                            pool));
  SVN_ERR (svn_wc__entry_modify
           (adm_access,
            (entry->kind == svn_node_dir ? NULL : base_name),
            entry, modify_flags, pool));
  SVN_ERR (svn_wc_adm_close (adm_access));
#endif

  if (notify_func)
    {
      /* Sanity check:  see if libsvn_wc *still* thinks this item is in a
         state of conflict that we have asked to resolve.  If not, report
         the successful resolution.  */     
      SVN_ERR (svn_wc_conflicted_p (&text_conflict, &prop_conflict,
                                    conflict_dir, entry, pool));
      if ((! (resolve_text && text_conflict))
          && (! (resolve_props && prop_conflict)))
        (*notify_func) (notify_baton, path, svn_wc_notify_resolve,
                        svn_node_unknown,
                        NULL,
                        svn_wc_notify_state_unknown,
                        svn_wc_notify_state_unknown,
                        SVN_INVALID_REVNUM);
    }
          
  return SVN_NO_ERROR;
}

/* Machinery for an automated entries walk... */

struct resolve_callback_baton
{
  svn_boolean_t resolve_text;
  svn_boolean_t resolve_props;
  svn_wc_notify_func_t notify_func;
  void *notify_baton;
  apr_pool_t *pool;
};

static svn_error_t *
resolve_found_entry_callback (const char *path,
                              const svn_wc_entry_t *entry,
                              void *walk_baton)
{
  struct resolve_callback_baton *baton = walk_baton;
  const char *conflict_dir, *base_name = NULL;

  /* We're going to receive dirents twice;  we want to ignore the
     first one (where it's a child of a parent dir), and only print
     the second one (where we're looking at THIS_DIR.)  */
  if ((entry->kind == svn_node_dir) 
      && (strcmp (entry->name, SVN_WC_ENTRY_THIS_DIR)))
    return SVN_NO_ERROR;

  /* Figger out the directory in which the conflict resides. */
  if (entry->kind == svn_node_dir)
    conflict_dir = path;
  else
    svn_path_split_nts (path, &conflict_dir, &base_name, baton->pool);

  return resolve_conflict_on_entry (path, entry, conflict_dir, base_name,
                                    baton->resolve_text, baton->resolve_props,
                                    baton->notify_func, baton->notify_baton,
                                    baton->pool);
}

static const svn_wc_entry_callbacks_t 
resolve_walk_callbacks =
  {
    resolve_found_entry_callback
  };


/* The public function */
svn_error_t *
svn_wc_resolve_conflict (const char *path,
                         svn_boolean_t resolve_text,
                         svn_boolean_t resolve_props,
                         svn_boolean_t recursive,
                         svn_wc_notify_func_t notify_func,
                         void *notify_baton,                         
                         apr_pool_t *pool)
{
  struct resolve_callback_baton *baton = apr_pcalloc (pool, sizeof(*baton));
  svn_wc_adm_access_t *adm_access;

  baton->resolve_text = resolve_text;
  baton->resolve_props = resolve_props;
  baton->notify_func = notify_func;
  baton->notify_baton = notify_baton;
  baton->pool = pool;

  /* ### Will probably need to have write access eventually */
  SVN_ERR (svn_wc_adm_probe_open (&adm_access, NULL, path, FALSE, TRUE, pool));

  if (! recursive)
    {
      const svn_wc_entry_t *entry;
      svn_wc_entry (&entry, path, adm_access, FALSE, pool);
      if (! entry)
        return svn_error_createf (SVN_ERR_ENTRY_NOT_FOUND, 0, NULL,
                                  "Not under version control: '%s'", path);

      SVN_ERR (resolve_found_entry_callback (path, entry, baton));
    }
  else
    {
      SVN_ERR (svn_wc_walk_entries (path, adm_access,
                                    &resolve_walk_callbacks, baton,
                                    FALSE, pool));

    }
  SVN_ERR (svn_wc_adm_close (adm_access));
  return SVN_NO_ERROR;
}




svn_error_t *
svn_wc_get_auth_file (const char *path,
                      const char *filename,
                      svn_stringbuf_t **contents,
                      apr_pool_t *pool)
{
  apr_file_t *file;
  SVN_ERR (svn_wc__open_auth_file (&file, path, filename, APR_READ, pool));

  /* Read the file's contents into a stringbuf, allocated in POOL. */
  SVN_ERR (svn_stringbuf_from_aprfile (contents, file, pool));

  SVN_ERR (svn_wc__close_auth_file (file, path, filename,
                                    0 /* Don't sync */, pool));

  return SVN_NO_ERROR;
}



svn_error_t *
svn_wc_set_auth_file (svn_wc_adm_access_t *adm_access,
                      svn_boolean_t recurse,
                      const char *filename,
                      svn_stringbuf_t *contents,
                      apr_pool_t *pool)
{
  apr_status_t status;
  apr_file_t *fp;
  apr_size_t sz;

  /* Create/overwrite the file in PATH's administrative area.
     (In reality, this opens a file 'path/.svn/tmp/auth/filename'.) */
  SVN_ERR (svn_wc__open_auth_file (&fp, svn_wc_adm_access_path (adm_access),
                                   filename,
                                   (APR_WRITE | APR_CREATE | APR_TRUNCATE),
                                   pool));

  status = apr_file_write_full (fp, contents->data, contents->len, &sz);
  if (status) 
    return svn_error_createf (status, 0, NULL,
                              "error writing to auth file '%s' in '%s'",
                              filename, svn_wc_adm_access_path (adm_access));

  SVN_ERR (svn_wc__close_auth_file (fp, svn_wc_adm_access_path (adm_access),
                                    filename, TRUE /* sync */, pool));
  
  if (recurse)
    {
      /* Loop over PATH's entries, and recurse into directories. */
      apr_hash_index_t *hi;
      apr_hash_t *entries;
      const char *base_name;
      const svn_wc_entry_t *entry;

      SVN_ERR (svn_wc_entries_read (&entries, adm_access, FALSE, pool));

      for (hi = apr_hash_first (pool, entries); hi; hi = apr_hash_next (hi))
        {
          const void *key;
          void *val;

          apr_hash_this (hi, &key, NULL, &val);
          base_name = key;          
          entry = val;

          if ((entry->kind == svn_node_dir)
              && (strcmp (base_name, SVN_WC_ENTRY_THIS_DIR)))
            {              
              svn_wc_adm_access_t *child_access;
              const char *childpath
                = svn_path_join (svn_wc_adm_access_path (adm_access), base_name,
                                 pool);

              SVN_ERR (svn_wc_adm_retrieve (&child_access, adm_access,
                                            childpath, pool));
              SVN_ERR (svn_wc_set_auth_file (child_access, TRUE,
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
