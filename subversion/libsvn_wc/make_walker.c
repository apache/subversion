/*
 * update.c :  routines for update and checkout
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
#include "svn_error.h"
#include "svn_hash.h"
#include "svn_wc.h"



/* If PATH exists already, return an svn error containing ERR_TO_REPORT.
 *
 * If PATH doesn't exist, return 0.
 *
 * If unable to determine whether or not PATH exists, due to another
 * error condition, return an svn error containing that apr error.
 */
static svn_error_t *
check_existence (svn_string_t *path,
                 apr_status_t err_to_report,
                 apr_pool_t *pool)
{
  apr_status_t apr_err;
  apr_file_t *tmp_f = NULL;  /* init to NULL hugely important to APR */

  apr_err = apr_open (&tmp_f, path->data,
                     (APR_READ | APR_CREATE | APR_EXCL),
                     APR_OS_DEFAULT, pool);

  if (apr_err == APR_EEXIST)
    {
      svn_error_t *err 
        = svn_create_error (err_to_report, 0, path->data, NULL, pool);
      return err;
    }
  else if (apr_err)  /* some error other than APR_EEXIST */
    {
      svn_error_t *err 
        = svn_create_error (apr_err, 0, path->data, NULL, pool);
      return err;
    }
  else              /* path definitely does not exist */
    {
      apr_close (tmp_f);
      apr_remove_file (path->data, pool);
      return 0;
    }
}



/* kff todo: this little path manipulation library will want to live
   somewhere else eventually, and handle more than just Unix-style
   paths. */

#define SVN_DIR_SEPARATOR '/'

static void
path_add_component (svn_string_t *path, 
                    svn_string_t *component,
                    apr_pool_t *pool)
{
  char dirsep = SVN_DIR_SEPARATOR;
  if (! svn_string_isempty (path))
    svn_string_appendbytes (path, &dirsep, 1, pool);
  svn_string_appendstr (path, component, pool);
}


static void
path_remove_component (svn_string_t *path)
{
  if (! svn_string_chop_back_to_char (path, SVN_DIR_SEPARATOR))
    svn_string_setempty (path);
}



struct w_baton
{
  svn_string_t *top_dir;
  int top_dir_done_p;
  apr_pool_t *pool;
};



static svn_error_t *
delete (svn_string_t *name, void *walk_baton, void *parent_baton)
{
  return 0;
}


static svn_error_t *
add_directory (svn_string_t *name,
               void *walk_baton, void *parent_baton,
               svn_string_t *ancestor_path,
               svn_vernum_t ancestor_version,
               void **child_baton)
{
  /* todo: we're not yet special-casing top_dir.  When we do, it'll be
     like "cvs checkout -d foo bar", which produces a tree whose top
     dir is named foo, but everything underneath is within the
     project's namespace and appears as in the project.  This is
     doubly important for Subversion because the root node in a
     repository has no name, so if no subdir of the repository is
     specified in the checkout, the client must make up a local name
     for the root (i.e., the -d option should probably be mandatory if
     no other name was given). 
  */

  /* todo: it's convenient under Unix, and possibly some other OS's,
     to just store the path as a string.  But eventually we may want
     some more general sort of path-chain.
  */
  apr_status_t apr_err;
  svn_string_t *path = (svn_string_t *) parent_baton;
  struct w_baton *wb = (struct w_baton *) walk_baton;

  path_add_component (path, name, wb->pool);
  printf ("%s/\n", path->data);

  /* kff todo: we're going to be doing this a lot.  Maybe we should
     have a wrapper around apr_make_dir().  But Yet Another
     Wrapper... Hmmm. */
  apr_err = apr_make_dir (path->data, APR_OS_DEFAULT, wb->pool);

  if (apr_err)
    return svn_create_error (apr_err, 0, path->data, NULL, wb->pool);

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
  return 0;
}


static svn_error_t *
finish_directory (void *child_baton)
{
  svn_string_t *path = (svn_string_t *) child_baton;

  path_remove_component (path);

  return 0;
}


static svn_error_t *
window_handler (svn_delta_window_t *window, void *baton)
{
  int i;

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
            const char *data = ((svn_string_t *) (window->new))->data;
            printf ("%.*s", (int) this_op.length, (data + this_op.offset));
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

  path_add_component (path, name, wb->pool);

  printf ("%s\n   ", path->data);

  return 0;
}


static svn_error_t *
replace_file (svn_string_t *name,
              void *walk_baton, void *parent_baton,
              svn_string_t *ancestor_path,
              svn_vernum_t ancestor_version)
{
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
  path_remove_component (fname);
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

  svn_string_t *fname = (svn_string_t *) parent_baton;

#if 0 /* kff todo */
  struct w_baton *wb = (struct w_baton *) walk_baton;
  apr_file_t *sink = NULL;
  apr_status_t apr_err;
  apr_err = apr_open (&sink, fname->data,
                      (APR_WRITE | APR_CREATE),
                      APR_OS_DEFAULT,
                      wb->pool);

  if (apr_err)
    return svn_create_error (apr_err, 0, fname->data, NULL, pool);

  *handler_baton = sink;
#endif /* 0 */

  *handler_baton = fname;   /* for now */
  *handler = window_handler;

  return 0;
}


static svn_error_t *
finish_textdelta (void *walk_baton, void *parent_baton)
{
#if 0   /* kff todo */
  /* This doesn't work.  We need a way to get the window_handler's
     handler_baton in here, because that's the apr_file_t pointer. */
  apr_file_t *f = (apr_file_t *) child_baton;
  apr_err = apr_close (f);
  
  if (apr_err)
    return svn_create_error (apr_err, 0, NULL, NULL, pool);
#endif /* 0 */

  return 0;
}



/* Apply a delta to a working copy, or to create a working copy.
 * 
 * If TARGET exists and is a working copy, or a subtree of a working
 * copy, then it is massaged into the updated state.
 *
 * If TARGET does not exist, a working copy is created there.
 *
 * If TARGET exists but is not a working copy, return error.
 *
 * todo: if TARGET == NULL, above rules apply with TARGET set to
 * the top directory mentioned in the delta?
 */
svn_error_t *
svn_wc_apply_delta (void *delta_src,
                    svn_delta_read_fn_t *read_fn,
                    svn_string_t *target,
                    apr_pool_t *pool)
{
  svn_error_t *err;
  svn_delta_walk_t walker;
  struct w_baton w_baton;
  svn_string_t *telescoping_path;

  /* Check existence of TARGET.  If present, just error out for now -- we
     can't do real updates, only fresh checkouts.  In the future, if
     TARGET exists we'll check if it's a working copy and only error
     out if it's not. */
  if (target)
    {
      err = check_existence (target, SVN_ERR_OBSTRUCTED_UPDATE, pool);

      /* Whether or not err->apr_err == SVN_ERR_OBSTRUCTED_UPDATE, we
         want to return it to caller. */
      if (err)
        return err;
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
  w_baton.top_dir = target;   /* Remember, target might be null. */
  w_baton.pool = pool;
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
