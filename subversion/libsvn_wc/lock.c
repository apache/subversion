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


static svn_error_t *
svn_wc__lock (svn_wc_adm_access_t *adm_access, int wait_for, apr_pool_t *pool)
{
  svn_error_t *err;

  do {
    err = svn_wc__make_adm_thing (adm_access, SVN_WC__ADM_LOCK,
                                  svn_node_file, APR_OS_DEFAULT, 0, pool);
    if (err)
      {
        if (APR_STATUS_IS_EEXIST(err->apr_err))
          {
            svn_error_clear_all (err);
            apr_sleep (1 * APR_USEC_PER_SEC);  /* micro-seconds */
            wait_for--;
          }
        else
          return err;
      }
    else
      return SVN_NO_ERROR;
  } while (wait_for > 0);

  return svn_error_createf (SVN_ERR_WC_LOCKED, 0, NULL, pool, 
                            "working copy locked: %s", adm_access->path); 
}


static svn_error_t *
svn_wc__unlock (const char *path, apr_pool_t *pool)
{
  return svn_wc__remove_adm_file (path, pool, SVN_WC__ADM_LOCK, NULL);
}

svn_error_t *
svn_wc__adm_steal_write_lock (svn_wc_adm_access_t **adm_access,
                              const char *path,
                              apr_pool_t *pool)
{
  svn_error_t *err;
  svn_wc_adm_access_t *lock = apr_palloc (pool, sizeof (**adm_access));
  lock->type = svn_wc_adm_access_write_lock;
  lock->lock_exists = FALSE;
  lock->path = apr_pstrdup (pool, path);
  lock->pool = pool;

  err = svn_wc__lock (lock, 0, pool);
  if (err)
    {
      if (err->apr_err == SVN_ERR_WC_LOCKED)
        svn_error_clear_all (err);  /* Steal existing lock */
      else
        return err;
    }

  lock->lock_exists = TRUE;
  *adm_access = lock;
  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc_adm_open (svn_wc_adm_access_t **adm_access,
                 const char *path,
                 svn_boolean_t write_lock,
                 apr_pool_t *pool)
{
  svn_wc_adm_access_t *lock = apr_palloc (pool, sizeof (**adm_access));
  lock->lock_exists = FALSE;
  lock->path = apr_pstrdup (pool, path);
  lock->pool = pool;

  if (write_lock)
    {
      lock->type = svn_wc_adm_access_write_lock;
      SVN_ERR (svn_wc__lock (lock, 0, pool));
      lock->lock_exists = TRUE;
    }
  else
    {
      /* ### Read-lock not yet implemented */
      lock->type = svn_wc_adm_access_unlocked;
    }

  *adm_access = lock;
  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc_adm_close (svn_wc_adm_access_t *adm_access)
{
  if (adm_access->type == svn_wc_adm_access_write_lock)
    {
      SVN_ERR (svn_wc__unlock (adm_access->path, adm_access->pool));
      /* Reset to prevent further use of the write lock. */
      adm_access->lock_exists = FALSE;
      adm_access->type = svn_wc_adm_access_unlocked;
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc_adm_write_check (svn_wc_adm_access_t *adm_access)
{
  if (adm_access->type == svn_wc_adm_access_write_lock)
    {
      if (adm_access->lock_exists)
        {
          svn_boolean_t locked;

          /* Check physical lock still exists and hasn't been stolen */
          SVN_ERR (svn_wc_locked (&locked, adm_access->path, adm_access->pool));
          if (! locked)
            return svn_error_createf (SVN_ERR_WC_NOT_LOCKED, 0, NULL,
                                      adm_access->pool, 
                                      "write-lock stolen in: %s",
                                      adm_access->path); 
        }
    }
  else
    {
      /* ### Could try to upgrade the read lock to a write lock here? */
      return svn_error_createf (SVN_ERR_WC_NOT_LOCKED, 0, NULL,
                                adm_access->pool, 
                                "no write-lock in: %s", adm_access->path); 
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc_locked (svn_boolean_t *locked, const char *path, apr_pool_t *pool)
{
  svn_node_kind_t kind;
  const char *lockfile
    = svn_wc__adm_path (path, 0, pool, SVN_WC__ADM_LOCK, NULL);
                                             
  SVN_ERR (svn_io_check_path (lockfile, &kind, pool));
  if (kind == svn_node_file)
    *locked = TRUE;
  else if (kind == svn_node_none)
    *locked = FALSE;
  else
    return svn_error_createf (SVN_ERR_WC_LOCKED, 0, NULL, pool,
                              "svn_wc__locked: "
                              "lock file is not a regular file (%s)",
                              lockfile);
    
  return SVN_NO_ERROR;
}



/* 
 * local variables:
 * eval: (load-file "../../tools/dev/svn-dev.el")
 * end:
 */
