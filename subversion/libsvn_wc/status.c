/*
 * status.c: construct a status structure from an entry structure
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
#include <apr_file_io.h>
#include <apr_hash.h>
#include <apr_time.h>
#include "svn_types.h"
#include "svn_string.h"
#include "svn_error.h"
#include "svn_path.h"
#include "svn_wc.h"
#include "wc.h"   



/* Fill in *STATUS for PATH, whose entry data is in ENTRY.  Allocate
   *STATUS in POOL. 

   ENTRY may be null, for non-versioned entities.
   Else, ENTRY's pool must not be shorter-lived than STATUS's, since
   ENTRY will be stored directly, not copied.

   If GET_ALL is zero, and ENTRY is not locally modified, then *STATUS
   will be set to NULL.  If GET_ALL is non-zero, then *STATUS will be
   allocated and returned no matter what.

*/
static svn_error_t *
assemble_status (svn_wc_status_t **status,
                 svn_stringbuf_t *path,
                 svn_wc_entry_t *entry,
                 svn_boolean_t get_all,
                 apr_pool_t *pool)
{
  svn_wc_status_t *stat;
  enum svn_node_kind path_kind;
  svn_boolean_t text_modified_p = FALSE;
  svn_boolean_t prop_modified_p = FALSE;

  /* Defaults for two main variables. */
  enum svn_wc_status_kind final_text_status = svn_wc_status_none;
  enum svn_wc_status_kind final_prop_status = svn_wc_status_none;

  if (! entry)
    {
      /* return a blank structure. */
      *status = apr_pcalloc (pool, sizeof(**status));
      return SVN_NO_ERROR;
    }

  /* Implement predecence rules: */

  /* 1. Set the two main variables to "discovered" values first (M, C). 
        Together, these two stati are of lowest precedence, and C has
        precedence over M. */

  /* If the entry has a property file, see if it has local changes. */
  SVN_ERR (svn_wc_props_modified_p (&prop_modified_p, path, pool));
  
  /* If the entry is a file, check for textual modifications */
  if (entry->kind == svn_node_file)
    SVN_ERR (svn_wc_text_modified_p (&text_modified_p, path, pool));
  
  if (text_modified_p)
    final_text_status = svn_wc_status_modified;
  
  if (prop_modified_p)
    final_prop_status = svn_wc_status_modified;      
  
  if (entry->conflicted)
    {
      /* We must decide if either component is still "conflicted",
         based on whether reject files continue to exist. */
      svn_boolean_t text_conflict_p, prop_conflict_p;
      svn_stringbuf_t *parent_dir;
      
      if (entry->kind == svn_node_file)
        {
          parent_dir = svn_stringbuf_dup (path, pool);
          svn_path_remove_component (parent_dir, svn_path_local_style);
        }
      else if (entry->kind == svn_node_dir)
        parent_dir = path;
      
      SVN_ERR (svn_wc_conflicted_p (&text_conflict_p, &prop_conflict_p,
                                    parent_dir, entry, pool));
      
      if (text_conflict_p)
        final_text_status = svn_wc_status_conflicted;
      if (prop_conflict_p)
        final_prop_status = svn_wc_status_conflicted;
    }

   
  /* 2. Possibly overwrite the the text_status variable with
        "scheduled" states from the entry (A, D, R).  As a group,
        these states are of medium precedence.  They also override any
        C or M that may be in the prop_status field at this point.*/

  if (entry->schedule == svn_wc_schedule_add)
    {
      final_text_status = svn_wc_status_added;
      final_prop_status = svn_wc_status_none;
    }
      
  else if (entry->schedule == svn_wc_schedule_replace)
    {
      final_text_status = svn_wc_status_replaced;
      final_prop_status = svn_wc_status_none;
    }
    
  else if ((entry->schedule == svn_wc_schedule_delete)
           || (entry->existence == svn_wc_existence_deleted))
    {
      final_text_status = svn_wc_status_deleted;
      final_prop_status = svn_wc_status_none;
    }


  /* 3. Highest precedence: check to see if file or dir is just
        missing.  This overrides every possible state *except*
        deletion.  (If something is deleted or scheduled for it, we
        don't care if the working file exists.)  */
  
  SVN_ERR(svn_io_check_path (path, &path_kind, pool));
  if ((path_kind == svn_node_none)
      && (final_text_status != svn_wc_status_deleted))
    final_text_status = svn_wc_status_absent;


  /* 4. Easy out:  unless we're fetching -every- entry, don't bother
     to allocate a struct for an uninteresting entry. */

  if (! get_all)
    if ((final_text_status == svn_wc_status_none)
        && ((final_prop_status == svn_wc_status_none)))
      {
        *status = NULL;
        return SVN_NO_ERROR;
      }


  /* 5. Build and return a status structure. */

  stat = apr_pcalloc (pool, sizeof(**status));
  stat->entry = entry;
  stat->repos_rev = SVN_INVALID_REVNUM;           /* caller fills in */
  stat->text_status = final_text_status;       
  stat->prop_status = final_prop_status;    
  stat->repos_text_status = svn_wc_status_none;   /* default */
  stat->repos_prop_status = svn_wc_status_none;   /* default */
  stat->locked = FALSE;

  
  /* 6. Check for locked directory. */

  if (entry->kind == svn_node_dir)
    SVN_ERR (svn_wc__locked (&(stat->locked), path, pool));
  

  *status = stat;

  return SVN_NO_ERROR;
}


