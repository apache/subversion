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

/* Helper routine: add to *PATTERNS patterns from the value of
   the SVN_PROP_IGNORE property set on DIRPATH.  If there is no such
   property, or the property contains no patterns, do nothing.
   Otherwise, add to *PATTERNS a list of (const char *) patterns to
   match. */
static svn_error_t *
add_ignore_patterns (svn_wc_adm_access_t *adm_access,
                     apr_array_header_t *patterns,
                     apr_pool_t *pool)
{
  const svn_string_t *value;

  /* Try to load the SVN_PROP_IGNORE property. */
  SVN_ERR (svn_wc_prop_get (&value, SVN_PROP_IGNORE,
                            svn_wc_adm_access_path (adm_access), adm_access,
                            pool));

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
   and store it in STATUSHASH.  */
static svn_error_t *
add_status_structure (apr_hash_t *statushash,
                      const char *path,
                      svn_wc_adm_access_t *adm_access,
                      const svn_wc_entry_t *entry,
                      const svn_wc_entry_t *parent_entry,
                      svn_node_kind_t path_kind,
                      svn_boolean_t get_all,
                      svn_boolean_t is_ignored,
                      svn_wc_notify_func_t notify_func,
                      void *notify_baton,
                      apr_pool_t *pool)
{
  svn_wc_status_t *statstruct;
  
  SVN_ERR (assemble_status (&statstruct, path, adm_access, entry, parent_entry,
                            path_kind, get_all, is_ignored, pool));
  if (statstruct)
    {
      apr_hash_set (statushash, path, APR_HASH_KEY_STRING, statstruct);
      if (notify_func != NULL)
        (*notify_func) (notify_baton, path, svn_wc_notify_status,
                        statstruct->entry ? 
                         statstruct->entry->kind : svn_node_unknown,
                        NULL,
                        svn_wc_notify_state_inapplicable,
                        svn_wc_notify_state_inapplicable,
                        SVN_INVALID_REVNUM);

    }
  
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

  /* Copy default ignores into the local PATTERNS array. */
  for (i = 0; i < ignores->nelts; i++)
    {
      const char *ignore = APR_ARRAY_IDX (ignores, i, const char *);
      (*((const char **) apr_array_push (patterns))) = ignore;
    }

  /* Then add any svn:ignore globs to the PATTERNS array. */
  SVN_ERR (add_ignore_patterns (adm_access, patterns, pool));

  return SVN_NO_ERROR;   
} 


/* Add a status structure for NAME to the STATUSHASH, assuming 
   that the file is unversioned.  This function should never
   be called on a versioned entry. 

   NAME is the basename of the unversioned file whose status is being 
   requested. 

   PATH_KIND is the node kind of NAME as determined by the caller.

   STATUSHASH is a mapping from path to status structure.  On entry, it 
   may or may not contain status structures for other paths.  Upon return
   it may contain a status structure for NAME.

   ADM_ACCESS is an access baton for the working copy path. 

   PATTERNS points to a list of filename patterns which are marked 
   as ignored.

   None of the above arguments may be NULL.

   If NO_IGNORE is non-zero, the item will be added regardless of whether 
   it is ignored; otherwise we will only add the item if it does not 
   match any of the patterns in PATTERNS.
   
   If a status structure for the item is added, NOTIFY_FUNC will called 
   with the path of the item and the NOTIFY_BATON.  NOTIFY_FUNC may be 
   NULL if no such notification is required.

   Allocate everything in POOL.
*/
static svn_error_t *
add_unversioned_item (const char *name, 
                      svn_node_kind_t path_kind, 
                      apr_hash_t *statushash,
                      svn_wc_adm_access_t *adm_access, 
                      apr_array_header_t *patterns,
                      svn_boolean_t no_ignore,
                      svn_wc_notify_func_t notify_func,
                      void *notify_baton,
                      apr_pool_t *pool)
{
  int ignore_me;
  const char *printable_path;

  ignore_me = svn_cstring_match_glob_list (name, patterns);

  /* If we aren't ignoring it, add a status structure for this dirent. */
  if (no_ignore || ! ignore_me)
    {
      printable_path = svn_path_join (svn_wc_adm_access_path (adm_access),
                                      name, pool);
      
      /* Add this item to the status hash. */
      SVN_ERR (add_status_structure (statushash,
                                     printable_path,
                                     adm_access,
                                     NULL, /* no entry */
                                     NULL,
                                     path_kind,
                                     FALSE,
                                     ignore_me, /* is_ignored */
                                     notify_func,
                                     notify_baton,
                                     pool));
    }

  return SVN_NO_ERROR;
}

/* Add an unversioned item PATH to the given STATUSHASH.
   This is a convenience wrapper around add_unversioned_item and takes the
   same parameters except:
   PATH is the full path; only its base name will be used.
   DEFAULT_IGNORES will have local ignores added to it.
   It is assumed that the item is not to be ignored.
*/
static svn_error_t *
add_unversioned_path (const char *path,
                      svn_node_kind_t path_kind,
                      apr_hash_t *statushash,
                      svn_wc_adm_access_t *adm_access,
                      apr_array_header_t *default_ignores,
                      svn_wc_notify_func_t notify_func,
                      void *notify_baton,
                      apr_pool_t *pool)
{
  char *name;
  apr_array_header_t *patterns;

  patterns = apr_array_make (pool, 1, sizeof(const char *));
  SVN_ERR (collect_ignore_patterns (patterns, default_ignores, adm_access,
                                    pool));

  name = svn_path_basename (path, pool);
  return add_unversioned_item (name, path_kind, statushash, adm_access,
                               patterns, TRUE, notify_func, notify_baton,
                               pool);
}

/* Add all items that are NOT in ENTRIES (which is a list of PATH's
   versioned things) to the STATUSHASH as unversioned items,

   allocating everything in POOL.

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
                       apr_hash_t *statushash,
                       apr_array_header_t *ignores,
                       svn_boolean_t no_ignore,
                       svn_wc_notify_func_t notify_func,
                       void *notify_baton,
                       apr_pool_t *pool)
{
  apr_pool_t *subpool = svn_pool_create (pool);
  apr_hash_t *dirents;
  apr_hash_index_t *hi;
  apr_array_header_t *patterns;

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
  else
    patterns = NULL;

  /* Add empty status structures for each of the unversioned things. */
  for (hi = apr_hash_first (subpool, dirents); hi; hi = apr_hash_next (hi))
    {
      const void *key;
      apr_ssize_t klen;
      void *val;
      const char *keystring;
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

      SVN_ERR (add_unversioned_item (keystring, *path_kind, statushash, 
                                     adm_access, patterns, no_ignore,
                                     notify_func, notify_baton, pool));
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

/* Fill STATUSHASH with (pointers to) svn_wc_status_t structures for the
   directory PATH and for all its entries.  ADM_ACCESS is an access baton
   for PATH, PARENT_ENTRY is the entry for the parent of PATH or NULL if
   PATH is a working copy root. */
static svn_error_t *
get_dir_status (apr_hash_t *statushash,
                const svn_wc_entry_t *parent_entry,
                svn_wc_adm_access_t *adm_access,
                apr_array_header_t *ignores,
                svn_boolean_t descend,
                svn_boolean_t get_all,
                svn_boolean_t no_ignore,
                svn_wc_notify_func_t notify_func,
                void *notify_baton,
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
  SVN_ERR (add_unversioned_items (adm_access, entries, statushash,
                                  ignores, no_ignore,
                                  notify_func, notify_baton, pool));
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

  /* Loop over entries hash */
  for (hi = apr_hash_first (pool, entries); hi; hi = apr_hash_next (hi))
    {
      const void *key;
      void *val;
      const char *base_name;
      const svn_wc_entry_t *entry;

      /* Put fullpath into the request pool since it becomes a key
         in the output statushash hash table. */
      const char *fullpath
        = apr_pstrdup (pool, svn_wc_adm_access_path (adm_access));

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
                                           entry, parent_entry, svn_node_dir,
                                           get_all, FALSE,
                                           notify_func, notify_baton, pool));
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

                 Of course, if there has been a kind-changing
                 replacement (for example, there is an entry for a
                 file 'foo', but 'foo' exists as a *directory* on
                 disk), we don't want to reach down into that
                 subdir to try to flesh out a "complete entry".  */

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
                  SVN_ERR (get_dir_status (statushash, dir_entry,
                                           dir_access, ignores, descend,
                                           get_all, no_ignore, notify_func,
                                           notify_baton, cancel_func,
                                           cancel_baton, traversal_info, 
                                           pool));
                }
              else
                SVN_ERR (add_status_structure 
                         (statushash, fullpath, adm_access, fullpath_entry, 
                          dir_entry, fullpath_kind, get_all, FALSE,
                          notify_func, notify_baton, pool));

            }
          else
            {
              /* File entries are ... just fine! */
              SVN_ERR (add_status_structure 
                       (statushash, fullpath, adm_access, entry, dir_entry,
                        fullpath_kind, get_all, FALSE,
                        notify_func, notify_baton, pool));
            }
        }
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc_statuses (apr_hash_t *statushash,
                 const char *path,
                 svn_wc_adm_access_t *adm_access,
                 svn_boolean_t descend,
                 svn_boolean_t get_all,
                 svn_boolean_t no_ignore,
                 svn_wc_notify_func_t notify_func,
                 void *notify_baton,
                 svn_cancel_func_t cancel_func,
                 void *cancel_baton,
                 apr_hash_t *config,
                 svn_wc_traversal_info_t *traversal_info,
                 apr_pool_t *pool)
{
  svn_node_kind_t kind;
  const svn_wc_entry_t *entry;
  apr_array_header_t *ignores;

  /* Is PATH a directory or file? */
  SVN_ERR (svn_io_check_path (path, &kind, pool));
  
  /* Read the default ignores from the config hash. */
  SVN_ERR (svn_wc_get_default_ignores (&ignores, config, pool));
  
  /* If path points to just one file, or at least to just one
     non-directory, store just one status structure in the
     STATUSHASH and return. */
  if (kind != svn_node_dir)
    {
      const svn_wc_entry_t *parent_entry;
      /* Get the entry for this file. Place it into the specified pool since
         we're going to return it in statushash. */
      SVN_ERR (svn_wc_entry (&entry, path, adm_access, FALSE, pool));

      /* Convert the entry into a status structure, store in the hash.
         
         ### Notice that because we're getting one specific file,
         we're ignoring the GET_ALL flag and unconditionally fetching
         the status structure. */
      if (!entry) 
        SVN_ERR (add_unversioned_path (path, kind, statushash, adm_access,
                                       ignores, notify_func, notify_baton,
                                       pool));
      else
        {
          SVN_ERR (svn_wc_entry (&parent_entry,
                                 svn_path_dirname (path,pool),
                                 adm_access, FALSE, pool));
          SVN_ERR (add_status_structure (statushash, path, adm_access, entry,
                                         parent_entry, kind, TRUE, FALSE,
                                         notify_func, notify_baton, pool));
        }
    }

  /* Fill the hash with a status structure for *each* entry in PATH */
  else
    {
      int wc_format_version;
      svn_boolean_t is_root;
      const svn_wc_entry_t *parent_entry;

      SVN_ERR (svn_wc_check_wc (path, &wc_format_version, pool));

      /* A wc format of 0 means this directory is not being versioned
         at all (not by Subversion, anyway). */
      if (wc_format_version == 0)
        return add_unversioned_path (path, kind, statushash, adm_access,
                                     ignores, notify_func, notify_baton, pool);

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
      else
        parent_entry = NULL;

      SVN_ERR (get_dir_status(statushash, parent_entry, adm_access,
                              ignores, descend, get_all, no_ignore,
                              notify_func, notify_baton,
                              cancel_func, cancel_baton, traversal_info,
                              pool));
    }

  return SVN_NO_ERROR;
}


