/*
 * status.c: construct a status structure from an entry structure
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
#include <apr_file_io.h>
#include <apr_hash.h>
#include <apr_time.h>
#include <apr_fnmatch.h>
#include "svn_pools.h"
#include "svn_types.h"
#include "svn_string.h"
#include "svn_error.h"
#include "svn_path.h"
#include "svn_wc.h"
#include "svn_config.h"

#include "wc.h"
#include "props.h"


static svn_error_t *
get_default_ignores (apr_array_header_t **patterns,
                     apr_pool_t *pool)
{
  struct svn_config_t *cfg;
  const char *val;

  SVN_ERR (svn_config_read_config (&cfg, pool));
  svn_config_get (cfg, &val, "miscellany", "global_ignores", 
                  "*.o *.lo *.la #*# *.rej *~ .#*");
  *patterns = apr_array_make (pool, 4, sizeof (const char *));
  svn_cstring_split_append (*patterns, val, "\n\r\t\v ", FALSE, pool);

  return SVN_NO_ERROR;
}


/* Helper routine: add to *PATTERNS patterns from the value of
   the SVN_PROP_IGNORE property set on DIRPATH.  If there is no such
   property, or the property contains no patterns, do nothing.
   Otherwise, add to *PATTERNS a list of (const char *) patterns to
   match. */
