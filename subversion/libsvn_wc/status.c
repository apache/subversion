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

  /* The configured set of default ignores. */
  apr_array_header_t *ignores;

  /* Externals info harvested during the status run. */
  svn_wc_traversal_info_t *traversal_info;
  apr_hash_t *externals;

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


/** Code **/

/* Fill in *STATUS for PATH, whose entry data is in ENTRY.  Allocate
   *STATUS in POOL. 

   ENTRY may be null, for non-versioned entities.  In this case, we
   will assemble a special status structure item which implies a
   non-versioned thing.

   Else, ENTRY's pool must not be shorter-lived than STATUS's, since
   ENTRY will be stored directly, not copied.

   PARENT_ENTRY is the entry for the parent directory of PATH, it may be
   NULL if ENTRY is NULL or if PATH is a working copy root.  The lifetime
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
   and pass it off to the STATUS_FUNC/STATUS_BATON.  All other
   arguments are the same as those passed to assemble_status().  */
static svn_error_t *
send_status_structure (const char *path,
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
      APR_ARRAY_PUSH (patterns, const char *) = ignore;
    }

  /* Then add any svn:ignore globs to the PATTERNS array. */
  SVN_ERR (svn_wc_prop_get (&value, SVN_PROP_IGNORE,
                            svn_wc_adm_access_path (adm_access), adm_access,
                            pool));
  if (value != NULL)
    svn_cstring_split_append (patterns, value->data, "\n\r", FALSE, pool);

  return SVN_NO_ERROR;   
} 


/* Compare PATH with items in the EXTERNALS hash to see if PATH is the
   drop location for, or an intermediate directory of the drop
   location for, an externals definition.  Use POOL for
   scratchwork. */
static svn_boolean_t
is_external_path (apr_hash_t *externals,
                  const char *path,
                  apr_pool_t *pool)
{
  apr_hash_index_t *hi;

  /* First try: does the path exist as a key in the hash? */
  if (apr_hash_get (externals, path, APR_HASH_KEY_STRING))
    return TRUE;

  /* Failing that, we need to check if any external is a child of
     PATH. */
  for (hi = apr_hash_first (pool, externals); hi; hi = apr_hash_next (hi))
    {
      const void *key;
      apr_hash_this (hi, &key, NULL, NULL);
      if (svn_path_is_child (path, key, pool))
        return TRUE;
    }

  return FALSE;
}


/* Assuming that NAME is unversioned, send a status structure
   for it through STATUS_FUNC/STATUS_BATON unless this path is being
   ignored.  This function should never be called on a versioned entry.

   NAME is the basename of the unversioned file whose status is being
   requested.  PATH_KIND is the node kind of NAME as determined by the
   caller.  ADM_ACCESS is an access baton for the working copy path.
   PATTERNS points to a list of filename patterns which are marked as
   ignored.  None of these parameter may be NULL.  EXTERNALS is a hash
   of known externals definitions for this status run.

   If NO_IGNORE is non-zero, the item will be added regardless of
   whether it is ignored; otherwise we will only add the item if it
   does not match any of the patterns in PATTERNS.

   Allocate everything in POOL.
*/
static svn_error_t *
send_unversioned_item (const char *name, 
                       svn_node_kind_t path_kind, 
                       svn_wc_adm_access_t *adm_access, 
                       apr_array_header_t *patterns,
                       apr_hash_t *externals,
                       svn_boolean_t no_ignore,
                       svn_wc_status_func_t status_func,
                       void *status_baton,
                       apr_pool_t *pool)
{
  int ignore_me = svn_cstring_match_glob_list (name, patterns);
  const char *path = svn_path_join (svn_wc_adm_access_path (adm_access), 
                                    name, pool);
  int is_external = is_external_path (externals, path, pool);
  svn_wc_status_t *status;

  /* If we aren't ignoring it, or if it's an externals path, create a
     status structure for this dirent. */
  if (no_ignore || (! ignore_me) || is_external)
    {
      SVN_ERR (assemble_status (&status, path, adm_access, NULL, NULL, 
                                path_kind, FALSE, ignore_me, pool));
      if (is_external)
        status->text_status = svn_wc_status_external;
      (status_func) (status_baton, path, status);
    }
  return SVN_NO_ERROR;
}


/* Prototype for untangling a tango-ing two-some. */
static svn_error_t *get_dir_status (struct edit_baton *eb,
                                    const svn_wc_entry_t *parent_entry,
                                    svn_wc_adm_access_t *adm_access,
                                    const char *entry,
                                    apr_array_header_t *ignores,
                                    svn_boolean_t descend,
                                    svn_boolean_t get_all,
                                    svn_boolean_t no_ignore,
                                    svn_boolean_t skip_this_dir,
                                    svn_wc_status_func_t status_func,
                                    void *status_baton,
                                    svn_cancel_func_t cancel_func,
                                    void *cancel_baton,
                                    apr_pool_t *pool);

/* Handle NAME (whose entry is ENTRY) as a directory entry of the
   directory represented by ADM_ACCESS (and whose entry is
   DIR_ENTRY).  All other arguments are the same as those passed to
   get_dir_status(), the function for which this one is a helper.  */
static svn_error_t *
handle_dir_entry (struct edit_baton *eb,
                  svn_wc_adm_access_t *adm_access,
                  const char *name,
                  const svn_wc_entry_t *dir_entry,
                  const svn_wc_entry_t *entry,
                  apr_array_header_t *ignores,
                  svn_boolean_t descend,
                  svn_boolean_t get_all,
                  svn_boolean_t no_ignore,
                  svn_wc_status_func_t status_func,
                  void *status_baton,
                  svn_cancel_func_t cancel_func,
                  void *cancel_baton,
                  apr_pool_t *pool)
{
  const char *dirname = svn_wc_adm_access_path (adm_access);
  const char *path = svn_path_join (dirname, name, pool);
  svn_node_kind_t kind;

  /* Get the entry's kind on disk. */
  SVN_ERR (svn_io_check_path (path, &kind, pool));
  if (kind == svn_node_dir)
    {
      /* Directory entries are incomplete.  We must get their full
         entry from their own THIS_DIR entry.  svn_wc_entry does this
         for us if it can.

         Of course, if there has been a kind-changing replacement (for
         example, there is an entry for a file 'foo', but 'foo' exists
         as a *directory* on disk), we don't want to reach down into
         that subdir to try to flesh out a "complete entry".  */
      const svn_wc_entry_t *full_entry = entry;
          
      if (entry->kind == kind)
        SVN_ERR (svn_wc_entry (&full_entry, path, adm_access, FALSE, pool));

      /* Descend only if the subdirectory is a working copy directory
         (and DESCEND is non-zero ofcourse)  */
      if (descend && (full_entry != entry))
        {
          svn_wc_adm_access_t *dir_access;
          SVN_ERR (svn_wc_adm_retrieve (&dir_access, adm_access, path, pool));
          SVN_ERR (get_dir_status (eb, dir_entry, dir_access, NULL, ignores, 
                                   descend, get_all, no_ignore, FALSE, 
                                   status_func, status_baton, cancel_func,
                                   cancel_baton, pool));
        }
      else
        {
          SVN_ERR (send_status_structure (path, adm_access, full_entry, 
                                          dir_entry, kind, get_all, FALSE,
                                          status_func, status_baton, pool));
        }
    }
  else
    {
      /* File entries are ... just fine! */
      SVN_ERR (send_status_structure (path, adm_access, entry, dir_entry, 
                                      kind, get_all, FALSE,
                                      status_func, status_baton, pool));
    }
  return SVN_NO_ERROR;
}


/* Send svn_wc_status_t * structures for the directory ADM_ACCESS and
   for all its entries through STATUS_FUNC/STATUS_BATON, or, if ENTRY
   is non-NULL, only for that directory entry.

   PARENT_ENTRY is the entry for the parent of the directory or NULL
   if that directory is a working copy root.

   If SKIP_THIS_DIR is TRUE (and ENTRY is NULL), the directory's own
   status will not be reported.  However, upon recursing, all subdirs
   *will* be reported, regardless of this parameter's value.

   Other arguments are the same as those passed to
   svn_wc_get_status_editor().  */
static svn_error_t *
get_dir_status (struct edit_baton *eb,
                const svn_wc_entry_t *parent_entry,
                svn_wc_adm_access_t *adm_access,
                const char *entry,
                apr_array_header_t *ignores,
                svn_boolean_t descend,
                svn_boolean_t get_all,
                svn_boolean_t no_ignore,
                svn_boolean_t skip_this_dir,
                svn_wc_status_func_t status_func,
                void *status_baton,
                svn_cancel_func_t cancel_func,
                void *cancel_baton,
                apr_pool_t *pool)
{
  apr_hash_t *entries;
  apr_hash_index_t *hi;
  const svn_wc_entry_t *dir_entry;
  const char *fullpath, *path = svn_wc_adm_access_path (adm_access);
  apr_hash_t *dirents;
  apr_array_header_t *patterns = NULL;
  apr_pool_t *iterpool, *subpool = svn_pool_create (pool);

  /* See if someone wants to cancel this operation. */
  if (cancel_func)
    SVN_ERR (cancel_func (cancel_baton));

  /* Load entries file for the directory into the requested pool. */
  SVN_ERR (svn_wc_entries_read (&entries, adm_access, FALSE, subpool));

  /* Read PATH's dirents. */
  SVN_ERR (svn_io_get_dirents (&dirents, path, subpool));

  /* Get this directory's entry. */
  SVN_ERR (svn_wc_entry (&dir_entry, path, adm_access, FALSE, subpool));

  /* Unless specified, add default ignore regular expressions and try
     to add any svn:ignore properties from the parent directory. */
  if (ignores)
    {
      patterns = apr_array_make (subpool, 1, sizeof (const char *));
      SVN_ERR (collect_ignore_patterns (patterns, ignores, 
                                        adm_access, subpool));
    }

  /* If "this dir" has "svn:externals" property set on it, store its
     name and value in traversal_info.  Also, we want to track the
     externals internally so we can report status more accurately. */
  if (eb->traversal_info)
    {
      const svn_string_t *prop_val;
      SVN_ERR (svn_wc_prop_get (&prop_val, SVN_PROP_EXTERNALS, path, 
                                adm_access, subpool));
      if (prop_val)
        {
          apr_pool_t *dup_pool = eb->traversal_info->pool;
          const char *dup_path = apr_pstrdup (dup_pool, path);
          const char *dup_val = apr_pstrmemdup (dup_pool, prop_val->data, 
                                                prop_val->len);
          apr_hash_t *ext_items;

          /* First things first -- we put the externals information
             into the "global" traversal info structure. */
          apr_hash_set (eb->traversal_info->externals_old,
                        dup_path, APR_HASH_KEY_STRING, dup_val);
          apr_hash_set (eb->traversal_info->externals_new,
                        dup_path, APR_HASH_KEY_STRING, dup_val);

          /* Now, parse the thing, and copy the parsed results into
             our "global" externals hash. */
          SVN_ERR (svn_wc_parse_externals_description (&ext_items, path,
                                                       dup_val, dup_pool));
          for (hi = apr_hash_first (dup_pool, ext_items); 
               hi; 
               hi = apr_hash_next (hi))
            {
              const void *key;
              void *val;
              apr_hash_this (hi, &key, NULL, &val);
              apr_hash_set (eb->externals, svn_path_join (path, key, dup_pool),
                            APR_HASH_KEY_STRING, val);
            }
        }
    }

  /* Early out -- our caller only cares about a single ENTRY in this
     directory.  */
  if (entry)
    {
      /* If ENTRY is unversioned, send its unversioned status. */
      if (apr_hash_get (dirents, entry, APR_HASH_KEY_STRING)
          && (! apr_hash_get (entries, entry, APR_HASH_KEY_STRING)))
        {
          svn_node_kind_t kind;
          fullpath = svn_path_join (path, entry, subpool);
          SVN_ERR (svn_io_check_path (path, &kind, subpool));
          SVN_ERR (send_unversioned_item (entry, kind, adm_access, 
                                          patterns, eb->externals, no_ignore, 
                                          status_func, status_baton, subpool));
        }
      /* Otherwise, send its versioned status. */
      else
        {
          const svn_wc_entry_t *entry_entry;
          entry_entry = apr_hash_get (entries, entry, APR_HASH_KEY_STRING);
          SVN_ERR (handle_dir_entry (eb, adm_access, entry, dir_entry, 
                                     entry_entry, ignores, descend, get_all, 
                                     no_ignore, status_func, status_baton, 
                                     cancel_func, cancel_baton, subpool));
        }

      /* Regardless, we're done here.  Let's go home. */
      return SVN_NO_ERROR;
    }

  /** If we get here, ENTRY is NULL and we are handling all the
      directory entries. */

  /* Make our iteration pool. */
  iterpool = svn_pool_create (subpool);

  /* Add empty status structures for each of the unversioned things. */
  for (hi = apr_hash_first (subpool, dirents); hi; hi = apr_hash_next (hi))
    {
      const void *key;
      apr_ssize_t klen;
      void *val;
      svn_node_kind_t *path_kind;

      apr_hash_this (hi, &key, &klen, &val);
        
      /* Skip versioned things, and skip the administrative
         directory. */
      if ((apr_hash_get (entries, key, klen)) 
          || (strcmp (key, SVN_WC_ADM_DIR_NAME) == 0))
        continue;

      /* Clear the iteration subpool. */
      svn_pool_clear (iterpool);

      /* Make an unversioned status item for KEY, and put it into our
         return hash. */
      path_kind = val;
      SVN_ERR (send_unversioned_item (key, *path_kind, adm_access, 
                                      patterns, eb->externals, no_ignore, 
                                      status_func, status_baton, iterpool));
    }

  /* Handle "this-dir" first. */
  if (! skip_this_dir)
    SVN_ERR (send_status_structure (path, adm_access, dir_entry, 
                                    parent_entry, svn_node_dir,
                                    get_all, FALSE, status_func, 
                                    status_baton, subpool));

  /* Loop over entries hash */
  for (hi = apr_hash_first (pool, entries); hi; hi = apr_hash_next (hi))
    {
      const void *key;
      void *val;

      /* Get the next dirent */
      apr_hash_this (hi, &key, NULL, &val);

      /* ### todo: What if the subdir is from another repository? */
          
      /* Skip "this-dir". */
      if (strcmp (key, SVN_WC_ENTRY_THIS_DIR) == 0)
        continue;

      /* Clear the iteration subpool. */
      svn_pool_clear (iterpool);

      /* Handle this directory entry (possibly recursing). */
      SVN_ERR (handle_dir_entry (eb, adm_access, key, dir_entry, val, ignores, 
                                 descend, get_all, no_ignore, 
                                 status_func, status_baton, cancel_func, 
                                 cancel_baton, iterpool));
    }
  
  /* Destroy our subpools. */
  svn_pool_destroy (subpool);

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
   exist, and the REPOS_TEXT_STATUS indicates that this is an
   addition, create a new status struct using the hash's pool.  Merge
   REPOS_TEXT_STATUS and REPOS_PROP_STATUS into the status structure's
   "network" fields. */
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

  /* Is PATH already a hash-key? */
  statstruct = apr_hash_get (statushash, path, APR_HASH_KEY_STRING);

  /* If not, make it so. */
  if (! statstruct)
    {
      /* This should only be missing from the hash if it's being added
         from the repository status drive. */
      assert (repos_text_status == svn_wc_status_added);

      /* Use the public API to get a statstruct, and put it into the hash. */
      SVN_ERR (svn_wc_status (&statstruct, path, NULL, pool));
      apr_hash_set (statushash, apr_pstrdup (pool, path), 
                    APR_HASH_KEY_STRING, statstruct);
    }

  /* Merge a repos "delete" + "add" into a single "replace". */
  if ((repos_text_status == svn_wc_status_added)
      && (statstruct->repos_text_status == svn_wc_status_deleted))
    repos_text_status = svn_wc_status_replaced;

  /* Tweak the structure's repos fields. */
  if (repos_text_status)
    statstruct->repos_text_status = repos_text_status;
  if (repos_prop_status)
    statstruct->repos_prop_status = repos_prop_status;
  
  return SVN_NO_ERROR;
}



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
      && (parent_status->text_status != svn_wc_status_deleted)
      && (parent_status->text_status != svn_wc_status_absent)
      && (parent_status->text_status != svn_wc_status_obstructed)
      && (parent_status->entry->kind == svn_node_dir)
      && (eb->descend || (! pb)))
    {
      svn_wc_adm_access_t *dir_access;
      apr_array_header_t *ignores = eb->ignores;
      SVN_ERR (svn_wc_adm_retrieve (&dir_access, eb->adm_access, 
                                    d->path, pool));
      SVN_ERR (get_dir_status (eb, parent_status->entry, dir_access, NULL, 
                               ignores, FALSE, TRUE, TRUE, TRUE, hash_stash, 
                               d->statii, NULL, NULL, pool));
    }

  *dir_baton = d;
  return SVN_NO_ERROR;
}


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


