/*
 * update_editor.c :  main editor for checkouts and updates
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



#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include <apr_pools.h>
#include <apr_hash.h>
#include <apr_tables.h>
#include <apr_file_io.h>
#include <apr_strings.h>
#include <apr_thread_proc.h>

#include "svn_types.h"
#include "svn_pools.h"
#include "svn_delta.h"
#include "svn_string.h"
#include "svn_path.h"
#include "svn_xml.h"
#include "svn_error.h"
#include "svn_io.h"
#include "svn_hash.h"
#include "svn_wc.h"
#include "svn_private_config.h"
#include "svn_time.h"

#include "wc.h"
#include "log.h"
#include "adm_files.h"
#include "adm_ops.h"
#include "entries.h"
#include "props.h"


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

  /* The revision we're targeting...or something like that. */
  svn_revnum_t target_revision;

  /* Whether this edit will descend into subdirs */
  svn_boolean_t recurse;

  /* These used only in checkouts. */
  svn_boolean_t is_checkout;
  const char *ancestor_url;

  /* Non-null if this is a 'switch' operation. */
  const char *switch_url;

  /* Object for gathering info to be accessed after the edit is
     complete. */
  svn_wc_traversal_info_t *traversal_info;

  /* This editor sends back notifications as it edits. */
  svn_wc_notify_func_t notify_func;
  void *notify_baton;

  apr_pool_t *pool;
};


struct dir_baton
{
  /* The path to this directory. */
  const char *path;

  /* Basename of this directory. */
  const char *name;

  /* The repository URL this directory corresponds to. */
  const char *URL;

  /* The global edit baton. */
  struct edit_baton *edit_baton;

  /* Baton for this directory's parent, or NULL if this is the root
     directory. */
  struct dir_baton *parent_baton;

  /* Gets set iff there's a change to this directory's properties, to
     guide us when syncing adm files later. */
  svn_boolean_t prop_changed;

  /* Gets set iff this is a new directory that is not yet versioned and not
     yet in the parent's list of entries */
  svn_boolean_t added;

  /* An array of svn_prop_t structures, representing all the property
     changes to be applied to this file. */
  apr_array_header_t *propchanges;

  /* The bump information for this directory. */
  struct bump_dir_info *bump_info;

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

  /* was this directory added? (if so, we'll add it to the parent dir
     at bump time) */
  svn_boolean_t added;
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


/* Return a new dir_baton to represent NAME (a subdirectory of
   PARENT_BATON).  If PATH is NULL, this is the root directory of the
   edit. */
static struct dir_baton *
make_dir_baton (const char *path,
                struct edit_baton *eb,
                struct dir_baton *pb,
                svn_boolean_t added,
                apr_pool_t *pool)
{
  struct dir_baton *d = apr_pcalloc (pool, sizeof (*d));
  const char *URL;
  svn_error_t *err;
  const svn_wc_entry_t *entry;
  struct bump_dir_info *bdi;
  
  /* Don't do this.  Just do NOT do this to me. */
  if (pb && (! path))
    abort();

  /* Construct the PATH and baseNAME of this directory. */
  d->path = apr_pstrdup (pool, eb->anchor);
  if (path)
    {
      d->path = svn_path_join (d->path, path, pool);
      d->name = svn_path_basename (path, pool);
    }
  else
    d->name = NULL;

  /* Figure out the URL for this directory. */
  if (eb->is_checkout)
    {
      /* for checkouts, telescope the URL normally.  no such thing as
         disjoint urls.   */
      if (pb)
        URL = svn_path_join (pb->URL, d->name, pool);
      else
        URL = apr_pstrdup (pool, eb->ancestor_url);
    }
  else 
    {
      /* For updates, look in the 'entries' file */
      svn_wc_adm_access_t *adm_access;
      err = svn_wc_adm_retrieve (&adm_access, eb->adm_access, d->path, pool);
      if (! err)
        err = svn_wc_entry (&entry, d->path, adm_access, FALSE, pool);
      if (err || (! entry) || (! entry->url))
        {
          if (err)
            svn_error_clear_all (err);
          URL = "";
        }
      else
        URL = entry->url;
    }

  /* the bump information lives in the edit pool */
  bdi = apr_palloc (eb->pool, sizeof (*bdi));
  bdi->parent = pb ? pb->bump_info : NULL;
  bdi->ref_count = 1;   /* we refer to it */
  bdi->path = apr_pstrdup (eb->pool, d->path);    /* full path */
  bdi->added = added;

  /* the parent's bump info has one more referer */
  if (pb)
    ++bdi->parent->ref_count;

  d->edit_baton   = eb;
  d->parent_baton = pb;
  d->pool         = pool;
  d->propchanges  = apr_array_make (pool, 1, sizeof (svn_prop_t));
  d->added        = added;
  d->URL          = URL;
  d->bump_info    = bdi;

  return d;
}



/* Decrement the bump_dir_info's reference count. If it hits zero,
   then this directory is "done". This means it is safe to bump the
   revision of the directory, and to add it as a child to its parent
   directory.

   In addition, when the directory is "done", we loop onto the parent's
   bump information to possibly mark it as done, too.
*/
static svn_error_t *
maybe_bump_dir_revision (struct edit_baton *eb,
                         struct bump_dir_info *bdi,
                         apr_pool_t *pool)
{
  /* Keep moving up the tree of directories until we run out of parents,
     or a directory is not yet "done".  */
  for ( ; bdi != NULL; bdi = bdi->parent)
    {
      svn_wc_entry_t tmp_entry;

      tmp_entry.revision = eb->target_revision;
      tmp_entry.kind = svn_node_dir;
      tmp_entry.deleted = FALSE;

      if (--bdi->ref_count > 0)
        return SVN_NO_ERROR;    /* directory isn't done yet */


      /* Bump this dir to the new revision if this directory is beneath
         the target of an update, or unconditionally if this is a
         checkout. */
      if (eb->is_checkout || bdi->parent)
        {
          svn_wc_adm_access_t *adm_access;
          SVN_ERR (svn_wc_adm_retrieve (&adm_access, eb->adm_access, bdi->path,
                                        pool));

          SVN_ERR (svn_wc__entry_modify (adm_access, NULL, &tmp_entry,
                                         SVN_WC__ENTRY_MODIFY_REVISION, pool));
        }

      /* If this directory is newly added, then it probably doesn't
         have an entry in the parent's list of entries (unless the
         parent contains a phantom "deleted" entry). The directory is
         now complete, and can be added. */
      if (bdi->added && bdi->parent)
        {
          const char *name = svn_path_basename (bdi->path, pool);
          svn_wc_adm_access_t *adm_access;
          SVN_ERR (svn_wc_adm_retrieve (&adm_access, eb->adm_access,
                                        bdi->parent->path, pool));

          SVN_ERR (svn_wc__entry_modify (adm_access, name, &tmp_entry,
                                         (SVN_WC__ENTRY_MODIFY_KIND
                                          | SVN_WC__ENTRY_MODIFY_DELETED),
                                         pool));
        }
    }
  /* we exited the for loop because there are no more parents */

  return SVN_NO_ERROR;
}



struct file_baton
{
  /* The global edit baton. */
  struct edit_baton *edit_baton;

  /* Pool specific to this file_baton. */
  apr_pool_t *pool;

  /* Name of this file (its entry in the directory). */
  const char *name;

  /* Path to this file, either abs or relative to the change-root. */
  const char *path;

  /* The repository URL this directory corresponds to. */
  const char *URL;

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

  /* Bump information for the directory this file lives in */
  struct bump_dir_info *bump_info;
};


/* Make a new file baton in the provided POOL, with PB as the parent baton.
   PATH is relative to the root of the edit. */
static struct file_baton *
make_file_baton (struct dir_baton *pb,
                 const char *path,
                 svn_boolean_t adding,
                 apr_pool_t *pool)
{
  struct file_baton *f = apr_pcalloc (pool, sizeof (*f));
  const char *URL;
  svn_error_t *err;
  const svn_wc_entry_t *entry;

  /* I rather need this information, yes. */
  if (! path)
    abort();

  /* Make the file's on-disk name. */
  f->path = svn_path_join (pb->edit_baton->anchor, path, pool);
  f->name = svn_path_basename (path, pool);

  /* Figure out the URL for this file. */
  if (pb->edit_baton->is_checkout)
    {
      /* for checkouts, telescope the URL normally.  no such thing as
         disjoint urls.   */
      URL = svn_path_join (pb->URL, f->name, pool);
    }
  else 
    {
      /* For updates, look in the 'entries' file */
      svn_wc_adm_access_t *adm_access;
      err = svn_wc_adm_retrieve (&adm_access, pb->edit_baton->adm_access,
                                 pb->path, pool);
      if (! err)
        err = svn_wc_entry (&entry, f->path, adm_access, FALSE, pool);
      if (err || (! entry) || (! entry->url))
        {
          if (err)
            svn_error_clear_all (err);
          URL = "";
        }
      else
        URL = entry->url;
    }

  f->pool         = pool;
  f->edit_baton   = pb->edit_baton;
  f->propchanges  = apr_array_make (pool, 1, sizeof (svn_prop_t));
  f->URL          = URL;
  f->bump_info    = pb->bump_info;
  f->added        = adding;

  /* the directory's bump info has one more referer now */
  ++f->bump_info->ref_count;

  return f;
}



/*** Helpers for the editor callbacks. ***/

static svn_error_t *
window_handler (svn_txdelta_window_t *window, void *baton)
{
  struct handler_baton *hb = baton;
  struct file_baton *fb = hb->fb;
  svn_error_t *err = NULL, *err2 = NULL;

  /* Apply this window.  We may be done at that point.  */
  err = hb->apply_handler (window, hb->apply_baton);
  if (window != NULL && err == SVN_NO_ERROR)
    return err;

  /* Either we're done (window is NULL) or we had an error.  In either
     case, clean up the handler.  */
  if ((! fb->edit_baton->is_checkout) && hb->source)
    {
      err2 = svn_wc__close_text_base (hb->source, fb->path, 0, fb->pool);
      if (err2 != SVN_NO_ERROR && err == SVN_NO_ERROR)
        err = err2;
    }
  err2 = svn_wc__close_text_base (hb->dest, fb->path, 0, fb->pool);
  if (err2 != SVN_NO_ERROR && err == SVN_NO_ERROR)
    err = err2;
  svn_pool_destroy (hb->pool);

  if (err != SVN_NO_ERROR)
    {
      /* We failed to apply the patch; clean up the temporary file.  */
      const char *tmppath = svn_wc__text_base_path (fb->path, TRUE, fb->pool);
      apr_file_remove (tmppath, fb->pool);
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
prep_directory (struct dir_baton *db,
                const char *ancestor_url,
                svn_revnum_t ancestor_revision,
                apr_pool_t *pool)
{
  /* Make sure the directory exists. */
  SVN_ERR (svn_wc__ensure_directory (db->path, pool));

  /* Make sure it's the right working copy, either by creating it so,
     or by checking that it is so already. */
  SVN_ERR (svn_wc__ensure_adm (db->path, ancestor_url, ancestor_revision,
                               pool));

  if (! db->edit_baton->adm_access
      || strcmp (svn_wc_adm_access_path (db->edit_baton->adm_access),
                 db->path))
    {
      svn_wc_adm_access_t *adm_access;
      apr_pool_t *adm_access_pool
        = db->edit_baton->adm_access
        ? svn_wc_adm_access_pool (db->edit_baton->adm_access)
        : db->edit_baton->pool;

      SVN_ERR (svn_wc_adm_open (&adm_access, db->edit_baton->adm_access,
                                db->path, TRUE, FALSE, adm_access_pool));
      if (!db->edit_baton->adm_access)
        db->edit_baton->adm_access = adm_access;
    }
  
  return SVN_NO_ERROR;
}



/*** The callbacks we'll plug into an svn_delta_editor_t structure. ***/

static svn_error_t *
set_target_revision (void *edit_baton, 
                     svn_revnum_t target_revision,
                     apr_pool_t *pool)
{
  struct edit_baton *eb = edit_baton;

  /* Stashing a target_revision in the baton */
  eb->target_revision = target_revision;
  return SVN_NO_ERROR;
}


static svn_error_t *
open_root (void *edit_baton,
           svn_revnum_t base_revision, /* This is ignored in co */
           apr_pool_t *pool,
           void **dir_baton)
{
  struct edit_baton *eb = edit_baton;
  struct dir_baton *d;

  *dir_baton = d = make_dir_baton (NULL, eb, NULL, eb->is_checkout, pool);
  if (eb->is_checkout)
    SVN_ERR (prep_directory (d, eb->ancestor_url, eb->target_revision, pool));

  return SVN_NO_ERROR;
}


static svn_error_t *
delete_entry (const char *path, 
              svn_revnum_t revision, 
              void *parent_baton,
              apr_pool_t *pool)
{
  struct dir_baton *pb = parent_baton;
  apr_status_t apr_err;
  apr_file_t *log_fp = NULL;
  const char *base_name;
  svn_wc_adm_access_t *adm_access;
  svn_stringbuf_t *log_item = svn_stringbuf_create ("", pool);

  SVN_ERR (svn_wc_adm_retrieve (&adm_access, pb->edit_baton->adm_access,
                                pb->path, pool));
  SVN_ERR (svn_wc__open_adm_file (&log_fp,
                                  pb->path,
                                  SVN_WC__ADM_LOG,
                                  (APR_WRITE | APR_CREATE), /* not excl */
                                  pool));

  /* Here's the deal: in the new editor interface, PATH is a full path
     below the editor's anchor, and pb->path is the parent directory.
     That's all fine and well, but our log-system requires that all
     log commands talk *only* about paths relative (and below)
     pb->path, i.e. where the log is being executed.  */
  base_name = svn_path_basename (path, pool);

  svn_xml_make_open_tag (&log_item,
                         pool,
                         svn_xml_self_closing,
                         SVN_WC__LOG_DELETE_ENTRY,
                         SVN_WC__LOG_ATTR_NAME,
                         base_name,
                         NULL);

  apr_err = apr_file_write_full (log_fp, log_item->data, log_item->len, NULL);
  if (apr_err)
    {
      apr_file_close (log_fp);
      return svn_error_createf (apr_err, 0, NULL, pool,
                                "delete error writing %s's log file",
                                pb->path);
    }

  SVN_ERR (svn_wc__close_adm_file (log_fp,
                                   pb->path,
                                   SVN_WC__ADM_LOG,
                                   TRUE, /* sync */
                                   pool));
    
  SVN_ERR (svn_wc__run_log (adm_access, pool));

  /* The passed-in `path' is relative to the anchor of the edit, so if
   * the operation was invoked on something other than ".", then
   * `path' will be wrong for purposes of notification.  However, we
   * can always count on the pb->path being the parent of base_name,
   * so we just join them together to get a good notification path.
   */
  if (pb->edit_baton->notify_func)
    (*pb->edit_baton->notify_func) (pb->edit_baton->notify_baton,
                                    svn_path_join (pb->path, base_name, pool),
                                    svn_wc_notify_update_delete,
                                    svn_node_unknown,
                                    NULL,
                                    svn_wc_notify_state_unknown,
                                    svn_wc_notify_state_unknown,
                                    SVN_INVALID_REVNUM);

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
  struct dir_baton *db = make_dir_baton (path, pb->edit_baton, pb, TRUE, pool);
  svn_node_kind_t kind;

  /* Semantic check.  Either both "copyfrom" args are valid, or they're
     NULL and SVN_INVALID_REVNUM.  A mixture is illegal semantics. */
  if ((copyfrom_path && (! SVN_IS_VALID_REVNUM (copyfrom_revision)))
      || ((! copyfrom_path) && (SVN_IS_VALID_REVNUM (copyfrom_revision))))
    abort();

  /* The directory may exist if this is a checkout, otherwise there should
     be nothing with this name. */
  SVN_ERR (svn_io_check_path (db->path, &kind, db->pool));
  if (kind != svn_node_none
      && !(pb->edit_baton->is_checkout && kind == svn_node_dir))
    return svn_error_createf
      (SVN_ERR_WC_OBSTRUCTED_UPDATE, 0, NULL, pool,
       "failed to add directory '%s': object of the same name already exists",
       db->path);

  /* Either we got real copyfrom args... */
  if (copyfrom_path || SVN_IS_VALID_REVNUM (copyfrom_revision))
    {
      /* ### todo: for now, this editor doesn't know how to deal with
         copyfrom args.  Someday it will interpet them as an update
         optimization, and actually copy one part of the wc to another.
         Then it will recursively "normalize" all the ancestry in the
         copied tree.  Someday! */      
      return svn_error_createf
        (SVN_ERR_UNSUPPORTED_FEATURE, 0, NULL, pool,
         "failed to add directory '%s': copyfrom args not yet supported",
         db->path);
    }
  else  /* ...or we got invalid copyfrom args. */
    {
      /* If the copyfrom args are both invalid, inherit the URL from the
         parent, and make the revision equal to the global target
         revision. */
      svn_wc_adm_access_t *adm_access;
      const svn_wc_entry_t *parent_entry;

      SVN_ERR (svn_wc_adm_retrieve (&adm_access, pb->edit_baton->adm_access,
                                    pb->path, db->pool));
      SVN_ERR (svn_wc_entry (&parent_entry, pb->path, adm_access, FALSE,
                             db->pool));
      copyfrom_path = svn_path_url_add_component (parent_entry->url, 
                                                  db->name,
                                                  db->pool);
      copyfrom_revision = pb->edit_baton->target_revision;      
    }

  /* Create dir (if it doesn't yet exist), make sure it's formatted
     with an administrative subdir.   */
  SVN_ERR (prep_directory (db, copyfrom_path, copyfrom_revision, db->pool));

  *child_baton = db;

  if (db->edit_baton->notify_func)
    (*db->edit_baton->notify_func) (db->edit_baton->notify_baton,
                                    db->path,
                                    svn_wc_notify_update_add,
                                    svn_node_dir,
                                    NULL,
                                    svn_wc_notify_state_unknown,
                                    svn_wc_notify_state_unknown,
                                    SVN_INVALID_REVNUM);

  return SVN_NO_ERROR;
}


static svn_error_t *
open_directory (const char *path,
                void *parent_baton,
                svn_revnum_t base_revision,
                apr_pool_t *pool,
                void **child_baton)
{
  struct dir_baton *parent_dir_baton = parent_baton;

  /* kff todo: check that the dir exists locally, find it somewhere if
     its not there?  Yes, all this and more...  And ancestor_url and
     ancestor_revision need to get used. */

  struct dir_baton *this_dir_baton
    = make_dir_baton (path,
                      parent_dir_baton->edit_baton,
                      parent_dir_baton,
                      FALSE,
                      pool);

  *child_baton = this_dir_baton;

  return SVN_NO_ERROR;
}


static svn_error_t *
change_dir_prop (void *dir_baton,
                 const char *name,
                 const svn_string_t *value,
                 apr_pool_t *pool)
{
  svn_prop_t *propchange;
  struct dir_baton *db = dir_baton;

  /* If this is a 'wc' prop, store it in the administrative area and
     get on with life.  It's not a regular versioned property. */
  if (svn_wc_is_wc_prop (name))
    return svn_wc__wcprop_set (name, value, db->path, pool);
  
  /* If this is an 'entry' prop, store it in the entries file and get
     on with life.  It's not a regular user property. */
  else if (svn_wc_is_entry_prop (name))
    {
      svn_wc_entry_t entry;
      apr_uint32_t modify_flags = 0;

      entry.kind = svn_node_dir;

      /* a NULL-valued entry prop means the information was not
         available.  We don't remove this field from the entries file;
         we have convention just leave it empty. */
      if ((! strcmp (name, SVN_PROP_ENTRY_COMMITTED_REV)) && value)
        {
          entry.cmt_rev = SVN_STR_TO_REV (value->data);
          modify_flags = SVN_WC__ENTRY_MODIFY_CMT_REV;
        }
      else if ((! strcmp (name, SVN_PROP_ENTRY_COMMITTED_DATE)) && value)
        {
          SVN_ERR (svn_time_from_nts (&entry.cmt_date, value->data, pool));
          modify_flags = SVN_WC__ENTRY_MODIFY_CMT_DATE;
        }
      else if ((! strcmp (name, SVN_PROP_ENTRY_LAST_AUTHOR)) && value)
        {
          entry.cmt_author = apr_pstrdup (pool, value->data);
          modify_flags = SVN_WC__ENTRY_MODIFY_CMT_AUTHOR;
        }

      if (modify_flags)
        {
          svn_wc_adm_access_t *adm_access;
          SVN_ERR (svn_wc_adm_retrieve (&adm_access, db->edit_baton->adm_access,
                                        db->path, pool));
          return svn_wc__entry_modify (adm_access, NULL, &entry,
                                       modify_flags, pool);
        }
    }

  /* Else, it's a real ("normal") property... */

  /* Push a new propchange to the directory baton's array of propchanges */
  propchange = apr_array_push (db->propchanges);
  propchange->name = apr_pstrdup (db->pool, name);
  propchange->value = value ? svn_string_dup (value, db->pool) : NULL;

  /* Let close_dir() know that propchanges are waiting to be
     applied. */
  db->prop_changed = 1;

  return SVN_NO_ERROR;
}



/* If any of the svn_prop_t objects in PROPCHANGES represents a change
   to the SVN_PROP_EXTERNALS property, return that change, else return
   null.  If PROPCHANGES contains more than one such change, return
   the first. */
static const svn_prop_t *
externals_prop_changed (apr_array_header_t *propchanges)
{
  int i;

  for (i = 0; i < propchanges->nelts; i++)
    {
      const svn_prop_t *p = &(APR_ARRAY_IDX(propchanges, i, svn_prop_t));
      if (strcmp (p->name, SVN_PROP_EXTERNALS) == 0)
        return p;
    }

  return NULL;
}


static svn_error_t *
close_directory (void *dir_baton,
                 apr_pool_t *pool)
{
  struct dir_baton *db = dir_baton;
  svn_wc_notify_state_t prop_state = svn_wc_notify_state_unknown;
  apr_hash_t *prop_conflicts;

  /* If this directory has property changes stored up, now is the time
     to deal with them. */
  if (db->prop_changed)
    {
      svn_boolean_t prop_modified;
      apr_status_t apr_err;
      char *revision_str;
      svn_wc_adm_access_t *adm_access;
      apr_file_t *log_fp = NULL;

      /* to hold log messages: */
      svn_stringbuf_t *entry_accum = svn_stringbuf_create ("", db->pool);

      SVN_ERR (svn_wc_adm_retrieve (&adm_access, db->edit_baton->adm_access,
                                    db->path, db->pool));
      
      /* Open log file */
      SVN_ERR (svn_wc__open_adm_file (&log_fp,
                                      db->path,
                                      SVN_WC__ADM_LOG,
                                      (APR_WRITE | APR_CREATE), /* not excl */
                                      db->pool));

      /* If recording traversal info, then see if the
         SVN_PROP_EXTERNALS property on this directory changed,
         and record before and after for the change. */
      if (db->edit_baton->traversal_info)
      {
        svn_wc_traversal_info_t *ti = db->edit_baton->traversal_info;
        const svn_prop_t *change = externals_prop_changed (db->propchanges);

        if (change)
          {
            const svn_string_t *new_val_s = change->value;
            const svn_string_t *old_val_s;

            SVN_ERR (svn_wc_prop_get
                     (&old_val_s, SVN_PROP_EXTERNALS,
                      db->path, db->pool));

            if ((new_val_s == NULL) && (old_val_s == NULL))
              ; /* No value before, no value after... so do nothing. */
            else if (new_val_s && old_val_s
                     && (svn_string_compare (old_val_s, new_val_s)))
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
                    apr_hash_set (ti->externals_old,
                                  apr_pstrdup (ti->pool, db->path),
                                  APR_HASH_KEY_STRING,
                                  apr_pstrmemdup (ti->pool, old_val_s->data,
                                                  old_val_s->len));

                if (new_val_s)
                  apr_hash_set (ti->externals_new,
                                apr_pstrdup (ti->pool, db->path),
                                APR_HASH_KEY_STRING,
                                apr_pstrmemdup (ti->pool, new_val_s->data,
                                                new_val_s->len));
              }
          }
      }

      /* Merge pending properties into temporary files and detect
         conflicts. */
      SVN_ERR_W (svn_wc__merge_prop_diffs (&prop_state, &prop_conflicts,
                                           adm_access, NULL,
                                           db->propchanges, db->pool,
                                           &entry_accum),
                 "close_dir: couldn't do prop merge.");

      /* Set revision. */
      revision_str = apr_psprintf (db->pool,
                                   "%" SVN_REVNUM_T_FMT,
                                   db->edit_baton->target_revision);
      
      /* Write a log entry to bump the directory's revision. */
      svn_xml_make_open_tag (&entry_accum,
                             db->pool,
                             svn_xml_self_closing,
                             SVN_WC__LOG_MODIFY_ENTRY,
                             SVN_WC__LOG_ATTR_NAME,
                             SVN_WC_ENTRY_THIS_DIR,
                             SVN_WC__ENTRY_ATTR_REVISION,
                             revision_str,
                             NULL);


      /* Are the directory's props locally modified? */
      SVN_ERR (svn_wc_props_modified_p (&prop_modified,
                                        db->path, adm_access,
                                        db->pool));

      /* Log entry which sets a new property timestamp, but *only* if
         there are no local changes to the props. */
      if (! prop_modified)
        svn_xml_make_open_tag (&entry_accum,
                               db->pool,
                               svn_xml_self_closing,
                               SVN_WC__LOG_MODIFY_ENTRY,
                               SVN_WC__LOG_ATTR_NAME,
                               SVN_WC_ENTRY_THIS_DIR,
                               SVN_WC__ENTRY_ATTR_PROP_TIME,
                               /* use wfile time */
                               SVN_WC_TIMESTAMP_WC,
                               NULL);

      
      /* Write our accumulation of log entries into a log file */
      apr_err = apr_file_write_full (log_fp, entry_accum->data,
                                     entry_accum->len, NULL);
      if (apr_err)
        {
          apr_file_close (log_fp);
          return svn_error_createf (apr_err, 0, NULL, db->pool,
                                    "close_dir: error writing %s's log file",
                                    db->path);
        }
      
      /* The log is ready to run, close it. */
      SVN_ERR (svn_wc__close_adm_file (log_fp,
                                       db->path,
                                       SVN_WC__ADM_LOG,
                                       TRUE, /* sync */
                                       db->pool));

      /* Run the log. */
      SVN_ERR (svn_wc__run_log (adm_access, db->pool));
    }


  /* We're done with this directory, so remove one reference from the
     bump information. This may trigger a number of actions. See
     maybe_bump_dir_revision() for more information.  */
  SVN_ERR (maybe_bump_dir_revision (db->edit_baton, db->bump_info, db->pool));

  /* Notify of any prop changes on this directory -- but do nothing
     if it's an added directory, because notification has already
     happened in that case. */
  if ((! db->added) && (db->edit_baton->notify_func))
    (*db->edit_baton->notify_func) (db->edit_baton->notify_baton,
                                    db->path,
                                    svn_wc_notify_update_update,
                                    svn_node_dir,
                                    NULL,
                                    svn_wc_notify_state_unknown,
                                    prop_state,
                                    SVN_INVALID_REVNUM);

  return SVN_NO_ERROR;
}



/* Common code for add_file() and open_file(). */
static svn_error_t *
add_or_open_file (const char *path,
                  void *parent_baton,
                  const char *copyfrom_path,
                  svn_revnum_t copyfrom_rev,
                  void **file_baton,
                  svn_boolean_t adding, /* 0 if replacing */
                  apr_pool_t *pool)
{
  struct dir_baton *pb = parent_baton;
  struct file_baton *fb;
  const svn_wc_entry_t *entry;
  int is_wc;
  enum svn_node_kind kind;
  svn_wc_adm_access_t *adm_access;

  /* the file_pool can stick around for a *long* time, so we want to use
     a subpool for any temporary allocations. */
  apr_pool_t *subpool = svn_pool_create (pool);

  /* ### kff todo: if file is marked as removed by user, then flag a
     conflict in the entry and proceed.  Similarly if it has changed
     kind.  see issuezilla task #398. */

  fb = make_file_baton (pb, path, adding, pool);

  /* It is interesting to note: everything below is just validation. We
     aren't actually doing any "work" or fetching any persistent data. */

  /* ### It would be nice to get the dirents and entries *once* and stash
     ### them in the directory baton.  But an important question is,
     ### are we re-reading the entries each time because we need to be
     ### sensitive to any work we've already done on the directory?
     ### Are editor drives guaranteed not to mention the same name
     ### twice in the same dir baton?  Don't know.  */

  SVN_ERR (svn_io_check_path (fb->path, &kind, subpool));
  SVN_ERR (svn_wc_adm_retrieve (&adm_access, pb->edit_baton->adm_access,
                                pb->path, subpool));
  SVN_ERR (svn_wc_entry (&entry, fb->path, adm_access, FALSE, subpool));
  
  /* Sanity checks. */

  /* If adding there may be a file with this name if this is a checkout,
     otherwise there should be nothing with this name. */
  if (adding
      && kind != svn_node_none
      && !(fb->edit_baton->is_checkout && kind == svn_node_file))
    return svn_error_createf
      (SVN_ERR_WC_OBSTRUCTED_UPDATE, 0, NULL, subpool,
       "failed to add file '%s': object of the same name already exists",
       fb->path);

  /* sussman sez: If we're trying to add a file that's already in
     `entries' (but not on disk), that's okay.  It's probably because
     the user deleted the working version and ran 'svn up' as a means
     of getting the file back.  

     It certainly doesn't hurt to re-add the file.  We can't possibly
     get the entry showing up twice in `entries', since it's a hash;
     and we know that we won't lose any local mods.  Let the existing
     entry be overwritten. */

  /* If replacing, make sure the .svn entry already exists. */
  if ((! adding) && (! entry))
    return svn_error_createf (SVN_ERR_ENTRY_NOT_FOUND, 0, NULL, subpool,
                              "trying to open non-versioned file "
                              "%s in directory %s",
                              fb->name, pb->path);
  
        
  /* Make sure we've got a working copy to put the file in. */
  /* kff todo: need stricter logic here */
  SVN_ERR (svn_wc_check_wc (pb->path, &is_wc, subpool));
  if (! is_wc)
    return svn_error_createf
      (SVN_ERR_WC_OBSTRUCTED_UPDATE, 0, NULL, subpool,
       "add_or_open_file: %s is not a working copy directory",
       pb->path);

  /* ### todo:  right now the incoming copyfrom* args are being
     completely ignored!  Someday the editor-driver may expect us to
     support this optimization;  when that happens, this func needs to
     -copy- the specified existing wc file to this location.  From
     there, the driver can apply_textdelta on it, etc. */

  svn_pool_destroy (subpool);

  *file_baton = fb;
  return SVN_NO_ERROR;
}


static svn_error_t *
add_file (const char *name,
          void *parent_baton,
          const char *copyfrom_path,
          svn_revnum_t copyfrom_revision,
          apr_pool_t *pool,
          void **file_baton)
{
  return add_or_open_file (name, parent_baton, copyfrom_path, 
                           copyfrom_revision, file_baton, TRUE, pool);
}


static svn_error_t *
open_file (const char *name,
           void *parent_baton,
           svn_revnum_t base_revision,
           apr_pool_t *pool,
           void **file_baton)
{
  return add_or_open_file (name, parent_baton, NULL, base_revision, 
                           file_baton, FALSE, pool);
}


static svn_error_t *
apply_textdelta (void *file_baton, 
                 apr_pool_t *pool,
                 svn_txdelta_window_handler_t *handler,
                 void **handler_baton)
{
  struct file_baton *fb = file_baton;
  apr_pool_t *subpool = svn_pool_create (fb->pool);
  struct handler_baton *hb = apr_palloc (subpool, sizeof (*hb));
  svn_error_t *err;

  /* Open the text base for reading, unless this is a checkout. */
  hb->source = NULL;
  if (! fb->edit_baton->is_checkout)
    {
      /* 
         kff todo: what we really need to do here is:
         
         1. See if there's a file or dir by this name already here.
         2. See if it's under revision control.
         3. If both are true, open text-base.
         4. If only 1 is true, bail, because we can't go destroying
            user's files (or as an alternative to bailing, move it to
            some tmp name and somehow tell the user, but communicating
            with the user without erroring is a whole callback system
            we haven't finished inventing yet.)
      */

      /* Before applying incoming svndiff data to text base, make sure
         text base hasn't been corrupted. */
      {
        svn_wc_adm_access_t *adm_access;
        const svn_wc_entry_t *ent;

        SVN_ERR (svn_wc_adm_retrieve (&adm_access, fb->edit_baton->adm_access,
                                      svn_path_remove_component_nts (fb->path,
                                                                     subpool),
                                      subpool));
        SVN_ERR (svn_wc_entry (&ent, fb->path, adm_access, FALSE, subpool));

        /* Only compare checksums this file has an entry, and the
           entry has a checksum.  If there's no entry, it just means
           the file is created in this update, so there won't be any
           previously recorded checksum to compare against.  If no
           checksum, well, for backwards compatibility we assume that
           no checksum always matches. */
        if (ent && ent->checksum)
          {
            svn_stringbuf_t *checksum;
            const char *tb;

            tb = svn_wc__text_base_path (fb->path, FALSE, subpool);
            SVN_ERR (svn_io_file_checksum (&checksum, tb, subpool));
            
            if (strcmp (checksum->data, ent->checksum) != 0)
              {
                return svn_error_createf
                  (SVN_ERR_WC_CORRUPT_TEXT_BASE, 0, NULL, subpool,
                   "apply_textdelta: checksum mismatch for '%s':\n"
                   "   recorded checksum: %s\n"
                   "   actual checksum:   %s\n",
                   tb, ent->checksum, checksum->data);
                
              }
          }
      }

      err = svn_wc__open_text_base (&hb->source, fb->path, APR_READ, subpool);
      if (err && !APR_STATUS_IS_ENOENT(err->apr_err))
        {
          if (hb->source)
            svn_wc__close_text_base (hb->source, fb->path, 0, subpool);
          svn_pool_destroy (subpool);
          return err;
        }
      else if (err)
        {
          svn_error_clear_all (err);
          hb->source = NULL;  /* make sure */
        }
    }

  /* Open the text base for writing (this will get us a temporary file).  */
  hb->dest = NULL;
  err = svn_wc__open_text_base (&hb->dest, fb->path,
                                (APR_WRITE | APR_TRUNCATE | APR_CREATE),
                                subpool);
  if (err)
    {
      if (hb->dest)
        svn_wc__close_text_base (hb->dest, fb->path, 0, subpool);
      svn_pool_destroy (subpool);
      return err;
    }
  
  /* Prepare to apply the delta.  */
  svn_txdelta_apply (svn_stream_from_aprfile (hb->source, subpool),
                     svn_stream_from_aprfile (hb->dest, subpool),
                     subpool, &hb->apply_handler, &hb->apply_baton);
  
  hb->pool = subpool;
  hb->fb = fb;
  
  /* We're all set.  */
  *handler_baton = hb;
  *handler = window_handler;

  return SVN_NO_ERROR;
}




static svn_error_t *
change_file_prop (void *file_baton,
                  const char *name,
                  const svn_string_t *value,
                  apr_pool_t *pool)
{
  struct file_baton *fb = file_baton;
  svn_prop_t *propchange;

  /* Push a new propchange to the file baton's array of propchanges */
  propchange = apr_array_push (fb->propchanges);
  propchange->name = apr_pstrdup (fb->pool, name);
  propchange->value = value ? svn_string_dup (value, fb->pool) : NULL;

  /* Let close_file() know that propchanges are waiting to be
     applied. */
  fb->prop_changed = 1;

  return SVN_NO_ERROR;
}



/* This is the small planet.  It has the complex responsibility of
   "integrating" a new revision of a file into a working copy.  It's
   used extensively by the update-editor, as well as by
   svn_client_switch(), when switching a single file in-place. */
svn_error_t *
svn_wc_install_file (svn_wc_notify_state_t *content_state,
                     svn_wc_notify_state_t *prop_state,
                     svn_wc_adm_access_t *adm_access,
                     const char *file_path,
                     svn_revnum_t new_revision,
                     const char *new_text_path,
                     const apr_array_header_t *props,
                     svn_boolean_t is_full_proplist,
                     const char *new_URL,
                     apr_pool_t *pool)
{
  apr_file_t *log_fp = NULL;
  apr_status_t apr_err;
  char *revision_str = NULL;
  const char *parent_dir, *base_name;
  svn_stringbuf_t *log_accum;
  svn_boolean_t is_locally_modified;
  svn_boolean_t magic_props_changed = FALSE;
  apr_array_header_t *regular_props = NULL, *wc_props = NULL,
    *entry_props = NULL;

  /* The code flow does not depend upon these being set to NULL, but
     it removes a gcc 3.1 `might be used uninitialized in this
     function' warning. */
  const char *txtb = NULL, *tmp_txtb = NULL;

  /* Start by splitting FILE_PATH. */
  svn_path_split_nts (file_path, &parent_dir, &base_name, pool);

  /*
     When this function is called on file F, we assume the following
     things are true:

         - The new pristine text of F, if any, is present at
           NEW_TEXT_PATH.           

         - The .svn/entries file still reflects the old version of F.

         - .svn/text-base/F.svn-base is the old pristine F.

      The goal is to update the local working copy of F to reflect
      the changes received from the repository, preserving any local
      modifications, in an interrupt-safe way.  So we first write our
      intentions to .svn/log, then run over the log file doing each
      operation in turn.  For a given operation, you can tell by
      inspection whether or not it has already been done; thus, those
      that have already been done are no-ops, and when we reach the
      end of the log file, we remove it.
  */

  /* Open a log file.  This is safe because the adm area is locked
     right now. */
  SVN_ERR (svn_wc__open_adm_file (&log_fp,
                                  parent_dir,
                                  SVN_WC__ADM_LOG,
                                  (APR_WRITE | APR_CREATE), /* not excl */
                                  pool));

  /* Accumulate log commands in this buffer until we're ready to close
     and run the log.  */
  log_accum = svn_stringbuf_create ("", pool);
  

  /* Log commands can only operate on paths that are below the
     parent_dir.  Thus if NEW_TEXT_PATH is somewhere *outside* of
     FILE_PATH's parent directory, we can't write a log command to do
     a move from one location to another.  So the solution, then, is
     to simply move NEW_TEXT_PATH to .svn/tmp/text-base/ immediately
     -- that's where the rest of this code wants it to be anyway. */
  if (new_text_path)
    {
      const char *final_location = 
        svn_wc__text_base_path (file_path, TRUE, pool);
      
      /* Only do the 'move' if NEW_TEXT_PATH isn't -already-
         pointing to parent_dir/.svn/tmp/text-base/basename.  */
      if (strcmp (final_location, new_text_path))
        {
          SVN_ERR_W (svn_io_file_rename (new_text_path, final_location,
                                         pool),
                     "svn_wc_install_file: move failed");

          new_text_path = final_location;
        }
    }
  
  /* Sort the property list into three arrays, based on kind. */
  if (props)
    SVN_ERR (svn_categorize_props (props,
                                   &entry_props, &wc_props, &regular_props,
                                   pool));

  /* Always initialize to unknown state. */
  if (prop_state)
    *prop_state = svn_wc_notify_state_unknown;

  /* Merge the 'regular' props into the existing working proplist. */
  if (regular_props)
    {
      apr_array_header_t *propchanges;
      apr_hash_t *old_pristine_props, *new_pristine_props;
      
      if (is_full_proplist)
        {         
          /* If the caller passed a definitive list that represents all
             of the file's properties, we need to compare it to the
             current 'pristine' list and deduce the differences. */
          const char *pristine_prop_path;
          int i;
          old_pristine_props = apr_hash_make (pool);
          new_pristine_props = apr_hash_make (pool);
          
          /* Get the current pristine props. */
          SVN_ERR (svn_wc__prop_base_path (&pristine_prop_path,
                                           file_path, 0, pool));
          SVN_ERR (svn_wc__load_prop_file (pristine_prop_path,
                                           old_pristine_props, pool));
          
          /* Convert the given array into hash of 'new' pristine props. */
          for (i = 0; i < regular_props->nelts; i++)
            {
              const svn_prop_t *prop = NULL;
              prop = &APR_ARRAY_IDX (regular_props, i, svn_prop_t);
              apr_hash_set (new_pristine_props,
                            prop->name, APR_HASH_KEY_STRING, prop->value);
            }
          
          /* Deduce changes. */
          SVN_ERR (svn_wc_get_local_propchanges (&propchanges,
                                                 new_pristine_props,
                                                 old_pristine_props,
                                                 pool));
        }
      else
        /* The user gave us a list prop diffs directly, yay. */
        propchanges = regular_props;
      
      /* Now that we have the list of diffs... */
      
      /* Determine if any of the propchanges are the "magic" ones that
         might require changing the working file. */
      {
        int i;
        for (i = 0; i < propchanges->nelts; i++)
          {
            svn_prop_t *propchange
              = &APR_ARRAY_IDX (propchanges, i, svn_prop_t);
            
            if ((! strcmp (propchange->name, SVN_PROP_EXECUTABLE))
                || (! strcmp (propchange->name, SVN_PROP_KEYWORDS))
                || (! strcmp (propchange->name, SVN_PROP_EOL_STYLE)))
              magic_props_changed = TRUE;
          }
      }

      /* This will merge the old and new props into a new prop db, and
         write <cp> commands to the logfile to install the merged
         props.  */
      {
        apr_hash_t *ignored_conflicts;

        /* ### There are at least two callers that are ignoring the
           conflict report.  Ideally, the argument would be optional,
           so we could just pass NULL.  It's mandatory right now.  */

        SVN_ERR (svn_wc__merge_prop_diffs (prop_state, &ignored_conflicts,
                                           adm_access, base_name,
                                           propchanges, pool,
                                           &log_accum));
      }
    }
  
  /* If there are any ENTRY PROPS, make sure those get appended to the
     growing log as fields for the file's entry.  This needs to happen
     before we do any textual merging, because that process might
     expand keywords, and we want the keyword info to be up-to-date.

     Note that no merging needs to happen; these kinds of props aren't
     versioned, so the value of IS_FULL_PROPLIST is irrelevant -- if
     the property is present, we overwrite the value. */  
  if (entry_props)
    {
      int i;
      
      /* foreach entry prop... */
      for (i = 0; i < entry_props->nelts; i++)
        {
          const svn_prop_t *prop;
          const char *propval;
          enum svn_prop_kind kind;
          const char *entry_field = NULL;

          prop = &APR_ARRAY_IDX (entry_props, i, svn_prop_t);
          kind = svn_property_kind (NULL, prop->name);

          /* A prop value of NULL means the information was not
             available.  We don't remove this field from the entries
             file; we have convention just leave it empty.  So let's
             just skip those entry props that have no values.
          */
          if (! prop->value)
            continue;

          if (! strcmp (prop->name, SVN_PROP_ENTRY_LAST_AUTHOR))
            entry_field = SVN_WC__ENTRY_ATTR_CMT_AUTHOR;
          else if (! strcmp (prop->name, SVN_PROP_ENTRY_COMMITTED_REV))
            entry_field = SVN_WC__ENTRY_ATTR_CMT_REV;
          else if (! strcmp (prop->name, SVN_PROP_ENTRY_COMMITTED_DATE))
            entry_field = SVN_WC__ENTRY_ATTR_CMT_DATE;
          else
            continue;

          if (prop->value)
            propval = prop->value->data;
          else
            propval = "";
          
          /* append a command to the log which will write the
             property as a entry attribute on the file. */
          svn_xml_make_open_tag (&log_accum,
                                 pool,
                                 svn_xml_self_closing,
                                 SVN_WC__LOG_MODIFY_ENTRY,
                                 SVN_WC__LOG_ATTR_NAME,
                                 base_name,
                                 entry_field,
                                 propval,
                                 NULL);         
        }
    }

  /* Has the user made local mods to the working file?  */
  SVN_ERR (svn_wc_text_modified_p (&is_locally_modified,
                                   file_path, adm_access, pool));

  if (new_text_path)   /* is there a new text-base to install? */
    {
      txtb     = svn_wc__text_base_path (base_name, FALSE, pool);
      tmp_txtb = svn_wc__text_base_path (base_name, TRUE, pool);
    }
  else if (magic_props_changed) /* no new text base, but... */
    {
      /* Special edge-case: it's possible that this file installation
         only involves propchanges, but that some of those props still
         require a retranslation of the working file. */

      const char *tmptext = svn_wc__text_base_path (base_name, TRUE, pool);

      /* A log command which copies and DEtranslates the working file
         to a tmp-text-base. */
      svn_xml_make_open_tag (&log_accum, pool,
                             svn_xml_self_closing,
                             SVN_WC__LOG_CP_AND_DETRANSLATE,
                             SVN_WC__LOG_ATTR_NAME, base_name,
                             SVN_WC__LOG_ATTR_DEST, tmptext,
                             NULL);

      /* A log command that copies the tmp-text-base and REtranslates
         the tmp-text-base back to the working file. */
      svn_xml_make_open_tag (&log_accum, pool,
                             svn_xml_self_closing,
                             SVN_WC__LOG_CP_AND_TRANSLATE,
                             SVN_WC__LOG_ATTR_NAME, tmptext,
                             SVN_WC__LOG_ATTR_DEST, base_name,
                             NULL);
    }

  /* Write log entry which will bump the revision number.  Also, just
     in case we're overwriting an existing phantom 'deleted' entry, be
     sure to remove the deleted-ness. */
  revision_str = apr_psprintf (pool, "%" SVN_REVNUM_T_FMT, new_revision);
  svn_xml_make_open_tag (&log_accum,
                         pool,
                         svn_xml_self_closing,
                         SVN_WC__LOG_MODIFY_ENTRY,
                         SVN_WC__LOG_ATTR_NAME,
                         base_name,
                         SVN_WC__ENTRY_ATTR_KIND,
                         SVN_WC__ENTRIES_ATTR_FILE_STR,
                         SVN_WC__ENTRY_ATTR_REVISION,
                         revision_str,
                         SVN_WC__ENTRY_ATTR_DELETED,
                         "false",
                         NULL);


  /* Possibly install a *non*-inherited URL in the entry. */
  if (new_URL)
    {
      svn_xml_make_open_tag (&log_accum,
                             pool,
                             svn_xml_self_closing,
                             SVN_WC__LOG_MODIFY_ENTRY,
                             SVN_WC__LOG_ATTR_NAME,
                             base_name,
                             SVN_WC__ENTRY_ATTR_URL,
                             new_URL,
                             NULL);
    }


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
      if (! is_locally_modified)
        {
          /* If there are no local mods, who cares whether it's a text
             or binary file!  Just write a log command to overwrite
             any working file with the new text-base.  If newline
             conversion or keyword substitution is activated, this
             will happen as well during the copy. */
          svn_xml_make_open_tag (&log_accum,
                                 pool,
                                 svn_xml_self_closing,
                                 SVN_WC__LOG_CP_AND_TRANSLATE,
                                 SVN_WC__LOG_ATTR_NAME,
                                 tmp_txtb,
                                 SVN_WC__LOG_ATTR_DEST,
                                 base_name,
                                 NULL);
        }
  
      else   /* working file is locally modified... */
        {
          enum svn_node_kind wfile_kind = svn_node_unknown;
          
          SVN_ERR (svn_io_check_path (file_path, &wfile_kind, pool));
          if (wfile_kind == svn_node_none) /* working file is missing?! */
            {
              /* Just copy the new text-base to the file. */
              svn_xml_make_open_tag (&log_accum,
                                     pool,
                                     svn_xml_self_closing,
                                     SVN_WC__LOG_CP_AND_TRANSLATE,
                                     SVN_WC__LOG_ATTR_NAME,
                                     tmp_txtb,
                                     SVN_WC__LOG_ATTR_DEST,
                                     base_name,
                                     NULL);
            }
          else  /* working file exists, and has local mods.*/
            {                  
              /* Now we need to let loose svn_wc_merge() to merge the
                 textual changes into the working file. */
              const char *oldrev_str, *newrev_str;
              const svn_wc_entry_t *e;
              
              /* Create strings representing the revisions of the
                 old and new text-bases. */
              SVN_ERR (svn_wc_entry (&e, file_path, adm_access, FALSE, pool));
              assert (e != NULL);
              oldrev_str = apr_psprintf (pool, ".r%" SVN_REVNUM_T_FMT,
                                         e->revision);
              newrev_str = apr_psprintf (pool, ".r%" SVN_REVNUM_T_FMT,
                                         new_revision);
              
              /* Merge the changes from the old-textbase (TXTB) to
                 new-textbase (TMP_TXTB) into the file we're
                 updating (BASE_NAME).  Either the merge will
                 happen smoothly, or a conflict will result.
                 Luckily, this routine will take care of all eol
                 and keyword translation, and diff3 will insert
                 conflict markers for us.  It also deals with binary
                 files appropriately.  */
              svn_xml_make_open_tag (&log_accum,
                                     pool,
                                     svn_xml_self_closing,
                                     SVN_WC__LOG_MERGE,
                                     SVN_WC__LOG_ATTR_NAME, base_name,
                                     SVN_WC__LOG_ATTR_ARG_1, txtb,
                                     SVN_WC__LOG_ATTR_ARG_2, tmp_txtb,
                                     SVN_WC__LOG_ATTR_ARG_3, oldrev_str,
                                     SVN_WC__LOG_ATTR_ARG_4, newrev_str,
                                     SVN_WC__LOG_ATTR_ARG_5, ".mine",
                                     NULL);
              
              /* If a conflict happens, then the entry will be
                 marked "Conflicted" and will track either 2 or 3 new
                 temporary fulltext files that resulted. */
              
            } /* end: working file exists and has mods */
        } /* end: working file has mods */
    }  /* end:  "textual" merging process */

  /* Possibly write log commands to tweak text/prop entry timestamps: */
  if (new_text_path)
    {
      /* Log entry which sets a new textual timestamp, but only if
         there are no local changes to the text. */
      if (! is_locally_modified)
        svn_xml_make_open_tag (&log_accum,
                               pool,
                               svn_xml_self_closing,
                               SVN_WC__LOG_MODIFY_ENTRY,
                               SVN_WC__LOG_ATTR_NAME,
                               base_name,
                               SVN_WC__ENTRY_ATTR_TEXT_TIME,
                               /* use wfile time */
                               SVN_WC_TIMESTAMP_WC,
                               NULL);
    }

  if (props)
    {
      svn_boolean_t prop_modified;

      /* Are the working file's props locally modified? */
      SVN_ERR (svn_wc_props_modified_p (&prop_modified,
                                        file_path, adm_access,
                                        pool));

      /* Log entry which sets a new property timestamp, but only if
         there are no local changes to the props. */
      if (! prop_modified)
        svn_xml_make_open_tag (&log_accum,
                               pool,
                               svn_xml_self_closing,
                               SVN_WC__LOG_MODIFY_ENTRY,
                               SVN_WC__LOG_ATTR_NAME,
                               base_name,
                               SVN_WC__ENTRY_ATTR_PROP_TIME,
                               /* use wfile time */
                               SVN_WC_TIMESTAMP_WC,
                               NULL);
    }

  if (new_text_path)
    {
      /* Write out log commands to set up the new text base and its
         checksum. */

      svn_xml_make_open_tag (&log_accum,
                             pool,
                             svn_xml_self_closing,
                             SVN_WC__LOG_MV,
                             SVN_WC__LOG_ATTR_NAME,
                             tmp_txtb,
                             SVN_WC__LOG_ATTR_DEST,
                             txtb,
                             NULL);
      
      svn_xml_make_open_tag (&log_accum,
                             pool,
                             svn_xml_self_closing,
                             SVN_WC__LOG_READONLY,
                             SVN_WC__LOG_ATTR_NAME,
                             txtb,
                             NULL);

      {
        svn_stringbuf_t *checksum;
        SVN_ERR (svn_io_file_checksum (&checksum, new_text_path, pool));
        svn_xml_make_open_tag (&log_accum,
                               pool,
                               svn_xml_self_closing,
                               SVN_WC__LOG_MODIFY_ENTRY,
                               SVN_WC__LOG_ATTR_NAME,
                               base_name,
                               SVN_WC__ENTRY_ATTR_CHECKSUM,
                               checksum->data,
                               NULL);
      }
    }

  /* Write our accumulation of log entries into a log file */
  apr_err = apr_file_write_full (log_fp, log_accum->data, 
                                 log_accum->len, NULL);
  if (apr_err)
    {
      apr_file_close (log_fp);
      return svn_error_createf (apr_err, 0, NULL, pool,
                                "svn_wc_install_file: error writing %s's log",
                                file_path);
    }

  /* The log is ready to run.  Close it and run it! */
  SVN_ERR (svn_wc__close_adm_file (log_fp, parent_dir, SVN_WC__ADM_LOG,
                                   TRUE, /* sync */ pool));
  SVN_ERR (svn_wc__run_log (adm_access, pool));

  /* Now that the file's text, props, and entries are fully installed,
     we dump any "wc" props.  ### This should be done *loggily*, see
     issue #628.  */
  if (wc_props)
    {
      int i;
      for (i = 0; i < wc_props->nelts; i++)
        {
          const svn_prop_t *prop;

          prop = &APR_ARRAY_IDX(wc_props, i, svn_prop_t);
          SVN_ERR (svn_wc__wcprop_set (prop->name, prop->value,
                                       file_path, pool));
        }
    }

  if (content_state)
    {
      const svn_wc_entry_t *entry;
      svn_boolean_t tc, pc;

      /* Initialize the state of our returned value. */
      *content_state = svn_wc_notify_state_unchanged;
      
      /* ### There should be a more efficient way of finding out whether
         or not the file is modified|merged|conflicted.  If the
         svn_wc__run_log() call above could return a special error code
         in case of a conflict or something, that would work. */

      SVN_ERR (svn_wc_entry (&entry, file_path, adm_access, TRUE, pool));
      SVN_ERR (svn_wc_conflicted_p (&tc, &pc, parent_dir, entry, pool));
      
      /* This is kind of interesting.  Even if no new text was
         installed (i.e., new_text_path was null), we could still
         report a pre-existing conflict state.  Say a file, already
         in a state of textual conflict, receives prop mods during an
         update.  Then we'll notify that it has text conflicts.  This
         seems okay to me.  I guess.  I dunno.  You? */

      if (tc)
        *content_state = svn_wc_notify_state_conflicted;
      else if (new_text_path)
        {
          if (is_locally_modified)
            *content_state = svn_wc_notify_state_merged;
          else
            *content_state = svn_wc_notify_state_modified;
        }
      else if (is_locally_modified)
        *content_state = svn_wc_notify_state_modified;
    }

  return SVN_NO_ERROR;
}



/* Mostly a wrapper around svn_wc_install_file. */
static svn_error_t *
close_file (void *file_baton,
            apr_pool_t *pool)
{
  struct file_baton *fb = file_baton;
  const char *new_text_path = NULL, *parent_path;
  apr_array_header_t *propchanges = NULL;
  svn_wc_notify_state_t content_state, prop_state;
  svn_wc_adm_access_t *adm_access;

  /* window-handler assembles new pristine text in .svn/tmp/text-base/  */
  if (fb->text_changed)
    new_text_path = svn_wc__text_base_path (fb->path, TRUE, fb->pool);

  if (fb->prop_changed)
    propchanges = fb->propchanges;

  parent_path = svn_path_remove_component_nts (fb->path, fb->pool);
    
  SVN_ERR (svn_wc_adm_retrieve (&adm_access, fb->edit_baton->adm_access,
                                parent_path, fb->pool));

  SVN_ERR (svn_wc_install_file (&content_state,
                                &prop_state,
                                adm_access,
                                fb->path,
                                fb->edit_baton->target_revision,
                                new_text_path,
                                propchanges,
                                FALSE, /* -not- a full proplist */
                                NULL, /* inherit URL from parent dir. */
                                fb->pool));

  /* We have one less referrer to the directory's bump information. */
  SVN_ERR (maybe_bump_dir_revision (fb->edit_baton,
                                    fb->bump_info,
                                    fb->pool));

  if ((content_state != svn_wc_notify_state_unchanged) ||
      (prop_state != svn_wc_notify_state_unchanged))
    {
      if (fb->edit_baton->notify_func)
        (*fb->edit_baton->notify_func)
          (fb->edit_baton->notify_baton,
           fb->path,
           fb->added ? svn_wc_notify_update_add : svn_wc_notify_update_update,
           svn_node_file,
           NULL,  /* ### if svn_wc_install_file gives mimetype, use it here */
           content_state,
           prop_state,
           SVN_INVALID_REVNUM);
    }
  return SVN_NO_ERROR;  
}


static svn_error_t *
close_edit (void *edit_baton,
            apr_pool_t *pool)
{
  struct edit_baton *eb = edit_baton;
  
  /* By definition, anybody "driving" this editor for update or switch
     purposes at a *minimum* must have called set_target_revision() at
     the outset, and close_edit() at the end -- even if it turned out
     that no changes ever had to be made, and open_root() was never
     called.  That's fine.  But regardless, when the edit is over,
     this editor needs to make sure that *all* paths have had their
     revisions bumped to the new target revision. */

  /* Do nothing for checkout;  all urls and working revs are fine.
     Updates and switches, though, have to be cleaned up.  */
  if (! eb->is_checkout)
    {
      /* Make sure our update target now has the new working revision.
         Also, if this was an 'svn switch', then rewrite the target's
         url.  All of this tweaking might happen recursively!  Note
         that if eb->target is NULL, that's okay (albeit "sneaky",
         some might say).  */
      SVN_ERR (svn_wc__do_update_cleanup
               (svn_path_join_many (eb->pool, eb->anchor, eb->target, NULL),
                eb->adm_access,
                eb->recurse,
                eb->switch_url,
                eb->target_revision,
                eb->pool));
    }

  if (eb->notify_func)
    (*eb->notify_func) (eb->notify_baton,
                        eb->anchor,
                        svn_wc_notify_update_completed,
                        svn_node_none,
                        NULL,
                        svn_wc_notify_state_inapplicable,
                        svn_wc_notify_state_inapplicable,
                        eb->target_revision);

  /* ### Would really like to pass this back to the caller, but there is no
     ### easy way to do it. So we close it. */
  if (eb->is_checkout)
    SVN_ERR (svn_wc_adm_close (eb->adm_access));

  /* The edit is over, free its pool.
     ### No, this is wrong.  Who says this editor/baton won't be used
     again?  But the change is not merely to remove this call.  We
     should also make eb->pool not be a subpool (see make_editor),
     and change callers of svn_client_{checkout,update,switch} to do
     better pool management. ### */
  svn_pool_destroy (eb->pool);
  
  return SVN_NO_ERROR;
}



/*** Returning editors. ***/

/* Helper for the two public editor-supplying functions. */
static svn_error_t *
make_editor (svn_wc_adm_access_t *adm_access,
             const char *anchor,
             const char *target,
             svn_revnum_t target_revision,
             svn_boolean_t is_checkout,
             const char *ancestor_url,
             const char *switch_url,
             svn_boolean_t recurse,
             svn_wc_notify_func_t notify_func,
             void *notify_baton,
             const svn_delta_editor_t **editor,
             void **edit_baton,
             svn_wc_traversal_info_t *traversal_info,
             apr_pool_t *pool)
{
  struct edit_baton *eb;
  apr_pool_t *subpool = svn_pool_create (pool);
  svn_delta_editor_t *tree_editor = svn_delta_default_editor (subpool);

  if (is_checkout)
    assert (ancestor_url != NULL);

  /* Construct an edit baton. */
  eb = apr_palloc (subpool, sizeof (*eb));
  eb->pool            = subpool;
  eb->is_checkout     = is_checkout;
  eb->target_revision = target_revision;
  eb->ancestor_url    = ancestor_url;
  eb->switch_url      = switch_url;
  eb->adm_access      = adm_access;
  eb->anchor          = anchor;
  eb->target          = target;
  eb->recurse         = recurse;
  eb->notify_func     = notify_func;
  eb->notify_baton    = notify_baton;
  eb->traversal_info  = traversal_info;

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

  *edit_baton = eb;
  *editor = tree_editor;

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc_get_update_editor (svn_wc_adm_access_t *anchor,
                          const char *target,
                          svn_revnum_t target_revision,
                          svn_boolean_t recurse,
                          svn_wc_notify_func_t notify_func,
                          void *notify_baton,
                          const svn_delta_editor_t **editor,
                          void **edit_baton,
                          svn_wc_traversal_info_t *traversal_info,
                          apr_pool_t *pool)
{
  return make_editor (anchor, svn_wc_adm_access_path (anchor),
                      target, target_revision, 
                      FALSE, NULL, NULL,
                      recurse, notify_func, notify_baton,
                      editor, edit_baton, traversal_info, pool);
}


svn_error_t *
svn_wc_get_checkout_editor (const char *dest,
                            const char *ancestor_url,
                            svn_revnum_t target_revision,
                            svn_boolean_t recurse,
                            svn_wc_notify_func_t notify_func,
                            void *notify_baton,
                            const svn_delta_editor_t **editor,
                            void **edit_baton,
                            svn_wc_traversal_info_t *traversal_info,
                            apr_pool_t *pool)
{
  return make_editor (NULL, dest, NULL, target_revision, 
                      TRUE, ancestor_url, NULL,
                      recurse, notify_func, notify_baton,
                      editor, edit_baton,
                      traversal_info, pool);
}


svn_error_t *
svn_wc_get_switch_editor (svn_wc_adm_access_t *anchor,
                          const char *target,
                          svn_revnum_t target_revision,
                          const char *switch_url,
                          svn_boolean_t recurse,
                          svn_wc_notify_func_t notify_func,
                          void *notify_baton,
                          const svn_delta_editor_t **editor,
                          void **edit_baton,
                          svn_wc_traversal_info_t *traversal_info,
                          apr_pool_t *pool)
{
  assert (switch_url);

  return make_editor (anchor, svn_wc_adm_access_path (anchor),
                      target, target_revision,
                      FALSE, NULL, switch_url,
                      recurse, notify_func, notify_baton,
                      editor, edit_baton,
                      traversal_info, pool);
}

svn_wc_traversal_info_t *
svn_wc_init_traversal_info (apr_pool_t *pool)
{
  svn_wc_traversal_info_t *ti = apr_palloc (pool, sizeof (*ti));
  
  ti->pool           = pool;
  ti->externals_old  = apr_hash_make (pool);
  ti->externals_new  = apr_hash_make (pool);
  
  return ti;
}


void
svn_wc_edited_externals (apr_hash_t **externals_old,
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
check_wc_root (svn_boolean_t *wc_root,
               svn_node_kind_t *kind,
               const char *path, 
               svn_wc_adm_access_t *adm_access,
               apr_pool_t *pool)
{
  const char *parent, *base_name;
  const svn_wc_entry_t *p_entry, *entry;
  svn_error_t *err;

  /* Go ahead and initialize our return value to the most common
     (code-wise) values. */
  *wc_root = TRUE;

  /* Get our ancestry (this doubles as a sanity check).  */
  SVN_ERR (svn_wc_entry (&entry, path, adm_access, FALSE, pool));
  if (! entry)
    return svn_error_createf 
      (SVN_ERR_ENTRY_NOT_FOUND, 0, NULL, pool,
       "svn_wc_is_wc_root: %s is not a versioned resource", path);
  if (kind)
    *kind = entry->kind;

  /* If PATH is the current working directory, we have no choice but
     to consider it a WC root (we can't examine its parent at all) */
  if (svn_path_is_empty_nts (path))
    return SVN_NO_ERROR;

  /* If we cannot get an entry for PATH's parent, PATH is a WC root. */
  svn_path_split_nts (path, &parent, &base_name, pool);
  SVN_ERR (svn_wc_adm_probe_open (&adm_access, NULL, parent, FALSE, FALSE,
                                  pool));
  err = svn_wc_entry (&p_entry, parent, adm_access, FALSE, pool);
  SVN_ERR (svn_wc_adm_close (adm_access));
  if (err || (! p_entry))
    {
      if (err)
        svn_error_clear_all (err);

      return SVN_NO_ERROR;
    }
  
  /* If the parent directory has no url information, something is
     messed up.  Bail with an error. */
  if (! p_entry->url)
    return svn_error_createf 
      (SVN_ERR_ENTRY_MISSING_URL, 0, NULL, pool,
       "svn_wc_is_wc_root: %s has no ancestry information.", 
       parent);

  /* If PATH's parent in the WC is not its parent in the repository,
     PATH is a WC root. */
  if (entry->url 
      && (strcmp (svn_path_url_add_component (p_entry->url, base_name, pool),
                  entry->url) != 0))
    return SVN_NO_ERROR;

  /* If we have not determined that PATH is a WC root by now, it must
     not be! */
  *wc_root = FALSE;
  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc_is_wc_root (svn_boolean_t *wc_root,
                   const char *path,
                   svn_wc_adm_access_t *adm_access,
                   apr_pool_t *pool)
{
  return check_wc_root (wc_root, NULL, path, adm_access, pool);
}


svn_error_t *
svn_wc_get_actual_target (const char *path,
                          const char **anchor,
                          const char **target,
                          apr_pool_t *pool)
{
  svn_wc_adm_access_t *adm_access;
  svn_boolean_t is_wc_root;
  svn_node_kind_t kind;

  SVN_ERR (svn_wc_adm_probe_open (&adm_access, NULL, path, FALSE, FALSE, pool));
  SVN_ERR (check_wc_root (&is_wc_root, &kind, path, adm_access, pool));
  SVN_ERR (svn_wc_adm_close (adm_access));

  /* If PATH is not a WC root, or if it is a file, lop off a basename. */
  if ((! is_wc_root) || (kind == svn_node_file))
    {
      svn_path_split_nts (path, anchor, target, pool);
      if ((*anchor)[0] == '\0')
        *anchor = "";
    }
  else
    {
      *anchor = apr_pstrdup (pool, path);
      *target = NULL;
    }

  return SVN_NO_ERROR;
}


/* 
 * local variables:
 * eval: (load-file "../../tools/dev/svn-dev.el")
 * end: 
 */