static svn_error_t *
add_ignore_patterns (const char *dirpath,
                     apr_array_header_t *patterns,
                     apr_pool_t *pool)
{
  const svn_string_t *value;

  /* Try to load the SVN_PROP_IGNORE property. */
  SVN_ERR (svn_wc_prop_get (&value, SVN_PROP_IGNORE, dirpath, pool));

  if (value != NULL)
    svn_cstring_split_append (patterns, value->data, "\n\r", FALSE, pool);

  return SVN_NO_ERROR;
}                  


                        
/* Fill in *STATUS for PATH, whose entry data is in ENTRY.  Allocate
   *STATUS in POOL. 

   ENTRY may be null, for non-versioned entities.  In this case, we
   will assemble a special status structure item which implies a
   non-versioned thing.

   Else, ENTRY's pool must not be shorter-lived than STATUS's, since
   ENTRY will be stored directly, not copied.

   PATH_KIND is the node kind of PATH as determined by the caller.
   NOTE: this may be svn_node_unknown if the caller has made no such
   determination.

   If GET_ALL is zero, and ENTRY is not locally modified, then *STATUS
   will be set to NULL.  If GET_ALL is non-zero, then *STATUS will be
   allocated and returned no matter what.
*/
static svn_error_t *
assemble_status (svn_wc_status_t **status,
                 const char *path,
                 svn_wc_adm_access_t *adm_access,
                 const svn_wc_entry_t *entry,
                 svn_node_kind_t path_kind,
                 svn_boolean_t get_all,
                 apr_pool_t *pool)
{
  svn_wc_status_t *stat;
  svn_boolean_t has_props;
  svn_boolean_t text_modified_p = FALSE;
  svn_boolean_t prop_modified_p = FALSE;
  svn_boolean_t locked_p = FALSE;

  /* Defaults for two main variables. */
  enum svn_wc_status_kind final_text_status = svn_wc_status_normal;
  enum svn_wc_status_kind final_prop_status = svn_wc_status_none;

  /* Check the path kind for PATH. */
  if (path_kind == svn_node_unknown)
    SVN_ERR (svn_io_check_path (path, &path_kind, pool));
  
  if (! entry)
    {
      /* return a blank structure. */
      stat = apr_pcalloc (pool, sizeof(*stat));
      stat->entry = NULL;
      stat->text_status = svn_wc_status_none;
      stat->prop_status = svn_wc_status_none;
      stat->repos_text_status = svn_wc_status_none;
      stat->repos_prop_status = svn_wc_status_none;
      stat->locked = FALSE;
      stat->copied = FALSE;

      /* If this path has no entry, but IS present on disk, it's
         unversioned. */
      if (path_kind != svn_node_none)
        stat->text_status = svn_wc_status_unversioned;

      *status = stat;
      return SVN_NO_ERROR;
    }

  /* Someone either deleted the administrative directory in the versioned
     subdir, or deleted the directory altogether and created a new one.
     In any case, what is currently there is in the way.
   */
  if (entry->kind == svn_node_dir
      && path_kind == svn_node_dir)
    {
      int is_wc;

      SVN_ERR (svn_wc_check_wc (path, &is_wc, pool));
      if (! is_wc)
        final_text_status = svn_wc_status_obstructed;
    }

  if (final_text_status != svn_wc_status_obstructed)
    {
      /* Implement predecence rules: */

      /* 1. Set the two main variables to "discovered" values first (M, C).
            Together, these two stati are of lowest precedence, and C has
            precedence over M. */

      /* Does the entry have props? */
      SVN_ERR (svn_wc__has_props (&has_props, path, pool));
      if (has_props)
        final_prop_status = svn_wc_status_normal;

      /* If the entry has a property file, see if it has local changes. */
      SVN_ERR (svn_wc_props_modified_p (&prop_modified_p, path, adm_access,
                                        pool));

      /* If the entry is a file, check for textual modifications */
      if (entry->kind == svn_node_file)
        SVN_ERR (svn_wc_text_modified_p (&text_modified_p, path, adm_access,
                                         pool));

      if (text_modified_p)
        final_text_status = svn_wc_status_modified;

      if (prop_modified_p)
        final_prop_status = svn_wc_status_modified;

      if (entry->prejfile || entry->conflict_old ||
          entry->conflict_new || entry->conflict_wrk)
        {
          svn_boolean_t text_conflict_p, prop_conflict_p;
          const char *parent_dir;

          if (entry->kind == svn_node_dir)
            parent_dir = path;
          else  /* non-directory, that's all we need to know */
            parent_dir = svn_path_remove_component_nts (path, pool);

          SVN_ERR (svn_wc_conflicted_p (&text_conflict_p, &prop_conflict_p,
                                        parent_dir, entry, pool));

          if (text_conflict_p)
            final_text_status = svn_wc_status_conflicted;
          if (prop_conflict_p)
            final_prop_status = svn_wc_status_conflicted;
        }

      /* 2. Possibly overwrite the text_status variable with "scheduled"
            states from the entry (A, D, R).  As a group, these states are
            of medium precedence.  They also override any C or M that may
            be in the prop_status field at this point.*/

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

      else if (entry->schedule == svn_wc_schedule_delete)
        {
          final_text_status = svn_wc_status_deleted;
          final_prop_status = svn_wc_status_none;
        }


      /* 3. Highest precedence:

            a. check to see if file or dir is just missing.  This
               overrides every possible state *except* deletion.
               (If something is deleted or scheduled for it, we
               don't care if the working file exists.)

            b. check to see if the file or dir is present in the
               file system as the same kind it was versioned as.

         4. Check for locked directory (only for directories). */

      if (path_kind == svn_node_none)
        {
          if (final_text_status != svn_wc_status_deleted)
            final_text_status = svn_wc_status_absent;
        }
      else if (path_kind != entry->kind)
        final_text_status = svn_wc_status_obstructed;
      else if (entry->kind == svn_node_dir)
        SVN_ERR (svn_wc_locked (&locked_p, path, pool));
    }

  /* 5. Easy out:  unless we're fetching -every- entry, don't bother
     to allocate a struct for an uninteresting entry. */

  if (! get_all)
    if (((final_text_status == svn_wc_status_none)
         || (final_text_status == svn_wc_status_normal))
        && ((final_prop_status == svn_wc_status_none)
            || (final_prop_status == svn_wc_status_normal))
        && (! locked_p))
      {
        *status = NULL;
        return SVN_NO_ERROR;
      }


  /* 6. Build and return a status structure. */

  stat = apr_pcalloc (pool, sizeof(**status));
  stat->entry = svn_wc_entry_dup (entry, pool);
  stat->text_status = final_text_status;       
  stat->prop_status = final_prop_status;    
  stat->repos_text_status = svn_wc_status_none;   /* default */
  stat->repos_prop_status = svn_wc_status_none;   /* default */
  stat->locked = locked_p;
  stat->copied = entry->copied;

  *status = stat;

  return SVN_NO_ERROR;
}


/* Given an ENTRY object representing PATH, build a status structure
   and store it in STATUSHASH.  */
static svn_error_t *
add_status_structure (apr_hash_t *statushash,
                      const char *path,
                      svn_wc_adm_access_t *adm_access,
                      const svn_wc_entry_t *entry,
                      svn_node_kind_t path_kind,
                      svn_boolean_t get_all,
                      apr_pool_t *pool)
{
  svn_wc_status_t *statstruct;
  
  SVN_ERR (assemble_status (&statstruct, path, adm_access, entry, path_kind, 
                            get_all, pool));
  if (statstruct)
    apr_hash_set (statushash, path, APR_HASH_KEY_STRING, statstruct);
  
  return SVN_NO_ERROR;
}


/* Add all items that are NOT in ENTRIES (which is a list of PATH's
   versioned things) to the STATUSHASH as unversioned items,
   allocating everything in POOL.  If IGNORES is non-NULL, it contains
   the default ignores, else this is an indication that no ignores
   should be honored. */