/* Return a boolean answer to the question "Is STATUS something that
   should be reported?".  EB is the edit baton. */
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


/* Baton for mark_status. */
struct status_baton
{
  svn_wc_status_func_t real_status_func;   /* real status function */
  void *real_status_baton;                 /* real status baton */
};

/* A status callback function which wraps the *real* status
   function/baton.   It simply sets the "repos_text_status" field of the
   STATUS to svn_wc_status_deleted and passes it off to the real
   status func/baton. */
static void
mark_deleted (void *baton,
              const char *path,
              svn_wc_status_t *status)
{
  struct status_baton *sb = baton;
  status->repos_text_status = svn_wc_status_deleted;
  sb->real_status_func (sb->real_status_baton, path, status);
}


/* Handle a directory's STATII hash.  EB is the edit baton.  DIR_PATH
   and DIR_ENTRY are the on-disk path and entry, respectively, for the
   directory itself.  If DESCEND is set, this function will recurse
   into subdirectories.  Also, if DIR_WAS_DELETED is set, each status
   that is reported through this function will have it's
   repos_text_status field showing a deletion.  Use POOL for all
   allocations. */
static svn_error_t *
handle_statii (struct edit_baton *eb,
               svn_wc_entry_t *dir_entry,
               const char *dir_path,
               apr_hash_t *statii,
               svn_boolean_t dir_was_deleted,
               svn_boolean_t descend,
               apr_pool_t *pool)
{
  apr_array_header_t *ignores = eb->ignores;
  apr_hash_index_t *hi; 
  apr_pool_t *subpool = svn_pool_create (pool);
  svn_wc_status_func_t status_func = eb->status_func;
  void *status_baton = eb->status_baton;
  struct status_baton sb;

  if (dir_was_deleted)
    {
      sb.real_status_func = eb->status_func;
      sb.real_status_baton = eb->status_baton;
      status_func = mark_deleted;
      status_baton = &sb;
    }

  /* Loop over all the statuses still in our hash, handling each one. */
  for (hi = apr_hash_first (pool, statii); hi; hi = apr_hash_next (hi))
    {
      const void *key;
      void *val;
      svn_wc_status_t *status;

      apr_hash_this (hi, &key, NULL, &val);
      status = val;

      /* Clear the subpool. */
      svn_pool_clear (subpool);

      /* Now, handle the status. */
      if (descend && status->entry && (status->entry->kind == svn_node_dir) )
        {
          svn_wc_adm_access_t *dir_access;
          SVN_ERR (svn_wc_adm_retrieve (&dir_access, eb->adm_access,
                                        key, subpool));
          SVN_ERR (get_dir_status (eb, dir_entry, dir_access, NULL,
                                   ignores, TRUE, eb->get_all, 
                                   eb->no_ignore, TRUE, status_func, 
                                   status_baton, eb->cancel_func, 
                                   eb->cancel_baton, subpool));
        }
      if (dir_was_deleted)
        status->repos_text_status = svn_wc_status_deleted;
      if (is_sendable_status (status, eb))
        (eb->status_func)(eb->status_baton, key, status);
    }
    
  /* Destroy the subpool. */
  svn_pool_destroy (subpool);

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

  SVN_ERR (make_dir_baton (child_baton, path, eb, pb, pool));

  /* Make this dir as added. */
  new_db = *child_baton;
  new_db->added = TRUE;

  /* Mark the parent as changed;  it gained an entry. */
  pb->text_changed = TRUE;

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
      enum svn_wc_status_kind repos_text_status;
      enum svn_wc_status_kind repos_prop_status;
  
      /* If this is a new file, add it to the statushash. */
      if (db->added)
        {
          repos_text_status = svn_wc_status_added;
          repos_prop_status = db->prop_changed ? svn_wc_status_added : 0;
        }
      else
        {
          repos_text_status = db->text_changed ? svn_wc_status_modified : 0;
          repos_prop_status = db->prop_changed ? svn_wc_status_modified : 0;
        }

      /* If this directory was added, add it to its parent's status hash. */
      if (pb)
        SVN_ERR (tweak_statushash (pb->statii,
                                   eb->adm_access,
                                   db->path, TRUE,
                                   repos_text_status,
                                   repos_prop_status));
    }

  /* Handle this directory's statuses, and then note in the parent
     that this has been done. */
  if (pb && eb->descend)
    {
      svn_boolean_t was_deleted = FALSE;

      /* See if the directory was deleted or replaced. */
      dir_status = apr_hash_get (pb->statii, db->path, APR_HASH_KEY_STRING);
      if ((dir_status->repos_text_status == svn_wc_status_deleted)
          || (dir_status->repos_text_status == svn_wc_status_replaced))
        was_deleted = TRUE;

      /* Now do the status reporting. */
      SVN_ERR (handle_statii (eb, dir_status ? dir_status->entry : NULL, 
                              db->path, db->statii, was_deleted, TRUE, pool));
      if (is_sendable_status (dir_status, eb))
        (eb->status_func) (eb->status_baton, db->path, dir_status);
      apr_hash_set (pb->statii, db->path, APR_HASH_KEY_STRING, NULL);
    }
  else if (! pb)
    {
      /* If this is the top-most directory, and the operation had a
         target, we should only report the target. */
      if (eb->target)
        {
          svn_wc_status_t *tgt_status;
          const char *path = svn_path_join (eb->anchor, eb->target, pool);
          dir_status = eb->anchor_status;
          tgt_status = apr_hash_get (db->statii, path, APR_HASH_KEY_STRING);
          /* ### need to pay attention to target's kind here */
          /* ### need to pay attention to if dir was deleted here */
          if (tgt_status)
            (eb->status_func) (eb->status_baton, path, tgt_status);
        }
      else
        {
          /* Otherwise, we report on all our children and ourself.
             Note that our directory couldn't have been deleted,
             because it is the root of the edit drive. */
          SVN_ERR (handle_statii (eb, eb->anchor_status->entry, db->path, 
                                  db->statii, FALSE, eb->descend, pool));
          if (is_sendable_status (eb->anchor_status, eb))
            (eb->status_func) (eb->status_baton, db->path, eb->anchor_status);
          eb->anchor_status = NULL;
        }
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
  enum svn_wc_status_kind repos_text_status;
  enum svn_wc_status_kind repos_prop_status;
  
  /* If nothing has changed, return. */
  if (! (fb->added || fb->prop_changed || fb->text_changed))
    return SVN_NO_ERROR;

  /* If this is a new file, add it to the statushash. */
  if (fb->added)
    {
      repos_text_status = svn_wc_status_added;
      repos_prop_status = fb->prop_changed ? svn_wc_status_added : 0;
    }
  else
    {
      repos_text_status = fb->text_changed ? svn_wc_status_modified : 0;
      repos_prop_status = fb->prop_changed ? svn_wc_status_modified : 0;
    }

  SVN_ERR (tweak_statushash (fb->dir_baton->statii,
                             fb->edit_baton->adm_access,
                             fb->path, FALSE,
                             repos_text_status,
                             repos_prop_status));

  return SVN_NO_ERROR;
}


