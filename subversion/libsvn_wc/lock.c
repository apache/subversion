/*
 * lock.c:  routines for locking working copy subdirectories.
 *
 * ====================================================================
 * Copyright (c) 2000-2002 CollabNet.  All rights reserved.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution.  The terms
 * are also available at http://subversion.tigris.org/license-1.html.
 * If newer versions of this license are posted there, you may use a
 * newer version instead, at your option.
 *
 * This software consists of voluntary contributions made by many
 * individuals.  For exact contribution history, see the revision
 * history and logs, available at http://subversion.tigris.org/.
 * ====================================================================
 */



#include <apr_pools.h>
#include <apr_time.h>

#include "wc.h"
#include "adm_files.h"



svn_error_t *
svn_wc_lock (svn_stringbuf_t *path, int wait_for, apr_pool_t *pool)
{
  svn_error_t *err;

  do {
    err = svn_wc__make_adm_thing (path, SVN_WC__ADM_LOCK,
                                  svn_node_file, APR_OS_DEFAULT, 0, pool);
    if (err && APR_STATUS_IS_EEXIST(err->apr_err))
      {
        svn_error_clear_all (err);
        apr_sleep (1 * APR_USEC_PER_SEC);  /* micro-seconds */
        wait_for--;
      }
    else
      return SVN_NO_ERROR;
  } while (wait_for > 0);

  return svn_error_createf (SVN_ERR_WC_LOCKED, 0, NULL, pool, 
                            "working copy locked: %s", path->data); 
}


svn_error_t *
svn_wc_unlock (svn_stringbuf_t *path, apr_pool_t *pool)
{
  return svn_wc__remove_adm_file (path, pool, SVN_WC__ADM_LOCK, NULL);
}


svn_error_t *
svn_wc_locked (svn_boolean_t *locked, svn_stringbuf_t *path, apr_pool_t *pool)
{
  svn_node_kind_t kind;
  svn_stringbuf_t *lockfile 
    = svn_wc__adm_path (path, 0, pool, SVN_WC__ADM_LOCK, NULL);
                                             
  SVN_ERR (svn_io_check_path (lockfile->data, &kind, pool));
  if (kind == svn_node_file)
    *locked = 1;
  else if (kind == svn_node_none)
    *locked = 0;
  else
    return svn_error_createf (SVN_ERR_WC_LOCKED, 0, NULL, pool,
                              "svn_wc__locked: "
                              "lock file is not a regular file (%s)",
                              path->data);
    
  return SVN_NO_ERROR;
}



/* 
 * local variables:
 * eval: (load-file "../../tools/dev/svn-dev.el")
 * end:
 */