struct edit_baton
{
  /* For status, the "destination" of the edit  and whether to honor
     any paths that are 'below'.  */
  const char *path;
  svn_wc_adm_access_t *adm_access;
  svn_boolean_t descend;

  /* True if we should report status for the root node of this editor
     drive, false if we should not. */
  svn_boolean_t report_root;

  /* The youngest revision in the repository.  This is a reference
     because this editor returns youngest rev to the driver directly,
     as well as in each statushash entry. */
  svn_revnum_t *youngest_revision;

  /* The hash of status structures we're editing. */
  apr_hash_t *statushash;

  /* The pool that will be used to add new structures to the hash,
     presumably the same one it's already been using. */
  apr_pool_t *hashpool;

  /* The pool which the editor uses for the whole tree-walk.*/
  apr_pool_t *pool;
};





/*** Helper ***/


/* Look up the key PATH in EDIT_BATON->STATUSHASH.

   If the value doesn't yet exist, create a new status struct using
   EDIT_BATON->HASHPOOL.

   Set the status structure's "network" fields to REPOS_TEXT_STATUS,
   REPOS_PROP_STATUS.  If either of these fields is 0, it will be
   ignored.  */
static svn_error_t *
tweak_statushash (void *edit_baton,
                  const char *path,
                  svn_boolean_t is_dir,
                  enum svn_wc_status_kind repos_text_status,
                  enum svn_wc_status_kind repos_prop_status)
{
  svn_wc_status_t *statstruct;
  struct edit_baton *eb = (struct edit_baton *) edit_baton;
  apr_hash_t *statushash = eb->statushash;
  apr_pool_t *pool = eb->hashpool;

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
      svn_wc_adm_access_t *adm_access;
      if (repos_text_status == svn_wc_status_added)
        adm_access = NULL;
      else if (is_dir)
        SVN_ERR (svn_wc_adm_retrieve (&adm_access, eb->adm_access,
                                      path, pool));
      else
        SVN_ERR (svn_wc_adm_retrieve (&adm_access, eb->adm_access,
                                      svn_path_dirname (path, pool),
                                      pool));

