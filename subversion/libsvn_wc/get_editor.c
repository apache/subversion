/*
 * get_editor.c :  routines for update and checkout
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

#include "wc.h"



/*** batons ***/

struct edit_baton
{
  /* For updates, the "destination" of the edit is the ANCHOR (the
     directory at which the edit is rooted) plus the TARGET (the
     actual thing we wish to update).  For checkouts, ANCHOR holds the
     whole path, and TARGET is unused. */
  svn_stringbuf_t *anchor;
  svn_stringbuf_t *target;

  /* The revision we're targeting...or something like that. */
  svn_revnum_t target_revision;

  /* Whether this edit will descend into subdirs */
  svn_boolean_t recurse;

  /* These used only in checkouts. */
  svn_boolean_t is_checkout;
  svn_stringbuf_t *ancestor_url;

  apr_pool_t *pool;
};


struct dir_baton
{
  /* The path to this directory. */
  svn_stringbuf_t *path;

  /* Basename of this directory. */
  svn_stringbuf_t *name;

  /* The number of other changes associated with this directory in the
     delta (typically, the number of files being changed here, plus
     this dir itself).  BATON->ref_count starts at 1, is incremented
     for each entity being changed, and decremented for each
     completion of one entity's changes.  When the ref_count is 0, the
     directory may be safely set to the target revision, and this baton
     freed. */
  int ref_count;

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

  /* An array of (svn_prop_t *)'s, representing all the property
     changes to be applied to this file. */
  apr_array_header_t *propchanges;

  /* The pool in which this baton itself is allocated. */
  apr_pool_t *pool;
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

/* Create a new dir_baton for subdir NAME in PARENT_PATH with
 * EDIT_BATON, using a new subpool of POOL.
 *
 * The new baton's ref_count is 1.
 *
 * NAME and PARENT_BATON can be null, meaning this is the root baton.
 */
static struct dir_baton *
make_dir_baton (svn_stringbuf_t *name,
                struct edit_baton *edit_baton,
                struct dir_baton *parent_baton,
                svn_boolean_t added,
                apr_pool_t *pool)
{
  struct edit_baton *eb = edit_baton;
  apr_pool_t *subpool = svn_pool_create (pool);
  struct dir_baton *d = apr_pcalloc (subpool, sizeof (*d));
  svn_stringbuf_t *path;

  if (parent_baton)
    {
      /* I, the baton-in-creation, have a parent, so base my path on
         that of my parent. */
      path = svn_stringbuf_dup (parent_baton->path, subpool);
    }
  else
    {
      /* I am Adam.  All my base are belong to me. */
      path = svn_stringbuf_dup (eb->anchor, subpool);
    }

  if (name)
    {
      d->name = svn_stringbuf_dup (name, subpool);
      svn_path_add_component (path, name, svn_path_local_style);
    }

  d->path         = path;
  d->edit_baton   = edit_baton;
  d->parent_baton = parent_baton;
  d->ref_count    = 1;
  d->pool         = subpool;
  d->propchanges  = apr_array_make (subpool, 1, sizeof(svn_prop_t *));
  d->added        = added;

  if (parent_baton)
    parent_baton->ref_count++;

  return d;
}


/* Avoid the circular prototypes problem. */
static svn_error_t *decrement_ref_count (struct dir_baton *d);


static svn_error_t *
free_dir_baton (struct dir_baton *dir_baton)
{
  svn_error_t *err;
  struct dir_baton *parent = dir_baton->parent_baton;

  /* Bump this dir to the new revision if this directory is beneath
     the target of an update, or unconditionally if this is a
     checkout. */
  if (dir_baton->edit_baton->is_checkout || parent)
    {
      SVN_ERR (svn_wc__entry_modify
               (dir_baton->path,
                NULL,
                SVN_WC__ENTRY_MODIFY_REVISION,
                dir_baton->edit_baton->target_revision,
                svn_node_dir,
                svn_wc_schedule_normal,
                FALSE, FALSE,
                0,
                0,
                NULL,
                NULL,
                dir_baton->pool,
                NULL));
    }

  /* If this directory is newly added it doesn't have an entry in the
     parent's list of entries. The directory is now complete, and can be
     added. */
  if (dir_baton->added && parent)
    {
      SVN_ERR (svn_wc__entry_modify (parent->path,
                                     dir_baton->name,
                                     SVN_WC__ENTRY_MODIFY_KIND,
                                     SVN_INVALID_REVNUM,
                                     svn_node_dir,
                                     svn_wc_schedule_normal,
                                     FALSE, FALSE,
                                     0,
                                     0,
                                     NULL,
                                     NULL,
                                     parent->pool,
                                     NULL));
    }

  /* After we destroy DIR_BATON->pool, DIR_BATON itself is lost. */
  svn_pool_destroy (dir_baton->pool);

  /* We've declared this directory done, so decrement its parent's ref
     count too. */ 
  if (parent)
    {
      err = decrement_ref_count (parent);
      if (err)
        return err;
    }

  return SVN_NO_ERROR;
}


/* Decrement DIR_BATON's ref count, and if the count hits 0, call
 * free_dir_baton().
 *
 * Note: There is no corresponding function for incrementing the
 * ref_count.  As far as we know, nothing special depends on that, so
 * it's always done inline.
 */
static svn_error_t *
decrement_ref_count (struct dir_baton *d)
{
  d->ref_count--;

  if (d->ref_count == 0)
    return free_dir_baton (d);

  return SVN_NO_ERROR;
}


struct file_baton
{
  /* Baton for this file's parent directory. */
  struct dir_baton *dir_baton;

  /* Pool specific to this file_baton. */
  apr_pool_t *pool;

  /* Name of this file (its entry in the directory). */
  const svn_stringbuf_t *name;

  /* Path to this file, either abs or relative to the change-root. */
  svn_stringbuf_t *path;

  /* This gets set if the file underwent a text change, which guides
     the code that syncs up the adm dir and working copy. */
  svn_boolean_t text_changed;

  /* This gets set if there's a conflict while merging the
     repository's file into the locally changed working file */
  svn_boolean_t text_conflict;

  /* This gets set if the file underwent a prop change, which guides
     the code that syncs up the adm dir and working copy. */
  svn_boolean_t prop_changed;

  /* This gets set if a "wc" prop was stored. */
  svn_boolean_t wcprop_changed;

  /* This gets set if an "entry" prop was stored. */
  svn_boolean_t entryprop_changed;

  /* This gets set if SVN_PROP_EOL_STYLE was set to some value other
     than 'none', and is -different- than the current value.   Also,
     the new eol string that should be used. */
  svn_boolean_t got_new_eol_style;
  enum svn_wc__eol_style new_style;
  svn_stringbuf_t *new_value;
  const char *new_eol;

  /* This gets set if there's a conflict when merging a prop-delta
     into the locally modified props.  */
  svn_boolean_t prop_conflict;

  /* An array of (svn_prop_t *)'s, representing all the property
     changes to be applied to this file. */
  apr_array_header_t *propchanges;

  /* An array of (svn_prop_t *)'s, representing all the "wc" property
     changes to be stored for this file. */
  apr_array_header_t *wcpropchanges;

  /* An array of (svn_prop_t *)'s, representing all the "entry"
     property changes to be stored for this file. */
  apr_array_header_t *entrypropchanges;

};


/* Make a file baton, using a new subpool of PARENT_DIR_BATON's pool.
   NAME is just one component, not a path. */
static struct file_baton *
make_file_baton (struct dir_baton *parent_dir_baton, svn_stringbuf_t *name)
{
  apr_pool_t *subpool = svn_pool_create (parent_dir_baton->pool);
  struct file_baton *f = apr_pcalloc (subpool, sizeof (*f));
  svn_stringbuf_t *path = svn_stringbuf_dup (parent_dir_baton->path,
                                       subpool);

  /* Make the file's on-disk name. */
  svn_path_add_component (path,
                          name,
                          svn_path_local_style);

  f->pool       = subpool;
  f->dir_baton  = parent_dir_baton;
  f->name       = name;
  f->path       = path;
  f->propchanges = apr_array_make (subpool, 1, sizeof(svn_prop_t *));
  f->wcpropchanges = apr_array_make (subpool, 1, sizeof(svn_prop_t *));
  f->entrypropchanges = apr_array_make (subpool, 1, sizeof(svn_prop_t *));

  parent_dir_baton->ref_count++;

  return f;
}


static svn_error_t *
free_file_baton (struct file_baton *fb)
{
  struct dir_baton *parent = fb->dir_baton;
  svn_pool_destroy (fb->pool);
  return decrement_ref_count (parent);
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
  if ((! fb->dir_baton->edit_baton->is_checkout) && hb->source)
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
      apr_pool_t *pool = svn_pool_create (fb->pool);
      svn_stringbuf_t *tmppath = svn_wc__text_base_path (fb->path, TRUE, pool);

      apr_file_remove (tmppath->data, pool);
      svn_pool_destroy (pool);
    }
  else
    {
      /* Leave a note in the baton indicating that there's new text to
         sync up.  */
      fb->text_changed = 1;
    }

  return err;
}


/* Prepare directory PATH for updating or checking out.
 *
 * If FORCE is non-zero, then the directory will definitely exist
 * after this call, else the directory must exist already.
 *
 * If the path already exists, but is not a working copy for
 * ANCESTOR_URL, then an error will be returned. 
 */
static svn_error_t *
prep_directory (svn_stringbuf_t *path,
                svn_stringbuf_t *ancestor_url,
                svn_revnum_t ancestor_revision,
                svn_boolean_t force,
                apr_pool_t *pool)
{
  svn_error_t *err;

  /* kff todo: how about a sanity check that it's not a dir of the
     same name from a different repository or something? 
     Well, that will be later on down the line... */

  if (force)   /* Make sure the directory exists. */
    {
      err = svn_wc__ensure_directory (path, pool);
      if (err)
        return err;
    }

  /* Make sure it's the right working copy, either by creating it so,
     or by checking that it is so already. */
  err = svn_wc__ensure_wc (path,
                           ancestor_url,
                           ancestor_revision,
                           pool);
  if (err)
    return err;

  return SVN_NO_ERROR;
}



/*** The callbacks we'll plug into an svn_delta_edit_fns_t structure. ***/

static svn_error_t *
set_target_revision (void *edit_baton, svn_revnum_t target_revision)
{
  struct edit_baton *eb = edit_baton;

  /* Stashing a target_revision in the baton */
  eb->target_revision = target_revision;
  return SVN_NO_ERROR;
}


static svn_error_t *
open_root (void *edit_baton,
           svn_revnum_t base_revision, /* This is ignored in co */
           void **dir_baton)
{
  struct edit_baton *eb = edit_baton;
  struct dir_baton *d;
  svn_error_t *err;
  svn_stringbuf_t *ancestor_url;
  svn_revnum_t ancestor_revision;

  *dir_baton = d = make_dir_baton (NULL, eb, NULL, FALSE, eb->pool);

  if (eb->is_checkout)
    {
      ancestor_url = eb->ancestor_url;
      ancestor_revision = eb->target_revision;
      
      err = prep_directory (d->path,
                            ancestor_url,
                            ancestor_revision,
                            1, /* force */
                            d->pool);
      if (err)
        return err;
    }

  return SVN_NO_ERROR;
}


static svn_error_t *
delete_entry (svn_stringbuf_t *name, svn_revnum_t revision, void *parent_baton)
{
  struct dir_baton *parent_dir_baton = parent_baton;
  apr_status_t apr_err;
  apr_file_t *log_fp = NULL;
  svn_stringbuf_t *log_item
    = svn_stringbuf_create ("", parent_dir_baton->pool);

  SVN_ERR (svn_wc__lock (parent_dir_baton->path, 0, parent_dir_baton->pool));
  SVN_ERR (svn_wc__open_adm_file (&log_fp,
                                  parent_dir_baton->path,
                                  SVN_WC__ADM_LOG,
                                  (APR_WRITE | APR_CREATE), /* not excl */
                                  parent_dir_baton->pool));
  svn_xml_make_open_tag (&log_item,
                         parent_dir_baton->pool,
                         svn_xml_self_closing,
                         SVN_WC__LOG_DELETE_ENTRY,
                         SVN_WC__LOG_ATTR_NAME,
                         name,
                         NULL);

  apr_err = apr_file_write_full (log_fp, log_item->data, log_item->len, NULL);
  if (apr_err)
    {
      apr_file_close (log_fp);
      return svn_error_createf (apr_err, 0, NULL, parent_dir_baton->pool,
                                "delete error writing %s's log file",
                                parent_dir_baton->path->data);
    }

  SVN_ERR (svn_wc__close_adm_file (log_fp,
                                   parent_dir_baton->path,
                                   SVN_WC__ADM_LOG,
                                   1, /* sync */
                                   parent_dir_baton->pool));
    
  SVN_ERR (svn_wc__run_log (parent_dir_baton->path, parent_dir_baton->pool));
  SVN_ERR (svn_wc__unlock (parent_dir_baton->path, parent_dir_baton->pool));
  return SVN_NO_ERROR;
}


static svn_error_t *
add_directory (svn_stringbuf_t *name,
               void *parent_baton,
               svn_stringbuf_t *copyfrom_path,
               svn_revnum_t copyfrom_revision,
               void **child_baton)
{
  svn_error_t *err;
  enum svn_node_kind kind;
  struct dir_baton *parent_dir_baton = parent_baton;

  /* Make a new dir baton for the new directory. */
  struct dir_baton *this_dir_baton
    = make_dir_baton (name,
                      parent_dir_baton->edit_baton,
                      parent_dir_baton,
                      TRUE,
                      parent_dir_baton->pool);

  /* Semantic check.  Either both "copyfrom" args are valid, or they're
     NULL and SVN_INVALID_REVNUM.  A mixture is illegal semantics. */
  if ((copyfrom_path && (! SVN_IS_VALID_REVNUM(copyfrom_revision)))
      || ((! copyfrom_path) && (SVN_IS_VALID_REVNUM(copyfrom_revision))))
    abort();
      
  /* Check that an object by this name doesn't already exist. */
  SVN_ERR (svn_io_check_path (this_dir_baton->path, &kind,
                              this_dir_baton->pool));
  if (kind != svn_node_none)
    return 
      svn_error_createf 
      (SVN_ERR_WC_OBSTRUCTED_UPDATE, 0, NULL, this_dir_baton->pool,
       "wc editor: add_dir `%s': object already exists and is in the way.",
       this_dir_baton->path->data);

  /* Either we got real copyfrom args... */
  if (copyfrom_path || SVN_IS_VALID_REVNUM(copyfrom_revision))
    {
      /* ### todo: for now, this editor doesn't know how to deal with
         copyfrom args.  Someday it will interpet them as an update
         optimization, and actually copy one part of the wc to another.
         Then it will recursively "normalize" all the ancestry in the
         copied tree.  Someday! */      
      return 
        svn_error_createf 
        (SVN_ERR_UNSUPPORTED_FEATURE, 0, NULL,
         parent_dir_baton->edit_baton->pool,
         "wc editor: add_dir `%s': sorry, I don't support copyfrom args yet.",
         name->data);
    }
  /* ...or we got invalid copyfrom args. */
  else
    {
      /* If the copyfrom args are both invalid, inherit the URL from the
         parent, and make the revision equal to the global target
         revision. */
      svn_stringbuf_t *new_URL;
      svn_wc_entry_t *parent_entry;
      SVN_ERR (svn_wc_entry (&parent_entry,
                             parent_dir_baton->path,
                             parent_dir_baton->pool));
      new_URL = svn_stringbuf_dup (parent_entry->url, this_dir_baton->pool);
      svn_path_add_component (new_URL, name, svn_path_url_style);

      copyfrom_path = new_URL;
      copyfrom_revision = parent_dir_baton->edit_baton->target_revision;      
    }

  /* Create dir (if it doesn't yet exist), make sure it's formatted
     with an administrative subdir.   */
  err = prep_directory (this_dir_baton->path,
                        copyfrom_path,
                        copyfrom_revision,
                        1, /* force */
                        this_dir_baton->pool);
  if (err)
    return (err);

  *child_baton = this_dir_baton;

  return SVN_NO_ERROR;
}


static svn_error_t *
open_directory (svn_stringbuf_t *name,
                void *parent_baton,
                svn_revnum_t base_revision,
                void **child_baton)
{
  struct dir_baton *parent_dir_baton = parent_baton;

  /* kff todo: check that the dir exists locally, find it somewhere if
     its not there?  Yes, all this and more...  And ancestor_url and
     ancestor_revision need to get used. */

  struct dir_baton *this_dir_baton
    = make_dir_baton (name,
                      parent_dir_baton->edit_baton,
                      parent_dir_baton,
                      FALSE,
                      parent_dir_baton->pool);

  *child_baton = this_dir_baton;

  return SVN_NO_ERROR;
}


static svn_error_t *
change_dir_prop (void *dir_baton,
                 svn_stringbuf_t *name,
                 svn_stringbuf_t *value)
{
  svn_stringbuf_t *local_name, *local_value;
  svn_prop_t *propchange, **receiver;
  struct dir_baton *db = dir_baton;

  /* Duplicate storage of name/value pair;  they should live in the
     dir baton's pool, not some pool within the editor's driver. :)
  */
  local_name = svn_stringbuf_dup (name, db->pool);
  if (value)
    /* remember that value could be NULL, signifying a property
       `delete' */
    local_value = svn_stringbuf_dup (value, db->pool);
  else
    local_value = NULL;

  /* If this is a 'wc' prop, store it in the administrative area and
     get on with life.  It's not a regular versioned property. */
  if (svn_wc_is_wc_prop (name))
    {
      svn_string_t value_struct;

      value_struct.data = value->data;
      value_struct.len = value->len;
      SVN_ERR (svn_wc__wcprop_set (name->data, &value_struct, db->path->data,
                                   db->pool));
      return SVN_NO_ERROR;
    }
  
  /* If this is an 'entry' prop, store it in the entries file and get
     on with life.  It's not a regular user property. */
  else if (svn_wc_is_entry_prop (name))
    {
      /* make a temporary hash */
      apr_pool_t *subpool = svn_pool_create (db->pool);
      apr_hash_t *att_hash = apr_hash_make (subpool);

      /* remove the 'svn:wc:entry:' prefix from the property name. */
      svn_wc__strip_entry_prefix (local_name);

      /* push the property into the att hash. */
      if (local_value)
        apr_hash_set (att_hash, local_name->data,
                      local_name->len, local_value);
      else
        apr_hash_set (att_hash, local_name->data,
                      local_name->len, svn_stringbuf_create ("", db->pool));

      /* write out the new attribute (via the hash) to the directory's
         THIS_DIR entry. */
      SVN_ERR (svn_wc__entry_modify (db->path, NULL,
                                     SVN_WC__ENTRY_MODIFY_ATTRIBUTES,
                                     SVN_INVALID_REVNUM, svn_node_none,
                                     svn_wc_schedule_normal, FALSE, FALSE,
                                     0, 0, NULL,
                                     att_hash,
                                     subpool, NULL));

      /* free whatever memory was used for this mini-operation */
      svn_pool_destroy (subpool);

      return SVN_NO_ERROR;
    }

  /* Else, it's a real ("normal") property... */

  /* Build propchange object */
  propchange = apr_pcalloc (db->pool, sizeof(*propchange));
  propchange->name = local_name;
  propchange->value = local_value;

  /* Push the object to the directory baton's array of propchanges */
  receiver = (svn_prop_t **) apr_array_push (db->propchanges);
  *receiver = propchange;

  /* Let close_dir() know that propchanges are waiting to be
     applied. */
  db->prop_changed = 1;

  return SVN_NO_ERROR;
}





static svn_error_t *
close_directory (void *dir_baton)
{
  struct dir_baton *db = dir_baton;
  svn_error_t *err = NULL;

  /* If this directory has property changes stored up, now is the time
     to deal with them. */
  if (db->prop_changed)
    {
      svn_boolean_t prop_modified;
      apr_hash_t *conflicts;
      apr_status_t apr_err;
      char *revision_str;
      apr_file_t *log_fp = NULL;

      /* to hold log messages: */
      svn_stringbuf_t *entry_accum = svn_stringbuf_create ("", db->pool);

      /* Lock down the administrative area */
      err = svn_wc__lock (db->path, 0, db->pool);
      if (err)
        return err;
      
      /* Open log file */
      err = svn_wc__open_adm_file (&log_fp,
                                   db->path,
                                   SVN_WC__ADM_LOG,
                                   (APR_WRITE | APR_CREATE), /* not excl */
                                   db->pool);
      if (err) return err;

      /* Merge pending properties into temporary files and detect
         conflicts. */
      err = svn_wc__do_property_merge (db->path, NULL,
                                       db->propchanges, db->pool,
                                       &entry_accum, &conflicts);
      if (err) 
        return 
          svn_error_quick_wrap (err, "close_dir: couldn't do prop merge.");

      /* Set revision. */
      revision_str = apr_psprintf (db->pool,
                                   "%ld",
                                   db->edit_baton->target_revision);
      
      /* Write a log entry to bump the directory's revision. */
      svn_xml_make_open_tag (&entry_accum,
                             db->pool,
                             svn_xml_self_closing,
                             SVN_WC__LOG_MODIFY_ENTRY,
                             SVN_WC__LOG_ATTR_NAME,
                             svn_stringbuf_create
                             (SVN_WC_ENTRY_THIS_DIR, db->pool),
                             SVN_WC_ENTRY_ATTR_REVISION,
                             svn_stringbuf_create (revision_str, db->pool),
                             NULL);


      /* Are the directory's props locally modified? */
      err = svn_wc_props_modified_p (&prop_modified,
                                     db->path,
                                     db->pool);
      if (err) return err;

      /* Log entry which sets a new property timestamp, but *only* if
         there are no local changes to the props. */
      if (! prop_modified)
        svn_xml_make_open_tag (&entry_accum,
                               db->pool,
                               svn_xml_self_closing,
                               SVN_WC__LOG_MODIFY_ENTRY,
                               SVN_WC__LOG_ATTR_NAME,
                               svn_stringbuf_create
                               (SVN_WC_ENTRY_THIS_DIR, db->pool),
                               SVN_WC_ENTRY_ATTR_PROP_TIME,
                               /* use wfile time */
                               svn_stringbuf_create (SVN_WC_TIMESTAMP_WC,
                                                  db->pool),
                               NULL);

      
      /* Write our accumulation of log entries into a log file */
      apr_err = apr_file_write_full (log_fp, entry_accum->data,
                                entry_accum->len, NULL);
      if (apr_err)
        {
          apr_file_close (log_fp);
          return svn_error_createf (apr_err, 0, NULL, db->pool,
                                    "close_dir: error writing %s's log file",
                                    db->path->data);
        }
      
      /* The log is ready to run, close it. */
      err = svn_wc__close_adm_file (log_fp,
                                    db->path,
                                    SVN_WC__ADM_LOG,
                                    1, /* sync */
                                    db->pool);
      if (err) return err;

      /* Run the log. */
      err = svn_wc__run_log (db->path, db->pool);
      if (err) return err;

      /* Unlock, we're done modifying directory props. */
      err = svn_wc__unlock (db->path, db->pool);
      if (err) return err;            
    }


  /* We're truly done with this directory now.  decrement_ref_count
  will actually destroy dir_baton if the ref count reaches zero, so we
  call this LAST. */
  err = decrement_ref_count (db);
  if (err)
    return err;

  return SVN_NO_ERROR;
}



