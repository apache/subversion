/*
 * add.c:  wrappers around wc add functionality.
 *
 * ====================================================================
 * Copyright (c) 2000-2001 CollabNet.  All rights reserved.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution.  The terms
 * are also available at http://subversion.tigris.org/license-1.html.
 * If newer versions of this license are posted there, you may use a
 * newer version instead, at your option.
 * ====================================================================
 */

/* ==================================================================== */



/*** Includes. ***/

#include "svn_wc.h"
#include "svn_client.h"
#include "svn_string.h"
#include "svn_error.h"
#include "svn_path.h"
#include "client.h"



/*** Code. ***/

static svn_error_t *
add_dir_recursive (const char *dirname,
                   apr_pool_t *pool)
{
  apr_dir_t *dir;
  apr_finfo_t this_entry;
  apr_status_t apr_err;
  apr_pool_t *subpool;

  /* Add this directory to revision control. */
  SVN_ERR (svn_wc_add_directory (svn_string_create (dirname, pool),
                                 pool));

  /* Create a subpool for iterative memory control. */
  subpool = svn_pool_create (pool);

  /* Read the directory entries one by one and add those things to
     revision control. */
  apr_err = apr_dir_open (&dir, dirname, pool);
  for (apr_err = apr_dir_read (&this_entry, APR_FINFO_NORM, dir);
       APR_STATUS_IS_SUCCESS (apr_err);
       apr_err = apr_dir_read (&this_entry, APR_FINFO_NORM, dir))
    {
      svn_stringbuf_t *fullpath;

      /* Skip over SVN admin directories. */
      if (strcmp (this_entry.name, SVN_WC_ADM_DIR_NAME) == 0)
        continue;

      /* Skip entries for this dir and its parent.  */
      if ((strcmp (this_entry.name, ".") == 0)
          || (strcmp (this_entry.name, "..") == 0))
        continue;

      /* Construct the full path of the entry. */
      fullpath = svn_string_create (dirname, subpool);
      svn_path_add_component 
        (fullpath,
         svn_string_create (this_entry.name, subpool),
         svn_path_local_style);

      if (this_entry.filetype == APR_DIR)
        /* Recurse. */
        SVN_ERR (add_dir_recursive (fullpath->data, subpool));

      else if (this_entry.filetype == APR_REG)
        SVN_ERR (svn_wc_add_file (fullpath, subpool));

      /* Clean out the per-iteration pool. */
      svn_pool_clear (subpool);
    }

  /* Destroy the per-iteration pool. */
  svn_pool_destroy (subpool);

  /* Check that the loop exited cleanly. */
  if (! (APR_STATUS_IS_ENOENT (apr_err)))
    {
      return svn_error_createf
        (apr_err, 0, NULL, subpool, "error during recursive add of `%s'",
         dirname);
    }
  else  /* Yes, it exited cleanly, so close the dir. */
    {
      apr_err = apr_dir_close (dir);
      if (! (APR_STATUS_IS_SUCCESS (apr_err)))
        return svn_error_createf
          (apr_err, 0, NULL, subpool, "error closing dir `%s'", dirname);
    }
  return SVN_NO_ERROR;
}


svn_error_t *
svn_client_add (svn_stringbuf_t *path, 
                svn_boolean_t recursive,
                apr_pool_t *pool)
{
  enum svn_node_kind kind;
  svn_error_t *err = NULL;

  SVN_ERR (svn_io_check_path (path, &kind, pool));
  
  if (kind == svn_node_file)
    {
      err = svn_wc_add_file (path, pool);
    }
  else if (kind == svn_node_dir)
    {
      if (recursive)
        SVN_ERR (add_dir_recursive (path->data, pool));
      else
        err = svn_wc_add_directory (path, pool);
    }
  else
    return
      svn_error_createf (SVN_ERR_WC_PATH_NOT_FOUND, 0, NULL, pool,
                         "No such object: %s", path->data);

  if (err)
    {
      if (err->apr_err == SVN_ERR_WC_ENTRY_EXISTS)
        return svn_error_quick_wrap 
          (err,
           "svn warning: Cannot add because entry already exists.");
      else
          return err;
    }
  return SVN_NO_ERROR;
}


svn_error_t *
svn_client_unadd (svn_stringbuf_t *path, 
                  apr_pool_t *pool)
{
  svn_error_t *err;

  err = svn_wc_unadd (path, pool);
  if (err)
    return err;

  return SVN_NO_ERROR;
}



/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end: */
