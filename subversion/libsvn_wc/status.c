/*
 * status.c: construct a status structure from an entry structure
 *
 * ====================================================================
 * Copyright (c) 2000-2003 CollabNet.  All rights reserved.
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



#include <assert.h>
#include <string.h>
#include <apr_pools.h>
#include <apr_file_io.h>
#include <apr_hash.h>
#include <apr_time.h>
#include <apr_fnmatch.h>
#include "svn_pools.h"
#include "svn_types.h"
#include "svn_delta.h"
#include "svn_string.h"
#include "svn_error.h"
#include "svn_path.h"
#include "svn_io.h"
#include "svn_hash.h"
#include "svn_wc.h"
#include "svn_config.h"
#include "svn_private_config.h"

#include "wc.h"
#include "props.h"


/* Fill in *STATUS for PATH, whose entry data is in ENTRY.  Allocate
   *STATUS in POOL. 

   ENTRY may be null, for non-versioned entities.  In this case, we
   will assemble a special status structure item which implies a
   non-versioned thing.

   Else, ENTRY's pool must not be shorter-lived than STATUS's, since
   ENTRY will be stored directly, not copied.

   PARENT_ENTRY is the entry for the parent directory of PATH, it may be
   NULL if entry is NULL or if PATH is a working copy root.  The lifetime
   of PARENT_ENTRY's pool is not important.

   PATH_KIND is the node kind of PATH as determined by the caller.
   NOTE: this may be svn_node_unknown if the caller has made no such
   determination.

   If GET_ALL is zero, and ENTRY is not locally modified, then *STATUS
   will be set to NULL.  If GET_ALL is non-zero, then *STATUS will be
   allocated and returned no matter what.

   If IS_IGNORED is non-zero and this is a non-versioned entity, set
   the text_status to svn_wc_status_none.  Otherwise set the
   text_status to svn_wc_status_unversioned.
*/
static svn_error_t *
assemble_status (svn_wc_status_t **status,
                 const char *path,
                 svn_wc_adm_access_t *adm_access,
                 const svn_wc_entry_t *entry,
                 const svn_wc_entry_t *parent_entry,
                 svn_node_kind_t path_kind,
                 svn_boolean_t get_all,
                 svn_boolean_t is_ignored,
                 apr_pool_t *pool)
{
  svn_wc_status_t *stat;
  svn_boolean_t has_props;
  svn_boolean_t text_modified_p = FALSE;
  svn_boolean_t prop_modified_p = FALSE;
  svn_boolean_t locked_p = FALSE;
  svn_boolean_t switched_p = FALSE;

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
      stat->switched = FALSE;

      /* If this path has no entry, but IS present on disk, it's
         unversioned.  If this file is being explicitly ignored (due
         to matching an ignore-pattern), the text_status is set to
         svn_wc_status_ignored.  Otherwise the text_status is set to
         svn_wc_status_unversioned. */
      if (path_kind != svn_node_none)
        {
          if (is_ignored)
            stat->text_status = svn_wc_status_ignored;
          else
            stat->text_status = svn_wc_status_unversioned;
        }

      *status = stat;
      return SVN_NO_ERROR;
    }

  /* Someone either deleted the administrative directory in the versioned
     subdir, or deleted the directory altogether and created a new one.
     In any case, what is currently there is in the way.
   */
  if (entry->kind == svn_node_dir)
    {
      if (path_kind == svn_node_dir)
        {
          if (svn_wc__adm_missing (adm_access, path))
            final_text_status = svn_wc_status_obstructed;
        }
      else if (path_kind != svn_node_none)
        final_text_status = svn_wc_status_obstructed;
    }

  /* Is this item switched?  Well, to be switched it must have both an URL
     and a parent with an URL, at the very least. */
  if (entry->url && parent_entry && parent_entry->url)
    {
      /* An item is switched if it's working copy basename differs from the
         basename of its URL. */
      if (strcmp (svn_path_uri_encode (svn_path_basename (path, pool), pool),
                  svn_path_basename (entry->url, pool)))
        switched_p = TRUE;

      /* An item is switched if it's URL, without the basename, does not
         equal its parent's URL. */
      if (! switched_p
          && strcmp (svn_path_dirname (entry->url, pool),
                     parent_entry->url))
        switched_p = TRUE;
    }

  if (final_text_status != svn_wc_status_obstructed)
    {
      /* Implement predecence rules: */

      /* 1. Set the two main variables to "discovered" values first (M, C).
            Together, these two stati are of lowest precedence, and C has
            precedence over M. */

      /* Does the entry have props? */
      SVN_ERR (svn_wc__has_props (&has_props, path, adm_access, pool));
      if (has_props)
        final_prop_status = svn_wc_status_normal;

      /* If the entry has a property file, see if it has local changes. */
      SVN_ERR (svn_wc_props_modified_p (&prop_modified_p, path, adm_access,
                                        pool));

      /* If the entry is a file, check for textual modifications */
      if (entry->kind == svn_node_file)
        SVN_ERR (svn_wc_text_modified_p (&text_modified_p, path, FALSE,
                                         adm_access, pool));

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
            parent_dir = svn_path_dirname (path, pool);

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

            a. check to see if file or dir is just missing, or
               incomplete.  This overrides every possible state
               *except* deletion.  (If something is deleted or
               scheduled for it, we don't care if the working file
               exists.)

            b. check to see if the file or dir is present in the
               file system as the same kind it was versioned as.

         4. Check for locked directory (only for directories). */

      if (entry->incomplete
          && (final_text_status != svn_wc_status_deleted)
          && (final_text_status != svn_wc_status_added))
        {
          final_text_status = svn_wc_status_incomplete;
        }
      else if (path_kind == svn_node_none)
        {
          if (final_text_status != svn_wc_status_deleted)
            final_text_status = svn_wc_status_absent;
        }
      else if (path_kind != entry->kind)
        final_text_status = svn_wc_status_obstructed;

      if (path_kind == svn_node_dir && entry->kind == svn_node_dir)
        SVN_ERR (svn_wc_locked (&locked_p, path, pool));
    }

  /* 5. Easy out:  unless we're fetching -every- entry, don't bother
     to allocate a struct for an uninteresting entry. */

  if (! get_all)
    if (((final_text_status == svn_wc_status_none)
         || (final_text_status == svn_wc_status_normal))
        && ((final_prop_status == svn_wc_status_none)
            || (final_prop_status == svn_wc_status_normal))
        && (! locked_p) && (! switched_p))
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
  stat->switched = switched_p;
  stat->copied = entry->copied;

  *status = stat;

  return SVN_NO_ERROR;
}


/* Given an ENTRY object representing PATH, build a status structure
   and pass it off to the STATUS_FUNC/STATUS_BATON. */
static svn_error_t *
add_status_structure (const char *path,
                      svn_wc_adm_access_t *adm_access,
                      const svn_wc_entry_t *entry,
                      const svn_wc_entry_t *parent_entry,
                      svn_node_kind_t path_kind,
                      svn_boolean_t get_all,
                      svn_boolean_t is_ignored,
                      svn_wc_status_func_t status_func,
                      void *status_baton,
                      apr_pool_t *pool)
{
  svn_wc_status_t *statstruct;
  
  SVN_ERR (assemble_status (&statstruct, path, adm_access, entry, parent_entry,
                            path_kind, get_all, is_ignored, pool));
  if (statstruct && (status_func))
      (*status_func) (status_baton, path, statstruct);
  
  return SVN_NO_ERROR;
}


/* Store in PATTERNS a list of all svn:ignore properties from 
   the working copy directory, including the default ignores
   passed in as IGNORES.

   Upon return, *PATTERNS will contain zero or more (const char *) 
   patterns from the value of the SVN_PROP_IGNORE property set on 
   the working directory path.

   IGNORES is a list of patterns to include; typically this will 
   be the default ignores as, for example, specified in a config file.

   ADM_ACCESS is an access baton for the working copy path. 
   
   Allocate everything in POOL.

   None of the arguments may be NULL.
*/
static svn_error_t *
collect_ignore_patterns (apr_array_header_t *patterns,
                         apr_array_header_t *ignores,
                         svn_wc_adm_access_t *adm_access,
                         apr_pool_t *pool)
{
  int i;
  const svn_string_t *value;

  /* Copy default ignores into the local PATTERNS array. */
  for (i = 0; i < ignores->nelts; i++)
    {
      const char *ignore = APR_ARRAY_IDX (ignores, i, const char *);
      (*((const char **) apr_array_push (patterns))) = ignore;
    }

  /* Then add any svn:ignore globs to the PATTERNS array. */
  SVN_ERR (svn_wc_prop_get (&value, SVN_PROP_IGNORE,
                            svn_wc_adm_access_path (adm_access), adm_access,
                            pool));
  if (value != NULL)
    svn_cstring_split_append (patterns, value->data, "\n\r", FALSE, pool);

  return SVN_NO_ERROR;   
} 


/* Create a status structure for NAME, and pass it off via the
   STATUS_FUNC/STATUS_BATON, assuming that the path is unversioned.
   This function should never be called on a versioned entry.

   NAME is the basename of the unversioned file whose status is being 
   requested. 

   PATH_KIND is the node kind of NAME as determined by the caller.

   ADM_ACCESS is an access baton for the working copy path. 

   PATTERNS points to a list of filename patterns which are marked 
   as ignored.

   None of the above arguments may be NULL.

   If NO_IGNORE is non-zero, the item will be added regardless of whether 
   it is ignored; otherwise we will only add the item if it does not 
   match any of the patterns in PATTERNS.

   Allocate everything in POOL.
*/
static svn_error_t *
add_unversioned_item (const char *name, 
                      svn_node_kind_t path_kind, 
                      svn_wc_adm_access_t *adm_access, 
                      apr_array_header_t *patterns,
                      svn_boolean_t no_ignore,
                      svn_wc_status_func_t status_func,
                      void *status_baton,
                      apr_pool_t *pool)
{
  int ignore_me = svn_cstring_match_glob_list (name, patterns);

  /* If we aren't ignoring it, add a status structure for this dirent. */
  if (no_ignore || ! ignore_me)
    SVN_ERR (add_status_structure 
             (svn_path_join (svn_wc_adm_access_path (adm_access), name, pool),
              adm_access, NULL, NULL, path_kind, FALSE, ignore_me,
              status_func, status_baton, pool));

  return SVN_NO_ERROR;
}


#ifdef STREAMY_STATUS_IN_PROGRESS
/* Add an unversioned item PATH to the given STATUSHASH.  This is a
   convenience wrapper around add_unversioned_item and takes the same
   parameters except: 
   
     - PATH is the full path; only its base name will be used.
     - IGNORES will have local ignores added to it.  

   It is assumed that the item is not to be ignored.
*/
static svn_error_t *
add_unversioned_path (const char *path,
                      svn_node_kind_t path_kind,
                      svn_wc_adm_access_t *adm_access,
                      apr_array_header_t *ignores,
                      svn_wc_status_func_t status_func,
                      void *status_baton,
                      apr_pool_t *pool)
{
  apr_array_header_t *patterns;
  patterns = apr_array_make (pool, 1, sizeof (const char *));
  SVN_ERR (collect_ignore_patterns (patterns, ignores, adm_access, pool));
  return add_unversioned_item (svn_path_basename (path, pool), path_kind, 
                               adm_access, patterns, TRUE, 
                               status_func, status_baton, pool);
}
#endif


/* Create status structures for all items that are NOT in ENTRIES
   (which is a list of PATH's versioned things) as unversioned items,
   and pass those structures to the STATUS_FUNC/STATUS_BATON.
 
   Use POOL for all allocations.

   IGNORES contains the list of patterns to be ignored.

   If NO_IGNORE is non-zero, all unversioned items will be added;
   otherwise we will only add the items that do not match any of the
   patterns in IGNORES.

   We need the IGNORES list of patterns even if NO_IGNORES is
   non-zero, because in that case we still need to distinguish between:

    (1) "Regular" unversioned items, i.e. files that haven't been
        placed under version control but don't match any of the
        patterns in IGNORES.  (These ultimately get their text_status
        set to svn_wc_status_unversioned.)

    (2) Items that would normally have been ignored because they match
        a pattern in IGNORES, but which are being represented in
        status structures anyway because the caller has explicitly
        requested _all_ items.  (These ultimately get their
        text_status set to svn_wc_status_ignored.)
*/
static svn_error_t *
add_unversioned_items (svn_wc_adm_access_t *adm_access,
                       apr_hash_t *entries,
                       apr_array_header_t *ignores,
                       svn_boolean_t no_ignore,
                       svn_wc_status_func_t status_func,
                       void *status_baton,
                       apr_pool_t *pool)
{
  apr_pool_t *subpool = svn_pool_create (pool);
  apr_hash_t *dirents;
  apr_hash_index_t *hi;
  apr_array_header_t *patterns = NULL;

  /* Read PATH's dirents. */
  SVN_ERR (svn_io_get_dirents (&dirents, svn_wc_adm_access_path (adm_access),
                               subpool));

  /* Unless specified, add default ignore regular expressions and try
     to add any svn:ignore properties from the parent directory. */
  if (ignores)
    {
      patterns = apr_array_make (subpool, 1, sizeof(const char *));
      SVN_ERR (collect_ignore_patterns (patterns, ignores, 
                                        adm_access, subpool));
    }

  /* Add empty status structures for each of the unversioned things. */
  for (hi = apr_hash_first (subpool, dirents); hi; hi = apr_hash_next (hi))
    {
      const void *key;
      apr_ssize_t klen;
      void *val;
      svn_node_kind_t *path_kind;

      apr_hash_this (hi, &key, &klen, &val);
      path_kind = val;
        
      /* If the dirent isn't in `.svn/entries'... */
      if (apr_hash_get (entries, key, klen))        
        continue;

      /* and we're not looking at .svn... */
      if (! strcmp (key, SVN_WC_ADM_DIR_NAME))
        continue;

      SVN_ERR (add_unversioned_item (key, *path_kind,
                                     adm_access, patterns, no_ignore,
                                     status_func, status_baton, pool));
    }

  svn_pool_destroy (subpool);

  return SVN_NO_ERROR;
}


/* Send svn_wc_status_t * structures for the directory PATH and for
   all its entries through STATUS_FUNC/STATUS_BATON.  ADM_ACCESS is an
   access baton for PATH, PARENT_ENTRY is the entry for the parent of
   PATH or NULL if PATH is a working copy root.

   If SKIP_THIS_DIR is TRUE, the directory's own status will not be
   reported.  However, upon recursing, all subdirs *will* be reported,
   regardless of this parameter's value.  */
static svn_error_t *
get_dir_status (const svn_wc_entry_t *parent_entry,
                svn_wc_adm_access_t *adm_access,
                apr_array_header_t *ignores,
                svn_boolean_t descend,
                svn_boolean_t get_all,
                svn_boolean_t no_ignore,
                svn_boolean_t skip_this_dir,
                svn_wc_status_func_t status_func,
                void *status_baton,
                svn_cancel_func_t cancel_func,
                void *cancel_baton,
                svn_wc_traversal_info_t *traversal_info,
                apr_pool_t *pool)
{
  apr_hash_t *entries;
  apr_hash_index_t *hi;
  const svn_wc_entry_t *dir_entry;
  const char *path;

  if (cancel_func)
    SVN_ERR (cancel_func (cancel_baton));

  /* Load entries file for the directory into the requested pool. */
  SVN_ERR (svn_wc_entries_read (&entries, adm_access, FALSE, pool));

  /* Add the unversioned items to the status output. */
  SVN_ERR (add_unversioned_items (adm_access, entries, ignores, no_ignore,
                                  status_func, status_baton, pool));
  path = svn_wc_adm_access_path (adm_access);
  SVN_ERR (svn_wc_entry (&dir_entry, path, adm_access, FALSE, pool));

  /* If "this dir" has "svn:externals" property set on it, store its name
     in traversal_info. */
  if (traversal_info)
    {
      const svn_string_t *val;
      SVN_ERR (svn_wc_prop_get (&val, SVN_PROP_EXTERNALS, path, adm_access,
                                pool));
      if (val)
        {
          apr_pool_t *dup_pool = traversal_info->pool;
          const char *dup_path = apr_pstrdup (dup_pool, path);
          const char *dup_val = apr_pstrmemdup (dup_pool, val->data, val->len);
          apr_hash_set (traversal_info->externals_old,
                        dup_path, APR_HASH_KEY_STRING, dup_val);
          apr_hash_set (traversal_info->externals_new,
                        dup_path, APR_HASH_KEY_STRING, dup_val);
        }
    }

  /* Handle "this-dir" first. */
  if (! skip_this_dir)
    SVN_ERR (add_status_structure (path, adm_access, dir_entry, 
                                   parent_entry, svn_node_dir,
                                   get_all, FALSE, status_func, 
                                   status_baton, pool));

  /* Loop over entries hash */
  for (hi = apr_hash_first (pool, entries); hi; hi = apr_hash_next (hi))
    {
      const void *key;
      void *val;
      const char *base_name;
      svn_wc_entry_t *entry;
      svn_node_kind_t fullpath_kind;
      const char *fullpath;

      /* Get the next dirent */
      apr_hash_this (hi, &key, NULL, &val);
      base_name = key;
      entry = val;
      fullpath = svn_path_join (path, base_name, pool);

      /* ### todo: What if the subdir is from another repository? */
          
      /* Skip "this-dir". */
      if (strcmp (base_name, SVN_WC_ENTRY_THIS_DIR) == 0)
        continue;

      /* Get the entry's kind on disk. */
      SVN_ERR (svn_io_check_path (fullpath, &fullpath_kind, pool));

      if (fullpath_kind == svn_node_dir)
        {
          /* Directory entries are incomplete.  We must get their full
             entry from their own THIS_DIR entry.  svn_wc_entry does
             this for us if it can.

             Of course, if there has been a kind-changing replacement
             (for example, there is an entry for a file 'foo', but
             'foo' exists as a *directory* on disk), we don't want to
             reach down into that subdir to try to flesh out a
             "complete entry".  */

          const svn_wc_entry_t *fullpath_entry = entry;
          
          if (entry->kind == fullpath_kind)
            SVN_ERR (svn_wc_entry (&fullpath_entry, fullpath, 
                                   adm_access, FALSE, pool));

          /* Descend only if the subdirectory is a working copy
             directory (and DESCEND is non-zero ofcourse)  */
          if (descend && fullpath_entry != entry)
            {
              svn_wc_adm_access_t *dir_access;
              SVN_ERR (svn_wc_adm_retrieve (&dir_access, adm_access,
                                            fullpath, pool));
              SVN_ERR (get_dir_status (dir_entry, dir_access, ignores, descend,
                                       get_all, no_ignore, FALSE, status_func,
                                       status_baton, cancel_func,
                                       cancel_baton, traversal_info, pool));
            }
          else
            {
              SVN_ERR (add_status_structure (fullpath, adm_access, 
                                             fullpath_entry, dir_entry, 
                                             fullpath_kind, get_all, FALSE,
                                             status_func, status_baton, pool));
            }
        }
      else
        {
          /* File entries are ... just fine! */
          SVN_ERR (add_status_structure (fullpath, adm_access, entry, 
                                         dir_entry, fullpath_kind, get_all, 
                                         FALSE, status_func, status_baton, 
                                         pool));
        }
    }

  return SVN_NO_ERROR;
}



/*** Helpers ***/

/* A faux status callback function for stashing STATUS item in an hash
   (which is the BATON), keyed on PATH.  This implements the
   svn_wc_status_func_t interface. */
static void
hash_stash (void *baton,
            const char *path,
            svn_wc_status_t *status)
{
  apr_hash_t *stat_hash = baton;
  apr_pool_t *hash_pool = apr_hash_pool_get (stat_hash);
  assert (! apr_hash_get (stat_hash, path, APR_HASH_KEY_STRING));
  apr_hash_set (stat_hash, apr_pstrdup (hash_pool, path), 
                APR_HASH_KEY_STRING, svn_wc_dup_status (status, hash_pool));
}


/* Look up the key PATH in STATUSHASH.  If the value doesn't yet
   exist, create a new status struct using the hash's pool.  Set the
   status structure's "network" fields to REPOS_TEXT_STATUS,
   REPOS_PROP_STATUS.  If either of these fields is 0, it will be
   ignored.  */
static svn_error_t *
tweak_statushash (apr_hash_t *statushash,
                  svn_wc_adm_access_t *adm_access,
                  const char *path,
                  svn_boolean_t is_dir,
                  enum svn_wc_status_kind repos_text_status,
                  enum svn_wc_status_kind repos_prop_status)
{
  svn_wc_status_t *statstruct;
  apr_pool_t *pool = apr_hash_pool_get (statushash);

  /* If you want temporary debugging info... */
  /* {
     apr_hash_index_t *hi;
     char buf[200];
     
     printf("---Tweaking statushash:  editing path `%s'\n", path);
     
     for (hi = apr_hash_first (pool, statushash); 
     hi; 
     hi = apr_hash_next (hi))
     {
     const void *key;
     void *val;
     apr_ssize_t klen;
         
     apr_hash_this (hi, &key, &klen, &val);
     snprintf(buf, klen+1, (const char *)key);
     printf("    %s\n", buf);
     }
     fflush(stdout);
     }
  */
  
  /* Is PATH already a hash-key? */
  statstruct = (svn_wc_status_t *) apr_hash_get (statushash, path,
                                                 APR_HASH_KEY_STRING);
  /* If not, make it so. */
  if (! statstruct)
    {
      /* Need a path with the same lifetime as the hash */
      const char *path_dup = apr_pstrdup (pool, path);
      svn_wc_adm_access_t *item_access;
      if (repos_text_status == svn_wc_status_added)
        item_access = NULL;
      else if (is_dir)
        SVN_ERR (svn_wc_adm_retrieve (&item_access, adm_access, path, pool));
      else
        SVN_ERR (svn_wc_adm_retrieve (&item_access, adm_access,
                                      svn_path_dirname (path, pool), pool));

      /* Use the public API to get a statstruct: */
      SVN_ERR (svn_wc_status (&statstruct, path, item_access, pool));

      /* Put the path/struct into the hash. */
      apr_hash_set (statushash, path_dup, APR_HASH_KEY_STRING, statstruct);
    }

  /* Tweak the structure's repos fields. */
  if (repos_text_status)
    statstruct->repos_text_status = repos_text_status;
  if (repos_prop_status)
    statstruct->repos_prop_status = repos_prop_status;
  
  return SVN_NO_ERROR;
}



/*** Editor batons ***/

struct edit_baton
{
  /* For status, the "destination" of the edit and whether to honor
     any paths that are 'below'.  */
  const char *anchor;
  const char *target;
  svn_wc_adm_access_t *adm_access;
  svn_boolean_t descend;

  /* Do we want all statuses (instead of just the interesting ones) ? */
  svn_boolean_t get_all;
 
  /* Ignore the svn:ignores. */
  svn_boolean_t no_ignore;

  /* The youngest revision in the repository.  This is a reference
     because this editor returns youngest rev to the driver directly,
     as well as in each statushash entry. */
  svn_revnum_t *youngest_revision;

  /* Subversion configuration hash. */
  apr_hash_t *config;

  /* Status function/baton. */
  svn_wc_status_func_t status_func;
  void *status_baton;

  /* Cancellation function/baton. */
  svn_cancel_func_t cancel_func;
  void *cancel_baton;

  /* Externals info harvested during the status run. */
  svn_wc_traversal_info_t *traversal_info;

  /* Status item for the path represented by the anchor of the edit. */
  svn_wc_status_t *anchor_status;

  /* Was open_root() called for this edit drive? */
  svn_boolean_t root_opened;

  /* The pool which the editor uses for the whole tree-walk.*/
  apr_pool_t *pool;
};


struct dir_baton
{
  /* The path to this directory. */
  const char *path;

  /* Basename of this directory. */
  const char *name;

  /* The global edit baton. */
  struct edit_baton *edit_baton;

  /* Baton for this directory's parent, or NULL if this is the root
     directory. */
  struct dir_baton *parent_baton;

  /* 'svn status' shouldn't print status lines for things that are
     added;  we're only interest in asking if objects that the user
     *already* has are up-to-date or not.  Thus if this flag is set,
     the next two will be ignored.  :-)  */
  svn_boolean_t added;

  /* Gets set iff there's a change to this directory's properties, to
     guide us when syncing adm files later. */
  svn_boolean_t prop_changed;

  /* This means (in terms of 'svn status') that some child was deleted
     or added to the directory */
  svn_boolean_t text_changed;

  /* Working copy status structures for children of this directory.
     This hash maps const char * paths (relative to the root of the
     edit) to svn_wc_status_t * status items. */
  apr_hash_t *statii;

  /* The pool in which this baton itself is allocated. */
  apr_pool_t *pool;
};



/* Create a new dir_baton for subdir PATH. */
static svn_error_t *
make_dir_baton (void **dir_baton,
                const char *path,
                struct edit_baton *edit_baton,
                struct dir_baton *parent_baton,
                apr_pool_t *pool)
{
  struct dir_baton *pb = parent_baton;
  struct edit_baton *eb = edit_baton;
  struct dir_baton *d = apr_pcalloc (pool, sizeof (*d));
  const char *full_path; 
  svn_wc_status_t *parent_status;

  /* Don't do this.  Just do NOT do this to me. */
  if (pb && (! path))
    abort();

  /* Construct the full path of this directory. */
  if (pb)
    full_path = svn_path_join (eb->anchor, path, pool);
  else
    full_path = apr_pstrdup (pool, eb->anchor);

  /* Finish populating the baton members. */
  d->path         = full_path;
  d->name         = path ? (svn_path_basename (path, pool)) : NULL;
  d->edit_baton   = edit_baton;
  d->parent_baton = parent_baton;
  d->pool         = pool;
  d->statii       = apr_hash_make (pool);

  /* Get the status for this path's children.  Of course, we only want
     to do this if the path is versioned as a directory. */
  if (pb)
    parent_status = apr_hash_get (pb->statii, d->path, APR_HASH_KEY_STRING);
  else
    parent_status = eb->anchor_status;
  if (parent_status
      && (parent_status->text_status != svn_wc_status_unversioned)
      && (parent_status->text_status != svn_wc_status_absent)
      && (parent_status->text_status != svn_wc_status_obstructed)
      && (parent_status->entry->kind == svn_node_dir)
      && (eb->descend || (! pb)))
    {
      apr_array_header_t *ignores;
      svn_wc_adm_access_t *dir_access;
      SVN_ERR (svn_wc_adm_retrieve (&dir_access, eb->adm_access, 
                                    d->path, pool));
      SVN_ERR (svn_wc_get_default_ignores (&ignores, eb->config, pool));
      SVN_ERR (get_dir_status (parent_status->entry, dir_access, ignores, 
                               FALSE, TRUE, TRUE, TRUE, hash_stash, d->statii, 
                               NULL, NULL, eb->traversal_info, pool));
    }

  *dir_baton = d;
  return SVN_NO_ERROR;
}


struct file_baton
{
  /* The global edit baton. */
  struct edit_baton *edit_baton;

  /* Baton for this file's parent directory. */
  struct dir_baton *dir_baton;

  /* Pool specific to this file_baton. */
  apr_pool_t *pool;

  /* Name of this file (its entry in the directory). */
  const char *name;

  /* Path to this file, either abs or relative to the change-root. */
  const char *path;

  /* 'svn status' shouldn't print status lines for things that are
     added;  we're only interest in asking if objects that the user
     *already* has are up-to-date or not.  Thus if this flag is set,
     the next two will be ignored.  :-)  */
  svn_boolean_t added;

  /* This gets set if the file underwent a text change, which guides
     the code that syncs up the adm dir and working copy. */
  svn_boolean_t text_changed;

  /* This gets set if the file underwent a prop change, which guides
     the code that syncs up the adm dir and working copy. */
  svn_boolean_t prop_changed;

};


/* Make a file baton, using a new subpool of PARENT_DIR_BATON's pool.
   NAME is just one component, not a path. */
static struct file_baton *
make_file_baton (struct dir_baton *parent_dir_baton, 
                 const char *path,
                 apr_pool_t *pool)
{
  struct dir_baton *pb = parent_dir_baton;
  struct edit_baton *eb = pb->edit_baton;
  struct file_baton *f = apr_pcalloc (pool, sizeof (*f));
  const char *full_path;
 
  /* Construct the full path of this directory. */
  if (pb)
    full_path = svn_path_join (eb->anchor, path, pool);
  else
    full_path = apr_pstrdup (pool, eb->anchor);

  /* Finish populating the baton members. */
  f->path       = full_path;
  f->name       = svn_path_basename (path, pool);
  f->pool       = pool;
  f->dir_baton  = pb;
  f->edit_baton = eb;

  return f;
}


static svn_boolean_t 
is_sendable_status (svn_wc_status_t *status,
                    struct edit_baton *eb)
{
  /* If the repository status was touched at all, it's interesting. */
  if (status->repos_text_status != svn_wc_status_none)
    return TRUE;
  if (status->repos_prop_status != svn_wc_status_none)
    return TRUE;