/* Given an ENTRY object representing PATH, build a status structure
   and store it in STATUSHASH.  */
static svn_error_t *
add_status_structure (apr_hash_t *statushash,
                      svn_stringbuf_t *path,
                      svn_wc_entry_t *entry,
                      svn_boolean_t get_all,
                      apr_pool_t *pool)
{
  svn_wc_status_t *statstruct;

  SVN_ERR (assemble_status (&statstruct, path, entry, get_all, pool));

  if (statstruct)
    apr_hash_set (statushash, path->data, path->len, statstruct);
  
  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc_status (svn_wc_status_t **status,
               svn_stringbuf_t *path,
               apr_pool_t *pool)
{
  svn_error_t *err;
  svn_wc_status_t *s;
  svn_wc_entry_t *entry = NULL;

  err = svn_wc_entry (&entry, path, pool);
  if (err)
    return err;

  if (entry && entry->existence == svn_wc_existence_deleted)
    return svn_error_createf
      (SVN_ERR_WC_ENTRY_NOT_FOUND, 0, NULL, pool,
       "entry '%s' has already been deleted", path->data);

  err = assemble_status (&s, path, entry, TRUE, pool);
  if (err)
    return err;
  
  *status = s;
  return SVN_NO_ERROR;
}



svn_error_t *
svn_wc_statuses (apr_hash_t *statushash,
                 svn_stringbuf_t *path,
                 svn_boolean_t descend,
                 svn_boolean_t get_all,
                 apr_pool_t *pool)
{
  svn_error_t *err;
  enum svn_node_kind kind;
  apr_hash_t *entries;
  svn_wc_entry_t *entry;
  void *value;
  
  /* Is PATH a directory or file? */
  err = svn_io_check_path (path, &kind, pool);
  if (err) return err;
  
  /* kff todo: this has to deal with the case of a type-changing edit,
     i.e., someone removed a file under vc and replaced it with a dir,
     or vice versa.  In such a case, when you ask for the status, you
     should get mostly information about the now-vanished entity, plus
     some information about what happened to it.  The same situation
     is handled in entries.c:svn_wc_entry. */

  /* Read the appropriate entries file */
  
  /* If path points to just one file, or at least to just one
     non-directory, store just one status structure in the
     STATUSHASH and return. */
  if ((kind == svn_node_file) || (kind == svn_node_none))
    {
      svn_stringbuf_t *dirpath, *basename;

      /* Figure out file's parent dir */
      svn_path_split (path, &dirpath, &basename,
                      svn_path_local_style, pool);      

      /* Load entries file for file's parent */
      err = svn_wc_entries_read (&entries, dirpath, pool);
      if (err) return err;

      /* Get the entry by looking up file's basename */
      value = apr_hash_get (entries, basename->data, basename->len);

      if (value)
        entry = (svn_wc_entry_t *) value;
      else
        return svn_error_createf (SVN_ERR_BAD_FILENAME, 0, NULL, pool,
                                  "svn_wc_statuses:  bogus path `%s'",
                                  path->data);

      /* Convert the entry into a status structure, store in the hash.
         
         ### Notice that because we're getting one specific file,
         we're ignoring the GET_ALL flag and unconditionally fetching
         the status structure. */
      err = add_status_structure (statushash, path, entry, TRUE, pool);
      if (err) return err;
    }


  /* Fill the hash with a status structure for *each* entry in PATH */
  else if (kind == svn_node_dir)
    {
      apr_hash_index_t *hi;

      /* Load entries file for the directory */
      err = svn_wc_entries_read (&entries, path, pool);
      if (err) return err;

      /* Loop over entries hash */
      for (hi = apr_hash_first (pool, entries); hi; hi = apr_hash_next (hi))
        {
          const void *key;
          void *val;
          const char *basename;
          apr_size_t keylen;
          svn_stringbuf_t *fullpath = svn_stringbuf_dup (path, pool);

          /* Get the next dirent */
          apr_hash_this (hi, &key, &keylen, &val);
          basename = (const char *) key;
          if (strcmp (basename, SVN_WC_ENTRY_THIS_DIR) != 0)
            {
              svn_path_add_component_nts (fullpath, basename,
                                          svn_path_local_style);
            }

          entry = (svn_wc_entry_t *) val;

          /* If the entry's existence is `deleted', skip it. */
          if ((entry->existence == svn_wc_existence_deleted)
              && (entry->schedule != svn_wc_schedule_add))
            continue;

          err = svn_io_check_path (fullpath, &kind, pool);
          if (err) return err;

          /* In deciding whether or not to descend, we use the actual
             kind of the entity, not the kind claimed by the entries
             file.  The two are usually the same, but where they are
             not, its usually because some directory got moved, and
             one would still want a status report on its contents.
             kff todo: However, must handle mixed working copies.
             What if the subdir is not under revision control, or is
             from another repository? */
          
          /* Do *not* store THIS_DIR in the statushash, unless this
             path has never been seen before.  We don't want to add
             the path key twice. */
          if (! strcmp (basename, SVN_WC_ENTRY_THIS_DIR))
            {
              svn_wc_status_t *s = apr_hash_get (statushash,
                                                 fullpath->data,
                                                 fullpath->len);
              if (! s)
                SVN_ERR (add_status_structure (statushash, fullpath,
                                               entry, get_all, pool));
            }
          else
            {
              if (kind == svn_node_dir)
                {
                  /* Directory entries are incomplete.  We must get
                     their full entry from their own THIS_DIR entry.
                     svn_wc_entry does this for us if it can.  */
                  svn_wc_entry_t *subdir;

                  SVN_ERR (svn_wc_entry (&subdir, fullpath, pool));
                  SVN_ERR (add_status_structure (statushash, fullpath,
                                                 subdir, get_all, pool));
                  if (descend)
                    {
                      /* If ask to descent, we do not contend. */
                      SVN_ERR (svn_wc_statuses (statushash, fullpath,
                                                descend, get_all, pool)); 
                    }
                }
              else if ((kind == svn_node_file) || (kind == svn_node_none))
                {
                  /* File entries are ... just fine! */
                  SVN_ERR (add_status_structure (statushash, fullpath,
                                                 entry, get_all, pool));
                }
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
