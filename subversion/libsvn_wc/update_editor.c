/*
 * update_editor.c :  main editor for checkouts and updates
 *
 * ====================================================================
 *    Licensed to the Subversion Corporation (SVN Corp.) under one
 *    or more contributor license agreements.  See the NOTICE file
 *    distributed with this work for additional information
 *    regarding copyright ownership.  The SVN Corp. licenses this file
 *    to you under the Apache License, Version 2.0 (the
 *    "License"); you may not use this file except in compliance
 *    with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing,
 *    software distributed under the License is distributed on an
 *    "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 *    KIND, either express or implied.  See the License for the
 *    specific language governing permissions and limitations
 *    under the License.
 * ====================================================================
 */



#include <stdlib.h>
#include <string.h>

#include <apr_pools.h>
#include <apr_hash.h>
#include <apr_md5.h>
#include <apr_tables.h>
#include <apr_file_io.h>
#include <apr_strings.h>

#include "svn_types.h"
#include "svn_pools.h"
#include "svn_delta.h"
#include "svn_string.h"
#include "svn_dirent_uri.h"
#include "svn_path.h"
#include "svn_xml.h"
#include "svn_error.h"
#include "svn_io.h"
#include "svn_private_config.h"
#include "svn_time.h"
#include "svn_config.h"
#include "svn_iter.h"

#include "wc.h"
#include "log.h"
#include "adm_files.h"
#include "adm_ops.h"
#include "entries.h"
#include "lock.h"
#include "props.h"
#include "translate.h"
#include "tree_conflicts.h"

#include "private/svn_wc_private.h"
#include "private/svn_debug.h"



/*
 * This code handles "checkout" and "update" and "switch".
 * A checkout is similar to an update that is only adding new items.
 *
 * The intended behaviour of "update" and "switch", focusing on the checks
 * to be made before applying a change, is:
 *
 *   For each incoming change:
 *     if target is already in conflict or obstructed:
 *       skip this change
 *     else
 *     if this action will cause a tree conflict:
 *       record the tree conflict
 *       skip this change
 *     else:
 *       make this change
 *
 * In more detail:
 *
 *   For each incoming change:
 *
 *   1.   if  # Incoming change is inside an item already in conflict:
 *    a.    tree/text/prop change to node beneath tree-conflicted dir
 *        then  # Skip all changes in this conflicted subtree [*1]:
 *          do not update the Base nor the Working
 *          notify "skipped because already in conflict" just once
 *            for the whole conflicted subtree
 *
 *        if  # Incoming change affects an item already in conflict:
 *    b.    tree/text/prop change to tree-conflicted dir/file, or
 *    c.    tree change to a text/prop-conflicted file/dir, or
 *    d.    text/prop change to a text/prop-conflicted file/dir [*2], or
 *    e.    tree change to a dir tree containing any conflicts,
 *        then  # Skip this change [*1]:
 *          do not update the Base nor the Working
 *          notify "skipped because already in conflict"
 *
 *   2.   if  # Incoming change affects an item that's "obstructed":
 *    a.    on-disk node kind doesn't match recorded Working node kind
 *            (including an absence/presence mis-match),
 *        then  # Skip this change [*1]:
 *          do not update the Base nor the Working
 *          notify "skipped because obstructed"
 *
 *   3.   if  # Incoming change raises a tree conflict:
 *    a.    tree/text/prop change to node beneath sched-delete dir, or
 *    b.    tree/text/prop change to sched-delete dir/file, or
 *    c.    text/prop change to tree-scheduled dir/file,
 *        then  # Skip this change:
 *          do not update the Base nor the Working [*3]
 *          notify "tree conflict"
 *
 *   4.   Apply the change:
 *          update the Base
 *          update the Working, possibly raising text/prop conflicts
 *          notify
 *
 * Notes:
 *
 *      "Tree change" here refers to an add or delete of the target node,
 *      including the add or delete part of a copy or move or rename.
 *
 * [*1] We should skip changes to an entire node, as the base revision number
 *      applies to the entire node. Not sure how this affects attempts to
 *      handle text and prop changes separately.
 *
 * [*2] Details of which combinations of property and text changes conflict
 *      are not specified here.
 *
 * [*3] For now, we skip the update, and require the user to:
 *        - Modify the WC to be compatible with the incoming change;
 *        - Mark the conflict as resolved;
 *        - Repeat the update.
 *      Ideally, it would be possible to resolve any conflict without
 *      repeating the update. To achieve this, we would have to store the
 *      necessary data at conflict detection time, and delay the update of
 *      the Base until the time of resolving.
 */


/*** batons ***/

struct edit_baton
{
  /* For updates, the "destination" of the edit is the ANCHOR (the
     directory at which the edit is rooted) plus the TARGET (the entry
     name of the actual thing we wish to update).  Target may be the empty
     string, but it is never NULL; for example, for checkouts and for updates
     that do not specify a target path, ANCHOR holds the whole path,
     and TARGET is empty. */
  /* ### ANCHOR is relative to CWD; TARGET is relative to ANCHOR? */
  const char *anchor;
  const char *target;

  /* Absolute variants of ANCHOR and TARGET */
  const char *anchor_abspath;
  const char *target_abspath;

  /* The DB handle for managing the working copy state.  */
  svn_wc__db_t *db;
  svn_wc_context_t *wc_ctx;

  /* ADM_ACCESS is an access baton that includes the ANCHOR directory */
  svn_wc_adm_access_t *adm_access;

  /* Array of file extension patterns to preserve as extensions in
     generated conflict files. */
  apr_array_header_t *ext_patterns;

  /* The revision we're targeting...or something like that.  This
     starts off as a pointer to the revision to which we are updating,
     or SVN_INVALID_REVNUM, but by the end of the edit, should be
     pointing to the final revision. */
  svn_revnum_t *target_revision;

  /* The requested depth of this edit. */
  svn_depth_t requested_depth;

  /* Is the requested depth merely an operational limitation, or is
     also the new sticky ambient depth of the update target? */
  svn_boolean_t depth_is_sticky;

  /* Need to know if the user wants us to overwrite the 'now' times on
     edited/added files with the last-commit-time. */
  svn_boolean_t use_commit_times;

  /* Was the root actually opened (was this a non-empty edit)? */
  svn_boolean_t root_opened;

  /* Was the update-target deleted?  This is a special situation. */
  svn_boolean_t target_deleted;

  /* Allow unversioned obstructions when adding a path. */
  svn_boolean_t allow_unver_obstructions;

  /* If this is a 'switch' operation, the target URL (### corresponding to
     the ANCHOR plus TARGET path?), else NULL. */
  const char *switch_url;

  /* The URL to the root of the repository, or NULL. */
  const char *repos;

  /* The UUID of the repos, or NULL. */
  const char *uuid;

  /* External diff3 to use for merges (can be null, in which case
     internal merge code is used). */
  const char *diff3_cmd;

  /* Externals handler */
  svn_wc_external_update_t external_func;
  void *external_baton;

  /* This editor sends back notifications as it edits. */
  svn_wc_notify_func2_t notify_func;
  void *notify_baton;

  /* This editor is normally wrapped in a cancellation editor anyway,
     so it doesn't bother to check for cancellation itself.  However,
     it needs a cancel_func and cancel_baton available to pass to
     long-running functions. */
  svn_cancel_func_t cancel_func;
  void *cancel_baton;

  /* This editor will invoke a interactive conflict-resolution
     callback, if available. */
  svn_wc_conflict_resolver_func_t conflict_func;
  void *conflict_baton;

  /* If the server sends add_file(copyfrom=...) and we don't have the
     copyfrom file in the working copy, we use this callback to fetch
     it directly from the repository. */
  svn_wc_get_file_t fetch_func;
  void *fetch_baton;

  /* Subtrees that were skipped during the edit, and therefore shouldn't
     have their revision/url info updated at the end.  If a path is a
     directory, its descendants will also be skipped.  The keys are absolute
     pathnames and the values unspecified. */
  apr_hash_t *skipped_trees;

  apr_pool_t *pool;
};


/* Record in the edit baton EB that LOCAL_ABSPATH's base version is not being
 * updated.
 *
 * Add to EB->skipped_trees a copy (allocated in EB->pool) of the string
 * LOCAL_ABSPATH.
 */
static svn_error_t *
remember_skipped_tree(struct edit_baton *eb, const char *local_abspath)
{
  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  apr_hash_set(eb->skipped_trees,  apr_pstrdup(eb->pool, local_abspath),
               APR_HASH_KEY_STRING, (void*)1);

  return SVN_NO_ERROR;
}

struct dir_baton
{
  /* The path to this directory. */
  const char *path;

  /* Basename of this directory. */
  const char *name;

  /* Absolute path of this directory */
  const char *local_abspath;

  /* The repository URL this directory will correspond to. */
  const char *new_URL;

  /* The revision of the directory before updating */
  svn_revnum_t old_revision;

  /* The global edit baton. */
  struct edit_baton *edit_baton;

  /* Baton for this directory's parent, or NULL if this is the root
     directory. */
  struct dir_baton *parent_baton;

  /* Set if updates to this directory are skipped */
  svn_boolean_t skip_this;

  /* Set if updates to all descendants of this directory are skipped */
  svn_boolean_t skip_descendants;

  /* Set if there was a previous notification for this directory */
  svn_boolean_t already_notified;

  /* Set on a node and its descendants when a node gets tree conflicted
     and descendants should be updated (not skipped), but these nodes should
     all be marked as deleted*/
  svn_boolean_t accept_deleted;

  /* Set iff this is a new directory that is not yet versioned and not
     yet in the parent's list of entries */
  svn_boolean_t added;

  /* Set if an unversioned dir of the same name already existed in
     this directory. */
  svn_boolean_t existed;

  /* Set if a dir of the same name already exists and is
     scheduled for addition without history. */
  svn_boolean_t add_existed;

  /* An array of svn_prop_t structures, representing all the property
     changes to be applied to this directory. */
  apr_array_header_t *propchanges;

  /* The bump information for this directory. */
  struct bump_dir_info *bump_info;

  /* The current log file number. */
  int log_number;

  /* The current log buffer. The content of this accumulator may be
     flushed and run at any time (in pool cleanup), so only append
     complete sets of operations to it; you may need to build up a
     buffer of operations and append it atomically with
     svn_stringbuf_appendstr. */
  svn_stringbuf_t *log_accum;

  /* The depth of the directory in the wc (or inferred if added).  Not
     used for filtering; we have a separate wrapping editor for that. */
  svn_depth_t ambient_depth;

  /* Was the directory marked as incomplete before the update?
     (In other words, are we resuming an interrupted update?) */
  svn_boolean_t was_incomplete;

  /* The pool in which this baton itself is allocated. */
  apr_pool_t *pool;
};


/* The bump information is tracked separately from the directory batons.
   This is a small structure kept in the edit pool, while the heavier
   directory baton is managed by the editor driver.

   In a postfix delta case, the directory batons are going to disappear.
   The files will refer to these structures, rather than the full
   directory baton.  */
struct bump_dir_info
{
  /* ptr to the bump information for the parent directory */
  struct bump_dir_info *parent;

  /* how many entries are referring to this bump information? */
  int ref_count;

  /* the absolute path of the directory to bump */
  const char *local_abspath;

  /* Set if this directory is skipped due to prop or tree conflicts.
     This does NOT mean that children are skipped. */
  svn_boolean_t skipped;
};


struct handler_baton
{
  svn_txdelta_window_handler_t apply_handler;
  void *apply_baton;
  apr_pool_t *pool;
  struct file_baton *fb;

  /* Where we are assembling the new file. */
  const char *work_path;

    /* The expected checksum of the text source or NULL if no base
     checksum is available */
  svn_checksum_t *expected_source_checksum;

  /* The calculated checksum of the text source or NULL if the acual
     checksum is not being calculated */
  svn_checksum_t *actual_source_checksum;

  /* The stream used to calculate the source checksum */
  svn_stream_t *source_checksum_stream;

  /* This is initialized to all zeroes when the baton is created, then
     populated with the MD5 digest of the resultant fulltext after the
     last window is handled by the handler returned from
     apply_textdelta(). */
  unsigned char digest[APR_MD5_DIGESTSIZE];
};


/* Return the url for LOCAL_ABSPATH of type KIND which can be unknown, 
 * allocated in RESULT_POOL, or null if unable to obtain a url.
 *
 * Use WC_CTX to retrieve information on LOCAL_ABSPATH, and do
 * all temporary allocation in SCRATCH_POOL.
 */
static const char *
get_entry_url(svn_wc_context_t *wc_ctx,
              const char *local_abspath,
              apr_pool_t *result_pool,
              apr_pool_t *scratch_pool)
{
  svn_error_t *err;
  const char *url;

  err = svn_wc__node_get_url(&url, wc_ctx, local_abspath, result_pool,
                                                          scratch_pool);

  if (err || !url)
    {
      svn_error_clear(err);
      return NULL;
    }

  return url;
}

/* Flush accumulated log entries to a log file on disk for DIR_BATON and
 * increase the log number of the dir baton.
 * Use POOL for temporary allocations. */
static svn_error_t *
flush_log(struct dir_baton *db, apr_pool_t *pool)
{
  if (! svn_stringbuf_isempty(db->log_accum))
    {
      SVN_ERR(svn_wc__write_log(db->local_abspath, db->log_number,
                                db->log_accum, pool));
      db->log_number++;
      svn_stringbuf_setempty(db->log_accum);
    }

  return SVN_NO_ERROR;
}

/* An APR pool cleanup handler.  This runs the log file for a
   directory baton. */
static apr_status_t
cleanup_dir_baton(void *dir_baton)
{
  struct dir_baton *db = dir_baton;
  svn_error_t *err;
  apr_status_t apr_err;
  svn_wc_adm_access_t *adm_access;
  apr_pool_t *pool = apr_pool_parent_get(db->pool);

  err = flush_log(db, pool);
  if (! err && db->log_number > 0)
    {
      err = svn_wc_adm_retrieve(&adm_access, db->edit_baton->adm_access,
                                db->path, pool);

      if (! err)
        {
          err = svn_wc__run_log(adm_access, pool);

          if (! err)
            return APR_SUCCESS;
        }
    }

  if (err)
    apr_err = err->apr_err;
  else
    apr_err = APR_SUCCESS;
  svn_error_clear(err);
  return apr_err;
}

/* An APR pool cleanup handler.  This is a child handler, it removes
   the mail pool handler. */
static apr_status_t
cleanup_dir_baton_child(void *dir_baton)
{
  struct dir_baton *db = dir_baton;
  apr_pool_cleanup_kill(db->pool, db, cleanup_dir_baton);
  return APR_SUCCESS;
}


/* Return a new dir_baton to represent NAME (a subdirectory of
   PARENT_BATON).  If PATH is NULL, this is the root directory of the
   edit. */
static svn_error_t *
make_dir_baton(struct dir_baton **d_p,
               const char *path,
               struct edit_baton *eb,
               struct dir_baton *pb,
               svn_boolean_t added,
               apr_pool_t *pool)
{
  struct dir_baton *d;
  struct bump_dir_info *bdi;

  SVN_ERR_ASSERT(path || (! pb));

  /* Okay, no easy out, so allocate and initialize a dir baton. */
  d = apr_pcalloc(pool, sizeof(*d));

  /* Construct the PATH and baseNAME of this directory. */
  if (path)
    {
      d->path = svn_dirent_join(eb->anchor, path, pool);
      d->name = svn_dirent_basename(path, pool);
      d->local_abspath = svn_dirent_join(pb->local_abspath, d->name, pool);
      d->accept_deleted = pb->accept_deleted;
    }
  else
    {
      d->path = apr_pstrdup(pool, eb->anchor);
      d->name = NULL;
      d->local_abspath = eb->anchor_abspath;
    }

  /* Figure out the new_URL for this directory. */
  if (eb->switch_url)
    {
      /* Switches are, shall we say, complex.  If this directory is
         the root directory (it has no parent), then it either gets
         the SWITCH_URL for its own (if it is both anchor and target)
         or the parent of the SWITCH_URL (if it is anchor, but there's
         another target). */
      if (! pb)
        {
          if (! *eb->target) /* anchor is also target */
            d->new_URL = apr_pstrdup(pool, eb->switch_url);
          else
            d->new_URL = svn_uri_dirname(eb->switch_url, pool);
        }
      /* Else this directory is *not* the root (has a parent).  If it
         is the target (there is a target, and this directory has no
         grandparent), then it gets the SWITCH_URL for its own.
         Otherwise, it gets a child of its parent's URL. */
      else
        {
          if (*eb->target && (! pb->parent_baton))
            d->new_URL = apr_pstrdup(pool, eb->switch_url);
          else
            d->new_URL = svn_path_url_add_component2(pb->new_URL,
                                                     d->name, pool);
        }
    }
  else  /* must be an update */
    {
      /* updates are the odds ones.  if we're updating a path already
         present on disk, we use its original URL.  otherwise, we'll
         telescope based on its parent's URL. */
      d->new_URL = get_entry_url(eb->wc_ctx, d->local_abspath, pool, pool);
      if ((! d->new_URL) && pb)
        d->new_URL = svn_path_url_add_component2(pb->new_URL, d->name, pool);
    }

  /* the bump information lives in the edit pool */
  bdi = apr_palloc(eb->pool, sizeof(*bdi));
  bdi->parent = pb ? pb->bump_info : NULL;
  bdi->ref_count = 1;
  bdi->local_abspath = apr_pstrdup(eb->pool, d->local_abspath);
  bdi->skipped = FALSE;

  /* the parent's bump info has one more referer */
  if (pb)
    ++bdi->parent->ref_count;

  d->edit_baton   = eb;
  d->parent_baton = pb;
  d->pool         = svn_pool_create(pool);
  d->propchanges  = apr_array_make(pool, 1, sizeof(svn_prop_t));
  d->added        = added;
  d->existed      = FALSE;
  d->add_existed  = FALSE;
  d->bump_info    = bdi;
  d->log_number   = 0;
  d->log_accum    = svn_stringbuf_create("", pool);
  d->old_revision = SVN_INVALID_REVNUM;

  /* The caller of this function needs to fill these in. */
  d->ambient_depth = svn_depth_unknown;
  d->was_incomplete = FALSE;

  apr_pool_cleanup_register(d->pool, d, cleanup_dir_baton,
                            cleanup_dir_baton_child);

  *d_p = d;
  return SVN_NO_ERROR;
}


/* Forward declarations. */
static svn_error_t *
do_entry_deletion(int *log_number,
                  struct edit_baton *eb,
                  const char *local_abspath,
                  const char *their_url,
                  svn_boolean_t accept_deleted,
                  apr_pool_t *pool);

static svn_error_t *
already_in_a_tree_conflict(svn_boolean_t *conflicted,
                           svn_wc__db_t *db,
                           const char *local_abspath,
                           apr_pool_t *scratch_pool);


/* Helper for maybe_bump_dir_info():

   In a single atomic action, (1) remove any 'deleted' entries from a
   directory, (2) remove any 'absent' entries whose revision numbers
   are different from the parent's new target revision, (3) remove any
   'missing' dir entries, and (4) remove the directory's 'incomplete'
   flag. */
static svn_error_t *
complete_directory(struct edit_baton *eb,
                   const char *local_abspath,
                   svn_boolean_t is_root_dir,
                   apr_pool_t *pool)
{
  const apr_array_header_t *children;
  int i;
  apr_pool_t *iterpool;
  svn_wc_entry_t tmp_entry;

  /* If this is the root directory and there is a target, we can't
     mark this directory complete. */
  if (is_root_dir && *eb->target)
    {
      /* Before we can finish, we may need to clear the exclude flag for
         target. Also give a chance to the target that is explicitly pulled
         in. */
      svn_error_t *err;
      const svn_wc_entry_t *target_entry;

      SVN_ERR_ASSERT(strcmp(local_abspath, eb->anchor_abspath) == 0);

      err = svn_wc__get_entry(&target_entry, eb->db, eb->target_abspath, TRUE,
                              svn_node_dir, TRUE, pool, pool);
      if (err)
        {
          if (err->apr_err != SVN_ERR_NODE_UNEXPECTED_KIND)
            return svn_error_return(err);
          svn_error_clear(err);

          /* No problem if it is actually a file. The depth won't be
             svn_depth_exclude, so we'll do nothing.  */
        }
      if (target_entry && target_entry->depth == svn_depth_exclude)
        {
          /* There is a small chance that the target is gone in the
             repository.  If so, we should get rid of the entry
             (and thus get rid of the exclude flag) now. */

          if (target_entry->kind == svn_node_dir &&
              svn_wc__adm_missing(eb->db, eb->target_abspath, pool))
            {
              int log_number = 0;
              /* Still passing NULL for THEIR_URL. A case where THEIR_URL
               * is needed in this call is rare or even non-existant.
               * ### TODO: Construct a proper THEIR_URL anyway. See also
               * NULL handling code in do_entry_deletion(). */
              SVN_ERR(do_entry_deletion(&log_number, eb, eb->target_abspath,
                                        NULL, FALSE, pool));
            }
          else
            {
              SVN_ERR(svn_wc__set_depth(eb->db, eb->target_abspath,
                                        svn_depth_infinity, pool));
            }
        }

      return SVN_NO_ERROR;
    }

  /* Mark THIS_DIR complete. */
  tmp_entry.incomplete = FALSE;
  SVN_ERR(svn_wc__entry_modify2(eb->db, local_abspath, svn_node_dir, FALSE,
                                &tmp_entry, SVN_WC__ENTRY_MODIFY_INCOMPLETE,
                                pool));

  if (eb->depth_is_sticky)
    {
      svn_depth_t depth;

      SVN_ERR(svn_wc__db_read_info(NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                                   NULL, NULL, NULL, &depth, NULL, NULL, NULL,
                                   NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                                   NULL, NULL, NULL,
                                   eb->db, local_abspath, pool, pool));


      if (depth != eb->requested_depth)
      {
        /* After a depth upgrade the entry must reflect the new depth.
           Upgrading to infinity changes the depth of *all* directories,
           upgrading to something else only changes the target. */

        if ((eb->requested_depth == svn_depth_infinity)
             || ((strcmp(local_abspath, eb->target_abspath) == 0)
                 && eb->requested_depth > depth))
          {
            SVN_ERR(svn_wc__set_depth(eb->db, local_abspath,
                                      eb->requested_depth, pool));
          }
      }
    }

  /* Remove any deleted or missing entries. */
  iterpool = svn_pool_create(pool);

  SVN_ERR(svn_wc__db_read_children(&children, eb->db, local_abspath, pool,
                                   iterpool));
  for (i = 0; i < children->nelts; i++)
    {
      const svn_wc_entry_t *this_entry;
      const char *name = APR_ARRAY_IDX(children, i, const char *);
      const char *node_abspath;
      svn_error_t *err;

      svn_pool_clear(iterpool);

      node_abspath = svn_dirent_join(local_abspath, name, iterpool);

      /* Try to read the entry of a file */
      err = svn_wc__get_entry(&this_entry, eb->db, node_abspath, FALSE,
                              svn_node_file, FALSE, iterpool, iterpool);

      /* Or the parent entry of a directory */
      if (err && ((err->apr_err == SVN_ERR_NODE_UNEXPECTED_KIND) ||
                  (err->apr_err == SVN_ERR_WC_PATH_NOT_FOUND)))
        {
          svn_error_clear(err);
          SVN_ERR(svn_wc__get_entry(&this_entry, eb->db, node_abspath,
                                    FALSE, svn_node_dir, TRUE, iterpool,
                                    iterpool));
        }
      else
        SVN_ERR(err);

      /* Any entry still marked as deleted (and not schedule add) can now
         be removed -- if it wasn't undeleted by the update, then it
         shouldn't stay in the updated working set.  Schedule add items
         should remain.
      */
      if (this_entry->deleted)
        {
          if (this_entry->schedule != svn_wc_schedule_add)
              SVN_ERR(svn_wc__entry_remove(eb->db, node_abspath, iterpool));
          else
            {
              tmp_entry.deleted = FALSE;

              SVN_ERR(svn_wc__entry_modify2(eb->db, node_abspath,
                                            this_entry->kind,
                                            (this_entry->kind == svn_node_dir),
                                            &tmp_entry,
                                            SVN_WC__ENTRY_MODIFY_DELETED,
                                            iterpool));
            }
        }
      /* An absent entry might have been reconfirmed as absent, and the way
         we can tell is by looking at its revision number: a revision
         number different from the target revision of the update means the
         update never mentioned the item, so the entry should be
         removed. */
      else if (this_entry->absent
               && (this_entry->revision != *(eb->target_revision)))
        {
          SVN_ERR(svn_wc__entry_remove(eb->db, node_abspath, iterpool));
        }
      else if (this_entry->kind == svn_node_dir)
        {
          if (this_entry->depth == svn_depth_exclude)
            {
              /* Clear the exclude flag if it is pulled in again. */
              if (eb->depth_is_sticky
                  && eb->requested_depth >= svn_depth_immediates)
                {
                  SVN_ERR(svn_wc__set_depth(eb->db, node_abspath,
                                            svn_depth_infinity, iterpool));
                }
            }
          else if ((svn_wc__adm_missing(eb->db, node_abspath, iterpool))
                   && (!this_entry->absent)
                   && (this_entry->schedule != svn_wc_schedule_add))
            {
              SVN_ERR(svn_wc__entry_remove(eb->db, node_abspath, iterpool));

              if (eb->notify_func)
                {
                  svn_wc_notify_t *notify
                    = svn_wc_create_notify(node_abspath,
                                           svn_wc_notify_update_delete,
                                           iterpool);
                  notify->kind = this_entry->kind;
                  (* eb->notify_func)(eb->notify_baton, notify, iterpool);
                }
            }
        }
    }

  svn_pool_destroy(iterpool);
  return SVN_NO_ERROR;
}



/* Decrement the bump_dir_info's reference count. If it hits zero,
   then this directory is "done". This means it is safe to remove the
   'incomplete' flag attached to the THIS_DIR entry.

   In addition, when the directory is "done", we loop onto the parent's
   bump information to possibly mark it as done, too.
*/
static svn_error_t *
maybe_bump_dir_info(struct edit_baton *eb,
                    struct bump_dir_info *bdi,
                    apr_pool_t *pool)
{
  /* Keep moving up the tree of directories until we run out of parents,
     or a directory is not yet "done".  */
  for ( ; bdi != NULL; bdi = bdi->parent)
    {
      if (--bdi->ref_count > 0)
        return SVN_NO_ERROR;    /* directory isn't done yet */

      /* The refcount is zero, so we remove any 'dead' entries from
         the directory and mark it 'complete'.  */
      if (! bdi->skipped)
        SVN_ERR(complete_directory(eb, bdi->local_abspath,
                                   bdi->parent == NULL, pool));
    }
  /* we exited the for loop because there are no more parents */

  return SVN_NO_ERROR;
}

struct file_baton
{
  /* Pool specific to this file_baton. */
  apr_pool_t *pool;

  /* Name of this file (its entry in the directory). */
  const char *name;

  /* Path to this file, either abs or relative to the change-root. */
  const char *path;

  /* Absolute path to this file */
  const char *local_abspath;

  /* The repository URL this file will correspond to. */
  const char *new_URL;

  /* The revision of the file before updating */
  svn_revnum_t old_revision;

  /* The global edit baton. */
  struct edit_baton *edit_baton;

  /* The parent directory of this file. */
  struct dir_baton *dir_baton;

  /* Set if updates to this directory are skipped */
  svn_boolean_t skip_this;

  /* Set if there was a previous notification  */
  svn_boolean_t already_notified;

  /* Set if this file is new. */
  svn_boolean_t added;

  /* Set if this file is new with history. */
  svn_boolean_t added_with_history;

  /* Set if an unversioned file of the same name already existed in
     this directory. */
  svn_boolean_t existed;

  /* Set if a file of the same name already exists and is
     scheduled for addition without history. */
  svn_boolean_t add_existed;

  /* Set if this file is locally deleted or is being added
     within a locally deleted tree. */
  svn_boolean_t deleted;

  /* The path to the current text base, if any.
     This gets set if there are file content changes. */
  const char *text_base_path;

  /* The path to the incoming text base (that is, to a text-base-file-
     in-progress in the tmp area).  This gets set if there are file
     content changes. */
  const char *new_text_base_path;

  /* The checksum for the file located at NEW_TEXT_BASE_PATH. */
  svn_checksum_t *actual_checksum;

  /* If this file was added with history, this is the path to a copy
     of the text base of the copyfrom file (in the temporary area). */
  const char *copied_text_base;

  /* If this file was added with history, this is the checksum of the
     text base (see copied_text_base). May be NULL if unknown. */
  svn_checksum_t *copied_base_checksum;

  /* If this file was added with history, and the copyfrom had local
     mods, this is the path to a copy of the user's version with local
     mods (in the temporary area). */
  const char *copied_working_text;

  /* If this file was added with history, this hash contains the base
     properties of the copied file. */
  apr_hash_t *copied_base_props;

  /* If this file was added with history, this hash contains the working
     properties of the copied file. */
  apr_hash_t *copied_working_props;

  /* Set if we've received an apply_textdelta for this file. */
  svn_boolean_t received_textdelta;

  /* An array of svn_prop_t structures, representing all the property
     changes to be applied to this file.  Once a file baton is
     initialized, this is never NULL, but it may have zero elements.  */
  apr_array_header_t *propchanges;

  /* The last-changed-date of the file.  This is actually a property
     that comes through as an 'entry prop', and will be used to set
     the working file's timestamp if it's added.  */
  const char *last_changed_date;

  /* Bump information for the directory this file lives in */
  struct bump_dir_info *bump_info;
};


/* Make a new file baton in the provided POOL, with PB as the parent baton.
 * PATH is relative to the root of the edit. ADDING tells whether this file
 * is being added. */
static svn_error_t *
make_file_baton(struct file_baton **f_p,
                struct dir_baton *pb,
                const char *path,
                svn_boolean_t adding,
                apr_pool_t *pool)
{
  struct file_baton *f = apr_pcalloc(pool, sizeof(*f));

  SVN_ERR_ASSERT(path);

  /* Make the file's on-disk name. */
  f->path = svn_dirent_join(pb->edit_baton->anchor, path, pool);
  f->name = svn_dirent_basename(path, pool);
  f->old_revision = SVN_INVALID_REVNUM;
  f->local_abspath = svn_dirent_join(pb->local_abspath, f->name, pool);

  /* Figure out the new_URL for this file. */
  if (pb->edit_baton->switch_url)
    {
      f->new_URL = svn_path_url_add_component2(pb->new_URL, f->name, pool);
    }
  else
    {
      f->new_URL = get_entry_url(pb->edit_baton->wc_ctx,
                                 svn_dirent_join(pb->local_abspath,
                                                 f->name, pool),
                                 pool, pool);
    }

  f->pool              = pool;
  f->edit_baton        = pb->edit_baton;
  f->propchanges       = apr_array_make(pool, 1, sizeof(svn_prop_t));
  f->bump_info         = pb->bump_info;
  f->added             = adding;
  f->existed           = FALSE;
  f->add_existed       = FALSE;
  f->deleted           = FALSE;
  f->dir_baton         = pb;

  /* No need to initialize f->digest, since we used pcalloc(). */

  /* the directory's bump info has one more referer now */
  ++f->bump_info->ref_count;

  *f_p = f;
  return SVN_NO_ERROR;
}



/*** Helpers for the editor callbacks. ***/

static svn_error_t *
window_handler(svn_txdelta_window_t *window, void *baton)
{
  struct handler_baton *hb = baton;
  struct file_baton *fb = hb->fb;
  svn_error_t *err;

  /* Apply this window.  We may be done at that point.  */
  err = hb->apply_handler(window, hb->apply_baton);
  if (window != NULL && !err)
    return SVN_NO_ERROR;

  if (hb->expected_source_checksum)
    {
      /* Close the stream to calculate the final checksum */
      svn_error_t *err2 = svn_stream_close(hb->source_checksum_stream);

      if (!err2 && !svn_checksum_match(hb->expected_source_checksum,
                                       hb->actual_source_checksum))
        {
          err = svn_error_createf(SVN_ERR_WC_CORRUPT_TEXT_BASE, err,
                    _("Checksum mismatch while updating '%s':\n"
                      "   expected:  %s\n"
                      "     actual:  %s\n"),
                    svn_dirent_local_style(fb->path, hb->pool),
                    svn_checksum_to_cstring(hb->expected_source_checksum,
                                            hb->pool),
                    svn_checksum_to_cstring(hb->actual_source_checksum,
                                            hb->pool));
        }
  
      err = svn_error_compose_create(err, err2);
    }

  if (err)
    {
      /* We failed to apply the delta; clean up the temporary file.  */
      svn_error_clear(svn_io_remove_file2(hb->work_path, TRUE, hb->pool));
    }
  else
    {
      /* Tell the file baton about the new text base. */
      fb->new_text_base_path = apr_pstrdup(fb->pool, hb->work_path);

      /* ... and its checksum. */
      fb->actual_checksum =
        svn_checksum__from_digest(hb->digest, svn_checksum_md5, fb->pool);
    }

  svn_pool_destroy(hb->pool);

  return err;
}


/* Prepare directory for dir_baton DB for updating or checking out.
 * Give it depth DEPTH.
 *
 * If the path already exists, but is not a working copy for
 * ANCESTOR_URL and ANCESTOR_REVISION, then an error will be returned.
 */
static svn_error_t *
prep_directory(struct dir_baton *db,
               const char *ancestor_url,
               svn_revnum_t ancestor_revision,
               apr_pool_t *pool)
{
  const char *repos;
  const char *dir_abspath;

  dir_abspath = db->local_abspath;

  /* Make sure the directory exists. */
  SVN_ERR(svn_wc__ensure_directory(dir_abspath, pool));

  /* Use the repository root of the anchor, but only if it actually is an
     ancestor of the URL of this directory. */
  if (db->edit_baton->repos
      && svn_uri_is_ancestor(db->edit_baton->repos, ancestor_url))
    repos = db->edit_baton->repos;
  else
    repos = NULL;

  /* Make sure it's the right working copy, either by creating it so,
     or by checking that it is so already. */
  SVN_ERR(svn_wc__internal_ensure_adm(db->edit_baton->db, dir_abspath,
                                      db->edit_baton->uuid, ancestor_url,
                                      repos, ancestor_revision,
                                      db->ambient_depth, pool));

  if (NULL == svn_wc__adm_retrieve_internal2(db->edit_baton->db, dir_abspath,
                                             pool))
    {
      svn_wc_adm_access_t *adm_access;
      apr_pool_t *adm_access_pool;
      const char *rel_path;

      SVN_ERR(svn_wc__temp_get_relpath(&rel_path, db->edit_baton->db,
                                       dir_abspath, pool, pool));

      adm_access_pool = svn_wc_adm_access_pool(db->edit_baton->adm_access);

      SVN_ERR(svn_wc__adm_open_in_context(&adm_access, db->edit_baton->wc_ctx,
                                          rel_path, TRUE, 0,
                                          db->edit_baton->cancel_func,
                                          db->edit_baton->cancel_baton,
                                          adm_access_pool));
    }

  return SVN_NO_ERROR;
}


/* Accumulate tags in LOG_ACCUM (associated with ADM_ABSPATH) to set
   ENTRY_PROPS for PATH.
   ENTRY_PROPS is an array of svn_prop_t* entry props.
   If ENTRY_PROPS contains the removal of a lock token, all entryprops
   related to a lock will be removed and LOCK_STATE, if non-NULL, will be
   set to svn_wc_notify_lock_state_unlocked.  Else, LOCK_STATE, if non-NULL
   will be set to svn_wc_lock_state_unchanged. */
static svn_error_t *
accumulate_entry_props(svn_stringbuf_t *log_accum,
                       const char *adm_abspath,
                       svn_wc_notify_lock_state_t *lock_state,
                       const char *path,
                       apr_array_header_t *entry_props,
                       apr_pool_t *pool)
{
  int i;
  svn_wc_entry_t tmp_entry;
  apr_uint64_t flags = 0;

  if (lock_state)
    *lock_state = svn_wc_notify_lock_state_unchanged;

  for (i = 0; i < entry_props->nelts; ++i)
    {
      const svn_prop_t *prop = &APR_ARRAY_IDX(entry_props, i, svn_prop_t);
      const char *val;

      /* The removal of the lock-token entryprop means that the lock was
         defunct. */
      if (! strcmp(prop->name, SVN_PROP_ENTRY_LOCK_TOKEN))
        {
          SVN_ERR(svn_wc__loggy_delete_lock(&log_accum, adm_abspath,
                                            path, pool, pool));

          if (lock_state)
            *lock_state = svn_wc_notify_lock_state_unlocked;
          continue;
        }
      /* A prop value of NULL means the information was not
         available.  We don't remove this field from the entries
         file; we have convention just leave it empty.  So let's
         just skip those entry props that have no values. */
      if (! prop->value)
        continue;

      val = prop->value->data;

      if (! strcmp(prop->name, SVN_PROP_ENTRY_LAST_AUTHOR))
        {
          flags |= SVN_WC__ENTRY_MODIFY_CMT_AUTHOR;
          tmp_entry.cmt_author = val;
        }
      else if (! strcmp(prop->name, SVN_PROP_ENTRY_COMMITTED_REV))
        {
          flags |= SVN_WC__ENTRY_MODIFY_CMT_REV;
          tmp_entry.cmt_rev = SVN_STR_TO_REV(val);
        }
      else if (! strcmp(prop->name, SVN_PROP_ENTRY_COMMITTED_DATE))
        {
          flags |= SVN_WC__ENTRY_MODIFY_CMT_DATE;
          SVN_ERR(svn_time_from_cstring(&tmp_entry.cmt_date, val, pool));
        }
      /* Starting with Subversion 1.7 we ignore the SVN_PROP_ENTRY_UUID
         property here. */
    }

  if (flags)
    SVN_ERR(svn_wc__loggy_entry_modify(&log_accum, adm_abspath,
                                       path, &tmp_entry, flags, pool, pool));

  return SVN_NO_ERROR;
}


/* Check that when ADD_PATH is joined to BASE_PATH, the resulting path
 * is still under BASE_PATH in the local filesystem.  If not, return
 * SVN_ERR_WC_OBSTRUCTED_UPDATE; else return success.
 *
 * This is to prevent the situation where the repository contains,
 * say, "..\nastyfile".  Although that's perfectly legal on some
 * systems, when checked out onto Win32 it would cause "nastyfile" to
 * be created in the parent of the current edit directory.
 *
 * (http://cve.mitre.org/cgi-bin/cvename.cgi?name=2007-3846)
 */
static svn_error_t *
check_path_under_root(const char *base_path,
                      const char *add_path,
                      apr_pool_t *pool)
{
  char *full_path;

  if (! svn_dirent_is_under_root(&full_path, base_path, add_path, pool))
    {
      return svn_error_createf(
          SVN_ERR_WC_OBSTRUCTED_UPDATE, NULL,
         _("Path '%s' is not in the working copy"),
         /* Not using full_path here because it might be NULL or
            undefined, since apr_filepath_merge() returned error.
            (Pity we can't pass NULL for &full_path in the first place,
            but the APR docs don't bless that.) */
         svn_dirent_local_style(svn_dirent_join(base_path, add_path, pool),
                                pool));
    }

  return SVN_NO_ERROR;
}


/*** The callbacks we'll plug into an svn_delta_editor_t structure. ***/

/* An svn_delta_editor_t function. */
static svn_error_t *
set_target_revision(void *edit_baton,
                    svn_revnum_t target_revision,
                    apr_pool_t *pool)
{
  struct edit_baton *eb = edit_baton;

  /* Stashing a target_revision in the baton */
  *(eb->target_revision) = target_revision;
  return SVN_NO_ERROR;
}


/* An svn_delta_editor_t function. */
static svn_error_t *
open_root(void *edit_baton,
          svn_revnum_t base_revision, /* This is ignored in co */
          apr_pool_t *pool,
          void **dir_baton)
{
  struct edit_baton *eb = edit_baton;
  struct dir_baton *db;
  svn_boolean_t already_conflicted;
  svn_wc__db_kind_t kind;
  svn_error_t *err;

  /* Note that something interesting is actually happening in this
     edit run. */
  eb->root_opened = TRUE;

  SVN_ERR(make_dir_baton(&db, NULL, eb, NULL, FALSE, pool));
  *dir_baton = db;

  SVN_ERR(svn_wc__db_read_kind(&kind, eb->db, db->local_abspath, TRUE, pool));

  if (kind == svn_wc__db_kind_dir)
    {
      err = already_in_a_tree_conflict(&already_conflicted, eb->db,
                                       db->local_abspath, pool);

      if (err && err->apr_err == SVN_ERR_WC_MISSING)
        {
          svn_error_clear(err);
          already_conflicted = FALSE;
        }
      else
        SVN_ERR(err);
    }
  else
    already_conflicted = FALSE;

  if (already_conflicted)
    {
      db->skip_this = TRUE;
      db->skip_descendants = TRUE;
      db->already_notified = TRUE;
      db->bump_info->skipped = TRUE;

      /* Notify that we skipped the target, while we actually skipped
         the anchor */
      if (eb->notify_func)
        eb->notify_func(eb->notify_baton,
                        svn_wc_create_notify(eb->target_abspath,
                                             svn_wc_notify_skip,
                                             pool),
                        pool);

      return SVN_NO_ERROR;
    }

  if (! *eb->target)
    {
      /* For an update with a NULL target, this is equivalent to open_dir(): */
      svn_wc_entry_t tmp_entry;
      svn_depth_t depth;
      svn_wc__db_status_t status;
      apr_uint64_t flags = SVN_WC__ENTRY_MODIFY_REVISION |
        SVN_WC__ENTRY_MODIFY_URL | SVN_WC__ENTRY_MODIFY_INCOMPLETE;

      /* Read the depth from the entry. */
      SVN_ERR(svn_wc__db_read_info(&status, NULL, NULL, NULL, NULL, NULL, NULL,
                                   NULL, NULL, NULL, &depth, NULL, NULL, NULL,
                                   NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                                   NULL, NULL, NULL,
                                   eb->db, db->local_abspath, pool, pool));
      db->ambient_depth = depth;
      db->was_incomplete = (status == svn_wc__db_status_incomplete);

      /* ### TODO: Skip if inside a conflicted tree. */

      /* Mark directory as being at target_revision, but incomplete. */
      tmp_entry.revision = *(eb->target_revision);
      tmp_entry.url = db->new_URL;
      tmp_entry.incomplete = TRUE;
      SVN_ERR(svn_wc__entry_modify2(eb->db, db->local_abspath, svn_node_dir,
                                    FALSE,
                                    &tmp_entry, flags,
                                    pool));
    }

  return SVN_NO_ERROR;
}


/* Helper for delete_entry() and do_entry_deletion().

   If the error chain ERR contains evidence that a local mod was left
   (an SVN_ERR_WC_LEFT_LOCAL_MOD error), clear ERR.  Otherwise, return ERR.
*/
static svn_error_t *
leftmod_error_chain(svn_error_t *err)
{
  svn_error_t *tmp_err;

  if (! err)
    return SVN_NO_ERROR;

  /* Advance TMP_ERR to the part of the error chain that reveals that
     a local mod was left, or to the NULL end of the chain. */
  for (tmp_err = err; tmp_err; tmp_err = tmp_err->child)
    if (tmp_err->apr_err == SVN_ERR_WC_LEFT_LOCAL_MOD)
      {
        /* We just found a "left a local mod" error, so tolerate it
           and clear the whole error. In that case we continue with
           modified files left on the disk. */
        svn_error_clear(err);
        return SVN_NO_ERROR;
      }

  /* Otherwise, we just return our top-most error. */
  return err;
}


/* ===================================================================== */
/* Checking for local modifications. */

/* Set *MODIFIED to true iff the item described by (LOCAL_ABSPATH, KIND)
 * has local modifications. For a file, this means text mods or property mods.
 * For a directory, this means property mods.
 *
 * Use SCRATCH_POOL for temporary allocations.
 */
static svn_error_t *
entry_has_local_mods(svn_boolean_t *modified,
                     svn_wc__db_t *db,
                     const char *local_abspath,
                     svn_node_kind_t kind,
                     apr_pool_t *scratch_pool)
{
  svn_boolean_t text_modified;
  svn_boolean_t props_modified;

  /* Check for text modifications */
  if (kind == svn_node_file)
    SVN_ERR(svn_wc__text_modified_internal_p(&text_modified, db, local_abspath,
                                             FALSE, TRUE, scratch_pool));
  else
    text_modified = FALSE;

  /* Check for property modifications */
  SVN_ERR(svn_wc__props_modified(&props_modified, db, local_abspath,
                                 scratch_pool));

  *modified = (text_modified || props_modified);

  return SVN_NO_ERROR;
}

/* A baton for use with modcheck_found_entry(). */
typedef struct modcheck_baton_t {
  svn_wc__db_t *db;         /* wc_db to access nodes */
  svn_boolean_t found_mod;  /* whether a modification has been found */
  svn_boolean_t all_edits_are_deletes;  /* If all the mods found, if any,
                                          were deletes.  If FOUND_MOD is false
                                          then this field has no meaning. */
} modcheck_baton_t;

static svn_error_t *
modcheck_found_node(const char *local_abspath,
                    void *walk_baton,
                    apr_pool_t *scratch_pool)
{
  modcheck_baton_t *baton = walk_baton;
  svn_wc__db_kind_t kind;
  svn_wc__db_status_t status;
  svn_boolean_t modified;

  SVN_ERR(svn_wc__db_read_info(&status, &kind, NULL, NULL, NULL, NULL, NULL,
                               NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                               NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                               NULL, NULL, NULL,
                               baton->db, local_abspath, scratch_pool,
                               scratch_pool));

  if (status != svn_wc__db_status_normal)
    modified = TRUE;
  else
    SVN_ERR(entry_has_local_mods(&modified, baton->db, local_abspath,
                                 kind == svn_wc__db_kind_file
                                   ? svn_node_file : svn_node_dir,
                                 scratch_pool));

  if (modified)
    {
      baton->found_mod = TRUE;
      if (status != svn_wc__db_status_deleted)
        baton->all_edits_are_deletes = FALSE;
    }

  return SVN_NO_ERROR;
}


/* Set *MODIFIED to true iff there are any local modifications within the
 * tree rooted at PATH whose admin access baton is ADM_ACCESS. If *MODIFIED
 * is set to true and all the local modifications were deletes then set
 * *ALL_EDITS_ARE_DELETES to true, set it to false otherwise.  PATH may be a
 * file or a directory. */
static svn_error_t *
tree_has_local_mods(svn_boolean_t *modified,
                    svn_boolean_t *all_edits_are_deletes,
                    svn_wc__db_t *db,
                    const char *local_abspath,
                    svn_cancel_func_t cancel_func,
                    void *cancel_baton,
                    apr_pool_t *pool)
{
  modcheck_baton_t modcheck_baton = { NULL, FALSE, TRUE };

  modcheck_baton.db = db;

  /* Walk the WC tree to its full depth, looking for any local modifications.
   * If it's a "sparse" directory, that's OK: there can be no local mods in
   * the pieces that aren't present in the WC. */

  SVN_ERR(svn_wc__internal_walk_children(db, local_abspath,
                                         FALSE /* show_hidden */,
                                         modcheck_found_node, &modcheck_baton,
                                         svn_depth_infinity, cancel_func,
                                         cancel_baton, pool));

  *modified = modcheck_baton.found_mod;
  *all_edits_are_deletes = modcheck_baton.all_edits_are_deletes;
  return SVN_NO_ERROR;
}

/* Check whether the incoming change ACTION on FULL_PATH would conflict with
 * LOCAL_ABSPATH's scheduled change. If so, then raise a tree conflict with
 * LOCAL_ABSPATH as the victim, by appending log actions to LOG_ACCUM.
 *
 * The edit baton EB gives information including whether the operation is
 * an update or a switch.
 *
 * If PCONFLICT is not null, set *PCONFLICT to the conflict description if
 * there is one or else to null.
 *
 * THEIR_NODE_KIND is the node kind reflected by the incoming edit
 * function. E.g. dir_opened() should pass svn_node_dir, etc.
 * In some cases of delete, svn_node_none may be used here.
 *
 * THEIR_URL is the involved node's URL on the source-right side, the
 * side that the target should become after the update. Simply put,
 * that's the URL obtained from the node's dir_baton->new_URL or
 * file_baton->new_URL (but it's more complex for a delete).
 *
 * ACCEPT_DELETED is true if one of the ancestors got tree conflicted, but
 * the operation continued updating a deleted base tree.
 *
 * Tree conflict use cases are described in issue #2282 and in
 * notest/tree-conflicts/detection.txt.
 */
static svn_error_t *
check_tree_conflict(svn_wc_conflict_description2_t **pconflict,
                    struct edit_baton *eb,
                    const char *local_abspath,
                    svn_stringbuf_t *log_accum,
                    svn_wc_conflict_action_t action,
                    svn_node_kind_t their_node_kind,
                    const char *their_url,
                    svn_boolean_t accept_deleted,
                    apr_pool_t *pool)
{
  svn_wc_conflict_reason_t reason = (svn_wc_conflict_reason_t)(-1);
  svn_boolean_t all_mods_are_deletes = FALSE;
  const svn_wc_entry_t *entry;
  svn_error_t *err;

  err = svn_wc__get_entry(&entry, eb->db, local_abspath, TRUE,
                          svn_node_unknown, FALSE, pool, pool);

  if (err && err->apr_err == SVN_ERR_NODE_UNEXPECTED_KIND)
    svn_error_clear(err);
  else
    SVN_ERR(err);

  if (entry)
    {
      svn_boolean_t hidden;
      SVN_ERR(svn_wc__db_node_hidden(&hidden, eb->db, local_abspath, pool));

      if (hidden)
        entry = NULL;
    }

  switch (action)
    {
    case svn_wc_conflict_action_edit:
      /* Use case 1: Modifying a locally-deleted item.
         If LOCAL_ABSPATH is an incoming leaf edit within a local
         tree deletion then we will already have recorded a tree
         conflict on the locally deleted parent tree.  No need
         to record a conflict within the conflict. */
      if ((entry->schedule == svn_wc_schedule_delete
           || entry->schedule == svn_wc_schedule_replace)
          && !accept_deleted)
        reason = entry->schedule == svn_wc_schedule_delete
                                    ? svn_wc_conflict_reason_deleted
                                    : svn_wc_conflict_reason_replaced;
      break;

    case svn_wc_conflict_action_add:
      /* Use case "3.5": Adding a locally-added item.
       *
       * When checking out a file-external, add_file() is called twice:
       * 1.) In the main update, a minimal entry is created.
       * 2.) In the external update, the file is added properly.
       * Don't raise a tree conflict the second time! */
      if (entry && !entry->file_external_path)
        reason = svn_wc_conflict_reason_added;
      break;

    case svn_wc_conflict_action_delete:
    case svn_wc_conflict_action_replace:
      /* Use case 3: Deleting a locally-deleted item. */
      if (entry->schedule == svn_wc_schedule_delete
          || entry->schedule == svn_wc_schedule_replace)
        {
          /* If LOCAL_ABSPATH is an incoming leaf deletion within a local
             tree deletion then we will already have recorded a tree
             conflict on the locally deleted parent tree.  No need
             to record a conflict within the conflict. */
          if (!accept_deleted)
            reason = entry->schedule == svn_wc_schedule_delete
                                        ? svn_wc_conflict_reason_deleted
                                        : svn_wc_conflict_reason_replaced;
        }
      else
        {
          svn_boolean_t modified = FALSE;

          /* Use case 2: Deleting a locally-modified item. */
          if (entry->kind == svn_node_file)
            {
              if (entry->schedule != svn_wc_schedule_normal)
                modified = TRUE;
              else
                SVN_ERR(entry_has_local_mods(&modified, eb->db, local_abspath,
                                             entry->kind, pool));
              if (entry->schedule == svn_wc_schedule_delete)
                all_mods_are_deletes = TRUE;
            }
          else if (entry->kind == svn_node_dir)
            {
              /* We must detect deep modifications in a directory tree,
               * but the update editor will not visit the subdirectories
               * of a directory that it wants to delete.  Therefore, we
               * need to start a separate crawl here. */

              if (!svn_wc__adm_missing(eb->db, local_abspath, pool))
                SVN_ERR(tree_has_local_mods(&modified, &all_mods_are_deletes,
                                            eb->db, local_abspath,
                                            eb->cancel_func, eb->cancel_baton,
                                            pool));
            }

          if (modified)
            {
              if (all_mods_are_deletes)
                reason = svn_wc_conflict_reason_deleted;
              else
                reason = svn_wc_conflict_reason_edited;
            }

        }
      break;
    }

  if (pconflict)
    *pconflict = NULL;

  /* If a conflict was detected, append log commands to the log accumulator
   * to record it. */
  if (reason != (svn_wc_conflict_reason_t)(-1))
    {
      svn_wc_conflict_description2_t *conflict;
      svn_wc_conflict_version_t *src_left_version;
      svn_wc_conflict_version_t *src_right_version;
      const char *repos_url = NULL;
      const char *path_in_repos = NULL;
      svn_node_kind_t left_kind = (entry->schedule == svn_wc_schedule_add)
                                  ? svn_node_none
                                  : (entry->schedule == svn_wc_schedule_delete)
                                    ? svn_node_unknown
                                    : entry->kind;

      /* Source-left repository root URL and path in repository.
       * The Source-right ones will be the same for update.
       * For switch, only the path in repository will differ, because
       * a cross-repository switch is not possible. */
      repos_url = entry->repos;
      path_in_repos = svn_uri_is_child(repos_url, entry->url, pool);
      if (path_in_repos == NULL)
        path_in_repos = "/";

      src_left_version = svn_wc_conflict_version_create(repos_url,
                                                        path_in_repos,
                                                        entry->revision,
                                                        left_kind,
                                                        pool);

      /* entry->kind is both base kind and working kind, because schedule
       * replace-by-different-kind is not supported. */
      /* ### TODO: but in case the entry is locally removed, entry->kind
       * is svn_node_none and doesn't reflect the older kind. Then we
       * need to find out the older kind in a different way! */

      /* For switch, find out the proper PATH_IN_REPOS for source-right. */
      if (eb->switch_url != NULL)
        {
          if (their_url != NULL)
            path_in_repos = svn_uri_is_child(repos_url, their_url, pool);
          else
            {
              /* The complete source-right URL is not available, but it
               * is somewhere below the SWITCH_URL. For now, just go
               * without it.
               * ### TODO: Construct a proper THEIR_URL in some of the
               * delete cases that still pass NULL for THEIR_URL when
               * calling this function. Do that on the caller's side. */
              path_in_repos = svn_uri_is_child(repos_url, eb->switch_url,
                                               pool);
              path_in_repos = apr_pstrcat(
                                pool, path_in_repos,
                                "_THIS_IS_INCOMPLETE",
                                NULL);
            }
        }

      src_right_version = svn_wc_conflict_version_create(repos_url,
                                                         path_in_repos,
                                                         *eb->target_revision,
                                                         their_node_kind,
                                                         pool);

      conflict = svn_wc_conflict_description_create_tree2(
        local_abspath, entry->kind,
        eb->switch_url ? svn_wc_operation_switch : svn_wc_operation_update,
        src_left_version, src_right_version, pool);
      conflict->action = action;
      conflict->reason = reason;

      /* Ensure 'log_accum' is non-null. svn_wc__loggy_add_tree_conflict()
       * would otherwise quietly set it to point to a newly allocated buffer
       * but we have no way to propagate that back to our caller. */
      SVN_ERR_ASSERT(log_accum != NULL);

      SVN_ERR(svn_wc__loggy_add_tree_conflict(&log_accum, conflict, pool));

      if (pconflict)
        *pconflict = conflict;
    }

  return SVN_NO_ERROR;
}

/* If LOCAL_ABSPATH is inside a conflicted tree, set *CONFLICTED to TRUE,
 * Otherwise set *CONFLICTED to FALSE.  Use SCRATCH_POOL for temporary
 * allocations.
 *
 * The search begins at the working copy root, returning the first
 * ("highest") tree conflict victim, which may be LOCAL_ABSPATH itself.
 *
 * ### this function MAY not cache 'entries' (lack of access batons), so
 * ### it will re-read the entries file for ancestor directories for
 * ### every path encountered during the update. however, the DB param
 * ### may have directories with access batons, holding the entries. it
 * ### depends on whether the update was done from the wcroot or not.
 */
static svn_error_t *
already_in_a_tree_conflict(svn_boolean_t *conflicted,
                           svn_wc__db_t *db,
                           const char *local_abspath,
                           apr_pool_t *scratch_pool)
{
  const char *ancestor_abspath = local_abspath;
  svn_error_t *err;
  apr_array_header_t *ancestors;
  const svn_wc_entry_t *entry;
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);
  int i;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  ancestors = apr_array_make(scratch_pool, 0, sizeof(const char *));

  /* If PATH is under version control, put it on the ancestor list. */
  err = svn_wc__get_entry(&entry, db, ancestor_abspath, TRUE,
                          svn_node_unknown, FALSE, iterpool, iterpool);
  if (err)
    {
      if (err->apr_err != SVN_ERR_NODE_UNEXPECTED_KIND
          && err->apr_err != SVN_ERR_WC_MISSING
          && err->apr_err != SVN_ERR_WC_PATH_NOT_FOUND)
        return svn_error_return(err);
      svn_error_clear(err);

      /* Obstructed or missing or whatever. Ignore it.  */
      entry = NULL;
    }
  if (entry != NULL)
    APR_ARRAY_PUSH(ancestors, const char *) = ancestor_abspath;

  ancestor_abspath = svn_dirent_dirname(ancestor_abspath, scratch_pool);

  /* Append to the list all ancestor-dirs in the working copy.  Ignore
     the root because it can't be tree-conflicted. */
  while (! svn_path_is_empty(ancestor_abspath))
    {
      svn_boolean_t is_wc_root;

      svn_pool_clear(iterpool);
      SVN_ERR(svn_wc__check_wc_root(&is_wc_root, NULL, db, ancestor_abspath,
                                    iterpool));
      if (is_wc_root)
        break;

      APR_ARRAY_PUSH(ancestors, const char *) = ancestor_abspath;

      ancestor_abspath = svn_dirent_dirname(ancestor_abspath, scratch_pool);
    }

  /* From the root end, check the conflict status of each ancestor. */
  for (i = ancestors->nelts - 1; i >= 0; i--)
    {
      const svn_wc_conflict_description2_t *conflict;

      ancestor_abspath = APR_ARRAY_IDX(ancestors, i, const char *);

      svn_pool_clear(iterpool);
      SVN_ERR(svn_wc__db_op_read_tree_conflict(&conflict, db, ancestor_abspath,
                                               scratch_pool, iterpool));
      if (conflict != NULL)
        {
          *conflicted = TRUE;

          return SVN_NO_ERROR;
        }
    }

  svn_pool_clear(iterpool);

  *conflicted = FALSE;

  return SVN_NO_ERROR;
}

/* Temporary helper until the new conflict handling is in place */
static svn_error_t *
node_already_conflicted(svn_boolean_t *conflicted,
                        svn_wc__db_t *db,
                        const char *local_abspath,
                        apr_pool_t *scratch_pool)
{
  const apr_array_header_t *conflicts;
  int i;

  SVN_ERR(svn_wc__db_read_conflicts(&conflicts, db, local_abspath,
                                    scratch_pool, scratch_pool));

  *conflicted = FALSE;

  for (i = 0; i < conflicts->nelts; i++)
    {
      const svn_wc_conflict_description2_t *cd;
      cd = APR_ARRAY_IDX(conflicts, i, const svn_wc_conflict_description2_t *);

      if (cd->kind == svn_wc_conflict_kind_tree)
        {
          *conflicted = TRUE;
          return SVN_NO_ERROR;
        }
      else if (cd->kind == svn_wc_conflict_kind_property ||
               cd->kind == svn_wc_conflict_kind_text)
        {
          svn_boolean_t text_conflicted, prop_conflicted, tree_conflicted;
          SVN_ERR(svn_wc__internal_conflicted_p(&text_conflicted,
                                                &prop_conflicted,
                                                &tree_conflicted,
                                                db, local_abspath,
                                                scratch_pool));

          *conflicted = (text_conflicted || prop_conflicted || tree_conflicted);
          return SVN_NO_ERROR;
        }
    }

  return SVN_NO_ERROR;
}


/* A walk baton for schedule_existing_item_for_re_add()'s call
   to svn_wc_walk_entries3(). */
struct set_copied_baton_t
{
  struct edit_baton *eb;

  /* The PATH arg to schedule_existing_item_for_re_add(). */
  const char *added_subtree_root_path;
};

/* An svn_wc__node_found_func_t callback function.
 * Set the 'copied' flag on the given ENTRY for every PATH
 * under ((set_copied_baton_t *)WALK_BATON)->ADDED_SUBTREE_ROOT_PATH
 * which has a normal schedule. */
static svn_error_t *
set_copied_callback(const char *local_abspath,
                    void *walk_baton,
                    apr_pool_t *scratch_pool)
{
  struct set_copied_baton_t *b = walk_baton;
  svn_wc__db_status_t status;
  svn_wc__db_kind_t kind;

  if (strcmp(local_abspath, b->added_subtree_root_path) == 0)
    return SVN_NO_ERROR; /* Don't touch the root */

  SVN_ERR(svn_wc__db_read_info(&status, &kind, NULL, NULL, NULL,
                               NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                               NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                               NULL, NULL, NULL, NULL, NULL,
                               b->eb->db, local_abspath,
                               scratch_pool, scratch_pool));

  if (kind == svn_wc__db_kind_dir)
    {
      /* We don't want to mark a deleted PATH as copied.  If PATH
         is added without history we don't want to make it look like
         it has history.  If PATH is replaced we don't want to make
         it look like it has history if it doesn't.  Only if PATH is
         schedule normal do we need to mark it as copied. */
      if (status == svn_wc__db_status_normal)
        {
          svn_wc_entry_t tmp_entry;

          /* Set the 'copied' flag and write the entry out to disk. */
          tmp_entry.copied = TRUE;
          SVN_ERR(svn_wc__entry_modify2(b->eb->db,
                                        local_abspath,
                                        svn_node_dir,
                                        TRUE,
                                       &tmp_entry,
                                       SVN_WC__ENTRY_MODIFY_COPIED,
                                       scratch_pool));
        }
    }

  /* We don't want to mark a deleted PATH as copied.  If PATH
     is added without history we don't want to make it look like
     it has history.  If PATH is replaced we don't want to make
     it look like it has history if it doesn't.  Only if PATH is
     schedule normal do we need to mark it as copied. */
  if (status == svn_wc__db_status_normal)
    {
      svn_wc_entry_t tmp_entry;

      /* Set the 'copied' flag and write the entry out to disk. */
      tmp_entry.copied = TRUE;
      SVN_ERR(svn_wc__entry_modify2(b->eb->db,
                                    local_abspath,
                                    kind == svn_wc__db_kind_dir
                                      ? svn_node_dir
                                      : svn_node_file,
                                    FALSE,
                                    &tmp_entry,
                                    SVN_WC__ENTRY_MODIFY_COPIED,
                                    scratch_pool));
    }
  return SVN_NO_ERROR;
}

/* Schedule the WC item LOCAL_ABSPATH, whose entry is ENTRY, for re-addition.
 * If MODIFY_COPYFROM is TRUE, re-add the item as a copy with history
 * of (ENTRY->url)@(ENTRY->rev). 
 * Assume that the item exists locally and is scheduled as still existing with
 * some local modifications relative to its (old) base, but does not exist in
 * the repository at the target revision.
 *
 * Use the local content of the item, even if it
 * If the item is a directory, recursively schedule its contents to be the
 * contents of the re-added tree, even if they are locally modified relative
 * to it.
 *
 * THEIR_URL is the deleted node's URL on the source-right side, the
 * side that the target should become after the update. In other words,
 * that's the new URL the node would have if it were not deleted.
 *
 * Make changes to entries immediately, not loggily, because that is easier
 * to keep track of when multiple directories are involved.
 *  */
static svn_error_t *
schedule_existing_item_for_re_add(const svn_wc_entry_t *entry,
                                  struct edit_baton *eb,
                                  const char *local_abspath,
                                  const char *their_url,
                                  svn_boolean_t modify_copyfrom,
                                  apr_pool_t *pool)
{
  svn_wc_entry_t tmp_entry;
  apr_uint64_t flags = 0;

  /* Update the details of the base rev/url to reflect the incoming
   * delete, while leaving the working version as it is, scheduling it
   * for re-addition unless it was already non-existent. */
  tmp_entry.url = their_url;
  flags |= SVN_WC__ENTRY_MODIFY_URL;

  /* Schedule the working version to be re-added. */
  tmp_entry.schedule = svn_wc_schedule_add;
  flags |= SVN_WC__ENTRY_MODIFY_SCHEDULE;
  flags |= SVN_WC__ENTRY_MODIFY_FORCE;

  if (modify_copyfrom)
    {
      tmp_entry.copyfrom_url = entry->url;
      flags |= SVN_WC__ENTRY_MODIFY_COPYFROM_URL;
      tmp_entry.copyfrom_rev = entry->revision;
      flags |= SVN_WC__ENTRY_MODIFY_COPYFROM_REV;
      tmp_entry.copied = TRUE;
      flags |= SVN_WC__ENTRY_MODIFY_COPIED;
    }

  /* ### Need to change the "base" into a "revert-base" ? */

  /* Determine which adm dir holds this node's entry */
  /* ### But this will fail if eb->adm_access holds only a shallow lock. */
  SVN_ERR(svn_wc__entry_modify2(eb->db,
                                local_abspath,
                                entry->kind,
                                FALSE,
                                &tmp_entry,
                                flags, pool));

  /* If it's a directory, set the 'copied' flag recursively. The rest of the
   * directory tree's state can stay exactly as it was before being
   * scheduled for re-add. */
  /* ### BH: I don't think this code handles switched subpaths, excluded
         and absent items in any usefull way. Needs carefull redesign */
  if (entry->kind == svn_node_dir)
    {
      struct set_copied_baton_t set_copied_baton;

      /* Set the 'copied' flag recursively, to support the
       * cases where this is a directory. */
      set_copied_baton.eb = eb;
      set_copied_baton.added_subtree_root_path = local_abspath;
      SVN_ERR(svn_wc__internal_walk_children(eb->db, local_abspath, FALSE,
                                             set_copied_callback,
                                             &set_copied_baton,
                                             svn_depth_infinity,
                                             NULL, NULL, pool));

      /* If PATH is a directory then we must also record in PARENT_PATH's
         entry that we are re-adding PATH. */
      flags &= ~SVN_WC__ENTRY_MODIFY_URL;
      SVN_ERR(svn_wc__entry_modify2(eb->db, local_abspath, svn_node_dir, TRUE,
                                   &tmp_entry, flags, pool));

      /* ### Need to do something more, such as change 'base' into 'revert-base'? */
    }

  return SVN_NO_ERROR;
}

/* Delete PATH from its immediate parent PARENT_PATH, in the edit
 * represented by EB. PATH is relative to EB->anchor.
 * PARENT_PATH is relative to the current working directory.
 *
 * THEIR_URL is the deleted node's URL on the source-right side, the
 * side that the target should become after the update. In other words,
 * that's the new URL the node would have if it were not deleted.
 *
 * Name temporary transactional logs based on *LOG_NUMBER, but set
 * *LOG_NUMBER to 0 after running the final log.  Perform all allocations in
 * POOL.
 */
static svn_error_t *
do_entry_deletion(int *log_number,
                  struct edit_baton *eb,
                  const char *local_abspath,
                  const char *their_url,
                  svn_boolean_t accept_deleted,
                  apr_pool_t *pool)
{
  svn_error_t *err;
  const svn_wc_entry_t *entry;
  svn_boolean_t already_conflicted;
  svn_stringbuf_t *log_item = svn_stringbuf_create("", pool);
  svn_wc_conflict_description2_t *tree_conflict;
  const char *dir_abspath = svn_dirent_dirname(local_abspath, pool);
  svn_wc_adm_access_t *parent_adm_access;


  parent_adm_access = svn_wc__adm_retrieve_internal2(eb->db, dir_abspath,
                                                     pool);
  SVN_ERR_ASSERT(parent_adm_access != NULL);

  /* ### hmm. in case we need to re-add the node, we use some fields from
     ### this entry. I believe the required fields are filled in, but getting
     ### just the stub might be a problem.  */
  err = svn_wc__get_entry(&entry, eb->db, local_abspath,
                          FALSE /* allow_unversioned */, svn_node_unknown,
                          TRUE /* need_parent_stub */, pool, pool);
  if (err)
    {
      if (err->apr_err != SVN_ERR_NODE_UNEXPECTED_KIND)
        return svn_error_return(err);

      /* The node was a file, and we got the "real" entry, not the stub.
         That is just what we'd like.  */
      svn_error_clear(err);
    }

  /* Receive the remote removal of excluded entry. Do not notify. */
  if (entry->depth == svn_depth_exclude)
    {
      SVN_ERR(svn_wc__entry_remove(eb->db, local_abspath, pool));

      if (strcmp(local_abspath, eb->target_abspath) == 0)
        eb->target_deleted = TRUE;
      return SVN_NO_ERROR;
    }

  /* Is this path a conflict victim? */
  SVN_ERR(node_already_conflicted(&already_conflicted, eb->db,
                                  local_abspath, pool));
  if (already_conflicted)
    {
      SVN_ERR(remember_skipped_tree(eb, local_abspath));

      /* ### TODO: Also print victim_path in the skip msg. */
      if (eb->notify_func)
        eb->notify_func(eb->notify_baton,
                        svn_wc_create_notify(local_abspath,
                                             svn_wc_notify_skip,
                                             pool),
                        pool);

      return SVN_NO_ERROR;
    }

  /* Is this path the victim of a newly-discovered tree conflict?  If so,
   * remember it and notify the client. Then (if it was existing and
   * modified), re-schedule the node to be added back again, as a (modified)
   * copy of the previous base version.
   */
  SVN_ERR(check_tree_conflict(&tree_conflict, eb, local_abspath, log_item,
                              svn_wc_conflict_action_delete, svn_node_none,
                              their_url, accept_deleted, pool));
  if (tree_conflict != NULL)
    {
      /* When we raise a tree conflict on a directory, we want to avoid
       * making any changes inside it. (Will an update ever try to make
       * further changes to or inside a directory it's just deleted?) */
      SVN_ERR(remember_skipped_tree(eb, local_abspath));

      if (eb->notify_func)
        eb->notify_func(eb->notify_baton,
                        svn_wc_create_notify(local_abspath,
                                             svn_wc_notify_tree_conflict,
                                             pool),
                        pool);

      if (tree_conflict->reason == svn_wc_conflict_reason_edited)
        {
          /* The item exists locally and has some sort of local mod.
           * It no longer exists in the repository at its target URL@REV.
           * (### If its WC parent was not updated similarly, then it needs to
           * be marked 'deleted' in its WC parent.)
           * To prepare the "accept mine" resolution for the tree conflict,
           * we must schedule the existing content for re-addition as a copy
           * of what it was, but with its local modifications preserved. */

          /* Run the log in the parent dir, to record the tree conflict.
           * Do this before schedule_existing_item_for_re_add(), in case
           * that needs to modify the same entries. */
          SVN_ERR(svn_wc__write_log(
                                svn_wc__adm_access_abspath(parent_adm_access),
                                *log_number, log_item, pool));
          SVN_ERR(svn_wc__run_log(parent_adm_access, pool));
          *log_number = 0;

          SVN_ERR(schedule_existing_item_for_re_add(entry, eb,
                                                    local_abspath, their_url,
                                                    TRUE, pool));
          return SVN_NO_ERROR;
        }
      else if (tree_conflict->reason == svn_wc_conflict_reason_deleted)
        {
          /* The item does not exist locally (except perhaps as a skeleton
           * directory tree) because it was already scheduled for delete.
           * We must complete the deletion, leaving the tree conflict info
           * as the only difference from a normal deletion. */

          /* Fall through to the normal "delete" code path. */
        }
      else if (tree_conflict->reason == svn_wc_conflict_reason_replaced)
        {
          /* The item was locally replaced with something else. We should
           * keep the existing item schedule-replace, but we also need to
           * update the BASE rev of the item to the revision we are updating
           * to. Otherwise, the replace cannot be committed because the item
           * is considered out-of-date, and it cannot be updated either because
           * we're here to do just that. */

          /* Run the log in the parent dir, to record the tree conflict.
           * Do this before schedule_existing_item_for_re_add(), in case
           * that needs to modify the same entries. */
          SVN_ERR(svn_wc__write_log(
                                svn_wc__adm_access_abspath(parent_adm_access),
                                *log_number, log_item, pool));
          SVN_ERR(svn_wc__run_log(parent_adm_access, pool));
          *log_number = 0;

          SVN_ERR(schedule_existing_item_for_re_add(entry, eb,
                                                    local_abspath, their_url,
                                                    FALSE, pool));
          return SVN_NO_ERROR;
        }
      else
        SVN_ERR_MALFUNCTION();  /* other reasons are not expected here */
    }

  /* Issue a loggy command to delete the entry from version control and to
   * delete it from disk if unmodified, but leave any modified files on disk
   * unversioned. */
  SVN_ERR(svn_wc__loggy_delete_entry(&log_item, dir_abspath, local_abspath,
                                     pool, pool));

  /* If the thing being deleted is the *target* of this update, then
     we need to recreate a 'deleted' entry, so that the parent can give
     accurate reports about itself in the future. */
  if (strcmp(local_abspath, eb->target_abspath) == 0)
    {
      svn_wc_entry_t tmp_entry;

      tmp_entry.revision = *(eb->target_revision);
      /* ### Why not URL as well? This might be a switch. ... */
      /* tmp_entry.url = *(eb->target_url) or db->new_URL ? */
      tmp_entry.kind = entry->kind;
      tmp_entry.deleted = TRUE;

      SVN_ERR(svn_wc__loggy_entry_modify(&log_item,
                               dir_abspath, local_abspath,
                               &tmp_entry,
                               SVN_WC__ENTRY_MODIFY_REVISION
                               | SVN_WC__ENTRY_MODIFY_KIND
                               | SVN_WC__ENTRY_MODIFY_DELETED,
                               pool, pool));

      eb->target_deleted = TRUE;
    }

  SVN_ERR(svn_wc__write_log(svn_wc__adm_access_abspath(parent_adm_access),
                            *log_number, log_item, pool));

  if (eb->switch_url)
    {
      /* The SVN_WC__LOG_DELETE_ENTRY log item will cause
       * svn_wc_remove_from_revision_control() to be run.  But that
       * function checks whether the deletion target's URL is child of
       * its parent directory's URL, and if it's not, then the entry
       * in parent won't be deleted (because presumably the child
       * represents a disjoint working copy, i.e., it is a wc_root).
       *
       * However, during a switch this works against us, because by
       * the time we get here, the parent's URL has already been
       * changed.  So we manually remove the child from revision
       * control after the delete-entry item has been written in the
       * parent's log, but before it is run, so the only work left for
       * the log item is to remove the entry in the parent directory.
       */

      if (entry->kind == svn_node_dir)
        {
          SVN_ERR(leftmod_error_chain(
                    svn_wc__remove_from_revision_control_internal(
                      eb->db,
                      local_abspath,
                      TRUE, /* destroy */
                      FALSE, /* instant error */
                      eb->cancel_func,
                      eb->cancel_baton,
                      pool)));
        }
    }

  /* Note: these two lines are duplicated in the tree-conflicts bail out
   * above. */
  SVN_ERR(svn_wc__run_log(parent_adm_access, pool));
  *log_number = 0;

  /* Notify. (If tree_conflict, we've already notified.) */
  if (eb->notify_func
      && tree_conflict == NULL)
    {
      eb->notify_func(eb->notify_baton,
                      svn_wc_create_notify(local_abspath,
                                           svn_wc_notify_update_delete,
                                           pool),
                      pool);
    }

  return SVN_NO_ERROR;
}