static svn_error_t *
add_unversioned_items (const char *path, 
                       svn_wc_adm_access_t *adm_access,
                       apr_hash_t *entries,
                       apr_hash_t *statushash,
                       apr_array_header_t *ignores,
                       apr_pool_t *pool)
{
  apr_pool_t *subpool = svn_pool_create (pool);
  apr_hash_t *dirents;
  apr_hash_index_t *hi;
  apr_array_header_t *patterns;

  /* Read PATH's dirents. */
  SVN_ERR (svn_io_get_dirents (&dirents, path, subpool));

  /* Unless specified, add default ignore regular expressions and try
     to add any svn:ignore properties from the parent directory. */
  if (ignores)
    {
      int i;

      /* Copy default ignores into the local PATTERNS array. */
      patterns = apr_array_make (subpool, 1, sizeof(const char *));
      for (i = 0; i < ignores->nelts; i++)
        {
          const char *ignore = APR_ARRAY_IDX (ignores, i, const char *);
          (*((const char **) apr_array_push (patterns))) = ignore;
        }

      /* Then add any svn:ignore globs to the PATTERNS array. */
      SVN_ERR (add_ignore_patterns (path, patterns, subpool));
    }
  else
    patterns = NULL;

  /* Add empty status structures for each of the unversioned things. */
  for (hi = apr_hash_first (subpool, dirents); hi; hi = apr_hash_next (hi))
    {
      const void *key;
      apr_ssize_t klen;
      void *val;
      const char *keystring;
      int i;
      int ignore_me;
      const char *printable_path;
      svn_node_kind_t *path_kind;

      apr_hash_this (hi, &key, &klen, &val);
      keystring = key;
      path_kind = val;
        
      /* If the dirent isn't in `.svn/entries'... */
      if (apr_hash_get (entries, key, klen))        
        continue;

      /* and we're not looking at .svn... */
      if (! strcmp (keystring, SVN_WC_ADM_DIR_NAME))
        continue;

      ignore_me = 0;

      /* See if any of the ignore patterns we have matches our
         keystring. */
      for (i = 0; patterns && (i < patterns->nelts); i++)
        {
          const char *pat = (((const char **) (patterns)->elts))[i];
                
          /* Try to match current_entry_name to pat. */
          if (APR_SUCCESS == apr_fnmatch (pat, keystring, FNM_PERIOD))
            {
              ignore_me = 1;
              break;
            }
        }
      
      /* If we aren't ignoring it, add a status structure for this
         dirent. */
      if (! ignore_me)
        {
          printable_path = svn_path_join (path, keystring, pool);
          
          /* Add this item to the status hash. */
          SVN_ERR (add_status_structure (statushash,
                                         printable_path,
                                         adm_access,
                                         NULL, /* no entry */
                                         *path_kind,
                                         FALSE,
                                         pool));
        }
    }

  svn_pool_destroy (subpool);

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc_status (svn_wc_status_t **status,
               const char *path,
               svn_wc_adm_access_t *adm_access,
               apr_pool_t *pool)
{
  svn_wc_status_t *s;
  const svn_wc_entry_t *entry;

  /* PATH may be unversioned, or nonexistent (in the case of 'svn st -u'
     being told about as-yet-unknnown paths), and either condition will
     cause svn_wc_entry to return an error.  If this routine returns error,
     then a NULL entry will be passed to assemble_status() below. */
  if (adm_access)
    {
      svn_error_t *err = svn_wc_entry (&entry, path, adm_access, FALSE, pool);
      if (err)
        svn_error_clear_all (err);
    }
  else
    entry = NULL;

  SVN_ERR (assemble_status (&s, path, adm_access, entry, svn_node_unknown,
                            TRUE, pool));
  *status = s;
  return SVN_NO_ERROR;
}



svn_error_t *
svn_wc_statuses (apr_hash_t *statushash,
                 const char *path,
                 svn_wc_adm_access_t *adm_access,
                 svn_boolean_t descend,
                 svn_boolean_t get_all,
                 svn_boolean_t no_ignore,
                 apr_pool_t *pool)
{
  svn_node_kind_t kind;
  const svn_wc_entry_t *entry;

  /* Is PATH a directory or file? */
  SVN_ERR (svn_io_check_path (path, &kind, pool));
  
  /* Read the appropriate entries file */
  
  /* If path points to just one file, or at least to just one
     non-directory, store just one status structure in the
     STATUSHASH and return. */
  if ((kind == svn_node_file) || (kind == svn_node_none))
    {
      /* Get the entry for this file. Place it into the specified pool since
         we're going to return it in statushash. */
      SVN_ERR (svn_wc_entry (&entry, path, adm_access, FALSE, pool));

      /* Convert the entry into a status structure, store in the hash.
         
         ### Notice that because we're getting one specific file,
         we're ignoring the GET_ALL flag and unconditionally fetching
         the status structure. */
      SVN_ERR (add_status_structure (statushash, path, adm_access, entry, kind, 
                                     TRUE, pool));
    }


  /* Fill the hash with a status structure for *each* entry in PATH */
  else if (kind == svn_node_dir)
    {
      apr_hash_t *entries;
      apr_hash_index_t *hi;
      int is_wc;
      apr_array_header_t *ignores = NULL;

      /* Sanity check to make sure that we're being called on a working copy.
         This isn't strictly necessary, since svn_wc_entries_read will fail 
         anyway, but it lets us return a more meaningful error. */ 
      SVN_ERR (svn_wc_check_wc (path, &is_wc, pool));
      if (! is_wc)
        return svn_error_createf
          (SVN_ERR_WC_NOT_DIRECTORY, 0, NULL, pool,
           "svn_wc_statuses: %s is not a working copy directory", path);

      /* Load entries file for the directory into the requested pool. */
      SVN_ERR (svn_wc_entries_read (&entries, adm_access, FALSE, pool));

      /* Read the default ignores from the config files. */
      if (! no_ignore)
        SVN_ERR (get_default_ignores (&ignores, pool));

      /* Add the unversioned items to the status output. */
      SVN_ERR (add_unversioned_items (path, adm_access, entries, statushash,
                                      ignores, pool));

      /* Loop over entries hash */
      for (hi = apr_hash_first (pool, entries); hi; hi = apr_hash_next (hi))
        {
          const void *key;
          void *val;
          const char *base_name;

          /* Put fullpath into the request pool since it becomes a key
             in the output statushash hash table. */
          const char *fullpath = apr_pstrdup (pool, path);

          /* Get the next dirent */
          apr_hash_this (hi, &key, NULL, &val);
          base_name = key;
          if (strcmp (base_name, SVN_WC_ENTRY_THIS_DIR) != 0)
            fullpath = svn_path_join (fullpath, base_name, pool);

          entry = val;

          /* ### todo: What if the subdir is from another repository? */
          
          /* Do *not* store THIS_DIR in the statushash, unless this
             path has never been seen before.  We don't want to add
             the path key twice. */
          if (strcmp (base_name, SVN_WC_ENTRY_THIS_DIR) == 0)
            {
              svn_wc_status_t *status 
                = apr_hash_get (statushash, fullpath, APR_HASH_KEY_STRING);
              if (! status)
                SVN_ERR (add_status_structure (statushash, fullpath, adm_access,
                                               entry, kind, get_all, pool));
            }
          else
            {
              svn_node_kind_t fullpath_kind;

              /* Get the entry's kind on disk. */
              SVN_ERR (svn_io_check_path (fullpath, &fullpath_kind, pool));

              if (fullpath_kind == svn_node_dir)
                {
                  /* Directory entries are incomplete.  We must get
                     their full entry from their own THIS_DIR entry.
                     svn_wc_entry does this for us if it can.

                     Don't error out if svn_wc_entry can't get the
                     entry for us because the path is not a (working
                     copy) directory.  Instead pass the incomplete
                     entry to add_status_structure, since that contains
                     enough information to determine the actual state
                     of this entry.  

                     Of course, if there has been a kind-changing
                     replacement (for example, there is an entry for a
                     file 'foo', but 'foo' exists as a *directory* on
                     disk), we don't want to reach down into that
                     subdir to try to flesh out a "complete entry".  */

                  const svn_wc_entry_t *fullpath_entry = entry;

                  if (entry->kind == fullpath_kind)
                    SVN_ERR (svn_wc_entry (&fullpath_entry, fullpath, 
                                           adm_access, FALSE, pool));

                  SVN_ERR (add_status_structure 
                           (statushash, fullpath, adm_access, fullpath_entry, 
                            fullpath_kind, get_all, pool));

                  /* Descend only if the subdirectory is a working copy
                     directory (and DESCEND is non-zero ofcourse)  */

                  if (descend && fullpath_entry != entry)
                    {
                      svn_wc_adm_access_t *dir_access;
                      SVN_ERR (svn_wc_adm_retrieve (&dir_access, adm_access,
                                                    fullpath, pool));
                      SVN_ERR (svn_wc_statuses (statushash, fullpath,
                                                dir_access, descend,
                                                get_all, no_ignore, pool));
                    }
                }
              else if ((fullpath_kind == svn_node_file) 
                       || (fullpath_kind == svn_node_none))
                {
                  /* File entries are ... just fine! */
                  SVN_ERR (add_status_structure 
                           (statushash, fullpath, adm_access, entry,
                            fullpath_kind, get_all, pool));
                }
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
