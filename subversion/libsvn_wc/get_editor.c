/*
 * get_editor.c :  routines for update and checkout
 *
 * ================================================================
 * Copyright (c) 2000 CollabNet.  All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 * 
 * 1. Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 * 
 * 2. Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 * 
 * 3. The end-user documentation included with the redistribution, if
 * any, must include the following acknowlegement: "This product includes
 * software developed by CollabNet (http://www.Collab.Net)."
 * Alternately, this acknowlegement may appear in the software itself, if
 * and wherever such third-party acknowlegements normally appear.
 * 
 * 4. The hosted project names must not be used to endorse or promote
 * products derived from this software without prior written
 * permission. For written permission, please contact info@collab.net.
 * 
 * 5. Products derived from this software may not use the "Tigris" name
 * nor may "Tigris" appear in their names without prior written
 * permission of CollabNet.
 * 
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESSED OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL COLLABNET OR ITS CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * ====================================================================
 * 
 * This software consists of voluntary contributions made by many
 * individuals on behalf of CollabNet.
 */



#include <stdio.h>       /* temporary, for printf() */
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <apr_pools.h>
#include <apr_hash.h>
#include <apr_file_io.h>
#include <apr_strings.h>
#include "svn_types.h"
#include "svn_delta.h"
#include "svn_string.h"
#include "svn_path.h"
#include "svn_xml.h"
#include "svn_error.h"
#include "svn_io.h"
#include "svn_hash.h"
#include "svn_wc.h"

#include "wc.h"



/*** batons ***/

struct edit_baton
{
  svn_string_t *dest_dir;
  svn_vernum_t target_version;

  /* These used only in checkouts. */
  svn_boolean_t is_checkout;
  svn_string_t *ancestor_path;
  svn_string_t *repository;

  apr_pool_t *pool;
};


struct dir_baton
{
  /* The path to this directory. */
  svn_string_t *path;

  /* Basename of this directory. */
  svn_string_t *name;

  /* The number of other changes associated with this directory in the
     delta (typically, the number of files being changed here, plus
     this dir itself).  BATON->ref_count starts at 1, is incremented
     for each entity being changed, and decremented for each
     completion of one entity's changes.  When the ref_count is 0, the
     directory may be safely set to the target version, and this baton
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

  /* The pool in which this baton itself is allocated. */
  apr_pool_t *pool;
};


struct handler_baton
{
  apr_file_t *source;
  apr_file_t *dest;
  svn_txdelta_window_handler_t *apply_handler;
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
make_dir_baton (svn_string_t *name,
                struct edit_baton *edit_baton,
                struct dir_baton *parent_baton,
                apr_pool_t *pool)
{
  apr_pool_t *subpool = svn_pool_create (pool);
  struct dir_baton *d = apr_pcalloc (subpool, sizeof (*d));
  svn_string_t *parent_path
    = parent_baton ? parent_baton->path : edit_baton->dest_dir;
  svn_string_t *path = svn_string_dup (parent_path, subpool);

  if (name)
    svn_path_add_component (path, name, svn_path_local_style);

  d->path         = path;
  d->edit_baton   = edit_baton;
  d->parent_baton = parent_baton;
  d->ref_count    = 1;
  d->pool         = subpool;

  if (name)
    d->name = svn_string_dup (name, subpool);

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

  /* Bump this dir to the new version. */
  err = svn_wc__entry_merge_sync (dir_baton->path,
                                  NULL,
                                  dir_baton->edit_baton->target_version,
                                  svn_dir_kind,
                                  0,
                                  0,
                                  dir_baton->pool,
                                  NULL);
   if (err)
     return err;

  /* After we destroy DIR_BATON->pool, DIR_BATON itself is lost. */
  apr_destroy_pool (dir_baton->pool);

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
  const svn_string_t *name;

  /* Path to this file, either abs or relative to the change-root. */
  svn_string_t *path;

  /* This gets set if the file underwent a text change, which guides
     the code that syncs up the adm dir and working copy. */
  svn_boolean_t text_changed;

  /* This gets set if there's a conflict while merging the
     repository's file into the locally changed working file */
  svn_boolean_t text_conflict;

  /* This gets set if the file underwent a prop change, which guides
     the code that syncs up the adm dir and working copy. */
  svn_boolean_t prop_changed;

  /* This gets set if there's a conflict when merging a prop-delta
     into the locally modified props.  */
  svn_boolean_t prop_conflict;

  /* Hashes containing the file's latest properies; these hashes are
     edited in-memory as a prop-delta is received.  Of course, if we
     ever decide to make properties "streamy", these hashes will be
     the first ones up against the wall.  :) */
  apr_hash_t *localprops;
  apr_hash_t *baseprops;

};


/* Make a file baton, using a new subpool of PARENT_DIR_BATON's pool.
   NAME is just one component, not a path. */
static struct file_baton *
make_file_baton (struct dir_baton *parent_dir_baton, svn_string_t *name)
{
  apr_pool_t *subpool = svn_pool_create (parent_dir_baton->pool);
  struct file_baton *f = apr_pcalloc (subpool, sizeof (*f));
  svn_string_t *path = svn_string_dup (parent_dir_baton->path,
                                       subpool);

  /* Make the file's on-disk name. */
  svn_path_add_component (path,
                          name,
                          svn_path_local_style);

  f->pool       = subpool;
  f->dir_baton  = parent_dir_baton;
  f->name       = name;
  f->path       = path;
  f->baseprops  = NULL;
  f->localprops = NULL;  /* Don't initialize these hashes unless we start
                            receiving a prop-delta later on. */

  parent_dir_baton->ref_count++;

  return f;
}


static svn_error_t *
free_file_baton (struct file_baton *fb)
{
  struct dir_baton *parent = fb->dir_baton;
  apr_destroy_pool (fb->pool);
  return decrement_ref_count (parent);
}



/*** Helpers for the editor callbacks. ***/

static svn_error_t *
read_from_file (void *baton, char *buffer, apr_size_t *len, apr_pool_t *pool)
{
  apr_file_t *fp = baton;
  apr_status_t status;

  if (fp == NULL)
    {
      *len = 0;
      return SVN_NO_ERROR;
    }
  status = apr_full_read (fp, buffer, *len, len);
  if (status && (status != APR_EOF))
    return svn_error_create (status, 0, NULL, pool, "Can't read base file");
  return SVN_NO_ERROR;
}


static svn_error_t *
write_to_file (void *baton, const char *data, apr_size_t *len,
               apr_pool_t *pool)
{
  apr_file_t *fp = baton;
  apr_status_t status;

  status = apr_full_write (fp, data, *len, len);
  if (status)
    return svn_error_create (status, 0, NULL, pool,
                             "Can't write new base file");
  return SVN_NO_ERROR;
}


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
  apr_destroy_pool (hb->pool);

  if (err != SVN_NO_ERROR)
    {
      /* We failed to apply the patch; clean up the temporary file.  */
      apr_pool_t *pool = svn_pool_create (fb->pool);
      svn_string_t *tmppath = svn_wc__text_base_path (fb->path, TRUE, pool);

      apr_remove_file (tmppath->data, pool);
      apr_destroy_pool (pool);
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
 * DIRECTORY, then an error will be returned. 
 */
static svn_error_t *
prep_directory (svn_string_t *path,
                svn_string_t *repository,
                svn_string_t *ancestor_path,
                svn_vernum_t ancestor_version,
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
                           repository,
                           ancestor_path,
                           ancestor_version,
                           pool);
  if (err)
    return err;

  return SVN_NO_ERROR;
}



/*** The callbacks we'll plug into an svn_delta_edit_fns_t structure. ***/

static svn_error_t *
replace_root (void *edit_baton,
              void **dir_baton)
{
  struct edit_baton *eb = edit_baton;
  struct dir_baton *d;
  svn_error_t *err;
  svn_string_t *ancestor_path;
  svn_vernum_t ancestor_version;

  *dir_baton = d = make_dir_baton (NULL, eb, NULL, eb->pool);

  if (eb->is_checkout)
    {
      ancestor_path = eb->ancestor_path;
      ancestor_version = eb->target_version;
      
      err = prep_directory (d->path,
                            eb->repository,
                            ancestor_path,
                            ancestor_version,
                            1, /* force */
                            d->pool);
      if (err)
        return err;
    }

  return SVN_NO_ERROR;
}


static svn_error_t *
delete (svn_string_t *name, void *parent_baton)
{
  svn_error_t *err;
  struct dir_baton *parent_dir_baton = parent_baton;
  apr_status_t apr_err;
  apr_file_t *log_fp = NULL;
  svn_string_t *log_item = svn_string_create ("", parent_dir_baton->pool);

  err = svn_wc__lock (parent_dir_baton->path, 0, parent_dir_baton->pool);
  if (err)
    return err;

    err = svn_wc__open_adm_file (&log_fp,
                                 parent_dir_baton->path,
                                 SVN_WC__ADM_LOG,
                                 (APR_WRITE | APR_CREATE), /* not excl */
                                 parent_dir_baton->pool);
    if (err)
      return err;

    svn_xml_make_open_tag (&log_item,
                           parent_dir_baton->pool,
                           svn_xml_self_closing,
                           SVN_WC__LOG_DELETE_ENTRY,
                           SVN_WC__LOG_ATTR_NAME,
                           name,
                           NULL);

    apr_err = apr_full_write (log_fp, log_item->data, log_item->len, NULL);
    if (apr_err)
      {
        apr_close (log_fp);
        return svn_error_createf (apr_err, 0, NULL, parent_dir_baton->pool,
                                  "delete error writing %s's log file",
                                  parent_dir_baton->path->data);
      }

    err = svn_wc__close_adm_file (log_fp,
                                  parent_dir_baton->path,
                                  SVN_WC__ADM_LOG,
                                  1, /* sync */
                                  parent_dir_baton->pool);
    if (err)
      return err;
    
    err = svn_wc__run_log (parent_dir_baton->path, parent_dir_baton->pool);
    if (err)
      return err;

    err = svn_wc__unlock (parent_dir_baton->path, parent_dir_baton->pool);
    if (err)
      return err;

  return SVN_NO_ERROR;
}


static svn_error_t *
add_directory (svn_string_t *name,
               void *parent_baton,
               svn_string_t *ancestor_path,
               svn_vernum_t ancestor_version,
               void **child_baton)
{
  svn_error_t *err;
  struct dir_baton *parent_dir_baton = parent_baton;

  struct dir_baton *this_dir_baton
    = make_dir_baton (name,
                      parent_dir_baton->edit_baton,
                      parent_dir_baton,
                      parent_dir_baton->pool);

  /* Notify the parent that this child dir exists.  This can happen
     right away, there is no need to wait until the child is done. */
  err = svn_wc__entry_merge_sync (parent_dir_baton->path,
                                  this_dir_baton->name,
                                  SVN_INVALID_VERNUM,
                                  svn_dir_kind,
                                  0,
                                  0,
                                  parent_dir_baton->pool,
                                  NULL);
  if (err)
    return err;


  err = prep_directory (this_dir_baton->path,
                        this_dir_baton->edit_baton->repository,
                        ancestor_path,
                        ancestor_version,
                        1, /* force */
                        this_dir_baton->pool);
  if (err)
    return (err);

  *child_baton = this_dir_baton;

  return SVN_NO_ERROR;
}


static svn_error_t *
replace_directory (svn_string_t *name,
                   void *parent_baton,
                   svn_string_t *ancestor_path,
                   svn_vernum_t ancestor_version,
                   void **child_baton)
{
  struct dir_baton *parent_dir_baton = parent_baton;

  /* kff todo: check that the dir exists locally, find it somewhere if
     its not there?  Yes, all this and more...  And ancestor_path and
     ancestor_version need to get used. */

  struct dir_baton *this_dir_baton
    = make_dir_baton (name,
                      parent_dir_baton->edit_baton,
                      parent_dir_baton,
                      parent_dir_baton->pool);

  *child_baton = this_dir_baton;

  return SVN_NO_ERROR;
}


static svn_error_t *
change_dir_prop (void *dir_baton,
                 svn_string_t *name,
                 svn_string_t *value)
{
  struct dir_baton *this_dir_baton = dir_baton;

  this_dir_baton->prop_changed = 1;

  /* kff todo */
  return SVN_NO_ERROR;
}


static svn_error_t *
close_directory (void *dir_baton)
{
  struct dir_baton *this_dir_baton = dir_baton;
  svn_error_t *err = NULL;

  err = decrement_ref_count (this_dir_baton);
  if (err)
    return err;

  /* kff todo: now that the child is finished, we should make an entry
     in the parent's base-tree (although frankly I'm beginning to
     wonder if child directories should be recorded anywhere but in
     themselves; perhaps that would be best, and just let the parent
     deduce their existence.  We can still tell when an update of the
     parent is complete, by refcounting.) */

  return SVN_NO_ERROR;
}


/* Common code for add_file() and replace_file(). */
static svn_error_t *
add_or_replace_file (svn_string_t *name,
                     void *parent_baton,
                     svn_string_t *ancestor_path,
                     svn_vernum_t ancestor_version,
                     void **file_baton,
                     svn_boolean_t adding)  /* 0 if replacing */
{
  struct dir_baton *parent_dir_baton = parent_baton;
  struct file_baton *fb;
  svn_error_t *err;
  apr_hash_t *entries = NULL;
  svn_wc__entry_t *entry;

  err = svn_wc__entries_read (&entries,
                              parent_dir_baton->path,
                              parent_dir_baton->pool);
  if (err)
    return err;

  entry = apr_hash_get (entries, name->data, name->len);

  /* Sanity checks. */
  if (adding && entry)
    return svn_error_createf (SVN_ERR_WC_ENTRY_EXISTS, 0, NULL,
                              parent_dir_baton->pool,
                              "trying to add versioned file "
                              "%s in directory %s",
                              name, parent_dir_baton->path);
  else if ((! adding) && (! entry))
    return svn_error_createf (SVN_ERR_WC_ENTRY_NOT_FOUND, 0, NULL,
                              parent_dir_baton->pool,
                              "trying to replace non-versioned file "
                              "%s in directory %s",
                              name, parent_dir_baton->path);

        
  /* Make sure we've got a working copy to put the file in. */
  /* kff todo: need stricter logic here */
  err = svn_wc__check_wc (parent_dir_baton->path, parent_dir_baton->pool);
  if (err)
    return err;

  /* Set up the file's baton. */
  fb = make_file_baton (parent_dir_baton, name);
  *file_baton = fb;

  return SVN_NO_ERROR;
}


static svn_error_t *
add_file (svn_string_t *name,
          void *parent_baton,
          svn_string_t *ancestor_path,
          svn_vernum_t ancestor_version,
          void **file_baton)
{
  return add_or_replace_file
    (name, parent_baton, ancestor_path, ancestor_version, file_baton, 1);
}


static svn_error_t *
replace_file (svn_string_t *name,
              void *parent_baton,
              svn_string_t *ancestor_path,
              svn_vernum_t ancestor_version,
              void **file_baton)
{
  return add_or_replace_file
    (name, parent_baton, ancestor_path, ancestor_version, file_baton, 0);
}


static svn_error_t *
apply_textdelta (void *file_baton, 
                 svn_txdelta_window_handler_t **handler,
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
         2. See if it's under version control.
         3. If both are true, open text-base.
         4. If only 1 is true, bail, because we can't go destroying
            user's files (or as an alternative to bailing, move it to
            some tmp name and somehow tell the user, but communicating
            with the user without erroring is a whole callback system
            we haven't finished inventing yet.)
      */

      err = svn_wc__open_text_base (&hb->source, fb->path, APR_READ, subpool);
      if (err && (err->apr_err != APR_ENOENT))
        {
          if (hb->source)
            svn_wc__close_text_base (hb->source, fb->path, 0, subpool);
          apr_destroy_pool (subpool);
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
      apr_destroy_pool (subpool);
      return err;
    }
  
  /* Prepare to apply the delta.  */
  svn_txdelta_apply (read_from_file, hb->source, write_to_file, hb->dest,
                     subpool, &hb->apply_handler, &hb->apply_baton);
  
  hb->pool = subpool;
  hb->fb = fb;
  
  /* We're all set.  */
  *handler_baton = hb;
  *handler = window_handler;

  return SVN_NO_ERROR;
}


/* If PROPFILE_PATH exists and is a file, assume it's full of
   properties and load this file into HASH.  Otherwise, leave HASH
   untouched.  */
static svn_error_t *
load_prop_file (svn_string_t *propfile_path,
                apr_hash_t *hash,
                apr_pool_t *pool)
{
  svn_error_t *err;
  enum svn_node_kind kind;

  err = svn_io_check_path (propfile_path, &kind, pool);
  if (err) return err;
  
  if (kind == svn_file_kind)
    {
      /* Ah, this file already has on-disk properties.  Load 'em. */
      apr_status_t status;
      apr_file_t *propfile = NULL;
            
      status = apr_open (&propfile, propfile_path->data,
                         APR_READ, APR_OS_DEFAULT, pool);
      if (status)
        return svn_error_createf (status, 0, NULL, pool,
                                  "load_prop_file: can't open `%s'",
                                  propfile_path->data);
      
      status = svn_hash_read (hash, svn_pack_bytestring,
                              propfile, pool);
      if (status)
        return svn_error_createf (status, 0, NULL, pool,
                                  "load_prop_file:  can't parse `%s'",
                                  propfile_path->data);
      
      status = apr_close (propfile);
      if (status)
        return svn_error_createf (status, 0, NULL, pool,
                                  "load_prop_file: can't close `%s'",
                                  propfile_path->data);
    }
  
  return SVN_NO_ERROR;
}


/* Given a HASH full of property name/values, write them to a file
   located at PROPFILE_PATH */
static svn_error_t *
save_prop_file (svn_string_t *propfile_path,
                apr_hash_t *hash,
                apr_pool_t *pool)
{
  apr_status_t apr_err;
  apr_file_t *prop_tmp;

  apr_err = apr_open (&prop_tmp, propfile_path->data,
                      (APR_WRITE | APR_CREATE),
                      APR_OS_DEFAULT, pool);
  if (apr_err)
    return svn_error_createf (apr_err, 0, NULL, pool,
                              "save_prop_file: can't open `%s'",
                              propfile_path->data);

  apr_err = svn_hash_write (hash, svn_unpack_bytestring,
                            prop_tmp);
  if (apr_err)
    return svn_error_createf (apr_err, 0, NULL, pool,
                              "save_prop_file: can't write prop hash to `%s'",
                              propfile_path->data);

  apr_err = apr_close (prop_tmp);
  if (apr_err)
    return svn_error_createf (apr_err, 0, NULL, pool,
                              "save_prop_file: can't close `%s'",
                              propfile_path->data);

  return SVN_NO_ERROR;
}



static svn_error_t *
change_file_prop (void *file_baton,
                  svn_string_t *name,
                  svn_string_t *value)
{
  svn_error_t *err;
  struct file_baton *fb = file_baton;

  /* Create the file's in-memory prophashes if they don't yet exist */
  if (! fb->baseprops)
    {
      svn_string_t *pristine_propfile_path;
      svn_string_t *working_propfile_path;
  
      fb->baseprops = apr_make_hash (fb->pool);
      fb->localprops = apr_make_hash (fb->pool);

      pristine_propfile_path = svn_wc__adm_path (fb->dir_baton->path,
                                                 0, /* not tmp */
                                                 fb->pool,
                                                 SVN_WC__ADM_PROP_BASE,
                                                 fb->name,
                                                 NULL);

      working_propfile_path = svn_wc__adm_path (fb->dir_baton->path,
                                                0, /* not tmp */
                                                fb->pool,
                                                SVN_WC__ADM_PROPS,
                                                fb->name,
                                                NULL);

      err = load_prop_file (pristine_propfile_path,
                            fb->baseprops, fb->pool);
      if (err) return err;

      err = load_prop_file (working_propfile_path,
                            fb->localprops, fb->pool);
      if (err) return err;
    }

  /* Add the new {name, value} to the hashes.  This is exactly the
     correct behavior of our <set> and <delete> tags : if the name
     doesn't yet exist, now it does.  If it already exists, it will be
     overwritten.  If value is NULL, the key is removed altogether.
     How fortuituous that these behaviors align so perfectly.  ;) */
  apr_hash_set (fb->baseprops, name->data, name->len, value);
  
  /* With our `local' properties, we want to signal conflicts on a
     merge, so we should first check to see if the hash key exists.  */
  {
    void *val = apr_hash_get (fb->localprops, name->data, name->len);
    if (val)
      fb->prop_conflict = 1;
  }
  /* Now do the conflicting merge anyway. */
  apr_hash_set (fb->localprops, name->data, name->len, value);

  /* Let close_file() know to write a future log entry about this: */
  fb->prop_changed = 1;

  return SVN_NO_ERROR;
}


static svn_error_t *
close_file (void *file_baton)
{
  struct file_baton *fb = file_baton;
  apr_file_t *log_fp = NULL;
  svn_string_t *base_prop_tmp_path;
  svn_string_t *local_prop_tmp_path;
  svn_error_t *err;
  apr_status_t apr_err;
  void *local_changes;
  char *version_str = NULL;
  svn_string_t *entry_accum;

  err = svn_wc__lock (fb->dir_baton->path, 0, fb->pool);
  if (err)
    return err;

  /* kff todo: if we return before unlocking, which is possible below,
     that is probably badness... */

  /* kff todo:
     
     Okay, let's plan this whole diff/log/update/merge thing a bit
     better, in the cold light of morning, as it were (actually, it's
     early afternoon, but I'm told that's hacker virtual morning).

     When we reach close_file() for file `blah', the following are
     true:

         - The new pristine text of blah, if any, is present in
           SVN/tmp/text-base/blah; and the file_baton is appropriately
           marked if so.

         - The new pristine props for blah, if any, are present inside
           the file_baton's `properties' hash; and the file_baton is
           appropriately marked if so.

         - The SVN/entries file still reflects the old blah.

         - And SVN/text-base/blah is the old pristine blah, too.

      The goal is to update the local working copy of blah to reflect
      the changes received from the repository, preserving any local
      modifications, in an interrupt-safe way.  So we first write our
      intentions to SVN/log, then run over the log file doing each
      operation in turn.  For a given operation, you can always tell
      whether or not it has already been done; thus, those that have
      already been done are ignored, and when we reach the end of the
      log file, we remove it.

      Because we must preserve local changes, the actual order of
      operations is this:

         0. Write file_baton->properties to SVN/tmp/prop

         1. Discover and save local mods (right now, this means do a
            GNU diff -c on ./SVN/text-base/blah vs ./blah, and save
            the result somewhere).

         2. Write out the following SVN/log entries, omitting any that
            aren't applicable of course:

              <merge-text name="blah" saved-mods="..."/>
                 <!-- Will attempt to merge local changes into the new
                      text.  When done, ./blah will reflect the new
                      state, either by having the changes folded in,
                      having them folded in with conflict markers, or
                      not having them folded in (in which case the
                      user is told that no merge was possible).  Yes,
                      this means that the working file is updated
                      *before* its text-base, but that's okay, because
                      the updating of the text-base will already be
                      logged by the time any of this runs, so it's "as
                      good as done".  -->
              <replace-text-base name="blah"/>
                  <!-- Now that the merge step is done, it's safe to
                       replace the old pristine copy with the new,
                       updated one, copying `./SVN/tmp/text-base/blah'
                       to `./SVN/text-base/blah' -->
              <merge-props name="blah">
                  <!-- This really just detects and warns about
                       conflicts between local prop changes and
                       received prop changes.  I'm not sure merging is
                       really applicable here. -->
              <replace-prop-base name="blah"/>
                  <!-- You know what to do. -->
              <set-entry name="blah" version="N"/>
                  <!-- Once everything else is done, we can set blah's
                       entry to version N, changing the ./SVN/entries
                       file. -->
         
         3. Now run over the log file, doing each operation.  Note
            that if an operation appears to have already been done,
            that means it _was_ done, so just count it and move on.
            When all entries have been done, the operation is
            complete, so remove SVN/log.
            
  */

  /* Save local mods. */
  err = svn_wc__get_local_changes (svn_wc__gnudiff_differ,
                                   &local_changes,
                                   fb->path,
                                   fb->pool);
  if (err)
    return err;

  /** Write out the appropriate log entries. 
      This is safe because the adm area is locked right now. **/ 
      
  err = svn_wc__open_adm_file (&log_fp,
                               fb->dir_baton->path,
                               SVN_WC__ADM_LOG,
                               (APR_WRITE | APR_CREATE), /* not excl */
                               fb->pool);
  if (err)
    return err;

  /* kff todo: save *local_changes somewhere, maybe to a tmp file
     in SVN/. */
  
  entry_accum = svn_string_create ("", fb->pool);

  if (fb->text_changed)
    {
      /* Merge text. */
      svn_xml_make_open_tag (&entry_accum,
                             fb->pool,
                             svn_xml_self_closing,
                             SVN_WC__LOG_MERGE_TEXT,
                             SVN_WC__LOG_ATTR_NAME,
                             fb->name,
                             SVN_WC__LOG_ATTR_SAVED_MODS,
                             svn_string_create ("kff todo", fb->pool),
                             NULL);
      
      /* Replace text base. */
      svn_xml_make_open_tag (&entry_accum,
                             fb->pool,
                             svn_xml_self_closing,
                             SVN_WC__LOG_REPLACE_TEXT_BASE,
                             SVN_WC__LOG_ATTR_NAME,
                             fb->name,
                             NULL);
    }
  
  if (fb->prop_changed)
    {
      /* Write the merged pristine prop hash to a file in SVN/tmp/prop-base/ */
      base_prop_tmp_path = svn_wc__adm_path (fb->dir_baton->path,
                                             TRUE, /* tmp area */
                                             fb->pool,
                                             SVN_WC__ADM_PROP_BASE,
                                             fb->name,
                                             NULL);
      
      err = save_prop_file (base_prop_tmp_path, fb->baseprops, fb->pool);
      if (err) return err;
      
      /* Write the merged local prop hash to a file in SVN/tmp/props/ */
      local_prop_tmp_path = svn_wc__adm_path (fb->dir_baton->path,
                                              TRUE, /* tmp area */
                                              fb->pool,
                                              SVN_WC__ADM_PROPS,
                                              fb->name,
                                              NULL);
      
      err = save_prop_file (local_prop_tmp_path, fb->localprops, fb->pool);
      if (err) return err;
      
      /* Merge props. */
      svn_xml_make_open_tag (&entry_accum,
                             fb->pool,
                             svn_xml_self_closing,
                             SVN_WC__LOG_MERGE_PROPS,
                             SVN_WC__LOG_ATTR_NAME,
                             fb->name,
                             NULL);
      
      /* Replace prop base. */
      svn_xml_make_open_tag (&entry_accum,
                             fb->pool,
                             svn_xml_self_closing,
                             SVN_WC__LOG_REPLACE_PROP_BASE,
                             SVN_WC__LOG_ATTR_NAME,
                             fb->name,
                             NULL);
    }

  /* Set version. */
  version_str = apr_psprintf (fb->pool,
                              "%d",
                              fb->dir_baton->edit_baton->target_version);

  svn_xml_make_open_tag (&entry_accum,
                         fb->pool,
                         svn_xml_self_closing,
                         SVN_WC__LOG_SET_VERSION,
                         SVN_WC__LOG_ATTR_NAME,
                         fb->name,
                         SVN_WC__LOG_ATTR_VERSION,
                         svn_string_create (version_str, fb->pool),
                         NULL);

  apr_err = apr_full_write (log_fp, entry_accum->data, entry_accum->len, NULL);
  if (apr_err)
    {
      apr_close (log_fp);
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

  /* The edit is over, free its pool. */
  apr_destroy_pool (eb->pool);

  /* kff todo:  Wow.  Is there _anything_ else that needs to be done? */

  return SVN_NO_ERROR;
}



/*** Returning editors. ***/

static const svn_delta_edit_fns_t tree_editor =
{
  replace_root,
  delete,
  add_directory,
  replace_directory,
  change_dir_prop,
  close_directory,
  add_file,
  replace_file,
  apply_textdelta,
  change_file_prop,
  close_file,
  close_edit
};


/* Helper for the two public editor-supplying functions. */
static svn_error_t *
make_editor (svn_string_t *dest,
             svn_vernum_t target_version,
             svn_boolean_t is_checkout,
             svn_string_t *repos,
             svn_string_t *ancestor_path,
             const svn_delta_edit_fns_t **editor,
             void **edit_baton,
             apr_pool_t *pool)
{
  struct edit_baton *eb;
  apr_pool_t *subpool = svn_pool_create (pool);

  /* Else nothing in the way, so continue. */

  *editor = &tree_editor;

  if (is_checkout)
    {
      assert (ancestor_path != NULL);
      assert (repos != NULL);
    }

  eb = apr_palloc (subpool, sizeof (*eb));
  eb->dest_dir       = dest;
  eb->pool           = subpool;
  eb->is_checkout    = is_checkout;
  eb->ancestor_path  = ancestor_path;
  eb->repository     = repos;
  eb->target_version = target_version;

  *edit_baton = eb;

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc_get_update_editor (svn_string_t *dest,
                          svn_vernum_t target_version,
                          const svn_delta_edit_fns_t **editor,
                          void **edit_baton,
                          apr_pool_t *pool)
{
  return
    make_editor (dest, target_version,
                 0, NULL, NULL,
                 editor, edit_baton, pool);
}


svn_error_t *
svn_wc_get_checkout_editor (svn_string_t *dest,
                            svn_string_t *repos,
                            svn_string_t *ancestor_path,
                            svn_vernum_t target_version,
                            const svn_delta_edit_fns_t **editor,
                            void **edit_baton,
                            apr_pool_t *pool)
{
  return make_editor (dest, target_version,
                      1, repos, ancestor_path,
                      editor, edit_baton, pool);
}



/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