/* An svn_delta_editor_t function. */
static svn_error_t *
delete_entry(const char *path,
             svn_revnum_t revision,
             void *parent_baton,
             apr_pool_t *pool)
{
  struct dir_baton *pb = parent_baton;
  const char *basename = svn_uri_basename(path, pool);
  const char *local_abspath;
  const char *their_url;

  local_abspath = svn_dirent_join(pb->local_abspath, basename, pool);
  
  if (pb->skip_descendants)
    {
      if (!pb->skip_this)
        SVN_ERR(remember_skipped_tree(pb->edit_baton, local_abspath));

      return SVN_NO_ERROR;
    }

  SVN_ERR(check_path_under_root(pb->local_abspath, basename, pool));

  their_url = svn_path_url_add_component2(pb->new_URL, basename, pool);

  return do_entry_deletion(&pb->log_number, pb->edit_baton, local_abspath,
                           their_url, pb->accept_deleted, pool);
}


/* An svn_delta_editor_t function. */
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
  struct dir_baton *db;
  svn_node_kind_t kind;
  svn_boolean_t already_conflicted;

  /* Semantic check.  Either both "copyfrom" args are valid, or they're
     NULL and SVN_INVALID_REVNUM.  A mixture is illegal semantics. */
  SVN_ERR_ASSERT((copyfrom_path && SVN_IS_VALID_REVNUM(copyfrom_revision))
                 || (!copyfrom_path &&
                     !SVN_IS_VALID_REVNUM(copyfrom_revision)));

  SVN_ERR(make_dir_baton(&db, path, eb, pb, TRUE, pool));
  *child_baton = db;

  if (pb->skip_descendants)
    {
      if (!pb->skip_this)
        SVN_ERR(remember_skipped_tree(eb, db->local_abspath));

      db->skip_this = TRUE;
      db->skip_descendants = TRUE;
      db->already_notified = TRUE;

      return SVN_NO_ERROR;
    }

  SVN_ERR(check_path_under_root(pb->local_abspath, db->name, pool));

  if (strcmp(eb->target, path) == 0)
    {
      /* The target of the edit is being added, give it the requested
         depth of the edit (but convert svn_depth_unknown to
         svn_depth_infinity). */
      db->ambient_depth = (eb->requested_depth == svn_depth_unknown)
        ? svn_depth_infinity : eb->requested_depth;
    }
  else if (eb->requested_depth == svn_depth_immediates
           || (eb->requested_depth == svn_depth_unknown
               && pb->ambient_depth == svn_depth_immediates))
    {
      db->ambient_depth = svn_depth_empty;
    }
  else
    {
      db->ambient_depth = svn_depth_infinity;
    }

  /* Flush the log for the parent directory before going into this subtree. */
  SVN_ERR(flush_log(pb, pool));




  /* Is this path a conflict victim? */
  SVN_ERR(node_already_conflicted(&already_conflicted, eb->db,
                                  db->local_abspath, pool));
  if (already_conflicted)
    {
      /* Record this conflict so that its descendants are skipped silently. */
      SVN_ERR(remember_skipped_tree(eb, db->local_abspath));

      db->skip_this = TRUE;
      db->skip_descendants = TRUE;
      db->already_notified = TRUE;

      /* ### TODO: Also print victim_path in the skip msg. */
      if (eb->notify_func)
        eb->notify_func(eb->notify_baton,
                        svn_wc_create_notify(db->local_abspath,
                                             svn_wc_notify_skip,
                                             pool),
                        pool);

      return SVN_NO_ERROR;
    }

  SVN_ERR(svn_io_check_path(db->local_abspath, &kind, db->pool));

  /* The path can exist, but it must be a directory... */
  if (kind == svn_node_file || kind == svn_node_unknown)
    {
    return svn_error_createf(
       SVN_ERR_WC_OBSTRUCTED_UPDATE, NULL,
       _("Failed to add directory '%s': a non-directory object of the "
         "same name already exists"),
       svn_dirent_local_style(db->path, pool));
    }

  if (kind == svn_node_dir)
    {
      const svn_wc_entry_t *entry;

      /* Test the obstructing dir to see if it's versioned. */
      svn_error_t *err = svn_wc__get_entry(&entry, eb->db, db->local_abspath,
                                           TRUE, svn_node_dir, FALSE,
                                           pool, pool);

      if (err && ((err->apr_err == SVN_ERR_WC_NOT_WORKING_COPY) ||
                  (err->apr_err == SVN_ERR_WC_MISSING)))
        {
          svn_error_clear(err);
          if (eb->allow_unver_obstructions)
            {
              /* Obstructing dir is not versioned, just need to flag it as
                 existing then we are done here. */
              db->existed = TRUE;
            }
          else
            {
              db->already_notified = TRUE;
              if (eb->notify_func)
                {
                  svn_wc_notify_t *notify = 
                        svn_wc_create_notify(db->local_abspath,
                                             svn_wc_notify_update_obstruction,
                                             pool);

                  notify->kind = svn_node_dir;
                  eb->notify_func(eb->notify_baton, notify, pool);
                }

              return svn_error_createf(
                 SVN_ERR_WC_OBSTRUCTED_UPDATE, NULL,
                 _("Failed to add directory '%s': an unversioned "
                   "directory of the same name already exists"),
                 svn_dirent_local_style(db->path, pool));
            }
        }
      else if ((entry != NULL) && 
               ((err == NULL) ||
                (err->apr_err == SVN_ERR_NODE_UNEXPECTED_KIND)))
        {
          /* Obstructing dir *is* versioned or scheduled for addition. */

          const svn_wc_entry_t *parent_entry;
          const svn_wc_entry_t *entry_in_parent;

          svn_error_clear(err);

          /* Check the parent directory */
          SVN_ERR(svn_wc__get_entry(&parent_entry, eb->db,
                                    svn_dirent_dirname(db->local_abspath, pool),
                                    FALSE, svn_node_dir, FALSE, pool, pool));


          /* What to do with a versioned or schedule-add dir:

             If the UUID doesn't match the parent's, or the URL isn't a
             child of the parent dir's URL, or the dir is unversioned in
             the parent entry, it's an error.

             A dir already added without history is OK.  Set add_existed
             so that user notification is delayed until after any prop
             conflicts have been found.

             An existing versioned dir is an error.  In the future we may
             relax this restriction and simply update such dirs.

             A dir added with history is a tree conflict. */

          if (entry->uuid && parent_entry->uuid)
            {
              if (strcmp(entry->uuid, parent_entry->uuid) != 0)
                return svn_error_createf(
                  SVN_ERR_WC_OBSTRUCTED_UPDATE, NULL,
                  _("UUID mismatch: existing directory '%s' was checked out "
                    "from a different repository"),
                  svn_dirent_local_style(db->path, pool));
            }

          SVN_ERR_ASSERT(db->new_URL != NULL);

          if (!eb->switch_url && entry->url
              && strcmp(db->new_URL, entry->url) != 0)
            return svn_error_createf(
               SVN_ERR_WC_OBSTRUCTED_UPDATE, NULL,
               _("URL '%s' of existing directory '%s' does not match "
                 "expected URL '%s'"),
               entry->url, svn_dirent_local_style(db->path, pool),
               db->new_URL);

          err = svn_wc__get_entry(&entry_in_parent, eb->db, db->local_abspath,
                                  FALSE, svn_node_dir, TRUE, pool, pool);

          if (err || ! entry_in_parent)
            {
              svn_error_clear(err);
              return svn_error_createf(
                 SVN_ERR_WC_OBSTRUCTED_UPDATE, NULL,
                 _("Failed to add directory '%s': a versioned "
                   "directory of the same name already exists"),
                 svn_dirent_local_style(db->path, pool));
            }

          if ((entry->schedule == svn_wc_schedule_add
               || entry->schedule == svn_wc_schedule_replace)
              && !entry->copied) /* added without history */
            {
              db->add_existed = TRUE;
            }
          else
            {
              svn_wc_conflict_description2_t *tree_conflict;

              /* Raise a tree conflict. */
              SVN_ERR(check_tree_conflict(&tree_conflict, eb,
                                          db->local_abspath, pb->log_accum,
                                          svn_wc_conflict_action_add,
                                          svn_node_dir, db->new_URL,
                                          db->accept_deleted,
                                          pool));

              if (tree_conflict != NULL)
                {
                  /* Record this conflict so that its descendants are
                     skipped silently. */
                  SVN_ERR(remember_skipped_tree(eb, db->local_abspath));

                  db->skip_this = TRUE;
                  db->skip_descendants = TRUE;
                  db->already_notified = TRUE;

                  if (eb->notify_func)
                    eb->notify_func(eb->notify_baton,
                                    svn_wc_create_notify(
                                               db->local_abspath,
                                               svn_wc_notify_tree_conflict,
                                               pool),
                                    pool);

                  return SVN_NO_ERROR;
                }
            }
        }
      else
        SVN_ERR(err);
    }

  /* It may not be named the same as the administrative directory. */
  if (svn_wc_is_adm_dir(svn_dirent_basename(path, pool), pool))
    return svn_error_createf(
       SVN_ERR_WC_OBSTRUCTED_UPDATE, NULL,
       _("Failed to add directory '%s': object of the same name as the "
         "administrative directory"),
       svn_dirent_local_style(db->path, pool));


  /* Either we got real copyfrom args... */
  if (copyfrom_path || SVN_IS_VALID_REVNUM(copyfrom_revision))
    {
      /* ### todo: for now, this editor doesn't know how to deal with
         copyfrom args.  Someday it will interpet them as an update
         optimization, and actually copy one part of the wc to another.
         Then it will recursively "normalize" all the ancestry in the
         copied tree.  Someday!

         Note from the future: if someday it does, we'll probably want
         to tweak libsvn_ra_neon/fetch.c:validate_element() to accept
         that an add-dir element can contain a delete-entry element
         (because the dir might be added with history).  Currently
         that combination will not validate.  See r30161, and see the
         thread in which this message appears:

      http://subversion.tigris.org/servlets/ReadMsg?list=dev&msgNo=136879
      From: "David Glasser" <glasser@davidglasser.net>
      To: "Karl Fogel" <kfogel@red-bean.com>, dev@subversion.tigris.org
      Cc: "Arfrever Frehtes Taifersar Arahesis" <arfrever.fta@gmail.com>,
          glasser@tigris.org
      Subject: Re: svn commit: r30161 - in trunk/subversion: \
               libsvn_ra_neon tests/cmdline
      Date: Fri, 4 Apr 2008 14:47:06 -0700
      Message-ID: <1ea387f60804041447q3aea0bbds10c2db3eacaf73e@mail.gmail.com>

      */
      return svn_error_createf(
         SVN_ERR_UNSUPPORTED_FEATURE, NULL,
         _("Failed to add directory '%s': "
           "copyfrom arguments not yet supported"),
         svn_dirent_local_style(db->path, pool));
    }
  else  /* ...or we got invalid copyfrom args. */
    {
      svn_wc_entry_t tmp_entry;
      apr_uint64_t modify_flags = SVN_WC__ENTRY_MODIFY_KIND |
        SVN_WC__ENTRY_MODIFY_DELETED | SVN_WC__ENTRY_MODIFY_ABSENT;

      /* Immediately create an entry for the new directory in the parent.
         Note that the parent must already be either added or opened, and
         thus it's in an 'incomplete' state just like the new dir.
         The entry may already exist if the new directory is already
         scheduled for addition without history, in that case set
         its schedule to normal. */
      tmp_entry.kind = svn_node_dir;
      /* Note that there may already exist a 'ghost' entry in the
         parent with the same name, in a 'deleted' or 'absent' state.
         If so, it's fine to overwrite it... but we need to make sure
         we get rid of the state flag when doing so: */
      tmp_entry.deleted = FALSE;
      tmp_entry.absent = FALSE;

      if (db->add_existed)
        {
          tmp_entry.schedule = svn_wc_schedule_normal;
          modify_flags |= SVN_WC__ENTRY_MODIFY_SCHEDULE |
            SVN_WC__ENTRY_MODIFY_FORCE;
        }

      SVN_ERR(svn_wc__entry_modify2(eb->db, db->local_abspath,
                                    svn_node_dir, TRUE,
                                    &tmp_entry, modify_flags, pool));

      if (db->add_existed)
        {
          /* Immediately tweak the schedule for "this dir" so it too
             is no longer scheduled for addition.  Change rev from 0
             to the target revision allowing prep_directory() to do
             its thing without error. */
          modify_flags  = SVN_WC__ENTRY_MODIFY_SCHEDULE
            | SVN_WC__ENTRY_MODIFY_FORCE | SVN_WC__ENTRY_MODIFY_REVISION;

          tmp_entry.revision = *(eb->target_revision);

          if (eb->switch_url)
            {
              tmp_entry.url = svn_path_url_add_component2(eb->switch_url,
                                                          db->name, pool);
              modify_flags |= SVN_WC__ENTRY_MODIFY_URL;
            }

          SVN_ERR(svn_wc__entry_modify2(eb->db, db->local_abspath,
                                        svn_node_dir, FALSE,
                                        &tmp_entry, modify_flags, pool));
        }
    }

  SVN_ERR(prep_directory(db,
                         db->new_URL,
                         *(eb->target_revision),
                         db->pool));

  /* If PATH is within a locally deleted tree then make it also
     scheduled for deletion.  We must do this after the call to
     prep_directory() otherwise the administrative area for DB->PATH
     is not present, nor is there an entry for DB->PATH in DB->PATH's
     entries. */
  if (pb->accept_deleted)
    {
      svn_wc_entry_t tmp_entry;
      apr_uint64_t modify_flags = SVN_WC__ENTRY_MODIFY_SCHEDULE;

      tmp_entry.schedule = svn_wc_schedule_delete;

      /* Mark PATH as scheduled for deletion in its parent. */
      SVN_ERR(svn_wc__entry_modify2(eb->db, db->local_abspath,
                                    svn_node_dir, TRUE,
                                    &tmp_entry, modify_flags, pool));

      /* Mark PATH's 'this dir' entry as scheduled for deletion. */
      SVN_ERR(svn_wc__entry_modify2(eb->db, db->local_abspath,
                                    svn_node_dir, FALSE,
                                    &tmp_entry, modify_flags, pool));
    }

  /* If this add was obstructed by dir scheduled for addition without
     history let close_file() handle the notification because there
     might be properties to deal with.  If PATH was added inside a locally
     deleted tree, then suppress notification, a tree conflict was already
     issued. */
  if (eb->notify_func && !db->already_notified && !db->add_existed)
    {
      svn_wc_notify_t *notify;
      svn_wc_notify_action_t action;

      if (db->accept_deleted)
        action = svn_wc_notify_update_add_deleted;
      else if (db->existed)
        action = svn_wc_notify_exists;
      else
        action = svn_wc_notify_update_add;

      notify = svn_wc_create_notify(db->local_abspath, action, pool);
      notify->kind = svn_node_dir;
      eb->notify_func(eb->notify_baton, notify, pool);
      db->already_notified = TRUE;
    }

  return SVN_NO_ERROR;
}

/* An svn_delta_editor_t function. */
static svn_error_t *
open_directory(const char *path,
               void *parent_baton,
               svn_revnum_t base_revision,
               apr_pool_t *pool,
               void **child_baton)
{
  struct dir_baton *db, *pb = parent_baton;
  struct edit_baton *eb = pb->edit_baton;
  svn_wc_entry_t tmp_entry;
  apr_uint64_t flags = SVN_WC__ENTRY_MODIFY_REVISION |
    SVN_WC__ENTRY_MODIFY_URL | SVN_WC__ENTRY_MODIFY_INCOMPLETE;

  svn_boolean_t already_conflicted;
  svn_wc_conflict_description2_t *tree_conflict;
  svn_wc__db_status_t status;

  SVN_ERR(make_dir_baton(&db, path, eb, pb, FALSE, pool));
  *child_baton = db;

  if (pb->skip_descendants)
    {
      if (!pb->skip_this)
        SVN_ERR(remember_skipped_tree(eb, db->local_abspath));

      db->skip_this = TRUE;
      db->skip_descendants = TRUE;
      db->already_notified = TRUE;

      db->bump_info->skipped = TRUE;

      return SVN_NO_ERROR;
    }

  SVN_ERR(check_path_under_root(pb->local_abspath, db->name, pool));

  /* Flush the log for the parent directory before going into this subtree. */
  SVN_ERR(flush_log(pb, pool));

  SVN_ERR(svn_wc__db_read_info(&status, NULL, &db->old_revision, NULL, NULL,
                               NULL, NULL, NULL, NULL, NULL,
                               &db->ambient_depth, NULL, NULL, NULL, NULL,
                               NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                               NULL, NULL,
                               eb->db, db->local_abspath, pool, pool));

  db->was_incomplete = (status == svn_wc__db_status_incomplete);

  /* Is this path a conflict victim? */
  SVN_ERR(node_already_conflicted(&already_conflicted, eb->db,
                                  db->local_abspath, pool));
  if (already_conflicted)
    {
      SVN_ERR(remember_skipped_tree(eb, db->local_abspath));

      db->skip_this = TRUE;
      db->skip_descendants = TRUE;
      db->already_notified = TRUE;

      if (eb->notify_func)
        eb->notify_func(eb->notify_baton,
                        svn_wc_create_notify(db->local_abspath,
                                             svn_wc_notify_skip,
                                             pool),
                        pool);

      return SVN_NO_ERROR;
    }
  
  /* Is this path a fresh tree conflict victim?  If so, skip the tree
     with one notification. */
  SVN_ERR(check_tree_conflict(&tree_conflict, eb, db->local_abspath,
                              pb->log_accum,
                              svn_wc_conflict_action_edit,
                              svn_node_dir, db->new_URL, 
                              db->accept_deleted, pool));

  /* Remember the roots of any locally deleted trees. */
  if (tree_conflict != NULL)
    {
      if (eb->notify_func)
        {
          svn_wc_notify_t *notify
              = svn_wc_create_notify(db->local_abspath,
                                     svn_wc_notify_tree_conflict,
                                     pool);
          notify->kind = svn_node_dir;

          eb->notify_func(eb->notify_baton, notify, pool);
          db->already_notified = TRUE;
        }

      /* Even if PATH is locally deleted we still need mark it as being
         at TARGET_REVISION, so fall through to the code below to do just
         that. */
      if (tree_conflict->reason != svn_wc_conflict_reason_deleted &&
          tree_conflict->reason != svn_wc_conflict_reason_replaced)
        {
          SVN_ERR(remember_skipped_tree(eb, db->local_abspath));
          db->skip_descendants = TRUE;
          db->skip_this = TRUE;

          return SVN_NO_ERROR;
        }
      else
        db->accept_deleted = TRUE;
    }

  /* Mark directory as being at target_revision and URL, but incomplete. */
  tmp_entry.revision = *(eb->target_revision);
  tmp_entry.url = db->new_URL;
  tmp_entry.incomplete = TRUE;

  return svn_wc__entry_modify2(eb->db, db->local_abspath,
                               svn_node_dir, FALSE,
                              &tmp_entry, flags,
                              pool);
}


/* An svn_delta_editor_t function. */
static svn_error_t *
change_dir_prop(void *dir_baton,
                const char *name,
                const svn_string_t *value,
                apr_pool_t *pool)
{
  svn_prop_t *propchange;
  struct dir_baton *db = dir_baton;

  if (db->skip_this)
    return SVN_NO_ERROR;

  propchange = apr_array_push(db->propchanges);
  propchange->name = apr_pstrdup(db->pool, name);
  propchange->value = value ? svn_string_dup(value, db->pool) : NULL;

  return SVN_NO_ERROR;
}

/* If any of the svn_prop_t objects in PROPCHANGES represents a change
   to the SVN_PROP_EXTERNALS property, return that change, else return
   null.  If PROPCHANGES contains more than one such change, return
   the first. */
static const svn_prop_t *
externals_prop_changed(apr_array_header_t *propchanges)
{
  int i;

  for (i = 0; i < propchanges->nelts; i++)
    {
      const svn_prop_t *p = &(APR_ARRAY_IDX(propchanges, i, svn_prop_t));
      if (strcmp(p->name, SVN_PROP_EXTERNALS) == 0)
        return p;
    }

  return NULL;
}

/* This implements the svn_iter_apr_hash_cb_t callback interface.
 *
 * Add a property named KEY ('const char *') to a list of properties
 * to be deleted.  BATON is the list: an 'apr_array_header_t *'
 * representing propchanges (the same type as found in struct dir_baton
 * and struct file_baton).
 *
 * Ignore KLEN, VAL, and POOL.
 */
static svn_error_t *
add_prop_deletion(void *baton, const void *key,
                  apr_ssize_t klen, void *val,
                  apr_pool_t *pool)
{
  apr_array_header_t *propchanges = baton;
  const char *name = key;
  svn_prop_t *prop = apr_array_push(propchanges);

  /* Add the deletion of NAME to PROPCHANGES. */
  prop->name = name;
  prop->value = NULL;

  return SVN_NO_ERROR;
}

/* Create in POOL a name->value hash from PROP_LIST, and return it. */
static apr_hash_t *
prop_hash_from_array(const apr_array_header_t *prop_list,
                     apr_pool_t *pool)
{
  int i;
  apr_hash_t *prop_hash = apr_hash_make(pool);

  for (i = 0; i < prop_list->nelts; i++)
    {
      const svn_prop_t *prop = &APR_ARRAY_IDX(prop_list, i, svn_prop_t);
      apr_hash_set(prop_hash, prop->name, APR_HASH_KEY_STRING, prop->value);
    }

  return prop_hash;
}

/* An svn_delta_editor_t function. */
static svn_error_t *
close_directory(void *dir_baton,
                apr_pool_t *pool)
{
  struct dir_baton *db = dir_baton;
  struct edit_baton *eb = db->edit_baton;
  svn_wc_notify_state_t prop_state = svn_wc_notify_state_unknown;
  apr_array_header_t *entry_props, *wc_props, *regular_props;
  apr_hash_t *base_props = NULL, *working_props = NULL;

  /* Skip if we're in a conflicted tree. */
  if (db->skip_this)
    {
      db->bump_info->skipped = TRUE;

      /* Allow the parent to complete its update. */
      SVN_ERR(maybe_bump_dir_info(db->edit_baton, db->bump_info, db->pool));

      return SVN_NO_ERROR;
    }

  SVN_ERR(svn_categorize_props(db->propchanges, &entry_props, &wc_props,
                               &regular_props, pool));

  /* An incomplete directory might have props which were supposed to be
     deleted but weren't.  Because the server sent us all the props we're
     supposed to have, any previous base props not in this list must be
     deleted (issue #1672). */
  if (db->was_incomplete)
    {
      const svn_wc_entry_t *entry;
      int i;
      apr_hash_t *props_to_delete;

      SVN_ERR(svn_wc__get_entry(&entry, db->edit_baton->db, db->local_abspath,
                                TRUE, svn_node_unknown, FALSE, pool, pool));
      if (entry == NULL)
        {
          base_props = apr_hash_make(pool);
          working_props = apr_hash_make(pool);
        }
      else
        {
          SVN_ERR(svn_wc__load_props(&base_props, &working_props, NULL,
                                     db->edit_baton->db, db->local_abspath,
                                     pool, pool));
        }

      /* Calculate which base props weren't also in the incoming
         propchanges. */
      props_to_delete = apr_hash_copy(pool, base_props);
      for (i = 0; i < regular_props->nelts; i++)
        {
          const svn_prop_t *prop;
          prop = &APR_ARRAY_IDX(regular_props, i, svn_prop_t);
          apr_hash_set(props_to_delete, prop->name,
                       APR_HASH_KEY_STRING, NULL);
        }

      /* Add these props to the incoming propchanges. */
      SVN_ERR(svn_iter_apr_hash(NULL, props_to_delete, add_prop_deletion,
                                regular_props, pool));
    }

  /* If this directory has property changes stored up, now is the time
     to deal with them. */
  if (regular_props->nelts || entry_props->nelts || wc_props->nelts)
    {
      /* Make a temporary log accumulator for dirprop changes.*/
      svn_stringbuf_t *dirprop_log = svn_stringbuf_create("", pool);

      if (regular_props->nelts)
        {
          /* If recording traversal info, then see if the
             SVN_PROP_EXTERNALS property on this directory changed,
             and record before and after for the change. */
            if (db->edit_baton->external_func)
            {
              const svn_prop_t *change = externals_prop_changed(regular_props);

              if (change)
                {
                  const svn_string_t *new_val_s = change->value;
                  const svn_string_t *old_val_s;

                  SVN_ERR(svn_wc__internal_propget(
                           &old_val_s, db->edit_baton->db, db->local_abspath,
                           SVN_PROP_EXTERNALS, db->pool, db->pool));

                  if ((new_val_s == NULL) && (old_val_s == NULL))
                    ; /* No value before, no value after... so do nothing. */
                  else if (new_val_s && old_val_s
                           && (svn_string_compare(old_val_s, new_val_s)))
                    ; /* Value did not change... so do nothing. */
                  else if (old_val_s || new_val_s)
                    /* something changed, record the change */
                    {
                      SVN_ERR((db->edit_baton->external_func)(
                                           db->edit_baton->external_baton,
                                           db->local_abspath,
                                           old_val_s,
                                           new_val_s,
                                           db->ambient_depth,
                                           db->pool));
                    }
                }
            }

          /* Merge pending properties into temporary files (ignoring
             conflicts). */
          SVN_ERR_W(svn_wc__merge_props(&dirprop_log,
                                        &prop_state,
                                        eb->db,
                                        db->local_abspath,
                                        db->local_abspath,
                                        NULL, /* left_version */
                                        NULL, /* right_version */
                                        NULL /* use baseprops */,
                                        base_props, working_props,
                                        regular_props, TRUE, FALSE,
                                        db->edit_baton->conflict_func,
                                        db->edit_baton->conflict_baton,
                                        db->edit_baton->cancel_func,
                                        db->edit_baton->cancel_baton,
                                        db->pool),
                    _("Couldn't do property merge"));
        }

      SVN_ERR(accumulate_entry_props(dirprop_log,
                                     db->local_abspath,
                                     NULL, db->path, entry_props, pool));

      /* Handle the wcprops. */
      if (wc_props && wc_props->nelts > 0)
        {
          SVN_ERR(svn_wc__db_base_set_dav_cache(eb->db, db->local_abspath,
                                                prop_hash_from_array(wc_props,
                                                                     pool),
                                                pool));
        }

      /* Add the dirprop loggy entries to the baton's log
         accumulator. */
      svn_stringbuf_appendstr(db->log_accum, dirprop_log);
    }

  /* Flush and run the log. */
  SVN_ERR(flush_log(db, pool));
  SVN_ERR(svn_wc__run_log(svn_wc__adm_retrieve_internal2(eb->db,
                                                         db->local_abspath,
                                                         pool),
                          pool));
  db->log_number = 0;

  /* We're done with this directory, so remove one reference from the
     bump information. This may trigger a number of actions. See
     maybe_bump_dir_info() for more information.  */
  SVN_ERR(maybe_bump_dir_info(db->edit_baton, db->bump_info, db->pool));

  /* Notify of any prop changes on this directory -- but do nothing if
     it's an added or skipped directory, because notification has already
     happened in that case - unless the add was obstructed by a dir
     scheduled for addition without history, in which case we handle
     notification here). */
  if (!db->already_notified && eb->notify_func)
    {
      svn_wc_notify_t *notify;
      svn_wc_notify_action_t action;

      if (db->accept_deleted)
        action = svn_wc_notify_update_update_deleted;
      else if (db->existed || db->add_existed)
        action = svn_wc_notify_exists;
      else
        action = svn_wc_notify_update_update;

      notify = svn_wc_create_notify(db->local_abspath, action, pool);
      notify->kind = svn_node_dir;
      notify->prop_state = prop_state;
      notify->revision = *db->edit_baton->target_revision;
      notify->old_revision = db->old_revision;

      eb->notify_func(db->edit_baton->notify_baton, notify, pool);
    }

  return SVN_NO_ERROR;
}


/* Common code for 'absent_file' and 'absent_directory'. */
static svn_error_t *
absent_file_or_dir(const char *path,
                   svn_node_kind_t kind,
                   void *parent_baton,
                   apr_pool_t *pool)
{
  const char *name = svn_dirent_basename(path, pool);
  const char *local_abspath;
  struct dir_baton *pb = parent_baton;
  struct edit_baton *eb = pb->edit_baton;
  svn_wc_entry_t tmp_entry;
  svn_boolean_t in_parent = (kind == svn_node_dir);

  local_abspath = svn_dirent_join(pb->local_abspath, name, pool);
  /* Extra check: an item by this name may not exist, but there may
     still be one scheduled for addition.  That's a genuine
     tree-conflict.  */

  {
    const svn_wc_entry_t *entry;
    svn_boolean_t hidden;
    SVN_ERR(svn_wc__get_entry(&entry, eb->db, local_abspath, TRUE, kind,
                              in_parent, pool, pool));

    if (entry)
      SVN_ERR(svn_wc__entry_is_hidden(&hidden, entry));

    /* ### BH: With WC-NG we should probably also check for replaced? */
    if (entry && !hidden && (entry->schedule == svn_wc_schedule_add))
      return svn_error_createf(
         SVN_ERR_WC_OBSTRUCTED_UPDATE, NULL,
         _("Failed to mark '%s' absent: item of the same name is already "
           "scheduled for addition"),
         svn_dirent_local_style(path, pool));
  }

  /* Immediately create an entry for the new item in the parent.  Note
     that the parent must already be either added or opened, and thus
     it's in an 'incomplete' state just like the new item.  */
  tmp_entry.kind = kind;

  /* Note that there may already exist a 'ghost' entry in the parent
     with the same name, in a 'deleted' state.  If so, it's fine to
     overwrite it... but we need to make sure we get rid of the
     'deleted' flag when doing so: */
  tmp_entry.deleted = FALSE;

  /* Post-update processing knows to leave this entry if its revision
     is equal to the target revision of the overall update. */
  tmp_entry.revision = *(eb->target_revision);

  /* And, of course, marking as absent is the whole point. */
  tmp_entry.absent = TRUE;

  return svn_wc__entry_modify2(eb->db, local_abspath, kind, in_parent,
                               &tmp_entry,
                               (SVN_WC__ENTRY_MODIFY_KIND    |
                                SVN_WC__ENTRY_MODIFY_REVISION |
                                SVN_WC__ENTRY_MODIFY_DELETED |
                                SVN_WC__ENTRY_MODIFY_ABSENT),
                               pool);
}


/* An svn_delta_editor_t function. */
static svn_error_t *
absent_file(const char *path,
            void *parent_baton,
            apr_pool_t *pool)
{
  return absent_file_or_dir(path, svn_node_file, parent_baton, pool);
}


/* An svn_delta_editor_t function. */
static svn_error_t *
absent_directory(const char *path,
                 void *parent_baton,
                 apr_pool_t *pool)
{
  return absent_file_or_dir(path, svn_node_dir, parent_baton, pool);
}


/* Beginning at DIR_ABSPATH (from repository with uuid DIR_REPOS_UUID and
   with repos_relpath dir_repos_relpath) within a working copy, search the
   working copy for an pre-existing versioned file which is exactly equal
   to COPYFROM_PATH@COPYFROM_REV.

   If the file isn't found, set *RETURN_ABSPATH to NULL.

   If the file is found, return the absolute path to it in
   *RETURN_ABSPATH.

   ### With a centralized datastore this becomes much easier. For now we
   ### keep the old algorithm because the result is also used for copying
   ### local changes. This support can probably be removed once we have real
   ### local file moves.
*/
static svn_error_t *
locate_copyfrom(svn_wc__db_t *db,
                const char *copyfrom_path,
                svn_revnum_t copyfrom_rev,
                const char *dir_abspath,
                const char *dir_repos_uuid,
                const char *dir_repos_relpath,
                const char **return_abspath,
                apr_pool_t *result_pool,
                apr_pool_t *scratch_pool)
{
  const char *ancestor_abspath, *ancestor_relpath;
  const char *copyfrom_relpath, *copyfrom_dir, *copyfrom_basename;
  apr_size_t levels_up;
  const char *file_abspath;
  svn_error_t *err;

  SVN_ERR_ASSERT(dir_repos_relpath && dir_repos_uuid);
  SVN_ERR_ASSERT(copyfrom_path[0] == '/');

  /* Be pessimistic.  This function is basically a series of tests
     that gives dozens of ways to fail our search, returning
     SVN_NO_ERROR in each case.  If we make it all the way to the
     bottom, we have a real discovery to return. */
  *return_abspath = NULL;

  copyfrom_relpath = copyfrom_path+1; /* Skip the initial '/' */
  svn_uri_split(copyfrom_relpath, &copyfrom_dir, &copyfrom_basename,
                scratch_pool);

  /* Subtract the dest_dir's URL from the repository "root" URL to get
     the absolute FS path represented by dest_dir. */

  /* Find nearest FS ancestor dir of current FS path and copyfrom_parent */
  ancestor_relpath = svn_uri_get_longest_ancestor(dir_repos_relpath,
                                                  copyfrom_relpath,
                                                  scratch_pool);
  if (strlen(ancestor_relpath) == 0)
    return SVN_NO_ERROR;

  /* Move 'up' the working copy to what ought to be the common ancestor dir. */
  levels_up = svn_path_component_count(dir_repos_relpath)
              - svn_path_component_count(ancestor_relpath);

  /* Walk up the path dirent safe */
  ancestor_abspath = dir_abspath;
  while (levels_up-- > 0)
    ancestor_abspath = svn_dirent_dirname(ancestor_abspath, scratch_pool);

  /* Verify hypothetical ancestor */
  {
    const char *repos_relpath, *repos_uuid;

    err = svn_wc__db_scan_base_repos(&repos_relpath, NULL, &repos_uuid,
                                     db, ancestor_abspath,
                                     scratch_pool, scratch_pool);

    if (err && ((err->apr_err == SVN_ERR_WC_NOT_WORKING_COPY) ||
                (err->apr_err == SVN_ERR_WC_PATH_FOUND)))
      {
        svn_error_clear(err);
        return SVN_NO_ERROR;
      }
    else
      SVN_ERR(err);

    /* If we got this far, we know that the ancestor dir exists, and
       that it's a working copy too.  But is it from the same
       repository?  And does it represent the URL we expect it to? */
    if (strcmp(dir_repos_uuid, repos_uuid) != 0)
      return SVN_NO_ERROR;

    if (strcmp(ancestor_relpath, repos_relpath) != 0)
      return SVN_NO_ERROR;
  }

  /* Add the remaining components to cwd, then add the remaining relpath to
     where we hope the copyfrom_relpath file exists. */
  file_abspath = svn_dirent_join(ancestor_abspath,
                                 svn_dirent_skip_ancestor(ancestor_relpath,
                                                          copyfrom_relpath),
                                 scratch_pool);

  /* Verify file in expected location */
  {
    svn_node_kind_t kind;
    svn_revnum_t addition_rev = SVN_INVALID_REVNUM;
    const char *repos_relpath, *repos_uuid;

    /* First: does the proposed file path even exist? */
    SVN_ERR(svn_io_check_path(file_abspath, &kind, scratch_pool));
    if (kind != svn_node_file)
      return SVN_NO_ERROR;

    /* Next: is the file under version control?   */
    err = svn_wc__db_scan_base_repos(&repos_relpath, NULL, &repos_uuid,
                                     db, file_abspath,
                                     scratch_pool, scratch_pool);

    if (err && ((err->apr_err == SVN_ERR_WC_NOT_WORKING_COPY) ||
                (err->apr_err == SVN_ERR_WC_PATH_NOT_FOUND)))
      {
        svn_error_clear(err);

        /* ### Our entries handling made us handle the following
               scenario: An older version of a file was copied at
               exactly the expected location. Reproduced this behavior
               until we can really querry the entire workingcopy. */

        err = svn_wc__db_scan_addition(NULL, NULL, NULL, NULL, NULL,
                                       &repos_relpath, NULL, &repos_uuid,
                                       &addition_rev, db, file_abspath,
                                       scratch_pool, scratch_pool);

        if (err && ((err->apr_err == SVN_ERR_WC_NOT_WORKING_COPY ||
              (err->apr_err == SVN_ERR_WC_PATH_NOT_FOUND))))
          {
            svn_error_clear(err);

            return SVN_NO_ERROR;
          }
        else
          SVN_ERR(err);
      }
    else
      SVN_ERR(err);

    if (strcmp(dir_repos_uuid, repos_uuid))
      return SVN_NO_ERROR;

    if (strcmp(copyfrom_relpath, repos_relpath))
      return SVN_NO_ERROR;

    if (SVN_IS_VALID_REVNUM(addition_rev) && addition_rev == copyfrom_rev)
      {
        /* We found the right file as copy source */
        *return_abspath = apr_pstrdup(result_pool, file_abspath);
        return SVN_NO_ERROR;
      }
  }

  /* Do we actually have valid revisions for the file?  (See Issue
     #2977.) */
  {
    svn_revnum_t wc_rev, change_rev;

    SVN_ERR(svn_wc__db_base_get_info(NULL, NULL, &wc_rev, NULL, NULL, NULL,
                                     &change_rev, NULL, NULL, NULL, NULL, NULL,
                                 NULL, NULL, NULL, db, file_abspath,
                                 scratch_pool, scratch_pool));

    if (!SVN_IS_VALID_REVNUM(wc_rev) || !SVN_IS_VALID_REVNUM(change_rev))
      return SVN_NO_ERROR;
  
    /* Do we have the the right *version* of the file? */
    if (! ((change_rev <= copyfrom_rev) && (copyfrom_rev <= wc_rev)))
      return SVN_NO_ERROR;
  }

  /* Success!  We found the exact file we wanted! */
  *return_abspath = apr_pstrdup(result_pool, file_abspath);

  return SVN_NO_ERROR;
}


/* Given a set of properties PROPS_IN, find all regular properties
   and shallowly copy them into a new set (allocate the new set in
   POOL, but the set's members retain their original allocations). */
static apr_hash_t *
copy_regular_props(apr_hash_t *props_in,
                   apr_pool_t *pool)
{
  apr_hash_t *props_out = apr_hash_make(pool);
  apr_hash_index_t *hi;

  for (hi = apr_hash_first(pool, props_in); hi; hi = apr_hash_next(hi))
    {
      const void *key;
      void *val;
      const char *propname;
      svn_string_t *propval;
      apr_hash_this(hi, &key, NULL, &val);
      propname = key;
      propval = val;

      if (svn_property_kind(NULL, propname) == svn_prop_regular_kind)
        apr_hash_set(props_out, propname, APR_HASH_KEY_STRING, propval);
    }
  return props_out;
}


/* Do the "with history" part of add_file().

   Attempt to locate COPYFROM_PATH@COPYFROM_REV within the existing
   working copy.  If found, copy it to PATH, and install it as a
   normal versioned file.  (Local edits are copied as well.)  If not
   found, then resort to fetching the file in a special RA request.

   After the file is fully installed, call the editor's open_file() on
   it, so that any subsequent apply_textdelta() commands coming from
   the server can further alter the file.
*/
static svn_error_t *
add_file_with_history(const char *path,
                      struct dir_baton *pb,
                      const char *copyfrom_path,
                      svn_revnum_t copyfrom_rev,
                      struct file_baton *tfb,
                      apr_pool_t *pool)
{
  struct edit_baton *eb = pb->edit_baton;
  svn_wc_adm_access_t *adm_access;
  apr_hash_t *base_props, *working_props;
  svn_error_t *err;
  svn_stream_t *copied_stream;
  const char *temp_dir_path;
  const char *src_local_abspath;
  svn_wc__db_t *db = eb->db;
  const char *dir_repos_relpath, *dir_repos_root, *dir_repos_uuid;

  /* The file_pool can stick around for a *long* time, so we want to
     use a subpool for any temporary allocations. */
  apr_pool_t *subpool = svn_pool_create(pool);

  tfb->added_with_history = TRUE;

  /* Attempt to locate the copyfrom_path in the working copy first. */

  SVN_ERR(svn_wc__db_scan_base_repos(&dir_repos_relpath, &dir_repos_root,
                                     &dir_repos_uuid, db, pb->local_abspath,
                                     subpool, subpool));

  err = locate_copyfrom(eb->db, copyfrom_path, copyfrom_rev, pb->local_abspath,
                        dir_repos_uuid, dir_repos_relpath,
                        &src_local_abspath, subpool, subpool);

  if (err && err->apr_err == SVN_ERR_WC_COPYFROM_PATH_NOT_FOUND)
    svn_error_clear(err);
  else
    SVN_ERR(err);

  SVN_ERR(svn_wc_adm_retrieve(&adm_access, pb->edit_baton->adm_access,
                              pb->path, subpool));

  SVN_ERR(svn_wc__db_temp_wcroot_tempdir(&temp_dir_path, db, pb->local_abspath,
                                         subpool, subpool));
  SVN_ERR(svn_stream_open_unique(&copied_stream,
                                 &tfb->copied_text_base,
                                 temp_dir_path,
                                 svn_io_file_del_none,
                                 pool, pool));

  /* Compute a checksum for the stream as we write stuff into it.
     ### this is temporary. in many cases, we already *know* the checksum
     ### since it is a copy. */
  copied_stream = svn_stream_checksummed2(copied_stream,
                                          NULL, &tfb->copied_base_checksum,
                                          svn_checksum_md5,
                                          FALSE, pool);

  if (src_local_abspath != NULL) /* Found a file to copy */
    {
      /* Copy the existing file's text-base over to the (temporary)
         new text-base, where the file baton expects it to be.  Get
         the text base and props from the usual place or from the
         revert place, depending on scheduling. */
      svn_stream_t *source_text_base;
      const svn_wc_entry_t *src_entry;

      SVN_ERR(svn_wc__get_entry(&src_entry, db, src_local_abspath, FALSE,
                            svn_node_file, FALSE, subpool, subpool));

      if (src_entry->schedule == svn_wc_schedule_replace
          && src_entry->copyfrom_url)
        {
          SVN_ERR(svn_wc__get_revert_contents(&source_text_base, db,
                                              src_local_abspath, subpool,
                                              subpool));

          SVN_ERR(svn_wc__load_props(NULL, NULL, &base_props, db,
                                     src_local_abspath, pool, subpool));
          /* The old working props are lost, just like the old
             working file text is.  Just use the base props. */
          working_props = base_props;
        }
      else
        {
          SVN_ERR(svn_wc__get_pristine_contents(&source_text_base, db,
                                                src_local_abspath,
                                                subpool, subpool));
          SVN_ERR(svn_wc__load_props(&base_props, &working_props, NULL, db,
                                     src_local_abspath, pool, subpool));
        }

      SVN_ERR(svn_stream_copy3(source_text_base, copied_stream,
                               eb->cancel_func, eb->cancel_baton, pool));
    }
  else  /* Couldn't find a file to copy  */
    {
      /* Fall back to fetching it from the repository instead. */

      if (! eb->fetch_func)
        return svn_error_create(SVN_ERR_WC_INVALID_OP_ON_CWD, NULL,
                                _("No fetch_func supplied to update_editor"));

      /* Fetch the repository file's text-base and base-props;
         svn_stream_close() automatically closes the text-base file for us. */

      /* copyfrom_path is a absolute path, fetch_func requires a path relative
         to the root of the repository so skip the first '/'. */
      SVN_ERR(eb->fetch_func(eb->fetch_baton, copyfrom_path + 1, copyfrom_rev,
                             copied_stream,
                             NULL, &base_props, pool));
      SVN_ERR(svn_stream_close(copied_stream));
      working_props = base_props;
    }

  /* Loop over whatever props we have in memory, and add all
     regular props to hashes in the baton. Skip entry and wc
     properties, these are only valid for the original file. */
  tfb->copied_base_props = copy_regular_props(base_props, pool);
  tfb->copied_working_props = copy_regular_props(working_props, pool);

  if (src_local_abspath != NULL)
    {
      /* If we copied an existing file over, we need to copy its
         working text too, to preserve any local mods.  (We already
         read its working *props* into tfb->copied_working_props.) */
      svn_boolean_t text_changed;

      SVN_ERR(svn_wc__text_modified_internal_p(&text_changed, eb->db,
                                               src_local_abspath, FALSE,
                                               TRUE, subpool));

      if (text_changed)
        {
          /* Make a unique file name for the copied_working_text. */
          SVN_ERR(svn_wc_create_tmp_file2(NULL, &tfb->copied_working_text,
                                          svn_wc_adm_access_path(adm_access),
                                          svn_io_file_del_none,
                                          pool));

          SVN_ERR(svn_io_copy_file(src_local_abspath, tfb->copied_working_text, TRUE,
                                   subpool));
        }
    }

  svn_pool_destroy(subpool);

  return SVN_NO_ERROR;
}