      /* Use the public API to get a statstruct: */
      SVN_ERR (svn_wc_status (&statstruct, path, adm_access, pool));

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




/*** batons ***/

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

  /* The pool in which this baton itself is allocated. */
  apr_pool_t *pool;
};



/* Create a new dir_baton for subdir PATH. */
static struct dir_baton *
make_dir_baton (const char *path,
                struct edit_baton *edit_baton,
                struct dir_baton *parent_baton,
                apr_pool_t *pool)
{
  struct dir_baton *pb = parent_baton;
  struct edit_baton *eb = edit_baton;
  struct dir_baton *d = apr_pcalloc (pool, sizeof (*d));
  const char *full_path; 

  /* Don't do this.  Just do NOT do this to me. */
  if (pb && (! path))
    abort();

  /* Construct the full path of this directory. */
  if (pb)
    full_path = svn_path_join (eb->path, path, pool);
  else
    full_path = apr_pstrdup (pool, eb->path);

  /* Finish populating the baton members. */
  d->path         = full_path;
  d->name         = path ? (svn_path_basename (path, pool)) : NULL;
  d->edit_baton   = edit_baton;
  d->parent_baton = parent_baton;
  d->pool         = pool;

  return d;
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
    full_path = svn_path_join (eb->path, path, pool);
  else
    full_path = apr_pstrdup (pool, eb->path);

  /* Finish populating the baton members. */
  f->path       = full_path;
  f->name       = svn_path_basename (path, pool);
  f->pool       = pool;
  f->dir_baton  = pb;
  f->edit_baton = eb;

  return f;
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
  *dir_baton = make_dir_baton (NULL, eb, NULL, pool);
  return SVN_NO_ERROR;
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
  const char *full_path = svn_path_join (eb->path, path, pool);
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
    SVN_ERR (tweak_statushash (db->edit_baton,
                               full_path, kind == svn_node_dir,
                               svn_wc_status_deleted, 0));

  /* Mark the parent dir -- it lost an entry (unless that parent dir
     is the root node and we're not supposed to report on the root
     node).  */
  if ((db->parent_baton) || (eb->report_root))
    SVN_ERR (tweak_statushash (db->edit_baton,
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
  struct dir_baton *new_db;

  new_db = make_dir_baton (path, pb->edit_baton, pb, pool);

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
  *child_baton = make_dir_baton (path, pb->edit_baton, pb, pool);
  return SVN_NO_ERROR;
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

  /* If nothing has changed, return. */
  if (! (db->added || db->prop_changed || db->text_changed))
    return SVN_NO_ERROR;

  /* If this directory was added, add the directory to the status hash. */
  if (db->added)
    SVN_ERR (tweak_statushash (db->edit_baton,
                               db->path, TRUE,
                               svn_wc_status_added,
                               db->prop_changed ? svn_wc_status_added : 0));

  /* Else, if this a) is not the root directory, or b) *is* the root
     directory, and we are supposed to report on it, then mark the
     existing directory in the statushash. */
  else if ((db->parent_baton) || (db->edit_baton->report_root))
    SVN_ERR (tweak_statushash (db->edit_baton,
                               db->path, TRUE,
                               db->text_changed ? svn_wc_status_modified : 0,
                               db->prop_changed ? svn_wc_status_modified : 0));
  
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
    SVN_ERR (tweak_statushash (fb->edit_baton,
                               fb->path, FALSE,
                               svn_wc_status_added, 
                               fb->prop_changed ? svn_wc_status_added : 0));
  /* Else, mark the existing file in the statushash. */
  else
    SVN_ERR (tweak_statushash (fb->edit_baton,
                               fb->path, FALSE,
                               fb->text_changed ? svn_wc_status_modified : 0,
                               fb->prop_changed ? svn_wc_status_modified : 0));

  return SVN_NO_ERROR;
}


static svn_error_t *
close_edit (void *edit_baton,
            apr_pool_t *pool)
{
  /* The edit is over, free its pool. */
  svn_pool_destroy (((struct edit_baton *) edit_baton)->pool);
  return SVN_NO_ERROR;
}



/*** Returning editors. ***/


/*** Public API ***/

svn_error_t *
svn_wc_get_status_editor (const svn_delta_editor_t **editor,
                          void **edit_baton,
                          const char *path,
                          svn_wc_adm_access_t *adm_access,
                          svn_boolean_t descend,
                          apr_hash_t *statushash,
                          svn_revnum_t *youngest,
                          svn_cancel_func_t cancel_func,
                          void *cancel_baton,
                          apr_pool_t *pool)
{
  struct edit_baton *eb;
  const char *anchor, *target, *tempbuf;
  apr_pool_t *subpool = svn_pool_create (pool);
  svn_delta_editor_t *tree_editor = svn_delta_default_editor (pool);

  /* Construct an edit baton. */
  eb = apr_palloc (subpool, sizeof (*eb));
  eb->pool              = subpool;
  eb->hashpool          = pool;
  eb->statushash        = statushash;
  eb->descend           = descend;
  eb->youngest_revision = youngest;
  eb->adm_access        = adm_access;

  /* Anchor target analysis, to make this editor able to match
     hash-keys already in the hash.  (svn_wc_statuses is ignorant of
     anchor/target issues.) */
  SVN_ERR (svn_wc_get_actual_target (path, &anchor, &target, pool));
  if (target)
    tempbuf = svn_path_join (anchor, target, pool);
  else
    tempbuf = apr_pstrdup (pool, anchor);

  if (strcmp (path, tempbuf) != 0)
    eb->path = "";
  else
    eb->path = anchor;

  /* Record whether or not there is a target; in other words, whether
     or not we want to report about the root directory of the edit
     drive. */
  eb->report_root = target ? FALSE : TRUE;
  
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

  SVN_ERR (svn_delta_get_cancellation_editor (cancel_func,
                                              cancel_baton,
                                              tree_editor,
                                              eb,
                                              editor,
                                              edit_baton,
                                              pool));

  return SVN_NO_ERROR;
}
