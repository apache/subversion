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
#include "svn_pools.h"


static svn_error_t *
svn_wc__do_adm_close (svn_wc_adm_access_t *adm_access,
                      svn_boolean_t abort);

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

static apr_status_t
svn_wc__adm_access_pool_cleanup (void *p)
{
  svn_wc_adm_access_t *lock = p;
  svn_error_t *err;

  err = svn_wc__do_adm_close (lock, TRUE);

  /* ### Is this the correct way to handle the error? */
  if (err)
    {
      apr_status_t apr_err = err->apr_err;
      svn_error_clear_all (err);
      return apr_err;
    }
  else
    return APR_SUCCESS;
}

static apr_status_t
svn_wc__adm_access_pool_cleanup_child (void *p)
{
  svn_wc_adm_access_t *lock = p;
  apr_pool_cleanup_kill (lock->pool, lock, svn_wc__adm_access_pool_cleanup);
  return APR_SUCCESS;
}

static svn_wc_adm_access_t *
svn_wc__adm_access_alloc (enum svn_wc__adm_access_type type,
                          svn_wc_adm_access_t *parent,
                          const char *path,
                          apr_pool_t *pool)
{
  svn_wc_adm_access_t *lock = apr_palloc (pool, sizeof (*lock));
  lock->type = type;
  lock->parent = parent;
  lock->children = NULL;
  lock->lock_exists = FALSE;
  /* ### Some places lock with a path that is not canonical, we need
     ### cannonical paths for reliable parent-child determination */
  lock->path = svn_path_canonicalize_nts (apr_pstrdup (pool, path), pool);
  lock->pool = pool;

  apr_pool_cleanup_register (lock->pool, lock,
                             svn_wc__adm_access_pool_cleanup,
                             svn_wc__adm_access_pool_cleanup_child);
  return lock;
}