  /* If the item is ignored, and we don't want ignores, skip it. */
  if ((status->text_status == svn_wc_status_ignored) && (! eb->no_ignore))
    return FALSE;

  /* If we want everything, we obviously want this single-item subset
     of everything. */
  if (eb->get_all)
    return TRUE;

  /* If the item is unversioned, display it. */
  if (status->text_status == svn_wc_status_unversioned)
    return TRUE;

  /* If the text or property states are interesting, send it. */
  if ((status->text_status != svn_wc_status_none)
      && (status->text_status != svn_wc_status_normal))
    return TRUE;
  if ((status->prop_status != svn_wc_status_none)
      && (status->prop_status != svn_wc_status_normal))
    return TRUE;

  /* If it's locked or switched, send it. */
  if (status->locked)
    return TRUE;
  if (status->switched)
    return TRUE;

  /* Otherwise, don't send it. */
  return FALSE;
}


static svn_error_t *
handle_statii (struct edit_baton *eb,
               svn_wc_entry_t *dir_entry,
               const char *dir_path,
               apr_hash_t *statii,
               svn_boolean_t descend,
               apr_pool_t *pool)
{
  apr_array_header_t *ignores;
  apr_hash_index_t *hi;

  /* Read the default ignores from the config hash. */
  SVN_ERR (svn_wc_get_default_ignores (&ignores, eb->config, pool));

  /* Loop over all the statuses still in our hash, handling each one. */
  for (hi = apr_hash_first (pool, statii); hi; hi = apr_hash_next (hi))
    {
      const void *key;
      void *val;
      svn_wc_status_t *status;
      const char *fullpath;

      apr_hash_this (hi, &key, NULL, &val);
      /* fullpath = svn_path_join (dir_path, key, pool); */
      fullpath = key;
      status = val;
      if (descend && status->entry && (status->entry->kind == svn_node_dir) )
        {
          svn_wc_adm_access_t *dir_access;
          SVN_ERR (svn_wc_adm_retrieve (&dir_access, eb->adm_access,
                                        key, pool));
          SVN_ERR (get_dir_status (dir_entry, dir_access,
                                   ignores, TRUE, eb->get_all, 
                                   eb->no_ignore, TRUE, eb->status_func, 
                                   eb->status_baton,
                                   eb->cancel_func, eb->cancel_baton, 
                                   eb->traversal_info, pool));
        }
      if (is_sendable_status (status, eb))
        (eb->status_func)(eb->status_baton, fullpath, status);
    }
    
  return SVN_NO_ERROR;
}


/*----------------------------------------------------------------------*/

/*** The callbacks we'll plug into an svn_delta_editor_t structure. ***/

static svn_error_t *
set_target_revision (void *edit_baton, 
                     svn_revnum_t target_revision,
                     apr_pool_t *pool)
{
  struct edit_baton *eb = edit_baton;
  *(eb->youngest_revision) = target_revision;
  return SVN_NO_ERROR;
}


static svn_error_t *
open_root (void *edit_baton,
           svn_revnum_t base_revision,
           apr_pool_t *pool,
           void **dir_baton)
{
  struct edit_baton *eb = edit_baton;
  eb->root_opened = TRUE;
  return make_dir_baton (dir_baton, NULL, eb, NULL, pool);
}


static svn_error_t *
delete_entry (const char *path,
              svn_revnum_t revision,
              void *parent_baton,
              apr_pool_t *pool)
{
  struct dir_baton *db = parent_baton;
  struct edit_baton *eb = db->edit_baton;
  apr_hash_t *entries;
  const char *name = svn_path_basename (path, pool);
  const char *full_path = svn_path_join (eb->anchor, path, pool);
  const char *dir_path;
  svn_node_kind_t kind;
  svn_wc_adm_access_t *adm_access;
  const char *hash_key;

  /* Note:  when something is deleted, it's okay to tweak the
     statushash immediately.  No need to wait until close_file or
     close_dir, because there's no risk of having to honor the 'added'
     flag.  We already know this item exists in the working copy. */

  /* Read the parent's entries file.  If the deleted thing is not
     versioned in this working copy, it was probably deleted via this
     working copy.  No need to report such a thing. */
  /* ### use svn_wc_entry() instead? */
  SVN_ERR (svn_io_check_path (full_path, &kind, pool));
  if (kind == svn_node_dir)
    {
      dir_path = full_path;
      hash_key = SVN_WC_ENTRY_THIS_DIR;
    }
  else
    {
      dir_path = svn_path_dirname (full_path, pool);
      hash_key = name;
    }
  SVN_ERR (svn_wc_adm_retrieve (&adm_access, eb->adm_access, dir_path, pool));
  SVN_ERR (svn_wc_entries_read (&entries, adm_access, FALSE, pool));
  if (apr_hash_get (entries, hash_key, APR_HASH_KEY_STRING))
    SVN_ERR (tweak_statushash (db->statii, eb->adm_access,
                               full_path, kind == svn_node_dir,
                               svn_wc_status_deleted, 0));

  /* Mark the parent dir -- it lost an entry (unless that parent dir
     is the root node and we're not supposed to report on the root
     node).  */
  if ((db->parent_baton) && (! eb->target))
    SVN_ERR (tweak_statushash (db->parent_baton->statii, eb->adm_access,
                               db->path, kind == svn_node_dir,
                               svn_wc_status_modified, 0));

  return SVN_NO_ERROR;
}


static svn_error_t *
add_directory (const char *path,
               void *parent_baton,
               const char *copyfrom_path,
               svn_revnum_t copyfrom_revision,
               apr_pool_t *pool,
               void **child_baton)
{
  struct dir_baton *pb = parent_baton;
  struct edit_baton *eb = pb->edit_baton;
  struct dir_baton *new_db;

  SVN_ERR (make_dir_baton ((void **)(&new_db), path, eb, pb, pool));

  /* Make this dir as added. */
  new_db->added = TRUE;

  /* Mark the parent as changed;  it gained an entry. */
  pb->text_changed = TRUE;

  *child_baton = new_db;
  return SVN_NO_ERROR;
}


