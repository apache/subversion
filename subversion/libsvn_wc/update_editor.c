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
#include "svn_wc.h"
#include "svn_private_config.h"
#include "svn_time.h"

#include "wc.h"
#include "log.h"
#include "adm_files.h"
#include "adm_ops.h"
#include "entries.h"
#include "lock.h"
#include "props.h"
#include "translate.h"


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

  /* The revision we're targeting...or something like that.  This
     starts off as a pointer to the revision to which we are updating,
     or SVN_INVALID_REVNUM, but by the end of the edit, should be
     pointing to the final revision. */
  svn_revnum_t *target_revision;

  /* Whether this edit will descend into subdirs */
  svn_boolean_t recurse;

  /* Need to know if the user wants us to overwrite the 'now' times on
     edited/added files with the last-commit-time. */
  svn_boolean_t use_commit_times;

  /* Was the root actually opened (was this a non-empty edit)? */
  svn_boolean_t root_opened;

  /* Was the update-target deleted?  This is a special situation. */
  svn_boolean_t target_deleted;
 
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

  /* An array of svn_prop_t structures, representing all the property
     changes to be applied to this directory. */
  apr_array_header_t *propchanges;

  /* The bump information for this directory. */
  struct bump_dir_info *bump_info;

  /* The current log file number. */
  int log_number;

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
      if (err)
        svn_error_clear(err);
      return NULL;
    }

  return entry->url;
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

  /* If there are no log files to write, return immediately. */
  if (db->log_number == 0)
    return APR_SUCCESS;

  err = svn_wc_adm_retrieve(&adm_access, db->edit_baton->adm_access,
                            db->path, apr_pool_parent_get(db->pool));

  if (! err)
    {
      err = svn_wc__run_log(adm_access, NULL, apr_pool_parent_get(db->pool));
      
      if (! err)
        return APR_SUCCESS;
    }
  
  apr_err = err->apr_err;
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
static struct dir_baton *
make_dir_baton(const char *path,
               struct edit_baton *eb,
               struct dir_baton *pb,
               svn_boolean_t added,
               apr_pool_t *pool)
{
  struct dir_baton *d = apr_pcalloc(pool, sizeof(*d));
  struct bump_dir_info *bdi;
  
  /* Don't do this.  Just do NOT do this to me. */
  if (pb && (! path))
    abort();

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

  /* the parent's bump info has one more referer */
  if (pb)
    ++bdi->parent->ref_count;

  d->edit_baton   = eb;
  d->parent_baton = pb;
  d->pool         = svn_pool_create(pool);
  d->propchanges  = apr_array_make(pool, 1, sizeof(svn_prop_t));
  d->added        = added;
  d->bump_info    = bdi;
  d->log_number   = 0;

  apr_pool_cleanup_register(d->pool, d, cleanup_dir_baton,
                            cleanup_dir_baton_child);
  
  return d;
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

  /* This gets set if the file underwent a text change, which guides
     the code that syncs up the adm dir and working copy. */
  svn_boolean_t text_changed;

  /* This gets set if the file underwent a prop change, which guides
     the code that syncs up the adm dir and working copy. */
  svn_boolean_t prop_changed;

  /* An array of svn_prop_t structures, representing all the property
     changes to be applied to this file. */
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
static struct file_baton *
make_file_baton(struct dir_baton *pb,
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

  f->pool         = pool;
  f->edit_baton   = pb->edit_baton;
  f->propchanges  = apr_array_make(pool, 1, sizeof(svn_prop_t));
  f->bump_info    = pb->bump_info;
  f->added        = adding;
  f->dir_baton    = pb;

  /* No need to initialize f->digest, since we used pcalloc(). */

  /* the directory's bump info has one more referer now */
  ++f->bump_info->ref_count;

  return f;
}



/*** Helpers for the editor callbacks. ***/

static svn_error_t *
window_handler(svn_txdelta_window_t *window, void *baton)
{
  struct handler_baton *hb = baton;
  struct file_baton *fb = hb->fb;
  svn_error_t *err = SVN_NO_ERROR, *err2 = SVN_NO_ERROR;

  /* Apply this window.  We may be done at that point.  */
  err = hb->apply_handler(window, hb->apply_baton);
  if (window != NULL && err == SVN_NO_ERROR)
    return err;

  /* Either we're done (window is NULL) or we had an error.  In either
     case, clean up the handler.  */
  if (hb->source)
    {
      err2 = svn_wc__close_text_base(hb->source, fb->path, 0, fb->pool);
      if (err2 != SVN_NO_ERROR && err == SVN_NO_ERROR)
        err = err2;
      else
        svn_error_clear(err2);
    }
  err2 = svn_wc__close_text_base(hb->dest, fb->path, 0, fb->pool);
  if (err2 != SVN_NO_ERROR)
    {
      if (err == SVN_NO_ERROR)
        err = err2;
      else
        svn_error_clear(err2);
    }
  svn_pool_destroy(hb->pool);

  if (err != SVN_NO_ERROR)
    {
      /* We failed to apply the patch; clean up the temporary file.  */
      const char *tmppath = svn_wc__text_base_path(fb->path, TRUE, fb->pool);
      apr_file_remove(tmppath, fb->pool);
    }
  else
    {
      /* Leave a note in the baton indicating that there's new text to
         sync up.  */
      fb->text_changed = 1;
    }

  return err;
}


/* Prepare directory for dir_baton DB for updating or checking out.
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
  SVN_ERR(svn_wc_ensure_adm2(db->path, NULL,
                             ancestor_url, repos,
                             ancestor_revision, pool));

  if (! db->edit_baton->adm_access
      || strcmp(svn_wc_adm_access_path(db->edit_baton->adm_access),
                db->path))
    {
      svn_wc_adm_access_t *adm_access;
      apr_pool_t *adm_access_pool
        = db->edit_baton->adm_access
        ? svn_wc_adm_access_pool(db->edit_baton->adm_access)
        : db->edit_baton->pool;

      SVN_ERR(svn_wc_adm_open3(&adm_access, db->edit_baton->adm_access,
                               db->path, TRUE, 0, NULL, NULL,
                               adm_access_pool));
      if (!db->edit_baton->adm_access)
        db->edit_baton->adm_access = adm_access;
    }
  
  return SVN_NO_ERROR;
}


/* Accumulate tags in LOG_ACCUM to set ENTRY_PROPS for BASE_NAME.
   ENTRY_PROPS is an array of svn_prop_t* entry props.
   If ENTRY_PROPS contains the removal of a lock token, all entryprops
   related to a lock will be removed and LOCK_STATE, if non-NULL, will be
   set to svn_wc_notify_lock_state_unlocked.  Else, LOCK_STATE, if non-NULL
   will be set to svn_wc_lock_state_unchanged. */
static svn_error_t *
accumulate_entry_props(svn_stringbuf_t *log_accum,
                       svn_wc_notify_lock_state_t *lock_state,
                       svn_wc_adm_access_t *adm_access,
                       const char *base_name,
                       apr_array_header_t *entry_props,
                       apr_pool_t *pool)
{
  int i;
  svn_wc_entry_t tmp_entry;
  apr_uint32_t flags = 0;

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
          SVN_ERR(svn_wc__loggy_delete_lock(&log_accum, adm_access,
                                            base_name, pool));
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
    SVN_ERR(svn_wc__loggy_entry_modify(&log_accum, adm_access, base_name,
                                       &tmp_entry, flags, pool));

  return SVN_NO_ERROR;
}


/* Accumulate tags in LOG_ACCUM to set WCPROPS for BASE_NAME.  WCPROPS is
   an array of svn_prop_t* wc props. */
static svn_error_t *
accumulate_wcprops(svn_stringbuf_t *log_accum,
                   svn_wc_adm_access_t *adm_access,
                   const char *base_name,
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
              (&log_accum, adm_access,
               base_name,
               prop->name, prop->value ? prop->value->data : NULL, pool));
    }

  return SVN_NO_ERROR;
}
      

/*** The callbacks we'll plug into an svn_delta_editor_t structure. ***/

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

  *dir_baton = d = make_dir_baton(NULL, eb, NULL, FALSE, pool);
  if (! *eb->target)
    {
      /* For an update with a NULL target, this is equivalent to open_dir(): */
      svn_wc_adm_access_t *adm_access;
      svn_wc_entry_t tmp_entry;
      apr_uint32_t flags = SVN_WC__ENTRY_MODIFY_REVISION |
        SVN_WC__ENTRY_MODIFY_URL | SVN_WC__ENTRY_MODIFY_INCOMPLETE;
                                     
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


static svn_error_t *
do_entry_deletion(struct edit_baton *eb,
                  const char *parent_path,
                  const char *path,
                  int *log_number,
                  apr_pool_t *pool)
{
  apr_file_t *log_fp = NULL;
  const char *base_name;
  svn_wc_adm_access_t *adm_access;
  svn_node_kind_t kind;
  const char *logfile_path, *logfile_name;
  const char *full_path = svn_path_join(eb->anchor, path, pool);
  svn_stringbuf_t *log_item = svn_stringbuf_create("", pool);

  SVN_ERR(svn_io_check_path(full_path, &kind, pool));

  SVN_ERR(svn_wc_adm_retrieve(&adm_access, eb->adm_access,
                              parent_path, pool));

  logfile_name = svn_wc__logfile_path(*log_number, pool);
  
  logfile_path = svn_wc__adm_path(parent_path, FALSE, pool,
                                  logfile_name, NULL);

  SVN_ERR(svn_wc__open_adm_file(&log_fp,
                                parent_path,
                                logfile_name,
                                (APR_WRITE | APR_CREATE), /* not excl */
                                pool));

  /* Here's the deal: in the new editor interface, PATH is a full path
     below the editor's anchor, and parent_path is the parent directory.
     That's all fine and well, but our log-system requires that all
     log commands talk *only* about paths relative (and below)
     parent_path, i.e. where the log is being executed.  */

  base_name = svn_path_basename(path, pool);
  SVN_ERR(svn_wc__loggy_delete_entry(&log_item, adm_access,
                                     base_name, pool));

  /* If the thing being deleted is the *target* of this update, then
     we need to recreate a 'deleted' entry, so that parent can give
     accurate reports about itself in the future. */
  if (strcmp(path, eb->target) == 0)
    {
      svn_wc_entry_t tmp_entry;

      tmp_entry.revision = *(eb->target_revision);
      tmp_entry.kind = (kind == svn_node_file) ? svn_node_file : svn_node_dir;
      tmp_entry.deleted = TRUE;

      SVN_ERR(svn_wc__loggy_entry_modify(&log_item, adm_access,
                                         path, &tmp_entry,
                                         /*### path should have been base_name?! */
                                         SVN_WC__ENTRY_MODIFY_REVISION
                                         | SVN_WC__ENTRY_MODIFY_KIND
                                         | SVN_WC__ENTRY_MODIFY_DELETED,
                                         pool));

      eb->target_deleted = TRUE;
    }

  SVN_ERR_W(svn_io_file_write_full(log_fp, log_item->data, 
                                   log_item->len, NULL, pool),
            apr_psprintf(pool, 
                         _("Error writing log file for '%s'"),
                         svn_path_local_style(parent_path, pool)));

  SVN_ERR(svn_wc__close_adm_file(log_fp,
                                 parent_path,
                                 logfile_name,
                                 TRUE, /* sync */
                                 pool));
    
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

      if (kind == svn_node_dir)
        {
          svn_wc_adm_access_t *child_access;

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

  SVN_ERR(leftmod_error_chain(svn_wc__run_log(adm_access, NULL, pool),
                              logfile_path, parent_path, pool));
  *log_number = 0;

  /* The passed-in `path' is relative to the anchor of the edit, so if
   * the operation was invoked on something other than ".", then
   * `path' will be wrong for purposes of notification.  However, we
   * can always count on the parent_path being the parent of base_name,
   * so we just join them together to get a good notification path.
   */
  if (eb->notify_func)
    (*eb->notify_func)
      (eb->notify_baton,
       svn_wc_create_notify(svn_path_join(parent_path, base_name, pool),
                            svn_wc_notify_update_delete, pool), pool);

  return SVN_NO_ERROR;
}


static svn_error_t *
delete_entry(const char *path, 
             svn_revnum_t revision, 
             void *parent_baton,
             apr_pool_t *pool)
{
  struct dir_baton *pb = parent_baton;
  return do_entry_deletion(pb->edit_baton, pb->path, path, &pb->log_number,
                           pool);
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
  struct dir_baton *db = make_dir_baton(path, eb, pb, TRUE, pool);
  svn_node_kind_t kind;

  /* Semantic check.  Either both "copyfrom" args are valid, or they're
     NULL and SVN_INVALID_REVNUM.  A mixture is illegal semantics. */
  if ((copyfrom_path && (! SVN_IS_VALID_REVNUM(copyfrom_revision)))
      || ((! copyfrom_path) && (SVN_IS_VALID_REVNUM(copyfrom_revision))))
    abort();

  /* There should be nothing with this name. */
  SVN_ERR(svn_io_check_path(db->path, &kind, db->pool));
  if (kind != svn_node_none)
    return svn_error_createf
      (SVN_ERR_WC_OBSTRUCTED_UPDATE, NULL,
       _("Failed to add directory '%s': "
         "object of the same name already exists"),
       svn_path_local_style(db->path, pool));

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
         copied tree.  Someday! */      
      return svn_error_createf
        (SVN_ERR_UNSUPPORTED_FEATURE, NULL,
         _("Failed to add directory '%s': "
           "copyfrom arguments not yet supported"),
         svn_path_local_style(db->path, pool));
    }
  else  /* ...or we got invalid copyfrom args. */
    {
      svn_wc_adm_access_t *adm_access;
      const svn_wc_entry_t *dir_entry;
      apr_hash_t *entries;
      svn_wc_entry_t tmp_entry;

      /* Extra check:  a directory by this name may not exist, but there
         may still be one scheduled for addition.  That's a genuine
         tree-conflict.  */
      SVN_ERR(svn_wc_adm_retrieve(&adm_access, eb->adm_access,
                                  pb->path, db->pool));
      SVN_ERR(svn_wc_entries_read(&entries, adm_access, FALSE, db->pool));
      dir_entry = apr_hash_get(entries, db->name, APR_HASH_KEY_STRING);
      if (dir_entry && dir_entry->schedule == svn_wc_schedule_add)
        return svn_error_createf
          (SVN_ERR_WC_OBSTRUCTED_UPDATE, NULL,
           _("Failed to add directory '%s': object of the same name "
             "is already scheduled for addition"),
           svn_path_local_style(path, pool));

      /* Immediately create an entry for the new directory in the parent.
         Note that the parent must already be either added or opened, and
         thus it's in an 'incomplete' state just like the new dir.  */      
      tmp_entry.kind = svn_node_dir;
      /* Note that there may already exist a 'ghost' entry in the
         parent with the same name, in a 'deleted' or 'absent' state.
         If so, it's fine to overwrite it... but we need to make sure
         we get rid of the state flag when doing so: */
      tmp_entry.deleted = FALSE;
      tmp_entry.absent = FALSE;
      SVN_ERR(svn_wc__entry_modify(adm_access, db->name, &tmp_entry,
                                   (SVN_WC__ENTRY_MODIFY_KIND    |
                                    SVN_WC__ENTRY_MODIFY_DELETED |
                                    SVN_WC__ENTRY_MODIFY_ABSENT),
                                   TRUE /* immediate write */, pool));
    }

  SVN_ERR(prep_directory(db,
                         db->new_URL,
                         *(eb->target_revision),
                         db->pool));

  *child_baton = db;

  if (eb->notify_func)
    {
      svn_wc_notify_t *notify = svn_wc_create_notify(db->path,
                                                     svn_wc_notify_update_add,
                                                     pool);
      notify->kind = svn_node_dir;
      (*eb->notify_func)(eb->notify_baton, notify, pool);
    }

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
  struct edit_baton *eb = pb->edit_baton;
  svn_wc_entry_t tmp_entry;
  apr_uint32_t flags = SVN_WC__ENTRY_MODIFY_REVISION |
    SVN_WC__ENTRY_MODIFY_URL | SVN_WC__ENTRY_MODIFY_INCOMPLETE;
                                 
  svn_wc_adm_access_t *adm_access;

  /* kff todo: check that the dir exists locally, find it somewhere if
     its not there?  Yes, all this and more...  And ancestor_url and
     ancestor_revision need to get used. */

  struct dir_baton *db = make_dir_baton(path, eb, pb, FALSE, pool);
  *child_baton = db;

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

  SVN_ERR(svn_wc_adm_retrieve(&adm_access, eb->adm_access,
                              db->path, pool));  
  SVN_ERR(svn_wc__entry_modify(adm_access, NULL /* THIS_DIR */,
                               &tmp_entry, flags,
                               TRUE /* immediate write */,
                               pool));

  return SVN_NO_ERROR;
}


static svn_error_t *
change_dir_prop(void *dir_baton,
                const char *name,
                const svn_string_t *value,
                apr_pool_t *pool)
{
  svn_prop_t *propchange;
  struct dir_baton *db = dir_baton;

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

static svn_error_t *
close_directory(void *dir_baton,
                apr_pool_t *pool)
{
  struct dir_baton *db = dir_baton;
  svn_wc_notify_state_t prop_state = svn_wc_notify_state_unknown;
  apr_array_header_t *entry_props, *wc_props, *regular_props;
  svn_wc_adm_access_t *adm_access;
      
  SVN_ERR(svn_categorize_props(db->propchanges, &entry_props, &wc_props,
                               &regular_props, pool));

  SVN_ERR(svn_wc_adm_retrieve(&adm_access, db->edit_baton->adm_access,
                              db->path, db->pool));
      
  /* If this directory has property changes stored up, now is the time
     to deal with them. */
  if (regular_props->nelts || entry_props->nelts || wc_props->nelts)
    {
      /* to hold log messages: */
      svn_stringbuf_t *entry_accum = svn_stringbuf_create("", db->pool);

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
                  else  /* something changed, record the change */
                    {
                      /* We can't assume that ti came pre-loaded with the
                         old values of the svn:externals property.  Yes,
                         most callers will have already initialized ti by
                         sending it through svn_wc_crawl_revisions, but we
                         shouldn't count on that here -- so we set both the
                         old and new values again. */

                      if (old_val_s)
                        apr_hash_set(ti->externals_old,
                                     apr_pstrdup(ti->pool, db->path),
                                     APR_HASH_KEY_STRING,
                                     apr_pstrmemdup(ti->pool, old_val_s->data,
                                                    old_val_s->len));

                      if (new_val_s)
                        apr_hash_set(ti->externals_new,
                                     apr_pstrdup(ti->pool, db->path),
                                     APR_HASH_KEY_STRING,
                                     apr_pstrmemdup(ti->pool, new_val_s->data,
                                                    new_val_s->len));
                    }
                }
            }

          /* Merge pending properties into temporary files (ignoring
             conflicts). */
          SVN_ERR_W(svn_wc__merge_props(&prop_state,
                                        adm_access, NULL,
                                        NULL /* use baseprops */,
                                        regular_props, TRUE, FALSE,
                                        db->pool, &entry_accum),
                    _("Couldn't do property merge"));
        }

      SVN_ERR(accumulate_entry_props(entry_accum, NULL,
                                     adm_access, SVN_WC_ENTRY_THIS_DIR,
                                     entry_props, pool));

      SVN_ERR(accumulate_wcprops(entry_accum, adm_access,
                                 SVN_WC_ENTRY_THIS_DIR, wc_props, pool));

      /* Write our accumulation of log entries into a log file */
      SVN_ERR(svn_wc__write_log(adm_access, db->log_number, entry_accum, pool));
    }

  /* Run the log. */
  SVN_ERR(svn_wc__run_log(adm_access, db->edit_baton->diff3_cmd, db->pool));
  db->log_number = 0;
  
  /* We're done with this directory, so remove one reference from the
     bump information. This may trigger a number of actions. See
     maybe_bump_dir_info() for more information.  */
  SVN_ERR(maybe_bump_dir_info(db->edit_baton, db->bump_info, db->pool));

  /* Notify of any prop changes on this directory -- but do nothing
     if it's an added directory, because notification has already
     happened in that case. */
  if ((! db->added) && (db->edit_baton->notify_func))
    {
      svn_wc_notify_t *notify
        = svn_wc_create_notify(db->path, svn_wc_notify_update_update, pool);
      notify->kind = svn_node_dir;
      notify->prop_state = prop_state;
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


static svn_error_t *
absent_file(const char *path,
            void *parent_baton,
            apr_pool_t *pool)
{
  return absent_file_or_dir(path, svn_node_file, parent_baton, pool);
}


static svn_error_t *
absent_directory(const char *path,
                 void *parent_baton,
                 apr_pool_t *pool)
{
  return absent_file_or_dir(path, svn_node_dir, parent_baton, pool);
}


/* Common code for add_file() and open_file(). */
static svn_error_t *
add_or_open_file(const char *path,
                 void *parent_baton,
                 const char *copyfrom_path,
                 svn_revnum_t copyfrom_rev,
                 void **file_baton,
                 svn_boolean_t adding, /* 0 if replacing */
                 apr_pool_t *pool)
{
  struct dir_baton *pb = parent_baton;
  struct edit_baton *eb = pb->edit_baton;
  struct file_baton *fb;
  const svn_wc_entry_t *entry;
  svn_node_kind_t kind;
  svn_wc_adm_access_t *adm_access;

  /* the file_pool can stick around for a *long* time, so we want to use
     a subpool for any temporary allocations. */
  apr_pool_t *subpool = svn_pool_create(pool);

  /* ### kff todo: if file is marked as removed by user, then flag a
     conflict in the entry and proceed.  Similarly if it has changed
     kind.  see issuezilla task #398. */

  fb = make_file_baton(pb, path, adding, pool);

  /* It is interesting to note: everything below is just validation. We
     aren't actually doing any "work" or fetching any persistent data. */

  SVN_ERR(svn_io_check_path(fb->path, &kind, subpool));
  SVN_ERR(svn_wc_adm_retrieve(&adm_access, eb->adm_access, 
                              pb->path, subpool));
  SVN_ERR(svn_wc_entry(&entry, fb->path, adm_access, FALSE, subpool));
  
  /* Sanity checks. */

  /* If adding, there should be nothing with this name. */
  if (adding && (kind != svn_node_none))
    return svn_error_createf
      (SVN_ERR_WC_OBSTRUCTED_UPDATE, NULL,
       _("Failed to add file '%s': object of the same name already exists"),
       svn_path_local_style(fb->path, pool));

  /* sussman sez: If we're trying to add a file that's already in
     `entries' (but not on disk), that's okay.  It's probably because
     the user deleted the working version and ran 'svn up' as a means
     of getting the file back.  

     It certainly doesn't hurt to re-add the file.  We can't possibly
     get the entry showing up twice in `entries', since it's a hash;
     and we know that we won't lose any local mods.  Let the existing
     entry be overwritten.

     sussman follows up to himself, many months later: the above
     scenario is fine, as long as the pre-existing entry isn't
     scheduled for addition.  that's a genuine tree-conflict,
     regardless of whether the working file still exists.  */

  if (adding && entry && entry->schedule == svn_wc_schedule_add)
    return svn_error_createf
      (SVN_ERR_WC_OBSTRUCTED_UPDATE, NULL,
       _("Failed to add file '%s': object of the same name is already "
         "scheduled for addition"),
       svn_path_local_style(fb->path, pool));
    

  /* If replacing, make sure the .svn entry already exists. */
  if ((! adding) && (! entry))
    return svn_error_createf(SVN_ERR_UNVERSIONED_RESOURCE, NULL,
                             _("File '%s' in directory '%s' "
                               "is not a versioned resource"),
                             fb->name,
                             svn_path_local_style(pb->path, pool));
  
  /* ### todo:  right now the incoming copyfrom* args are being
     completely ignored!  Someday the editor-driver may expect us to
     support this optimization;  when that happens, this func needs to
     -copy- the specified existing wc file to this location.  From
     there, the driver can apply_textdelta on it, etc. */

  svn_pool_destroy(subpool);

  *file_baton = fb;
  return SVN_NO_ERROR;
}


static svn_error_t *
add_file(const char *name,
         void *parent_baton,
         const char *copyfrom_path,
         svn_revnum_t copyfrom_revision,
         apr_pool_t *pool,
         void **file_baton)
{
  return add_or_open_file(name, parent_baton, copyfrom_path, 
                          copyfrom_revision, file_baton, TRUE, pool);
}


static svn_error_t *
open_file(const char *name,
          void *parent_baton,
          svn_revnum_t base_revision,
          apr_pool_t *pool,
          void **file_baton)
{
  return add_or_open_file(name, parent_baton, NULL, base_revision, 
                          file_baton, FALSE, pool);
}


static svn_error_t *
apply_textdelta(void *file_baton, 
                const char *base_checksum,
                apr_pool_t *pool,
                svn_txdelta_window_handler_t *handler,
                void **handler_baton)
{
  struct file_baton *fb = file_baton;
  struct edit_baton *eb = fb->edit_baton;
  apr_pool_t *handler_pool = svn_pool_create(fb->pool);
  struct handler_baton *hb = apr_palloc(handler_pool, sizeof(*hb));
  svn_error_t *err;
  svn_wc_adm_access_t *adm_access;
  const svn_wc_entry_t *ent;
  svn_boolean_t replaced;
  svn_boolean_t use_revert_base;

  /* Open the text base for reading, unless this is a checkout. */
  hb->source = NULL;

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

  /* Before applying incoming svndiff data to text base, make sure
     text base hasn't been corrupted, and that its checksum
     matches the expected base checksum. */
  SVN_ERR(svn_wc_adm_retrieve(&adm_access, eb->adm_access,
                              svn_path_dirname(fb->path, pool), pool));
  SVN_ERR(svn_wc_entry(&ent, fb->path, adm_access, FALSE, pool));

  replaced = ent && ent->schedule == svn_wc_schedule_replace;
  use_revert_base = replaced && (ent->copyfrom_url != NULL);

  /* Only compare checksums this file has an entry, and the entry has
     a checksum.  If there's no entry, it just means the file is
     created in this update, so there won't be any previously recorded
     checksum to compare against.  If no checksum, well, for backwards
     compatibility we assume that no checksum always matches. */
  if (ent && ent->checksum)
    {
      unsigned char digest[APR_MD5_DIGESTSIZE];
      const char *hex_digest;
      const char *tb;

      if (use_revert_base)
        tb = svn_wc__text_revert_path(fb->path, FALSE, pool);
      else
        tb = svn_wc__text_base_path(fb->path, FALSE, pool);
      SVN_ERR(svn_io_file_checksum(digest, tb, pool));
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
               svn_path_local_style(tb, pool), base_checksum, hex_digest);
        }
      
      if ((ent && ent->checksum) && ! replaced &&
          strcmp(hex_digest, ent->checksum) != 0)
        {
          return svn_error_createf
            (SVN_ERR_WC_CORRUPT_TEXT_BASE, NULL,
             _("Checksum mismatch for '%s'; recorded: '%s', actual: '%s'"),
             svn_path_local_style(tb, pool), ent->checksum, hex_digest);
        }
    }

  if (use_revert_base)
    err = svn_wc__open_revert_base(&hb->source, fb->path,
                                   APR_READ,
                                   handler_pool);
  else
    err = svn_wc__open_text_base(&hb->source, fb->path, APR_READ,
                                 handler_pool);

  if (err && !APR_STATUS_IS_ENOENT(err->apr_err))
    {
      if (hb->source)
        {
          if (use_revert_base)
            svn_error_clear(svn_wc__close_revert_base(hb->source, fb->path,
                                                      0, handler_pool));
          else
            svn_error_clear(svn_wc__close_text_base(hb->source, fb->path,
                                                    0, handler_pool));
        }
      svn_pool_destroy(handler_pool);
      return err;
    }
  else if (err)
    {
      svn_error_clear(err);
      hb->source = NULL;  /* make sure */
    }

  /* Open the text base for writing (this will get us a temporary file).  */
  hb->dest = NULL;

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
      if (hb->dest)
        {
          if (replaced)
            svn_error_clear(svn_wc__close_revert_base(hb->dest, fb->path, 0,
                                                      handler_pool));
          else
            svn_error_clear(svn_wc__close_text_base(hb->dest, fb->path, 0,
                                                    handler_pool));
        }
      svn_pool_destroy(handler_pool);
      return err;
    }
  
  /* Prepare to apply the delta.  */
  {
    const char *tmp_path;

    apr_file_name_get(&tmp_path, hb->dest);
    svn_txdelta_apply(svn_stream_from_aprfile(hb->source, handler_pool),
                      svn_stream_from_aprfile(hb->dest, handler_pool),
                      fb->digest, tmp_path, handler_pool,
                      &hb->apply_handler, &hb->apply_baton);
  }
  
  hb->pool = handler_pool;
  hb->fb = fb;
  
  /* We're all set.  */
  *handler_baton = hb;
  *handler = window_handler;

  return SVN_NO_ERROR;
}




