/*
 * wc_adm_files.c: helper routines for handling files & dirs in the
 *                 working copy administrative area (creating,
 *                 deleting, opening, and closing).
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



#include <stdio.h>       /* for sprintf() */
#include <stdlib.h>
#include <apr_pools.h>
#include <apr_hash.h>
#include <apr_file_io.h>
#include <apr_time.h>
#include "svn_types.h"
#include "svn_string.h"
#include "svn_error.h"
#include "svn_hash.h"
#include "svn_path.h"
#include "svn_wc.h"
#include "wc.h"



/*** Names in the SVN/ directory. ***/

/* No one outside this file should ever need to know this.  In fact,
   no one outside adm_subdir() should ever need to know this. */
#define SVN_WC__ADM_DIR_DEFAULT   "SVN"
static svn_string_t *
adm_subdir (apr_pool_t *pool)
{
  static svn_string_t *adm_dir_str = NULL;

  if (! adm_dir_str)
    adm_dir_str = svn_string_create (SVN_WC__ADM_DIR_DEFAULT, pool);

  return adm_dir_str;
}


/* kff todo: getting SVN/blah names could be more efficient, at the
 * expense of caller readability, since the names are all string
 * constants whose lengths are known at compile time.  But we're I/O
 * bound anyway in the code that's using those names, so let's go
 * for readability.
 */

/* Make name of wc admin file ADM_FILE by appending to directory PATH. 
 * 
 * IMPORTANT: chances are you will want to call chop_admin_name() to
 * restore PATH to its original value before exiting anything that
 * calls this.  If you exit, say by returning an error, before calling
 * chop_admin_name(), then PATH will still be in its extended state.
 * So for safety, always do this: callers of extend_with_admin_name()
 * should have only one `return' statement, and that return occurs
 * *after* an unconditional call to chop_admin_name().  
 */
static void
extend_with_admin_name (svn_string_t *path,
                        char *adm_file,
                        apr_pool_t *pool)
{
  svn_path_add_component     (path, adm_subdir (pool), 
                              SVN_PATH_LOCAL_STYLE, pool);
  svn_path_add_component_nts (path, adm_file, 
                              SVN_PATH_LOCAL_STYLE, pool);
}


/* Restore PATH to what it was before an adm filename was appended to it. */
static void
chop_admin_name (svn_string_t *path)
{
  svn_path_remove_component (path, SVN_PATH_LOCAL_STYLE);
  svn_path_remove_component (path, SVN_PATH_LOCAL_STYLE);
}


/* Helper func for the svn_wc__init_FILE() functions. */
svn_error_t *
svn_wc__make_adm_thing (svn_string_t *path,
                        char *thing,
                        int type,
                        apr_pool_t *pool)
{
  svn_error_t *err = NULL;
  apr_file_t *f = NULL;
  apr_status_t apr_err = 0;

  extend_with_admin_name (path, thing, pool);

  if (type == svn_file_kind)
    {
      apr_err = apr_open (&f, path->data,
                          (APR_WRITE | APR_CREATE | APR_EXCL),
                          APR_OS_DEFAULT,
                          pool);

      if (apr_err)
        err = svn_create_error (apr_err, 0, path->data, NULL, pool);
      else
        {
          /* Creation succeeded, so close immediately. */
          apr_err = apr_close (f);
          if (apr_err)
            err = svn_create_error (apr_err, 0, path->data, NULL, pool);
        }
    }
  else if (type == svn_directory_kind)
    {
      apr_err = apr_make_dir (path->data, APR_OS_DEFAULT, pool);
      if (apr_err)
        err = svn_create_error (apr_err, 0, path->data, NULL, pool);
    }
  else   /* unknown type argument, wrongness */
    {
      err = svn_create_error 
        (0, 0, "init_admin_thing: bad type indicator", NULL, pool);
    }

  /* Restore path to its original state no matter what. */
  chop_admin_name (path);

  return err;
}


svn_error_t *
svn_wc__open_adm_file (apr_file_t **handle,
                       svn_string_t *path,
                       char *fname,
                       apr_int32_t flags,
                       apr_pool_t *pool)
{
  svn_error_t *err = NULL;
  apr_status_t apr_err = 0;

  extend_with_admin_name (path, fname, pool);

  apr_err = apr_open (handle, path->data, flags, APR_OS_DEFAULT, pool);

  if (apr_err)
    err = svn_create_error (apr_err, 0, path->data, NULL, pool);

  /* Restore path to its original state no matter what. */
  chop_admin_name (path);

  return err;
}


svn_error_t *
svn_wc__close_adm_file (apr_file_t *fp,
                        svn_string_t *path,
                        char *fname,
                        apr_pool_t *pool)
{
  svn_error_t *err = NULL;
  apr_status_t apr_err = 0;

  extend_with_admin_name (path, fname, pool);

  apr_err = apr_close (fp);

  if (apr_err)
    err = svn_create_error (apr_err, 0, path->data, NULL, pool);

  /* Restore path to its original state no matter what. */
  chop_admin_name (path);

  return err;
}


/* Remove path/SVN/thing. */
svn_error_t *
svn_wc__remove_adm_thing (svn_string_t *path,
                          char *thing,
                          apr_pool_t *pool)
{
  svn_error_t *err = NULL;
  apr_status_t apr_err = 0;

  extend_with_admin_name (path, thing, pool);

  apr_err = apr_remove_file (path->data, pool);
  if (apr_err)
    err = svn_create_error (apr_err, 0, path->data, NULL, pool);

  /* Restore path to its original state no matter what. */
  chop_admin_name (path);

  return err;
}



/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