static svn_error_t *
close_edit (void *edit_baton,
            apr_pool_t *pool)
{
  struct edit_baton *eb = edit_baton;
  apr_array_header_t *ignores = eb->ignores;
  
  /* If we get here and the root was not opened as part of the edit,
     we need to transmit statuses for everything.  Otherwise, we
     should be done. */
  if (eb->root_opened)
    return SVN_NO_ERROR;

  /* If we have a target, that's the thing we're sending, otherwise
     we're sending the anchor. */

  if (eb->target)
    {
      svn_node_kind_t kind;
      const char *full_path = svn_path_join (eb->anchor, eb->target, pool);

      SVN_ERR (svn_io_check_path (full_path, &kind, pool));
      if (kind == svn_node_dir)
        {
          svn_wc_adm_access_t *tgt_access;
          const svn_wc_entry_t *tgt_entry;

          SVN_ERR (svn_wc_entry (&tgt_entry, full_path, eb->adm_access, 
                                 FALSE, pool));
          if (! tgt_entry)
            {
              SVN_ERR (get_dir_status (eb, NULL, eb->adm_access, eb->target, 
                                       ignores, FALSE, eb->get_all, TRUE,
                                       TRUE, eb->status_func, eb->status_baton,
                                       eb->cancel_func, eb->cancel_baton,
                                       pool));
            }
          else
            {
              SVN_ERR (svn_wc_adm_retrieve (&tgt_access, eb->adm_access,
                                            full_path, pool));
              SVN_ERR (get_dir_status (eb, NULL, tgt_access, NULL, ignores, 
                                       eb->descend, eb->get_all, 
                                       eb->no_ignore, FALSE, 
                                       eb->status_func, eb->status_baton, 
                                       eb->cancel_func, eb->cancel_baton,
                                       pool));
            }
        }
      else
        {
          SVN_ERR (get_dir_status (eb, NULL, eb->adm_access, eb->target, 
                                   ignores, FALSE, eb->get_all, TRUE,
                                   TRUE, eb->status_func, eb->status_baton,
                                   eb->cancel_func, eb->cancel_baton, pool));
        }
    }
  else
    {
      SVN_ERR (get_dir_status (eb, NULL, eb->adm_access, NULL, ignores, 
                               eb->descend, eb->get_all, eb->no_ignore, 
                               FALSE, eb->status_func, eb->status_baton, 
                               eb->cancel_func, eb->cancel_baton, pool));
    }
  
  return SVN_NO_ERROR;
}