svn_error_t *
svn_wc__adm_steal_write_lock (svn_wc_adm_access_t **adm_access,
                              const char *path,
                              apr_pool_t *pool)
{
  svn_error_t *err;
  svn_wc_adm_access_t *lock
    = svn_wc__adm_access_alloc (svn_wc__adm_access_write_lock, NULL, path,
                                pool);

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

static svn_error_t *
svn_wc__adm_access_child (svn_wc_adm_access_t **adm_access,
                          svn_wc_adm_access_t *parent_access,
                          const char *path,
                          apr_pool_t *pool)
{

  /* PATH must be a child. We only need the result, not the allocated
     name. */
  const char *name = svn_path_is_child (parent_access->path, path, pool);
  if (!name)
    return svn_error_createf (SVN_ERR_WC_INVALID_LOCK, 0, NULL, pool,
                              "lock path is not a child (%s)",
                              path);

  if (parent_access->children)
    *adm_access = apr_hash_get (parent_access->children, path,
                                APR_HASH_KEY_STRING);
  else
    *adm_access = NULL;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc_adm_open (svn_wc_adm_access_t **adm_access,
                 svn_wc_adm_access_t *parent_access,
                 const char *path,
                 svn_boolean_t write_lock,
                 svn_boolean_t tree_lock,
                 apr_pool_t *pool)
{
  svn_wc_adm_access_t *lock;

  if (parent_access)
    {
      SVN_ERR (svn_wc__adm_access_child (&lock, parent_access, path,
                                         pool));
      if (lock)
        /* Already locked */
        return svn_error_createf (SVN_ERR_WC_LOCKED, 0, NULL, pool,
                                  "directory already locked (%s)",
                                  path);
    }

  /* Need to create a new lock */
  if (write_lock)
    {
      lock = svn_wc__adm_access_alloc (svn_wc__adm_access_write_lock,
                                       parent_access, path, pool);
      SVN_ERR (svn_wc__lock (lock, 0, pool));
      lock->lock_exists = TRUE;
    }
  else
    {
      /* ### Read-lock not yet implemented. Since no physical lock gets
         ### created we must check PATH is not a file. */
      enum svn_node_kind node_kind;
      SVN_ERR (svn_io_check_path (path, &node_kind, pool));
      if (node_kind != svn_node_dir)
        return svn_error_createf (SVN_ERR_WC_INVALID_LOCK, 0, NULL, pool,
                                  "lock path is not a directory (%s)",
                                  path);

      lock = svn_wc__adm_access_alloc (svn_wc__adm_access_unlocked,
                                       parent_access, path, pool);
    }

  lock->parent = parent_access;
  if (lock->parent)
    {
      if (! lock->parent->children)
        lock->parent->children = apr_hash_make (lock->parent->pool);

#if 0
      /* ### Need to think about this. Creating an empty APR hash allocates
         ### several hundred bytes. Since the hash key is the complete
         ### path, children for different parents cannot clash. Can we save
         ### memory by reusing the parent's hash? */
      lock->children = lock->parent->children;
#endif

      apr_hash_set (lock->parent->children, lock->path, APR_HASH_KEY_STRING,
                    lock);
    }

  if (tree_lock)
    {
      /* ### Could use this code to initialise the cache, in which case the
         ### subpool should be removed. */
      apr_hash_t *entries;
      apr_hash_index_t *hi;
      apr_pool_t *subpool = svn_pool_create (pool);
      SVN_ERR (svn_wc_entries_read (&entries, path, FALSE, subpool));
      for (hi = apr_hash_first (subpool, entries); hi; hi = apr_hash_next (hi))
        {
          void *val;
          svn_wc_entry_t *entry;
          svn_wc_adm_access_t *entry_access;
          const char *entry_path;
          svn_error_t *svn_err;

          apr_hash_this (hi, NULL, NULL, &val);
          entry = val;
          if (entry->kind != svn_node_dir
              || ! strcmp (entry->name, SVN_WC_ENTRY_THIS_DIR))
            continue;
          entry_path = svn_path_join (lock->path, entry->name, subpool);

          /* Use the main lock's pool here, it needs to persist */
          svn_err = svn_wc_adm_open (&entry_access, lock, entry_path,
                                     write_lock, tree_lock, lock->pool);
          if (svn_err)
            {
              /* Give up any locks we have acquired. */
              svn_wc_adm_close (lock);
              return svn_err;
            }
        }
      svn_pool_destroy (subpool);
    }

  *adm_access = lock;
  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc_adm_retrieve (svn_wc_adm_access_t **adm_access,
                     svn_wc_adm_access_t *parent_access,
                     const char *path,
                     apr_pool_t *pool)
{
  SVN_ERR (svn_wc__adm_access_child (adm_access, parent_access, path, pool));
  if (! *adm_access)
    return svn_error_createf (SVN_ERR_WC_NOT_LOCKED, 0, NULL, pool,
                              "directory not locked (%s)",
                              path);
  return SVN_NO_ERROR;
}

static svn_error_t *
svn_wc__do_adm_close (svn_wc_adm_access_t *adm_access,
                      svn_boolean_t preserve_lock)
{
  apr_hash_index_t *hi;

  apr_pool_cleanup_kill (adm_access->pool, adm_access,
                         svn_wc__adm_access_pool_cleanup);

  /* Close children */
  if (adm_access->children)
    {
      for (hi = apr_hash_first (adm_access->pool, adm_access->children);
           hi;
           hi = apr_hash_next (hi))
        {
          void *val;
          svn_wc_adm_access_t *child_access;
          apr_hash_this (hi, NULL, NULL, &val);
          child_access = val;
          SVN_ERR (svn_wc__do_adm_close (child_access, preserve_lock));
        }
    }

  /* Physically unlock if required */
  if (adm_access->type == svn_wc__adm_access_write_lock)
    {
      if (adm_access->lock_exists && ! preserve_lock)
        {
          SVN_ERR (svn_wc__unlock (adm_access->path, adm_access->pool));
          adm_access->lock_exists = FALSE;
        }
      /* Reset to prevent further use of the write lock. */
      adm_access->type = svn_wc__adm_access_closed;
    }

  /* Detach from parent */
  if (adm_access->parent)
    apr_hash_set (adm_access->parent->children, adm_access->path,
                  APR_HASH_KEY_STRING, NULL);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc_adm_close (svn_wc_adm_access_t *adm_access)
{
  return svn_wc__do_adm_close (adm_access, FALSE);
}

svn_error_t *
svn_wc_adm_write_check (svn_wc_adm_access_t *adm_access)
{
  if (adm_access->type == svn_wc__adm_access_write_lock)
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
