/*
 * status.c: construct a status structure from an entry structure
 *
 * ====================================================================
 * Copyright (c) 2000-2008 CollabNet.  All rights reserved.
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
#include "svn_pools.h"
#include "svn_types.h"
#include "svn_delta.h"
#include "svn_string.h"
#include "svn_error.h"
#include "svn_path.h"
#include "svn_io.h"
#include "svn_config.h"
#include "svn_time.h"
#include "svn_private_config.h"

#include "wc.h"
#include "lock.h"
#include "props.h"
#include "translate.h"
#include "tree_conflicts.h"

#include "private/svn_wc_private.h"


/*** Editor batons ***/

struct edit_baton
{
  /* For status, the "destination" of the edit.  */
  const char *anchor;
  const char *target;
  svn_wc_adm_access_t *adm_access;

  /* The overall depth of this edit (a dir baton may override this).
   *
   * If this is svn_depth_unknown, the depths found in the working
   * copy will govern the edit; or if the edit depth indicates a
   * descent deeper than the found depths are capable of, the found
   * depths also govern, of course (there's no point descending into
   * something that's not there).
   */
  svn_depth_t default_depth;

  /* Do we want all statuses (instead of just the interesting ones) ? */
  svn_boolean_t get_all;

  /* Ignore the svn:ignores. */
  svn_boolean_t no_ignore;

  /* The comparison revision in the repository.  This is a reference
     because this editor returns this rev to the driver directly, as
     well as in each statushash entry. */
  svn_revnum_t *target_revision;

  /* Status function/baton. */
  svn_wc_status_func3_t status_func;
  void *status_baton;

  /* Cancellation function/baton. */
  svn_cancel_func_t cancel_func;
  void *cancel_baton;

  /* The configured set of default ignores. */
  const apr_array_header_t *ignores;

  /* Externals info harvested during the status run. */
  svn_wc_traversal_info_t *traversal_info;
  apr_hash_t *externals;

  /* Status item for the path represented by the anchor of the edit. */
  svn_wc_status2_t *anchor_status;

  /* Was open_root() called for this edit drive? */
  svn_boolean_t root_opened;

  /* The repository root URL, if set. */
  const char *repos_root;

  /* Repository locks, if set. */
  apr_hash_t *repos_locks;
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

  /* The ambient requested depth below this point in the edit.  This
     can differ from the parent baton's depth (with the edit baton
     considered the ultimate parent baton).  For example, if the
     parent baton has svn_depth_immediates, then here we should have
     svn_depth_empty, because there would be no further recursion, not
     even to file children. */
  svn_depth_t depth;

  /* Is this directory filtered out due to depth?  (Note that if this
     is TRUE, the depth field is undefined.) */
  svn_boolean_t excluded;

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
     edit) to svn_wc_status2_t * status items. */
  apr_hash_t *statii;

  /* The pool in which this baton itself is allocated. */
  apr_pool_t *pool;

  /* The URI to this item in the repository. */
  const char *url;

  /* out-of-date info corresponding to ood_* fields in svn_wc_status2_t. */
  svn_revnum_t ood_last_cmt_rev;
  apr_time_t ood_last_cmt_date;
  svn_node_kind_t ood_kind;
  const char *ood_last_cmt_author;
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

  /* The URI to this item in the repository. */
  const char *url;

  /* out-of-date info corresponding to ood_* fields in svn_wc_status2_t. */
  svn_revnum_t ood_last_cmt_rev;
  apr_time_t ood_last_cmt_date;
  svn_node_kind_t ood_kind;
  const char *ood_last_cmt_author;
};


/** Code **/

/* Fill in *STATUS for PATH, whose entry data is in ENTRY.  Allocate
   *STATUS in POOL.

   ADM_ACCESS is an access baton for PATH.

   ENTRY may be null, for non-versioned entities.  In this case, we
   will assemble a special status structure item which implies a
   non-versioned thing.

   PARENT_ENTRY is the entry for the parent directory of PATH, it may be
   NULL if ENTRY is NULL or if PATH is a working copy root.  The lifetime
   of PARENT_ENTRY's pool is not important.

   PATH_KIND is the node kind of PATH as determined by the caller.
   NOTE: this may be svn_node_unknown if the caller has made no such
   determination.

   If PATH_KIND is not svn_node_unknown, PATH_SPECIAL indicates whether
   the entry is a special file.

   If GET_ALL is zero, and ENTRY is not locally modified, then *STATUS
   will be set to NULL.  If GET_ALL is non-zero, then *STATUS will be
   allocated and returned no matter what.

   If IS_IGNORED is non-zero and this is a non-versioned entity, set
   the text_status to svn_wc_status_none.  Otherwise set the
   text_status to svn_wc_status_unversioned.

   If non-NULL, look up a repository lock in REPOS_LOCKS and set the repos_lock
   field of the status struct to that lock if it exists.  If REPOS_LOCKS is
   non-NULL, REPOS_ROOT must contain the repository root URL of the entry.
*/
static svn_error_t *
assemble_status(svn_wc_status2_t **status,
                const char *path,
                svn_wc_adm_access_t *adm_access,
                const svn_wc_entry_t *entry,
                const svn_wc_entry_t *parent_entry,
                svn_node_kind_t path_kind, svn_boolean_t path_special,
                svn_boolean_t get_all,
                svn_boolean_t is_ignored,
                apr_hash_t *repos_locks,
                const char *repos_root,
                apr_pool_t *pool)
{
  svn_wc_status2_t *stat;
  svn_boolean_t has_props;
  svn_boolean_t text_modified_p = FALSE;
  svn_boolean_t prop_modified_p = FALSE;
  svn_boolean_t locked_p = FALSE;
  svn_boolean_t switched_p = FALSE;
  svn_wc_conflict_description_t *tree_conflict;
  svn_boolean_t file_external_p = FALSE;
#ifdef HAVE_SYMLINK
  svn_boolean_t wc_special;
#endif /* HAVE_SYMLINK */

  /* Defaults for two main variables. */
  enum svn_wc_status_kind final_text_status = svn_wc_status_normal;
  enum svn_wc_status_kind final_prop_status = svn_wc_status_none;
  /* And some intermediate results */
  enum svn_wc_status_kind pristine_text_status = svn_wc_status_none;
  enum svn_wc_status_kind pristine_prop_status = svn_wc_status_none;

  svn_lock_t *repos_lock = NULL;

  /* Check for a repository lock. */
  if (repos_locks)
    {
      const char *abs_path;

      if (entry && entry->url)
        abs_path = entry->url + strlen(repos_root);
      else if (parent_entry && parent_entry->url)
        abs_path = svn_path_join(parent_entry->url + strlen(repos_root),
                                 svn_path_basename(path, pool), pool);
      else
        abs_path = NULL;

      if (abs_path)
        repos_lock = apr_hash_get(repos_locks,
                                  svn_path_uri_decode(abs_path, pool),
                                  APR_HASH_KEY_STRING);
    }

  /* Check the path kind for PATH. */
  if (path_kind == svn_node_unknown)
    SVN_ERR(svn_io_check_special_path(path, &path_kind, &path_special,
                                      pool));

  /* Find out whether the path is a tree conflict victim.
   * This function will set tree_conflict to NULL if the path
   * is not a victim. */
  SVN_ERR(svn_wc__get_tree_conflict(&tree_conflict, path, adm_access, pool));

  if (! entry)
    {
      /* return a fairly blank structure. */
      stat = apr_pcalloc(pool, sizeof(*stat));
      stat->entry = NULL;
      stat->text_status = svn_wc_status_none;
      stat->prop_status = svn_wc_status_none;
      stat->repos_text_status = svn_wc_status_none;
      stat->repos_prop_status = svn_wc_status_none;
      stat->locked = FALSE;
      stat->copied = FALSE;
      stat->switched = FALSE;
      stat->tree_conflict = tree_conflict;
      stat->file_external = FALSE;

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

      /* If this path has no entry, is NOT present on disk, and IS a
         tree conflict victim, count it as missing. */
      if ((path_kind == svn_node_none) && tree_conflict)
        stat->text_status = svn_wc_status_missing;

      stat->repos_lock = repos_lock;
      stat->url = NULL;
      stat->ood_last_cmt_rev = SVN_INVALID_REVNUM;
      stat->ood_last_cmt_date = 0;
      stat->ood_kind = svn_node_none;
      stat->ood_last_cmt_author = NULL;

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
          if (svn_wc__adm_missing(adm_access, path))
            final_text_status = svn_wc_status_obstructed;
        }
      else if (path_kind != svn_node_none)
        final_text_status = svn_wc_status_obstructed;
    }

  /** File externals are switched files, but they are not shown as
      such.  To be switched it must have both an URL and a parent with
      an URL, at the very least.  If this is the root folder on the
      (virtual) disk, entry and parent_entry will be equal. */
  if (entry->file_external_path)
    {
      file_external_p = TRUE;
    }
  else if (entry->url && parent_entry && parent_entry->url &&
           entry != parent_entry)
    {
      /* An item is switched if its working copy basename differs from the
         basename of its URL. */
      if (strcmp(svn_path_uri_encode(svn_path_basename(path, pool), pool),
                 svn_path_basename(entry->url, pool)))
        switched_p = TRUE;

      /* An item is switched if its URL, without the basename, does not
         equal its parent's URL. */
      if (! switched_p
          && strcmp(svn_path_dirname(entry->url, pool),
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
      SVN_ERR(svn_wc__has_props(&has_props, path, adm_access, pool));
      if (has_props)
        final_prop_status = svn_wc_status_normal;

      /* If the entry has a property file, see if it has local changes. */
      SVN_ERR(svn_wc_props_modified_p(&prop_modified_p, path, adm_access,
                                      pool));

      /* Record actual property status */
      pristine_prop_status = prop_modified_p ? svn_wc_status_modified
                                             : svn_wc_status_normal;

#ifdef HAVE_SYMLINK
      if (has_props)
        SVN_ERR(svn_wc__get_special(&wc_special, path, adm_access, pool));
      else
        wc_special = FALSE;
#endif /* HAVE_SYMLINK */

      /* If the entry is a file, check for textual modifications */
      if ((entry->kind == svn_node_file)
#ifdef HAVE_SYMLINK
          && (wc_special == path_special)
#endif /* HAVE_SYMLINK */
          )
        {
          SVN_ERR(svn_wc_text_modified_p(&text_modified_p, path, FALSE,
                                         adm_access, pool));

          /* Record actual text status */
          pristine_text_status = text_modified_p ? svn_wc_status_modified
                                                 : svn_wc_status_normal;
        }

      if (text_modified_p)
        final_text_status = svn_wc_status_modified;

      if (prop_modified_p)
        final_prop_status = svn_wc_status_modified;

      if (entry->prejfile || entry->conflict_old ||
          entry->conflict_new || entry->conflict_wrk)
        {
          svn_boolean_t text_conflict_p, prop_conflict_p;

          /* The entry says there was a conflict, but the user might have
             marked it as resolved by deleting the artifact files, so check
             for that. */
            SVN_ERR(svn_wc_conflicted_p2(&text_conflict_p, &prop_conflict_p,
                                         NULL, path, adm_access, pool));

          if (text_conflict_p)
            final_text_status = svn_wc_status_conflicted;
          if (prop_conflict_p)
            final_prop_status = svn_wc_status_conflicted;
        }

      /* 2. Possibly overwrite the text_status variable with "scheduled"
            states from the entry (A, D, R).  As a group, these states are
            of medium precedence.  They also override any C or M that may
            be in the prop_status field at this point, although they do not
            override a C text status.*/

      if (entry->schedule == svn_wc_schedule_add
          && final_text_status != svn_wc_status_conflicted)
        {
          final_text_status = svn_wc_status_added;
          final_prop_status = svn_wc_status_none;
        }

      else if (entry->schedule == svn_wc_schedule_replace
               && final_text_status != svn_wc_status_conflicted)
        {
          final_text_status = svn_wc_status_replaced;
          final_prop_status = svn_wc_status_none;
        }

      else if (entry->schedule == svn_wc_schedule_delete
               && final_text_status != svn_wc_status_conflicted)
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
            final_text_status = svn_wc_status_missing;
        }
      else if (path_kind != entry->kind)
        final_text_status = svn_wc_status_obstructed;
#ifdef HAVE_SYMLINK
      else if (((! wc_special) && (path_special))
               || (wc_special && (! path_special))
               )
        final_text_status = svn_wc_status_obstructed;
#endif /* HAVE_SYMLINK */

      if (path_kind == svn_node_dir && entry->kind == svn_node_dir)
        SVN_ERR(svn_wc_locked(&locked_p, path, pool));
    }

  /* 5. Easy out:  unless we're fetching -every- entry, don't bother
     to allocate a struct for an uninteresting entry. */

  if (! get_all)
    if (((final_text_status == svn_wc_status_none)
         || (final_text_status == svn_wc_status_normal))
        && ((final_prop_status == svn_wc_status_none)
            || (final_prop_status == svn_wc_status_normal))
        && (! locked_p) && (! switched_p) && (! file_external_p)
        && (! entry->lock_token) && (! repos_lock) && (! entry->changelist)
        && (! tree_conflict))
      {
        *status = NULL;
        return SVN_NO_ERROR;
      }


  /* 6. Build and return a status structure. */

  stat = apr_pcalloc(pool, sizeof(**status));
  stat->entry = svn_wc_entry_dup(entry, pool);
  stat->text_status = final_text_status;
  stat->prop_status = final_prop_status;
  stat->repos_text_status = svn_wc_status_none;   /* default */
  stat->repos_prop_status = svn_wc_status_none;   /* default */
  stat->locked = locked_p;
  stat->switched = switched_p;
  stat->file_external = file_external_p;
  stat->copied = entry->copied;
  stat->repos_lock = repos_lock;
  stat->url = (entry->url ? entry->url : NULL);
  stat->ood_last_cmt_rev = SVN_INVALID_REVNUM;
  stat->ood_last_cmt_date = 0;
  stat->ood_kind = svn_node_none;
  stat->ood_last_cmt_author = NULL;
  stat->tree_conflict = tree_conflict;
  stat->pristine_text_status = pristine_text_status;
  stat->pristine_prop_status = pristine_prop_status;

  *status = stat;

  return SVN_NO_ERROR;
}




/* Given an ENTRY object representing PATH, build a status structure
   and pass it off to the STATUS_FUNC/STATUS_BATON.  All other
   arguments are the same as those passed to assemble_status().  */
static svn_error_t *
send_status_structure(const char *path,
                      svn_wc_adm_access_t *adm_access,
                      const svn_wc_entry_t *entry,
                      const svn_wc_entry_t *parent_entry,
                      svn_node_kind_t path_kind,
                      svn_boolean_t path_special,
                      svn_boolean_t get_all,
                      svn_boolean_t is_ignored,
                      apr_hash_t *repos_locks,
                      const char *repos_root,
                      svn_wc_status_func3_t status_func,
                      void *status_baton,
                      apr_pool_t *pool)
{
  svn_wc_status2_t *statstruct;

  SVN_ERR(assemble_status(&statstruct, path, adm_access, entry, parent_entry,
                          path_kind, path_special, get_all, is_ignored,
                          repos_locks, repos_root, pool));
  if (statstruct && (status_func))
    return (*status_func)(status_baton, path, statstruct, pool);

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
collect_ignore_patterns(apr_array_header_t **patterns,
                        const apr_array_header_t *ignores,
                        svn_wc_adm_access_t *adm_access,
                        apr_pool_t *pool)
{
  int i;
  const svn_string_t *value;

  *patterns = apr_array_make(pool, 1, sizeof(const char *));

  /* Copy default ignores into the local PATTERNS array. */
  for (i = 0; i < ignores->nelts; i++)
    {
      const char *ignore = APR_ARRAY_IDX(ignores, i, const char *);
      APR_ARRAY_PUSH(*patterns, const char *) = ignore;
    }

  /* Then add any svn:ignore globs to the PATTERNS array. */
  SVN_ERR(svn_wc_prop_get(&value, SVN_PROP_IGNORE,
                          svn_wc_adm_access_path(adm_access), adm_access,
                          pool));
  if (value != NULL)
    svn_cstring_split_append(*patterns, value->data, "\n\r", FALSE, pool);

  return SVN_NO_ERROR;
}


/* Compare PATH with items in the EXTERNALS hash to see if PATH is the
   drop location for, or an intermediate directory of the drop
   location for, an externals definition.  Use POOL for
   scratchwork. */
static svn_boolean_t
is_external_path(apr_hash_t *externals,
                 const char *path,
                 apr_pool_t *pool)
{
  apr_hash_index_t *hi;

  /* First try: does the path exist as a key in the hash? */
  if (apr_hash_get(externals, path, APR_HASH_KEY_STRING))
    return TRUE;

  /* Failing that, we need to check if any external is a child of
     PATH. */
  for (hi = apr_hash_first(pool, externals); hi; hi = apr_hash_next(hi))
    {
      const void *key;
      apr_hash_this(hi, &key, NULL, NULL);
      if (svn_path_is_child(path, key, pool))
        return TRUE;
    }

  return FALSE;
}


/* Assuming that NAME is unversioned, send a status structure
   for it through STATUS_FUNC/STATUS_BATON unless this path is being
   ignored.  This function should never be called on a versioned entry.

   NAME is the basename of the unversioned file whose status is being
   requested.  PATH_KIND is the node kind of NAME as determined by the
   caller.  PATH_SPECIAL is the special status of the path, also determined
   by the caller.  ADM_ACCESS is an access baton for the working copy path.
   PATTERNS points to a list of filename patterns which are marked as
   ignored.  None of these parameter may be NULL.  EXTERNALS is a hash
   of known externals definitions for this status run.

   If NO_IGNORE is non-zero, the item will be added regardless of
   whether it is ignored; otherwise we will only add the item if it
   does not match any of the patterns in PATTERNS.

   Allocate everything in POOL.
*/
static svn_error_t *
send_unversioned_item(const char *name,
                      svn_node_kind_t path_kind, svn_boolean_t path_special,
                      svn_wc_adm_access_t *adm_access,
                      apr_array_header_t *patterns,
                      apr_hash_t *externals,
                      svn_boolean_t no_ignore,
                      apr_hash_t *repos_locks,
                      const char *repos_root,
                      svn_wc_status_func3_t status_func,
                      void *status_baton,
                      apr_pool_t *pool)
{
  svn_boolean_t ignore_me = svn_wc_match_ignore_list(name, patterns, pool);
  const char *path = svn_path_join(svn_wc_adm_access_path(adm_access),
                                   name, pool);
  svn_boolean_t is_external = is_external_path(externals, path, pool);
  svn_wc_status2_t *status;

  SVN_ERR(assemble_status(&status, path, adm_access, NULL, NULL,
                          path_kind, path_special, FALSE, ignore_me,
                          repos_locks, repos_root, pool));

  if (is_external)
    status->text_status = svn_wc_status_external;

  /* Don't ever ignore tree conflict victims. */
  if (status->tree_conflict)
    ignore_me = FALSE;

  /* If we aren't ignoring it, or if it's an externals path, or it has a lock
     in the repository, pass this entry to the status func. */
  if (no_ignore || (! ignore_me) || is_external || status->repos_lock)
    return (status_func)(status_baton, path, status, pool);

  return SVN_NO_ERROR;
}


/* Prototype for untangling a tango-ing two-some. */
static svn_error_t *
get_dir_status(struct edit_baton *eb,
               const svn_wc_entry_t *parent_entry,
               svn_wc_adm_access_t *adm_access,
               const char *entry,
               const apr_array_header_t *ignores,
               svn_depth_t depth,
               svn_boolean_t get_all,
               svn_boolean_t no_ignore,
               svn_boolean_t skip_this_dir,
               svn_wc_status_func3_t status_func,
               void *status_baton,
               svn_cancel_func_t cancel_func,
               void *cancel_baton,
               apr_pool_t *pool);

/* Handle NAME (whose entry is ENTRY) as a directory entry of the
   directory represented by ADM_ACCESS (and whose entry is
   DIR_ENTRY).  All other arguments are the same as those passed to
   get_dir_status(), the function for which this one is a helper.  */
static svn_error_t *
handle_dir_entry(struct edit_baton *eb,
                 svn_wc_adm_access_t *adm_access,
                 const char *name,
                 const svn_wc_entry_t *dir_entry,
                 const svn_wc_entry_t *entry,
                 svn_node_kind_t kind,
                 svn_boolean_t special,
                 const apr_array_header_t *ignores,
                 svn_depth_t depth,
                 svn_boolean_t get_all,
                 svn_boolean_t no_ignore,
                 svn_wc_status_func3_t status_func,
                 void *status_baton,
                 svn_cancel_func_t cancel_func,
                 void *cancel_baton,
                 apr_pool_t *pool)
{
  const char *dirname = svn_wc_adm_access_path(adm_access);
  const char *path = svn_path_join(dirname, name, pool);

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
        SVN_ERR(svn_wc__entry_versioned(&full_entry, path, adm_access, FALSE,
                                       pool));

      /* Descend only if the subdirectory is a working copy directory
         (and DEPTH permits it, of course)  */
      if (full_entry != entry
          && (depth == svn_depth_unknown
              || depth == svn_depth_immediates
              || depth == svn_depth_infinity))
        {
          svn_wc_adm_access_t *dir_access;
          SVN_ERR(svn_wc_adm_retrieve(&dir_access, adm_access, path, pool));
          SVN_ERR(get_dir_status(eb, dir_entry, dir_access, NULL, ignores,
                                 depth, get_all, no_ignore, FALSE,
                                 status_func, status_baton, cancel_func,
                                 cancel_baton, pool));
        }
      else
        {
          SVN_ERR(send_status_structure(path, adm_access, full_entry,
                                        dir_entry, kind, special, get_all,
                                        FALSE, eb->repos_locks,
                                        eb->repos_root,
                                        status_func, status_baton, pool));
        }
    }
  else
    {
      /* File entries are ... just fine! */
      SVN_ERR(send_status_structure(path, adm_access, entry, dir_entry,
                                    kind, special, get_all, FALSE,
                                    eb->repos_locks, eb->repos_root,
                                    status_func, status_baton, pool));
    }
  return SVN_NO_ERROR;
}


/* Send svn_wc_status2_t * structures for the directory ADM_ACCESS and
   for all its entries through STATUS_FUNC/STATUS_BATON, or, if ENTRY
   is non-NULL, only for that directory entry.

   PARENT_ENTRY is the entry for the parent of the directory or NULL
   if that directory is a working copy root.

   If SKIP_THIS_DIR is TRUE (and ENTRY is NULL), the directory's own
   status will not be reported.  However, upon recursing, all subdirs
   *will* be reported, regardless of this parameter's value.

   Other arguments are the same as those passed to
   svn_wc_get_status_editor4().  */
static svn_error_t *
get_dir_status(struct edit_baton *eb,
               const svn_wc_entry_t *parent_entry,
               svn_wc_adm_access_t *adm_access,
               const char *entry,
               const apr_array_header_t *ignore_patterns,
               svn_depth_t depth,
               svn_boolean_t get_all,
               svn_boolean_t no_ignore,
               svn_boolean_t skip_this_dir,
               svn_wc_status_func3_t status_func,
               void *status_baton,
               svn_cancel_func_t cancel_func,
               void *cancel_baton,
               apr_pool_t *pool)
{
  apr_hash_t *entries;
  apr_hash_index_t *hi;
  const svn_wc_entry_t *dir_entry;
  const char *path = svn_wc_adm_access_path(adm_access);
  apr_hash_t *dirents;
  apr_array_header_t *patterns = NULL;
  apr_pool_t *iterpool, *subpool = svn_pool_create(pool);
  apr_array_header_t *tree_conflicts;
  int j;

  /* See if someone wants to cancel this operation. */
  if (cancel_func)
    SVN_ERR(cancel_func(cancel_baton));

  if (depth == svn_depth_unknown)
    depth = svn_depth_infinity;

  /* Load entries file for the directory into the requested pool. */
  SVN_ERR(svn_wc_entries_read(&entries, adm_access, FALSE, subpool));

  /* Read PATH's dirents. */
  SVN_ERR(svn_io_get_dirents2(&dirents, path, subpool));

  /* Get this directory's entry. */
  SVN_ERR(svn_wc_entry(&dir_entry, path, adm_access, FALSE, subpool));

  /* If "this dir" has "svn:externals" property set on it, store the
     name and value in traversal_info, along with this directory's depth.
     (Also, we want to track the externals internally so we can report
     status more accurately.) */
    {
      const svn_string_t *prop_val;
      SVN_ERR(svn_wc_prop_get(&prop_val, SVN_PROP_EXTERNALS, path,
                              adm_access, subpool));
      if (prop_val)
        {
          apr_array_header_t *ext_items;
          int i;

          if (eb->traversal_info)
            {
              apr_pool_t *dup_pool = eb->traversal_info->pool;
              const char *dup_path = apr_pstrdup(dup_pool, path);
              const char *dup_val = apr_pstrmemdup(dup_pool, prop_val->data,
                                                   prop_val->len);

              /* First things first -- we put the externals information
                 into the "global" traversal info structure. */
              apr_hash_set(eb->traversal_info->externals_old,
                           dup_path, APR_HASH_KEY_STRING, dup_val);
              apr_hash_set(eb->traversal_info->externals_new,
                           dup_path, APR_HASH_KEY_STRING, dup_val);
              apr_hash_set(eb->traversal_info->depths,
                           dup_path, APR_HASH_KEY_STRING,
                           svn_depth_to_word(dir_entry->depth));
            }

          /* Now, parse the thing, and copy the parsed results into
             our "global" externals hash. */
          SVN_ERR(svn_wc_parse_externals_description3(&ext_items, path,
                                                      prop_val->data, FALSE,
                                                      pool));
          for (i = 0; ext_items && i < ext_items->nelts; i++)
            {
              svn_wc_external_item2_t *item;

              item = APR_ARRAY_IDX(ext_items, i, svn_wc_external_item2_t *);
              apr_hash_set(eb->externals, svn_path_join(path,
                                                        item->target_dir,
                                                        pool),
                           APR_HASH_KEY_STRING, item);
            }
        }
    }

  /* Early out -- our caller only cares about a single ENTRY in this
     directory.  */
  if (entry)
    {
      const svn_wc_entry_t *entry_entry;
      svn_io_dirent_t* dirent_p = apr_hash_get(dirents, entry,
                                               APR_HASH_KEY_STRING);
      entry_entry = apr_hash_get(entries, entry, APR_HASH_KEY_STRING);

      /* If ENTRY is versioned, send its versioned status. */
      if (entry_entry)
        {
          SVN_ERR(handle_dir_entry(eb, adm_access, entry, dir_entry,
                                   entry_entry,
                                   dirent_p ? dirent_p->kind : svn_node_none,
                                   dirent_p ? dirent_p->special : FALSE,
                                   ignore_patterns, depth, get_all,
                                   no_ignore, status_func, status_baton,
                                   cancel_func, cancel_baton, subpool));
        }
      /* Otherwise, if it exists, send its unversioned status. */
      else if (dirent_p)
        {
          if (ignore_patterns && ! patterns)
            SVN_ERR(collect_ignore_patterns(&patterns, ignore_patterns,
                                            adm_access, subpool));
          SVN_ERR(send_unversioned_item(entry, dirent_p->kind,
                                        dirent_p->special, adm_access,
                                        patterns, eb->externals, no_ignore,
                                        eb->repos_locks, eb->repos_root,
                                        status_func, status_baton, subpool));
        }
      /* Otherwise, if it doesn't exist, but is a tree conflict victim,
         send its unversioned status. */
      else
        {
          svn_wc_conflict_description_t *tree_conflict;
          SVN_ERR(svn_wc__get_tree_conflict(&tree_conflict,
                                           svn_path_join(path, entry, subpool),
                                           adm_access, subpool));
          if (tree_conflict)
            {
              /* A tree conflict will block commit, so we'll pass TRUE
                 instead of the user's no_ignore arg. */
              if (ignore_patterns && ! patterns)
                SVN_ERR(collect_ignore_patterns(&patterns, ignore_patterns,
                                                adm_access, subpool));
              SVN_ERR(send_unversioned_item(entry, svn_node_none, FALSE,
                                            adm_access, patterns,
                                            eb->externals, TRUE,
                                            eb->repos_locks, eb->repos_root,
                                            status_func, status_baton,
                                            subpool));
            }
        }

      /* Regardless, we're done here.  Let's go home. */
      return SVN_NO_ERROR;
    }

  /** If we get here, ENTRY is NULL and we are handling all the
      directory entries (depending on specified depth). */

  /* Handle "this-dir" first. */
  if (! skip_this_dir)
    SVN_ERR(send_status_structure(path, adm_access, dir_entry,
                                  parent_entry, svn_node_dir, FALSE,
                                  get_all, FALSE, eb->repos_locks,
                                  eb->repos_root, status_func, status_baton,
                                  subpool));

  /* If the requested depth is empty, we only need status on this-dir. */
  if (depth == svn_depth_empty)
    return SVN_NO_ERROR;

  /* Make our iteration pool. */
  iterpool = svn_pool_create(subpool);

  /* Add empty status structures for each of the unversioned things.
     This also catches externals; not sure whether that's good or bad,
     but it's what's happening right now. */
  for (hi = apr_hash_first(subpool, dirents); hi; hi = apr_hash_next(hi))
    {
      const void *key;
      apr_ssize_t klen;
      void *val;
      svn_io_dirent_t *dirent_p;

      svn_pool_clear(iterpool);

      apr_hash_this(hi, &key, &klen, &val);

      /* Skip versioned, non-external things, and skip the
         administrative directory. */
      if (apr_hash_get(entries, key, klen)
          || svn_wc_is_adm_dir(key, iterpool))
        continue;

      dirent_p = val;

      if (depth == svn_depth_files && dirent_p->kind == svn_node_dir)
        continue;

      if (ignore_patterns && ! patterns)
        SVN_ERR(collect_ignore_patterns(&patterns, ignore_patterns,
                                        adm_access, subpool));

      SVN_ERR(send_unversioned_item(key, dirent_p->kind, dirent_p->special,
                                    adm_access,
                                    patterns, eb->externals, no_ignore,
                                    eb->repos_locks, eb->repos_root,
                                    status_func, status_baton, iterpool));
    }

  /* Add empty status structures for nonexistent tree conflict victims. */
  SVN_ERR(svn_wc__read_tree_conflicts(&tree_conflicts,
                                      dir_entry->tree_conflict_data,
                                      path, subpool));

  for (j = 0; j < tree_conflicts->nelts; j++)
    {
      svn_wc_conflict_description_t *conflict;
      char *tree_basename;

      svn_pool_clear(iterpool);

      conflict = APR_ARRAY_IDX(tree_conflicts, j,
                               svn_wc_conflict_description_t *);

      /* Skip versioned and non-versioned things. */
      tree_basename = svn_path_basename(conflict->path, iterpool);
      if (apr_hash_get(entries, tree_basename, APR_HASH_KEY_STRING)
          || apr_hash_get(dirents, tree_basename, APR_HASH_KEY_STRING))
        continue;

      if (ignore_patterns && ! patterns)
        SVN_ERR(collect_ignore_patterns(&patterns, ignore_patterns,
                                        adm_access, subpool));

      SVN_ERR(send_unversioned_item(tree_basename, svn_node_none, FALSE,
                                    adm_access, patterns, eb->externals,
                                    no_ignore, eb->repos_locks, eb->repos_root,
                                    status_func, status_baton, iterpool));
    }



  /* Loop over entries hash */
  for (hi = apr_hash_first(pool, entries); hi; hi = apr_hash_next(hi))
    {
      const void *key;
      void *val;
      svn_io_dirent_t *dirent_p;

      /* Get the next entry */
      apr_hash_this(hi, &key, NULL, &val);

      dirent_p = apr_hash_get(dirents, key, APR_HASH_KEY_STRING);

      /* ### todo: What if the subdir is from another repository? */

      /* Skip "this-dir". */
      if (strcmp(key, SVN_WC_ENTRY_THIS_DIR) == 0)
        continue;

      /* Skip directories if user is only interested in files */
      if (depth == svn_depth_files
          && dirent_p && dirent_p->kind == svn_node_dir)
        continue;

      /* Clear the iteration subpool. */
      svn_pool_clear(iterpool);

      /* Handle this directory entry (possibly recursing). */
      SVN_ERR(handle_dir_entry(eb, adm_access, key, dir_entry, val,
                               dirent_p ? dirent_p->kind : svn_node_none,
                               dirent_p ? dirent_p->special : FALSE,
                               ignore_patterns,
                               depth == svn_depth_infinity ? depth
                                                           : svn_depth_empty,
                               get_all, no_ignore,
                               status_func, status_baton, cancel_func,
                               cancel_baton, iterpool));
    }

  /* Destroy our subpools. */
  svn_pool_destroy(subpool);

  return SVN_NO_ERROR;
}



/*** Helpers ***/

/* A faux status callback function for stashing STATUS item in an hash
   (which is the BATON), keyed on PATH.  This implements the
   svn_wc_status_func3_t interface. */
static svn_error_t *
hash_stash(void *baton,
           const char *path,
           svn_wc_status2_t *status,
           apr_pool_t *pool)
{
  apr_hash_t *stat_hash = baton;
  apr_pool_t *hash_pool = apr_hash_pool_get(stat_hash);
  assert(! apr_hash_get(stat_hash, path, APR_HASH_KEY_STRING));
  apr_hash_set(stat_hash, apr_pstrdup(hash_pool, path),
               APR_HASH_KEY_STRING, svn_wc_dup_status2(status, hash_pool));

  return SVN_NO_ERROR;
}


/* Look up the key PATH in BATON->STATII.  IS_DIR_BATON indicates whether
   baton is a struct *dir_baton or struct *file_baton.  If the value doesn't
   yet exist, and the REPOS_TEXT_STATUS indicates that this is an
   addition, create a new status struct using the hash's pool.

   If IS_DIR_BATON is true, THIS_DIR_BATON is a *dir_baton cotaining the out
   of date (ood) information we want to set in BATON.  This is necessary
   because this function tweaks the status of out-of-date directories
   (BATON == THIS_DIR_BATON) and out-of-date directories' parents
   (BATON == THIS_DIR_BATON->parent_baton).  In the latter case THIS_DIR_BATON
   contains the ood info we want to bubble up to ancestor directories so these
   accurately reflect the fact they have an ood descendant.

   Merge REPOS_TEXT_STATUS and REPOS_PROP_STATUS into the status structure's
   "network" fields.

   Iff IS_DIR_BATON is true, DELETED_REV is used as follows, otherwise it
   is ignored:

       If REPOS_TEXT_STATUS is svn_wc_status_deleted then DELETED_REV is
       optionally the revision path was deleted, in all other cases it must
       be set to SVN_INVALID_REVNUM.  If DELETED_REV is not
       SVN_INVALID_REVNUM and REPOS_TEXT_STATUS is svn_wc_status_deleted,
       then use DELETED_REV to set PATH's ood_last_cmt_rev field in BATON.
       If DELETED_REV is SVN_INVALID_REVNUM and REPOS_TEXT_STATUS is
       svn_wc_status_deleted, set PATH's ood_last_cmt_rev to its parent's
       ood_last_cmt_rev value - see comment below.

   If a new struct was added, set the repos_lock to REPOS_LOCK. */