/*** Public API ***/

svn_error_t *
svn_wc_get_status_editor (const svn_delta_editor_t **editor,
                          void **edit_baton,
                          svn_revnum_t *youngest,
                          svn_wc_adm_access_t *anchor,
                          const char *target,
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
  svn_delta_editor_t *tree_editor = svn_delta_default_editor (pool);

  /* Construct an edit baton. */
  eb = apr_palloc (pool, sizeof (*eb));
  eb->descend           = descend;
  eb->youngest_revision = youngest;
  eb->adm_access        = anchor;
  eb->config            = config;
  eb->get_all           = get_all;
  eb->no_ignore         = no_ignore;
  eb->status_func       = status_func;
  eb->status_baton      = status_baton;
  eb->cancel_func       = cancel_func;
  eb->cancel_baton      = cancel_baton;
  eb->traversal_info    = traversal_info;
  eb->externals         = traversal_info 
                          ? apr_hash_make (traversal_info->pool)
                          : NULL;
  eb->anchor            = svn_wc_adm_access_path (anchor);
  eb->target            = target;
  eb->root_opened       = FALSE;

  /* The edit baton's status structure maps to PATH, and the editor
     have to be aware of whether that is the anchor or the target. */
  SVN_ERR (svn_wc_status (&(eb->anchor_status), eb->anchor, anchor, pool));

  /* Get the set of default ignores. */
  SVN_ERR (svn_wc_get_default_ignores (&(eb->ignores), eb->config, pool));

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

  SVN_ERR (assemble_status (status, path, adm_access, entry, parent_entry,
                            svn_node_unknown, TRUE, FALSE, pool));
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
