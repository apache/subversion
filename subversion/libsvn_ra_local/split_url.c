/*
 * split_url.c : divide a file:/ URL into repository and path
 *
 * ====================================================================
 * Copyright (c) 2000 CollabNet.  All rights reserved.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution.  The terms
 * are also available at http://subversion.tigris.org/license-1.html.
 * If newer versions of this license are posted there, you may use a
 * newer version instead, at your option.
 * ====================================================================
 */

#include "ra_local.h"
#include <assert.h>



svn_error_t *
svn_ra_local__split_URL (svn_string_t **repos_path,
                         svn_string_t **fs_path,
                         svn_string_t *URL,
                         apr_pool_t *pool)
{
  svn_error_t *err;
  int i = 1;
  apr_pool_t *subpool = svn_pool_create (pool);
  apr_array_header_t *components = apr_array_make (subpool, 1,
                                                   sizeof(svn_string_t *));

  svn_string_t *shrinking_URL = svn_string_dup (URL, subpool);
  svn_string_t *growing_URL = svn_string_create ("/", subpool);
  svn_string_t *remainder = svn_string_create ("/", subpool);
  svn_fs_t *test_fs = svn_fs_new (subpool);

  /* Yank path components off the end of URL_copy, storing them in an
     array.  */
  do
    {
      svn_string_t *component = svn_path_last_component (shrinking_URL,
                                                         svn_path_url_style,
                                                         subpool);
      *((svn_string_t **)apr_array_push (components)) = component;
      svn_path_remove_component (shrinking_URL, svn_path_url_style);

    } while (shrinking_URL);
  
  /* Start from the 2nd item in the component array, build up a path,
     successively adding new components and trying to successfuly call
     svn_fs_open_berkeley().  */
  do
    {
      svn_string_t *component = (((svn_string_t **)(components)->elts)[i]);
      svn_path_add_component (growing_URL, component, svn_path_url_style);
      err = svn_fs_open_berkeley (test_fs, growing_URL->data);
      i++;
    } while ((i < components->nelts) && err);

  /* We're out of the loop, either because we ran out of search
     paths... */
  if (i >= components->nelts)
    return 
      svn_error_create 
      (SVN_ERR_RA_ILLEGAL_URL, 0, NULL, pool,
       "svn_ra_local__split_url:  can't find a repository anywhere in URL!");
  
  /* ..or because svn_fs_open_berkeley() finally returned SVN_NO_ERROR. */
  else
    {
      assert (err == SVN_NO_ERROR);
      err = svn_fs_close_fs (test_fs);
      if (err) return err;
    }

  /* Create repos_path and fs_path in -original- pool, then free our
     scratchwork subpool */
  *repos_path = svn_string_dup (growing_URL, pool);

  while (i < components->nelts)
    {
      svn_string_t *component = (((svn_string_t **)(components)->elts)[i]);
      svn_path_add_component (remainder, component, svn_path_url_style);
    }

  *fs_path = svn_string_dup (remainder, pool);

  apr_pool_destroy (subpool);

  return SVN_NO_ERROR;
}




/* ----------------------------------------------------------------
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */





