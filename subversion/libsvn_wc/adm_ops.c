/*
 * adm_ops.c: routines for affecting working copy administrative
 *            information.  NOTE: this code doesn't know where the adm
 *            info is actually stored.  Instead, generic handles to
 *            adm data are requested via a reference to some PATH
 *            (PATH being a regular, non-administrative directory or
 *            file in the working copy).
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



/*** adm area guarantees ***/

/* Make sure that PATH (a directory) contains a complete adm area,
 * based at REPOSITORY.
 *
 * Creates the adm area if none, in which case PATH starts out at
 * revision 0.
 *
 * Note: The adm area's lock-state is not changed by this function,
 * and if the adm area is created, it is left in an unlocked state.
 */
svn_error_t *
svn_wc__ensure_wc (svn_string_t *path,
                   svn_string_t *repository,
                   svn_string_t *ancestor_path,
                   svn_revnum_t ancestor_revision,
                   apr_pool_t *pool)
{
  svn_error_t *err;

  err = svn_wc__ensure_adm (path,
                            repository,
                            ancestor_path,
                            ancestor_revision,
                            pool);
  if (err)
    return err;

  return SVN_NO_ERROR;
}



/*** Closing commits. ***/

svn_error_t *
svn_wc_close_commit (svn_string_t *path,
                     svn_revnum_t new_revision,
                     apr_hash_t *targets,
                     apr_pool_t *pool)
{
  svn_error_t *err;

  err = svn_wc__log_commit (path, targets, new_revision, pool);
  if (err)
    return err;

  err = svn_wc__cleanup (path, targets, 0, pool);
  if (err)
    return err;

  return SVN_NO_ERROR;
}




/* kff todo: not all of these really belong in wc_adm.  Some may get
   broken out into other files later.  They're just here to satisfy
   the public header file that they exist. */

svn_error_t *
svn_wc_rename (svn_string_t *src, svn_string_t *dst, apr_pool_t *pool)
{
  /* kff todo */
  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc_copy (svn_string_t *src, svn_string_t *dst, apr_pool_t *pool)
{
  /* kff todo */
  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc_delete_file (svn_string_t *file, apr_pool_t *pool)
{
  svn_string_t *dir, *basename;
  svn_error_t *err;

  svn_path_split (file, &dir, &basename, svn_path_local_style, pool);

  err = svn_wc__entry_fold_sync_intelligently (dir,
                                               basename,
                                               SVN_INVALID_REVNUM,
                                               svn_node_file,
                                               SVN_WC_ENTRY_DELETED,
                                               0,
                                               0,
                                               pool,
                                               NULL,
                                               NULL);
  if (err)
    return err;

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc_add_file (svn_string_t *file, apr_pool_t *pool)
{
  svn_string_t *dir, *basename;
  svn_error_t *err;

  svn_path_split (file, &dir, &basename, svn_path_local_style, pool);

  err = svn_wc__entry_fold_sync_intelligently (dir,
                                               basename,
                                               0,
                                               svn_node_file,
                                               SVN_WC_ENTRY_ADDED,
                                               0,
                                               0,
                                               pool,
                                               NULL,
                                               NULL);
  if (err)
    return err;

  return SVN_NO_ERROR;
}



svn_error_t *
svn_wc_get_pristine_copy_path (svn_string_t *path,
                               svn_string_t **pristine_path,
                               apr_pool_t *pool)
{
  svn_error_t *err;

  *pristine_path = svn_wc__text_base_path (path, FALSE, pool);

  return SVN_NO_ERROR;
}



/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
