/*
 * wc_adm.c :  routines for managing working copies, especially the
 *             stuff in the SVN/ subdirectories.
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
#include "svn_wc.h"
#include "wc.h"



/*** A path manipulation library. ***/

/* kff todo: this little path manipulation library will want to live
   somewhere else eventually, and handle more than just Unix-style
   paths. */

#define SVN_DIR_SEPARATOR '/'

/* Helper for svn_wc__path_add_component_*, which see. */
static void
path_add_component_internal (svn_string_t *path,
                             const char *component,
                             size_t len,
                             apr_pool_t *pool)
{
  char dirsep = SVN_DIR_SEPARATOR;

  if (! svn_string_isempty (path))
    svn_string_appendbytes (path, &dirsep, 1, pool);

  svn_string_appendbytes (path, component, len, pool);
}


/* Like svn_wc__path_add_component(), but COMPONENT is a null-terminated
   c-string ("nts").  Not public until there's a need for it to be. */
static void
svn_wc__path_add_component_nts (svn_string_t *path, 
                               char *component,
                               apr_pool_t *pool)
{
  path_add_component_internal (path, component, strlen (component), pool);
}


/* Extend PATH with new COMPONENT, destructively. */
void
svn_wc__path_add_component (svn_string_t *path, 
                            svn_string_t *component,
                            apr_pool_t *pool)
{
  path_add_component_internal (path, component->data, component->len, pool);
}


/* Remove PATH's deepest COMPONENT, destructively. */
void
svn_wc__path_remove_component (svn_string_t *path)
{
  if (! svn_string_chop_back_to_char (path, SVN_DIR_SEPARATOR))
    svn_string_setempty (path);
}



/*** Names in the SVN/ directory. ***/