/* Common code for add_file() and open_file(). */
static svn_error_t *
add_or_open_file (svn_stringbuf_t *name,
                  void *parent_baton,
                  svn_stringbuf_t *ancestor_url,
                  svn_revnum_t ancestor_revision,
                  void **file_baton,
                  svn_boolean_t adding)  /* 0 if replacing */
{
  struct dir_baton *parent_dir_baton = parent_baton;
  struct file_baton *fb;
  apr_hash_t *dirents;
  svn_error_t *err;
  apr_hash_t *entries = NULL;
  svn_wc_entry_t *entry;
  svn_boolean_t is_wc;

  /* ### kff todo: if file is marked as removed by user, then flag a
     conflict in the entry and proceed.  Similarly if it has changed
     kind.  see issuezilla task #398. */
  
  SVN_ERR (svn_io_get_dirents (&dirents, parent_dir_baton->path,
                               parent_dir_baton->pool));
  SVN_ERR (svn_wc_entries_read (&entries,
                                parent_dir_baton->path,
                                parent_dir_baton->pool));

  entry = apr_hash_get (entries, name->data, name->len);
  
  /* Sanity checks. */

  /* If adding, make sure there isn't already a disk entry here with the
     same name.  This error happen if either a) the user changed the
     filetype of the working file and ran 'update', or b) the
     update-driver is very confused. */
  if (adding && apr_hash_get (dirents, name->data, name->len))
    return svn_error_createf
      (SVN_ERR_WC_OBSTRUCTED_UPDATE, 0, NULL, parent_dir_baton->pool,
       "Can't add '%s':\n object of same name already exists in '%s'",
       name->data, parent_dir_baton->path->data);

  /* ben sez: If we're trying to add a file that's already in
     `entries' (but not on disk), that's okay.  It's probably because
     the user deleted the working version and ran 'svn up' as a means
     of getting the file back.  

     Or... perhaps the entry was removed and committed, leaving its
     existence == `deleted'.  The user may be updating to a revision
     where the entry exists again.

     Either way, it certainly doesn't hurt to re-add the file.  We
     can't possibly get the entry showing up twice in `entries', since
     it's a hash; and we know that we won't lose any local mods.  Let
     the existing entry be overwritten. */

  /* If replacing, make sure the .svn entry already exists. */
  if ((! adding) && (! entry))
    return svn_error_createf (SVN_ERR_WC_ENTRY_NOT_FOUND, 0, NULL,
                              parent_dir_baton->pool,
                              "trying to open non-versioned file "
                              "%s in directory %s",
                              name->data, parent_dir_baton->path->data);

        
  /* Make sure we've got a working copy to put the file in. */
  /* kff todo: need stricter logic here */
  err = svn_wc_check_wc (parent_dir_baton->path, &is_wc,
                         parent_dir_baton->pool);
  if (err)
    return err;
  else if (! is_wc)
    return svn_error_createf
      (SVN_ERR_WC_OBSTRUCTED_UPDATE, 0, NULL, parent_dir_baton->pool,
       "add_or_open_file: %s is not a working copy directory",
       parent_dir_baton->path->data);

  /* Set up the file's baton. */
  fb = make_file_baton (parent_dir_baton, name);
  *file_baton = fb;


  /* ### todo:  right now the incoming copyfrom* args are being
     completely ignored!  Someday the editor-driver may expect us to
     support this optimization;  when that happens, this func needs to
     -copy- the specified existing wc file to this location.  From
     there, the driver can apply_textdelta on it, etc. */

  return SVN_NO_ERROR;
}


static svn_error_t *
add_file (svn_stringbuf_t *name,
          void *parent_baton,
          svn_stringbuf_t *copyfrom_path,
          svn_revnum_t copyfrom_revision,
          void **file_baton)
{
  return add_or_open_file
    (name, parent_baton, copyfrom_path, copyfrom_revision, file_baton, 1);
}


static svn_error_t *
open_file (svn_stringbuf_t *name,
              void *parent_baton,
              svn_revnum_t base_revision,
              void **file_baton)
{
  return add_or_open_file
    (name, parent_baton, NULL, base_revision, file_baton, 0);
}


static svn_error_t *
apply_textdelta (void *file_baton, 
                 svn_txdelta_window_handler_t *handler,
                 void **handler_baton)
{
  struct file_baton *fb = file_baton;
  apr_pool_t *subpool = svn_pool_create (fb->pool);
  struct handler_baton *hb = apr_palloc (subpool, sizeof (*hb));
  svn_error_t *err;

  /* Open the text base for reading, unless this is a checkout. */
  hb->source = NULL;
  if (! fb->dir_baton->edit_baton->is_checkout)
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

      err = svn_wc__open_text_base (&hb->source, fb->path, APR_READ, subpool);
      if (err && !APR_STATUS_IS_ENOENT(err->apr_err))
        {
          if (hb->source)
            svn_wc__close_text_base (hb->source, fb->path, 0, subpool);
          svn_pool_destroy (subpool);
          return err;
        }
      else if (err)
        hb->source = NULL;  /* make sure */
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
                  svn_stringbuf_t *name,
                  svn_stringbuf_t *value)
{
  struct file_baton *fb = file_baton;
  svn_stringbuf_t *local_name, *local_value;
  svn_prop_t *propchange, **receiver;

  /* Duplicate storage of name/value pair;  they should live in the
     file baton's pool, not some pool within the editor's driver. :)
  */
  local_name = svn_stringbuf_dup (name, fb->pool);
  if (value)
    /* remember that value could be NULL, signifying a property
       `delete' */
    local_value = svn_stringbuf_dup (value, fb->pool);
  else
    local_value = NULL;

  /* Build propchange object */
  propchange = apr_pcalloc (fb->pool, sizeof(*propchange));
  propchange->name = local_name;
  propchange->value = local_value;

  /* If this is a 'wc' prop, store it in a different array. */
  if (svn_wc_is_wc_prop (name))
    {
      receiver = (svn_prop_t **) apr_array_push (fb->wcpropchanges);
      *receiver = propchange;
      
      fb->wcprop_changed = 1;
      return SVN_NO_ERROR;
    }
  
  /* If this is an 'entry' prop, store it a different array. */
  else if (svn_wc_is_entry_prop (name))
    {
      receiver = (svn_prop_t **) apr_array_push (fb->entrypropchanges);
      *receiver = propchange;
      
      fb->entryprop_changed = 1;
      return SVN_NO_ERROR;
    }

  /* Else, it's a normal property... */

  /* Push the object to the file baton's array of propchanges */
  receiver = (svn_prop_t **) apr_array_push (fb->propchanges);
  *receiver = propchange;

  /* Let close_file() know that propchanges are waiting to be
     applied. */
  fb->prop_changed = 1;

  /* If the eol-style was set, remember info for close_file(). */
  if (! strcmp (local_name->data, SVN_PROP_EOL_STYLE))
    {
      svn_wc__eol_style_from_value (&fb->new_style, &fb->new_eol, 
                                    value ? value->data : NULL);
      fb->got_new_eol_style = TRUE;
    }

  return SVN_NO_ERROR;
}


