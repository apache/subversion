/*
 * update_editor.c :  main editor for checkouts and updates
 *
 * ====================================================================
 * Copyright (c) 2000-2007 CollabNet.  All rights reserved.
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



#include <stdlib.h>
#include <string.h>
#include <assert.h>

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
#include "svn_path.h"
#include "svn_xml.h"
#include "svn_error.h"
#include "svn_io.h"
#include "svn_md5.h"
#include "svn_private_config.h"
#include "svn_time.h"
#include "svn_config.h"

#include "wc.h"
#include "questions.h"
#include "log.h"
#include "adm_files.h"
#include "adm_ops.h"
#include "entries.h"
#include "lock.h"
#include "props.h"
#include "translate.h"
#include "tree_conflicts.h"

#include "private/svn_wc_private.h"


/** Forward declarations  **/

static svn_error_t *add_file_with_history(const char *path,
                                          void *parent_baton,
                                          const char *copyfrom_path,
                                          svn_revnum_t copyfrom_rev,
                                          void **file_baton,
                                          apr_pool_t *pool);


/*** batons ***/

struct edit_baton
{
  /* For updates, the "destination" of the edit is the ANCHOR (the
     directory at which the edit is rooted) plus the TARGET (the
     actual thing we wish to update).  For checkouts, ANCHOR holds the
     whole path, and TARGET is unused. */
  const char *anchor;
  const char *target;

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

  /* Non-null if this is a 'switch' operation. */
  const char *switch_url;

  /* The URL to the root of the repository, or NULL. */
  const char *repos;

  /* External diff3 to use for merges (can be null, in which case
     internal merge code is used). */
  const char *diff3_cmd;

  /* Object for gathering info to be accessed after the edit is
     complete. */
  svn_wc_traversal_info_t *traversal_info;

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

  /* Paths that were skipped during the edit, and therefore shouldn't have
     their revision/url info updated at the end.
     The keys are pathnames and the values unspecified. */
  apr_hash_t *skipped_paths;

  apr_pool_t *pool;
};


struct dir_baton
{
  /* The path to this directory. */
  const char *path;

  /* Basename of this directory. */
  const char *name;

  /* The repository URL this directory will correspond to. */
  const char *new_URL;

  /* The global edit baton. */
  struct edit_baton *edit_baton;

  /* Baton for this directory's parent, or NULL if this is the root
     directory. */
  struct dir_baton *parent_baton;

  /* Gets set iff this is a new directory that is not yet versioned and not
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

  /* the path of the directory to bump */
  const char *path;

  /* Set if this directory is skipped due to prop or tree conflicts.
     This does NOT mean that children are skipped. */
  svn_boolean_t skipped;
};


struct handler_baton
{
  apr_file_t *source;
  apr_file_t *dest;
  svn_txdelta_window_handler_t apply_handler;
  void *apply_baton;
  apr_pool_t *pool;
  struct file_baton *fb;
};


/* Return the url for NAME in DIR, allocated in POOL, or null if
 * unable to obtain a url.  If NAME is null, get the url for DIR.
 *
 * Use ASSOCIATED_ACCESS to retrieve an access baton for PATH, and do
 * all temporary allocation in POOL.
 */
static const char *
get_entry_url(svn_wc_adm_access_t *associated_access,
              const char *dir,
              const char *name,
              apr_pool_t *pool)
{
  svn_error_t *err;
  const svn_wc_entry_t *entry;
  svn_wc_adm_access_t *adm_access;

  err = svn_wc_adm_retrieve(&adm_access, associated_access, dir, pool);

  if (! err)
    {
      /* Note that `name' itself may be NULL. */
      err = svn_wc_entry(&entry, svn_path_join_many(pool, dir, name, NULL),
                         adm_access, FALSE, pool);
    }
  if (err || (! entry) || (! entry->url))
    {
      svn_error_clear(err);
      return NULL;
    }

  return entry->url;
}

/* Flush accumulated log entries to a log file on disk for DIR_BATON and
 * increase the log number of the dir baton.
 * Use POOL for temporary allocations. */
static svn_error_t *
flush_log(struct dir_baton *db, apr_pool_t *pool)
{
  if (! svn_stringbuf_isempty(db->log_accum))
    {
      svn_wc_adm_access_t *adm_access;

      SVN_ERR(svn_wc_adm_retrieve(&adm_access, db->edit_baton->adm_access,
                                   db->path, pool));
      SVN_ERR(svn_wc__write_log(adm_access, db->log_number, db->log_accum,
                                pool));
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
          err = svn_wc__run_log(adm_access, NULL, pool);

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

  /* Don't do this.  Just do NOT do this to me. */
  if (pb && (! path))
    abort();

  /* Okay, no easy out, so allocate and initialize a dir baton. */
  d = apr_pcalloc(pool, sizeof(*d));

  /* Construct the PATH and baseNAME of this directory. */
  d->path = apr_pstrdup(pool, eb->anchor);
  if (path)
    {
      d->path = svn_path_join(d->path, path, pool);
      d->name = svn_path_basename(path, pool);
    }
  else
    {
      d->name = NULL;
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
            d->new_URL = svn_path_dirname(eb->switch_url, pool);
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
            d->new_URL = svn_path_url_add_component(pb->new_URL,
                                                    d->name, pool);
        }
    }
  else  /* must be an update */
    {
      /* updates are the odds ones.  if we're updating a path already
         present on disk, we use its original URL.  otherwise, we'll
         telescope based on its parent's URL. */
      d->new_URL = get_entry_url(eb->adm_access, d->path, NULL, pool);
      if ((! d->new_URL) && pb)
        d->new_URL = svn_path_url_add_component(pb->new_URL, d->name, pool);
    }

  /* the bump information lives in the edit pool */
  bdi = apr_palloc(eb->pool, sizeof(*bdi));
  bdi->parent = pb ? pb->bump_info : NULL;
  bdi->ref_count = 1;
  bdi->path = apr_pstrdup(eb->pool, d->path);
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

  /* The caller of this function needs to fill this in. */
  d->ambient_depth = svn_depth_unknown;

  apr_pool_cleanup_register(d->pool, d, cleanup_dir_baton,
                            cleanup_dir_baton_child);

  *d_p = d;
  return SVN_NO_ERROR;
}



/* Helper for maybe_bump_dir_info():

   In a single atomic action, (1) remove any 'deleted' entries from a
   directory, (2) remove any 'absent' entries whose revision numbers
   are different from the parent's new target revision, (3) remove any
   'missing' dir entries, and (4) remove the directory's 'incomplete'
   flag. */
static svn_error_t *
complete_directory(struct edit_baton *eb,
                   const char *path,
                   svn_boolean_t is_root_dir,
                   apr_pool_t *pool)
{
  svn_wc_adm_access_t *adm_access;
  apr_hash_t *entries;
  svn_wc_entry_t *entry;
  apr_hash_index_t *hi;
  apr_pool_t *subpool;
  svn_wc_entry_t *current_entry;
  const char *name;

  /* If this is the root directory and there is a target, we can't
     mark this directory complete. */
  if (is_root_dir && *eb->target)
    return SVN_NO_ERROR;

  /* All operations are on the in-memory entries hash. */
  SVN_ERR(svn_wc_adm_retrieve(&adm_access, eb->adm_access, path, pool));
  SVN_ERR(svn_wc_entries_read(&entries, adm_access, TRUE, pool));

  /* Mark THIS_DIR complete. */
  entry = apr_hash_get(entries, SVN_WC_ENTRY_THIS_DIR, APR_HASH_KEY_STRING);
  if (! entry)
    return svn_error_createf(SVN_ERR_ENTRY_NOT_FOUND, NULL,
                             _("No '.' entry in: '%s'"),
                             svn_path_local_style(path, pool));
  entry->incomplete = FALSE;

  /* After a depth upgrade the entry must reflect the new depth.
     Upgrading to infinity changes the depth of *all* directories,
     upgrading to something else only changes the target. */
  if (eb->depth_is_sticky &&
      (eb->requested_depth == svn_depth_infinity
       || (strcmp(path, svn_path_join(eb->anchor, eb->target, pool)) == 0
           && eb->requested_depth > entry->depth)))
    entry->depth = eb->requested_depth;

  /* Remove any deleted or missing entries. */
  subpool = svn_pool_create(pool);
  for (hi = apr_hash_first(pool, entries); hi; hi = apr_hash_next(hi))
    {
      const void *key;
      void *val;

      svn_pool_clear(subpool);
      apr_hash_this(hi, &key, NULL, &val);
      name = key;
      current_entry = val;

      /* Any entry still marked as deleted (and not schedule add) can now
         be removed -- if it wasn't undeleted by the update, then it
         shouldn't stay in the updated working set.  Schedule add items
         should remain.
      */
      if (current_entry->deleted)
        {
          if (current_entry->schedule != svn_wc_schedule_add)
            svn_wc__entry_remove(entries, name);
          else
            {
              svn_wc_entry_t tmpentry;
              tmpentry.deleted = FALSE;
              SVN_ERR(svn_wc__entry_modify(adm_access, current_entry->name,
                                           &tmpentry,
                                           SVN_WC__ENTRY_MODIFY_DELETED,
                                           FALSE, subpool));
            }
        }
      /* An absent entry might have been reconfirmed as absent, and the way
         we can tell is by looking at its revision number: a revision
         number different from the target revision of the update means the
         update never mentioned the item, so the entry should be
         removed. */
      else if (current_entry->absent
               && (current_entry->revision != *(eb->target_revision)))
        {
          svn_wc__entry_remove(entries, name);
        }
      else if (current_entry->kind == svn_node_dir)
        {
          const char *child_path = svn_path_join(path, name, subpool);

          if ((svn_wc__adm_missing(adm_access, child_path))
              && (! current_entry->absent)
              && (current_entry->schedule != svn_wc_schedule_add))
            {
              svn_wc__entry_remove(entries, name);
              if (eb->notify_func)
                {
                  svn_wc_notify_t *notify
                    = svn_wc_create_notify(child_path,
                                           svn_wc_notify_update_delete,
                                           subpool);
                  notify->kind = current_entry->kind;
                  (* eb->notify_func)(eb->notify_baton, notify, subpool);
                }
            }
        }
    }

  svn_pool_destroy(subpool);

  /* An atomic write of the whole entries file. */
  SVN_ERR(svn_wc__entries_write(entries, adm_access, pool));

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
        SVN_ERR(complete_directory(eb, bdi->path,
                                   bdi->parent ? FALSE : TRUE, pool));
    }
  /* we exited the for loop because there are no more parents */

  return SVN_NO_ERROR;
}

struct file_baton
{
  /* The global edit baton. */
  struct edit_baton *edit_baton;

  /* The parent directory of this file. */
  struct dir_baton *dir_baton;

  /* Pool specific to this file_baton. */
  apr_pool_t *pool;

  /* Name of this file (its entry in the directory). */
  const char *name;

  /* Path to this file, either abs or relative to the change-root. */
  const char *path;

  /* The repository URL this file will correspond to. */
  const char *new_URL;

  /* Set if this file is new. */
  svn_boolean_t added;

  /* Set if this file is new with history. */
  svn_boolean_t added_with_history;

  /* Set if this file is skipped because it was in conflict. */
  svn_boolean_t skipped;

  /* Set if an unversioned file of the same name already existed in
     this directory. */
  svn_boolean_t existed;

  /* Set if a file of the same name already exists and is
     scheduled for addition without history. */
  svn_boolean_t add_existed;

  /* The path to the current text base, if any.
     This gets set if there are file content changes. */
  const char *text_base_path;

  /* The path to the incoming text base (that is, to a text-base-file-
     in-progress in the tmp area).  This gets set if there are file
     content changes. */
  const char *new_text_base_path;

  /* If this file was added with history, this is the path to a copy
     of the text base of the copyfrom file (in the temporary area). */
  const char *copied_text_base;

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

  /* This is initialized to all zeroes when the baton is created, then
     populated with the MD5 digest of the resultant fulltext after the
     last window is handled by the handler returned from
     apply_textdelta(). */
  unsigned char digest[APR_MD5_DIGESTSIZE];
};


/* Make a new file baton in the provided POOL, with PB as the parent baton.
   PATH is relative to the root of the edit. */

static svn_error_t *
make_file_baton(struct file_baton **f_p,
                struct dir_baton *pb,
                const char *path,
                svn_boolean_t adding,
                apr_pool_t *pool)
{
  struct file_baton *f = apr_pcalloc(pool, sizeof(*f));

  /* I rather need this information, yes. */
  if (! path)
    abort();

  /* Make the file's on-disk name. */
  f->path = svn_path_join(pb->edit_baton->anchor, path, pool);
  f->name = svn_path_basename(path, pool);

  /* Figure out the new_URL for this file. */
  if (pb->edit_baton->switch_url)
    {
      f->new_URL = svn_path_url_add_component(pb->new_URL, f->name, pool);
    }
  else
    {
      f->new_URL = get_entry_url(pb->edit_baton->adm_access,
                                 pb->path, f->name, pool);
    }

  f->pool              = pool;
  f->edit_baton        = pb->edit_baton;
  f->propchanges       = apr_array_make(pool, 1, sizeof(svn_prop_t));
  f->bump_info         = pb->bump_info;
  f->added             = adding;
  f->existed           = FALSE;
  f->add_existed       = FALSE;
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
  svn_error_t *err, *err2;

  /* Apply this window.  We may be done at that point.  */
  err = hb->apply_handler(window, hb->apply_baton);
  if (window != NULL && !err)
    return err;

  /* Either we're done (window is NULL) or we had an error.  In either
     case, clean up the handler.  */
  if (hb->source)
    {
      if (fb->copied_text_base)
        err2 = svn_io_file_close(hb->source, hb->pool);
      else
        err2 = svn_wc__close_text_base(hb->source, fb->path, 0, hb->pool);

      if (err2 && !err)
        err = err2;
      else
        svn_error_clear(err2);
    }
  err2 = svn_wc__close_text_base(hb->dest, fb->path, 0, hb->pool);
  if (err2)
    {
      if (!err)
        err = err2;
      else
        svn_error_clear(err2);
    }

  if (err)
    {
      /* We failed to apply the delta; clean up the temporary file.  */
      svn_error_clear(svn_io_remove_file(fb->new_text_base_path, hb->pool));
      fb->new_text_base_path = NULL;
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

  /* Make sure the directory exists. */
  SVN_ERR(svn_wc__ensure_directory(db->path, pool));

  /* Use the repository root of the anchor, but only if it actually is an
     ancestor of the URL of this directory. */
  if (db->edit_baton->repos
      && svn_path_is_ancestor(db->edit_baton->repos, ancestor_url))
    repos = db->edit_baton->repos;
  else
    repos = NULL;

  /* Make sure it's the right working copy, either by creating it so,
     or by checking that it is so already. */
  SVN_ERR(svn_wc_ensure_adm3(db->path, NULL,
                             ancestor_url, repos,
                             ancestor_revision, db->ambient_depth, pool));

  if (! db->edit_baton->adm_access
      || strcmp(svn_wc_adm_access_path(db->edit_baton->adm_access),
                db->path))
    {
      svn_wc_adm_access_t *adm_access;
      apr_pool_t *adm_access_pool
        = db->edit_baton->adm_access
        ? svn_wc_adm_access_pool(db->edit_baton->adm_access)
        : db->edit_baton->pool;
      svn_error_t *err = svn_wc_adm_open3(&adm_access,
                                          db->edit_baton->adm_access,
                                          db->path, TRUE, 0, NULL, NULL,
                                          adm_access_pool);

      /* db->path may be scheduled for addition without history.
         In that case db->edit_baton->adm_access already has it locked. */
      if (err && err->apr_err == SVN_ERR_WC_LOCKED)
        {
           svn_error_clear(err);
           err = svn_wc_adm_retrieve(&adm_access,
                                     db->edit_baton->adm_access,
                                     db->path, adm_access_pool);
        }

      SVN_ERR(err);

      if (!db->edit_baton->adm_access)
        db->edit_baton->adm_access = adm_access;
    }

  return SVN_NO_ERROR;
}


/* Accumulate tags in LOG_ACCUM to set ENTRY_PROPS for PATH.
   ENTRY_PROPS is an array of svn_prop_t* entry props.
   If ENTRY_PROPS contains the removal of a lock token, all entryprops
   related to a lock will be removed and LOCK_STATE, if non-NULL, will be
   set to svn_wc_notify_lock_state_unlocked.  Else, LOCK_STATE, if non-NULL
   will be set to svn_wc_lock_state_unchanged. */
static svn_error_t *
accumulate_entry_props(svn_stringbuf_t *log_accum,
                       svn_wc_notify_lock_state_t *lock_state,
                       svn_wc_adm_access_t *adm_access,
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
          SVN_ERR(svn_wc__loggy_delete_lock
                  (&log_accum, adm_access, path, pool));

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
      else if (! strcmp(prop->name, SVN_PROP_ENTRY_UUID))
        {
          flags |= SVN_WC__ENTRY_MODIFY_UUID;
          tmp_entry.uuid = val;
        }
    }

  if (flags)
    SVN_ERR(svn_wc__loggy_entry_modify(&log_accum, adm_access, path,
                                       &tmp_entry, flags, pool));

  return SVN_NO_ERROR;
}


/* Accumulate tags in LOG_ACCUM to set WCPROPS for PATH.  WCPROPS is
   an array of svn_prop_t* wc props. */
static svn_error_t *
accumulate_wcprops(svn_stringbuf_t *log_accum,
                   svn_wc_adm_access_t *adm_access,
                   const char *path,
                   apr_array_header_t *wcprops,
                   apr_pool_t *pool)
{
  int i;

  /* ### The log file will rewrite the props file for each property :( It
     ### would be better if all the changes could be combined into one
     ### write. */
  for (i = 0; i < wcprops->nelts; ++i)
    {
      const svn_prop_t *prop = &APR_ARRAY_IDX(wcprops, i, svn_prop_t);

      SVN_ERR(svn_wc__loggy_modify_wcprop
              (&log_accum, adm_access, path,
               prop->name, prop->value ? prop->value->data : NULL, pool));
    }

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
  apr_status_t path_status;

  path_status = apr_filepath_merge
    (&full_path, base_path, add_path,
     APR_FILEPATH_NOTABOVEROOT | APR_FILEPATH_SECUREROOTTEST,
     pool);

  if (path_status != APR_SUCCESS)
    {
      return svn_error_createf
        (SVN_ERR_WC_OBSTRUCTED_UPDATE, NULL,
         _("Path '%s' is not in the working copy"),
         /* Not using full_path here because it might be NULL or
            undefined, since apr_filepath_merge() returned error.
            (Pity we can't pass NULL for &full_path in the first place,
            but the APR docs don't bless that.) */
         svn_path_local_style(svn_path_join(base_path, add_path, pool), pool));
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
  struct dir_baton *d;

  /* Note that something interesting is actually happening in this
     edit run. */
  eb->root_opened = TRUE;

  SVN_ERR(make_dir_baton(&d, NULL, eb, NULL, FALSE, pool));
  *dir_baton = d;

  if (! *eb->target)
    {
      /* For an update with a NULL target, this is equivalent to open_dir(): */
      svn_wc_adm_access_t *adm_access;
      svn_wc_entry_t tmp_entry;
      const svn_wc_entry_t *entry;
      apr_uint64_t flags = SVN_WC__ENTRY_MODIFY_REVISION |
        SVN_WC__ENTRY_MODIFY_URL | SVN_WC__ENTRY_MODIFY_INCOMPLETE;

      /* Read the depth from the entry. */
      SVN_ERR(svn_wc_entry(&entry, d->path, eb->adm_access,
                           FALSE, pool));
      if (entry)
        d->ambient_depth = entry->depth;

      /* Mark directory as being at target_revision, but incomplete. */
      tmp_entry.revision = *(eb->target_revision);
      tmp_entry.url = d->new_URL;
      /* See open_directory() for why this check is necessary. */
      if (eb->repos && svn_path_is_ancestor(eb->repos, d->new_URL))
        {
          tmp_entry.repos = eb->repos;
          flags |= SVN_WC__ENTRY_MODIFY_REPOS;
        }
      tmp_entry.incomplete = TRUE;
      SVN_ERR(svn_wc_adm_retrieve(&adm_access, eb->adm_access,
                                  d->path, pool));
      SVN_ERR(svn_wc__entry_modify(adm_access, NULL /* THIS_DIR */,
                                   &tmp_entry, flags,
                                   TRUE /* immediate write */,
                                   pool));
    }

  return SVN_NO_ERROR;
}


/* Helper for delete_entry().

   Search an error chain (ERR) for evidence that a local mod was left.
   If so, cleanup LOGFILE and return an appropriate error.  Otherwise,
   just return the original error chain.
*/
static svn_error_t *
leftmod_error_chain(svn_error_t *err,
                    const char *logfile,
                    const char *path,
                    apr_pool_t *pool)
{
  svn_error_t *tmp_err;

  if (! err)
    return SVN_NO_ERROR;

  /* Advance TMP_ERR to the part of the error chain that reveals that
     a local mod was left, or to the NULL end of the chain. */
  for (tmp_err = err; tmp_err; tmp_err = tmp_err->child)
    if (tmp_err->apr_err == SVN_ERR_WC_LEFT_LOCAL_MOD)
      break;

  /* If we found a "left a local mod" error, wrap and return it.
     Otherwise, we just return our top-most error. */
  if (tmp_err)
    {
      /* Remove the LOGFILE (and eat up errors from this process). */
      svn_error_clear(svn_io_remove_file(logfile, pool));

      return svn_error_createf
        (SVN_ERR_WC_OBSTRUCTED_UPDATE, tmp_err,
         _("Won't delete locally modified directory '%s'"),
         svn_path_local_style(path, pool));
    }

  return err;
}

/* Set *MODIFIED to true iff the item described by ENTRY has local
 * modifications.
 * For a file, this means text mods or property mods.
 * For a directory, this means property mods.
 * PARENT_ADM_ACCESS is the admin access baton of FULL_PATH's parent dir.
 */
static svn_error_t *
entry_has_local_mods(svn_boolean_t *modified,
                     svn_wc_adm_access_t *parent_adm_access,
                     const svn_wc_entry_t *entry,
                     const char *full_path,
                     apr_pool_t *pool)
{
  svn_boolean_t text_modified;
  svn_boolean_t props_modified;
  svn_wc_adm_access_t *adm_access;

  /* one way */
  /*  if (entry && (entry->schedule != svn_wc_schedule_normal
                    || entry->has_prop_mods
                    || (entry-kind == svn_node_file && ...modified...)))
  */

  /* another way */
  if (entry->kind == svn_node_file)
    {
      SVN_ERR(svn_wc_text_modified_p(&text_modified, full_path, FALSE,
                                     parent_adm_access, pool));
      adm_access = parent_adm_access;
    }
  else
    {
      text_modified = FALSE;
      SVN_ERR(svn_wc_adm_retrieve(&adm_access, parent_adm_access, full_path, pool));
    }
  SVN_ERR(svn_wc_props_modified_p(&props_modified, full_path,
                                  adm_access, pool));

  *modified = (text_modified || props_modified);
  return SVN_NO_ERROR;
}


/* Raise a tree conflict on the parent directory if the ACTION on FULL_PATH
 * would conflict with FULL_PATH's scheduled change. ENTRY must be the wc-entry
 * for FULL_PATH, if there is one (even if schedule-delete etc.), or NULL if
 * FULL_PATH is unversioned.
 * PARENT_ADM_ACCESS is the admin access baton of FULL_PATH's parent directory.
 */

static svn_error_t *
check_tree_conflict(svn_stringbuf_t *log_accum,
                    const char *full_path,
                    const svn_wc_entry_t *entry,
                    svn_wc_adm_access_t *parent_adm_access,
                    svn_wc_conflict_action_t action,
                    apr_pool_t *pool)
{
  svn_wc_conflict_reason_t reason = (svn_wc_conflict_reason_t)(-1);

  /* Test whether CONFLICT_ACTION conflicts with the state of ENTRY.
   * If so, set CONFLICT_REASON to an appropriate value. */
  switch (action)
    {
    case svn_wc_conflict_action_edit:
      if (!entry)
        reason = svn_wc_conflict_reason_missing;
      else if (entry->schedule == svn_wc_schedule_delete
               || entry->schedule == svn_wc_schedule_replace)
        reason = svn_wc_conflict_reason_deleted;
      break;

    case svn_wc_conflict_action_add:
      if (entry)
        reason = svn_wc_conflict_reason_added;
      break;

    case svn_wc_conflict_action_delete:
      if (!entry)
        reason = svn_wc_conflict_reason_missing;
      else if (entry->schedule != svn_wc_schedule_normal)
        /* If we are about to delete a path that has been scheduled
         * for deletion, mark the containing directory as tree conflicted.
         * This _could_ be tree conflict use case 3 as described in the
         * paper attached to issue #2282
         *
         * XXX: Flagging every delete of an already deleted file by the
         * update as a tree conflict causes false positives.
         * Use case 3 actually only applies if the file that was locally
         * deleted and the file deleted by the update have a common ancestor.
         * Getting at this information is very hard though without proper
         * rename tracking. So currently, this is the best we can do.
         * See also notes/tree-conflicts/detection.txt
         */
        reason = (entry->schedule == svn_wc_schedule_delete
                  ? svn_wc_conflict_reason_deleted
                  : svn_wc_conflict_reason_obstructed);  /* replace, add, etc. */
      else
        {
          svn_boolean_t modified;

          /* If we are about to delete a path that has local mods,
           * mark the containing directory as tree conflicted.
           * This is tree conflict use case 2 as described in the
           * paper attached to issue #2282
           * See also notes/tree-conflicts/detection.txt
           */
          /* ### TODO: also detect deep modifications in a directory tree */
          SVN_ERR(entry_has_local_mods(&modified, parent_adm_access, entry,
                                       full_path, pool));
          if (modified)
            {
              reason = svn_wc_conflict_reason_edited;
            }
        }
      break;
    }

  if (reason != (svn_wc_conflict_reason_t)(-1))
    {
      svn_wc_conflict_description_t *conflict;

      /* The entry is a tree conflict victim. */
      conflict = apr_pcalloc(pool, sizeof(svn_wc_conflict_description_t));
      conflict->victim_path = (strcmp(entry->name, SVN_WC_ENTRY_THIS_DIR) == 0
                               ? full_path
                               : entry->name);
      conflict->node_kind = entry->kind;
      conflict->operation = svn_wc_operation_update;
      conflict->action = action;
      conflict->reason = reason;

      SVN_ERR(svn_wc__loggy_add_tree_conflict_data(log_accum, conflict,
                                                   parent_adm_access, pool));
    }

  return SVN_NO_ERROR;
}

/* Helper for delete_entry(). */
/* PARENT_ADM_ACCESS is the admin access baton for the parent directory,
 * or NULL if this is the target of the "update" being deleted.
 */
static svn_error_t *
do_entry_deletion(struct edit_baton *eb,
                  const char *parent_path,
                  const char *path,
                  int *log_number,
                  svn_wc_adm_access_t *parent_adm_access,
                  apr_pool_t *pool)
{
  const svn_wc_entry_t *entry;
  const char *full_path = svn_path_join(eb->anchor, path, pool);
  svn_stringbuf_t *log_item = svn_stringbuf_create("", pool);

  SVN_ERR(svn_wc_entry(&entry, full_path, parent_adm_access, FALSE, pool));

  if (parent_adm_access)
    SVN_ERR(check_tree_conflict(log_item, full_path, entry, parent_adm_access,
                                svn_wc_conflict_action_delete, pool));

  SVN_ERR(svn_wc__loggy_delete_entry(&log_item, parent_adm_access, full_path,
                                     pool));

  /* If the thing being deleted is the *target* of this update, then
     we need to recreate a 'deleted' entry, so that parent can give
     accurate reports about itself in the future. */
  if (strcmp(path, eb->target) == 0)
    {
      svn_wc_entry_t tmp_entry;

      tmp_entry.revision = *(eb->target_revision);
      tmp_entry.kind =
        (entry->kind == svn_node_file) ? svn_node_file : svn_node_dir;  /* ### redundant? */
      tmp_entry.deleted = TRUE;

      SVN_ERR(svn_wc__loggy_entry_modify(&log_item, parent_adm_access,
                                         full_path, &tmp_entry,
                                         SVN_WC__ENTRY_MODIFY_REVISION
                                         | SVN_WC__ENTRY_MODIFY_KIND  /* ### redundant change? */
                                         | SVN_WC__ENTRY_MODIFY_DELETED,
                                         pool));

      eb->target_deleted = TRUE;
    }

  SVN_ERR(svn_wc__write_log(parent_adm_access, *log_number, log_item, pool));

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
          svn_wc_adm_access_t *child_access;
          const char *logfile_path
            = svn_wc__adm_path(parent_path, FALSE, pool,
                               svn_wc__logfile_path(*log_number, pool), NULL);

          SVN_ERR(svn_wc_adm_retrieve
                  (&child_access, eb->adm_access,
                   full_path, pool));

          SVN_ERR(leftmod_error_chain
                  (svn_wc_remove_from_revision_control
                   (child_access,
                    SVN_WC_ENTRY_THIS_DIR,
                    TRUE, /* destroy */
                    TRUE, /* instant error */
                    eb->cancel_func,
                    eb->cancel_baton,
                    pool),
                   logfile_path, parent_path, pool));
        }
    }

  SVN_ERR(svn_wc__run_log(parent_adm_access, NULL, pool));
  *log_number = 0;

  if (eb->notify_func)
    (*eb->notify_func)
      (eb->notify_baton,
       svn_wc_create_notify(full_path,
                            svn_wc_notify_update_delete, pool), pool);

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
  svn_wc_adm_access_t *parent_adm_access;

  SVN_ERR(check_path_under_root(pb->path, svn_path_basename(path, pool),
                                pool));
  SVN_ERR(svn_wc_adm_retrieve(&parent_adm_access, pb->edit_baton->adm_access,
                              pb->path, pool));

  return do_entry_deletion(pb->edit_baton, pb->path, path, &pb->log_number,
                           parent_adm_access, pool);
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

  SVN_ERR(make_dir_baton(&db, path, eb, pb, TRUE, pool));
  *child_baton = db;

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

  /* Semantic check.  Either both "copyfrom" args are valid, or they're
     NULL and SVN_INVALID_REVNUM.  A mixture is illegal semantics. */
  if ((copyfrom_path && (! SVN_IS_VALID_REVNUM(copyfrom_revision)))
      || ((! copyfrom_path) && (SVN_IS_VALID_REVNUM(copyfrom_revision))))
    abort();

  SVN_ERR(check_path_under_root(pb->path, db->name, pool));
  SVN_ERR(svn_io_check_path(db->path, &kind, db->pool));

  /* The path can exist, but it must be a directory... */
  if (kind == svn_node_file || kind == svn_node_unknown)
    {
      /* ### TODO: raise a tree conflict */

    return svn_error_createf
      (SVN_ERR_WC_OBSTRUCTED_UPDATE, NULL,
       _("Failed to add directory '%s': a non-directory object of the "
         "same name already exists"),
       svn_path_local_style(db->path, pool));
    }

  if (kind == svn_node_dir)
    {
      /* ...Ok, it's a directory but it can't be versioned or
         scheduled for addition with history. */
      svn_wc_adm_access_t *adm_access;

      /* Test the obstructing dir to see if it's versioned. */
      svn_error_t *err = svn_wc_adm_open3(&adm_access, NULL,
                                          db->path, FALSE, 0,
                                          NULL, NULL, pool);

      if (err && err->apr_err != SVN_ERR_WC_NOT_DIRECTORY)
        {
          /* Something quite unexpected has happened. */
          return err;
        }
      else if (err) /* Not a versioned dir. */
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
              /* ### TODO: raise a tree conflict */

              return svn_error_createf
                (SVN_ERR_WC_OBSTRUCTED_UPDATE, NULL,
                 _("Failed to add directory '%s': an unversioned "
                   "directory of the same name already exists"),
                 svn_path_local_style(db->path, pool));
            }
        }
      else /* Obstructing dir *is* versioned or scheduled for addition. */
        {
          const svn_wc_entry_t *entry;

          SVN_ERR(svn_wc_entry(&entry, db->path, adm_access, FALSE, pool));

          /* Anything other than a dir scheduled for addition without
             history is an error. */
          /* ### what's this "add_existed" clause for? */
          if (entry
              && entry->schedule == svn_wc_schedule_add
              && ! entry->copied)
            {
              db->add_existed = TRUE;
            }
          else
            {
              svn_wc_adm_access_t *parent_adm_access;
              const char *repos;
              /* Use the repository root of the anchor, but only if it 
                 actually is an ancestor of the URL of this directory. */
              if (eb->repos && svn_path_is_ancestor(eb->repos, db->new_URL))
                repos = eb->repos;
              else
                repos = NULL;

              /* Make sure it's the right working copy. */
              SVN_ERR(svn_wc_ensure_adm3(db->path,
                                         NULL /* TODO check uuid too.*/,
                                         db->new_URL, repos,
                                         *(eb->target_revision),
                                         db->ambient_depth, pool));

              SVN_ERR(svn_wc_adm_retrieve(&parent_adm_access, eb->adm_access,
                                          pb->path, pool));

              /* Raise a tree conflict if this directory is already present. */
              SVN_ERR(check_tree_conflict(pb->log_accum, db->path, entry,
                                          parent_adm_access,
                                          svn_wc_conflict_action_add, pool));

              /*
              return svn_error_createf
                (SVN_ERR_WC_OBSTRUCTED_UPDATE, NULL,
                 _("Failed to add directory '%s': a versioned "
                   "directory of the same name already exists"),
                 svn_path_local_style(db->path, pool));
              */
            }
        }
    }

  /* It may not be named the same as the administrative directory. */
  if (svn_wc_is_adm_dir(svn_path_basename(path, pool), pool))
    return svn_error_createf
      (SVN_ERR_WC_OBSTRUCTED_UPDATE, NULL,
       _("Failed to add directory '%s': object of the same name as the "
         "administrative directory"),
       svn_path_local_style(db->path, pool));

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
      return svn_error_createf
        (SVN_ERR_UNSUPPORTED_FEATURE, NULL,
         _("Failed to add directory '%s': "
           "copyfrom arguments not yet supported"),
         svn_path_local_style(db->path, pool));
    }
  else  /* ...or we got invalid copyfrom args. */
    {
      svn_wc_adm_access_t *adm_access;
      svn_wc_entry_t tmp_entry;
      apr_uint64_t modify_flags = SVN_WC__ENTRY_MODIFY_KIND |
        SVN_WC__ENTRY_MODIFY_DELETED | SVN_WC__ENTRY_MODIFY_ABSENT;

      SVN_ERR(svn_wc_adm_retrieve(&adm_access, eb->adm_access,
                                  pb->path, db->pool));

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

      SVN_ERR(svn_wc__entry_modify(adm_access, db->name, &tmp_entry,
                                   modify_flags,
                                   TRUE /* immediate write */, pool));

      if (db->add_existed)
        {
          /* Immediately tweak the schedule for "this dir" so it too
             is no longer scheduled for addition.  Change rev from 0
             to the target revision allowing prep_directory() to do
             its thing without error. */
          modify_flags  = SVN_WC__ENTRY_MODIFY_SCHEDULE
            | SVN_WC__ENTRY_MODIFY_FORCE | SVN_WC__ENTRY_MODIFY_REVISION;

          SVN_ERR(svn_wc_adm_retrieve(&adm_access,
                                      db->edit_baton->adm_access,
                                      db->path, pool));
          tmp_entry.revision = *(eb->target_revision);

          if (eb->switch_url)
            {
              tmp_entry.url = svn_path_url_add_component(eb->switch_url,
                                                         db->name, pool);
              modify_flags |= SVN_WC__ENTRY_MODIFY_URL;
            }

          SVN_ERR(svn_wc__entry_modify(adm_access, NULL, &tmp_entry,
                                       modify_flags,
                                       TRUE /* immediate write */, pool));
        }
    }

  SVN_ERR(prep_directory(db,
                         db->new_URL,
                         *(eb->target_revision),
                         db->pool));

  /* If this add was obstructed by dir scheduled for addition without
     history let close_file() handle the notification because there
     might be properties to deal with. */
  if (eb->notify_func && !(db->add_existed))
    {
      svn_wc_notify_t *notify = svn_wc_create_notify(
        db->path,
        db->existed ?
        svn_wc_notify_exists : svn_wc_notify_update_add,
        pool);
      notify->kind = svn_node_dir;
      (*eb->notify_func)(eb->notify_baton, notify, pool);
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
  const svn_wc_entry_t *entry;
  svn_wc_entry_t tmp_entry;
  apr_uint64_t flags = SVN_WC__ENTRY_MODIFY_REVISION |
    SVN_WC__ENTRY_MODIFY_URL | SVN_WC__ENTRY_MODIFY_INCOMPLETE;

  svn_wc_adm_access_t *adm_access;
  svn_wc_adm_access_t *parent_adm_access;

  SVN_ERR(make_dir_baton(&db, path, eb, pb, FALSE, pool));
  *child_baton = db;

  /* Flush the log for the parent directory before going into this subtree. */
  SVN_ERR(flush_log(pb, pool));

  SVN_ERR(check_path_under_root(pb->path, db->name, pool));

  SVN_ERR(svn_wc_adm_retrieve(&adm_access, eb->adm_access,
                              db->path, pool));
  SVN_ERR(svn_wc_adm_retrieve(&parent_adm_access, eb->adm_access,
                              pb->path, pool));

  /* Skip this directory if it has property or tree conflicts. */
  SVN_ERR(svn_wc_entry(&entry, db->path, adm_access, FALSE, pool));
  if (entry)
    {
      /* Text conflicts can't happen for a directory, but we need to supply
         all flags. */
      svn_boolean_t text_conflicted;
      svn_boolean_t prop_conflicted;
      svn_boolean_t tree_conflicted;

      db->ambient_depth = entry->depth;

      SVN_ERR(svn_wc_conflicted_p2(&text_conflicted, &prop_conflicted,
                                   &tree_conflicted, db->path, entry, pool));
      assert(! text_conflicted);
      if (prop_conflicted || tree_conflicted)
        {
          db->bump_info->skipped = TRUE;
          apr_hash_set(eb->skipped_paths, apr_pstrdup(eb->pool, db->path),
                       APR_HASH_KEY_STRING, (void*)1);
          if (eb->notify_func)
            {
              svn_wc_notify_t *notify
                = svn_wc_create_notify(db->path, svn_wc_notify_skip, pool);
              notify->kind = svn_node_dir;
              notify->prop_state = svn_wc_notify_state_conflicted;
              (*eb->notify_func)(eb->notify_baton, notify, pool);
            }
          return SVN_NO_ERROR;
        }
    }

  /* Raise a tree conflict if scheduled for deletion or similar. */
  SVN_ERR(check_tree_conflict(pb->log_accum, db->path, entry,
                              parent_adm_access, svn_wc_conflict_action_edit,
                              pool));

  /* Mark directory as being at target_revision and URL, but incomplete. */
  tmp_entry.revision = *(eb->target_revision);
  tmp_entry.url = db->new_URL;
  /* In some situations, the URL of this directory does not have the same
     repository root as the anchor of the update; we can't just blindly
     use the that repository root here, so make sure it is really an
     ancestor. */
  if (eb->repos && svn_path_is_ancestor(eb->repos, db->new_URL))
    {
      tmp_entry.repos = eb->repos;
      flags |= SVN_WC__ENTRY_MODIFY_REPOS;
    }
  tmp_entry.incomplete = TRUE;

  SVN_ERR(svn_wc__entry_modify(adm_access, NULL /* THIS_DIR */,
                               &tmp_entry, flags,
                               TRUE /* immediate write */,
                               pool));

  return SVN_NO_ERROR;
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

  if (db->bump_info->skipped)
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

/* An svn_delta_editor_t function. */
static svn_error_t *
close_directory(void *dir_baton,
                apr_pool_t *pool)
{
  struct dir_baton *db = dir_baton;
  svn_wc_notify_state_t prop_state = svn_wc_notify_state_unknown;
  apr_array_header_t *entry_props, *wc_props, *regular_props;
  svn_wc_adm_access_t *adm_access;
  const svn_wc_entry_t *entry;
  svn_boolean_t tree_conflicted;
  svn_boolean_t text_conflicted, prop_conflicted; /* Dummies (never read). */

  SVN_ERR(svn_categorize_props(db->propchanges, &entry_props, &wc_props,
                               &regular_props, pool));

  SVN_ERR(svn_wc_adm_retrieve(&adm_access, db->edit_baton->adm_access,
                              db->path, db->pool));

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
          if (db->edit_baton->traversal_info)
            {
              svn_wc_traversal_info_t *ti = db->edit_baton->traversal_info;
              const svn_prop_t *change = externals_prop_changed(regular_props);

              if (change)
                {
                  const svn_string_t *new_val_s = change->value;
                  const svn_string_t *old_val_s;

                  SVN_ERR(svn_wc_prop_get
                          (&old_val_s, SVN_PROP_EXTERNALS,
                           db->path, adm_access, db->pool));

                  if ((new_val_s == NULL) && (old_val_s == NULL))
                    ; /* No value before, no value after... so do nothing. */
                  else if (new_val_s && old_val_s
                           && (svn_string_compare(old_val_s, new_val_s)))
                    ; /* Value did not change... so do nothing. */
                  else if (old_val_s || new_val_s)
                    /* something changed, record the change */
                    {
                      const char *d_path = apr_pstrdup(ti->pool, db->path);

                      apr_hash_set(ti->depths, d_path, APR_HASH_KEY_STRING,
                                   svn_depth_to_word(db->ambient_depth));

                      /* We can't assume that ti came pre-loaded with
                         the old values of the svn:externals property.
                         Yes, most callers will have already
                         initialized ti by sending it through
                         svn_wc_crawl_revisions, but we shouldn't
                         count on that here -- so we set both the old
                         and new values again. */
                      if (old_val_s)
                        apr_hash_set(ti->externals_old, d_path,
                                     APR_HASH_KEY_STRING,
                                     apr_pstrmemdup(ti->pool, old_val_s->data,
                                                    old_val_s->len));
                      if (new_val_s)
                        apr_hash_set(ti->externals_new, d_path,
                                     APR_HASH_KEY_STRING,
                                     apr_pstrmemdup(ti->pool, new_val_s->data,
                                                    new_val_s->len));
                    }
                }
            }

          /* Merge pending properties into temporary files (ignoring
             conflicts). */
          SVN_ERR_W(svn_wc__merge_props(&prop_state,
                                        adm_access, db->path,
                                        NULL /* use baseprops */,
                                        NULL, NULL,
                                        regular_props, TRUE, FALSE,
                                        db->edit_baton->conflict_func,
                                        db->edit_baton->conflict_baton,
                                        db->pool, &dirprop_log),
                    _("Couldn't do property merge"));
        }

      SVN_ERR(accumulate_entry_props(dirprop_log, NULL,
                                     adm_access, db->path,
                                     entry_props, pool));

      SVN_ERR(accumulate_wcprops(dirprop_log, adm_access,
                                 db->path, wc_props, pool));

      /* Add the dirprop loggy entries to the baton's log
         accumulator. */
      svn_stringbuf_appendstr(db->log_accum, dirprop_log);
    }

  /* Flush and run the log. */
  SVN_ERR(flush_log(db, pool));
  SVN_ERR(svn_wc__run_log(adm_access, db->edit_baton->diff3_cmd, db->pool));
  db->log_number = 0;

  /* We're done with this directory, so remove one reference from the
     bump information. This may trigger a number of actions. See
     maybe_bump_dir_info() for more information.  */
  SVN_ERR(maybe_bump_dir_info(db->edit_baton, db->bump_info, db->pool));

  /* Check for tree conflicts in this directory. */
  SVN_ERR(svn_wc_entry(&entry, db->path, adm_access, TRUE, db->pool));
  SVN_ERR(svn_wc_conflicted_p2(&text_conflicted, &prop_conflicted, 
                               &tree_conflicted, db->path, entry, db->pool));

  /* Notify of any prop changes on this directory -- but do nothing
     if it's an added or skipped directory, because notification has already
     happened in that case - unless the add was obstructed by a dir
     scheduled for addition without history, in which case we handle
     notification here). */
  if (! db->bump_info->skipped && (db->add_existed || (! db->added))
      && (db->edit_baton->notify_func))
    {
      svn_wc_notify_t *notify
        = svn_wc_create_notify(db->path,
                               db->existed || db->add_existed
                               ? svn_wc_notify_exists
                               : svn_wc_notify_update_update,
                               pool);
      notify->kind = svn_node_dir;
      notify->prop_state = prop_state;
      notify->content_state = tree_conflicted
          ? svn_wc_notify_state_conflicted
          : svn_wc_notify_state_unknown;
    (*db->edit_baton->notify_func)(db->edit_baton->notify_baton,
                                   notify, pool);
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
  const char *name = svn_path_basename(path, pool);
  struct dir_baton *pb = parent_baton;
  struct edit_baton *eb = pb->edit_baton;
  svn_wc_adm_access_t *adm_access;
  apr_hash_t *entries;
  const svn_wc_entry_t *ent;
  svn_wc_entry_t tmp_entry;

  /* Extra check: an item by this name may not exist, but there may
     still be one scheduled for addition.  That's a genuine
     tree-conflict.  */
  SVN_ERR(svn_wc_adm_retrieve(&adm_access, eb->adm_access, pb->path, pool));
  SVN_ERR(svn_wc_entries_read(&entries, adm_access, FALSE, pool));
  ent = apr_hash_get(entries, name, APR_HASH_KEY_STRING);
  if (ent && (ent->schedule == svn_wc_schedule_add))
    return svn_error_createf
      (SVN_ERR_WC_OBSTRUCTED_UPDATE, NULL,
       _("Failed to mark '%s' absent: item of the same name is already "
         "scheduled for addition"),
       svn_path_local_style(path, pool));

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

  SVN_ERR(svn_wc__entry_modify(adm_access, name, &tmp_entry,
                               (SVN_WC__ENTRY_MODIFY_KIND    |
                                SVN_WC__ENTRY_MODIFY_REVISION |
                                SVN_WC__ENTRY_MODIFY_DELETED |
                                SVN_WC__ENTRY_MODIFY_ABSENT),
                               TRUE /* immediate write */, pool));

  return SVN_NO_ERROR;
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
  svn_wc_adm_access_t *adm_access;
  apr_pool_t *subpool;

  if (copyfrom_path || SVN_IS_VALID_REVNUM(copyfrom_rev))
    {
      /* Sanity checks */
      if (! (copyfrom_path && SVN_IS_VALID_REVNUM(copyfrom_rev)))
        return svn_error_create(SVN_ERR_WC_INVALID_OP_ON_CWD, NULL,
                                  _("Bad copyfrom arguments received"));

      return add_file_with_history(path, parent_baton,
                                   copyfrom_path, copyfrom_rev,
                                   file_baton, pool);
    }

  /* The file_pool can stick around for a *long* time, so we want to
     use a subpool for any temporary allocations. */
  subpool = svn_pool_create(pool);

  SVN_ERR(make_file_baton(&fb, pb, path, TRUE, pool));
  *file_baton = fb;


  SVN_ERR(check_path_under_root(fb->dir_baton->path, fb->name, subpool));

  /* It is interesting to note: everything below is just validation. We
     aren't actually doing any "work" or fetching any persistent data. */

  SVN_ERR(svn_io_check_path(fb->path, &kind, subpool));
  SVN_ERR(svn_wc_adm_retrieve(&adm_access, eb->adm_access,
                              pb->path, subpool));
  SVN_ERR(svn_wc_entry(&entry, fb->path, adm_access, FALSE, subpool));

  /* Sanity checks. */

  /* Raise a tree conflict if there's already something versioned here. */
  SVN_ERR(check_tree_conflict(pb->log_accum, path, entry, adm_access,
                              svn_wc_conflict_action_add, pool));

  /* When adding, there should be nothing with this name unless unversioned
     obstructions are permitted. */
  /* ### " or the obstruction is scheduled for addition
     without history." ??? */
  if (kind != svn_node_none)
    {
      if (eb->allow_unver_obstructions
          || (entry && entry->schedule == svn_wc_schedule_add))
        {
          /* ### now detected as a tree conflict instead.
          if (entry && entry->copied)
            {
              return svn_error_createf(SVN_ERR_WC_OBSTRUCTED_UPDATE,
                                       NULL,
                                       _("Failed to add file '%s': a "
                                         "file of the same name is "
                                         "already scheduled for addition "
                                         "with history"),
                                       svn_path_local_style(fb->path,
                                                            pool));
            }
          */

          /* The name can exist, but it better *really* be a file. */
          if (kind != svn_node_file)
            return svn_error_createf(SVN_ERR_WC_OBSTRUCTED_UPDATE,
                                     NULL,
                                     _("Failed to add file '%s': "
                                       "a non-file object of the same "
                                       "name already exists"),
                                     svn_path_local_style(fb->path,
                                                          pool));

          if (entry)
            fb->add_existed = TRUE; /* Flag as addition without history. */
          else
            fb->existed = TRUE;     /* Flag as unversioned obstruction. */
        }
      else
        {
          return svn_error_createf
            (SVN_ERR_WC_OBSTRUCTED_UPDATE, NULL,
             _("Failed to add file '%s': object of the same name "
               "already exists"), svn_path_local_style(fb->path, pool));
        }
    }
  /* sussman sez: If we're trying to add a file that's already in
     `entries' (but not on disk), that's okay.  It's probably because
     the user deleted the working version and ran 'svn up' as a means
     of getting the file back.

     It certainly doesn't hurt to re-add the file.  We can't possibly
     get the entry showing up twice in `entries', since it's a hash;
     and we know that we won't lose any local mods.  Let the existing
     entry be overwritten. */

  svn_pool_destroy(subpool);

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
  const svn_wc_entry_t *entry;
  svn_node_kind_t kind;
  svn_wc_adm_access_t *adm_access;
  svn_boolean_t text_conflicted;
  svn_boolean_t prop_conflicted;

  /* the file_pool can stick around for a *long* time, so we want to use
     a subpool for any temporary allocations. */
  apr_pool_t *subpool = svn_pool_create(pool);

  SVN_ERR(make_file_baton(&fb, pb, path, FALSE, pool));
  *file_baton = fb;

  SVN_ERR(check_path_under_root(fb->dir_baton->path, fb->name, subpool));


  SVN_ERR(svn_io_check_path(fb->path, &kind, subpool));
  SVN_ERR(svn_wc_adm_retrieve(&adm_access, eb->adm_access,
                              pb->path, subpool));
  SVN_ERR(svn_wc_entry(&entry, fb->path, adm_access, FALSE, subpool));

  /* Sanity check. */

  /* If replacing, make sure the .svn entry already exists. */
  if (! entry)
    return svn_error_createf(SVN_ERR_UNVERSIONED_RESOURCE, NULL,
                             _("File '%s' in directory '%s' "
                               "is not a versioned resource"),
                             fb->name,
                             svn_path_local_style(pb->path, pool));

  /* If the file is scheduled for deletion, we have a tree conflict.
   * This is use case 1 described in the paper attached to issue #2282
   * See also notes/tree-conflicts/detection.txt
   */
  SVN_ERR(check_tree_conflict(pb->log_accum, fb->path, entry, adm_access,
                              svn_wc_conflict_action_edit, pool));

  /* It is interesting to note: everything below is just validation. We
     aren't actually doing any "work" or fetching any persistent data. */

  /* If the file is in conflict, don't mess with it. */
  SVN_ERR(svn_wc_conflicted_p(&text_conflicted, &prop_conflicted,
                              pb->path, entry, pool));
  if (text_conflicted || prop_conflicted)
    {
      fb->skipped = TRUE;
      apr_hash_set(eb->skipped_paths, apr_pstrdup(eb->pool, fb->path),
                   APR_HASH_KEY_STRING, (void*)1);
      if (eb->notify_func)
        {
          svn_wc_notify_t *notify
            = svn_wc_create_notify(fb->path, svn_wc_notify_skip, pool);
          notify->kind = svn_node_file;
          notify->content_state = text_conflicted
            ? svn_wc_notify_state_conflicted
            : svn_wc_notify_state_unknown;
          notify->prop_state = prop_conflicted
            ? svn_wc_notify_state_conflicted
            : svn_wc_notify_state_unknown;
          (*eb->notify_func)(eb->notify_baton, notify, pool);
        }
    }

  svn_pool_destroy(subpool);

  return SVN_NO_ERROR;
}