/* Cache the SVN/ directory name. */
static svn_string_t *adm_dir_str = NULL;
svn_string_t *
svn_wc__adm_subdir (apr_pool_t *pool)
{
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

/* Make name of wc admin file ADM_FILE by appending to directory PATH. */
static void
extend_with_admin_name (svn_string_t *path,
                        char *adm_file,
                        apr_pool_t *pool)
{
  svn_wc__path_add_component     (path, svn_wc__adm_subdir (pool), pool);
  svn_wc__path_add_component_nts (path, adm_file, pool);
}


/* Restore PATH to what it was before an adm filename was appended to it. */
static void
chop_admin_name (svn_string_t *path)
{
  svn_wc__path_remove_component (path);
  svn_wc__path_remove_component (path);
}



/*** Setup of the adm dir. ***/

/* Make the working copy administrative directory. */
static svn_error_t *
make_adm_subdir (svn_string_t *path, apr_pool_t *pool)
{
  svn_error_t *err = NULL;
  apr_status_t apr_err = 0;

  svn_wc__path_add_component (path, svn_wc__adm_subdir (pool), pool);

  apr_err = apr_make_dir (path->data, APR_OS_DEFAULT, pool);
  if (apr_err)
    err = svn_create_error (apr_err, 0, path->data, NULL, pool);

  /* Restore path to its original state no matter what. */
  svn_wc__path_remove_component (path);

  return err;
}


/* Helper func for the svn_wc__init_FILE() functions:
 *
 * In directory PATH, create SVN/THING, with type TYPE indicating file
 * or directory (see enum below).
 *
 * If file, and RETURN_FILE is non-null, then *RETURN_FILE will be set
 * to an open handle on the thing.
 *
 * If not file type, but RETURN_FILE is non-null, then return an
 * error, because the caller is probably making a mistake.
 */ 
enum { file_type = 1, dir_type };
static svn_error_t *
create_adm_thing (svn_string_t *path,
                  char *thing,
                  int type,
                  apr_file_t **return_file,
                  apr_pool_t *pool)
{
  svn_error_t *err = NULL;
  apr_file_t *f = NULL;
  apr_status_t apr_err = 0;

  extend_with_admin_name (path, thing, pool);

  if (type == file_type)
    {
      apr_err = apr_open (&f, path->data,
                          (APR_WRITE | APR_CREATE),
                          APR_OS_DEFAULT,
                          pool);

      if (apr_err)
        {
          err = svn_create_error (apr_err, 0, path->data, NULL, pool);
          goto leave_town;
        }
      else
        {
          if (return_file)
            *return_file = f;
          else
            apr_err = apr_close (f);
          if (apr_err)
            err = svn_create_error (apr_err, 0, path->data, NULL, pool);
        }
    }
  else if (type == dir_type)
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

  leave_town:
  return err;
}
                       

/* Remove path/SVN/thing; really only used for lock files right now. */
static svn_error_t *
remove_adm_thing (svn_string_t *path,
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


/* Initialize the `versions' file in the administrative subdir. */
static svn_error_t *
adm_init_versions (svn_string_t *path,
                   svn_vernum_t ancestor_version,
                   apr_pool_t *pool)
{
  svn_error_t *err = NULL;
  apr_file_t *v = NULL;
  apr_status_t apr_err = 0;

  err = create_adm_thing (path, SVN_WC__ADM_VERSIONS, file_type, &v, pool);
  if (err)
    return err;

  apr_fprintf (v, ". %ld %s\n", ancestor_version, path->data);

  apr_err = apr_close (v);
  if (apr_err)
    {
      svn_string_t *new = svn_string_dup (path, pool);
      extend_with_admin_name (new, SVN_WC__ADM_VERSIONS, pool);
      return svn_create_error (apr_err, 0, new->data, NULL, pool);
    }

  return 0;
}


/* Set up working copy directory PATH with appropriate ancestry.
   Leaves the directory in a locked state. */
svn_error_t *
svn_wc__set_up_new_dir (svn_string_t *path, 
                        svn_string_t *ancestor_path,
                        svn_vernum_t ancestor_version,
                        apr_pool_t *pool)
{
  apr_status_t apr_err;
  svn_error_t *err;

  /* Make the directory. */
  apr_err = apr_make_dir (path->data, APR_OS_DEFAULT, pool);
  if (apr_err)
    return svn_create_error (apr_err, 0, path->data, NULL, pool);

  /* Make `SVN/'. */
  err = make_adm_subdir (path, pool);
  if (err)
    return err;

  /* And lock it immediately! */
  err = svn_wc__lock (path, 0, pool);
  if (err)
    return err;

  /* Make `SVN/versions'. */
  err = adm_init_versions (path, ancestor_version, pool);
  if (err)
    return err;

  /* Make `SVN/versions'. */
  err = adm_init_versions (path, ancestor_version, pool);
  if (err)
    return err;

  /* kff todo: to be continued. */

  return 0;
}



svn_error_t *
svn_wc__lock (svn_string_t *path, int wait, apr_pool_t *pool)
{
  svn_error_t *err = NULL;

  /* kff todo: fooo, in progress */
  err = create_adm_thing (path, SVN_WC__ADM_LOCK, file_type, NULL, pool);
  if (err)
    return err;

  return 0;
}


svn_error_t *
svn_wc__unlock (svn_string_t *path, apr_pool_t *pool)
{
  /* kff todo: in progress */

  return 0;
}


svn_string_t *
svn_wc__working_name (svn_string_t *path, apr_pool_t *pool)
{
  return path;  /* todo: in progress */
}



/* kff todo: not all of these really belong in wc_adm.  Some may get
   broken out into other files later.  They're just here to satisfy
   the public header file that they exist. */

svn_error_t *
svn_wc_rename (svn_string_t *src, svn_string_t *dst)
{
  return 0;
}


svn_error_t *
svn_wc_copy (svn_string_t *src, svn_string_t *dst)
{
  return 0;
}


svn_error_t *
svn_wc_add (apr_array_header_t *paths)
{
  return 0;
}


svn_error_t *
svn_wc_delete (apr_array_header_t *paths)
{
  return 0;
}


svn_error_t *
svn_wc_get_path_prop (svn_string_t **value,
                      svn_string_t *propname,
                      svn_string_t *path)
{
  return 0;
}


svn_error_t *
svn_wc_get_dirent_prop (svn_string_t **value,
                        svn_string_t *propname,
                        svn_string_t *path)
{
  return 0;
}





/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */

