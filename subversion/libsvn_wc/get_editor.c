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
#include <apr_pools.h>
#include <apr_hash.h>
#include <apr_file_io.h>
#include <apr_strings.h>
#include "svn_types.h"
#include "svn_delta.h"
#include "svn_string.h"
#include "svn_path.h"
#include "svn_error.h"
#include "svn_hash.h"
#include "svn_wc.h"

#include "wc.h"



/*** batons ***/

struct edit_baton
{
  svn_string_t *dest_dir;
  svn_string_t *repository;
  svn_vernum_t target_version;
  apr_pool_t *pool;
};


struct dir_baton
{
  /* The path to this directory. */
  svn_string_t *path;

  /* The number of other changes associated with this directory in the
     delta (typically, the number of files being changed here, plus
     this dir itself).  BATON->ref_count starts at 1, is incremented
     for each entity being changed, and decremented for each
     completion of one entity's changes.  When the ref_count is 0, the
     directory may be safely set to the target version, and this baton
     freed. */
  int ref_count;

  /* The global edit baton. */
  /* kff todo: suspect we may never use this, remove it if so. */
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
  apr_pool_t *subpool = svn_pool_create (pool, NULL);
  struct dir_baton *d = apr_pcalloc (subpool, sizeof (*d));
  svn_string_t *parent_path
    = parent_baton ? parent_baton->path : edit_baton->dest_dir;
  svn_string_t *path = svn_string_dup (parent_path, subpool);

  if (name)
    svn_path_add_component (path, name, SVN_PATH_LOCAL_STYLE, subpool);

  d->path         = path;
  d->edit_baton   = edit_baton;
  d->parent_baton = parent_baton;
  d->ref_count    = 1;
  d->pool         = subpool;

  return d;
}


static svn_error_t *
free_dir_baton (struct dir_baton *dir_baton)
{
  /* Do whatever cleanup is needed on PATH with EDIT_BATON. */

  /* kff todo fooo working here */

  /* After we destroy DIR_BATON->pool, DIR_BATON itself is lost. */
  apr_destroy_pool (dir_baton->pool);

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

  /* This gets set if the file underwent a prop change, which guides
     the code that syncs up the adm dir and working copy. */
  svn_boolean_t prop_changed;
};


/* Make a file baton, using a new subpool of PARENT_DIR_BATON's pool.
   NAME is just one component, not a path. */
static struct file_baton *
make_file_baton (struct dir_baton *parent_dir_baton, svn_string_t *name)
{
  apr_pool_t *subpool = svn_pool_create (parent_dir_baton->pool, NULL);
  struct file_baton *f = apr_pcalloc (subpool, sizeof (*f));
  svn_string_t *path = svn_string_dup (parent_dir_baton->path,
                                       subpool);

  /* Make the file's on-disk name. */
  svn_path_add_component (path,
                          name,
                          SVN_PATH_LOCAL_STYLE,
                          subpool);

  f->pool       = subpool;
  f->dir_baton  = parent_dir_baton;
  f->name       = name;
  f->path       = path;

  return f;
}



/*** Helpers for the editor callbacks. ***/

static svn_error_t *
window_handler (svn_txdelta_window_t *window, void *baton)
{
  int i;
  struct file_baton *fb = (struct file_baton *) baton;
  apr_file_t *dest = NULL;  /* always init to null for APR */
  svn_error_t *err = NULL;

  /* kff todo: get more sophisticated when we can handle more ops. */
  err = svn_wc__open_text_base (&dest,
                                fb->path,
                                (APR_WRITE | APR_APPEND | APR_CREATE),
                                window->pool);
  if (err)
    return err;
  
  /* else */

  for (i = 0; i < window->num_ops; i++)
    {
      svn_txdelta_op_t this_op = (window->ops)[i];
      switch (this_op.action_code)
        {
        case svn_txdelta_source:
          /* todo */
          break;

        case svn_txdelta_target:
          /* todo */
          break;

        case svn_txdelta_new:
          {
            apr_status_t apr_err;
            apr_size_t written;
            const char *data = ((svn_string_t *) (window->new))->data;

            printf ("%.*s", (int) this_op.length, (data + this_op.offset));
            apr_err = apr_full_write (dest, (data + this_op.offset),
                                      this_op.length, &written);
            if (apr_err)
              return svn_error_create (apr_err, 0, NULL,
                                       window->pool, fb->path->data);

            break;
          }
        }
    }

  /* Close the file after each window, but pass 0 so it stays in the
     tmp area.  When close_file() is called it will take care of
     syncing it back into the real location. */
  err = svn_wc__close_text_base (dest, fb->path, 0, window->pool);
  if (err)
    return err;

  /* Leave a note in the baton indicating that there's new text to
     sync up. */
  fb->text_changed = 1;
  
  return SVN_NO_ERROR;
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

  printf ("%s\n", path->data);

  return SVN_NO_ERROR;
}



/*** The callbacks we'll plug into an svn_delta_edit_fns_t structure. ***/

static svn_error_t *
replace_root (svn_string_t *ancestor_path,
              svn_vernum_t ancestor_version,
              void *edit_baton,
              void **dir_baton)
{
  struct edit_baton *eb = (struct edit_baton *) edit_baton;
  struct dir_baton *d;
  svn_error_t *err;

  *dir_baton = d = make_dir_baton (NULL, eb, NULL, eb->pool);

  err = prep_directory (d->path,
                        eb->repository,
                        ancestor_path,
                        ancestor_version,
                        1, /* force */
                        d->pool);
  if (err)
    return (err);

  return SVN_NO_ERROR;
}


static svn_error_t *
delete (svn_string_t *name, void *parent_baton)
{
#if 0
  struct dir_baton *parent_dir_baton = (struct dir_baton *) parent_baton;
#endif /* 0 */

  /* kff todo */

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
  struct dir_baton *parent_dir_baton = (struct dir_baton *) parent_baton;

  struct dir_baton *this_dir_baton
    = make_dir_baton (name,
                      parent_dir_baton->edit_baton,
                      parent_dir_baton,
                      parent_dir_baton->pool);

  /* kff todo urgent: need to also let the parent know this new
     subdirectory exists! */

  parent_dir_baton->ref_count++;
  *child_baton = this_dir_baton;

  err = prep_directory (this_dir_baton->path,
                        this_dir_baton->edit_baton->repository,
                        ancestor_path,
                        ancestor_version,
                        1, /* force */
                        this_dir_baton->pool);
  if (err)
    return (err);

  return SVN_NO_ERROR;
}


static svn_error_t *
replace_directory (svn_string_t *name,
                   void *parent_baton,
                   svn_string_t *ancestor_path,
                   svn_vernum_t ancestor_version,
                   void **child_baton)
{
#if 0
  struct dir_baton *parent_dir_baton = (struct dir_baton *) parent_baton;
#endif /* 0 */

  /* kff todo */

  return SVN_NO_ERROR;
}


static svn_error_t *
change_dir_prop (void *dir_baton,
                 svn_string_t *name,
                 svn_string_t *value)
{
  struct dir_baton *this_dir_baton = (struct dir_baton *) dir_baton;

  this_dir_baton->prop_changed = 1;

  /* kff todo */
  return SVN_NO_ERROR;
}


static svn_error_t *
change_dirent_prop (void *dir_baton,
                    svn_string_t *entry,
                    svn_string_t *name,
                    svn_string_t *value)
{
#if 0
  struct dir_baton *this_dir_baton = (struct dir_baton *) dir_baton;
#endif /* 0 */

  /* kff todo */
  return SVN_NO_ERROR;
}


static svn_error_t *
close_directory (void *dir_baton)
{
  struct dir_baton *this_dir_baton = (struct dir_baton *) dir_baton;
  svn_error_t *err = NULL;

  err = decrement_ref_count (this_dir_baton);

  /* kff todo: now that the child is finished, we should make an entry
     in the parent's base-tree (although frankly I'm beginning to
     wonder if child directories should be recorded anywhere but in
     themselves; perhaps that would be best, and just let the parent
     deduce their existence.  We can still tell when an update of the
     parent is complete, by refcounting.) */

  return err;
}


static svn_error_t *
add_file (svn_string_t *name,
          void *parent_baton,
          svn_string_t *ancestor_path,
          svn_vernum_t ancestor_version,
          void **file_baton)
{
  struct dir_baton *parent_dir_baton = (struct dir_baton *) parent_baton;
  struct file_baton *fb;
  svn_error_t *err;

  /* kff todo fooo: this should go away once we can guarantee a call
     either to {add,replace}_directory() or start_edit() before the
     first add_file(). */
  {
    /* fooo: nothing can happen until the first dir_baton is taken
       care of, fix callers to make this happen */
  }

  /* Make sure we've got a working copy to put the file in. */
  err = svn_wc__check_wc (parent_dir_baton->path, parent_dir_baton->pool);
  if (err)
    return err;

  /* Okay, looks like we're good to go. */

  /* kff todo urgent: check all calls you know what I mean. */
  fb = make_file_baton (parent_dir_baton, name);
  parent_dir_baton->ref_count++;
  *file_baton = fb;

  printf ("%s\n   ", fb->path->data);

  return SVN_NO_ERROR;
}


static svn_error_t *
replace_file (svn_string_t *name,
              void *parent_baton,
              svn_string_t *ancestor_path,
              svn_vernum_t ancestor_version,
              void **file_baton)
{
  struct dir_baton *parent_dir_baton = (struct dir_baton *) parent_baton;
  svn_error_t *err = NULL;

  /* Replacing is mostly like adding... */
  err = add_file (name,
                  parent_dir_baton,
                  ancestor_path,
                  ancestor_version, 
                  file_baton);

  /* ... except that you must check that the file existed already, and
     was under version control */

  /* kff todo urgent: HERE, do what above says */

  printf ("replace file \"%s\"\n",
          ((struct file_baton *) (*file_baton))->path->data);

  return err;
}