static svn_error_t *
change_file_prop(void *file_baton,
                 const char *name,
                 const svn_string_t *value,
                 apr_pool_t *pool)
{
  struct file_baton *fb = file_baton;
  struct edit_baton *eb = fb->edit_baton;
  svn_prop_t *propchange;

  /* Push a new propchange to the file baton's array of propchanges */
  propchange = apr_array_push(fb->propchanges);
  propchange->name = apr_pstrdup(fb->pool, name);
  propchange->value = value ? svn_string_dup(value, fb->pool) : NULL;

  /* Let close_file() know that propchanges are waiting to be
     applied. */
  fb->prop_changed = 1;

  /* Special case: if the file is added during a checkout, cache the
     last-changed-date propval for future use. */
  if (eb->use_commit_times
      && (strcmp(name, SVN_PROP_ENTRY_COMMITTED_DATE) == 0)
      && value)
    fb->last_changed_date = apr_pstrdup(fb->pool, value->data);

  return SVN_NO_ERROR;
}


/* Write log commands to merge PROP_CHANGES into the existing properties of
   FILE_PATH.  PROP_CHANGES can contain regular properties as well as
   entryprops and wcprops.  Update *PROP_STATE (unless PROP_STATE is NULL)
   to reflect the result of the regular prop merge.
   Make *LOCK_STATE reflect the possible removal of a lock token from
   FILE_PATH's entryprops.

   ADM_ACCESS is the access baton for FILE_PATH.  Append log commands to
   LOG_ACCUM.  Use POOL for temporary allocations. */
static svn_error_t *
merge_props(svn_stringbuf_t *log_accum,
            svn_wc_notify_state_t *prop_state,
            svn_wc_notify_lock_state_t *lock_state,
            svn_wc_adm_access_t *adm_access,
            const char *file_path,
            const apr_array_header_t *prop_changes,
            apr_pool_t *pool)
{
  apr_array_header_t *regular_props = NULL, *wc_props = NULL,
    *entry_props = NULL;
  const char *base_name;

  svn_path_split(file_path, NULL, &base_name, pool);

  /* Sort the property list into three arrays, based on kind. */
  SVN_ERR(svn_categorize_props(prop_changes,
                               &entry_props, &wc_props, &regular_props,
                               pool));

  /* Always initialize to unknown state. */
  if (prop_state)
    *prop_state = svn_wc_notify_state_unknown;

  /* Merge the 'regular' props into the existing working proplist. */
  if (regular_props)
    {
      /* This will merge the old and new props into a new prop db, and
         write <cp> commands to the logfile to install the merged
         props.  */
      SVN_ERR(svn_wc__merge_props(prop_state,
                                  adm_access, base_name,
                                  NULL /* use base props */,
                                  regular_props, TRUE, FALSE, pool,
                                  &log_accum));
    }
  
  /* If there are any ENTRY PROPS, make sure those get appended to the
     growing log as fields for the file's entry.

     Note that no merging needs to happen; these kinds of props aren't
     versioned, so if the property is present, we overwrite the value. */  
  if (entry_props)
    SVN_ERR(accumulate_entry_props(log_accum, lock_state,
                                   adm_access, base_name,
                                   entry_props, pool));
  else
    *lock_state = svn_wc_notify_lock_state_unchanged;

  /* Possibly write log commands to tweak prop entry timestamp. */
  if (regular_props)
    {
      svn_boolean_t prop_modified;

      /* Are the working file's props locally modified? */
      SVN_ERR(svn_wc_props_modified_p(&prop_modified,
                                      file_path, adm_access,
                                      pool));

      /* Log entry which sets a new property timestamp, but only if
         there are no local changes to the props. */
      if (! prop_modified)
        SVN_ERR(svn_wc__loggy_set_entry_timestamp_from_wc
                (&log_accum, adm_access,
                 base_name, SVN_WC__ENTRY_ATTR_PROP_TIME, pool));
    }

  /* This writes a whole bunch of log commands to install wcprops.  */
  if (wc_props)
    SVN_ERR(accumulate_wcprops(log_accum, adm_access,
                               base_name, wc_props, pool));

  return SVN_NO_ERROR;
}