/* An svn_delta_editor_t function. */
static svn_error_t *
add_file(const char *path,
         void *parent_baton,
         const char *copyfrom_path,
         svn_revnum_t copyfrom_rev,
         apr_pool_t *pool,
         void **file_baton)
{
  struct dir_baton *pb = parent_baton;
  struct edit_baton *eb = pb->edit_baton;
  struct file_baton *fb;
  const svn_wc_entry_t *entry;
  svn_node_kind_t kind;
  apr_pool_t *subpool;
  svn_boolean_t already_conflicted;
  svn_error_t *err;

  /* Semantic check.  Either both "copyfrom" args are valid, or they're
     NULL and SVN_INVALID_REVNUM.  A mixture is illegal semantics. */
  SVN_ERR_ASSERT((copyfrom_path && SVN_IS_VALID_REVNUM(copyfrom_rev))
                 || (!copyfrom_path &&
                     !SVN_IS_VALID_REVNUM(copyfrom_rev)));

  SVN_ERR(make_file_baton(&fb, pb, path, TRUE, pool));
  *file_baton = fb;

  if (pb->skip_descendants)
    {
      if (!pb->skip_this)
        SVN_ERR(remember_skipped_tree(eb, fb->local_abspath));

      fb->skip_this = TRUE;
      fb->already_notified = TRUE;

      return SVN_NO_ERROR;
    }

  SVN_ERR(check_path_under_root(pb->local_abspath, fb->name, pool));

  fb->deleted = pb->accept_deleted;

  /* The file_pool can stick around for a *long* time, so we want to
     use a subpool for any temporary allocations. */
  subpool = svn_pool_create(pool);

  SVN_ERR(svn_io_check_path(fb->local_abspath, &kind, subpool));
  err = svn_wc__get_entry(&entry, eb->db, fb->local_abspath, TRUE,
                          svn_node_unknown, FALSE, subpool, subpool);

  if (err && err->apr_err == SVN_ERR_NODE_UNEXPECTED_KIND)
    svn_error_clear(err);
  else
    SVN_ERR(err);

  if (entry)
    {
      svn_boolean_t hidden;
      SVN_ERR(svn_wc__db_node_hidden(&hidden, eb->db, fb->local_abspath, pool));

      if (hidden)
        entry = NULL;
    }

  /* Is this path a conflict victim? */
  SVN_ERR(node_already_conflicted(&already_conflicted, eb->db,
                                  fb->local_abspath, pool));
  if (already_conflicted)
    {
      SVN_ERR(remember_skipped_tree(eb, fb->local_abspath));

      fb->skip_this = TRUE;
      fb->already_notified = TRUE;

      if (eb->notify_func)
        eb->notify_func(eb->notify_baton,
                        svn_wc_create_notify(fb->local_abspath,
                                             svn_wc_notify_skip,
                                             subpool),
                        subpool);

      svn_pool_destroy(subpool);

      return SVN_NO_ERROR;
    }

  /* An obstructing dir (or unknown, just to be paranoid) is an error. */
  if (kind == svn_node_dir || kind == svn_node_unknown)
    return svn_error_createf(
       SVN_ERR_WC_OBSTRUCTED_UPDATE, NULL,
       _("Failed to add file '%s': a non-file object of the "
         "same name already exists"),
       svn_dirent_local_style(fb->local_abspath, subpool));

  /* An unversioned, obstructing file may be OK. */
  if (!entry && kind == svn_node_file)
    {
      if (eb->allow_unver_obstructions)
        fb->existed = TRUE;
      else
        {
          if (eb->notify_func)
            {
              svn_wc_notify_t *notify = 
                      svn_wc_create_notify(fb->local_abspath,
                                           svn_wc_notify_update_obstruction,
                                           pool);

              notify->kind = svn_node_file;
              eb->notify_func(eb->notify_baton, notify, pool);
            }
          return svn_error_createf(
             SVN_ERR_WC_OBSTRUCTED_UPDATE, NULL,
             _("Failed to add file '%s': an unversioned "
               "file of the same name already exists"),
             svn_dirent_local_style(fb->local_abspath, subpool));
        }
    }

  /* What to do with a versioned or schedule-add file:

     If the UUID doesn't match the parent's, or the URL isn't a child of
     the parent dir's URL, it's an error.

     A file already added without history is OK.  Set add_existed so that
     user notification is delayed until after any text or prop conflicts
     have been found.

     A file added with history is a tree conflict.

     sussman sez: If we're trying to add a file that's already in
     `entries' (but not on disk), that's okay.  It's probably because
     the user deleted the working version and ran 'svn up' as a means
     of getting the file back.

     It certainly doesn't hurt to re-add the file.  We can't possibly
     get the entry showing up twice in `entries', since it's a hash;
     and we know that we won't lose any local mods.  Let the existing
     entry be overwritten. */
  if (entry)
    {
      const svn_wc_entry_t *parent_entry;
      SVN_ERR(svn_wc__get_entry(&parent_entry, eb->db, pb->local_abspath,
                                FALSE, svn_node_dir, FALSE, pool, pool));

      if (entry->uuid /* UUID is optional for file entries. */
          && strcmp(entry->uuid, parent_entry->uuid) != 0)
        return svn_error_createf(
           SVN_ERR_WC_OBSTRUCTED_UPDATE, NULL,
           _("UUID mismatch: existing file '%s' was checked out "
             "from a different repository"),
           svn_dirent_local_style(fb->local_abspath, pool));

      if (!eb->switch_url && fb->new_URL && entry->url
          && strcmp(fb->new_URL, entry->url) != 0)
        return svn_error_createf(
           SVN_ERR_WC_OBSTRUCTED_UPDATE, NULL,
           _("URL '%s' of existing file '%s' does not match "
             "expected URL '%s'"),
           entry->url, svn_dirent_local_style(fb->local_abspath, pool),
           fb->new_URL);
    }

  if (entry && kind == svn_node_file)
    {
      if ((entry->schedule == svn_wc_schedule_add
           || entry->schedule == svn_wc_schedule_replace)
          && !entry->copied) /* added without history */
        fb->add_existed = TRUE;
      else
        {
          svn_wc_conflict_description2_t *tree_conflict;

          SVN_ERR(check_tree_conflict(&tree_conflict, eb, fb->local_abspath,
                                      pb->log_accum,
                                      svn_wc_conflict_action_add,
                                      svn_node_file, fb->new_URL,
                                      pb->accept_deleted, subpool));

          if (tree_conflict != NULL)
            {
              /* Record the conflict so that the file is skipped silently
                 by the other callbacks. */
              SVN_ERR(remember_skipped_tree(eb, fb->local_abspath));
              fb->skip_this = TRUE;
              fb->already_notified = TRUE;

              if (eb->notify_func)
                eb->notify_func(eb->notify_baton,
                                svn_wc_create_notify(
                                       fb->local_abspath,
                                       svn_wc_notify_tree_conflict,
                                       subpool),
                                subpool);

              return SVN_NO_ERROR;
            }
        }
    }

  svn_pool_destroy(subpool);

  /* Now, if this is an add with history, do the history part. */
  if (copyfrom_path)
    {
      SVN_ERR(add_file_with_history(path, pb, copyfrom_path, copyfrom_rev,
                                    fb, pool));
    }

  return SVN_NO_ERROR;
}


/* An svn_delta_editor_t function. */
static svn_error_t *
open_file(const char *path,
          void *parent_baton,
          svn_revnum_t base_revision,
          apr_pool_t *pool,
          void **file_baton)
{
  struct dir_baton *pb = parent_baton;
  struct edit_baton *eb = pb->edit_baton;
  struct file_baton *fb;
  svn_node_kind_t kind;
  svn_boolean_t already_conflicted;
  svn_wc_conflict_description2_t *tree_conflict;

  /* the file_pool can stick around for a *long* time, so we want to use
     a subpool for any temporary allocations. */
  apr_pool_t *subpool = svn_pool_create(pool);

  SVN_ERR(make_file_baton(&fb, pb, path, FALSE, pool));
  *file_baton = fb;

  if (pb->skip_descendants)
    {
      if (!pb->skip_this)
        SVN_ERR(remember_skipped_tree(eb, fb->local_abspath));

      fb->skip_this = TRUE;
      fb->already_notified = TRUE;

      return SVN_NO_ERROR;
    }

  SVN_ERR(check_path_under_root(pb->local_abspath, fb->name, subpool));

  SVN_ERR(svn_io_check_path(fb->local_abspath, &kind, subpool));

  /* Sanity check. */

  /* If replacing, make sure the .svn entry already exists. */
  SVN_ERR(svn_wc__db_read_info(NULL, NULL, &fb->old_revision, NULL, NULL,
                               NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                               NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                               NULL, NULL, NULL, NULL, NULL,
                               eb->db, fb->local_abspath, FALSE, subpool));

  /* Is this path a conflict victim? */
  SVN_ERR(node_already_conflicted(&already_conflicted, eb->db,
                                  fb->local_abspath, pool));
  if (already_conflicted)
    {
      SVN_ERR(remember_skipped_tree(eb, fb->local_abspath));

      fb->skip_this = TRUE;
      fb->already_notified = TRUE;

      if (eb->notify_func)
        eb->notify_func(eb->notify_baton,
                        svn_wc_create_notify(fb->local_abspath,
                                             svn_wc_notify_skip,
                                             subpool),
                        subpool);

      svn_pool_destroy(subpool);

      return SVN_NO_ERROR;
    }

  fb->deleted = pb->accept_deleted;

  /* Is this path the victim of a newly-discovered tree conflict? */
  SVN_ERR(check_tree_conflict(&tree_conflict, eb, fb->local_abspath,
                              pb->log_accum,
                              svn_wc_conflict_action_edit,
                              svn_node_file, fb->new_URL, 
                              pb->accept_deleted, pool));

  if (tree_conflict)
    {
      if (tree_conflict->reason == svn_wc_conflict_reason_deleted ||
          tree_conflict->reason == svn_wc_conflict_reason_replaced)
        {
          fb->deleted = TRUE;
        }
      else
        SVN_ERR(remember_skipped_tree(eb, fb->local_abspath));

      if (!fb->deleted)
        fb->skip_this = TRUE;

      fb->already_notified = TRUE;
      if (eb->notify_func)
        eb->notify_func(eb->notify_baton,
                        svn_wc_create_notify(fb->local_abspath,
                                             svn_wc_notify_tree_conflict,
                                             pool),
                         pool);
    }

  svn_pool_destroy(subpool);

  return SVN_NO_ERROR;
}

/* For the given PATH, fill out OLD_TEXT_BASE with the permanent text-base
   path, or (if the entry is replaced with history) to the permanent
   revert-base path.

   If REPLACED_P is non-NULL, set *REPLACED_P to whether or not the
   entry is replaced (which also implies whether or not it needs to
   use the revert base).  If CHECKSUM_P is non-NULL and the path
   already has an entry, set *CHECKSUM_P to the entry's checksum.

   ROOT_ACCESS is an access baton which can be used to find associated
   batons for the directory that PATH resides within.

   Use SCRATCH_POOL for temporary allocation and for *CHECKSUM_P (if
   applicable), but allocate OLD_TEXT_BASE in RESULT_POOL. */
static svn_error_t *
choose_base_paths(const char **old_text_base,
                  const char **checksum_p,
                  svn_boolean_t *replaced_p,
                  svn_wc__db_t *db,
                  const char *local_abspath,
                  apr_pool_t *result_pool,
                  apr_pool_t *scratch_pool)
{
  const svn_wc_entry_t *entry;
  svn_boolean_t replaced;

  SVN_ERR(svn_wc__get_entry(&entry, db, local_abspath, TRUE, svn_node_file,
                            FALSE, scratch_pool, scratch_pool));

  replaced = entry && entry->schedule == svn_wc_schedule_replace;
  /* ### Should use pristine api here */
  if (replaced)
    SVN_ERR(svn_wc__text_revert_path(old_text_base,
                                     db, local_abspath, result_pool));
  else
    SVN_ERR(svn_wc__text_base_path(old_text_base,
                                   db, local_abspath, FALSE, result_pool));

  if (checksum_p)
    {
      *checksum_p = NULL;
      if (entry)
        *checksum_p = entry->checksum;
    }
  if (replaced_p)
    *replaced_p = replaced;

  return SVN_NO_ERROR;
}


/* An svn_delta_editor_t function. */
static svn_error_t *
apply_textdelta(void *file_baton,
                const char *base_checksum,
                apr_pool_t *pool,
                svn_txdelta_window_handler_t *handler,
                void **handler_baton)
{
  struct file_baton *fb = file_baton;
  apr_pool_t *handler_pool = svn_pool_create(fb->pool);
  struct handler_baton *hb = apr_pcalloc(handler_pool, sizeof(*hb));
  svn_error_t *err;
  const char *checksum;
  svn_boolean_t replaced;
  svn_stream_t *source;
  svn_stream_t *target;

  if (fb->skip_this)
    {
      *handler = svn_delta_noop_window_handler;
      *handler_baton = NULL;
      return SVN_NO_ERROR;
    }

  fb->received_textdelta = TRUE;

  /* Before applying incoming svndiff data to text base, make sure
     text base hasn't been corrupted, and that its checksum
     matches the expected base checksum. */

  SVN_ERR(choose_base_paths(&fb->text_base_path,
                            &checksum, &replaced,
                            fb->edit_baton->db, fb->local_abspath,
                            fb->pool, pool));

  /* The incoming delta is targeted against BASE_CHECKSUM. Make sure that
     it matches our recorded checksum. We cannot do this test for replaced
     nodes -- that checksum is missing or the checksum of the replacement.  */
  if (!replaced && checksum && base_checksum
      && strcmp(base_checksum, checksum) != 0)
    {
      return svn_error_createf(SVN_ERR_WC_CORRUPT_TEXT_BASE, NULL,
                     _("Checksum mismatch for '%s':\n"
                       "   expected:  %s\n"
                       "   recorded:  %s\n"),
                     svn_dirent_local_style(fb->path, pool),
                     base_checksum, checksum);
    }

  /* Open the text base for reading, unless this is an added file. */

  /*
     kff todo: what we really need to do here is:

     1. See if there's a file or dir by this name already here.
     2. See if it's under revision control.
     3. If both are true, open text-base.
     4. If only 1 is true, bail, because we can't go destroying user's
        files (or as an alternative to bailing, move it to some tmp
        name and somehow tell the user, but communicating with the
        user without erroring is a whole callback system we haven't
        finished inventing yet.)
  */

  if (! fb->added)
    {
      if (replaced)
        SVN_ERR(svn_wc__get_revert_contents(&source, fb->edit_baton->db,
                                            fb->local_abspath,
                                            handler_pool, handler_pool));
      else
        SVN_ERR(svn_wc__get_pristine_contents(&source, fb->edit_baton->db,
                                             fb->local_abspath,
                                             handler_pool, handler_pool));
    }
  else
    {
      if (fb->copied_text_base)
        SVN_ERR(svn_stream_open_readonly(&source, fb->copied_text_base,
                                         handler_pool, handler_pool));
      else
        source = svn_stream_empty(handler_pool);
    }

  /* If we don't have a local checksum, use the ra provided checksum */
  if (replaced || !checksum)
    checksum = base_checksum;

  /* Checksum the text base while applying deltas */
  if (checksum)
    {
      SVN_ERR(svn_checksum_parse_hex(&hb->expected_source_checksum,
                                     svn_checksum_md5, checksum,
                                     handler_pool));

      /* Wrap stream and store reference to allow calculating */
      hb->source_checksum_stream =
                 source = svn_stream_checksummed2(source,
                                                  &hb->actual_source_checksum,
                                                  NULL, svn_checksum_md5,
                                                  TRUE, handler_pool);
    }

  /* Open the text base for writing (this will get us a temporary file).  */
  {
    err = svn_wc__open_writable_base(&target, &hb->work_path, fb->path,
                                     replaced /* need_revert_base */,
                                     handler_pool, pool);
    if (err)
      {
        svn_pool_destroy(handler_pool);
        return err;
      }
  }

  /* Prepare to apply the delta.  */
  svn_txdelta_apply(source, target,
                    hb->digest, hb->work_path /* error_info */,
                    handler_pool,
                    &hb->apply_handler, &hb->apply_baton);

  hb->pool = handler_pool;
  hb->fb = fb;

  /* We're all set.  */
  *handler_baton = hb;
  *handler = window_handler;

  return SVN_NO_ERROR;
}


/* An svn_delta_editor_t function. */
static svn_error_t *
change_file_prop(void *file_baton,
                 const char *name,
                 const svn_string_t *value,
                 apr_pool_t *pool)
{
  struct file_baton *fb = file_baton;
  struct edit_baton *eb = fb->edit_baton;
  svn_prop_t *propchange;

  if (fb->skip_this)
    return SVN_NO_ERROR;

  /* Push a new propchange to the file baton's array of propchanges */
  propchange = apr_array_push(fb->propchanges);
  propchange->name = apr_pstrdup(fb->pool, name);
  propchange->value = value ? svn_string_dup(value, fb->pool) : NULL;

  /* Special case: If use-commit-times config variable is set we
     cache the last-changed-date propval so we can use it to set
     the working file's timestamp. */
  if (eb->use_commit_times
      && (strcmp(name, SVN_PROP_ENTRY_COMMITTED_DATE) == 0)
      && value)
    fb->last_changed_date = apr_pstrdup(fb->pool, value->data);

  return SVN_NO_ERROR;
}


/* Write log commands to merge PROP_CHANGES into the existing
   properties of FILE_PATH.  PROP_CHANGES can contain regular
   properties as well as entryprops and wcprops.  Update *PROP_STATE
   to reflect the result of the regular prop merge.  Make *LOCK_STATE
   reflect the possible removal of a lock token from FILE_PATH's
   entryprops.  BASE_PROPS and WORKING_PROPS are hashes of the base and
   working props of the file; if NULL they are read from the wc.

   CONFICT_FUNC/BATON is a callback which allows the client to
   possibly resolve a property conflict interactively.

   ADM_ACCESS is the access baton for FILE_PATH.  Append log commands to
   LOG_ACCUM.  Use POOL for temporary allocations. */
static svn_error_t *
merge_props(svn_stringbuf_t *log_accum,
            svn_wc_notify_state_t *prop_state,
            svn_wc_notify_lock_state_t *lock_state,
            svn_wc__db_t *db,
            const char *file_abspath,
            const char *dir_abspath,
            const svn_wc_conflict_version_t *left_version,
            const svn_wc_conflict_version_t *right_version,
            const apr_array_header_t *prop_changes,
            apr_hash_t *base_props,
            apr_hash_t *working_props,
            svn_wc_conflict_resolver_func_t conflict_func,
            void *conflict_baton,
            svn_cancel_func_t cancel_func,
            void *cancel_baton,
            apr_pool_t *pool)
{
  apr_array_header_t *regular_props = NULL, *wc_props = NULL,
    *entry_props = NULL;

  /* Sort the property list into three arrays, based on kind. */
  SVN_ERR(svn_categorize_props(prop_changes, &entry_props, &wc_props,
                               &regular_props, pool));

  /* Always initialize to unknown state. */
  *prop_state = svn_wc_notify_state_unknown;

  /* Merge the 'regular' props into the existing working proplist. */
  if (regular_props)
    {
      /* This will merge the old and new props into a new prop db, and
         write <cp> commands to the logfile to install the merged
         props.  */
      SVN_ERR(svn_wc__merge_props(&log_accum,
                                  prop_state,
                                  db,
                                  file_abspath,
                                  dir_abspath,
                                  left_version,
                                  right_version,
                                  NULL /* update, not merge */,
                                  base_props,
                                  working_props,
                                  regular_props, TRUE, FALSE,
                                  conflict_func, conflict_baton,
                                  cancel_func, cancel_baton,
                                  pool));
    }

  /* If there are any ENTRY PROPS, make sure those get appended to the
     growing log as fields for the file's entry.

     Note that no merging needs to happen; these kinds of props aren't
     versioned, so if the property is present, we overwrite the value. */
  if (entry_props)
    SVN_ERR(accumulate_entry_props(log_accum, dir_abspath,
                                   lock_state, file_abspath, entry_props,
                                   pool));
  else
    *lock_state = svn_wc_notify_lock_state_unchanged;

  /* This writes a whole bunch of log commands to install wcprops.  */
  if (wc_props)
    SVN_ERR(svn_wc__db_base_set_dav_cache(db, file_abspath,
                                          prop_hash_from_array(wc_props, pool),
                                          pool));

  return SVN_NO_ERROR;
}

/* Append, to LOG_ACCUM, log commands to update the entry for NAME in
   ADM_ACCESS with a NEW_REVISION and a NEW_URL (if non-NULL), making sure
   the entry refers to a file and has no absent or deleted state.
   Use POOL for temporary allocations. */
static svn_error_t *
loggy_tweak_entry(svn_stringbuf_t *log_accum,
                  const char *local_abspath,
                  const char *dir_abspath,
                  svn_revnum_t new_revision,
                  const char *new_URL,
                  apr_pool_t *pool)
{
  /* Write log entry which will bump the revision number.  Also, just
     in case we're overwriting an existing phantom 'deleted' or
     'absent' entry, be sure to remove the hiddenness. */
  svn_wc_entry_t tmp_entry;
  apr_uint64_t modify_flags = SVN_WC__ENTRY_MODIFY_KIND
    | SVN_WC__ENTRY_MODIFY_REVISION
    | SVN_WC__ENTRY_MODIFY_DELETED
    | SVN_WC__ENTRY_MODIFY_ABSENT
    | SVN_WC__ENTRY_MODIFY_TEXT_TIME
    | SVN_WC__ENTRY_MODIFY_WORKING_SIZE;


  tmp_entry.revision = new_revision;
  tmp_entry.kind = svn_node_file;
  tmp_entry.deleted = FALSE;
  tmp_entry.absent = FALSE;
  /* Indicate the file was locally modified and we didn't get to
     calculate the true value, but we can't set it to UNKNOWN (-1),
     because that would indicate absense of this value.
     If it isn't locally modified,
     we'll overwrite with the actual value later. */
  tmp_entry.working_size = SVN_WC_ENTRY_WORKING_SIZE_UNKNOWN;
  /* The same is true for the TEXT_TIME field, except that that doesn't
     have an explicid 'changed' value, so we set the value to 'undefined'. */
  tmp_entry.text_time = 0;

  /* Possibly install a *non*-inherited URL in the entry. */
  if (new_URL)
    {
      tmp_entry.url = new_URL;
      modify_flags |= SVN_WC__ENTRY_MODIFY_URL;
    }

  return svn_error_return(
    svn_wc__loggy_entry_modify(&log_accum, dir_abspath,
                               local_abspath, &tmp_entry, modify_flags,
                               pool, pool));
}


/* This is the small planet.  It has the complex responsibility of
 * "integrating" a new revision of a file into a working copy.
 *
 * Given a file_baton FB for a file either already under version control, or
 * prepared (see below) to join version control, fully install a
 * new revision of the file.
 *
 * By "install", we mean: create a new text-base and prop-base, merge
 * any textual and property changes into the working file, and finally
 * update all metadata so that the working copy believes it has a new
 * working revision of the file.  All of this work includes being
 * sensitive to eol translation, keyword substitution, and performing
 * all actions accumulated to FB->DIR_BATON->LOG_ACCUM.
 *
 * If there's a new text base, NEW_TEXT_BASE_PATH must be the full
 * pathname of the new text base, somewhere in the administrative area
 * of the working file.  It will be installed as the new text base for
 * this file, and removed after a successful run of the generated log
 * commands.
 *
 * Set *CONTENT_STATE, *PROP_STATE and *LOCK_STATE to the state of the
 * contents, properties and repository lock, respectively, after the
 * installation.  If an error is returned, the value of these three
 * variables is undefined.
 *
 * ACTUAL_CHECKSUM is the checksum that was computed as we constructed
 * the (new) text base. That was performed during a txdelta apply, or
 * during a copy of an add-with-history.
 *
 * POOL is used for all bookkeeping work during the installation.
 */
static svn_error_t *
merge_file(svn_wc_notify_state_t *content_state,
           svn_wc_notify_state_t *prop_state,
           svn_wc_notify_lock_state_t *lock_state,
           struct file_baton *fb,
           const char *new_text_base_path,
           const svn_checksum_t *actual_checksum,
           apr_pool_t *pool)
{
  const char *parent_dir;
  struct edit_baton *eb = fb->edit_baton;
  svn_stringbuf_t *log_accum = svn_stringbuf_create("", pool);
  svn_wc_adm_access_t *adm_access;
  svn_boolean_t is_locally_modified;
  svn_boolean_t is_replaced = FALSE;
  svn_boolean_t magic_props_changed;
  enum svn_wc_merge_outcome_t merge_outcome = svn_wc_merge_unchanged;
  const svn_wc_entry_t *entry;
  svn_wc_conflict_version_t *left_version = NULL; /* ### Fill */
  svn_wc_conflict_version_t *right_version = NULL; /* ### Fill */
  const char *parent_abspath;

  /* Accumulated entry modifications. */
  svn_wc_entry_t tmp_entry;
  apr_uint64_t flags = 0;

  /*
     When this function is called on file F, we assume the following
     things are true:

         - The new pristine text of F, if any, is present at
           NEW_TEXT_BASE_PATH

         - The .svn/entries file still reflects the old version of F.

         - fb->old_text_base_path is the old pristine F.
           (This is only set if there's a new text base).

      The goal is to update the local working copy of F to reflect
      the changes received from the repository, preserving any local
      modifications.
  */

  /* Start by splitting the file path, getting an access baton for the parent,
     and an entry for the file if any. */
  svn_dirent_split(fb->path, &parent_dir, NULL, pool);
  parent_abspath = svn_dirent_dirname(fb->local_abspath, pool);
  SVN_ERR(svn_wc_adm_retrieve(&adm_access, eb->adm_access,
                              parent_dir, pool));

  SVN_ERR(svn_wc__get_entry(&entry, eb->db, fb->local_abspath, TRUE,
                            svn_node_file, FALSE, pool, pool));
  if (! entry && ! fb->added)
    return svn_error_createf(
        SVN_ERR_UNVERSIONED_RESOURCE, NULL,
        _("'%s' is not under version control"),
        svn_dirent_local_style(fb->local_abspath, pool));

  /* Determine if any of the propchanges are the "magic" ones that
     might require changing the working file. */
  magic_props_changed = svn_wc__has_magic_property(fb->propchanges);

  /* Set the new revision and URL in the entry and clean up some other
     fields. This clears DELETED from any prior versioned file with the
     same name (needed before attempting to install props).  */
  SVN_ERR(loggy_tweak_entry(log_accum, fb->local_abspath, parent_abspath,
                            *eb->target_revision, fb->new_URL, pool));

  /* Install all kinds of properties.  It is important to do this before
     any file content merging, since that process might expand keywords, in
     which case we want the new entryprops to be in place. */
  SVN_ERR(merge_props(log_accum, prop_state, lock_state, eb->db,
                      fb->local_abspath, parent_abspath,
                      left_version, right_version,
                      fb->propchanges,
                      fb->copied_base_props, fb->copied_working_props,
                      eb->conflict_func, eb->conflict_baton,
                      eb->cancel_func, eb->cancel_baton, pool));

  /* Has the user made local mods to the working file?
     Note that this compares to the current pristine file, which is
     different from fb->old_text_base_path if we have a replaced-with-history
     file.  However, in the case we had an obstruction, we check against the
     new text base. (And if we're doing an add-with-history and we've already
     saved a copy of a locally-modified file, then there certainly are mods.)

     Special case: The working file is referring to a file external? If so
                   then we must mark it as unmodified in order to avoid bogus
                   conflicts, since this file was added as a place holder to
                   merge externals item from the repository. */
  if (fb->copied_working_text)
    is_locally_modified = TRUE;
  else if (entry && entry->file_external_path
           && entry->schedule == svn_wc_schedule_add)
    is_locally_modified = FALSE;
  else if (! fb->existed)
    SVN_ERR(svn_wc__text_modified_internal_p(&is_locally_modified, eb->db,
                                             fb->local_abspath, FALSE, FALSE,
                                             pool));
  else if (new_text_base_path)
    {
      const char *new_text_base_abspath;

      SVN_ERR(svn_dirent_get_absolute(&new_text_base_abspath,
                                      new_text_base_path, pool));
      SVN_ERR(svn_wc__internal_versioned_file_modcheck(&is_locally_modified,
                                                       eb->db,
                                                       fb->local_abspath,
                                                       new_text_base_abspath,
                                                       FALSE, pool));
    }
  else
    is_locally_modified = FALSE;

  if (entry && entry->schedule == svn_wc_schedule_replace)
    is_replaced = TRUE;

  if (fb->add_existed)
    {
      /* Tweak schedule for the file's entry so it is no longer
         scheduled for addition. */
      tmp_entry.schedule = svn_wc_schedule_normal;
      flags |= (SVN_WC__ENTRY_MODIFY_SCHEDULE |
                SVN_WC__ENTRY_MODIFY_FORCE);
    }

  /* For 'textual' merging, we implement this matrix.

                          Text file                   Binary File
                         -----------------------------------------------
    "Local Mods" &&      | svn_wc_merge uses diff3, | svn_wc_merge     |
    (!fb->existed ||     | possibly makes backups & | makes backups,   |
     fb->add_existed)    | marks file as conflicted.| marks conflicted |
                         -----------------------------------------------
    "Local Mods" &&      |        Just leave obstructing file as-is.   |
    fb->existed          |                                             |
                         -----------------------------------------------
    No Mods              |        Just overwrite working file.         |
                         |                                             |
                         -----------------------------------------------
    File is Locally      |        Same as if 'No Mods' except we       |
    Deleted              |        don't move the new text base to      |
                         |        the working file location.           |
                         -----------------------------------------------
    File is Locally      |        Install the new text base.           |
    Replaced             |        Leave working file alone.            |
                         |                                             |
                         -----------------------------------------------

   So the first thing we do is figure out where we are in the
   matrix. */
  if (new_text_base_path)
    {
      if (is_replaced)
        {
          /* Nothing to do, the delete half of the local replacement will
             have already raised a tree conflict.  So we will just fall
             through to the installation of the new textbase. */
        }
      else if (! is_locally_modified)
        {
          if (!fb->deleted)
            /* If there are no local mods, who cares whether it's a text
               or binary file!  Just write a log command to overwrite
               any working file with the new text-base.  If newline
               conversion or keyword substitution is activated, this
               will happen as well during the copy.
               For replaced files, though, we want to merge in the changes
               even if the file is not modified compared to the (non-revert)
               text-base. */
            SVN_ERR(svn_wc__loggy_copy(&log_accum,
                                       svn_wc__adm_access_abspath(adm_access),
                                       new_text_base_path,
                                       fb->path, pool, pool));
        }
      else   /* working file or obstruction is locally modified... */
        {
          svn_node_kind_t wfile_kind = svn_node_unknown;

          SVN_ERR(svn_io_check_path(fb->local_abspath, &wfile_kind, pool));
          if (wfile_kind == svn_node_none && ! fb->added_with_history)
            {
              /* working file is missing?!
                 Just copy the new text-base to the file. */
              SVN_ERR(svn_wc__loggy_copy(&log_accum,
                                         svn_wc__adm_access_abspath(adm_access),
                                         new_text_base_path,
                                         fb->path, pool, pool));
            }
          else if (! fb->existed)
            /* Working file exists and has local mods
               or is scheduled for addition but is not an obstruction. */
            {
              /* Now we need to let loose svn_wc__merge_internal() to merge
                 the textual changes into the working file. */
              const char *oldrev_str, *newrev_str, *mine_str;
              const char *merge_left;
              svn_boolean_t delete_left = FALSE;
              const char *path_ext = "";

              /* If we have any file extensions we're supposed to
                 preserve in generated conflict file names, then find
                 this path's extension.  But then, if it isn't one of
                 the ones we want to keep in conflict filenames,
                 pretend it doesn't have an extension at all. */
              if (eb->ext_patterns && eb->ext_patterns->nelts)
                {
                  svn_path_splitext(NULL, &path_ext, fb->local_abspath, pool);
                  if (! (*path_ext
                         && svn_cstring_match_glob_list(path_ext,
                                                        eb->ext_patterns)))
                    path_ext = "";
                }

              /* Create strings representing the revisions of the
                 old and new text-bases. */
              /* Either an old version, or an add-with-history */
              if (fb->added_with_history)
                oldrev_str = apr_psprintf(pool, ".copied%s%s",
                                          *path_ext ? "." : "",
                                          *path_ext ? path_ext : "");
              else
                oldrev_str = apr_psprintf(pool, ".r%ld%s%s",
                                          entry->revision,
                                          *path_ext ? "." : "",
                                          *path_ext ? path_ext : "");

              newrev_str = apr_psprintf(pool, ".r%ld%s%s",
                                        *eb->target_revision,
                                        *path_ext ? "." : "",
                                        *path_ext ? path_ext : "");
              mine_str = apr_psprintf(pool, ".mine%s%s",
                                      *path_ext ? "." : "",
                                      *path_ext ? path_ext : "");

              if (fb->add_existed && ! is_replaced)
                {
                  SVN_ERR(svn_wc_create_tmp_file2(NULL, &merge_left,
                                                  svn_wc_adm_access_path(
                                                      adm_access),
                                                  svn_io_file_del_none,
                                                  pool));
                  delete_left = TRUE;
                }
              else if (fb->copied_text_base)
                merge_left = fb->copied_text_base;
              else
                merge_left = fb->text_base_path;

              /* Merge the changes from the old textbase to the new
                 textbase into the file we're updating.
                 Remember that this function wants full paths! */
              /* ### TODO: Pass version info here. */
              SVN_ERR(svn_wc__merge_internal(
                       &log_accum, &merge_outcome,
                       eb->db,
                       merge_left, left_version,
                       new_text_base_path, right_version,
                       fb->path,
                       fb->copied_working_text,
                       oldrev_str, newrev_str, mine_str,
                       FALSE, eb->diff3_cmd, NULL, fb->propchanges,
                       eb->conflict_func, eb->conflict_baton, 
                       eb->cancel_func, eb->cancel_baton,
                       pool));

              /* If we created a temporary left merge file, get rid of it. */
              if (delete_left)
                SVN_ERR(svn_wc__loggy_remove(
                            &log_accum, svn_wc__adm_access_abspath(adm_access),
                            merge_left, pool, pool));

              /* And clean up add-with-history-related temp file too. */
              if (fb->copied_working_text)
                SVN_ERR(svn_wc__loggy_remove(
                            &log_accum, svn_wc__adm_access_abspath(adm_access),
                            fb->copied_working_text, pool, pool));

            } /* end: working file exists and has mods */
        } /* end: working file has mods */
    } /* end: "textual" merging process */
  else
    {
      apr_hash_t *keywords;

      SVN_ERR(svn_wc__get_keywords(&keywords, eb->db, fb->local_abspath, NULL,
                                   pool, pool));
      if (magic_props_changed || keywords)
        /* no new text base, but... */
        {
          /* Special edge-case: it's possible that this file installation
             only involves propchanges, but that some of those props still
             require a retranslation of the working file.

             OR that the file doesn't involve propchanges which by themselves
             require retranslation, but receiving a change bumps the revision
             number which requires re-expansion of keywords... */

          const char *tmptext;

          /* Copy and DEtranslate the working file to a temp text-base.
             Note that detranslation is done according to the old props. */
          SVN_ERR(svn_wc__internal_translated_file(
                   &tmptext, fb->local_abspath, eb->db, fb->local_abspath,
                   SVN_WC_TRANSLATE_TO_NF | SVN_WC_TRANSLATE_NO_OUTPUT_CLEANUP,
                   pool, pool));

          /* A log command that copies the tmp-text-base and REtranslates
             it back to the working file.
             Now, since this is done during the execution of the log file, this
             retranslation is actually done according to the new props. */
          SVN_ERR(svn_wc__loggy_copy(&log_accum,
                                     svn_wc__adm_access_abspath(adm_access),
                                     tmptext, fb->path, pool, pool));
        }

      if (*lock_state == svn_wc_notify_lock_state_unlocked)
        /* If a lock was removed and we didn't update the text contents, we
           might need to set the file read-only. */
        SVN_ERR(svn_wc__loggy_maybe_set_readonly(&log_accum,
                                    svn_wc__adm_access_abspath(adm_access),
                                    fb->path, pool, pool));
    }

  /* Deal with installation of the new textbase, if appropriate. */
  if (new_text_base_path)
    {
      SVN_ERR(svn_wc__loggy_move(&log_accum,
                                 svn_wc__adm_access_abspath(adm_access),
                                 new_text_base_path,
                                 fb->text_base_path, pool, pool));
      SVN_ERR(svn_wc__loggy_set_readonly(
                        &log_accum, svn_wc__adm_access_abspath(adm_access),
                        fb->text_base_path, pool, pool));
      tmp_entry.checksum = svn_checksum_to_cstring(actual_checksum, pool);
      flags |= SVN_WC__ENTRY_MODIFY_CHECKSUM;
    }

  /* If FB->PATH is locally deleted, but not as part of a replacement
     then keep it deleted. */
  if (fb->deleted && !is_replaced)
    {
      tmp_entry.schedule = svn_wc_schedule_delete;
      flags |= SVN_WC__ENTRY_MODIFY_SCHEDULE;
    }

  /* Do the entry modifications we've accumulated. */
  SVN_ERR(svn_wc__loggy_entry_modify(&log_accum,
                                     svn_wc__adm_access_abspath(adm_access),
                                     fb->path, &tmp_entry, flags,
                                     pool, pool));

  /* Log commands to handle text-timestamp and working-size,
     if the file is - or will be - unmodified and schedule-normal */
  if (!is_locally_modified &&
      (fb->added || entry->schedule == svn_wc_schedule_normal))
    {
      /* Adjust working copy file unless this file is an allowed
         obstruction. */
      if (fb->last_changed_date && !fb->existed)
        SVN_ERR(svn_wc__loggy_set_timestamp(
                        &log_accum, svn_wc__adm_access_abspath(adm_access),
                        fb->path, fb->last_changed_date, pool, pool));

      if ((new_text_base_path || magic_props_changed)
          && !fb->deleted)
        {
          /* Adjust entries file to match working file */
          SVN_ERR(svn_wc__loggy_set_entry_timestamp_from_wc(
                        &log_accum, svn_wc__adm_access_abspath(adm_access),
                        fb->path, pool, pool));
        }
      SVN_ERR(svn_wc__loggy_set_entry_working_size_from_wc(
                        &log_accum, svn_wc__adm_access_abspath(adm_access),
                        fb->path, pool, pool));
    }

  /* Clean up add-with-history temp file. */
  if (fb->copied_text_base)
    SVN_ERR(svn_wc__loggy_remove(&log_accum,
                                 svn_wc__adm_access_abspath(adm_access),
                                 fb->copied_text_base, pool, pool));


  /* Set the returned content state. */

  /* This is kind of interesting.  Even if no new text was
     installed (i.e., new_text_path was null), we could still
     report a pre-existing conflict state.  Say a file, already
     in a state of textual conflict, receives prop mods during an
     update.  Then we'll notify that it has text conflicts.  This
     seems okay to me.  I guess.  I dunno.  You? */

  if (merge_outcome == svn_wc_merge_conflict)
    *content_state = svn_wc_notify_state_conflicted;
  else if (new_text_base_path)
    {
      if (is_locally_modified)
        *content_state = svn_wc_notify_state_merged;
      else
        *content_state = svn_wc_notify_state_changed;
    }
  else
    *content_state = svn_wc_notify_state_unchanged;

  /* Now that we've built up *all* of the loggy commands for this
     file, add them to the directory's log accumulator in one fell
     swoop. */
  svn_stringbuf_appendstr(fb->dir_baton->log_accum, log_accum);

  return SVN_NO_ERROR;
}


/* An svn_delta_editor_t function. */
/* Mostly a wrapper around merge_file. */
static svn_error_t *
close_file(void *file_baton,
           const char *expected_hex_digest,
           apr_pool_t *pool)
{
  struct file_baton *fb = file_baton;
  struct edit_baton *eb = fb->edit_baton;
  svn_wc_notify_state_t content_state, prop_state;
  svn_wc_notify_lock_state_t lock_state;
  svn_checksum_t *expected_checksum = NULL;
  svn_checksum_t *actual_checksum;
  const char *new_base_path;

  if (fb->skip_this)
    return maybe_bump_dir_info(eb, fb->bump_info, pool);

  if (expected_hex_digest)
    SVN_ERR(svn_checksum_parse_hex(&expected_checksum, svn_checksum_md5,
                                   expected_hex_digest, pool));

  /* Was this an add-with-history, with no apply_textdelta? */
  if (fb->added_with_history && ! fb->received_textdelta)
    {
      SVN_ERR_ASSERT(! fb->text_base_path && ! fb->new_text_base_path
                     && fb->copied_text_base);

      /* Set up the base paths like apply_textdelta does. */
      SVN_ERR(choose_base_paths(&fb->text_base_path,
                                NULL, NULL,
                                eb->db, fb->local_abspath,
                                fb->pool, pool));

      actual_checksum = fb->copied_base_checksum;
      new_base_path = fb->copied_text_base;
    }
  else
    {
      /* Pull the actual checksum from the file_baton, computed during
         the application of a text delta. */
      actual_checksum = fb->actual_checksum;
      new_base_path = fb->new_text_base_path;
    }

  /* window-handler assembles new pristine text in .svn/tmp/text-base/  */
  if (new_base_path && expected_checksum
      && !svn_checksum_match(expected_checksum, actual_checksum))
    return svn_error_createf(SVN_ERR_CHECKSUM_MISMATCH, NULL,
            _("Checksum mismatch for '%s':\n"
              "   expected:  %s\n"
              "     actual:  %s\n"),
            svn_dirent_local_style(fb->path, pool), expected_hex_digest,
            svn_checksum_to_cstring_display(actual_checksum, pool));

  SVN_ERR(merge_file(&content_state, &prop_state, &lock_state, fb,
                     new_base_path,
                     actual_checksum, pool));

  /* We have one less referrer to the directory's bump information. */
  SVN_ERR(maybe_bump_dir_info(eb, fb->bump_info, pool));

  /* Skip notifications about files which were already notified for
     another reason */
  if (eb->notify_func && !fb->already_notified)
    {
      const svn_string_t *mime_type;
      svn_wc_notify_t *notify;
      svn_wc_notify_action_t action = svn_wc_notify_update_update;

      if (fb->deleted)
        action = svn_wc_notify_update_add_deleted;
      else if (fb->existed || fb->add_existed)
        {
          if (content_state != svn_wc_notify_state_conflicted)
            action = svn_wc_notify_exists;
        }
      else if (fb->added)
        {
          action = svn_wc_notify_update_add;
        }

      notify = svn_wc_create_notify(fb->path, action, pool);
      notify->kind = svn_node_file;
      notify->content_state = content_state;
      notify->prop_state = prop_state;
      notify->lock_state = lock_state;
      notify->revision = *eb->target_revision;
      notify->old_revision = fb->old_revision;

      /* Fetch the mimetype */
      SVN_ERR(svn_wc__internal_propget(&mime_type, eb->db, fb->local_abspath,
                                       SVN_PROP_MIME_TYPE, pool, pool));
      notify->mime_type = mime_type == NULL ? NULL : mime_type->data;

      eb->notify_func(eb->notify_baton, notify, pool);
    }
  return SVN_NO_ERROR;
}


/* An svn_delta_editor_t function. */
static svn_error_t *
close_edit(void *edit_baton,
           apr_pool_t *pool)
{
  struct edit_baton *eb = edit_baton;
  const char *target_path = svn_dirent_join(eb->anchor, eb->target, pool);
  const char *target_abspath;
  int log_number = 0;

  SVN_ERR(svn_dirent_get_absolute(&target_abspath, target_path, pool));

  /* If there is a target and that target is missing, then it
     apparently wasn't re-added by the update process, so we'll
     pretend that the editor deleted the entry.  The helper function
     do_entry_deletion() will take care of the necessary steps.  */
  if ((*eb->target) && (svn_wc__adm_missing(eb->db, target_abspath, pool)))
    /* Still passing NULL for THEIR_URL. A case where THEIR_URL
     * is needed in this call is rare or even non-existant.
     * ### TODO: Construct a proper THEIR_URL anyway. See also
     * NULL handling code in do_entry_deletion(). */
    SVN_ERR(do_entry_deletion(&log_number, eb, eb->target_abspath, NULL,
                              FALSE, pool));

  /* The editor didn't even open the root; we have to take care of
     some cleanup stuffs. */
  if (! eb->root_opened)
    {
      /* We need to "un-incomplete" the root directory. */
      SVN_ERR(complete_directory(eb, eb->anchor_abspath, TRUE, pool));
    }


  /* By definition, anybody "driving" this editor for update or switch
     purposes at a *minimum* must have called set_target_revision() at
     the outset, and close_edit() at the end -- even if it turned out
     that no changes ever had to be made, and open_root() was never
     called.  That's fine.  But regardless, when the edit is over,
     this editor needs to make sure that *all* paths have had their
     revisions bumped to the new target revision. */

  /* Make sure our update target now has the new working revision.
     Also, if this was an 'svn switch', then rewrite the target's
     url.  All of this tweaking might happen recursively!  Note
     that if eb->target is NULL, that's okay (albeit "sneaky",
     some might say).  */

  /* Extra check: if the update did nothing but make its target
     'deleted', then do *not* run cleanup on the target, as it
     will only remove the deleted entry!  */
  if (! eb->target_deleted)
    {
      SVN_ERR(svn_wc__do_update_cleanup(eb->db, target_abspath,
                                        eb->requested_depth,
                                        eb->switch_url,
                                        eb->repos,
                                        *(eb->target_revision),
                                        eb->notify_func,
                                        eb->notify_baton,
                                        TRUE, eb->skipped_trees,
                                        eb->pool));
    }

  /* The edit is over, free its pool.
     ### No, this is wrong.  Who says this editor/baton won't be used
     again?  But the change is not merely to remove this call.  We
     should also make eb->pool not be a subpool (see make_editor),
     and change callers of svn_client_{checkout,update,switch} to do
     better pool management. ### */
  svn_pool_destroy(eb->pool);

  return SVN_NO_ERROR;
}



/*** Returning editors. ***/

/* Helper for the three public editor-supplying functions. */
static svn_error_t *
make_editor(svn_revnum_t *target_revision,
            svn_wc_context_t *wc_ctx,
            const char *anchor_abspath,
            const char *target_basename,
            svn_boolean_t use_commit_times,
            const char *switch_url,
            svn_depth_t depth,
            svn_boolean_t depth_is_sticky,
            svn_boolean_t allow_unver_obstructions,
            svn_wc_notify_func2_t notify_func,
            void *notify_baton,
            svn_cancel_func_t cancel_func,
            void *cancel_baton,
            svn_wc_conflict_resolver_func_t conflict_func,
            void *conflict_baton,
            svn_wc_external_update_t external_func,
            void *external_baton,
            svn_wc_get_file_t fetch_func,
            void *fetch_baton,
            const char *diff3_cmd,
            apr_array_header_t *preserved_exts,
            const svn_delta_editor_t **editor,
            void **edit_baton,
            apr_pool_t *pool, /* = result_pool */
            apr_pool_t *scratch_pool)
{
  struct edit_baton *eb;
  void *inner_baton;
  apr_pool_t *subpool = svn_pool_create(pool);
  svn_delta_editor_t *tree_editor = svn_delta_default_editor(subpool);
  const svn_delta_editor_t *inner_editor;
  const char *repos_root, *repos_uuid;
  svn_wc_adm_access_t *adm_access;
  const char *anchor;

  adm_access = svn_wc__adm_retrieve_internal2(wc_ctx->db, anchor_abspath,
                                              scratch_pool);
  anchor = svn_wc_adm_access_path(adm_access);

  /* An unknown depth can't be sticky. */
  if (depth == svn_depth_unknown)
    depth_is_sticky = FALSE;

  /* Get the anchor entry, so we can fetch the repository root. */
  SVN_ERR(svn_wc__node_get_repos_info(&repos_root, &repos_uuid, wc_ctx,
                                      anchor_abspath, pool, scratch_pool));

  /* Disallow a switch operation to change the repository root of the target,
     if that is known. */
  if (switch_url && repos_root &&
      ! svn_uri_is_ancestor(repos_root, switch_url))
    return svn_error_createf(
       SVN_ERR_WC_INVALID_SWITCH, NULL,
       _("'%s'\n"
         "is not the same repository as\n"
         "'%s'"), switch_url, repos_root);

  /* Construct an edit baton. */
  eb = apr_pcalloc(subpool, sizeof(*eb));
  eb->pool                     = subpool;
  eb->use_commit_times         = use_commit_times;
  eb->target_revision          = target_revision;
  eb->switch_url               = switch_url;
  eb->repos                    = repos_root;
  eb->uuid                     = repos_uuid;
  eb->db                       = wc_ctx->db;
  eb->wc_ctx                   = wc_ctx;
  eb->adm_access               = adm_access;
  eb->anchor                   = anchor;
  eb->target                   = target_basename;
  eb->anchor_abspath           = anchor_abspath;

  if (svn_path_is_empty(target_basename))
    eb->target_abspath = eb->anchor_abspath;
  else
    eb->target_abspath = svn_dirent_join(eb->anchor_abspath, target_basename,
                                         pool);

  eb->requested_depth          = depth;
  eb->depth_is_sticky          = depth_is_sticky;
  eb->notify_func              = notify_func;
  eb->notify_baton             = notify_baton;
  eb->external_func            = external_func;
  eb->external_baton           = external_baton;
  eb->diff3_cmd                = diff3_cmd;
  eb->cancel_func              = cancel_func;
  eb->cancel_baton             = cancel_baton;
  eb->conflict_func            = conflict_func;
  eb->conflict_baton           = conflict_baton;
  eb->fetch_func               = fetch_func;
  eb->fetch_baton              = fetch_baton;
  eb->allow_unver_obstructions = allow_unver_obstructions;
  eb->skipped_trees            = apr_hash_make(subpool);
  eb->ext_patterns             = preserved_exts;

  /* Construct an editor. */
  tree_editor->set_target_revision = set_target_revision;
  tree_editor->open_root = open_root;
  tree_editor->delete_entry = delete_entry;
  tree_editor->add_directory = add_directory;
  tree_editor->open_directory = open_directory;
  tree_editor->change_dir_prop = change_dir_prop;
  tree_editor->close_directory = close_directory;
  tree_editor->absent_directory = absent_directory;
  tree_editor->add_file = add_file;
  tree_editor->open_file = open_file;
  tree_editor->apply_textdelta = apply_textdelta;
  tree_editor->change_file_prop = change_file_prop;
  tree_editor->close_file = close_file;
  tree_editor->absent_file = absent_file;
  tree_editor->close_edit = close_edit;

  /* Fiddle with the type system. */
  inner_editor = tree_editor;
  inner_baton = eb;

  /* We need to limit the scope of our operation to the ambient depths
     present in the working copy already, but only if the requested
     depth is not sticky. If a depth was explicitly requested,
     libsvn_delta/depth_filter_editor.c will ensure that we never see
     editor calls that extend beyond the scope of the requested depth.
     But even what we do so might extend beyond the scope of our
     ambient depth.  So we use another filtering editor to avoid
     modifying the ambient working copy depth when not asked to do so.
     (This can also be skipped if the server understands depth; consider
     letting the depth RA capability percolate down to this level.) */
  if (!depth_is_sticky)
    SVN_ERR(svn_wc__ambient_depth_filter_editor(&inner_editor,
                                                &inner_baton,
                                                inner_editor,
                                                inner_baton,
                                                anchor,
                                                target_basename,
                                                adm_access,
                                                pool));

  return svn_delta_get_cancellation_editor(cancel_func,
                                           cancel_baton,
                                           inner_editor,
                                           inner_baton,
                                           editor,
                                           edit_baton,
                                           pool);
}