static svn_error_t *
apply_textdelta (void *file_baton, 
                 svn_txdelta_window_handler_t **handler,
                 void **handler_baton)
{
  *handler_baton = file_baton;
  *handler = window_handler;
  
  return SVN_NO_ERROR;
}


static svn_error_t *
change_file_prop (void *file_baton,
                  svn_string_t *name,
                  svn_string_t *value)
{
  struct file_baton *fb = (struct file_baton *) file_baton;

  /* kff todo */

  fb->prop_changed = 1;

  return SVN_NO_ERROR;
}


static svn_error_t *
close_file (void *file_baton)
{
  struct file_baton *fb = (struct file_baton *) file_baton;
  apr_file_t *log_fp = NULL;
  svn_error_t *err;
  void *local_changes;
  char *version_str = NULL;

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

         - The new pristine props for blah, if any, are present in
           SVN/tmp/prop-base/blah; and the file_baton is appropriately
           marked if so.

         - The SVN/versions file still reflects the old blah.

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
              <set-version name="blah" version="N"/>
                  <!-- Once everything else is done, we can set blah's
                       version to N, changing the ./SVN/versions
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

  /* kff todo: there's going to be an issue with the lack of
     outermost wrapping XML element in the log file.  Several ways
     to deal with that, sleep on it... */
  
  /* kff todo: save *local_changes somewhere, maybe to a tmp file
     in SVN/. */
  
  if (fb->text_changed)
    {
      /* Merge text. */
      err = svn_wc__write_adm_entry (log_fp,
                                     fb->pool,
                                     SVN_WC__LOG_MERGE_TEXT,
                                     SVN_WC__LOG_ATTR_NAME,
                                     fb->name,
                                     SVN_WC__LOG_ATTR_SAVED_MODS,
                                     svn_string_create ("kff todo", fb->pool),
                                     NULL);
      if (err)
        return err;
      
      /* Replace text base. */
      err = svn_wc__write_adm_entry (log_fp,
                                     fb->pool,
                                     SVN_WC__LOG_REPLACE_TEXT_BASE,
                                     SVN_WC__LOG_ATTR_NAME,
                                     fb->name,
                                     NULL);
      if (err)
        return err;
    }
  
  if (fb->prop_changed)
    {
      /* Merge props. */
      err = svn_wc__write_adm_entry (log_fp,
                                     fb->pool,
                                     SVN_WC__LOG_MERGE_PROPS,
                                     SVN_WC__LOG_ATTR_NAME,
                                     fb->name,
                                     NULL);
      if (err)
        return err;
      
      /* Replace prop base. */
      err = svn_wc__write_adm_entry (log_fp,
                                     fb->pool,
                                     SVN_WC__LOG_REPLACE_PROP_BASE,
                                     SVN_WC__LOG_ATTR_NAME,
                                     fb->name,
                                     NULL);
      if (err)
        return err;
    }

  /* Set version. */
  version_str = apr_psprintf (fb->pool,
                              "%d",
                              fb->dir_baton->edit_baton->target_version);

  err = svn_wc__write_adm_entry (log_fp,
                                 fb->pool,
                                 SVN_WC__LOG_SET_VERSION,
                                 SVN_WC__LOG_ATTR_NAME,
                                 fb->name,
                                 SVN_WC__LOG_ATTR_VERSION,
                                 svn_string_create (version_str, fb->pool),
                                 NULL);
  if (err)
    return err;

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
  err = decrement_ref_count (fb->dir_baton);
  if (err)
    return err;

  printf ("\n");
  return SVN_NO_ERROR;
}


static svn_error_t *
close_edit (void *edit_baton)
{
  struct edit_baton *eb = (struct edit_baton *) edit_baton;

  /* The edit is over, free its pool. */
  apr_destroy_pool (eb->pool);

  /* kff todo:  Wow.  Is there _anything_ else that needs to be done? */

  printf ("\n");
  return SVN_NO_ERROR;
}



static const svn_delta_edit_fns_t tree_editor =
{
  replace_root,
  delete,
  add_directory,
  replace_directory,
  change_dir_prop,
  change_dirent_prop,
  close_directory,
  add_file,
  replace_file,
  apply_textdelta,
  change_file_prop,
  close_file,
  close_edit
};


svn_error_t *
svn_wc_get_update_editor (svn_string_t *dest,
                          svn_string_t *repos,
                          svn_vernum_t target_version,
                          const svn_delta_edit_fns_t **editor,
                          void **edit_baton,
                          apr_pool_t *pool)
{
  struct edit_baton *eb;
  apr_pool_t *subpool = svn_pool_create (pool, NULL);

  /* Else nothing in the way, so continue. */

  *editor = &tree_editor;

  eb = apr_pcalloc (subpool, sizeof (*edit_baton));
  eb->dest_dir       = dest;   /* Remember, DEST might be null. */
  eb->repository     = repos;
  eb->pool           = subpool;
  eb->target_version = target_version;

  *edit_baton = eb;

  return SVN_NO_ERROR;
}



/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