static svn_error_t *
tweak_statushash(void *baton,
                 void *this_dir_baton,
                 svn_boolean_t is_dir_baton,
                 svn_wc_adm_access_t *adm_access,
                 const char *path,
                 svn_boolean_t is_dir,
                 enum svn_wc_status_kind repos_text_status,
                 enum svn_wc_status_kind repos_prop_status,
                 svn_revnum_t deleted_rev,
                 svn_lock_t *repos_lock)
{
  svn_wc_status2_t *statstruct;
  apr_pool_t *pool;
  apr_hash_t *statushash;

  if (is_dir_baton)
    statushash = ((struct dir_baton *) baton)->statii;
  else
    statushash = ((struct file_baton *) baton)->dir_baton->statii;
  pool = apr_hash_pool_get(statushash);

  /* Is PATH already a hash-key? */
  statstruct = apr_hash_get(statushash, path, APR_HASH_KEY_STRING);

  /* If not, make it so. */
  if (! statstruct)
    {
      /* If this item isn't being added, then we're most likely
         dealing with a non-recursive (or at least partially
         non-recursive) working copy.  Due to bugs in how the client
         reports the state of non-recursive working copies, the
         repository can send back responses about paths that don't
         even exist locally.  Our best course here is just to ignore
         those responses.  After all, if the client had reported
         correctly in the first, that path would either be mentioned
         as an 'add' or not mentioned at all, depending on how we
         eventually fix the bugs in non-recursivity.  See issue
         #2122 for details. */
      if (repos_text_status != svn_wc_status_added)
        return SVN_NO_ERROR;

      /* Use the public API to get a statstruct, and put it into the hash. */
      SVN_ERR(svn_wc_status2(&statstruct, path, adm_access, pool));
      statstruct->repos_lock = repos_lock;
      apr_hash_set(statushash, apr_pstrdup(pool, path),
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

  /* Copy out-of-date info. */
  if (is_dir_baton)
    {
      struct dir_baton *b = this_dir_baton;

      if (b->url)
        {
          if (statstruct->repos_text_status == svn_wc_status_deleted)
            {
              /* When deleting PATH, BATON is for PATH's parent,
                 so we must construct PATH's real statstruct->url. */
              statstruct->url =
                svn_path_url_add_component2(b->url,
                                            svn_path_basename(path, pool),
                                            pool);
            }
          else
            statstruct->url = apr_pstrdup(pool, b->url);
        }

      /* The last committed date, and author for deleted items
         isn't available. */
      if (statstruct->repos_text_status == svn_wc_status_deleted)
        {
          statstruct->ood_kind = is_dir ? svn_node_dir : svn_node_file;

          /* Pre 1.5 servers don't provide the revision a path was deleted.
             So we punt and use the last committed revision of the path's
             parent, which has some chance of being correct.  At worse it
             is a higher revision than the path was deleted, but this is
             better than nothing... */
          if (deleted_rev == SVN_INVALID_REVNUM)
            statstruct->ood_last_cmt_rev =
              ((struct dir_baton *) baton)->ood_last_cmt_rev;
          else
            statstruct->ood_last_cmt_rev = deleted_rev;
        }
      else
        {
          statstruct->ood_kind = b->ood_kind;
          statstruct->ood_last_cmt_rev = b->ood_last_cmt_rev;
          statstruct->ood_last_cmt_date = b->ood_last_cmt_date;
          if (b->ood_last_cmt_author)
            statstruct->ood_last_cmt_author =
              apr_pstrdup(pool, b->ood_last_cmt_author);
        }

    }
  else
    {
      struct file_baton *b = baton;
      if (b->url)
        statstruct->url = apr_pstrdup(pool, b->url);
      statstruct->ood_last_cmt_rev = b->ood_last_cmt_rev;
      statstruct->ood_last_cmt_date = b->ood_last_cmt_date;
      statstruct->ood_kind = b->ood_kind;
      if (b->ood_last_cmt_author)
        statstruct->ood_last_cmt_author =
          apr_pstrdup(pool, b->ood_last_cmt_author);
    }
  return SVN_NO_ERROR;
}

/* Returns the URL for DB, or NULL: */
static const char *
find_dir_url(const struct dir_baton *db, apr_pool_t *pool)
{
  /* If we have no name, we're the root, return the anchor URL. */
  if (! db->name)
    return db->edit_baton->anchor_status->entry->url;
  else
    {
      const char *url;
      struct dir_baton *pb = db->parent_baton;
      svn_wc_status2_t *status = apr_hash_get(pb->statii, db->name,
                                              APR_HASH_KEY_STRING);
      /* Note that status->entry->url is NULL in the case of a missing
       * directory, which means we need to recurse up another level to
       * get a useful URL. */
      if (status && status->entry && status->entry->url)
        return status->entry->url;

      url = find_dir_url(pb, pool);
      if (url)
        return svn_path_url_add_component2(url, db->name, pool);
      else
        return NULL;
    }
}



/* Create a new dir_baton for subdir PATH. */
static svn_error_t *
make_dir_baton(void **dir_baton,
               const char *path,
               struct edit_baton *edit_baton,
               struct dir_baton *parent_baton,
               apr_pool_t *pool)
{
  struct dir_baton *pb = parent_baton;
  struct edit_baton *eb = edit_baton;
  struct dir_baton *d = apr_pcalloc(pool, sizeof(*d));
  const char *full_path;
  svn_wc_status2_t *status_in_parent;

  SVN_ERR_ASSERT(path || (! pb));

  /* Construct the full path of this directory. */
  if (pb)
    full_path = svn_path_join(eb->anchor, path, pool);
  else
    full_path = apr_pstrdup(pool, eb->anchor);

  /* Finish populating the baton members. */
  d->path = full_path;
  d->name = path ? (svn_path_basename(path, pool)) : NULL;
  d->edit_baton = edit_baton;
  d->parent_baton = parent_baton;
  d->pool = pool;
  d->statii = apr_hash_make(pool);
  d->url = apr_pstrdup(pool, find_dir_url(d, pool));
  d->ood_last_cmt_rev = SVN_INVALID_REVNUM;
  d->ood_last_cmt_date = 0;
  d->ood_kind = svn_node_dir;
  d->ood_last_cmt_author = NULL;

  if (pb)
    {
      if (pb->excluded)
        d->excluded = TRUE;
      else if (pb->depth == svn_depth_immediates)
        d->depth = svn_depth_empty;
      else if (pb->depth == svn_depth_files || pb->depth == svn_depth_empty)
        d->excluded = TRUE;
      else if (pb->depth == svn_depth_unknown)
        /* This is only tentative, it can be overridden from d's entry
           later. */
        d->depth = svn_depth_unknown;
      else
        d->depth = svn_depth_infinity;
    }
  else
    {
      d->depth = eb->default_depth;
    }

  /* Get the status for this path's children.  Of course, we only want
     to do this if the path is versioned as a directory. */
  if (pb)
    status_in_parent = apr_hash_get(pb->statii, d->path, APR_HASH_KEY_STRING);
  else
    status_in_parent = eb->anchor_status;

  /* Order is important here.  We can't depend on status_in_parent->entry
     being non-NULL until after we've checked all the conditions that
     might indicate that the parent is unversioned ("unversioned" for
     our purposes includes being an external or ignored item). */
  if (status_in_parent
      && (status_in_parent->text_status != svn_wc_status_unversioned)
      && (status_in_parent->text_status != svn_wc_status_missing)
      && (status_in_parent->text_status != svn_wc_status_obstructed)
      && (status_in_parent->text_status != svn_wc_status_external)
      && (status_in_parent->text_status != svn_wc_status_ignored)
      && (status_in_parent->entry->kind == svn_node_dir)
      && (! d->excluded)
      && (d->depth == svn_depth_unknown
          || d->depth == svn_depth_infinity
          || d->depth == svn_depth_files
          || d->depth == svn_depth_immediates)
          )
    {
      svn_wc_adm_access_t *dir_access;
      svn_wc_status2_t *this_dir_status;
      const apr_array_header_t *ignores = eb->ignores;
      SVN_ERR(svn_wc_adm_retrieve(&dir_access, eb->adm_access,
                                  d->path, pool));
      SVN_ERR(get_dir_status(eb, status_in_parent->entry, dir_access, NULL,
                             ignores, d->depth == svn_depth_files ?
                             svn_depth_files : svn_depth_immediates,
                             TRUE, TRUE, TRUE, hash_stash, d->statii, NULL,
                             NULL, pool));

      /* If we found a depth here, it should govern. */
      this_dir_status = apr_hash_get(d->statii, d->path, APR_HASH_KEY_STRING);
      if (this_dir_status && this_dir_status->entry
          && (d->depth == svn_depth_unknown
              || d->depth > status_in_parent->entry->depth))
        {
          d->depth = this_dir_status->entry->depth;
        }
    }

  *dir_baton = d;
  return SVN_NO_ERROR;
}


/* Make a file baton, using a new subpool of PARENT_DIR_BATON's pool.
   NAME is just one component, not a path. */
static struct file_baton *
make_file_baton(struct dir_baton *parent_dir_baton,
                const char *path,
                apr_pool_t *pool)
{
  struct dir_baton *pb = parent_dir_baton;
  struct edit_baton *eb = pb->edit_baton;
  struct file_baton *f = apr_pcalloc(pool, sizeof(*f));
  const char *full_path;

  /* Construct the full path of this file. */
  full_path = svn_path_join(eb->anchor, path, pool);

  /* Finish populating the baton members. */
  f->path = full_path;
  f->name = svn_path_basename(path, pool);
  f->pool = pool;
  f->dir_baton = pb;
  f->edit_baton = eb;
  f->url = svn_path_url_add_component2(find_dir_url(pb, pool),
                                       svn_path_basename(full_path, pool),
                                       pool);
  f->ood_last_cmt_rev = SVN_INVALID_REVNUM;
  f->ood_last_cmt_date = 0;
  f->ood_kind = svn_node_file;
  f->ood_last_cmt_author = NULL;
  return f;
}


svn_boolean_t
svn_wc__is_sendable_status(const svn_wc_status2_t *status,
                           svn_boolean_t no_ignore,
                           svn_boolean_t get_all)
{
  /* If the repository status was touched at all, it's interesting. */
  if (status->repos_text_status != svn_wc_status_none)
    return TRUE;
  if (status->repos_prop_status != svn_wc_status_none)
    return TRUE;

  /* If there is a lock in the repository, send it. */
  if (status->repos_lock)
    return TRUE;

  /* If the item is ignored, and we don't want ignores, skip it. */
  if ((status->text_status == svn_wc_status_ignored) && (! no_ignore))
    return FALSE;

  /* If we want everything, we obviously want this single-item subset
     of everything. */
  if (get_all)
    return TRUE;

  /* If the item is unversioned, display it. */
  if (status->text_status == svn_wc_status_unversioned)
    return TRUE;

  /* If the text, property or tree state is interesting, send it. */
  if ((status->text_status != svn_wc_status_none)
      && (status->text_status != svn_wc_status_normal))
    return TRUE;
  if ((status->prop_status != svn_wc_status_none)
      && (status->prop_status != svn_wc_status_normal))
    return TRUE;
  if (status->tree_conflict)
    return TRUE;

  /* If it's locked or switched, send it. */
  if (status->locked)
    return TRUE;
  if (status->switched)
    return TRUE;
  if (status->file_external)
    return TRUE;

  /* If there is a lock token, send it. */
  if (status->entry && status->entry->lock_token)
    return TRUE;

  /* If the entry is associated with a changelist, send it. */
  if (status->entry && status->entry->changelist)
    return TRUE;

  /* Otherwise, don't send it. */
  return FALSE;
}


/* Baton for mark_status. */
struct status_baton
{
  svn_wc_status_func3_t real_status_func;  /* real status function */
  void *real_status_baton;                 /* real status baton */
};

/* A status callback function which wraps the *real* status
   function/baton.   It simply sets the "repos_text_status" field of the
   STATUS to svn_wc_status_deleted and passes it off to the real
   status func/baton. */
static svn_error_t *
mark_deleted(void *baton,
             const char *path,
             svn_wc_status2_t *status,
             apr_pool_t *pool)
{
  struct status_baton *sb = baton;
  status->repos_text_status = svn_wc_status_deleted;
  return sb->real_status_func(sb->real_status_baton, path, status, pool);
}


/* Handle a directory's STATII hash.  EB is the edit baton.  DIR_PATH
   and DIR_ENTRY are the on-disk path and entry, respectively, for the
   directory itself.  Descend into subdirectories according to DEPTH.
   Also, if DIR_WAS_DELETED is set, each status that is reported
   through this function will have its repos_text_status field showing
   a deletion.  Use POOL for all allocations. */
static svn_error_t *
handle_statii(struct edit_baton *eb,
              svn_wc_entry_t *dir_entry,
              const char *dir_path,
              apr_hash_t *statii,
              svn_boolean_t dir_was_deleted,
              svn_depth_t depth,
              apr_pool_t *pool)
{
  const apr_array_header_t *ignores = eb->ignores;
  apr_hash_index_t *hi;
  apr_pool_t *subpool = svn_pool_create(pool);
  svn_wc_status_func3_t status_func = eb->status_func;
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
  for (hi = apr_hash_first(pool, statii); hi; hi = apr_hash_next(hi))
    {
      const void *key;
      void *val;
      svn_wc_status2_t *status;

      apr_hash_this(hi, &key, NULL, &val);
      status = val;

      /* Clear the subpool. */
      svn_pool_clear(subpool);

      /* Now, handle the status.  We don't recurse for svn_depth_immediates
         because we already have the subdirectories' statii. */
      if (status->text_status != svn_wc_status_obstructed
          && status->text_status != svn_wc_status_missing
          && status->entry && status->entry->kind == svn_node_dir
          && (depth == svn_depth_unknown
              || depth == svn_depth_infinity))
        {
          svn_wc_adm_access_t *dir_access;

          SVN_ERR(svn_wc_adm_retrieve(&dir_access, eb->adm_access,
                                      key, subpool));

          SVN_ERR(get_dir_status(eb, dir_entry, dir_access, NULL,
                                 ignores, depth, eb->get_all,
                                 eb->no_ignore, TRUE, status_func,
                                 status_baton, eb->cancel_func,
                                 eb->cancel_baton, subpool));
        }
      if (dir_was_deleted)
        status->repos_text_status = svn_wc_status_deleted;
      if (svn_wc__is_sendable_status(status, eb->no_ignore, eb->get_all))
        SVN_ERR((eb->status_func)(eb->status_baton, key, status, subpool));
    }

  /* Destroy the subpool. */
  svn_pool_destroy(subpool);

  return SVN_NO_ERROR;
}


/*----------------------------------------------------------------------*/

/*** The callbacks we'll plug into an svn_delta_editor_t structure. ***/

static svn_error_t *
set_target_revision(void *edit_baton,
                    svn_revnum_t target_revision,
                    apr_pool_t *pool)
{
  struct edit_baton *eb = edit_baton;
  *(eb->target_revision) = target_revision;
  return SVN_NO_ERROR;
}


static svn_error_t *
open_root(void *edit_baton,
          svn_revnum_t base_revision,
          apr_pool_t *pool,
          void **dir_baton)
{
  struct edit_baton *eb = edit_baton;
  eb->root_opened = TRUE;
  return make_dir_baton(dir_baton, NULL, eb, NULL, pool);
}


static svn_error_t *
delete_entry(const char *path,
             svn_revnum_t revision,
             void *parent_baton,
             apr_pool_t *pool)
{
  struct dir_baton *db = parent_baton;
  struct edit_baton *eb = db->edit_baton;
  apr_hash_t *entries;
  const char *name = svn_path_basename(path, pool);
  const char *full_path = svn_path_join(eb->anchor, path, pool);
  const char *dir_path;
  svn_node_kind_t kind;
  svn_wc_adm_access_t *adm_access;
  const char *hash_key;
  const svn_wc_entry_t *entry;
  svn_error_t *err;

  /* Note:  when something is deleted, it's okay to tweak the
     statushash immediately.  No need to wait until close_file or
     close_dir, because there's no risk of having to honor the 'added'
     flag.  We already know this item exists in the working copy. */

  /* Read the parent's entries file.  If the deleted thing is not
     versioned in this working copy, it was probably deleted via this
     working copy.  No need to report such a thing. */
  /* ### use svn_wc_entry() instead? */
  SVN_ERR(svn_wc__entry_versioned(&entry, full_path, eb->adm_access,
                                  FALSE, pool));
  if (entry->kind == svn_node_dir)
    {
      dir_path = full_path;
      hash_key = SVN_WC_ENTRY_THIS_DIR;
    }
  else
    {
      dir_path = svn_path_dirname(full_path, pool);
      hash_key = name;
    }

  err = svn_wc_adm_retrieve(&adm_access, eb->adm_access, dir_path, pool);
  if (err)
    {
      SVN_ERR(svn_io_check_path(full_path, &kind, pool));
      if ((kind == svn_node_none) && (err->apr_err == SVN_ERR_WC_NOT_LOCKED))
        {
          /* We're probably dealing with a non-recursive, (or
             partially non-recursive, working copy.  Due to deep bugs
             in how the client reports the state of non-recursive
             working copies, the repository can report that a path is
             deleted in an area where we not only don't have the path
             in question, we don't even have its parent(s).  A
             complete fix would require a serious revamp of how
             non-recursive working copies store and report themselves,
             plus some thinking about the UI behavior we want when
             someone runs 'svn st -u' in a [partially] non-recursive
             working copy.

             For now, we just do our best to detect the condition and
             not report an error if it holds.  See issue #2122. */
          svn_error_clear(err);
          return SVN_NO_ERROR;
        }
      else
        return err;
    }

  SVN_ERR(svn_wc_entries_read(&entries, adm_access, FALSE, pool));
  if (apr_hash_get(entries, hash_key, APR_HASH_KEY_STRING))
    SVN_ERR(tweak_statushash(db, db, TRUE, eb->adm_access,
                             full_path, entry->kind == svn_node_dir,
                             svn_wc_status_deleted, 0, revision, NULL));

  /* Mark the parent dir -- it lost an entry (unless that parent dir
     is the root node and we're not supposed to report on the root
     node).  */
  if (db->parent_baton && (! *eb->target))
    SVN_ERR(tweak_statushash(db->parent_baton, db, TRUE, eb->adm_access,
                             db->path, entry->kind == svn_node_dir,
                             svn_wc_status_modified, 0, SVN_INVALID_REVNUM,
                             NULL));

  return SVN_NO_ERROR;
}


static svn_error_t *
add_directory(const char *path,
              void *parent_baton,
              const char *copyfrom_path,
              svn_revnum_t copyfrom_revision,
              apr_pool_t *pool,
              void **child_baton)
{
  struct dir_baton *pb = parent_baton;
  struct edit_baton *eb = pb->edit_baton;
  struct dir_baton *new_db;

  SVN_ERR(make_dir_baton(child_baton, path, eb, pb, pool));

  /* Make this dir as added. */
  new_db = *child_baton;
  new_db->added = TRUE;

  /* Mark the parent as changed;  it gained an entry. */
  pb->text_changed = TRUE;

  return SVN_NO_ERROR;
}


static svn_error_t *
open_directory(const char *path,
               void *parent_baton,
               svn_revnum_t base_revision,
               apr_pool_t *pool,
               void **child_baton)
{
  struct dir_baton *pb = parent_baton;
  return make_dir_baton(child_baton, path, pb->edit_baton, pb, pool);
}


static svn_error_t *
change_dir_prop(void *dir_baton,
                const char *name,
                const svn_string_t *value,
                apr_pool_t *pool)
{
  struct dir_baton *db = dir_baton;
  if (svn_wc_is_normal_prop(name))
    db->prop_changed = TRUE;

  /* Note any changes to the repository. */
  if (value != NULL)
    {
      if (strcmp(name, SVN_PROP_ENTRY_COMMITTED_REV) == 0)
        db->ood_last_cmt_rev = SVN_STR_TO_REV(value->data);
      else if (strcmp(name, SVN_PROP_ENTRY_LAST_AUTHOR) == 0)
        db->ood_last_cmt_author = apr_pstrdup(db->pool, value->data);
      else if (strcmp(name, SVN_PROP_ENTRY_COMMITTED_DATE) == 0)
        {
          apr_time_t tm;
          SVN_ERR(svn_time_from_cstring(&tm, value->data, db->pool));
          db->ood_last_cmt_date = tm;
        }
    }

  return SVN_NO_ERROR;
}



static svn_error_t *
close_directory(void *dir_baton,
                apr_pool_t *pool)
{
  struct dir_baton *db = dir_baton;
  struct dir_baton *pb = db->parent_baton;
  struct edit_baton *eb = db->edit_baton;
  svn_wc_status2_t *dir_status = NULL;

  /* If nothing has changed and directory has no out of
     date descendants, return. */
  if (db->added || db->prop_changed || db->text_changed
      || db->ood_last_cmt_rev != SVN_INVALID_REVNUM)
    {
      enum svn_wc_status_kind repos_text_status;
      enum svn_wc_status_kind repos_prop_status;

      /* If this is a new directory, add it to the statushash. */
      if (db->added)
        {
          repos_text_status = svn_wc_status_added;
          repos_prop_status = db->prop_changed ? svn_wc_status_added
                              : svn_wc_status_none;
        }
      else
        {
          repos_text_status = db->text_changed ? svn_wc_status_modified
                              : svn_wc_status_none;
          repos_prop_status = db->prop_changed ? svn_wc_status_modified
                              : svn_wc_status_none;
        }

      /* Maybe add this directory to its parent's status hash.  Note
         that tweak_statushash won't do anything if repos_text_status
         is not svn_wc_status_added. */
      if (pb)
        {
          /* ### When we add directory locking, we need to find a
             ### directory lock here. */
          SVN_ERR(tweak_statushash(pb, db, TRUE,
                                   eb->adm_access,
                                   db->path, TRUE,
                                   repos_text_status,
                                   repos_prop_status, SVN_INVALID_REVNUM,
                                   NULL));
        }
      else
        {
          /* We're editing the root dir of the WC.  As its repos
             status info isn't otherwise set, set it directly to
             trigger invocation of the status callback below. */
          eb->anchor_status->repos_prop_status = repos_prop_status;
          eb->anchor_status->repos_text_status = repos_text_status;

          /* If the root dir is out of date set the ood info directly too. */
          if (db->ood_last_cmt_rev != eb->anchor_status->entry->revision)
            {
              eb->anchor_status->ood_last_cmt_rev = db->ood_last_cmt_rev;
              eb->anchor_status->ood_last_cmt_date = db->ood_last_cmt_date;
              eb->anchor_status->ood_kind = db->ood_kind;
              eb->anchor_status->ood_last_cmt_author =
                apr_pstrdup(pool, db->ood_last_cmt_author);
            }
        }
    }

  /* Handle this directory's statuses, and then note in the parent
     that this has been done. */
  if (pb && ! db->excluded)
    {
      svn_boolean_t was_deleted = FALSE;

      /* See if the directory was deleted or replaced. */
      dir_status = apr_hash_get(pb->statii, db->path, APR_HASH_KEY_STRING);
      if (dir_status &&
          ((dir_status->repos_text_status == svn_wc_status_deleted)
           || (dir_status->repos_text_status == svn_wc_status_replaced)))
        was_deleted = TRUE;

      /* Now do the status reporting. */
      SVN_ERR(handle_statii(eb, dir_status ? dir_status->entry : NULL,
                            db->path, db->statii, was_deleted, db->depth,
                            pool));
      if (dir_status && svn_wc__is_sendable_status(dir_status, eb->no_ignore,
                                                  eb->get_all))
        SVN_ERR((eb->status_func)(eb->status_baton, db->path, dir_status,
                                  pool));
      apr_hash_set(pb->statii, db->path, APR_HASH_KEY_STRING, NULL);
    }
  else if (! pb)
    {
      /* If this is the top-most directory, and the operation had a
         target, we should only report the target. */
      if (*eb->target)
        {
          svn_wc_status2_t *tgt_status;
          const char *path = svn_path_join(eb->anchor, eb->target, pool);
          dir_status = eb->anchor_status;
          tgt_status = apr_hash_get(db->statii, path, APR_HASH_KEY_STRING);
          if (tgt_status)
            {
              if (tgt_status->entry
                  && tgt_status->entry->kind == svn_node_dir)
                {
                  svn_wc_adm_access_t *dir_access;
                  SVN_ERR(svn_wc_adm_retrieve(&dir_access, eb->adm_access,
                                              path, pool));
                  SVN_ERR(get_dir_status
                          (eb, tgt_status->entry, dir_access, NULL,
                           eb->ignores, eb->default_depth, eb->get_all,
                           eb->no_ignore, TRUE,
                           eb->status_func, eb->status_baton,
                           eb->cancel_func, eb->cancel_baton, pool));
                }
              if (svn_wc__is_sendable_status(tgt_status, eb->no_ignore,
                                             eb->get_all))
                SVN_ERR((eb->status_func)(eb->status_baton, path, tgt_status,
                                          pool));
            }
        }
      else
        {
          /* Otherwise, we report on all our children and ourself.
             Note that our directory couldn't have been deleted,
             because it is the root of the edit drive. */
          SVN_ERR(handle_statii(eb, eb->anchor_status->entry, db->path,
                                db->statii, FALSE, eb->default_depth, pool));
          if (svn_wc__is_sendable_status(eb->anchor_status, eb->no_ignore,
                                         eb->get_all))
            SVN_ERR((eb->status_func)(eb->status_baton, db->path,
                                      eb->anchor_status, pool));
          eb->anchor_status = NULL;
        }
    }
  return SVN_NO_ERROR;
}



static svn_error_t *
add_file(const char *path,
         void *parent_baton,
         const char *copyfrom_path,
         svn_revnum_t copyfrom_revision,
         apr_pool_t *pool,
         void **file_baton)
{
  struct dir_baton *pb = parent_baton;
  struct file_baton *new_fb = make_file_baton(pb, path, pool);

  /* Mark parent dir as changed */
  pb->text_changed = TRUE;

  /* Make this file as added. */
  new_fb->added = TRUE;

  *file_baton = new_fb;
  return SVN_NO_ERROR;
}


static svn_error_t *
open_file(const char *path,
          void *parent_baton,
          svn_revnum_t base_revision,
          apr_pool_t *pool,
          void **file_baton)
{
  struct dir_baton *pb = parent_baton;
  struct file_baton *new_fb = make_file_baton(pb, path, pool);

  *file_baton = new_fb;
  return SVN_NO_ERROR;
}


static svn_error_t *
apply_textdelta(void *file_baton,
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
change_file_prop(void *file_baton,
                 const char *name,
                 const svn_string_t *value,
                 apr_pool_t *pool)
{
  struct file_baton *fb = file_baton;
  if (svn_wc_is_normal_prop(name))
    fb->prop_changed = TRUE;

  /* Note any changes to the repository. */
  if (value != NULL)
    {
      if (strcmp(name, SVN_PROP_ENTRY_COMMITTED_REV) == 0)
        fb->ood_last_cmt_rev = SVN_STR_TO_REV(value->data);
      else if (strcmp(name, SVN_PROP_ENTRY_LAST_AUTHOR) == 0)
        fb->ood_last_cmt_author = apr_pstrdup(fb->dir_baton->pool,
                                              value->data);
      else if (strcmp(name, SVN_PROP_ENTRY_COMMITTED_DATE) == 0)
        {
          apr_time_t tm;
          SVN_ERR(svn_time_from_cstring(&tm, value->data,
                                        fb->dir_baton->pool));
          fb->ood_last_cmt_date = tm;
        }
    }

  return SVN_NO_ERROR;
}


static svn_error_t *
close_file(void *file_baton,
           const char *text_checksum,  /* ignored, as we receive no data */
           apr_pool_t *pool)
{
  struct file_baton *fb = file_baton;
  enum svn_wc_status_kind repos_text_status;
  enum svn_wc_status_kind repos_prop_status;
  svn_lock_t *repos_lock = NULL;

  /* If nothing has changed, return. */
  if (! (fb->added || fb->prop_changed || fb->text_changed))
    return SVN_NO_ERROR;

  /* If this is a new file, add it to the statushash. */
  if (fb->added)
    {
      const char *url;
      repos_text_status = svn_wc_status_added;
      repos_prop_status = fb->prop_changed ? svn_wc_status_added : 0;

      if (fb->edit_baton->repos_locks)
        {
          url = find_dir_url(fb->dir_baton, pool);
          if (url)
            {
              url = svn_path_url_add_component2(url, fb->name, pool);
              repos_lock = apr_hash_get
                (fb->edit_baton->repos_locks,
                 svn_path_uri_decode(url +
                                     strlen(fb->edit_baton->repos_root),
                                     pool), APR_HASH_KEY_STRING);
            }
        }
    }
  else
    {
      repos_text_status = fb->text_changed ? svn_wc_status_modified : 0;
      repos_prop_status = fb->prop_changed ? svn_wc_status_modified : 0;
    }

  return tweak_statushash(fb, NULL, FALSE,
                          fb->edit_baton->adm_access,
                          fb->path, FALSE,
                          repos_text_status,
                          repos_prop_status, SVN_INVALID_REVNUM,
                          repos_lock);
}


static svn_error_t *
close_edit(void *edit_baton,
           apr_pool_t *pool)
{
  struct edit_baton *eb = edit_baton;
  const apr_array_header_t *ignores = eb->ignores;
  svn_error_t *err = NULL;

  /* If we get here and the root was not opened as part of the edit,
     we need to transmit statuses for everything.  Otherwise, we
     should be done. */
  if (eb->root_opened)
    goto cleanup;

  /* If we have a target, that's the thing we're sending, otherwise
     we're sending the anchor. */

  if (*eb->target)
    {
      svn_node_kind_t kind;
      const char *full_path = svn_path_join(eb->anchor, eb->target, pool);

      err = svn_io_check_path(full_path, &kind, pool);
      if (err) goto cleanup;

      if (kind == svn_node_dir)
        {
          const svn_wc_entry_t *tgt_entry;

          err = svn_wc_entry(&tgt_entry, full_path, eb->adm_access,
                             FALSE, pool);
          if (err) goto cleanup;

          if (! tgt_entry)
            {
              err = get_dir_status(eb, NULL, eb->adm_access, eb->target,
                                   ignores, svn_depth_empty, eb->get_all,
                                   TRUE, TRUE,
                                   eb->status_func, eb->status_baton,
                                   eb->cancel_func, eb->cancel_baton,
                                   pool);
              if (err) goto cleanup;
            }
          else
            {
              svn_wc_adm_access_t *tgt_access;

              err = svn_wc_adm_retrieve(&tgt_access, eb->adm_access,
                                        full_path, pool);
              if (err) goto cleanup;

              err = get_dir_status(eb, NULL, tgt_access, NULL, ignores,
                                   eb->default_depth, eb->get_all,
                                   eb->no_ignore, FALSE,
                                   eb->status_func, eb->status_baton,
                                   eb->cancel_func, eb->cancel_baton,
                                   pool);
              if (err) goto cleanup;
            }
        }
      else
        {
          err = get_dir_status(eb, NULL, eb->adm_access, eb->target,
                               ignores, svn_depth_empty, eb->get_all,
                               TRUE, TRUE, eb->status_func, eb->status_baton,
                               eb->cancel_func, eb->cancel_baton, pool);
          if (err) goto cleanup;
        }
    }
  else
    {
      err = get_dir_status(eb, NULL, eb->adm_access, NULL, ignores,
                           eb->default_depth, eb->get_all, eb->no_ignore,
                           FALSE, eb->status_func, eb->status_baton,
                           eb->cancel_func, eb->cancel_baton, pool);
      if (err) goto cleanup;
    }

 cleanup:
  /* Let's make sure that we didn't harvest any traversal info for the
     anchor if we had a target. */
  if (eb->traversal_info && *eb->target)
    {
      apr_hash_set(eb->traversal_info->externals_old,
                   eb->anchor, APR_HASH_KEY_STRING, NULL);
      apr_hash_set(eb->traversal_info->externals_new,
                   eb->anchor, APR_HASH_KEY_STRING, NULL);
      apr_hash_set(eb->traversal_info->depths,
                   eb->anchor, APR_HASH_KEY_STRING, NULL);
    }

  return err;
}



/*** Public API ***/

svn_error_t *
svn_wc_get_status_editor4(const svn_delta_editor_t **editor,
                          void **edit_baton,
                          void **set_locks_baton,
                          svn_revnum_t *edit_revision,
                          svn_wc_adm_access_t *anchor,
                          const char *target,
                          svn_depth_t depth,
                          svn_boolean_t get_all,
                          svn_boolean_t no_ignore,
                          const apr_array_header_t *ignore_patterns,
                          svn_wc_status_func3_t status_func,
                          void *status_baton,
                          svn_cancel_func_t cancel_func,
                          void *cancel_baton,
                          svn_wc_traversal_info_t *traversal_info,
                          apr_pool_t *pool)
{
  struct edit_baton *eb;
  svn_delta_editor_t *tree_editor = svn_delta_default_editor(pool);

  /* Construct an edit baton. */
  eb = apr_palloc(pool, sizeof(*eb));
  eb->default_depth     = depth;
  eb->target_revision   = edit_revision;
  eb->adm_access        = anchor;
  eb->get_all           = get_all;
  eb->no_ignore         = no_ignore;
  eb->status_func       = status_func;
  eb->status_baton      = status_baton;
  eb->cancel_func       = cancel_func;
  eb->cancel_baton      = cancel_baton;
  eb->traversal_info    = traversal_info;
  eb->externals         = apr_hash_make(pool);
  eb->anchor            = svn_wc_adm_access_path(anchor);
  eb->target            = target;
  eb->root_opened       = FALSE;
  eb->repos_locks       = NULL;
  eb->repos_root        = NULL;

  /* Use the caller-provided ignore patterns if provided; the build-time
     configured defaults otherwise. */
  if (ignore_patterns)
    {
      eb->ignores = ignore_patterns;
    }
  else
    {
      apr_array_header_t *ignores = apr_array_make(pool, 16,
                                                   sizeof(const char *));
      svn_cstring_split_append(ignores, SVN_CONFIG_DEFAULT_GLOBAL_IGNORES,
                               "\n\r\t\v ", FALSE, pool);
      eb->ignores = ignores;
    }

  /* The edit baton's status structure maps to PATH, and the editor
     have to be aware of whether that is the anchor or the target. */
  SVN_ERR(svn_wc_status2(&(eb->anchor_status), eb->anchor, anchor, pool));

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
  SVN_ERR(svn_delta_get_cancellation_editor(cancel_func, cancel_baton,
                                            tree_editor, eb, editor,
                                            edit_baton, pool));

  if (set_locks_baton)
    *set_locks_baton = eb;

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc_status_set_repos_locks(void *edit_baton,
                              apr_hash_t *locks,
                              const char *repos_root,
                              apr_pool_t *pool)
{
  struct edit_baton *eb = edit_baton;

  eb->repos_locks = locks;
  eb->repos_root = apr_pstrdup(pool, repos_root);

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc_get_default_ignores(apr_array_header_t **patterns,
                           apr_hash_t *config,
                           apr_pool_t *pool)
{
  svn_config_t *cfg = config ? apr_hash_get(config,
                                            SVN_CONFIG_CATEGORY_CONFIG,
                                            APR_HASH_KEY_STRING) : NULL;
  const char *val;

  /* Check the Subversion run-time configuration for global ignores.
     If no configuration value exists, we fall back to our defaults. */
  svn_config_get(cfg, &val, SVN_CONFIG_SECTION_MISCELLANY,
                 SVN_CONFIG_OPTION_GLOBAL_IGNORES,
                 SVN_CONFIG_DEFAULT_GLOBAL_IGNORES);
  *patterns = apr_array_make(pool, 16, sizeof(const char *));

  /* Split the patterns on whitespace, and stuff them into *PATTERNS. */
  svn_cstring_split_append(*patterns, val, "\n\r\t\v ", FALSE, pool);
  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc_status2(svn_wc_status2_t **status,
               const char *path,
               svn_wc_adm_access_t *adm_access,
               apr_pool_t *pool)
{
  const svn_wc_entry_t *entry = NULL;
  const svn_wc_entry_t *parent_entry = NULL;

  if (adm_access)
    SVN_ERR(svn_wc_entry(&entry, path, adm_access, FALSE, pool));

  if (entry && ! svn_path_is_empty(path))
    {
      const char *parent_path = svn_path_dirname(path, pool);
      svn_wc_adm_access_t *parent_access;
      SVN_ERR(svn_wc__adm_retrieve_internal(&parent_access, adm_access,
                                            parent_path, pool));
      if (parent_access)
        SVN_ERR(svn_wc_entry(&parent_entry, parent_path, parent_access,
                             FALSE, pool));
    }

  return assemble_status(status, path, adm_access, entry, parent_entry,
                         svn_node_unknown, FALSE, /* bogus */
                         TRUE, FALSE, NULL, NULL, pool);
}


svn_error_t *
svn_wc_status(svn_wc_status_t **status,
              const char *path,
              svn_wc_adm_access_t *adm_access,
              apr_pool_t *pool)
{
  svn_wc_status2_t *stat2;

  SVN_ERR(svn_wc_status2(&stat2, path, adm_access, pool));
  *status = (svn_wc_status_t *) stat2;
  return SVN_NO_ERROR;
}



svn_wc_status2_t *
svn_wc_dup_status2(const svn_wc_status2_t *orig_stat,
                   apr_pool_t *pool)
{
  svn_wc_status2_t *new_stat = apr_palloc(pool, sizeof(*new_stat));

  /* Shallow copy all members. */
  *new_stat = *orig_stat;

  /* No go back and dup the deep item. */
  if (orig_stat->entry)
    new_stat->entry = svn_wc_entry_dup(orig_stat->entry, pool);

  if (orig_stat->repos_lock)
    new_stat->repos_lock = svn_lock_dup(orig_stat->repos_lock, pool);

  if (orig_stat->url)
    new_stat->url = apr_pstrdup(pool, orig_stat->url);

  if (orig_stat->ood_last_cmt_author)
    new_stat->ood_last_cmt_author
      = apr_pstrdup(pool, orig_stat->ood_last_cmt_author);

  if (orig_stat->tree_conflict)
    new_stat->tree_conflict
      = svn_wc__conflict_description_dup(orig_stat->tree_conflict, pool);

  /* Return the new hotness. */
  return new_stat;
}


svn_wc_status_t *
svn_wc_dup_status(const svn_wc_status_t *orig_stat,
                  apr_pool_t *pool)
{
  svn_wc_status_t *new_stat = apr_palloc(pool, sizeof(*new_stat));

  /* Shallow copy all members. */
  *new_stat = *orig_stat;

  /* No go back and dup the deep item. */
  if (orig_stat->entry)
    new_stat->entry = svn_wc_entry_dup(orig_stat->entry, pool);

  /* Return the new hotness. */
  return new_stat;
}

svn_error_t *
svn_wc_get_ignores(apr_array_header_t **patterns,
                   apr_hash_t *config,
                   svn_wc_adm_access_t *adm_access,
                   apr_pool_t *pool)
{
  apr_array_header_t *default_ignores;

  SVN_ERR(svn_wc_get_default_ignores(&default_ignores, config, pool));
  return collect_ignore_patterns(patterns, default_ignores, adm_access,
                                 pool);
}
