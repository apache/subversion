/*
 * apply_delta.c :  routines for update and checkout
 *
 * ================================================================
 * Copyright (c) 2000 Collab.Net.  All rights reserved.
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
 * software developed by Collab.Net (http://www.Collab.Net/)."
 * Alternately, this acknowlegement may appear in the software itself, if
 * and wherever such third-party acknowlegements normally appear.
 * 
 * 4. The hosted project names must not be used to endorse or promote
 * products derived from this software without prior written
 * permission. For written permission, please contact info@collab.net.
 * 
 * 5. Products derived from this software may not use the "Tigris" name
* nor may "Tigris" appear in their names without prior written
 * permission of Collab.Net.
 * 
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESSED OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL COLLAB.NET OR ITS CONTRIBUTORS BE LIABLE FOR ANY
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
 * individuals on behalf of Collab.Net.
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



static svn_error_t *
delete (svn_string_t *name, void *walk_baton, void *parent_baton)
{
  return 0;
}


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
      
  /* kff todo: the right way would be to write
     svn_string_prepend_str(), obviating the need to pass by
     reference.  Do that soon. */

  if (wb->dest_dir && (svn_path_isempty (parent)))
    {
      svn_string_t *tmp = svn_string_dup (wb->dest_dir, wb->pool);
      svn_path_add_component (tmp, *name, SVN_PATH_LOCAL_STYLE, wb->pool);
      *name = tmp;
    }
}


static svn_error_t *
add_directory (svn_string_t *name,
               void *walk_baton, void *parent_baton,
               svn_string_t *ancestor_path,
               svn_vernum_t ancestor_version,
               void **child_baton)
{
  svn_error_t *err;
  svn_string_t *path = (svn_string_t *) parent_baton;
  struct w_baton *wb = (struct w_baton *) walk_baton;

  maybe_prepend_dest (&name, wb, path);

  svn_path_add_component (path, name, SVN_PATH_LOCAL_STYLE, wb->pool);
  printf ("%s/    (ancestor == %s, %d)\n",
          path->data, ancestor_path->data, (int) ancestor_version);

  err = svn_wc__set_up_new_dir (path,
                                ancestor_path,
                                ancestor_version,
                                wb->pool);
  if (err)
    return err;
  /* else */

  *child_baton = path;
  return 0;
}


static svn_error_t *
replace_directory (svn_string_t *name,
                   void *walk_baton, void *parent_baton,
                   svn_string_t *ancestor_path,
                   svn_vernum_t ancestor_version,
                   void **child_baton)
{
  maybe_prepend_dest (&name,
                      (struct w_baton *) walk_baton,
                      (svn_string_t *) parent_baton);

  return 0;
}


static svn_error_t *
finish_directory (void *child_baton)
{
  svn_string_t *path = (svn_string_t *) child_baton;

  svn_path_remove_component (path, SVN_PATH_LOCAL_STYLE);

  return 0;
}


static svn_error_t *
window_handler (svn_delta_window_t *window, void *baton)
{
  int i;
  apr_file_t *dest = (apr_file_t *) baton;

  for (i = 0; i < window->num_ops; i++)
    {
      svn_delta_op_t this_op = (window->ops)[i];
      switch (this_op.action_code)
        {
        case svn_delta_source:
          /* todo */
          break;

        case svn_delta_target:
          /* todo */
          break;

        case svn_delta_new:
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

  return 0;
}


static svn_error_t *
add_file (svn_string_t *name,
          void *walk_baton, void *parent_baton,
          svn_string_t *ancestor_path,
          svn_vernum_t ancestor_version)
{
  svn_string_t *path = (svn_string_t *) parent_baton;
  struct w_baton *wb = (struct w_baton *) walk_baton;

  /* kff todo: YO!  Need to make an adm subdir for orphan files, such
     as `iota' in checkout-1.delta. */
  /* fooo workig here */

  maybe_prepend_dest (&name, wb, path);

  svn_path_add_component (path, name, SVN_PATH_LOCAL_STYLE, wb->pool);

  printf ("%s\n   ", path->data);

  return 0;
}


static svn_error_t *
replace_file (svn_string_t *name,
              void *walk_baton, void *parent_baton,
              svn_string_t *ancestor_path,
              svn_vernum_t ancestor_version)
{
  maybe_prepend_dest (&name,
                      (struct w_baton *) walk_baton,
                      (svn_string_t *) parent_baton);

  printf ("replace file \"%s\" (%s, %ld)\n",
          name->data, ancestor_path->data, ancestor_version);
  return 0;
}


static svn_error_t *
finish_file (void *child_baton)
{
  svn_string_t *fname = (svn_string_t *) child_baton;

  printf ("\n");
  /* Lop off the filename, so baton is the parent directory again. */
  svn_path_remove_component (fname, SVN_PATH_LOCAL_STYLE);
  return 0;
}


static svn_error_t *
begin_textdelta (void *walk_baton, void *parent_baton,
                 svn_text_delta_window_handler_t **handler,
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

  return 0;
}


static svn_error_t *
finish_textdelta (void *walk_baton, void *parent_baton, void *handler_baton)
{
  struct w_baton *wb = (struct w_baton *) walk_baton;
  apr_file_t *f = (apr_file_t *) handler_baton;
  apr_status_t apr_err = apr_close (f);
  
  if (apr_err)
    return svn_create_error (apr_err, 0, NULL, NULL, wb->pool);
  else
    return 0;
}



svn_error_t *
svn_wc_apply_delta (void *delta_src,
                    svn_delta_read_fn_t *read_fn,
                    svn_string_t *dest,
                    svn_string_t *repos,
                    apr_pool_t *pool)
{
  svn_error_t *err;
  svn_delta_walk_t walker;
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

  /* Set up the walker callbacks... */
  memset (&walker, 0, sizeof (walker));
  walker.delete            = delete;
  walker.add_directory     = add_directory;
  walker.replace_directory = replace_directory;
  walker.finish_directory  = finish_directory;
  walker.finish_file       = finish_file;
  walker.add_file          = add_file;
  walker.replace_file      = replace_file;
  walker.begin_textdelta   = begin_textdelta;
  walker.finish_textdelta  = finish_textdelta;

  /* Set up the batons... */
  memset (&w_baton, 0, sizeof (w_baton));
  w_baton.dest_dir   = dest;      /* Remember, DEST might be null. */
  w_baton.repository = repos;
  w_baton.pool       = pool;
  telescoping_path = svn_string_create ("", pool);

  /* ... and walk! */
  err = svn_delta_parse (read_fn, delta_src,
                         &walker, &w_baton, telescoping_path, pool);

  return err;
}




/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