static svn_error_t *
open_directory (const char *path,
                void *parent_baton,
                svn_revnum_t base_revision,
                apr_pool_t *pool,
                void **child_baton)
{
  struct dir_baton *pb = parent_baton;
  return make_dir_baton (child_baton, path, pb->edit_baton, pb, pool);
}


static svn_error_t *
change_dir_prop (void *dir_baton,
                 const char *name,
                 const svn_string_t *value,
                 apr_pool_t *pool)
{
  struct dir_baton *db = dir_baton;
  if (svn_wc_is_normal_prop (name))    
    db->prop_changed = TRUE;
  return SVN_NO_ERROR;
}



static svn_error_t *
close_directory (void *dir_baton,
                 apr_pool_t *pool)
{
  struct dir_baton *db = dir_baton;
  struct dir_baton *pb = db->parent_baton;
  struct edit_baton *eb = db->edit_baton;
  svn_wc_status_t *dir_status = NULL;

  /* If nothing has changed, return. */
  if (db->added || db->prop_changed || db->text_changed)
    {
      /* If this directory was added, add the directory to the status hash. */
      if (db->added)
        SVN_ERR (tweak_statushash (db->statii,
                                   db->edit_baton->adm_access,
                                   db->path, TRUE,
                                   svn_wc_status_added,
                                   db->prop_changed ? svn_wc_status_added 
                                                    : 0));
      
      /* Else, if this a) is not the root directory, or b) *is* the root
         directory, and we are supposed to report on it, then mark the
         existing directory in the statushash. */
      else if (pb && (! db->edit_baton->target))
        SVN_ERR (tweak_statushash (pb->statii,
                                   db->edit_baton->adm_access,
                                   db->path, TRUE,
                                   db->text_changed ? svn_wc_status_modified 
                                                    : 0,
                                   db->prop_changed ? svn_wc_status_modified 
                                                    : 0));
    }

  /* Handle this directory's statuses, and then note in the parent
     that this has been done. */
  if (pb && eb->descend)
    {
      dir_status = apr_hash_get (pb->statii, db->path, APR_HASH_KEY_STRING);
      SVN_ERR (handle_statii (db->edit_baton, 
                              dir_status ? dir_status->entry : NULL, 
                              db->path, db->statii, TRUE, pool));
      apr_hash_set (pb->statii, db->path, APR_HASH_KEY_STRING, NULL);
    }
  else if (! pb)
    {
      SVN_ERR (handle_statii (db->edit_baton, NULL, db->path, 
                              db->statii, eb->descend, pool));
    }
  return SVN_NO_ERROR;
}



static svn_error_t *
add_file (const char *path,
          void *parent_baton,
          const char *copyfrom_path,
          svn_revnum_t copyfrom_revision,
          apr_pool_t *pool,
          void **file_baton)
{
  struct dir_baton *pb = parent_baton;
  struct file_baton *new_fb = make_file_baton (pb, path, pool);

  /* Mark parent dir as changed */  
  pb->text_changed = TRUE;

  /* Make this file as added. */
  new_fb->added = TRUE;

  *file_baton = new_fb;
  return SVN_NO_ERROR;
}


static svn_error_t *
open_file (const char *path,
           void *parent_baton,
           svn_revnum_t base_revision,
           apr_pool_t *pool,
           void **file_baton)
{
  struct dir_baton *pb = parent_baton;
  struct file_baton *new_fb = make_file_baton (pb, path, pool);

  *file_baton = new_fb;
  return SVN_NO_ERROR;
}


static svn_error_t *
apply_textdelta (void *file_baton, 
                 const char *base_checksum,
                 apr_pool_t *pool,
                 svn_txdelta_window_handler_t *handler,
                 void **handler_baton)
{
  struct file_baton *fb = file_baton;
  
  /* Mark file as having textual mods. */
  fb->text_changed = TRUE;

  /* Send back a NULL window handler -- we don't need the actual diffs. */
  *handler_baton = NULL;
  *handler = svn_delta_noop_window_handler;

  return SVN_NO_ERROR;
}


static svn_error_t *
change_file_prop (void *file_baton,
                  const char *name,
                  const svn_string_t *value,
                  apr_pool_t *pool)
{
  struct file_baton *fb = file_baton;
  if (svn_wc_is_normal_prop (name))
    fb->prop_changed = TRUE;
  return SVN_NO_ERROR;
}


static svn_error_t *
close_file (void *file_baton,
            const char *text_checksum,  /* ignored, as we receive no data */
            apr_pool_t *pool)
{
  struct file_baton *fb = file_baton;

  /* If nothing has changed, return. */
  if (! (fb->added || fb->prop_changed || fb->text_changed))
    return SVN_NO_ERROR;

  /* If this is a new file, add it to the statushash. */
  if (fb->added)
    SVN_ERR (tweak_statushash (fb->dir_baton->statii,
                               fb->edit_baton->adm_access,
                               fb->path, FALSE,
                               svn_wc_status_added, 
                               fb->prop_changed ? svn_wc_status_added : 0));
  /* Else, mark the existing file in the statushash. */
  else
    SVN_ERR (tweak_statushash (fb->dir_baton->statii,
                               fb->edit_baton->adm_access,
                               fb->path, FALSE,
                               fb->text_changed ? svn_wc_status_modified : 0,
                               fb->prop_changed ? svn_wc_status_modified : 0));

  return SVN_NO_ERROR;
}


static svn_error_t *
close_edit (void *edit_baton,
            apr_pool_t *pool)
{
  struct edit_baton *eb = edit_baton;
  if ((! eb->root_opened) && (! eb->target))
    {
      apr_array_header_t *ignores;
      SVN_ERR (svn_wc_get_default_ignores (&ignores, eb->config, pool));
      SVN_ERR (get_dir_status (NULL, eb->adm_access, ignores, 
                               eb->descend, eb->get_all, eb->no_ignore, 
                               FALSE, eb->status_func, eb->status_baton, 
                               eb->cancel_func, eb->cancel_baton,
                               eb->traversal_info, pool));
    }
  return SVN_NO_ERROR;
}



/*** Public API ***/

svn_error_t *
svn_wc_get_status_editor (const svn_delta_editor_t **editor,
                          void **edit_baton,
                          svn_revnum_t *youngest,
                          const char *path,
                          svn_wc_adm_access_t *adm_access,
                          apr_hash_t *config,
                          svn_boolean_t descend,
                          svn_boolean_t get_all,
                          svn_boolean_t no_ignore,
                          svn_wc_status_func_t status_func,
                          void *status_baton,
                          svn_cancel_func_t cancel_func,
                          void *cancel_baton,
                          svn_wc_traversal_info_t *traversal_info,
                          apr_pool_t *pool)
{
  struct edit_baton *eb;
  const char *anchor, *target;
  svn_delta_editor_t *tree_editor = svn_delta_default_editor (pool);

  /* Get the editor's anchor/target. */
  SVN_ERR (svn_wc_get_actual_target (path, &anchor, &target, pool));

  /* Construct an edit baton. */
  eb = apr_palloc (pool, sizeof (*eb));
  eb->descend           = descend;
  eb->youngest_revision = youngest;
  eb->adm_access        = adm_access;
  eb->config            = config;
  eb->get_all           = get_all;
  eb->no_ignore         = no_ignore;
  eb->status_func       = status_func;
  eb->status_baton      = status_baton;
  eb->cancel_func       = cancel_func;
  eb->cancel_baton      = cancel_baton;
  eb->traversal_info    = traversal_info;
  eb->anchor            = anchor;
  eb->target            = target;
  eb->root_opened       = FALSE;

  /* The edit baton's status structure maps to PATH, and the editor
     have to be aware of whether that is the anchor or the target. */
  SVN_ERR (svn_wc_status (&(eb->anchor_status), anchor, adm_access, pool));

  /* Construct an editor. */
  tree_editor->set_target_revision = set_target_revision;
  tree_editor->open_root = open_root;
  tree_editor->delete_entry = delete_entry;
  tree_editor->add_directory = add_directory;
  tree_editor->open_directory = open_directory;
  tree_editor->change_dir_prop = change_dir_prop;
  tree_editor->close_directory = close_directory;
  tree_editor->add_file = add_file;
  tree_editor->open_file = open_file;
  tree_editor->apply_textdelta = apply_textdelta;
  tree_editor->change_file_prop = change_file_prop;
  tree_editor->close_file = close_file;
  tree_editor->close_edit = close_edit;

  /* Conjoin a cancellation editor with our status editor. */
  SVN_ERR (svn_delta_get_cancellation_editor (cancel_func, cancel_baton,
                                              tree_editor, eb, editor,
                                              edit_baton, pool));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc_get_default_ignores (apr_array_header_t **patterns,
                            apr_hash_t *config,
                            apr_pool_t *pool)
{
  svn_config_t *cfg = config ? apr_hash_get (config, 
                                             SVN_CONFIG_CATEGORY_CONFIG, 
                                             APR_HASH_KEY_STRING) : NULL;
  const char *val;

  /* Check the Subversion run-time configuration for global ignores.
     If no configuration value exists, we fall back to our defaults. */
  svn_config_get (cfg, &val, SVN_CONFIG_SECTION_MISCELLANY, 
                  SVN_CONFIG_OPTION_GLOBAL_IGNORES,
                  SVN_CONFIG_DEFAULT_GLOBAL_IGNORES);
  *patterns = apr_array_make (pool, 16, sizeof (const char *));

  /* Split the patterns on whitespace, and stuff them into *PATTERNS. */
  svn_cstring_split_append (*patterns, val, "\n\r\t\v ", FALSE, pool);
  return SVN_NO_ERROR;
}

                        
svn_error_t *
svn_wc_status (svn_wc_status_t **status,
               const char *path,
               svn_wc_adm_access_t *adm_access,
               apr_pool_t *pool)
{
  svn_wc_status_t *s;
  const svn_wc_entry_t *entry = NULL;
  const svn_wc_entry_t *parent_entry = NULL;

  if (adm_access)
    SVN_ERR (svn_wc_entry (&entry, path, adm_access, FALSE, pool));

  /* If we have an entry, and PATH is not a root, then we need a parent
     entry */
  if (entry)
    {
      svn_boolean_t is_root;
      SVN_ERR (svn_wc_is_wc_root (&is_root, path, adm_access, pool));
      if (! is_root)
        {
          const char *parent_path = svn_path_dirname (path, pool);
          svn_wc_adm_access_t *parent_access;
          SVN_ERR (svn_wc_adm_open (&parent_access, NULL, parent_path,
                                    FALSE, FALSE, pool));
          SVN_ERR (svn_wc_entry (&parent_entry, parent_path, parent_access,
                                 FALSE, pool));
        }
    }

  SVN_ERR (assemble_status (&s, path, adm_access, entry, parent_entry,
                            svn_node_unknown, TRUE, FALSE, pool));
  *status = s;
  return SVN_NO_ERROR;
}


svn_wc_status_t *
svn_wc_dup_status (svn_wc_status_t *orig_stat,
                   apr_pool_t *pool)
{
  svn_wc_status_t *new_stat = apr_palloc (pool, sizeof (*new_stat));
  
  /* Shallow copy all members. */
  *new_stat = *orig_stat;
  
  /* No go back and dup the deep item. */
  if (orig_stat->entry)
    new_stat->entry = svn_wc_entry_dup (orig_stat->entry, pool);

  /* Return the new hotness. */
  return new_stat;
}
