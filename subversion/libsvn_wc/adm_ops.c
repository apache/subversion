/*
 * adm_ops.c:  routines for doing things in the administrative
 *             subdirectory.
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



/* Make the working copy administrative directory. */
static svn_error_t *
create_empty_adm_subdir (svn_string_t *path, apr_pool_t *pool)
{
  return svn_wc__make_adm_thing (path, "", svn_directory_kind, pool);
}



/* Initialize the `versions' file in the administrative subdir. */
static svn_error_t *
adm_init_versions (svn_string_t *path,
                   svn_string_t *ancestor_path,
                   svn_vernum_t ancestor_version,
                   apr_pool_t *pool)
{
  svn_error_t *err = NULL;
  apr_file_t *v = NULL;

  err = svn_wc__make_adm_thing (path, SVN_WC__ADM_VERSIONS,
                                svn_file_kind, pool);
  if (err)
    return err;

  err = svn_wc__open_adm_file (&v, path, SVN_WC__ADM_VERSIONS,
                               APR_WRITE, pool);
  if (err)
    return err;

  apr_fprintf (v, ". %ld %s\n", ancestor_version, ancestor_path->data);

  err = svn_wc__close_adm_file (v, path, SVN_WC__ADM_VERSIONS, pool);
  if (err)
    return err;

  return SVN_NO_ERROR;
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
  err = create_empty_adm_subdir (path, pool);
  if (err)
    return err;

  /* And lock it immediately! */
  err = svn_wc__lock (path, 0, pool);
  if (err)
    return err;

#if 0   /* kff todo: rewrite to use the new action bookkeeping mech. */
  err = svn_wc__make_adm_thing (path, SVN_WC__ADM_DOING_CHECKOUT,
                                svn_file_kind, pool);
  if (err)
    return err;
#endif /* 0 */


  /* Make `SVN/versions'. */
  err = adm_init_versions (path, ancestor_path, ancestor_version, pool);
  if (err)
    return err;

  /* Make `SVN/text-base/'. */
  err = svn_wc__make_adm_thing (path, SVN_WC__ADM_TEXT_BASE,
                                svn_directory_kind, pool);
  if (err)
    return err;

  /* Make `SVN/prop-base/' */
  err = svn_wc__make_adm_thing (path, SVN_WC__ADM_PROP_BASE,
                                svn_directory_kind, pool);
  if (err)
    return err;

  /* Make `SVN/tmp/' */
  err = svn_wc__make_adm_thing (path, SVN_WC__ADM_TMP,
                                svn_directory_kind, pool);
  if (err)
    return err;
  
  /* kff todo: to be continued. */

  return SVN_NO_ERROR;
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
  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc_copy (svn_string_t *src, svn_string_t *dst)
{
  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc_add (apr_array_header_t *paths)
{
  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc_delete (apr_array_header_t *paths)
{
  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc_get_path_prop (svn_string_t **value,
                      svn_string_t *propname,
                      svn_string_t *path)
{
  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc_get_dirent_prop (svn_string_t **value,
                        svn_string_t *propname,
                        svn_string_t *path)
{
  return SVN_NO_ERROR;
}



/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