/* Fill out FB->text_base_path and FB->new_text_base_path to the
   permanent and temporary text-base paths respectively, or (if the
   entry is replaced with history) to the permanent and temporary
   revert-base paths respectively.

   If REPLACED_P and USE_REVERT_BASE are non-NULL, set *REPLACED_P and
   *USE_REVERT_BASE_P to whether or not the entry is replaced, and to
   whether or not it needs to use the revert base (i.e., it's replaced
   with history), respectively.  If CHECKSUM_P is non-NULL and the
   path already has an entry, set *CHECKSUM_P to the entry's checksum.

   Use POOL for temporary allocation and for *CHECKSUM_P (if
   applicable), but allocate FB->text_base_path and
   FB->new_text_base_path in FB->pool. */
static svn_error_t *
choose_base_paths(const char **checksum_p,
                  svn_boolean_t *replaced_p,
                  svn_boolean_t *use_revert_base_p,
                  struct file_baton *fb,
                  apr_pool_t *pool)
{
  struct edit_baton *eb = fb->edit_baton;
  svn_wc_adm_access_t *adm_access;
  const svn_wc_entry_t *ent;
  svn_boolean_t replaced, use_revert_base;

  SVN_ERR(svn_wc_adm_retrieve(&adm_access, eb->adm_access,
                              svn_path_dirname(fb->path, pool), pool));
  SVN_ERR(svn_wc_entry(&ent, fb->path, adm_access, FALSE, pool));

  replaced = ent && ent->schedule == svn_wc_schedule_replace;
  use_revert_base = replaced && (ent->copyfrom_url != NULL);
  if (use_revert_base)
    {
      fb->text_base_path = svn_wc__text_revert_path(fb->path, FALSE, fb->pool);
      fb->new_text_base_path = svn_wc__text_revert_path(fb->path, TRUE,
                                                        fb->pool);
    }
  else
    {
      fb->text_base_path = svn_wc__text_base_path(fb->path, FALSE, fb->pool);
      fb->new_text_base_path = svn_wc__text_base_path(fb->path, TRUE,
                                                      fb->pool);
    }

  if (checksum_p)
    {
      *checksum_p = NULL;
      if (ent)
        *checksum_p = ent->checksum;
    }
  if (replaced_p)
    *replaced_p = replaced;
  if (use_revert_base_p)
    *use_revert_base_p = use_revert_base;

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
  struct handler_baton *hb = apr_palloc(handler_pool, sizeof(*hb));
  svn_error_t *err;
  const char *checksum;
  svn_boolean_t replaced;
  svn_boolean_t use_revert_base;

  if (fb->skipped)
    {
      *handler = svn_delta_noop_window_handler;
      *handler_baton = NULL;
      return SVN_NO_ERROR;
    }

  fb->received_textdelta = TRUE;

  /* Before applying incoming svndiff data to text base, make sure
     text base hasn't been corrupted, and that its checksum
     matches the expected base checksum. */
  SVN_ERR(choose_base_paths(&checksum, &replaced, &use_revert_base,
                            fb, pool));

  /* Only compare checksums if this file has an entry, and the entry has
     a checksum.  If there's no entry, it just means the file is
     created in this update, so there won't be any previously recorded
     checksum to compare against.  If no checksum, well, for backwards
     compatibility we assume that no checksum always matches. */
  if (checksum)
    {
      unsigned char digest[APR_MD5_DIGESTSIZE];
      const char *hex_digest;

      SVN_ERR(svn_io_file_checksum(digest, fb->text_base_path, pool));
      hex_digest = svn_md5_digest_to_cstring_display(digest, pool);

      /* Compare the base_checksum here, rather than in the window
         handler, because there's no guarantee that the handler will
         see every byte of the base file. */
      if (base_checksum)
        {
          if (strcmp(hex_digest, base_checksum) != 0)
            return svn_error_createf
              (SVN_ERR_WC_CORRUPT_TEXT_BASE, NULL,
               _("Checksum mismatch for '%s'; expected: '%s', actual: '%s'"),
               svn_path_local_style(fb->text_base_path, pool), base_checksum,
               hex_digest);
        }

      if (! replaced && strcmp(hex_digest, checksum) != 0)
        {
          return svn_error_createf
            (SVN_ERR_WC_CORRUPT_TEXT_BASE, NULL,
             _("Checksum mismatch for '%s'; recorded: '%s', actual: '%s'"),
             svn_path_local_style(fb->text_base_path, pool), checksum,
             hex_digest);
        }
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
      if (use_revert_base)
        SVN_ERR(svn_wc__open_revert_base(&hb->source, fb->path,
                                         APR_READ,
                                         handler_pool));
      else
        SVN_ERR(svn_wc__open_text_base(&hb->source, fb->path, APR_READ,
                                       handler_pool));

    }
  else
    {
      if (fb->copied_text_base)
        SVN_ERR(svn_io_file_open(&hb->source, fb->copied_text_base,
                                 APR_READ, APR_OS_DEFAULT, handler_pool));
      else
        hb->source = NULL;
    }

  /* Open the text base for writing (this will get us a temporary file).  */

  if (use_revert_base)
    err = svn_wc__open_revert_base(&hb->dest, fb->path,
                                   (APR_WRITE | APR_TRUNCATE | APR_CREATE),
                                   handler_pool);
  else
    err = svn_wc__open_text_base(&hb->dest, fb->path,
                                 (APR_WRITE | APR_TRUNCATE | APR_CREATE),
                                 handler_pool);

  if (err)
    {
      svn_pool_destroy(handler_pool);
      return err;
    }

  /* Prepare to apply the delta.  */
  svn_txdelta_apply(svn_stream_from_aprfile(hb->source, handler_pool),
                    svn_stream_from_aprfile(hb->dest, handler_pool),
                    fb->digest, fb->new_text_base_path, handler_pool,
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

  if (fb->skipped)
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
            svn_wc_adm_access_t *adm_access,
            const char *file_path,
            const apr_array_header_t *prop_changes,
            apr_hash_t *base_props,
            apr_hash_t *working_props,
            svn_wc_conflict_resolver_func_t conflict_func,
            void *conflict_baton,
            apr_pool_t *pool)
{
  apr_array_header_t *regular_props = NULL, *wc_props = NULL,
    *entry_props = NULL;

  /* Sort the property list into three arrays, based on kind. */
  SVN_ERR(svn_categorize_props(prop_changes,
                               &entry_props, &wc_props, &regular_props,
                               pool));

  /* Always initialize to unknown state. */
  *prop_state = svn_wc_notify_state_unknown;

  /* Merge the 'regular' props into the existing working proplist. */
  if (regular_props)
    {
      /* This will merge the old and new props into a new prop db, and
         write <cp> commands to the logfile to install the merged
         props.  */
      SVN_ERR(svn_wc__merge_props(prop_state,
                                  adm_access, file_path,
                                  NULL /* update, not merge */,
                                  base_props,
                                  working_props,
                                  regular_props, TRUE, FALSE,
                                  conflict_func, conflict_baton,
                                  pool, &log_accum));
    }

  /* If there are any ENTRY PROPS, make sure those get appended to the
     growing log as fields for the file's entry.

     Note that no merging needs to happen; these kinds of props aren't
     versioned, so if the property is present, we overwrite the value. */
  if (entry_props)
    SVN_ERR(accumulate_entry_props(log_accum, lock_state,
                                   adm_access, file_path,
                                   entry_props, pool));
  else
    *lock_state = svn_wc_notify_lock_state_unchanged;

  /* This writes a whole bunch of log commands to install wcprops.  */
  if (wc_props)
    SVN_ERR(accumulate_wcprops(log_accum, adm_access,
                               file_path, wc_props, pool));

  return SVN_NO_ERROR;
}

/* Append, to LOG_ACCUM, log commands to update the entry for NAME in
   ADM_ACCESS with a NEW_REVISION and a NEW_URL (if non-NULL), making sure
   the entry refers to a file and has no absent or deleted state.
   Use POOL for temporary allocations. */
static svn_error_t *
loggy_tweak_entry(svn_stringbuf_t *log_accum,
                  svn_wc_adm_access_t *adm_access,
                  const char *path,
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

  SVN_ERR(svn_wc__loggy_entry_modify(&log_accum, adm_access,
                                     path, &tmp_entry, modify_flags,
                                     pool));

  return SVN_NO_ERROR;
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
 * If there's a new text base, FB->NEW_TEXT_BASE_PATH must be the full
 * pathname of the new text base, somewhere in the administrative area
 * of the working file.  The temporary text base will be removed after
 * a successful run of the generated log commands.
 *
 * Set *CONTENT_STATE, *PROP_STATE and *LOCK_STATE to the state of the
 * contents, properties and repository lock, respectively, after the
 * installation.  If an error is returned, the value of these three
 * variables is undefined.
 *
 * POOL is used for all bookkeeping work during the installation.
 */
static svn_error_t *
merge_file(svn_wc_notify_state_t *content_state,
           svn_wc_notify_state_t *prop_state,
           svn_wc_notify_lock_state_t *lock_state,
           struct file_baton *fb,
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

  /* Accumulated entry modifications. */
  svn_wc_entry_t tmp_entry;
  apr_uint64_t flags = 0;

  /*
     When this function is called on file F, we assume the following
     things are true:

         - The new pristine text of F, if any, is present at
           fb->new_text_base_path

         - The .svn/entries file still reflects the old version of F.

         - fb->old_text_base_path is the old pristine F.
           (This is only set if there's a new text base).

      The goal is to update the local working copy of F to reflect
      the changes received from the repository, preserving any local
      modifications.
  */

  /* Start by splitting the file path, getting an access baton for the parent,
     and an entry for the file if any. */
  svn_path_split(fb->path, &parent_dir, NULL, pool);
  SVN_ERR(svn_wc_adm_retrieve(&adm_access, eb->adm_access,
                              parent_dir, pool));

  SVN_ERR(svn_wc_entry(&entry, fb->path, adm_access, FALSE, pool));
  if (! entry && ! fb->added)
    return svn_error_createf(
        SVN_ERR_UNVERSIONED_RESOURCE, NULL,
        _("'%s' is not under version control"),
        svn_path_local_style(fb->path, pool));

  /* Determine if any of the propchanges are the "magic" ones that
     might require changing the working file. */
  magic_props_changed = svn_wc__has_magic_property(fb->propchanges);

  /* Install all kinds of properties.  It is important to do this before
     any file content merging, since that process might expand keywords, in
     which case we want the new entryprops to be in place. */
  SVN_ERR(merge_props(log_accum, prop_state, lock_state, adm_access,
                      fb->path, fb->propchanges,
                      fb->copied_base_props, fb->copied_working_props,
                      eb->conflict_func, eb->conflict_baton, pool));

  /* Has the user made local mods to the working file?
     Note that this compares to the current pristine file, which is
     different from fb->old_text_base_path if we have a replaced-with-history
     file.  However, in the case we had an obstruction, we check against the
     new text base. (And if we're doing an add-with-history and we've already
     saved a copy of a locally-modified file, then there certainly are mods.) */
  if (fb->copied_working_text)
    is_locally_modified = TRUE;
  else if (! fb->existed)
    SVN_ERR(svn_wc__text_modified_internal_p(&is_locally_modified, fb->path,
                                             FALSE, adm_access, FALSE, pool));
  else if (fb->new_text_base_path)
    SVN_ERR(svn_wc__versioned_file_modcheck(&is_locally_modified, fb->path,
                                             adm_access,
                                             fb->new_text_base_path,
                                             FALSE, pool));
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

  /* Set the new revision and URL in the entry and clean up some other
     fields. */
  SVN_ERR(loggy_tweak_entry(log_accum, adm_access, fb->path,
                            *eb->target_revision, fb->new_URL, pool));

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

   So the first thing we do is figure out where we are in the
   matrix. */
  if (fb->new_text_base_path)
    {
      if (! is_locally_modified && ! is_replaced)
        {
          /* If there are no local mods, who cares whether it's a text
             or binary file!  Just write a log command to overwrite
             any working file with the new text-base.  If newline
             conversion or keyword substitution is activated, this
             will happen as well during the copy.
             For replaced files, though, we want to merge in the changes
             even if the file is not modified compared to the (non-revert)
             text-base. */
          SVN_ERR(svn_wc__loggy_copy(&log_accum, NULL, adm_access,
                                     svn_wc__copy_translate,
                                     fb->new_text_base_path,
                                     fb->path, FALSE, pool));
        }
      else   /* working file or obstruction is locally modified... */
        {
          svn_node_kind_t wfile_kind = svn_node_unknown;

          SVN_ERR(svn_io_check_path(fb->path, &wfile_kind, pool));
          if (wfile_kind == svn_node_none && ! fb->added_with_history)
            {
              /* working file is missing?!
                 Just copy the new text-base to the file. */
              SVN_ERR(svn_wc__loggy_copy(&log_accum, NULL, adm_access,
                                         svn_wc__copy_translate,
                                         fb->new_text_base_path,
                                         fb->path, FALSE, pool));
            }
          else if (! fb->existed)
            /* Working file exists and has local mods
               or is scheduled for addition but is not an obstruction. */
            {
              /* Now we need to let loose svn_wc__merge_internal() to merge
                 the textual changes into the working file. */
              const char *oldrev_str, *newrev_str, *mine_str;
              const char *merge_left;
              const char *path_ext = "";

              /* If we have any file extensions we're supposed to
                 preserve in generated conflict file names, then find
                 this path's extension.  But then, if it isn't one of
                 the ones we want to keep in conflict filenames,
                 pretend it doesn't have an extension at all. */
              if (eb->ext_patterns && eb->ext_patterns->nelts)
                {
                  svn_path_splitext(NULL, &path_ext, fb->path, pool);
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
                }
              else if (fb->copied_text_base)
                merge_left = fb->copied_text_base;
              else
                merge_left = fb->text_base_path;

              /* Merge the changes from the old textbase to the new
                 textbase into the file we're updating.
                 Remember that this function wants full paths! */
              SVN_ERR(svn_wc__merge_internal
                      (&log_accum, &merge_outcome,
                       merge_left,
                       fb->new_text_base_path,
                       fb->path,
                       fb->copied_working_text,
                       adm_access,
                       oldrev_str, newrev_str, mine_str,
                       FALSE, eb->diff3_cmd, NULL, fb->propchanges,
                       eb->conflict_func, eb->conflict_baton, pool));

              /* If we created a temporary left merge file, get rid of it. */
              if (merge_left != fb->text_base_path)
                SVN_ERR(svn_wc__loggy_remove(&log_accum, adm_access,
                                             merge_left, pool));

              /* And clean up add-with-history-related temp file too. */
              if (fb->copied_working_text)
                SVN_ERR(svn_wc__loggy_remove(&log_accum, adm_access,
                                             fb->copied_working_text, pool));

            } /* end: working file exists and has mods */
        } /* end: working file has mods */
    } /* end: "textual" merging process */
  else
    {
      apr_hash_t *keywords;

      SVN_ERR(svn_wc__get_keywords(&keywords, fb->path,
                                   adm_access, NULL, pool));
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
          SVN_ERR(svn_wc_translated_file2(&tmptext, fb->path, fb->path,
                                          adm_access,
                                          SVN_WC_TRANSLATE_TO_NF
                                          | SVN_WC_TRANSLATE_NO_OUTPUT_CLEANUP,
                                          pool));

          /* A log command that copies the tmp-text-base and REtranslates
             it back to the working file.
             Now, since this is done during the execution of the log file, this
             retranslation is actually done according to the new props. */
          SVN_ERR(svn_wc__loggy_copy(&log_accum, NULL, adm_access,
                                     svn_wc__copy_translate,
                                     tmptext, fb->path, FALSE, pool));
        }

      if (*lock_state == svn_wc_notify_lock_state_unlocked)
        /* If a lock was removed and we didn't update the text contents, we
           might need to set the file read-only. */
        SVN_ERR(svn_wc__loggy_maybe_set_readonly(&log_accum, adm_access,
                                                 fb->path, pool));
    }

  /* Deal with installation of the new textbase, if appropriate. */
  if (fb->new_text_base_path)
    {
      SVN_ERR(svn_wc__loggy_move(&log_accum, NULL,
                                 adm_access, fb->new_text_base_path,
                                 fb->text_base_path, FALSE, pool));
      SVN_ERR(svn_wc__loggy_set_readonly(&log_accum, adm_access,
                                         fb->text_base_path, pool));

      /* If the file is replaced don't write the checksum.  Checksum is blank
         on replaced files. */
      if (!is_replaced)
        {
          tmp_entry.checksum = svn_md5_digest_to_cstring(fb->digest, pool);
          flags |= SVN_WC__ENTRY_MODIFY_CHECKSUM;
        }
    }

  /* Do the entry modifications we've accumulated. */
  SVN_ERR(svn_wc__loggy_entry_modify(&log_accum, adm_access,
                                     fb->path, &tmp_entry, flags, pool));

  /* Log commands to handle text-timestamp and working-size,
     if the file is - or will be - unmodified and schedule-normal */
  if (!is_locally_modified &&
      (fb->added || entry->schedule == svn_wc_schedule_normal))
    {
      /* Adjust working copy file unless this file is an allowed
         obstruction. */
      if (fb->last_changed_date && !fb->existed)
        SVN_ERR(svn_wc__loggy_set_timestamp(&log_accum, adm_access,
                                            fb->path, fb->last_changed_date,
                                            pool));

      if (fb->new_text_base_path || magic_props_changed)
        {
          /* Adjust entries file to match working file */
          SVN_ERR(svn_wc__loggy_set_entry_timestamp_from_wc
                  (&log_accum, adm_access,
                   fb->path, SVN_WC__ENTRY_ATTR_TEXT_TIME, pool));
        }
      SVN_ERR(svn_wc__loggy_set_entry_working_size_from_wc
              (&log_accum, adm_access, fb->path, pool));
    }

  /* Clean up add-with-history temp file. */
  if (fb->copied_text_base)
    SVN_ERR(svn_wc__loggy_remove(&log_accum, adm_access,
                                 fb->copied_text_base,
                                 pool));


  /* Set the returned content state. */

  /* This is kind of interesting.  Even if no new text was
     installed (i.e., new_text_path was null), we could still
     report a pre-existing conflict state.  Say a file, already
     in a state of textual conflict, receives prop mods during an
     update.  Then we'll notify that it has text conflicts.  This
     seems okay to me.  I guess.  I dunno.  You? */

  if (merge_outcome == svn_wc_merge_conflict)
    *content_state = svn_wc_notify_state_conflicted;
  else if (fb->new_text_base_path)
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
           const char *text_checksum,
           apr_pool_t *pool)
{
  struct file_baton *fb = file_baton;
  struct edit_baton *eb = fb->edit_baton;
  svn_wc_notify_state_t content_state, prop_state;
  svn_wc_notify_lock_state_t lock_state;

  if (fb->skipped)
    {
      SVN_ERR(maybe_bump_dir_info(eb, fb->bump_info, pool));
      return SVN_NO_ERROR;
    }

  /* Was this an add-with-history, with no apply_textdelta? */
  if (fb->added_with_history && ! fb->received_textdelta)
    {
      assert(! fb->text_base_path && ! fb->new_text_base_path
             && fb->copied_text_base);

      /* Set up the base paths like apply_textdelta does. */
      SVN_ERR(choose_base_paths(NULL, NULL, NULL, fb, pool));

      /* Now simulate applying a trivial delta. */
      SVN_ERR(svn_io_copy_file(fb->copied_text_base,
                               fb->new_text_base_path,
                               TRUE, pool));
      SVN_ERR(svn_io_file_checksum(fb->digest,
                                   fb->new_text_base_path,
                                   pool));
    }

  /* window-handler assembles new pristine text in .svn/tmp/text-base/  */
  if (fb->new_text_base_path && text_checksum)
    {
      const char *real_sum = svn_md5_digest_to_cstring(fb->digest, pool);

      if (real_sum && (strcmp(text_checksum, real_sum) != 0))
        return svn_error_createf
          (SVN_ERR_CHECKSUM_MISMATCH, NULL,
           _("Checksum mismatch for '%s'; expected: '%s', actual: '%s'"),
           svn_path_local_style(fb->path, pool), text_checksum, real_sum);
    }

  SVN_ERR(merge_file(&content_state, &prop_state, &lock_state, fb, pool));

  /* We have one less referrer to the directory's bump information. */
  SVN_ERR(maybe_bump_dir_info(eb, fb->bump_info, pool));

  if (((content_state != svn_wc_notify_state_unchanged) ||
       (prop_state != svn_wc_notify_state_unchanged) ||
       (lock_state != svn_wc_notify_lock_state_unchanged))
      && eb->notify_func)
    {
      svn_wc_notify_t *notify;
      svn_wc_notify_action_t action = svn_wc_notify_update_update;

      if (fb->existed || fb->add_existed)
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
      /* ### use merge_file() mimetype here */
      (*eb->notify_func)(eb->notify_baton, notify, pool);
    }
  return SVN_NO_ERROR;
}



/* Beginning at DEST_DIR (and its associated entry DEST_ENTRY) within
   a working copy, search the working copy for an pre-existing
   versioned file which is exactly equal to COPYFROM_PATH@COPYFROM_REV.

   If the file isn't found, set *RETURN_PATH to NULL.

   If the file is found, return the absolute path to it in
   *RETURN_PATH, its entry in *RETURN_ENTRY, and a (read-only)
   access_t for its parent in *RETURN_ACCESS.
*/
static svn_error_t *
locate_copyfrom(const char *copyfrom_path,
                svn_revnum_t copyfrom_rev,
                const char *dest_dir,
                const svn_wc_entry_t *dest_entry,
                const char **return_path,
                const svn_wc_entry_t **return_entry,
                svn_wc_adm_access_t **return_access,
                apr_pool_t *pool)
{
  const char *dest_fs_path, *ancestor_fs_path, *ancestor_url, *file_url;
  const char *copyfrom_parent, *copyfrom_file;
  const char *abs_dest_dir, *extra_components;
  const svn_wc_entry_t *ancestor_entry, *file_entry;
  svn_wc_adm_access_t *ancestor_access;
  apr_size_t levels_up;
  svn_stringbuf_t *cwd, *cwd_parent;
  svn_node_kind_t kind;
  svn_error_t *err;
  apr_pool_t *subpool = svn_pool_create(pool);

  /* Be pessimistic.  This function is basically a series of tests
     that gives dozens of ways to fail our search, returning
     SVN_NO_ERROR in each case.  If we make it all the way to the
     bottom, we have a real discovery to return. */
  *return_path = NULL;

  if ((! dest_entry->repos) || (! dest_entry->url))
    return svn_error_create(SVN_ERR_WC_COPYFROM_PATH_NOT_FOUND, NULL,
                            _("Destination directory of add-with-history "
                              "is missing a URL"));

  svn_path_split(copyfrom_path, &copyfrom_parent, &copyfrom_file, pool);
  SVN_ERR(svn_path_get_absolute(&abs_dest_dir, dest_dir, pool));

  /* Subtract the dest_dir's URL from the repository "root" URL to get
     the absolute FS path represented by dest_dir. */
  dest_fs_path = svn_path_is_child(dest_entry->repos, dest_entry->url, pool);
  if (! dest_fs_path)
    {
      if (strcmp(dest_entry->repos, dest_entry->url) == 0)
        dest_fs_path = "";  /* the urls are identical; that's ok. */
      else
        return svn_error_create(SVN_ERR_WC_COPYFROM_PATH_NOT_FOUND, NULL,
                                _("Destination URLs are broken"));
    }
  dest_fs_path = apr_pstrcat(pool, "/", dest_fs_path, NULL);
  dest_fs_path = svn_path_canonicalize(dest_fs_path, pool);

  /* Find nearest FS ancestor dir of current FS path and copyfrom_parent */
  ancestor_fs_path = svn_path_get_longest_ancestor(dest_fs_path,
                                                   copyfrom_parent, pool);
  if (strlen(ancestor_fs_path) == 0)
    return SVN_NO_ERROR;

  /* Move 'up' the working copy to what ought to be the common ancestor dir. */
  levels_up = svn_path_component_count(dest_fs_path)
              - svn_path_component_count(ancestor_fs_path);
  cwd = svn_stringbuf_create(dest_dir, pool);
  svn_path_remove_components(cwd, levels_up);

  /* Open up this hypothetical common ancestor directory. */
  SVN_ERR(svn_io_check_path(cwd->data, &kind, subpool));
  if (kind != svn_node_dir)
    return SVN_NO_ERROR;
  err = svn_wc_adm_open3(&ancestor_access, NULL, cwd->data,
                         FALSE, /* open read-only, please */
                         0,     /* open only this directory */
                         NULL, NULL, subpool);
  if (err && err->apr_err == SVN_ERR_WC_NOT_DIRECTORY)
    {
      /* The common ancestor directory isn't version-controlled. */
      svn_error_clear(err);
      return SVN_NO_ERROR;
    }
  else if (err)
    return err;

  SVN_ERR(svn_wc_entry(&ancestor_entry, cwd->data, ancestor_access,
                       FALSE, subpool));

  /* If we got this far, we know that the ancestor dir exists, and
     that it's a working copy too.  But is it from the same
     repository?  And does it represent the URL we expect it to? */
  if (dest_entry->uuid && ancestor_entry->uuid
      && (strcmp(dest_entry->uuid, ancestor_entry->uuid) != 0))
    return SVN_NO_ERROR;

  ancestor_url = apr_pstrcat(subpool,
                             dest_entry->repos, ancestor_fs_path, NULL);
  if (strcmp(ancestor_url, ancestor_entry->url) != 0)
    return SVN_NO_ERROR;

  svn_pool_clear(subpool);  /* clean up adm_access junk. */

  /* Add the remaining components to cwd, then 'drill down' to where
     we hope the copyfrom_path file exists. */
  extra_components = svn_path_is_child(ancestor_fs_path,
                                       copyfrom_path, pool);
  svn_path_add_component(cwd, extra_components);
  cwd_parent = svn_stringbuf_create(cwd->data, pool);
  svn_path_remove_component(cwd_parent);

  /* First: does the proposed file path even exist? */
  SVN_ERR(svn_io_check_path(cwd->data, &kind, subpool));
  if (kind != svn_node_file)
    return SVN_NO_ERROR;

  /* Next: is the file's parent-dir under version control?   */
  err = svn_wc_adm_open3(&ancestor_access, NULL, cwd_parent->data,
                         FALSE, /* open read-only, please */
                         0,     /* open only the parent dir */
                         NULL, NULL, pool);
  if (err && err->apr_err == SVN_ERR_WC_NOT_DIRECTORY)
    {
      svn_error_clear(err);

      /* There's an unversioned directory (and file) in the exact
         correct place in the working copy.  Chances are high that
         this file (or some parent) was deleted by 'svn update' --
         perhaps as part of a move operation -- and this file was left
         behind becouse it had local edits.  If that's true, we may
         want this thing copied over to the new place.

         Unfortunately, we have no way of knowing if this file is the
         one we're looking for.  Guessing incorrectly can be really
         hazardous, breaking the entire update.: we might find out
         when the server fails to apply a subsequent txdelta against
         it.  Or, if the server doesn't try to do that now, what if a
         future update fails to apply?  For now, the only safe thing
         to do is return no results. :-/
      */
      return SVN_NO_ERROR;
    }
  else if (err)
    return err;

  /* The candidate file is under version control;  but is it
     really the file we're looking for?  <wave hand in circle> */
  SVN_ERR(svn_wc_entry(&file_entry, cwd->data, ancestor_access,
                       FALSE, pool));
  if (! file_entry)
    /* Parent dir is versioned, but file is not.  Be safe and
       return no results (see large discourse above.) */
    return SVN_NO_ERROR;

  /* Is the repos UUID and file's URL what we expect it to be? */
  if (file_entry->uuid && dest_entry->uuid
      && (strcmp(file_entry->uuid, dest_entry->uuid) != 0))
    return SVN_NO_ERROR;

  file_url = apr_pstrcat(subpool, file_entry->repos, copyfrom_path, NULL);
  if (strcmp(file_url, file_entry->url) != 0)
    return SVN_NO_ERROR;

  /* Do we actually have valid revisions for the file?  (See Issue
     #2977.) */
  if (! (SVN_IS_VALID_REVNUM(file_entry->cmt_rev)
         && SVN_IS_VALID_REVNUM(file_entry->revision)))
    return SVN_NO_ERROR;

  /* Do we have the the right *version* of the file? */
  if (! ((file_entry->cmt_rev <= copyfrom_rev)
         && (copyfrom_rev <= file_entry->revision)))
    return SVN_NO_ERROR;

  /* Success!  We found the exact file we wanted! */
  *return_path = apr_pstrdup(pool, cwd->data);
  *return_entry = file_entry;
  *return_access = ancestor_access;

  svn_pool_clear(subpool);
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


/* Similar to add_file(), but not actually part of the editor vtable.

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
                      void *parent_baton,
                      const char *copyfrom_path,
                      svn_revnum_t copyfrom_rev,
                      void **file_baton,
                      apr_pool_t *pool)
{
  void *fb;
  struct file_baton *tfb;
  struct dir_baton *pb = parent_baton;
  struct edit_baton *eb = pb->edit_baton;
  svn_wc_adm_access_t *adm_access, *src_access;
  const char *src_path;
  const svn_wc_entry_t *src_entry;
  apr_hash_t *base_props, *working_props;
  const svn_wc_entry_t *path_entry;
  svn_error_t *err;
  svn_stringbuf_t *log_accum = svn_stringbuf_create("", pool);

  /* The file_pool can stick around for a *long* time, so we want to
     use a subpool for any temporary allocations. */
  apr_pool_t *subpool = svn_pool_create(pool);

  /* First, fake an add_file() call.  Notice that we don't send any
     copyfrom args, lest we end up infinitely recursing.  :-)  */
  SVN_ERR(add_file(path, parent_baton, NULL, SVN_INVALID_REVNUM, pool, &fb));
  tfb = (struct file_baton *)fb;
  tfb->added_with_history = TRUE;

  /* Attempt to locate the copyfrom_path in the working copy first. */
  SVN_ERR(svn_wc_entry(&path_entry, pb->path, eb->adm_access, FALSE, subpool));
  err = locate_copyfrom(copyfrom_path, copyfrom_rev,
                        pb->path, path_entry,
                        &src_path, &src_entry, &src_access, subpool);
  if (err && err->apr_err == SVN_ERR_WC_COPYFROM_PATH_NOT_FOUND)
    svn_error_clear(err);
  else if (err)
    return err;

  SVN_ERR(svn_wc_adm_retrieve(&adm_access, pb->edit_baton->adm_access,
                              pb->path, subpool));

  /* Raise a tree conflict if there's already something versioned here. */
  SVN_ERR(check_tree_conflict(log_accum, pb->path, path_entry, adm_access,
                              svn_wc_conflict_action_add, pool));

  /* Make a unique file name for the copyfrom text-base. */
  SVN_ERR(svn_wc_create_tmp_file2(NULL, &tfb->copied_text_base,
                                  svn_wc_adm_access_path(adm_access),
                                  svn_io_file_del_none,
                                  pool));

  if (src_path != NULL) /* Found a file to copy */
    {
      /* Copy the existing file's text-base over to the (temporary)
         new text-base, where the file baton expects it to be.  Get
         the text base and props from the usual place or from the
         revert place, depending on scheduling. */

      const char *src_text_base_path;

      if (src_entry->schedule == svn_wc_schedule_replace
          && src_entry->copyfrom_url)
        {
          src_text_base_path = svn_wc__text_revert_path(src_path,
                                                        FALSE, subpool);
          SVN_ERR(svn_wc__load_props(NULL, NULL, &base_props,
                                     src_access, src_path, pool));
          /* The old working props are lost, just like the old
             working file text is.  Just use the base props. */
          working_props = base_props;
        }
      else
        {
          src_text_base_path = svn_wc__text_base_path(src_path,
                                                      FALSE, subpool);
          SVN_ERR(svn_wc__load_props(&base_props, &working_props, NULL,
                                     src_access, src_path, pool));
        }

      SVN_ERR(svn_io_copy_file(src_text_base_path, tfb->copied_text_base,
                               TRUE, subpool));
    }
  else  /* Couldn't find a file to copy  */
    {
      apr_file_t *textbase_file;
      svn_stream_t *textbase_stream;

      /* Fall back to fetching it from the repository instead. */

      if (! eb->fetch_func)
        return svn_error_create(SVN_ERR_WC_INVALID_OP_ON_CWD, NULL,
                                _("No fetch_func supplied to update_editor"));

      /* Fetch the repository file's text-base and base-props;
         svn_stream_close() automatically closes the text-base file for us. */
      SVN_ERR(svn_io_file_open(&textbase_file, tfb->copied_text_base,
                               (APR_WRITE | APR_TRUNCATE | APR_CREATE),
                               APR_OS_DEFAULT, subpool));
      textbase_stream = svn_stream_from_aprfile2(textbase_file, FALSE, pool);

      /* copyfrom_path is a absolute path, fetch_func requires a path relative
         to the root of the repository so skip the first '/'. */
      SVN_ERR(eb->fetch_func(eb->fetch_baton, copyfrom_path + 1, copyfrom_rev,
                             textbase_stream,
                             NULL, &base_props, pool));
      SVN_ERR(svn_stream_close(textbase_stream));
      working_props = base_props;
    }

  /* Loop over whatever props we have in memory, and add all
     regular props to hashes in the baton. Skip entry and wc
     properties, these are only valid for the original file. */
  tfb->copied_base_props = copy_regular_props(base_props, pool);
  tfb->copied_working_props = copy_regular_props(working_props, pool);

  if (src_path != NULL)
    {
      /* If we copied an existing file over, we need to copy its
         working text too, to preserve any local mods.  (We already
         read its working *props* into tfb->copied_working_props.) */
      svn_boolean_t text_changed;

      SVN_ERR(svn_wc_text_modified_p(&text_changed, src_path, FALSE,
                                     src_access, subpool));

      if (text_changed)
        {
          /* Make a unique file name for the copied_working_text. */
          SVN_ERR(svn_wc_create_tmp_file2(NULL, &tfb->copied_working_text,
                                          svn_wc_adm_access_path(adm_access),
                                          svn_io_file_del_none,
                                          pool));

          SVN_ERR(svn_io_copy_file(src_path, tfb->copied_working_text, TRUE,
                                   subpool));
        }
    }

  svn_pool_destroy(subpool);

  *file_baton = tfb;
  return SVN_NO_ERROR;
}


/* An svn_delta_editor_t function. */
static svn_error_t *
close_edit(void *edit_baton,
           apr_pool_t *pool)
{
  struct edit_baton *eb = edit_baton;
  const char *target_path = svn_path_join(eb->anchor, eb->target, pool);
  int log_number = 0;

  /* If there is a target and that target is missing, then it
     apparently wasn't re-added by the update process, so we'll
     pretend that the editor deleted the entry.  The helper function
     do_entry_deletion() will take care of the necessary steps.  */
  if ((*eb->target) && (svn_wc__adm_missing(eb->adm_access, target_path)))
    SVN_ERR(do_entry_deletion(eb, eb->anchor, eb->target, &log_number, NULL,
                              pool));

  /* The editor didn't even open the root; we have to take care of
     some cleanup stuffs. */
  if (! eb->root_opened)
    {
      /* We need to "un-incomplete" the root directory. */
      SVN_ERR(complete_directory(eb, eb->anchor, TRUE, pool));
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
    SVN_ERR(svn_wc__do_update_cleanup(target_path,
                                      eb->adm_access,
                                      eb->requested_depth,
                                      eb->switch_url,
                                      eb->repos,
                                      *(eb->target_revision),
                                      eb->notify_func,
                                      eb->notify_baton,
                                      TRUE, eb->skipped_paths,
                                      eb->pool));

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
            svn_wc_adm_access_t *adm_access,
            const char *anchor,
            const char *target,
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
            svn_wc_get_file_t fetch_func,
            void *fetch_baton,
            const char *diff3_cmd,
            apr_array_header_t *preserved_exts,
            const svn_delta_editor_t **editor,
            void **edit_baton,
            svn_wc_traversal_info_t *traversal_info,
            apr_pool_t *pool)
{
  struct edit_baton *eb;
  void *inner_baton;
  apr_pool_t *subpool = svn_pool_create(pool);
  svn_delta_editor_t *tree_editor = svn_delta_default_editor(subpool);
  const svn_delta_editor_t *inner_editor;
  const svn_wc_entry_t *entry;

  /* An unknown depth can't be sticky. */
  if (depth == svn_depth_unknown)
    depth_is_sticky = FALSE;

  /* Get the anchor entry, so we can fetch the repository root. */
  SVN_ERR(svn_wc_entry(&entry, anchor, adm_access, FALSE, pool));

  /* Disallow a switch operation to change the repository root of the target,
     if that is known. */
  if (switch_url && entry && entry->repos &&
      ! svn_path_is_ancestor(entry->repos, switch_url))
    return svn_error_createf
      (SVN_ERR_WC_INVALID_SWITCH, NULL,
       _("'%s'\n"
         "is not the same repository as\n"
         "'%s'"), switch_url, entry->repos);

  /* Construct an edit baton. */
  eb = apr_pcalloc(subpool, sizeof(*eb));
  eb->pool                     = subpool;
  eb->use_commit_times         = use_commit_times;
  eb->target_revision          = target_revision;
  eb->switch_url               = switch_url;
  eb->repos                    = entry ? entry->repos : NULL;
  eb->adm_access               = adm_access;
  eb->anchor                   = anchor;
  eb->target                   = target;
  eb->requested_depth          = depth;
  eb->depth_is_sticky          = depth_is_sticky;
  eb->notify_func              = notify_func;
  eb->notify_baton             = notify_baton;
  eb->traversal_info           = traversal_info;
  eb->diff3_cmd                = diff3_cmd;
  eb->cancel_func              = cancel_func;
  eb->cancel_baton             = cancel_baton;
  eb->conflict_func            = conflict_func;
  eb->conflict_baton           = conflict_baton;
  eb->fetch_func               = fetch_func;
  eb->fetch_baton              = fetch_baton;
  eb->allow_unver_obstructions = allow_unver_obstructions;
  eb->skipped_paths            = apr_hash_make(subpool);
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

  /* If our requested depth is sticky, we'll raise an error if asked
     to make our target more shallow, which is currently unsupported.

     Otherwise, if our requested depth is *not* sticky, then we need
     to limit the scope of our operation to the ambient depths present
     in the working copy already.  If a depth was explicitly
     requested, libsvn_delta/depth_filter_editor.c will ensure that we
     never see editor calls that extend beyond the scope of the
     requested depth.  But even what we do so might extend beyond the
     scope of our ambient depth.  So we use another filtering editor
     to avoid modifying the ambient working copy depth when not asked
     to do so.  (This can also be skipped if the server understands
     consider letting the depth RA capability percolate down to this
     level.) */
  if (depth_is_sticky)
    {
      const svn_wc_entry_t *target_entry;
      SVN_ERR(svn_wc_entry(&target_entry, svn_path_join(anchor, target, pool),
                           adm_access, FALSE, pool));
      if (target_entry && (target_entry->depth > depth))
        return svn_error_createf(SVN_ERR_UNSUPPORTED_FEATURE, NULL,
                                 _("Shallowing of working copy depths is not "
                                   "yet supported"));
    }
  else
    {
      SVN_ERR(svn_wc__ambient_depth_filter_editor(&inner_editor,
                                                  &inner_baton,
                                                  inner_editor,
                                                  inner_baton,
                                                  anchor,
                                                  target,
                                                  adm_access,
                                                  pool));
    }

  SVN_ERR(svn_delta_get_cancellation_editor(cancel_func,
                                            cancel_baton,
                                            inner_editor,
                                            inner_baton,
                                            editor,
                                            edit_baton,
                                            pool));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc_get_update_editor3(svn_revnum_t *target_revision,
                          svn_wc_adm_access_t *anchor,
                          const char *target,
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
                          svn_wc_get_file_t fetch_func,
                          void *fetch_baton,
                          const char *diff3_cmd,
                          apr_array_header_t *preserved_exts,
                          const svn_delta_editor_t **editor,
                          void **edit_baton,
                          svn_wc_traversal_info_t *traversal_info,
                          apr_pool_t *pool)
{
  return make_editor(target_revision, anchor, svn_wc_adm_access_path(anchor),
                     target, use_commit_times, NULL, depth, depth_is_sticky,
                     allow_unver_obstructions, notify_func, notify_baton,
                     cancel_func, cancel_baton, conflict_func, conflict_baton,
                     fetch_func, fetch_baton,
                     diff3_cmd, preserved_exts, editor, edit_baton,
                     traversal_info, pool);
}


svn_error_t *
svn_wc_get_update_editor2(svn_revnum_t *target_revision,
                          svn_wc_adm_access_t *anchor,
                          const char *target,
                          svn_boolean_t use_commit_times,
                          svn_boolean_t recurse,
                          svn_wc_notify_func2_t notify_func,
                          void *notify_baton,
                          svn_cancel_func_t cancel_func,
                          void *cancel_baton,
                          const char *diff3_cmd,
                          const svn_delta_editor_t **editor,
                          void **edit_baton,
                          svn_wc_traversal_info_t *traversal_info,
                          apr_pool_t *pool)
{
  return svn_wc_get_update_editor3(target_revision, anchor, target,
                                   use_commit_times,
                                   SVN_DEPTH_INFINITY_OR_FILES(recurse), FALSE,
                                   FALSE, notify_func, notify_baton,
                                   cancel_func, cancel_baton, NULL, NULL,
                                   NULL, NULL,
                                   diff3_cmd, NULL, editor, edit_baton,
                                   traversal_info, pool);
}

svn_error_t *
svn_wc_get_update_editor(svn_revnum_t *target_revision,
                         svn_wc_adm_access_t *anchor,
                         const char *target,
                         svn_boolean_t use_commit_times,
                         svn_boolean_t recurse,
                         svn_wc_notify_func_t notify_func,
                         void *notify_baton,
                         svn_cancel_func_t cancel_func,
                         void *cancel_baton,
                         const char *diff3_cmd,
                         const svn_delta_editor_t **editor,
                         void **edit_baton,
                         svn_wc_traversal_info_t *traversal_info,
                         apr_pool_t *pool)
{
  svn_wc__compat_notify_baton_t *nb = apr_palloc(pool, sizeof(*nb));
  nb->func = notify_func;
  nb->baton = notify_baton;

  return svn_wc_get_update_editor3(target_revision, anchor, target,
                                   use_commit_times,
                                   SVN_DEPTH_INFINITY_OR_FILES(recurse), FALSE,
                                   FALSE, svn_wc__compat_call_notify_func, nb,
                                   cancel_func, cancel_baton, NULL, NULL,
                                   NULL, NULL,
                                   diff3_cmd, NULL, editor, edit_baton,
                                   traversal_info, pool);
}

svn_error_t *
svn_wc_get_switch_editor3(svn_revnum_t *target_revision,
                          svn_wc_adm_access_t *anchor,
                          const char *target,
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
                          const char *diff3_cmd,
                          apr_array_header_t *preserved_exts,
                          const svn_delta_editor_t **editor,
                          void **edit_baton,
                          svn_wc_traversal_info_t *traversal_info,
                          apr_pool_t *pool)
{
  assert(switch_url);

  return make_editor(target_revision, anchor, svn_wc_adm_access_path(anchor),
                     target, use_commit_times, switch_url,
                     depth, depth_is_sticky, allow_unver_obstructions,
                     notify_func, notify_baton, cancel_func, cancel_baton,
                     conflict_func, conflict_baton,
                     NULL, NULL, /* TODO(sussman): add fetch callback here  */
                     diff3_cmd, preserved_exts,
                     editor, edit_baton, traversal_info, pool);
}

svn_error_t *
svn_wc_get_switch_editor2(svn_revnum_t *target_revision,
                          svn_wc_adm_access_t *anchor,
                          const char *target,
                          const char *switch_url,
                          svn_boolean_t use_commit_times,
                          svn_boolean_t recurse,
                          svn_wc_notify_func2_t notify_func,
                          void *notify_baton,
                          svn_cancel_func_t cancel_func,
                          void *cancel_baton,
                          const char *diff3_cmd,
                          const svn_delta_editor_t **editor,
                          void **edit_baton,
                          svn_wc_traversal_info_t *traversal_info,
                          apr_pool_t *pool)
{
  assert(switch_url);

  return svn_wc_get_switch_editor3(target_revision, anchor, target,
                                   switch_url, use_commit_times,
                                   SVN_DEPTH_INFINITY_OR_FILES(recurse), FALSE,
                                   FALSE, notify_func, notify_baton,
                                   cancel_func, cancel_baton,
                                   NULL, NULL, diff3_cmd,
                                   NULL, editor, edit_baton, traversal_info,
                                   pool);
}

svn_error_t *
svn_wc_get_switch_editor(svn_revnum_t *target_revision,
                         svn_wc_adm_access_t *anchor,
                         const char *target,
                         const char *switch_url,
                         svn_boolean_t use_commit_times,
                         svn_boolean_t recurse,
                         svn_wc_notify_func_t notify_func,
                         void *notify_baton,
                         svn_cancel_func_t cancel_func,
                         void *cancel_baton,
                         const char *diff3_cmd,
                         const svn_delta_editor_t **editor,
                         void **edit_baton,
                         svn_wc_traversal_info_t *traversal_info,
                         apr_pool_t *pool)
{
  svn_wc__compat_notify_baton_t *nb = apr_palloc(pool, sizeof(*nb));
  nb->func = notify_func;
  nb->baton = notify_baton;

  return svn_wc_get_switch_editor3(target_revision, anchor, target,
                                   switch_url, use_commit_times,
                                   SVN_DEPTH_INFINITY_OR_FILES(recurse), FALSE,
                                   FALSE, svn_wc__compat_call_notify_func, nb,
                                   cancel_func, cancel_baton,
                                   NULL, NULL, diff3_cmd,
                                   NULL, editor, edit_baton, traversal_info,
                                   pool);
}


svn_wc_traversal_info_t *
svn_wc_init_traversal_info(apr_pool_t *pool)
{
  svn_wc_traversal_info_t *ti = apr_palloc(pool, sizeof(*ti));

  ti->pool           = pool;
  ti->externals_old  = apr_hash_make(pool);
  ti->externals_new  = apr_hash_make(pool);
  ti->depths         = apr_hash_make(pool);

  return ti;
}


void
svn_wc_edited_externals(apr_hash_t **externals_old,
                        apr_hash_t **externals_new,
                        svn_wc_traversal_info_t *traversal_info)
{
  *externals_old = traversal_info->externals_old;
  *externals_new = traversal_info->externals_new;
}


void
svn_wc_traversed_depths(apr_hash_t **depths,
                        svn_wc_traversal_info_t *traversal_info)
{
  *depths = traversal_info->depths;
}


/* THE GOAL

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

   svn_wc_get_actual_target() is that function.

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
   directory itself needs to be modified) */
static svn_error_t *
check_wc_root(svn_boolean_t *wc_root,
              svn_node_kind_t *kind,
              const char *path,
              svn_wc_adm_access_t *adm_access,
              apr_pool_t *pool)
{
  const char *parent, *base_name;
  const svn_wc_entry_t *p_entry, *entry;
  svn_error_t *err;
  svn_wc_adm_access_t *p_access;

  /* Go ahead and initialize our return value to the most common
     (code-wise) values. */
  *wc_root = TRUE;

  /* Get our ancestry.  In the event that the path is unversioned,
     treat it as if it were a file so that the anchor will be the
     parent directory. */
  SVN_ERR(svn_wc_entry(&entry, path, adm_access, FALSE, pool));
  if (kind)
    *kind = entry ? entry->kind : svn_node_file;

  /* If PATH is the current working directory, we have no choice but
     to consider it a WC root (we can't examine its parent at all) */
  if (svn_path_is_empty(path))
    return SVN_NO_ERROR;

  /* If this is the root folder (of a drive), it should be the WC
     root too. */
  if (svn_dirent_is_root(path, strlen(path)))
    return SVN_NO_ERROR;

  /* If we cannot get an entry for PATH's parent, PATH is a WC root. */
  p_entry = NULL;
  svn_path_split(path, &parent, &base_name, pool);
  SVN_ERR(svn_wc__adm_retrieve_internal(&p_access, adm_access, parent,
                                        pool));
  err = SVN_NO_ERROR;
  if (! p_access)
    /* For historical reasons we cannot rely on the caller having opened
       the parent, so try it here.  I'd like this bit to go away.  */
    err = svn_wc_adm_probe_open3(&p_access, NULL, parent, FALSE, 0,
                                 NULL, NULL, pool);

  if (! err)
    err = svn_wc_entry(&p_entry, parent, p_access, FALSE, pool);

  if (err || (! p_entry))
    {
      svn_error_clear(err);
      return SVN_NO_ERROR;
    }

  /* If the parent directory has no url information, something is
     messed up.  Bail with an error. */
  if (! p_entry->url)
    return svn_error_createf
      (SVN_ERR_ENTRY_MISSING_URL, NULL,
       _("'%s' has no ancestry information"),
       svn_path_local_style(parent, pool));

  /* If PATH's parent in the WC is not its parent in the repository,
     PATH is a WC root. */
  if (entry && entry->url
      && (strcmp(svn_path_url_add_component(p_entry->url, base_name, pool),
                 entry->url) != 0))
    return SVN_NO_ERROR;

  /* If PATH's parent in the repository is not its parent in the WC,
     PATH is a WC root. */
  SVN_ERR(svn_wc_entry(&p_entry, path, p_access, FALSE, pool));
  if (! p_entry)
      return SVN_NO_ERROR;

  /* If we have not determined that PATH is a WC root by now, it must
     not be! */
  *wc_root = FALSE;
  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc_is_wc_root(svn_boolean_t *wc_root,
                  const char *path,
                  svn_wc_adm_access_t *adm_access,
                  apr_pool_t *pool)
{
  return check_wc_root(wc_root, NULL, path, adm_access, pool);
}


svn_error_t *
svn_wc_get_actual_target(const char *path,
                         const char **anchor,
                         const char **target,
                         apr_pool_t *pool)
{
  svn_wc_adm_access_t *adm_access;
  svn_boolean_t is_wc_root;
  svn_node_kind_t kind;

  SVN_ERR(svn_wc_adm_probe_open3(&adm_access, NULL, path, FALSE, 0,
                                 NULL, NULL, pool));
  SVN_ERR(check_wc_root(&is_wc_root, &kind, path, adm_access, pool));
  SVN_ERR(svn_wc_adm_close(adm_access));

  /* If PATH is not a WC root, or if it is a file, lop off a basename. */
  if ((! is_wc_root) || (kind == svn_node_file))
    {
      svn_path_split(path, anchor, target, pool);
    }
  else
    {
      *anchor = apr_pstrdup(pool, path);
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

  /* Categorize the base properties. */
  {
    apr_array_header_t *prop_array;
    int i;

    /* Diff an empty prop has against the new base props gives us an array
       of all props. */
    SVN_ERR(svn_prop_diffs(&prop_array, new_base_props,
                           apr_hash_make(pool), pool));
    SVN_ERR(svn_categorize_props(prop_array,
                                 &entry_props, &wc_props, &regular_props,
                                 pool));

    /* Put regular props back into a hash table. */
    new_base_props = apr_hash_make(pool);
    for (i = 0; i < regular_props->nelts; ++i)
      {
        const svn_prop_t *prop = &APR_ARRAY_IDX(regular_props, i, svn_prop_t);

        apr_hash_set(new_base_props, prop->name, APR_HASH_KEY_STRING,
                     prop->value);
      }
  }

  /* Install base and working props. */
  SVN_ERR(svn_wc__install_props(&log_accum, adm_access, dst_path,
                                new_base_props,
                                new_props ? new_props : new_base_props,
                                TRUE, pool));

  /* Install the entry props. */
  SVN_ERR(accumulate_entry_props(log_accum, NULL,
                                 adm_access, dst_path,
                                 entry_props, pool));

  /* This writes a whole bunch of log commands to install wcprops.  */
  SVN_ERR(accumulate_wcprops(log_accum, adm_access,
                             dst_path, wc_props, pool));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc_add_repos_file2(const char *dst_path,
                       svn_wc_adm_access_t *adm_access,
                       const char *new_text_base_path,
                       const char *new_text_path,
                       apr_hash_t *new_base_props,
                       apr_hash_t *new_props,
                       const char *copyfrom_url,
                       svn_revnum_t copyfrom_rev,
                       apr_pool_t *pool)
{
  const char *new_URL;
  const char *adm_path = svn_wc_adm_access_path(adm_access);
  const char *tmp_text_base_path =
    svn_wc__text_base_path(dst_path, TRUE, pool);
  const char *text_base_path =
    svn_wc__text_base_path(dst_path, FALSE, pool);
  const svn_wc_entry_t *ent;
  const svn_wc_entry_t *dst_entry;
  svn_stringbuf_t *log_accum;
  const char *dir_name, *base_name;

  svn_path_split(dst_path, &dir_name, &base_name, pool);

  /* Fabricate the anticipated new URL of the target and check the
     copyfrom URL to be in the same repository. */
  {
    SVN_ERR(svn_wc__entry_versioned(&ent, dir_name, adm_access, FALSE, pool));

    new_URL = svn_path_url_add_component(ent->url, base_name, pool);

    if (copyfrom_url && ent->repos &&
        ! svn_path_is_ancestor(ent->repos, copyfrom_url))
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
  SVN_ERR(svn_wc_entry(&dst_entry, dst_path, adm_access, FALSE, pool));
  if (dst_entry && dst_entry->schedule == svn_wc_schedule_delete)
    {
      const char *dst_rtext = svn_wc__text_revert_path(dst_path, FALSE,
                                                       pool);
      const char *dst_txtb = svn_wc__text_base_path(dst_path, FALSE, pool);

      SVN_ERR(svn_wc__loggy_move(&log_accum, NULL,
                                 adm_access, dst_txtb, dst_rtext,
                                 FALSE, pool));
      SVN_ERR(svn_wc__loggy_revert_props_create(&log_accum,
                                                dst_path, adm_access,
                                                TRUE, pool));
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
        assert(SVN_IS_VALID_REVNUM(copyfrom_rev));

        tmp_entry.copyfrom_url = copyfrom_url;
        tmp_entry.copyfrom_rev = copyfrom_rev;
        tmp_entry.copied = TRUE;

        modify_flags |= SVN_WC__ENTRY_MODIFY_COPYFROM_URL
          | SVN_WC__ENTRY_MODIFY_COPYFROM_REV
          | SVN_WC__ENTRY_MODIFY_COPIED;
      }

    SVN_ERR(svn_wc__loggy_entry_modify(&log_accum, adm_access,
                                       dst_path, &tmp_entry,
                                       modify_flags, pool));
  }

  /* Set the new revision number and URL in the entry and clean up some other
     fields. */
  SVN_ERR(loggy_tweak_entry(log_accum, adm_access, dst_path,
                            dst_entry ? dst_entry->revision : ent->revision,
                            new_URL, pool));

  SVN_ERR(install_added_props(log_accum, adm_access, dst_path,
                              new_base_props, new_props, pool));

  /* Make sure the text base is where our log file can refer to it. */
  if (strcmp(tmp_text_base_path, new_text_base_path) != 0)
    SVN_ERR(svn_io_file_move(new_text_base_path, tmp_text_base_path,
                             pool));

  /* Install working file. */
  if (new_text_path)
    {
      /* If the caller gave us a new working file, move it in place. */
      const char *tmp_text_path;

      /* Move new text to temporary file in adm_access. */
      SVN_ERR(svn_wc_create_tmp_file2(NULL, &tmp_text_path, adm_path,
                                      svn_io_file_del_none, pool));

      SVN_ERR(svn_io_file_move(new_text_path, tmp_text_path, pool));

      /* Translate/rename new temporary text file to working text. */
      if (svn_wc__has_special_property(new_base_props))
        {
          SVN_ERR(svn_wc__loggy_copy(&log_accum, NULL, adm_access,
                                     svn_wc__copy_translate_special_only,
                                     tmp_text_path,
                                     dst_path, FALSE, pool));
          /* Remove the copy-source, making it look like a move */
          SVN_ERR(svn_wc__loggy_remove(&log_accum, adm_access,
                                       tmp_text_path, pool));
        }
      else
        SVN_ERR(svn_wc__loggy_move(&log_accum, NULL, adm_access,
                                   tmp_text_path, dst_path,
                                   FALSE, pool));

      SVN_ERR(svn_wc__loggy_maybe_set_readonly(&log_accum, adm_access,
                                               dst_path, pool));
    }
  else
    {
      /* No working file provided by the caller, copy and translate the
         text base. */
      SVN_ERR(svn_wc__loggy_copy(&log_accum, NULL, adm_access,
                                 svn_wc__copy_translate,
                                 tmp_text_base_path, dst_path, FALSE,
                                 pool));
      SVN_ERR(svn_wc__loggy_set_entry_timestamp_from_wc
              (&log_accum, adm_access,
               dst_path, SVN_WC__ENTRY_ATTR_TEXT_TIME, pool));
      SVN_ERR(svn_wc__loggy_set_entry_working_size_from_wc
              (&log_accum, adm_access, dst_path, pool));
    }

  /* Install new text base. */
  {
    unsigned char digest[APR_MD5_DIGESTSIZE];
    svn_wc_entry_t tmp_entry;

    /* Write out log commands to set up the new text base and its
       checksum. */
    SVN_ERR(svn_wc__loggy_move(&log_accum, NULL,
                               adm_access, tmp_text_base_path,
                               text_base_path, FALSE, pool));
    SVN_ERR(svn_wc__loggy_set_readonly(&log_accum, adm_access,
                                       text_base_path, pool));

    SVN_ERR(svn_io_file_checksum(digest, tmp_text_base_path, pool));

    tmp_entry.checksum = svn_md5_digest_to_cstring(digest, pool);
    SVN_ERR(svn_wc__loggy_entry_modify(&log_accum, adm_access,
                                       dst_path, &tmp_entry,
                                       SVN_WC__ENTRY_MODIFY_CHECKSUM,
                                       pool));
  }


  /* Write our accumulation of log entries into a log file */
  SVN_ERR(svn_wc__write_log(adm_access, 0, log_accum, pool));

  SVN_ERR(svn_wc__run_log(adm_access, NULL, pool));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc_add_repos_file(const char *dst_path,
                      svn_wc_adm_access_t *adm_access,
                      const char *new_text_path,
                      apr_hash_t *new_props,
                      const char *copyfrom_url,
                      svn_revnum_t copyfrom_rev,
                      apr_pool_t *pool)
{
  return svn_wc_add_repos_file2(dst_path, adm_access,
                                new_text_path, NULL,
                                new_props, NULL,
                                copyfrom_url, copyfrom_rev,
                                pool);
}
