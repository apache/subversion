/*
 * apply_delta.c :  routines for update and checkout
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



struct w_baton
{
  svn_string_t *dest_dir;
  svn_string_t *repository;
  apr_pool_t *pool;
};



/*** Helpers for the walker callbacks. ***/

/* Prepend WB->dest_dir to *NAME, iff PARENT is an empty path or null. */
static void
maybe_prepend_dest (svn_string_t **name,
                    struct w_baton *wb,
                    svn_string_t *parent)
{
  /* This is a bit funky.  We need to prepend wb->dest_dir to every
     path the delta will touch, but due to the way parent/child batons
     are passed, we only need to do it once at the top of the delta,
     as it will will get passed along automatically underneath that.
     So we should only do this if parent_baton hasn't been set yet. */
      
  /* kff todo: or, write svn_string_prepend_str(), obviating the need
     to pass by reference. */

  if (wb->dest_dir && (svn_path_isempty (parent, SVN_PATH_LOCAL_STYLE)))
    {
      svn_string_t *new = svn_string_dup (wb->dest_dir, wb->pool);
      svn_path_add_component (new, *name, SVN_PATH_LOCAL_STYLE, wb->pool);
      *name = new;
    }
}


static svn_error_t *
window_handler (svn_txdelta_window_t *window, void *baton)
{
  int i;
  apr_file_t *dest = (apr_file_t *) baton;

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
              {
                /* kff todo: hey, is this pool appropriate to use? */
                return svn_create_error
                  (apr_err, 0, NULL, NULL, window->pool);
              }

            break;
          }
        }
    }

  return SVN_NO_ERROR;
}



/*** The callbacks we'll plug into an svn_delta_walk_t structure. ***/

static svn_error_t *
delete (svn_string_t *name, void *walk_baton, void *parent_baton)
{
  struct w_baton *wb = (struct w_baton *) walk_baton;
  svn_string_t *path = (svn_string_t *) parent_baton;

  maybe_prepend_dest (&name, wb, path);

  return SVN_NO_ERROR;
}


static svn_error_t *
add_directory (svn_string_t *name,
               void *walk_baton,
               void *parent_baton,
               svn_string_t *ancestor_path,
               svn_vernum_t ancestor_version,
               void **child_baton)
{
  svn_error_t *err;
  struct w_baton *wb = (struct w_baton *) walk_baton;
  svn_string_t *path_so_far = (svn_string_t *) parent_baton;
  svn_string_t *npath;

  /* kff todo: fooo this apparently didn't work. */
  maybe_prepend_dest (&name, wb, path_so_far);

  npath = svn_string_dup (path_so_far, wb->pool);
  svn_path_add_component (npath, name, SVN_PATH_LOCAL_STYLE, wb->pool);

  printf ("%s/    (ancestor == %s, %d)\n",
          npath->data, ancestor_path->data, (int) ancestor_version);

  /* kff todo: fooo working here.
     Setup has to be done carefully.  We have set up the directory
     NAME, but also let PATH know about it iff PATH is a concerned
     working copy. */
  err = svn_wc__set_up_new_dir (npath,
                                ancestor_path,
                                ancestor_version,
                                wb->pool);
  if (err)
    return err;

  /* else */

  *child_baton = npath;
  return SVN_NO_ERROR;
}


static svn_error_t *
replace_directory (svn_string_t *name,
                   void *walk_baton,
                   void *parent_baton,
                   svn_string_t *ancestor_path,
                   svn_vernum_t ancestor_version,
                   void **child_baton)
{
  struct w_baton *wb = (struct w_baton *) walk_baton;
  svn_string_t *path = (svn_string_t *) parent_baton;

  maybe_prepend_dest (&name, wb, path);

  return SVN_NO_ERROR;
}


static svn_error_t *
change_dir_prop (void *walk_baton,
                 void *dir_baton,
                 svn_string_t *name,
                 svn_string_t *value)
{
  /* kff todo */
  return SVN_NO_ERROR;
}


static svn_error_t *
change_dirent_prop (void *walk_baton,
                    void *dir_baton,
                    svn_string_t *entry,
                    svn_string_t *name,
                    svn_string_t *value)
{
  /* kff todo */
  return SVN_NO_ERROR;
}


static svn_error_t *
finish_directory (void *child_baton)
{
  /* kff todo ? */
  return SVN_NO_ERROR;
}


static svn_error_t *
add_file (svn_string_t *name,
          void *walk_baton,
          void *parent_baton,
          svn_string_t *ancestor_path,
          svn_vernum_t ancestor_version,
          void **file_baton)
{
  struct w_baton *wb = (struct w_baton *) walk_baton;
  svn_string_t *path_so_far = (svn_string_t *) parent_baton;
  svn_string_t *npath;

  /* kff todo: YO!  Need to make an adm subdir for orphan files, such
     as `iota' in checkout-1.delta. */

  /* fooo working here -- don't forget to set *file_baton! */

  maybe_prepend_dest (&name, wb, path_so_far);

  npath = svn_string_dup (path_so_far, wb->pool);
  svn_path_add_component (npath, name, SVN_PATH_LOCAL_STYLE, wb->pool);
  printf ("%s\n   ", npath->data);

  return SVN_NO_ERROR;
}


static svn_error_t *
replace_file (svn_string_t *name,
              void *walk_baton,
              void *parent_baton,
              svn_string_t *ancestor_path,
              svn_vernum_t ancestor_version,
              void **file_baton)
{
  maybe_prepend_dest (&name,
                      (struct w_baton *) walk_baton,
                      (svn_string_t *) parent_baton);

  /* kff todo: don't forget to set *file_baton! */

  printf ("replace file \"%s\" (%s, %ld)\n",
          name->data, ancestor_path->data, ancestor_version);
  return SVN_NO_ERROR;
}


static svn_error_t *
apply_textdelta (void *walk_baton,
                 void *parent_baton,
                 void *file_baton, 
                 svn_txdelta_window_handler_t **handler,
                 void **handler_baton)
{
  /* kff todo: this replaces begin_textdelta() and finish_textdelta(). */

#if 0
  static svn_error_t *
    begin_textdelta (void *walk_baton,
                     void *parent_baton,
                     svn_txdelta_window_handler_t **handler,
                     void **handler_baton)
    {
      /* kff todo: this also needs to handle already-present files by
         applying a text-delta, eventually.  And operate on a tmp file
         first, for atomicity/crash-recovery, etc, etc. */
      
      struct w_baton *wb = (struct w_baton *) walk_baton;
      svn_string_t *fname = (svn_string_t *) parent_baton;
      apr_file_t *sink = NULL;
      apr_status_t apr_err;
      
      apr_err = apr_open (&sink, fname->data,
                          (APR_WRITE | APR_CREATE),
                          APR_OS_DEFAULT,
                          wb->pool);
      
      if (apr_err)
        return svn_create_error (apr_err, 0, fname->data, NULL, wb->pool);
      
      *handler_baton = sink;
      *handler = window_handler;
      
      return SVN_NO_ERROR;
    }
  
  
  static svn_error_t *
    finish_textdelta (void *walk_baton,
                      void *parent_baton,
                      void *handler_baton)
    {
      struct w_baton *wb = (struct w_baton *) walk_baton;
      apr_file_t *f = (apr_file_t *) handler_baton;
      apr_status_t apr_err = apr_close (f);
      
      if (apr_err)
        return svn_create_error (apr_err, 0, NULL, NULL, wb->pool);
      else
        return SVN_NO_ERROR;
    }
#endif /* 0 */

  return SVN_NO_ERROR;
}


static svn_error_t *
change_file_prop (void *walk_baton,
                  void *parent_baton,
                  void *file_baton,
                  svn_string_t *name,
                  svn_string_t *value)
{
  /* kff todo */
  return SVN_NO_ERROR;
}


static svn_error_t *
finish_file (void *child_baton)
{
  /* kff todo */
  printf ("\n");
  return SVN_NO_ERROR;
}



static const svn_delta_walk_t change_walker = {
  delete,
  add_directory,
  replace_directory,
  change_dir_prop,
  change_dirent_prop,
  finish_directory,
  add_file,
  replace_file,
  apply_textdelta,
  change_file_prop,
  finish_file,
};

svn_error_t *
svn_wc_apply_delta (void *delta_src,
                    svn_delta_read_fn_t *read_fn,
                    svn_string_t *dest,
                    svn_string_t *repos,
                    apr_pool_t *pool)
{
  svn_error_t *err;
  struct w_baton w_baton;
  svn_string_t *telescoping_path;

  if (dest)
    {
      err = svn_wc__ensure_directory (dest, pool);

      if (err)
        return err;
      else
        {
          /* kff todo: actually, we can't always err out if dest turns out
             to be a working copy; instead, we just need to note it
             somewhere and be careful.  Right now, though, punt. */

          int is_working_copy = 0;
          
          svn_wc__working_copy_p (&is_working_copy, dest, pool);
          if (is_working_copy)
            return svn_create_error (SVN_ERR_OBSTRUCTED_UPDATE,
                                     0, dest->data, NULL, pool);
        }
    }

  /* Else nothing in the way, so continue. */

  /* Set up the batons... */
  memset (&w_baton, 0, sizeof (w_baton));
  w_baton.dest_dir   = dest;      /* Remember, DEST might be null. */
  w_baton.repository = repos;
  w_baton.pool       = pool;
  telescoping_path = svn_string_create ("", pool);

  /* ... and walk! */
  err = svn_XML_parse (read_fn, delta_src,
                       &change_walker, &w_baton, telescoping_path, pool);

  return err;
}

svn_error_t *svn_wc_get_change_walker(svn_string_t *dest,
                                      const svn_delta_walk_t **walker,
                                      void **walk_baton,
                                      void **dir_baton,
                                      apr_pool_t *pool)
{
  svn_error_t *err;
  struct w_baton *w_baton;

  /* ### this bit with creating the destination should be deferred */

  if (dest)
    {
      int is_working_copy = 0;

      err = svn_wc__ensure_directory (dest, pool);
      if (err)
        return err;

      /* kff todo: actually, we can't always err out if dest turns out
         to be a working copy; instead, we just need to note it
         somewhere and be careful.  Right now, though, punt. */
      svn_wc__working_copy_p (&is_working_copy, dest, pool);
      if (is_working_copy)
        return svn_create_error (SVN_ERR_OBSTRUCTED_UPDATE,
                                 0, dest->data, NULL, pool);
    }

  /* Else nothing in the way, so continue. */

  *walker = &change_walker;

  w_baton = apr_pcalloc(pool, sizeof(*w_baton));
  w_baton->dest_dir   = dest;   /* Remember, DEST might be null. */
  w_baton->pool       = pool;
  *walk_baton = w_baton;

  *dir_baton = svn_string_create ("", pool);

  return NULL;
}


/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
