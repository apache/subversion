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


struct svn_wc_adm_access_t
{
   /* PATH to directory which contains the administrative area */
   const char *path;

   enum svn_wc__adm_access_type {

      /* SVN_WC__ADM_ACCESS_UNLOCKED indicates no lock is held allowing
         read-only access without cacheing. */
      svn_wc__adm_access_unlocked,

#if 0 /* How cacheing might work one day */

      /* ### If read-only operations are allowed sufficient write access to
         ### create read locks (did you follow that?) then entries cacheing
         ### could apply to read-only operations as well.  This would
         ### probably want to fall back to unlocked access if the
         ### filesystem permissions prohibit writing to the administrative
         ### area (consider running svn_wc_status on some other user's
         ### working copy). */

      /* SVN_WC__ADM_ACCESS_READ_LOCK indicates that read-only access and
         cacheing are allowed. */
      svn_wc__adm_access_read_lock,
#endif

      /* SVN_WC__ADM_ACCESS_WRITE_LOCK indicates that read-write access and
         cacheing are allowed. */
      svn_wc__adm_access_write_lock,

      /* SVN_WC__ADM_ACCESS_CLOSED indicates that the baton has been
         closed. */
      svn_wc__adm_access_closed

   } type;

   /* LOCK_EXISTS is set TRUE when the write lock exists */
   svn_boolean_t lock_exists;

#if 0 /* How cacheing might work one day */

   /* ENTRIES_MODIFED is set TRUE when the entries cached in ENTRIES have
      been modified from the original values read from the file. */
   svn_boolean_t entries_modified;

   /* Once the 'entries' file has been read, ENTRIES will cache the
      contents if this access baton has an appropriate lock. Otherwise
      ENTRIES will be NULL. */
   apr_hash_t *entries;
#endif

   /* SET is a hash of svn_wc_adm_access_t* keyed on char* representing the
      path to other directories that are also locked. */
   apr_hash_t *set;

   /* POOL is used to allocate cached items, they need to persist for the
      lifetime of this access baton */
   apr_pool_t *pool;

};


static svn_error_t *
do_close (svn_wc_adm_access_t *adm_access, svn_boolean_t preserve_lock);

/* Create a physical lock file in the admin directory for ADM_ACCESS. Wait
   up to WAIT_FOR seconds if the lock already exists retrying every
   second. */
static svn_error_t *
create_lock (svn_wc_adm_access_t *adm_access, int wait_for, apr_pool_t *pool)
{
  svn_error_t *err;

  for (;;)
    {
      err = svn_wc__make_adm_thing (adm_access, SVN_WC__ADM_LOCK,
                                    svn_node_file, APR_OS_DEFAULT, 0, pool);
      if (err)
        {
          if (APR_STATUS_IS_EEXIST(err->apr_err))
            {
              svn_error_clear_all (err);
              if (wait_for <= 0)
                break;
              wait_for--;
              apr_sleep (1 * APR_USEC_PER_SEC);  /* micro-seconds */
            }
          else
            return err;
        }
      else
        return SVN_NO_ERROR;
    }

  return svn_error_createf (SVN_ERR_WC_LOCKED, 0, NULL, pool, 
                            "working copy locked: %s", adm_access->path); 
}


/* Remove the physical lock in the admin directory for PATH. It is
   acceptable for the administrative area to have disappeared, such as when
   the directory is removed from the working copy.  It is an error for the
   lock to have disappeared if the administrative area still exists. */
static svn_error_t *
remove_lock (const char *path, apr_pool_t *pool)
{
  if (svn_wc__adm_path_exists (path, FALSE, pool, NULL))
    SVN_ERR (svn_wc__remove_adm_file (path, pool, SVN_WC__ADM_LOCK, NULL));

  return SVN_NO_ERROR;
}

/* An APR pool cleanup handler.  This handles access batons that have not
   been closed when their pool gets destroyed.  The physical locks
   associated with such batons remain in the working copy. */
static apr_status_t
pool_cleanup (void *p)
{
  svn_wc_adm_access_t *lock = p;
  svn_boolean_t cleanup;
  svn_error_t *err;

  err = svn_wc__adm_is_cleanup_required (&cleanup, lock, lock->pool);
  if (!err)
    err = do_close (lock, cleanup);

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

/* An APR pool cleanup handler.  This is a child handler, it removes the
   main pool handler. */
static apr_status_t
pool_cleanup_child (void *p)
{
  svn_wc_adm_access_t *lock = p;
  apr_pool_cleanup_kill (lock->pool, lock, pool_cleanup);
  return APR_SUCCESS;
}

/* Allocate from POOL, intialise and return an access baton. TYPE and PATH
   are used to initialise the baton.  */
static svn_wc_adm_access_t *
adm_access_alloc (enum svn_wc__adm_access_type type,
                  const char *path,
                  apr_pool_t *pool)
{
  svn_wc_adm_access_t *lock = apr_palloc (pool, sizeof (*lock));
  lock->type = type;
  lock->set = NULL;
  lock->lock_exists = FALSE;
  lock->path = apr_pstrdup (pool, path);
  lock->pool = pool;

  apr_pool_cleanup_register (lock->pool, lock, pool_cleanup,
                             pool_cleanup_child);
  return lock;
}

static void
adm_ensure_set (svn_wc_adm_access_t *adm_access)
{
  if (! adm_access->set)
    {
      adm_access->set = apr_hash_make (adm_access->pool);
      apr_hash_set (adm_access->set, adm_access->path, APR_HASH_KEY_STRING,
                    adm_access);
    }
}

svn_error_t *
svn_wc__adm_steal_write_lock (svn_wc_adm_access_t **adm_access,
                              svn_wc_adm_access_t *associated,
                              const char *path,
                              apr_pool_t *pool)
{
  svn_error_t *err;
  svn_wc_adm_access_t *lock = adm_access_alloc (svn_wc__adm_access_write_lock,
                                                path, pool);

  err = create_lock (lock, 0, pool);
  if (err)
    {
      if (err->apr_err == SVN_ERR_WC_LOCKED)
        svn_error_clear_all (err);  /* Steal existing lock */
      else
        return err;
    }

  if (associated)
    {
      adm_ensure_set (associated);
      lock->set = associated->set;
      apr_hash_set (lock->set, lock->path, APR_HASH_KEY_STRING, lock);
    }

  lock->lock_exists = TRUE;
  *adm_access = lock;
  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc_adm_open (svn_wc_adm_access_t **adm_access,
                 svn_wc_adm_access_t *associated,
                 const char *path,
                 svn_boolean_t write_lock,
                 svn_boolean_t tree_lock,
                 apr_pool_t *pool)
{
  svn_wc_adm_access_t *lock;

  if (associated && associated->set)
    {
      lock = apr_hash_get (associated->set, path, APR_HASH_KEY_STRING);
      if (lock)
        /* Already locked.  The reason we don't return the existing baton
           here is that the user is supposed to know whether a directory is
           locked: if it's not locked call svn_wc_adm_open, if it is locked
           call svn_wc_adm_retrieve.  */
        return svn_error_createf (SVN_ERR_WC_LOCKED, 0, NULL, pool,
                                  "directory already locked (%s)",
                                  path);
    }

  /* Need to create a new lock */
  if (write_lock)
    {
      lock = adm_access_alloc (svn_wc__adm_access_write_lock, path, pool);
      SVN_ERR (create_lock (lock, 0, pool));
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

      lock = adm_access_alloc (svn_wc__adm_access_unlocked, path, pool);
    }

  if (associated)
    adm_ensure_set (associated);

  if (tree_lock)
    {
      /* ### Use this code to initialise the cache? */
      apr_hash_t *entries;
      apr_hash_index_t *hi;
      apr_pool_t *subpool = svn_pool_create (pool);

      SVN_ERR (svn_wc_entries_read (&entries, lock->path, FALSE, subpool));

      /* Use a temporary hash until all children have been opened. */
      if (associated)
        lock->set = apr_hash_make (subpool);

      /* Open the tree */
      for (hi = apr_hash_first (subpool, entries); hi; hi = apr_hash_next (hi))
        {
          void *val;
          svn_wc_entry_t *entry;
          svn_wc_adm_access_t *entry_access;
          const char *entry_path;
          svn_error_t *svn_err;
          svn_node_kind_t kind;

          apr_hash_this (hi, NULL, NULL, &val);
          entry = val;
          if (entry->kind != svn_node_dir
              || ! strcmp (entry->name, SVN_WC_ENTRY_THIS_DIR))
            continue;
          entry_path = svn_path_join (lock->path, entry->name, subpool);

          /* If this is not physically a directory, it may have been
             deleted say, then ignore it. */
          SVN_ERR (svn_io_check_path (entry_path, &kind, pool));
          if (kind != svn_node_dir)
            continue;

          /* Don't use the subpool pool here, the lock needs to persist */
          svn_err = svn_wc_adm_open (&entry_access, lock, entry_path,
                                     write_lock, tree_lock, lock->pool);
          if (svn_err)
            {
              /* This closes all the children in temporary hash as well */
              svn_wc_adm_close (lock);
              svn_pool_destroy (subpool);
              return svn_err;
            }
        }

      /* Switch from temporary hash to permanent hash */
      if (associated)
        {
          for (hi = apr_hash_first (subpool, lock->set);
               hi;
               hi = apr_hash_next (hi))
            {
              const void *key;
              void *val;
              const char *entry_path;
              svn_wc_adm_access_t *entry_access;

              apr_hash_this (hi, &key, NULL, &val);
              entry_path = key;
              entry_access = val;
              apr_hash_set (associated->set, entry_path, APR_HASH_KEY_STRING,
                            entry_access);
              entry_access->set = associated->set;
            }
          lock->set = associated->set;
        }
      svn_pool_destroy (subpool);
    }

  if (associated)
    {
      lock->set = associated->set;
      apr_hash_set (lock->set, lock->path, APR_HASH_KEY_STRING, lock);
    }

  *adm_access = lock;
  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc_adm_retrieve (svn_wc_adm_access_t **adm_access,
                     svn_wc_adm_access_t *associated,
                     const char *path,
                     apr_pool_t *pool)
{
  if (associated->set)
    *adm_access = apr_hash_get (associated->set, path, APR_HASH_KEY_STRING);
  else if (! strcmp (associated->path, path))
    *adm_access = associated;
  else
    *adm_access = NULL;
  if (! *adm_access)
    return svn_error_createf (SVN_ERR_WC_NOT_LOCKED, 0, NULL, pool,
                              "directory not locked (%s)",
                              path);
  return SVN_NO_ERROR;
}

/* Does the work of closing the access baton ADM_ACCESS.  Any physical
   locks are removed from the working copy if PRESERVE_LOCK is FALSE, or
   are left if PRESERVE_LOCK is TRUE.  Any associated access batons that
   are direct descendents will also be closed. */
static svn_error_t *
do_close (svn_wc_adm_access_t *adm_access,
          svn_boolean_t preserve_lock)
{
  apr_hash_index_t *hi;

  apr_pool_cleanup_kill (adm_access->pool, adm_access, pool_cleanup);

  /* Close children */
  if (adm_access->set)
    {
      /* The documentation says that modifying a hash while iterating over
         it is allowed but unpredictable!  So, first loop to identify and
         copy direct descendents, second loop to close them. */
      int i;
      apr_array_header_t *children = apr_array_make (adm_access->pool, 1,
                                                     sizeof (adm_access));

      for (hi = apr_hash_first (adm_access->pool, adm_access->set);
           hi;
           hi = apr_hash_next (hi))
        {
          void *val;
          svn_wc_adm_access_t *associated;
          const char *name;
          apr_hash_this (hi, NULL, NULL, &val);
          associated = val;
          name = svn_path_is_child (adm_access->path, associated->path,
                                    adm_access->pool);
          if (name && svn_path_is_single_path_component (name))
            {
              *(svn_wc_adm_access_t**)apr_array_push (children) = associated;
              /* Deleting current element is allowed and predictable */
              apr_hash_set (adm_access->set, associated->path,
                            APR_HASH_KEY_STRING, NULL);
            }
        }
      for (i = 0; i < children->nelts; ++i)
        {
          svn_wc_adm_access_t *child = APR_ARRAY_IDX(children, i,
                                                     svn_wc_adm_access_t*);
          SVN_ERR (do_close (child, preserve_lock));
        }
    }

  /* Physically unlock if required */
  if (adm_access->type == svn_wc__adm_access_write_lock)
    {
      if (adm_access->lock_exists && ! preserve_lock)
        {
          SVN_ERR (remove_lock (adm_access->path, adm_access->pool));
          adm_access->lock_exists = FALSE;
        }
      /* Reset to prevent further use of the write lock. */
      adm_access->type = svn_wc__adm_access_closed;
    }

  /* Detach from set */
  if (adm_access->set)
    apr_hash_set (adm_access->set, adm_access->path, APR_HASH_KEY_STRING, NULL);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc_adm_close (svn_wc_adm_access_t *adm_access)
{
  return do_close (adm_access, FALSE);
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
                              "svn_wc_locked: "
                              "lock file is not a regular file (%s)",
                              lockfile);
    
  return SVN_NO_ERROR;
}


const char *
svn_wc_adm_access_path (svn_wc_adm_access_t *adm_access)
{
  return adm_access->path;
}

apr_pool_t *
svn_wc_adm_access_pool (svn_wc_adm_access_t *adm_access)
{
  return adm_access->pool;
}


svn_error_t *
svn_wc__adm_is_cleanup_required (svn_boolean_t *cleanup,
                                 svn_wc_adm_access_t *adm_access,
                                 apr_pool_t *pool)
{
  enum svn_node_kind kind;
  const char *log_path = svn_wc__adm_path (svn_wc_adm_access_path (adm_access),
                                           FALSE, pool, SVN_WC__ADM_LOG, NULL);

  /* The presence of a log file demands cleanup */
  SVN_ERR (svn_io_check_path (log_path, &kind, pool));
  *cleanup = (kind == svn_node_file);

  return SVN_NO_ERROR;
}



/* 
 * local variables:
 * eval: (load-file "../../tools/dev/svn-dev.el")
 * end:
 */