static svn_error_t *
close_file (void *file_baton)
{
  struct file_baton *fb = file_baton;
  apr_file_t *log_fp = NULL;
  svn_error_t *err;
  apr_status_t apr_err;
  char *revision_str = NULL;
  svn_stringbuf_t *entry_accum, *txtb, *tmp_txtb, *tmp_loc;
  svn_stringbuf_t *tmp_txtb_full_path, *txtb_full_path, *s_eol_str;
  svn_boolean_t has_binary_prop, is_locally_modified;
  apr_hash_t *prop_conflicts;
  enum svn_wc__eol_style eol_style;
  const char *eol_str;

  /* Lock the working directory while we change things. */
  SVN_ERR (svn_wc__lock (fb->dir_baton->path, 0, fb->pool));

  /*
     When we reach close_file() for file `F', the following are
     true:

         - The new pristine text of F, if any, is present in
           .svn/tmp/text-base/F.svn-base, and the file_baton->text_changed
           is set if necessary.

         - The new pristine props for F, if any, are present in
           the file_baton->propchanges array, and
           file_baton->prop_changed is set.

         - The .svn/entries file still reflects the old F.

         - .svn/text-base/F.svn-base is the old pristine F.

         - .svn/prop-base/F.svn-base is the old pristine F props.

      The goal is to update the local working copy of F to reflect
      the changes received from the repository, preserving any local
      modifications, in an interrupt-safe way.  So we first write our
      intentions to .svn/log, then run over the log file doing each
      operation in turn.  For a given operation, you can tell by
      inspection whether or not it has already been done; thus, those
      that have already been done are no-ops, and when we reach the
      end of the log file, we remove it.

      Because we must preserve local changes, the actual order of
      operations to update F is this:

         1. receive svndiff data D
         2. svnpatch .svn/text-base/F.svn-base < D >
            .svn/tmp/text-base/F.svn-base
         3. gdiff -c .svn/text-base/F.svn-base .svn/tmp/text-base/F.svn-base
            > .svn/tmp/F.blah.tmp
         4. cp .svn/tmp/text-base/F.svn-base .svn/text-base/F.svn-base
         5. gpatch F < .svn/tmp/F.tmpfile
              ==> possibly producing F.blah.rej

       Of course, newline-translation makes this a hair more complex.
       If we need to use 'native' newline style, then in step 3 above
       we generate the patch file by running gdiff on two *translated*
       copies of the old and new text-base.  This ensures that the
       patch file is in native EOL style as well, so it can be cleanly
       applied to F.

  */

  /* Open a log file.  This is safe because the adm area is locked
     right now. */
  SVN_ERR (svn_wc__open_adm_file (&log_fp,
                                  fb->dir_baton->path,
                                  SVN_WC__ADM_LOG,
                                  (APR_WRITE | APR_CREATE), /* not excl */
                                  fb->pool));

  /* Accumulate log commands in this buffer until we're ready to close
     and run the log.  */
  entry_accum = svn_stringbuf_create ("", fb->pool);

  /* We implement this matrix:

                  Text file                Binary File
               --------------------------------------------
    Local Mods |  run diff/patch   |  rename working file; | 
               |                   |  copy new file out.   |
               --------------------------------------------
    No Mods    |        Just overwrite working file.       |
               |                                           |
               ---------------------------------------------

   So the first thing we do is figure out where we are in the
   matrix. */

  /* MERGE ANY PROPERTY CHANGES, if they exist. */
  if (fb->prop_changed)
    {
      /* This will merge the old and new props into a new prop db, and
         write <cp> commands to the logfile to install the merged
         props. It also returns any conflicts to us in a hash. */
      err = svn_wc__do_property_merge (fb->dir_baton->path, fb->name,
                                       fb->propchanges, fb->pool,
                                       &entry_accum, &prop_conflicts);
      if (err) 
        return
          svn_error_quick_wrap (err, "close_file: couldn't do prop merge.");
    }


  if (fb->text_changed)
    {
      /* Text or Binary File?  Note that this is not a definitive test of
         whether the file is actually text or binary, just whether it has
         a mime-type that "marks" the file as binary. */
      SVN_ERR (svn_wc_has_binary_prop (&has_binary_prop, fb->path, fb->pool));
      
      /* Local textual mods?  (We can ignore local prop-mods;  those will
         be automagically merged later on in this routine.) */
      SVN_ERR (svn_wc_text_modified_p (&is_locally_modified,
                                       fb->path, fb->pool));

      /* Decide which value of eol-style to use.  This is complex! */
      {
        /* Did we get an eol-style during this update?
           If not, use whatever style is currently in our props. */        
        if (! fb->got_new_eol_style)
          SVN_ERR (svn_wc__get_eol_style (&eol_style, &eol_str,
                                          fb->path->data, fb->pool));      

        else  /* got a new eol-style from the server */
          {            
            /* Check to see if the new property conflicted. */
            svn_prop_t *conflict = 
              (svn_prop_t *) apr_hash_get (prop_conflicts, SVN_PROP_EOL_STYLE,
                                           strlen(SVN_PROP_EOL_STYLE));

            if (conflict)
              /* Use our current locally-modified style. */
              SVN_ERR (svn_wc__get_eol_style (&eol_style, &eol_str,
                                              fb->path->data, fb->pool));      
            else
              {
                /* Go ahead and use the new style from the repository. */
                eol_style = fb->new_style;
                eol_str = fb->new_eol;

                /* And also:  when we call svn_wc_text_modified_p()
                   below, it needs to know this new value.  So we'll
                   set this value immediately.  This is allowed,
                   because we know it won't conflict! */
                SVN_ERR (svn_wc_prop_set 
                         (svn_stringbuf_create (SVN_PROP_EOL_STYLE, fb->pool),
                          fb->new_value, fb->path, fb->pool));
              }
          }
        
        if (eol_str)
          s_eol_str = svn_stringbuf_create (eol_str, fb->pool);
        else
          s_eol_str = NULL;
      }

      /* ### check keyword property here. */

      /* Before doing any logic, we *know* that the first thing the
         logfile should do is overwrite the old text-base file with the
         newly received one in tmp/.  */
      tmp_txtb = svn_wc__text_base_path (fb->name, 1, fb->pool);
      txtb     = svn_wc__text_base_path (fb->name, 0, fb->pool);
      svn_xml_make_open_tag (&entry_accum,
                             fb->pool,
                             svn_xml_self_closing,
                             SVN_WC__LOG_MV,
                             SVN_WC__LOG_ATTR_NAME,
                             tmp_txtb,
                             SVN_WC__LOG_ATTR_DEST,
                             txtb,
                             NULL);

      if ( (! is_locally_modified)
           || (fb->dir_baton->edit_baton->is_checkout) )
        {
          /* If there are no local mods (or we're doing an initial
             checkout), who cares whether it's a text or binary file!
             Just overwrite any working file with the new one. */
          if (eol_style == svn_wc__eol_style_native)
            {
              /* CP command:  translate to 'native' EOL style. */
              svn_xml_make_open_tag (&entry_accum,
                                     fb->pool,
                                     svn_xml_self_closing,
                                     SVN_WC__LOG_CP,
                                     SVN_WC__LOG_ATTR_NAME,
                                     txtb,
                                     SVN_WC__LOG_ATTR_DEST,
                                     fb->name,
                                     SVN_WC__LOG_ATTR_EOL_STR,
                                     s_eol_str,
                                     NULL);
            }
          else
            /* regular CP command */
            svn_xml_make_open_tag (&entry_accum,
                                   fb->pool,
                                   svn_xml_self_closing,
                                   SVN_WC__LOG_CP,
                                   SVN_WC__LOG_ATTR_NAME,
                                   txtb,
                                   SVN_WC__LOG_ATTR_DEST,
                                   fb->name,
                                   NULL);
        }
  
      else   /* file is locally modified, and this is an update. */
        {
          if (! has_binary_prop)  /* type text */
            {
              enum svn_node_kind wfile_kind = svn_node_unknown;
              svn_stringbuf_t *received_diff_filename;
              svn_stringbuf_t *patch_cmd
                = svn_stringbuf_create (SVN_CLIENT_PATCH, fb->pool);
              apr_file_t *reject_file = NULL;
              svn_stringbuf_t *reject_filename = NULL;
              
              SVN_ERR (svn_io_check_path (fb->path, &wfile_kind, fb->pool));
              if (wfile_kind == svn_node_none)
                {
                  /* If the working file is missing, then just copy
                     the new base text to the working file. */
                  if (eol_style == svn_wc__eol_style_native)
                    {
                      /* CP command:  translate to 'native' EOL style. */
                      svn_xml_make_open_tag (&entry_accum,
                                             fb->pool,
                                             svn_xml_self_closing,
                                             SVN_WC__LOG_CP,
                                             SVN_WC__LOG_ATTR_NAME,
                                             txtb,
                                             SVN_WC__LOG_ATTR_DEST,
                                             fb->name,
                                             SVN_WC__LOG_ATTR_EOL_STR,
                                             s_eol_str,
                                             NULL);
                    }
                  else
                    /* regular CP command */
                    svn_xml_make_open_tag (&entry_accum,
                                           fb->pool,
                                           svn_xml_self_closing,
                                           SVN_WC__LOG_CP,
                                           SVN_WC__LOG_ATTR_NAME,
                                           txtb,
                                           SVN_WC__LOG_ATTR_DEST,
                                           fb->name,
                                           NULL);
                }
              else  /* working file exists */
                {                  
                  /* Run the external `diff' command immediately and
                     create a temporary patch.  Note that we -always-
                     create the patchfile by diffing two LF versions
                     of our old and new textbases.   */
                  const char *diff_args[2];                  
                  apr_file_t *received_diff_file;
                  apr_file_t *tr_txtb_fp, *tr_tmp_txtb_fp;
                  svn_stringbuf_t *tr_txtb, *tr_tmp_txtb;
                  
                  /* Reserve a filename for the patchfile we'll create. */
                  tmp_loc
                    = svn_wc__adm_path (fb->dir_baton->path, 1, fb->pool, 
                                        fb->name->data, NULL);
                  SVN_ERR (svn_io_open_unique_file (&received_diff_file,
                                                    &received_diff_filename,
                                                    tmp_loc,
                                                    SVN_WC__DIFF_EXT,
                                                    FALSE,
                                                    fb->pool));
                  
                  /* Reserve filenames for temporary LF-converted textbases. */
                  tmp_txtb_full_path
                    = svn_wc__text_base_path (fb->path, 1, fb->pool);
                  txtb_full_path
                    = svn_wc__text_base_path (fb->path, 0, fb->pool);
                  
                  SVN_ERR (svn_io_open_unique_file (&tr_txtb_fp,
                                                    &tr_txtb,
                                                    tmp_loc, SVN_WC__BASE_EXT,
                                                    FALSE, fb->pool));
                  SVN_ERR (svn_io_open_unique_file (&tr_tmp_txtb_fp,
                                                    &tr_tmp_txtb,
                                                    tmp_loc, SVN_WC__BASE_EXT,
                                                    FALSE, fb->pool));

                  /* Copy *LF-translated* text-base files to these
                     reserved locations. */
                  SVN_ERR (svn_io_copy_and_translate (txtb_full_path->data,
                                                      tr_txtb->data,
                                                      "\n",
                                                      1, NULL, NULL, 
                                                      NULL, NULL, 
                                                      TRUE, /* ### expand? */
                                                      fb->pool));
                  
                  SVN_ERR (svn_io_copy_and_translate (tmp_txtb_full_path->data,
                                                      tr_tmp_txtb->data,
                                                      eol_str,
                                                      1, NULL, NULL,
                                                      NULL, NULL, 
                                                      TRUE, /* ### expand? */
                                                      fb->pool));

                  /* Build the diff command. */
                  diff_args[0] = "-c";
                  diff_args[1] = "--";

                  SVN_ERR(svn_io_run_diff
                    (".", diff_args, 2, NULL, tr_txtb->data, tr_tmp_txtb->data, 
                     NULL, received_diff_file, NULL, fb->pool));

                  /* Write log commands to remove the two tmp text-bases. */
                  
                  /* (gack, we need the paths to be relative to log's
                     working directory)  */ 
                  tr_txtb = svn_stringbuf_ncreate 
                    (tr_txtb->data + fb->dir_baton->path->len + 1,
                     tr_txtb->len - fb->dir_baton->path->len - 1,
                     fb->pool);

                  tr_tmp_txtb = svn_stringbuf_ncreate 
                    (tr_tmp_txtb->data + fb->dir_baton->path->len + 1,
                     tr_tmp_txtb->len - fb->dir_baton->path->len - 1,
                     fb->pool);
                  
                  svn_xml_make_open_tag (&entry_accum,
                                         fb->pool,
                                         svn_xml_self_closing,
                                         SVN_WC__LOG_RM,
                                         SVN_WC__LOG_ATTR_NAME,
                                         tr_txtb,
                                         NULL);

                  svn_xml_make_open_tag (&entry_accum,
                                         fb->pool,
                                         svn_xml_self_closing,
                                         SVN_WC__LOG_RM,
                                         SVN_WC__LOG_ATTR_NAME,
                                         tr_tmp_txtb,
                                         NULL);

                  /* Great, swell.  When we get here, we are
                     guaranteed to have a patchfile between the old
                     and new textbases, in LF format.  What we -do-
                     with that patchfile depends on the eol-style property. */
                  
                  /* Get the reject file ready. */
                  /* kff todo: code dup with above, abstract it? */
                  SVN_ERR (svn_io_open_unique_file (&reject_file,
                                                    &reject_filename,
                                                    fb->path,
                                                    SVN_WC__TEXT_REJ_EXT,
                                                    FALSE,
                                                    fb->pool));
                  apr_err = apr_file_close (reject_file);
                  if (apr_err)
                    return svn_error_createf (apr_err, 0, NULL, fb->pool,
                                              "close_file: error closing %s",
                                              reject_filename->data);

                  /* Paths need to be relative to the working dir that uses
                     this log file, so we chop the prefix.
                     
                     kff todo: maybe this should be abstracted into
                     svn_path_whatever, but it's so simple I'm inclined not
                     to.  On the other hand, the +1/-1's are for slashes, and
                     technically only svn_path should know such dirty details.
                     On the third hand, whatever the separator char is, it's
                     still likely to be one char, so the code would work even
                     if it weren't slash.
                     
                     Sometimes I think I think too much.  I think. */ 
                  reject_filename = svn_stringbuf_ncreate
                    (reject_filename->data + fb->dir_baton->path->len + 1,
                     reject_filename->len - fb->dir_baton->path->len - 1,
                     fb->pool);
                  
                  received_diff_filename = svn_stringbuf_ncreate
                    (received_diff_filename->data
                     + fb->dir_baton->path->len + 1,
                     received_diff_filename->len
                     - fb->dir_baton->path->len - 1,
                     fb->pool);

                  
                  if (eol_style == svn_wc__eol_style_none)
                    {
                      /* If the eol property is turned off, then just
                         apply the LF patchfile directly to the
                         working file.  No big deal. */

                      /* kff todo: these options will have to be
                         portablized too.  Even if we know we're doing a
                         plaintext patch, not all patch programs support
                         these args. */
                      svn_xml_make_open_tag
                        (&entry_accum, fb->pool, svn_xml_self_closing,
                         SVN_WC__LOG_RUN_CMD,
                         SVN_WC__LOG_ATTR_NAME,
                         patch_cmd,
                         SVN_WC__LOG_ATTR_ARG_1,
                         svn_stringbuf_create ("-r", fb->pool),
                         SVN_WC__LOG_ATTR_ARG_2,
                         reject_filename,
                         SVN_WC__LOG_ATTR_ARG_3,
                         svn_stringbuf_create ("-B.#", fb->pool),
                         SVN_WC__LOG_ATTR_ARG_4,
                         svn_stringbuf_create ("--", fb->pool),
                         SVN_WC__LOG_ATTR_ARG_5,
                         fb->name,
                         SVN_WC__LOG_ATTR_INFILE,
                         received_diff_filename,
                         NULL);
                    }
                  else  /* eol_style is native or fixed */
                    {
                      apr_file_t *tmp_fp;
                      svn_stringbuf_t *tmp_working;

                      /* Reserve a temporary working file. */
                      SVN_ERR (svn_io_open_unique_file (&tmp_fp,
                                                        &tmp_working,
                                                        tmp_loc,
                                                        SVN_WC__TMP_EXT,
                                                        FALSE,
                                                        fb->pool));

                      /* Copy the working file to tmp-working with LF's.*/

                      /* note: pass the repair flag.  if the
                         locally-modified working file has mixed EOL
                         style, we *should* be doing a non-reversible
                         normalization, because the eol prop is set,
                         and an update is a 'checkpoint' just like a
                         commit. */
                      svn_xml_make_open_tag (&entry_accum, fb->pool,
                                             svn_xml_self_closing,
                                             SVN_WC__LOG_CP,
                                             SVN_WC__LOG_ATTR_NAME,
                                             fb->name,
                                             SVN_WC__LOG_ATTR_DEST,
                                             tmp_working,
                                             SVN_WC__LOG_ATTR_EOL_STR,
                                             svn_stringbuf_create ("\n",
                                                                   fb->pool),
                                             SVN_WC__LOG_ATTR_REPAIR,
                                             svn_stringbuf_create ("true",
                                                                   fb->pool),
                                             NULL);

                      /* Now patch the tmp-working file. */
                      svn_xml_make_open_tag
                        (&entry_accum, fb->pool, svn_xml_self_closing,
                         SVN_WC__LOG_RUN_CMD,
                         SVN_WC__LOG_ATTR_NAME,
                         patch_cmd,
                         SVN_WC__LOG_ATTR_ARG_1,
                         svn_stringbuf_create ("-r", fb->pool),
                         SVN_WC__LOG_ATTR_ARG_2,
                         reject_filename,
                         SVN_WC__LOG_ATTR_ARG_3,
                         svn_stringbuf_create ("-B.#", fb->pool),
                         SVN_WC__LOG_ATTR_ARG_4,
                         svn_stringbuf_create ("--", fb->pool),
                         SVN_WC__LOG_ATTR_ARG_5,
                         tmp_working,
                         SVN_WC__LOG_ATTR_INFILE,
                         received_diff_filename, NULL);

                      /* We already know that the latest eol-style
                         must be either 'native' or 'fixed', and is
                         already defined in eol_str.  Therefore, copy
                         the merged tmp_working back to working with
                         this style. */
                        svn_xml_make_open_tag (&entry_accum,
                                               fb->pool,
                                               svn_xml_self_closing,
                                               SVN_WC__LOG_CP,
                                               SVN_WC__LOG_ATTR_NAME,
                                               tmp_working,
                                               SVN_WC__LOG_ATTR_DEST,
                                               fb->name,
                                               SVN_WC__LOG_ATTR_EOL_STR,
                                               s_eol_str, NULL);

                        /* Remove tmp_working. */
                        svn_xml_make_open_tag (&entry_accum,
                                               fb->pool,
                                               svn_xml_self_closing,
                                               SVN_WC__LOG_RM,
                                               SVN_WC__LOG_ATTR_NAME,
                                               tmp_working, NULL);
                        
                    } /* end:  eol-style is native or fixed */

                  /* Remove the patchfile. */
                  svn_xml_make_open_tag (&entry_accum,
                                         fb->pool,
                                         svn_xml_self_closing,
                                         SVN_WC__LOG_RM,
                                         SVN_WC__LOG_ATTR_NAME,
                                         received_diff_filename,
                                         NULL);

                  /* Remove the reject file that patch will have used,
                     IFF the reject file is empty (zero bytes) --
                     implying that there was no conflict.  If the
                     reject file is nonzero, then mark the entry as
                     conflicted!  Yes, this is a complex log
                     command. :-) */
                  svn_xml_make_open_tag (&entry_accum,
                                         fb->pool,
                                         svn_xml_self_closing,
                                         SVN_WC__LOG_DETECT_CONFLICT,
                                         SVN_WC__LOG_ATTR_NAME,
                                         fb->name,
                                         SVN_WC_ENTRY_ATTR_REJFILE,
                                         reject_filename,
                                         NULL);                  

                } /* end: working file exists */
            } /* end:  file is type text */

          else  /* file is marked as binary */
            {
              apr_file_t *renamed_fp;
              svn_stringbuf_t *renamed_path, *renamed_basename;
              
              /* Rename the working file. */
              SVN_ERR (svn_io_open_unique_file (&renamed_fp,
                                                &renamed_path,
                                                fb->path,
                                                ".orig",
                                                FALSE,
                                                fb->pool));
              apr_err = apr_file_close (renamed_fp);
              if (apr_err)
                return svn_error_createf (apr_err, 0, NULL, fb->pool,
                                          "close_file: error closing %s",
                                          renamed_path->data);
              
              renamed_basename
                = svn_path_last_component (renamed_path,
                                           svn_path_local_style, fb->pool);

              svn_xml_make_open_tag (&entry_accum,
                                     fb->pool,
                                     svn_xml_self_closing,
                                     SVN_WC__LOG_CP,
                                     SVN_WC__LOG_ATTR_NAME,
                                     fb->name,
                                     SVN_WC__LOG_ATTR_DEST,
                                     renamed_basename,
                                     NULL);
              
              /* Copy the new file out into working area. */
              svn_xml_make_open_tag (&entry_accum,
                                     fb->pool,
                                     svn_xml_self_closing,
                                     SVN_WC__LOG_CP,
                                     SVN_WC__LOG_ATTR_NAME,
                                     txtb,
                                     SVN_WC__LOG_ATTR_DEST,
                                     fb->name,
                                     NULL);
            }
        }
      
    }  /* End  of "textual" merging process */
  

  /* If there are any ENTRY PROPS, make sure those get appended to the
     growing log as fields for the file's entry. */
  if (fb->entryprop_changed)
    {
      int i;

      /* foreach entry prop... */
      for (i = 0; i < fb->entrypropchanges->nelts; i++)
        {
          svn_prop_t *prop;
          prop = (((svn_prop_t **)(fb->entrypropchanges)->elts)[i]);

          /* strip the 'svn:wc:entry:' prefix from the property name. */
          svn_wc__strip_entry_prefix (prop->name);

          /* append a command to the log which will append the
             property as a entry attribute. */
          svn_xml_make_open_tag (&entry_accum,
                                 fb->pool,
                                 svn_xml_self_closing,
                                 SVN_WC__LOG_MODIFY_ENTRY,
                                 SVN_WC__LOG_ATTR_NAME,
                                 fb->name,
                                 prop->name->data,
                                 prop->value ? 
                                      prop->value : 
                                      svn_stringbuf_create ("", fb->pool),
                                 NULL);         
        }
    }

  /* Set revision. */
  revision_str = apr_psprintf (fb->pool,
                              "%ld",
                              fb->dir_baton->edit_baton->target_revision);

  /* Write log entry which will bump the revision number:  */
  svn_xml_make_open_tag (&entry_accum,
                         fb->pool,
                         svn_xml_self_closing,
                         SVN_WC__LOG_MODIFY_ENTRY,
                         SVN_WC__LOG_ATTR_NAME,
                         fb->name,
                         SVN_WC_ENTRY_ATTR_KIND,
                         svn_stringbuf_create (SVN_WC__ENTRIES_ATTR_FILE_STR, 
                                            fb->pool),
                         SVN_WC_ENTRY_ATTR_REVISION,
                         svn_stringbuf_create (revision_str, fb->pool),
                         NULL);

  if (fb->text_changed)
    {
      /* Log entry which sets a new textual timestamp, but only if
         there are no local changes to the text. */
      if (! is_locally_modified)
        svn_xml_make_open_tag (&entry_accum,
                               fb->pool,
                               svn_xml_self_closing,
                               SVN_WC__LOG_MODIFY_ENTRY,
                               SVN_WC__LOG_ATTR_NAME,
                               fb->name,
                               SVN_WC_ENTRY_ATTR_TEXT_TIME,
                               /* use wfile time */
                               svn_stringbuf_create (SVN_WC_TIMESTAMP_WC,
                                                  fb->pool),
                               NULL);
    }

  if (fb->prop_changed)
    {
      svn_boolean_t prop_modified;

      /* Are the working file's props locally modified? */
      err = svn_wc_props_modified_p (&prop_modified,
                                     fb->path,
                                     fb->pool);
      if (err) return err;

      /* Log entry which sets a new property timestamp, but only if
         there are no local changes to the props. */
      if (! prop_modified)
        svn_xml_make_open_tag (&entry_accum,
                               fb->pool,
                               svn_xml_self_closing,
                               SVN_WC__LOG_MODIFY_ENTRY,
                               SVN_WC__LOG_ATTR_NAME,
                               fb->name,
                               SVN_WC_ENTRY_ATTR_PROP_TIME,
                               /* use wfile time */
                               svn_stringbuf_create (SVN_WC_TIMESTAMP_WC,
                                                  fb->pool),
                               NULL);
    }

  /* Write our accumulation of log entries into a log file */
  apr_err = apr_file_write_full (log_fp, entry_accum->data, 
                                 entry_accum->len, NULL);
  if (apr_err)
    {
      apr_file_close (log_fp);
      return svn_error_createf (apr_err, 0, NULL, fb->pool,
                                "close_file: error writing %s's log file",
                                fb->path->data);
    }

  /* The log is ready to run, close it. */
  err = svn_wc__close_adm_file (log_fp,
                                fb->dir_baton->path,
                                SVN_WC__ADM_LOG,
                                1, /* sync */
                                fb->pool);
  if (err)
    return err;

  /* Run the log. */
  err = svn_wc__run_log (fb->dir_baton->path, fb->pool);
  if (err)
    return err;

  /* Dump any stored-up "wc" props, now that the file really exists. */
  if (fb->wcprop_changed)
    {
      int i;
      for (i = 0; i < fb->wcpropchanges->nelts; i++)
        {
          svn_prop_t *prop;
          svn_string_t value_struct;

          prop = (((svn_prop_t **)(fb->wcpropchanges)->elts)[i]);
          value_struct.data = prop->value->data;
          value_struct.len = prop->value->len;
          SVN_ERR (svn_wc__wcprop_set (prop->name->data, &value_struct,
                                       fb->path->data, fb->pool));
        }
    }

  /* Unlock, we're done with this whole file-update. */
  err = svn_wc__unlock (fb->dir_baton->path, fb->pool);
  if (err)
    return err;

  /* Tell the directory it has one less thing to worry about. */
  err = free_file_baton (fb);
  if (err)
    return err;

  return SVN_NO_ERROR;
}


static svn_error_t *
close_edit (void *edit_baton)
{
  struct edit_baton *eb = edit_baton;
  
  /* By definition, anybody "driving" this editor for update purposes
     at a *minimum* must have called set_target_revision() at the
     outset, and close_edit() at the end -- even if it turned out that
     no changes ever had to be made, and open_root() was never
     called.  That's fine.  But regardless, when the edit is over,
     this editor needs to make sure that *all* paths have had their
     revisions bumped to the new target revision. */

  if (! eb->is_checkout)  
    {
      /* checkouts already have a uniform wc revision; only updates
         need this bumping, and only directory updates at that.
         Updated files should already be up-to-date. */
      svn_wc_entry_t *entry;
      svn_stringbuf_t *full_path = svn_stringbuf_dup (eb->anchor, eb->pool);
      if (eb->target)
        svn_path_add_component (full_path, eb->target,
                                svn_path_local_style);
      SVN_ERR (svn_wc_entry (&entry, full_path, eb->pool));
      if (entry->kind == svn_node_dir)
        SVN_ERR (svn_wc__ensure_uniform_revision (full_path,
                                                  eb->target_revision,
                                                  eb->recurse,
                                                  eb->pool));
    }

  /* The edit is over, free its pool. */
  svn_pool_destroy (eb->pool);
    
  return SVN_NO_ERROR;
}