svn_error_t *
svn_wc_get_update_editor4(const svn_delta_editor_t **editor,
                          void **edit_baton,
                          svn_revnum_t *target_revision,
                          svn_wc_context_t *wc_ctx,
                          const char *anchor_abspath,
                          const char *target_basename,
                          svn_boolean_t use_commit_times,
                          svn_depth_t depth,
                          svn_boolean_t depth_is_sticky,
                          svn_boolean_t allow_unver_obstructions,
                          svn_wc_notify_func2_t notify_func,
                          void *notify_baton,
                          svn_cancel_func_t cancel_func,
                          void *cancel_baton,
                          svn_wc_conflict_resolver_func_t conflict_func,
                          void *conflict_baton,
                          svn_wc_external_update_t external_func,
                          void *external_baton,
                          svn_wc_get_file_t fetch_func,
                          void *fetch_baton,
                          const char *diff3_cmd,
                          apr_array_header_t *preserved_exts,
                          apr_pool_t *result_pool,
                          apr_pool_t *scratch_pool)
{
  return make_editor(target_revision, wc_ctx, anchor_abspath,
                     target_basename, use_commit_times,
                     NULL, depth, depth_is_sticky, allow_unver_obstructions,
                     notify_func, notify_baton,
                     cancel_func, cancel_baton,
                     conflict_func, conflict_baton,
                     external_func, external_baton,
                     fetch_func, fetch_baton,
                     diff3_cmd, preserved_exts, editor, edit_baton,
                     result_pool, scratch_pool);
}

svn_error_t *
svn_wc_get_switch_editor4(const svn_delta_editor_t **editor,
                          void **edit_baton,
                          svn_revnum_t *target_revision,
                          svn_wc_context_t *wc_ctx,
                          const char *anchor_abspath,
                          const char *target_basename,
                          const char *switch_url,
                          svn_boolean_t use_commit_times,
                          svn_depth_t depth,
                          svn_boolean_t depth_is_sticky,
                          svn_boolean_t allow_unver_obstructions,
                          svn_wc_notify_func2_t notify_func,
                          void *notify_baton,
                          svn_cancel_func_t cancel_func,
                          void *cancel_baton,
                          svn_wc_conflict_resolver_func_t conflict_func,
                          void *conflict_baton,
                          svn_wc_external_update_t external_func,
                          void *external_baton,
                          svn_wc_get_file_t fetch_func,
                          void *fetch_baton,
                          const char *diff3_cmd,
                          apr_array_header_t *preserved_exts,
                          apr_pool_t *result_pool,
                          apr_pool_t *scratch_pool)
{
  SVN_ERR_ASSERT(switch_url && svn_uri_is_canonical(switch_url, scratch_pool));

  return make_editor(target_revision, wc_ctx, anchor_abspath,
                     target_basename, use_commit_times,
                     switch_url,
                     depth, depth_is_sticky, allow_unver_obstructions,
                     notify_func, notify_baton,
                     cancel_func, cancel_baton,
                     conflict_func, conflict_baton,
                     external_func, external_baton,
                     fetch_func, fetch_baton,
                     diff3_cmd, preserved_exts,
                     editor, edit_baton,
                     result_pool, scratch_pool);
}

/* ABOUT ANCHOR AND TARGET, AND svn_wc_get_actual_target2()

   THE GOAL

   Note the following actions, where X is the thing we wish to update,
   P is a directory whose repository URL is the parent of
   X's repository URL, N is directory whose repository URL is *not*
   the parent directory of X (including the case where N is not a
   versioned resource at all):

      1.  `svn up .' from inside X.
      2.  `svn up ...P/X' from anywhere.
      3.  `svn up ...N/X' from anywhere.

   For the purposes of the discussion, in the '...N/X' situation, X is
   said to be a "working copy (WC) root" directory.

   Now consider the four cases for X's type (file/dir) in the working
   copy vs. the repository:

      A.  dir in working copy, dir in repos.
      B.  dir in working copy, file in repos.
      C.  file in working copy, dir in repos.
      D.  file in working copy, file in repos.

   Here are the results we expect for each combination of the above:

      1A. Successfully update X.
      1B. Error (you don't want to remove your current working
          directory out from underneath the application).
      1C. N/A (you can't be "inside X" if X is a file).
      1D. N/A (you can't be "inside X" if X is a file).

      2A. Successfully update X.
      2B. Successfully update X.
      2C. Successfully update X.
      2D. Successfully update X.

      3A. Successfully update X.
      3B. Error (you can't create a versioned file X inside a
          non-versioned directory).
      3C. N/A (you can't have a versioned file X in directory that is
          not its repository parent).
      3D. N/A (you can't have a versioned file X in directory that is
          not its repository parent).

   To summarize, case 2 always succeeds, and cases 1 and 3 always fail
   (or can't occur) *except* when the target is a dir that remains a
   dir after the update.

   ACCOMPLISHING THE GOAL

   Updates are accomplished by driving an editor, and an editor is
   "rooted" on a directory.  So, in order to update a file, we need to
   break off the basename of the file, rooting the editor in that
   file's parent directory, and then updating only that file, not the
   other stuff in its parent directory.

   Secondly, we look at the case where we wish to update a directory.
   This is typically trivial.  However, one problematic case, exists
   when we wish to update a directory that has been removed from the
   repository and replaced with a file of the same name.  If we root
   our edit at the initial directory, there is no editor mechanism for
   deleting that directory and replacing it with a file (this would be
   like having an editor now anchored on a file, which is disallowed).

   All that remains is to have a function with the knowledge required
   to properly decide where to root our editor, and what to act upon
   with that now-rooted editor.  Given a path to be updated, this
   function should conditionally split that path into an "anchor" and
   a "target", where the "anchor" is the directory at which the update
   editor is rooted (meaning, editor->open_root() is called with
   this directory in mind), and the "target" is the actual intended
   subject of the update.

   svn_wc_get_actual_target2() is that function.

   So, what are the conditions?

   Case I: Any time X is '.' (implying it is a directory), we won't
   lop off a basename.  So we'll root our editor at X, and update all
   of X.

   Cases II & III: Any time we are trying to update some path ...N/X,
   we again will not lop off a basename.  We can't root an editor at
   ...N with X as a target, either because ...N isn't a versioned
   resource at all (Case II) or because X is X is not a child of ...N
   in the repository (Case III).  We root at X, and update X.

   Cases IV-???: We lop off a basename when we are updating a
   path ...P/X, rooting our editor at ...P and updating X, or when X
   is missing from disk.

   These conditions apply whether X is a file or directory.

   ---

   As it turns out, commits need to have a similar check in place,
   too, specifically for the case where a single directory is being
   committed (we have to anchor at that directory's parent in case the
   directory itself needs to be modified).
*/


svn_error_t *
svn_wc__check_wc_root(svn_boolean_t *wc_root,
                      svn_node_kind_t *kind,
                      svn_wc__db_t *db,
                      const char *local_abspath,
                      apr_pool_t *scratch_pool)
{
  const char *parent, *base_name;
  const svn_wc_entry_t *p_entry, *entry;
  svn_error_t *err;
  svn_boolean_t hidden;

  /* Go ahead and initialize our return value to the most common
     (code-wise) values. */
  *wc_root = TRUE;

  /* Get our ancestry.  In the event that the path is unversioned (or
     otherwise hidden), treat it as if it were a file so that the anchor
     will be the parent directory. If the node is a FILE, then it is
     definitely not a root.  */
  err = svn_wc__get_entry(&entry, db, local_abspath, TRUE, svn_node_unknown,
                          FALSE, scratch_pool, scratch_pool);

  if (err && err->apr_err == SVN_ERR_NODE_UNEXPECTED_KIND
        && entry->kind == svn_node_dir && *entry->name != '\0')
    {
      /* The (subdir) node is (most likely) not present. We said we wanted
         the actual information, but got the stub info instead. We can
         pretend this is a file so the parent will be the anchor.  */
      svn_error_clear(err);

      if (kind)
        *kind = svn_node_file;
      *wc_root = FALSE;
      return SVN_NO_ERROR;
    }
  else if (err)
    return svn_error_return(err);

  if (entry == NULL || entry->kind == svn_node_file)
    {
      if (kind)
        *kind = svn_node_file;
      *wc_root = FALSE;
      return SVN_NO_ERROR;
    }

  SVN_ERR(svn_wc__entry_is_hidden(&hidden, entry));
  if (hidden)
    {
      if (kind)
        *kind = svn_node_file;
      *wc_root = FALSE;
      return SVN_NO_ERROR;
    }

  SVN_ERR_ASSERT(entry->kind == svn_node_dir);
  if (kind)
    *kind = svn_node_dir;

  /* If this is the root folder (of a drive), it should be the WC
     root too. */
  if (svn_dirent_is_root(local_abspath, strlen(local_abspath)))
    return SVN_NO_ERROR;

  svn_dirent_split(local_abspath, &parent, &base_name, scratch_pool);

  /* If we cannot get an entry for PATH's parent, PATH is a WC root. */
  err = svn_wc__get_entry(&p_entry, db, parent, FALSE, svn_node_dir, FALSE,
                          scratch_pool, scratch_pool);
  if (err)
    {
      svn_error_clear(err);
      return SVN_NO_ERROR;
    }
  SVN_ERR(svn_wc__entry_is_hidden(&hidden, p_entry));
  SVN_ERR_ASSERT(!hidden);

  /* If the parent directory has no url information, something is
     messed up.  Bail with an error. */
  if (! p_entry->url)
    return svn_error_createf(
       SVN_ERR_ENTRY_MISSING_URL, NULL,
       _("'%s' has no ancestry information"),
       svn_dirent_local_style(parent, scratch_pool));

  /* If PATH's parent in the WC is not its parent in the repository,
     PATH is a WC root. */
  if (entry && entry->url
      && (strcmp(svn_path_url_add_component2(p_entry->url, base_name,
                                             scratch_pool),
                 entry->url) != 0))
    return SVN_NO_ERROR;

  /* If PATH's parent in the repository is not its parent in the WC,
     PATH is a WC root. */
  err = svn_wc__get_entry(&p_entry, db, local_abspath, FALSE, svn_node_dir,
                          TRUE, scratch_pool, scratch_pool);
  if (err)
    {
      svn_error_clear(err);
      return SVN_NO_ERROR;
    }

  SVN_ERR(svn_wc__entry_is_hidden(&hidden, p_entry));
  if (hidden)
    {
      return SVN_NO_ERROR;
    }

  /* If we have not determined that PATH is a WC root by now, it must
     not be! */
  *wc_root = FALSE;
  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc_is_wc_root2(svn_boolean_t *wc_root,
                   svn_wc_context_t *wc_ctx,
                   const char *local_abspath,
                   apr_pool_t *scratch_pool)
{
  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  return svn_error_return(
    svn_wc__check_wc_root(wc_root, NULL, wc_ctx->db, local_abspath,
                          scratch_pool));
}


svn_error_t*
svn_wc__strictly_is_wc_root(svn_boolean_t *wc_root,
                            svn_wc_context_t *wc_ctx,
                            const char *local_abspath,
                            apr_pool_t *scratch_pool)
{
  SVN_ERR(svn_wc__check_wc_root(wc_root, NULL, wc_ctx->db, local_abspath,
                                scratch_pool));

  if (*wc_root)
    {
      svn_wc__db_kind_t kind;
      svn_error_t *err;

      /* Check whether this is a switched subtree or an absent item.
       * Switched subtrees are considered working copy roots by
       * svn_wc_is_wc_root(). */
      err = svn_wc__db_read_info(NULL, &kind, NULL, NULL, NULL, NULL,
                                 NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                                 NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                                 NULL, NULL, NULL, NULL,
                                 wc_ctx->db, local_abspath,
                                 scratch_pool, scratch_pool);

      /* If the node doesn't exist, it can't possibly be a switched subdir.
       * It can't be a WC root either, for that matter.*/
      if (err)
        {
          svn_error_clear(err);
          *wc_root = FALSE;
          return SVN_NO_ERROR;
        }

      if (kind == svn_wc__db_kind_dir)
        {
          svn_boolean_t switched;

          err = svn_wc__internal_path_switched(&switched, wc_ctx->db,
                                               local_abspath, scratch_pool);

          if (err && (err->apr_err == SVN_ERR_ENTRY_MISSING_URL))
            {
              /* This is e.g. a locally deleted dir. It has an entry but
               * no repository URL. It cannot be a WC root. */
              svn_error_clear(err);
              *wc_root = FALSE;
            }
          else
            {
              SVN_ERR(err);
              /* The query for a switched dir succeeded. If switched,
               * don't consider this a WC root. */
              *wc_root = ! switched;
            }
        }
    }

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc_get_actual_target2(const char **anchor,
                          const char **target,
                          svn_wc_context_t *wc_ctx,
                          const char *path,
                          apr_pool_t *result_pool,
                          apr_pool_t *scratch_pool)
{
  svn_boolean_t is_wc_root;
  svn_node_kind_t kind;
  const char *local_abspath;

  SVN_ERR(svn_dirent_get_absolute(&local_abspath, path, scratch_pool));

  SVN_ERR(svn_wc__check_wc_root(&is_wc_root, &kind, wc_ctx->db, local_abspath,
                                scratch_pool));

  /* If PATH is not a WC root, or if it is a file, lop off a basename. */
  if ((! is_wc_root) || (kind == svn_node_file))
    {
      svn_dirent_split(path, anchor, target, result_pool);
    }
  else
    {
      *anchor = apr_pstrdup(result_pool, path);
      *target = "";
    }

  return SVN_NO_ERROR;
}

/* Write, to LOG_ACCUM, commands to install properties for an added DST_PATH.
   NEW_BASE_PROPS and NEW_PROPS are base and working properties, respectively.
   BASE_PROPS can contain entryprops and wcprops as well.  ADM_ACCESS must
   be an access baton for DST_PATH.
   Use @a POOL for temporary allocations. */
static svn_error_t *
install_added_props(svn_stringbuf_t *log_accum,
                    svn_wc_adm_access_t *adm_access,
                    const char *dst_path,
                    apr_hash_t *new_base_props,
                    apr_hash_t *new_props,
                    apr_pool_t *pool)
{
  apr_array_header_t *regular_props = NULL, *wc_props = NULL,
    *entry_props = NULL;
  const char *local_abspath;
  svn_wc__db_t *db = svn_wc__adm_get_db(adm_access);
  const char *adm_abspath = svn_wc__adm_access_abspath(adm_access);

  SVN_ERR(svn_dirent_get_absolute(&local_abspath, dst_path, pool));

  /* Categorize the base properties. */
  {
    apr_array_header_t *prop_array;

    /* Diff an empty prop has against the new base props gives us an array
       of all props. */
    SVN_ERR(svn_prop_diffs(&prop_array, new_base_props,
                           apr_hash_make(pool), pool));
    SVN_ERR(svn_categorize_props(prop_array,
                                 &entry_props, &wc_props, &regular_props,
                                 pool));

    /* Put regular props back into a hash table. */
    new_base_props = prop_hash_from_array(regular_props, pool);
  }

  /* Install base and working props. */
  SVN_ERR(svn_wc__install_props(&log_accum, adm_abspath, local_abspath,
                                new_base_props,
                                new_props ? new_props : new_base_props,
                                TRUE, pool));

  /* Install the entry props. */
  SVN_ERR(accumulate_entry_props(log_accum, adm_abspath,
                                 NULL, dst_path, entry_props, pool));

  return svn_error_return(svn_wc__db_base_set_dav_cache(db, local_abspath,
                                        prop_hash_from_array(wc_props, pool),
                                        pool));
}

svn_error_t *
svn_wc_add_repos_file4(svn_wc_context_t *wc_ctx,
                       const char *local_abspath,
                       svn_stream_t *new_base_contents,
                       svn_stream_t *new_contents,
                       apr_hash_t *new_base_props,
                       apr_hash_t *new_props,
                       const char *copyfrom_url,
                       svn_revnum_t copyfrom_rev,
                       svn_cancel_func_t cancel_func,
                       void *cancel_baton,
                       svn_wc_notify_func2_t notify_func,
                       void *notify_baton,
                       apr_pool_t *pool)
{
  const char *new_URL;
  const char *dir_abspath = svn_dirent_dirname(local_abspath, pool);
  apr_file_t *base_file;
  const char *tmp_text_base_path;
  svn_checksum_t *base_checksum;
  svn_stream_t *tmp_base_contents;
  const char *dst_path;
  const char *text_base_path;
  const svn_wc_entry_t *ent;
  const svn_wc_entry_t *dst_entry;
  svn_stringbuf_t *log_accum;
  svn_wc_adm_access_t *adm_access = 
      svn_wc__adm_retrieve_internal2(wc_ctx->db, dir_abspath, pool);

  SVN_ERR_ASSERT(strcmp(dir_abspath,
                        svn_wc__adm_access_abspath(adm_access)) == 0);

  SVN_ERR(svn_wc__text_base_path(&text_base_path, wc_ctx->db, local_abspath,
                                 FALSE, pool));

  /* Calculate a valid relative path for the loggy code below */
  SVN_ERR(svn_wc__temp_get_relpath(&dst_path, wc_ctx->db, local_abspath,
                                   pool, pool));

  /* Fabricate the anticipated new URL of the target and check the
     copyfrom URL to be in the same repository. */
  {
    SVN_ERR(svn_wc__get_entry(&ent, wc_ctx->db, dir_abspath, FALSE,
                              svn_node_dir, FALSE, pool, pool));

    new_URL = svn_path_url_add_component2(ent->url,
                                          svn_dirent_basename(local_abspath,
                                                              NULL),
                                          pool);

    if (copyfrom_url && ent->repos &&
        ! svn_uri_is_ancestor(ent->repos, copyfrom_url))
      return svn_error_createf(SVN_ERR_UNSUPPORTED_FEATURE, NULL,
                               _("Copyfrom-url '%s' has different repository"
                                 " root than '%s'"),
                               copyfrom_url, ent->repos);
  }

  /* Accumulate log commands in this buffer until we're ready to close
     and run the log.  */
  log_accum = svn_stringbuf_create("", pool);

  /* If we're replacing the file then we need to save the destination files
     text base and prop base before replacing it. This allows us to revert
     the entire change. */
  SVN_ERR(svn_wc__get_entry(&dst_entry, wc_ctx->db, local_abspath, TRUE,
                           svn_node_unknown, FALSE, pool, pool));
  if (dst_entry && dst_entry->schedule == svn_wc_schedule_delete)
    {
      const char *dst_rtext;
      const char *dst_txtb;

      /* ### replace this block with: svn_wc__wq_prepare_revert_files()  */

      SVN_ERR(svn_wc__text_revert_path(&dst_rtext, wc_ctx->db, local_abspath,
                                       pool));
      SVN_ERR(svn_wc__text_base_path(&dst_txtb, wc_ctx->db, local_abspath,
                                     FALSE, pool));

      SVN_ERR(svn_wc__loggy_move(&log_accum, dir_abspath,
                                 dst_txtb, dst_rtext, pool, pool));
      SVN_ERR(svn_wc__loggy_revert_props_create(&log_accum, wc_ctx->db,
                                                local_abspath, dir_abspath,
                                                pool));
    }

  /* Schedule this for addition first, before the entry exists.
   * Otherwise we'll get bounced out with an error about scheduling
   * an already-versioned item for addition.
   */
  {
    svn_wc_entry_t tmp_entry;
    apr_uint64_t modify_flags = SVN_WC__ENTRY_MODIFY_SCHEDULE;

    tmp_entry.schedule = svn_wc_schedule_add;

    if (copyfrom_url)
      {
        SVN_ERR_ASSERT(SVN_IS_VALID_REVNUM(copyfrom_rev));

        tmp_entry.copyfrom_url = copyfrom_url;
        tmp_entry.copyfrom_rev = copyfrom_rev;
        tmp_entry.copied = TRUE;

        modify_flags |= SVN_WC__ENTRY_MODIFY_COPYFROM_URL
          | SVN_WC__ENTRY_MODIFY_COPYFROM_REV
          | SVN_WC__ENTRY_MODIFY_COPIED;
      }

    SVN_ERR(svn_wc__loggy_entry_modify(&log_accum, dir_abspath,
                                       dst_path, &tmp_entry,
                                       modify_flags, pool, pool));
  }

  /* Set the new revision number and URL in the entry and clean up some other
     fields. This clears DELETED from any prior versioned file with the
     same name (needed before attempting to install props).  */
  SVN_ERR(loggy_tweak_entry(log_accum, local_abspath, dir_abspath,
                            dst_entry ? dst_entry->revision : ent->revision,
                            new_URL, pool));

  /* Install the props before the loggy translation, so that it has access to
     the properties for this file. */
  SVN_ERR(install_added_props(log_accum, adm_access, dst_path,
                              new_base_props, new_props, pool));

  /* Copy the text base contents into a temporary file so our log
     can refer to it. Compute its checksum as we copy. */
  SVN_ERR(svn_wc_create_tmp_file2(&base_file, &tmp_text_base_path, dir_abspath,
                                  svn_io_file_del_none, pool));
  new_base_contents = svn_stream_checksummed2(new_base_contents,
                                              &base_checksum, NULL,
                                              svn_checksum_md5, TRUE, pool);
  tmp_base_contents = svn_stream_from_aprfile2(base_file, FALSE, pool);
  SVN_ERR(svn_stream_copy3(new_base_contents, tmp_base_contents,
                           cancel_func, cancel_baton,
                           pool));

  /* Install working file. */
  if (new_contents)
    {
      /* If the caller gave us a new working file, copy it in place. */
      apr_file_t *contents_file;
      svn_stream_t *tmp_contents;
      const char *tmp_text_path;

      SVN_ERR(svn_wc_create_tmp_file2(&contents_file, &tmp_text_path,
                                      dir_abspath, svn_io_file_del_none,
                                      pool));
      tmp_contents = svn_stream_from_aprfile2(contents_file, FALSE, pool);
      SVN_ERR(svn_stream_copy3(new_contents,
                               tmp_contents,
                               cancel_func, cancel_baton,
                               pool));

      /* Translate new temporary text file to working text. */
      SVN_ERR(svn_wc__loggy_copy(&log_accum, dir_abspath,
                                 tmp_text_path, dst_path,
                                 pool, pool));

      /* After copying to the working directory, lose the temp file. */
      SVN_ERR(svn_wc__loggy_remove(&log_accum, dir_abspath,
                                   tmp_text_path, pool, pool));
    }
  else
    {
      /* No working file provided by the caller, copy and translate the
         text base. */
      SVN_ERR(svn_wc__loggy_copy(
                &log_accum, dir_abspath, tmp_text_base_path, dst_path,
                pool, pool));
      SVN_ERR(svn_wc__loggy_set_entry_timestamp_from_wc(
                &log_accum, dir_abspath, dst_path, pool, pool));
      SVN_ERR(svn_wc__loggy_set_entry_working_size_from_wc(
                &log_accum, dir_abspath, dst_path, pool, pool));
    }

  /* Install new text base. */
  {
    svn_wc_entry_t tmp_entry;

    /* Write out log commands to set up the new text base and its
       checksum. */
    SVN_ERR(svn_wc__loggy_move(&log_accum, dir_abspath,
                               tmp_text_base_path, text_base_path,
                               pool, pool));
    SVN_ERR(svn_wc__loggy_set_readonly(&log_accum, dir_abspath,
                                       text_base_path, pool, pool));

    tmp_entry.checksum = svn_checksum_to_cstring(base_checksum, pool);
    SVN_ERR(svn_wc__loggy_entry_modify(&log_accum, dir_abspath,
                                       dst_path, &tmp_entry,
                                       SVN_WC__ENTRY_MODIFY_CHECKSUM,
                                       pool, pool));
  }

  /* Write our accumulation of log entries into a log file */
  SVN_ERR(svn_wc__write_log(dir_abspath, 0, log_accum, pool));

  return svn_wc__run_log(adm_access, pool);
}