/* Append, to LOG_ACCUM, log commands to update the entry for NAME in
   ADM_ACCESS with a NEW_REVISION and a NEW_URL (if non-NULL), making sure
   the entry refers to a file and has no absent or deleted state.
   Use POOL for temporary allocations. */
static svn_error_t *
tweak_entry(svn_stringbuf_t *log_accum,
            svn_wc_adm_access_t *adm_access,
            const char *name,
            svn_revnum_t new_revision,
            const char *new_URL,
            apr_pool_t *pool)
{
  /* Write log entry which will bump the revision number.  Also, just
     in case we're overwriting an existing phantom 'deleted' or
     'absent' entry, be sure to remove the hiddenness. */
  svn_wc_entry_t tmp_entry;
  apr_uint32_t modify_flags = SVN_WC__ENTRY_MODIFY_KIND
    | SVN_WC__ENTRY_MODIFY_REVISION
    | SVN_WC__ENTRY_MODIFY_DELETED
    | SVN_WC__ENTRY_MODIFY_ABSENT;


  tmp_entry.revision = new_revision;
  tmp_entry.kind = svn_node_file;
  tmp_entry.deleted = FALSE;
  tmp_entry.absent = FALSE;

  /* Possibly install a *non*-inherited URL in the entry. */
  if (new_URL)
    {
      tmp_entry.url = new_URL;
      modify_flags |= SVN_WC__ENTRY_MODIFY_URL;
    }

  SVN_ERR(svn_wc__loggy_entry_modify(&log_accum, adm_access,
                                     name, &tmp_entry, modify_flags,
                                     pool));

  return SVN_NO_ERROR;
}


/* This is the small planet.  It has the complex responsibility of
 * "integrating" a new revision of a file into a working copy. 
 *
 * Given a FILE_PATH either already under version control, or
 * prepared (see below) to join version control, fully install a
 * NEW_REVISION of the file;  ADM_ACCESS is an access baton with a
 * write lock for the directory containing FILE_PATH.
 *
 * If FILE_PATH is not already under version control (i.e., does not
 * have an entry), then the raw data (for example the new text
 * base and new props) required to put it under version control must
 * be provided by the caller.  See below for details.
 *
 * By "install", we mean: create a new text-base and prop-base, merge
 * any textual and property changes into the working file, and finally
 * update all metadata so that the working copy believes it has a new
 * working revision of the file.  All of this work includes being
 * sensitive to eol translation, keyword substitution, and performing
 * all actions accumulated to existing LOG_ACCUM.
 *
 * If HAS_NEW_TEXT_BASE is TRUE, the administrative area must contain the
 * 'new' text base for NEW_REVISION in the temporary text base location.
 * The temporary text base will be removed after a successful run of the
 * generated log commands.  If there is no text base, HAS_NEW_TEXT_BASE
 * must be FALSE.
 *
 * The caller also provides the property changes for the file in the
 * PROP_CHANGES array; if there are no prop changes, then the caller must pass 
 * NULL instead.  This argument is an array of svn_prop_t structures, 
 * representing differences against the files existing base properties.
 * (A deletion is represented by setting an svn_prop_t's 'value'
 * field to NULL.)
 *
 * Note that the PROP_CHANGES array is expected to contain all categories of
 * props, not just 'regular' ones that the user sees.  (See enum
 * svn_prop_kind).
 *
 * If CONTENT_STATE is non-null, set *CONTENT_STATE to the state of
 * the file contents after the installation; if return error, the
 * value of *CONTENT_STATE is undefined.
 *
 * If PROP_STATE is non-null, set *PROP_STATE to the state of the
 * properties after the installation; if return error, the value of
 * *PROP_STATE is undefined.
 *
 * If LOCK_STATE is non-null, set it to the state of the lock on the
 * file after the operation; if an error is returned, the value of
 * @a LOCK_STATE is undefined.
 *
 * If NEW_URL is non-null, then this URL will be attached to the file
 * in the 'entries' file.  Otherwise, the file will simply "inherit"
 * its URL from the parent dir.
 *
 * If DIFF3_CMD is non-null, then use it as the diff3 command for
 * any merging; otherwise, use the built-in merge code.
 *
 * If TIMESTAMP_STRING is non-null, then use it to set the
 * timestamp on the final working file.  The string should be
 * formatted for use by svn_time_from_cstring().
 *
 * POOL is used for all bookkeeping work during the installation.
 */
static svn_error_t *
merge_file(svn_stringbuf_t *log_accum,
           svn_wc_notify_state_t *content_state,
           svn_wc_notify_state_t *prop_state,
           svn_wc_notify_lock_state_t *lock_state,
           svn_wc_adm_access_t *adm_access,
           const char *file_path,
           svn_revnum_t new_revision,
           svn_boolean_t has_new_text_base,
           const apr_array_header_t *prop_changes,
           const char *new_URL,
           const char *diff3_cmd,
           const char *timestamp_string,
           apr_pool_t *pool)
{
  const char *parent_dir, *base_name;
  const char *new_text_path = NULL;
  svn_boolean_t is_locally_modified;
  svn_boolean_t is_replaced = FALSE;
  svn_boolean_t magic_props_changed = FALSE;
  svn_boolean_t use_revert_base = FALSE;
  enum svn_wc_merge_outcome_t merge_outcome = svn_wc_merge_unchanged;
  svn_wc_notify_lock_state_t local_lock_state;

  /* The code flow does not depend upon these being set to NULL, but
     it removes a gcc 3.1 `might be used uninitialized in this
     function' warning. */
  const char *txtb = NULL, *tmp_txtb = NULL;

  /* We need the lock state, even if the caller doesn't. */
  if (! lock_state)
    lock_state = &local_lock_state;

  /* Start by splitting FILE_PATH. */
  svn_path_split(file_path, &parent_dir, &base_name, pool);

  /*
     When this function is called on file F, we assume the following
     things are true:

         - The new pristine text of F, if any, is present at
           .svn/tmp/text-base/F.svn-base

         - The .svn/entries file still reflects the old version of F.

         - .svn/text-base/F.svn-base is the old pristine F.

      The goal is to update the local working copy of F to reflect
      the changes received from the repository, preserving any local
      modifications.
  */

  if (has_new_text_base)
    new_text_path = svn_wc__text_base_path(file_path, TRUE, pool);
      
  /* Determine if any of the propchanges are the "magic" ones that
     might require changing the working file. */
  magic_props_changed = svn_wc__has_magic_property(prop_changes);

  /* Install all kinds of properties.  It is important to do this before
     any file content merging, since that process might expand keywords, in
     which case we want the new entryprops to be in place. */
  SVN_ERR(merge_props(log_accum, prop_state, lock_state, adm_access,
                      file_path, prop_changes, pool));

  /* Has the user made local mods to the working file?  */
  SVN_ERR(svn_wc_text_modified_p(&is_locally_modified, file_path,
                                 FALSE, adm_access, pool));

  /* In the case where the user has replaced a file with a copy it may 
   * not show as locally modified.  So if the file isn't listed as
   * locally modified test to see if it has been replaced.  If so,
   * remember that. */
  if (!is_locally_modified)
    {
      const svn_wc_entry_t *entry;
      SVN_ERR(svn_wc_entry(&entry, file_path, adm_access, FALSE, pool));
      if (entry && entry->schedule == svn_wc_schedule_replace)
        is_replaced = TRUE;
      if (is_replaced && entry->copyfrom_url)
        use_revert_base = TRUE;
    }

  if (new_text_path)   /* is there a new text-base to install? */
    {
      if (!use_revert_base)
        {
          txtb = svn_wc__text_base_path(base_name, FALSE, pool);
          tmp_txtb = svn_wc__text_base_path(base_name, TRUE, pool);
        }
      else
        {
          txtb = svn_wc__text_revert_path(base_name, FALSE, pool);
          tmp_txtb = svn_wc__text_revert_path(base_name, TRUE, pool);
        }
    }
  else
    {
      apr_hash_t *keywords;

      SVN_ERR(svn_wc__get_keywords(&keywords, file_path,
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

          /* A log command which copies and DEtranslates the working file
             to a tmp-text-base. */
          SVN_ERR(svn_wc_translated_file2(&tmptext, file_path, file_path,
                                          adm_access,
                                          SVN_WC_TRANSLATE_TO_NF
                                          | SVN_WC_TRANSLATE_NO_OUTPUT_CLEANUP,
                                          pool));

          tmptext = svn_path_is_child(parent_dir, tmptext, pool);
          /* A log command that copies the tmp-text-base and REtranslates
             the tmp-text-base back to the working file. */
          SVN_ERR(svn_wc__loggy_copy(&log_accum, NULL, adm_access,
                                 svn_wc__copy_translate,
                                     tmptext, base_name, FALSE, pool));

        }
    }

  /* Set the new revision and URL in the entry and clean up some other
     fields. */
  SVN_ERR(tweak_entry(log_accum, adm_access, base_name,
                      new_revision, new_URL, pool));

  /* For 'textual' merging, we implement this matrix.

                  Text file                   Binary File
               -----------------------------------------------
    Local Mods | svn_wc_merge uses diff3, | svn_wc_merge     |
               | possibly makes backups & | makes backups,   |
               | marks file as conflicted.| marks conflicted |
               -----------------------------------------------
    No Mods    |        Just overwrite working file.         |
               |                                             |
               -----------------------------------------------

   So the first thing we do is figure out where we are in the
   matrix. */
  if (new_text_path)
    {
      if (! is_locally_modified && ! is_replaced)
        {
          /* If there are no local mods, who cares whether it's a text
             or binary file!  Just write a log command to overwrite
             any working file with the new text-base.  If newline
             conversion or keyword substitution is activated, this
             will happen as well during the copy. */
          SVN_ERR(svn_wc__loggy_copy(&log_accum, NULL, adm_access,
                                     svn_wc__copy_translate,
                                     tmp_txtb, base_name, FALSE, pool));
        }
      else   /* working file is locally modified... */
        {
          svn_node_kind_t wfile_kind = svn_node_unknown;
          
          SVN_ERR(svn_io_check_path(file_path, &wfile_kind, pool));
          if (wfile_kind == svn_node_none) /* working file is missing?! */
            {
              /* Just copy the new text-base to the file. */
              SVN_ERR(svn_wc__loggy_copy(&log_accum, NULL, adm_access,
                                         svn_wc__copy_translate,
                                         tmp_txtb, base_name, FALSE, pool));
            }
          else  /* working file exists, and has local mods.*/
            {                  
              /* Now we need to let loose svn_wc_merge2() to merge the
                 textual changes into the working file. */
              const char *oldrev_str, *newrev_str;
              const svn_wc_entry_t *e;
              const char *base;
              
              /* Create strings representing the revisions of the
                 old and new text-bases. */
              SVN_ERR(svn_wc_entry(&e, file_path, adm_access, FALSE, pool));
              if (! e)
                return svn_error_createf(
                  SVN_ERR_UNVERSIONED_RESOURCE, NULL,
                  _("'%s' is not under version control"),
                  svn_path_local_style(file_path, pool));

              oldrev_str = apr_psprintf(pool, ".r%ld",
                                        e->revision);
              newrev_str = apr_psprintf(pool, ".r%ld",
                                        new_revision);

              /* Merge the changes from the old-textbase (TXTB) to
                 new-textbase (TMP_TXTB) into the file we're
                 updating (BASE_NAME). Append commands to update the
                 working copy to LOG_ACCUM. */
              base = svn_wc_adm_access_path(adm_access);
              SVN_ERR(svn_wc__merge_internal
                      (&log_accum, &merge_outcome,
                       svn_path_join(base, txtb, pool),
                       svn_path_join(base, tmp_txtb, pool),
                       svn_path_join(base, base_name, pool),
                       adm_access,
                       oldrev_str, newrev_str, ".mine",
                       FALSE, diff3_cmd, NULL,
                       pool));

            } /* end: working file exists and has mods */
        } /* end: working file has mods */
    }  /* end:  "textual" merging process */
  else if (*lock_state == svn_wc_notify_lock_state_unlocked)
    /* If a lock was removed and we didn't update the text contents, we
       might need to set the file read-only. */
    SVN_ERR(svn_wc__loggy_maybe_set_readonly(&log_accum, adm_access,
                                             base_name, pool));

  if (new_text_path)
    {
      /* Write out log commands to set up the new text base and its
         checksum. */

      SVN_ERR(svn_wc__loggy_move(&log_accum, NULL,
                                 adm_access, tmp_txtb, txtb, FALSE, pool));

      SVN_ERR(svn_wc__loggy_set_readonly(&log_accum, adm_access,
                                         txtb, pool));

      /* If the file is replaced don't write the checksum.  Checksum is blank
       * on replaced files */
      if (!is_replaced)
        {
          svn_wc_entry_t tmp_entry;
          unsigned char digest[APR_MD5_DIGESTSIZE];
          SVN_ERR(svn_io_file_checksum(digest, new_text_path, pool));

          tmp_entry.checksum = svn_md5_digest_to_cstring(digest, pool);

          SVN_ERR(svn_wc__loggy_entry_modify(&log_accum, adm_access,
                                             base_name, &tmp_entry,
                                             SVN_WC__ENTRY_MODIFY_CHECKSUM,
                                             pool));
        }
    }

  /* Log commands to handle text-timestamp */
  if (!is_locally_modified)
    {
      if (timestamp_string)
        /* Adjust working copy file */
        SVN_ERR(svn_wc__loggy_set_timestamp(&log_accum, adm_access,
                                            base_name, timestamp_string,
                                            pool));

      if (new_text_path || magic_props_changed)
        /* Adjust entries file to match working file */
        SVN_ERR(svn_wc__loggy_set_entry_timestamp_from_wc
                (&log_accum, adm_access,
                 base_name, SVN_WC__ENTRY_ATTR_TEXT_TIME, pool));
    }


  if (content_state)
    {
      /* Initialize the state of our returned value. */
      *content_state = svn_wc_notify_state_unknown;
      
      /* This is kind of interesting.  Even if no new text was
         installed (i.e., new_text_path was null), we could still
         report a pre-existing conflict state.  Say a file, already
         in a state of textual conflict, receives prop mods during an
         update.  Then we'll notify that it has text conflicts.  This
         seems okay to me.  I guess.  I dunno.  You? */

      if (merge_outcome == svn_wc_merge_conflict)
        *content_state = svn_wc_notify_state_conflicted;
      else if (new_text_path)
        {
          if (is_locally_modified)
            *content_state = svn_wc_notify_state_merged;
          else
            *content_state = svn_wc_notify_state_changed;
        }
      else
        *content_state = svn_wc_notify_state_unchanged;
    }

  return SVN_NO_ERROR;
}


/* Mostly a wrapper around merge_file. */
static svn_error_t *
close_file(void *file_baton,
           const char *text_checksum,
           apr_pool_t *pool)
{
  struct file_baton *fb = file_baton;
  struct edit_baton *eb = fb->edit_baton;
  const char *parent_path;
  apr_array_header_t *propchanges = NULL;
  svn_wc_notify_state_t content_state, prop_state;
  svn_wc_notify_lock_state_t lock_state;
  svn_wc_adm_access_t *adm_access;
  svn_stringbuf_t *log_accum;

  /* window-handler assembles new pristine text in .svn/tmp/text-base/  */
  if (fb->text_changed && text_checksum)
    {
      const char *real_sum = svn_md5_digest_to_cstring(fb->digest, pool);
          
      if (real_sum && (strcmp(text_checksum, real_sum) != 0))
        return svn_error_createf
          (SVN_ERR_CHECKSUM_MISMATCH, NULL,
           _("Checksum mismatch for '%s'; expected: '%s', actual: '%s'"),
           svn_path_local_style(fb->path, pool), text_checksum, real_sum);
    }

  if (fb->prop_changed)
    propchanges = fb->propchanges;

  parent_path = svn_path_dirname(fb->path, pool);
    
  SVN_ERR(svn_wc_adm_retrieve(&adm_access, eb->adm_access, 
                              parent_path, pool));

  /* Accumulate log commands in this buffer until we're ready to
     write the log. */
  log_accum = svn_stringbuf_create("", pool);

  SVN_ERR(merge_file(log_accum,
                     &content_state,
                     &prop_state,
                     &lock_state,
                     adm_access,
                     fb->path,
                     (*eb->target_revision),
                     fb->text_changed,
                     propchanges,
                     fb->new_URL,
                     eb->diff3_cmd,
                     fb->last_changed_date,
                     pool));

  /* Write our accumulation of log entries into a log file */
  SVN_ERR(svn_wc__write_log(adm_access, fb->dir_baton->log_number,
                            log_accum, pool));

  fb->dir_baton->log_number++;

  /* We have one less referrer to the directory's bump information. */
  SVN_ERR(maybe_bump_dir_info(eb, fb->bump_info, pool));

  if (((content_state != svn_wc_notify_state_unchanged) ||
       (prop_state != svn_wc_notify_state_unchanged) ||
       (lock_state != svn_wc_notify_lock_state_unchanged)) &&
      eb->notify_func)
    {
      svn_wc_notify_t *notify
        = svn_wc_create_notify(fb->path,
                               fb->added ? svn_wc_notify_update_add 
                               : svn_wc_notify_update_update, pool);
      notify->kind = svn_node_file;
      notify->content_state = content_state;
      notify->prop_state = prop_state;
      notify->lock_state = lock_state;
      /* ### use merge_file() mimetype here */
      (*eb->notify_func)(eb->notify_baton, notify, pool);
    }
  return SVN_NO_ERROR;  
}


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
    SVN_ERR(do_entry_deletion(eb, eb->anchor, eb->target, &log_number,
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
                                      eb->recurse,
                                      eb->switch_url,
                                      eb->repos,
                                      *(eb->target_revision),
                                      eb->notify_func,
                                      eb->notify_baton,
                                      TRUE,
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
  struct edit_baton *eb;
  apr_pool_t *subpool = svn_pool_create(pool);
  svn_delta_editor_t *tree_editor = svn_delta_default_editor(subpool);
  const svn_wc_entry_t *entry;

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
  eb->pool            = subpool;
  eb->use_commit_times= use_commit_times;
  eb->target_revision = target_revision;
  eb->switch_url      = switch_url;
  eb->repos           = entry ? entry->repos : NULL;
  eb->adm_access      = adm_access;
  eb->anchor          = anchor;
  eb->target          = target;
  eb->recurse         = recurse;
  eb->notify_func     = notify_func;
  eb->notify_baton    = notify_baton;
  eb->traversal_info  = traversal_info;
  eb->diff3_cmd       = diff3_cmd;
  eb->cancel_func     = cancel_func;
  eb->cancel_baton    = cancel_baton;

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

  SVN_ERR(svn_delta_get_cancellation_editor(cancel_func,
                                            cancel_baton,
                                            tree_editor,
                                            eb,
                                            editor,
                                            edit_baton,
                                            pool));

  return SVN_NO_ERROR;
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
  return make_editor(target_revision, anchor, svn_wc_adm_access_path(anchor),
                     target, use_commit_times, NULL, recurse, notify_func, 
                     notify_baton, cancel_func, cancel_baton, diff3_cmd,
                     editor, edit_baton, traversal_info, pool);
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
  
  return svn_wc_get_update_editor2(target_revision, anchor, target,
                                   use_commit_times, recurse,
                                   svn_wc__compat_call_notify_func, nb,
                                   cancel_func, cancel_baton, diff3_cmd,
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

  return make_editor(target_revision, anchor, svn_wc_adm_access_path(anchor),
                     target, use_commit_times, switch_url, recurse, 
                     notify_func, notify_baton, cancel_func, cancel_baton, 
                     diff3_cmd, editor, edit_baton, traversal_info, pool);
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

  return svn_wc_get_switch_editor2(target_revision, anchor, target,
                                   switch_url, use_commit_times, recurse,
                                   svn_wc__compat_call_notify_func, nb,
                                   cancel_func, cancel_baton, diff3_cmd,
                                   editor, edit_baton, traversal_info, pool);
}
                                    

svn_wc_traversal_info_t *
svn_wc_init_traversal_info(apr_pool_t *pool)
{
  svn_wc_traversal_info_t *ti = apr_palloc(pool, sizeof(*ti));
  
  ti->pool           = pool;
  ti->externals_old  = apr_hash_make(pool);
  ti->externals_new  = apr_hash_make(pool);
  
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
  const char *base_name = svn_path_basename(dst_path, pool);

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
  SVN_ERR(svn_wc__install_props(&log_accum, adm_access, base_name,
                                new_base_props,
                                new_props ? new_props : new_base_props,
                                TRUE, pool));

  /* Install the entry props. */
  SVN_ERR(accumulate_entry_props(log_accum, NULL,
                                 adm_access, base_name,
                                 entry_props, pool));

  /* This writes a whole bunch of log commands to install wcprops.  */
  SVN_ERR(accumulate_wcprops(log_accum, adm_access,
                             base_name, wc_props, pool));

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
  const char *local_tmp_text_base_path =
    svn_path_is_child(adm_path, tmp_text_base_path, pool);
  const char *text_base_path =
    svn_wc__text_base_path(dst_path, FALSE, pool);
  const char *local_text_base_path =
    svn_path_is_child(adm_path, text_base_path, pool);
  const svn_wc_entry_t *ent;
  const svn_wc_entry_t *dst_entry;
  svn_stringbuf_t *log_accum;
  const char *dir_name, *base_name;

  svn_path_split(dst_path, &dir_name, &base_name, pool);

  /* Fabricate the anticipated new URL of the target and check the
     copyfrom URL to be in the same repository. */
  {
    SVN_ERR(svn_wc_entry(&ent, dir_name, adm_access, FALSE, pool));
    if (! ent)
      return svn_error_createf(SVN_ERR_UNVERSIONED_RESOURCE, NULL,
                               _("'%s' is not under version control"),
                               svn_path_local_style(dir_name, pool));

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
      const char *full_path = svn_wc_adm_access_path(adm_access);
      const char *dst_rtext = svn_wc__text_revert_path(base_name, FALSE,
                                                       pool);
      const char *dst_txtb = svn_wc__text_base_path(base_name, FALSE, pool);
      const char *dst_rprop;
      const char *dst_bprop;
      svn_node_kind_t kind;

      SVN_ERR(svn_wc__prop_revert_path(&dst_rprop, base_name,
                                       svn_node_file, FALSE, pool));

      SVN_ERR(svn_wc__prop_base_path(&dst_bprop, base_name,
                                     svn_node_file, FALSE, pool));

      SVN_ERR(svn_wc__loggy_move(&log_accum, NULL,
                                 adm_access, dst_txtb, dst_rtext,
                                 FALSE, pool));

      /* If prop base exist, copy it to revert base. */
      SVN_ERR(svn_io_check_path(svn_path_join(full_path, dst_bprop, pool),
                                &kind, pool));
      if (kind == svn_node_file)
        SVN_ERR(svn_wc__loggy_move(&log_accum, NULL,
                                   adm_access, dst_bprop, dst_rprop,
                                   FALSE, pool));
    }
  
  /* Schedule this for addition first, before the entry exists.
   * Otherwise we'll get bounced out with an error about scheduling
   * an already-versioned item for addition.
   */
  {
    svn_wc_entry_t tmp_entry;
    apr_uint32_t modify_flags = SVN_WC__ENTRY_MODIFY_SCHEDULE;

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
                                       base_name, &tmp_entry,
                                       modify_flags, pool));
  }

  /* Set the new revision number and URL in the entry and clean up some other
     fields. */
  SVN_ERR(tweak_entry(log_accum, adm_access, base_name,
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
      const char *local_tmp_text_path;

      /* Move new text to temporary file in adm_access. */
      SVN_ERR(svn_wc_create_tmp_file2(NULL, &tmp_text_path, adm_path,
                                      svn_io_file_del_none, pool));

      SVN_ERR(svn_io_file_move(new_text_path, tmp_text_path, pool));

      local_tmp_text_path = svn_path_is_child(adm_path, tmp_text_path, pool);
      /* Translate/rename new temporary text file to working text. */
      if (svn_wc__has_special_property(new_base_props))
        {
          SVN_ERR(svn_wc__loggy_copy(&log_accum, NULL, adm_access,
                                     svn_wc__copy_translate_special_only,
                                     local_tmp_text_path,
                                     base_name, FALSE, pool));
          /* Remove the copy-source, making it look like a move */
          SVN_ERR(svn_wc__loggy_remove(&log_accum, adm_access,
                                       local_tmp_text_path, pool));
        }
      else
        SVN_ERR(svn_wc__loggy_move(&log_accum, NULL, adm_access,
                                   local_tmp_text_path, base_name,
                                   FALSE, pool));
      SVN_ERR(svn_wc__loggy_maybe_set_readonly(&log_accum, adm_access,
                                               base_name, pool));
    }
  else
    {
      /* No working file provided by the caller, copy and translate the
         text base. */
      SVN_ERR(svn_wc__loggy_copy(&log_accum, NULL, adm_access,
                                 svn_wc__copy_translate,
                                 local_tmp_text_base_path, base_name, FALSE,
                                 pool));
      SVN_ERR(svn_wc__loggy_set_entry_timestamp_from_wc
              (&log_accum, adm_access,
               base_name, SVN_WC__ENTRY_ATTR_TEXT_TIME, pool));
    }

  /* Install new text base. */
  {
    unsigned char digest[APR_MD5_DIGESTSIZE];
    svn_wc_entry_t tmp_entry;
      
    /* Write out log commands to set up the new text base and its
       checksum. */
    SVN_ERR(svn_wc__loggy_move(&log_accum, NULL,
                               adm_access, local_tmp_text_base_path,
                               local_text_base_path, FALSE, pool));
    SVN_ERR(svn_wc__loggy_set_readonly(&log_accum, adm_access,
                                       local_text_base_path, pool));

    SVN_ERR(svn_io_file_checksum(digest, tmp_text_base_path, pool));

    tmp_entry.checksum = svn_md5_digest_to_cstring(digest, pool);
    SVN_ERR(svn_wc__loggy_entry_modify(&log_accum, adm_access,
                                       base_name, &tmp_entry,
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
