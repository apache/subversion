/* lock.c : shared and exclusive repository locking
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

#include "apr_pools.h"
#include "apr_file_io.h"

#include "svn_pools.h"
#include "svn_error.h"
#include "svn_path.h"
#include "svn_fs.h"
#include "svn_repos.h"


/* This code manages repository locking, which is motivated by the
 * need to support DB_RUN_RECOVERY.  Here's how it works:
 *
 * Every accessor of a repository's database takes out a shared lock
 * on the repository -- both readers and writers get shared locks, and
 * there can be an unlimited number of shared locks simultaneously.
 *
 * Sometimes, a db access returns the error DB_RUN_RECOVERY.  When
 * this happens, we need to run svn_fs_berkeley_recover() on the db
 * with no other accessors present.  So we take out an exclusive lock
 * on the repository.  From the moment we request the exclusive lock,
 * no more shared locks are granted, and when the last shared lock
 * disappears, the exclusive lock is granted.  As soon as we get it,
 * we can run recovery.
 *
 * We assume that once any berkeley call returns DB_RUN_RECOVERY, they
 * all do, until recovery is run.
 */



#if 0  /* in progress */
/* doc string here */
static apr_status_t
unlock_repository (void *arg)
{
  /* in progress */

  return 0;
}
#endif /* 0 */

svn_error_t *
svn_repos_open (svn_fs_t **fs_p,
                const char *path,
                apr_pool_t *pool)
{
#if 0  /* in progress */
  svn_fs_t *fs;
  apr_status_t apr_err;

  fs = svn_fs_new (pool);
  SVN_ERR (svn_fs_open_berkeley (fs, path));

  /* Get shared lock. */
  apr_err = apr_file_lock (db->dirf, APR_FLOCK_SHARED);
  if (! APR_STATUS_IS_SUCCESS (apr_err))
    return svn_error_createf (SVN_ERR_REPOS_LOCKED, 0, err, pool,
             "svn_repos_open: repository `%s' locked", path);

  /* Register an unlock function for the shared lock. */
  apr_pool_cleanup_register (pool, dbt->data,
                             unlock_repository,
                             apr_pool_cleanup_null);

  *fs_p = fs;
#endif /* 0 */

  return SVN_NO_ERROR;
}



/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