/*** Returning editors. ***/

/* Helper for the two public editor-supplying functions. */
static svn_error_t *
make_editor (svn_stringbuf_t *anchor,
             svn_stringbuf_t *target,
             svn_revnum_t target_revision,
             svn_boolean_t is_checkout,
             svn_stringbuf_t *ancestor_url,
             svn_boolean_t recurse,
             const svn_delta_edit_fns_t **editor,
             void **edit_baton,
             apr_pool_t *pool)
{
  struct edit_baton *eb;
  apr_pool_t *subpool = svn_pool_create (pool);
  svn_delta_edit_fns_t *tree_editor = svn_delta_default_editor (pool);

  if (is_checkout)
    assert (ancestor_url != NULL);

  /* Construct an edit baton. */
  eb = apr_palloc (subpool, sizeof (*eb));
  eb->pool            = subpool;
  eb->is_checkout     = is_checkout;
  eb->target_revision = target_revision;
  eb->ancestor_url    = ancestor_url;
  eb->anchor          = anchor;
  eb->target          = target;
  eb->recurse         = recurse;

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
svn_wc_get_update_editor (svn_stringbuf_t *anchor,
                          svn_stringbuf_t *target,
                          svn_revnum_t target_revision,
                          svn_boolean_t recurse,
                          const svn_delta_edit_fns_t **editor,
                          void **edit_baton,
                          apr_pool_t *pool)
{
  return make_editor (anchor, target, target_revision, FALSE, NULL, recurse,
                      editor, edit_baton, pool);
}


svn_error_t *
svn_wc_get_checkout_editor (svn_stringbuf_t *dest,
                            svn_stringbuf_t *ancestor_url,
                            svn_revnum_t target_revision,
                            svn_boolean_t recurse,
                            const svn_delta_edit_fns_t **editor,
                            void **edit_baton,
                            apr_pool_t *pool)
{
  return make_editor (dest, NULL, target_revision, TRUE, ancestor_url, recurse,
                      editor, edit_baton, pool);
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
   for deleting that directory and replacing it with a file (this
   would be like having an editor now anchored on a file, which is
   disallowed).

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

svn_error_t *
svn_wc_is_wc_root (svn_boolean_t *wc_root,
                   svn_stringbuf_t *path,
                   apr_pool_t *pool)
{
  svn_stringbuf_t *parent, *basename, *expected_url;
  svn_wc_entry_t *p_entry, *entry;
  svn_error_t *err;

  /* Go ahead and initialize our return value to the most common
     (code-wise) values. */
  *wc_root = TRUE;

  /* Get our ancestry (this doubles as a sanity check).  */
  SVN_ERR (svn_wc_entry (&entry, path, pool));
  if (! entry)
    return svn_error_createf 
      (SVN_ERR_WC_ENTRY_NOT_FOUND, 0, NULL, pool,
       "svn_wc_is_wc_root: %s is not a versioned resource", path->data);

  /* If PATH is the current working directory, we have no choice but
     to consider it a WC root (we can't examine its parent at all) */
  if (svn_path_is_empty (path, svn_path_local_style))
    return SVN_NO_ERROR;

  /* If we cannot get an entry for PATH's parent, PATH is a WC root. */
  svn_path_split (path, &parent, &basename, svn_path_local_style, pool);
  if (svn_path_is_empty (parent, svn_path_local_style))
    svn_stringbuf_set (parent, ".");
  err = svn_wc_entry (&p_entry, parent, pool);
  if (err || ! p_entry)
    return SVN_NO_ERROR;
  
  /* If the parent directory has no url information, something is
     messed up.  Bail with an error. */
  if (! p_entry->url)
    return svn_error_createf 
      (SVN_ERR_WC_ENTRY_MISSING_URL, 0, NULL, pool,
       "svn_wc_is_wc_root: %s has no ancestry information.", 
       parent->data);

  /* If PATH's parent in the WC is not its parent in the repository,
     PATH is a WC root. */
  expected_url = svn_stringbuf_dup (p_entry->url, pool);
  svn_path_add_component (expected_url, basename, svn_path_url_style);
  if (entry->url && (! svn_stringbuf_compare (expected_url, entry->url)))
    return SVN_NO_ERROR;

  /* If we have not determined that PATH is a WC root by now, it must
     not be! */
  *wc_root = FALSE;
  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc_get_actual_target (svn_stringbuf_t *path,
                          svn_stringbuf_t **anchor,
                          svn_stringbuf_t **target,
                          apr_pool_t *pool)
{
  svn_boolean_t wc_root;

  /* If PATH is a WC root, do not lop off a basename. */
  SVN_ERR (svn_wc_is_wc_root (&wc_root, path, pool));
  if (wc_root)
    {
      *anchor = svn_stringbuf_dup (path, pool);
      *target = NULL;
    }
  else
    {
      svn_path_split (path, anchor, target, svn_path_local_style, pool);
      if (svn_path_is_empty (*anchor, svn_path_local_style))
        svn_stringbuf_set (*anchor, ".");
    }

  return SVN_NO_ERROR;
}


/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end: 
 */

