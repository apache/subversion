/*
 * lock.c:  routines for locking working copy subdirectories.
 *
 * ====================================================================
 * Copyright (c) 2000-2004 CollabNet.  All rights reserved.
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

#include <assert.h>

#include <apr_pools.h>
#include <apr_time.h>

#include "svn_pools.h"
#include "svn_path.h"

#include "wc.h"
#include "adm_files.h"
#include "questions.h"

#include "svn_private_config.h"



struct svn_wc_adm_access_t
{
  /* PATH to directory which contains the administrative area */
  const char *path;

  enum svn_wc__adm_access_type {

    /* SVN_WC__ADM_ACCESS_UNLOCKED indicates no lock is held allowing
       read-only access */
    svn_wc__adm_access_unlocked,

    /* SVN_WC__ADM_ACCESS_WRITE_LOCK indicates that a write lock is held
       allowing read-write access */
    svn_wc__adm_access_write_lock,

    /* SVN_WC__ADM_ACCESS_CLOSED indicates that the baton has been
       closed. */
    svn_wc__adm_access_closed

  } type;

  /* LOCK_EXISTS is set TRUE when the write lock exists */
  svn_boolean_t lock_exists;

  /* SET_OWNER is TRUE if SET is allocated from this access baton */
  svn_boolean_t set_owner;

  /* The working copy format version number for the directory */
  int wc_format;

  /* SET is a hash of svn_wc_adm_access_t* keyed on char* representing the
     path to directories that are open. */
  apr_hash_t *set;

  /* ENTRIES is the cached entries for PATH, without those in state
     deleted. ENTRIES_HIDDEN is the cached entries including those in
     state deleted or state absent. Either may be NULL. */
  apr_hash_t *entries;
  apr_hash_t *entries_hidden;

  /* POOL is used to allocate cached items, they need to persist for the
     lifetime of this access baton */
  apr_pool_t *pool;

};

/* This is a placeholder used in the set hash to represent missing
   directories.  Only its address is important, it contains no useful
   data. */
static svn_wc_adm_access_t missing;


static svn_error_t *
do_close (svn_wc_adm_access_t *adm_access, svn_boolean_t preserve_lock);


/* Maybe upgrade the working copy directory represented by ADM_ACCESS
   to the latest 'SVN_WC__VERSION'.  ADM_ACCESS must contain a write
   lock.  Use POOL for all temporary allocation.

   Not all upgrade paths are necessarily supported.  For example,
   upgrading a version 1 working copy results in an error.

   Sometimes the format file can contain "0" while the administrative
   directory is being constructed; calling this on a format 0 working
   copy has no effect and returns no error. */
static svn_error_t *
maybe_upgrade_format (svn_wc_adm_access_t *adm_access, apr_pool_t *pool)
{
  SVN_ERR (svn_wc__check_format (adm_access->wc_format,
                                 adm_access->path,
                                 pool));

  /* We can upgrade all formats that are accepted by
     svn_wc__check_format. */
  if (adm_access->wc_format != SVN_WC__VERSION)
    {
      const char *path = svn_wc__adm_path (adm_access->path, FALSE, pool,
                                           SVN_WC__ADM_FORMAT, NULL);

      SVN_ERR (svn_io_write_version_file (path, SVN_WC__VERSION, pool));
      adm_access->wc_format = SVN_WC__VERSION;
    }

  return SVN_NO_ERROR;
}


/* Create a physical lock file in the admin directory for ADM_ACCESS. Wait
   up to WAIT_FOR seconds if the lock already exists retrying every
   second. 

   Note: most callers of this function determine the wc_format for the
   lock soon afterwards.  We recommend calling maybe_upgrade_format()
   as soon as you have the wc_format for a lock, since that's a good
   opportunity to drag old working directories into the modern era. */
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
              svn_error_clear (err);
              if (wait_for <= 0)
                break;
              wait_for--;
              apr_sleep (apr_time_from_sec(1));  /* micro-seconds */
            }
          else
            return err;
        }
      else
        return SVN_NO_ERROR;
    }

  return svn_error_createf (SVN_ERR_WC_LOCKED, NULL,
                            _("Working copy '%s' locked"),
                            svn_path_local_style (adm_access->path, pool));
}


/* Remove the physical lock in the admin directory for PATH. It is
   acceptable for the administrative area to have disappeared, such as when
   the directory is removed from the working copy.  It is an error for the
   lock to have disappeared if the administrative area still exists. */
static svn_error_t *
remove_lock (const char *path, apr_pool_t *pool)
{
  svn_error_t *err = svn_wc__remove_adm_file (path, pool, SVN_WC__ADM_LOCK,
                                              NULL);
  if (err)
    {
      if (svn_wc__adm_path_exists (path, FALSE, pool, NULL))
        return err;
      svn_error_clear (err);
    }
  return SVN_NO_ERROR;
}

/* An APR pool cleanup handler.  This handles access batons that have not
   been closed when their pool gets destroyed.  The physical locks
   associated with such batons remain in the working copy if they are
   protecting a log file. */
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
      svn_error_clear (err);
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
  lock->entries = NULL;
  lock->entries_hidden = NULL;
  lock->wc_format = 0;
  lock->set = NULL;
  lock->lock_exists = FALSE;
  lock->set_owner = FALSE;
  lock->path = apr_pstrdup (pool, path);
  lock->pool = pool;

  return lock;
}

static void
adm_ensure_set (svn_wc_adm_access_t *adm_access)
{
  if (! adm_access->set)
    {
      adm_access->set_owner = TRUE;
      adm_access->set = apr_hash_make (adm_access->pool);
      apr_hash_set (adm_access->set, adm_access->path, APR_HASH_KEY_STRING,
                    adm_access);
    }
}

static svn_error_t *
probe (const char **dir,
       const char *path,
       int *wc_format,
       apr_pool_t *pool)
{
  svn_node_kind_t kind;

  SVN_ERR (svn_io_check_path (path, &kind, pool));
  if (kind == svn_node_dir)
    SVN_ERR (svn_wc_check_wc (path, wc_format, pool));
  else
    *wc_format = 0;

  /* a "version" of 0 means a non-wc directory */
  if (kind != svn_node_dir || *wc_format == 0)
    {
      /* Passing a path ending in "." or ".." to svn_path_dirname() is
         probably always a bad idea; certainly it is in this case.
         Unfortunately, svn_path_dirname()'s current signature can't
         return an error, so we have to insert the protection in this
         caller, as making the larger API change would be very
         destabilizing right now (just before 1.0).  See issue #1617. */
      const char *base_name = svn_path_basename (path, pool);
      if ((strcmp (base_name, "..") == 0)
          || (strcmp (base_name, ".") == 0))
        {
          return svn_error_createf
            (SVN_ERR_WC_BAD_PATH, NULL,
             _("Path '%s' ends in '%s', "
               "which is unsupported for this operation"),
             svn_path_local_style (path, pool), base_name);
        }

      *dir = svn_path_dirname (path, pool);
    }
  else
    *dir = path;

  return SVN_NO_ERROR;
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
        svn_error_clear (err);  /* Steal existing lock */
      else
        return err;
    }

  if (associated)
    {
      adm_ensure_set (associated);
      lock->set = associated->set;
      apr_hash_set (lock->set, lock->path, APR_HASH_KEY_STRING, lock);
    }

  /* We have a write lock.  If the working copy has an old
     format, this is the time to upgrade it. */
  SVN_ERR (svn_wc_check_wc (path, &lock->wc_format, pool));
  SVN_ERR (maybe_upgrade_format (lock, pool));
  
  lock->lock_exists = TRUE;
  *adm_access = lock;
  return SVN_NO_ERROR;
}

/* This is essentially the guts of svn_wc_adm_open2, with the additional
 * parameter UNDER_CONSTRUCTION that gets set TRUE only when locking the
 * admin directory during initial creation.
 */
static svn_error_t *
do_open (svn_wc_adm_access_t **adm_access,
         svn_wc_adm_access_t *associated,
         const char *path,
         svn_boolean_t write_lock,
         int depth,
         svn_boolean_t under_construction,
         apr_pool_t *pool)
{
  svn_wc_adm_access_t *lock;
  int wc_format;
  svn_error_t *err;

  if (associated)
    {
      adm_ensure_set (associated);

      lock = apr_hash_get (associated->set, path, APR_HASH_KEY_STRING);
      if (lock && lock != &missing)
        /* Already locked.  The reason we don't return the existing baton
           here is that the user is supposed to know whether a directory is
           locked: if it's not locked call svn_wc_adm_open, if it is locked
           call svn_wc_adm_retrieve.  */
        return svn_error_createf (SVN_ERR_WC_LOCKED, NULL,
                                  _("Working copy '%s' locked"),
                                  svn_path_local_style (path, pool));
    }

  if (! under_construction)
    {
      /* By reading the format file we check both that PATH is a directory
         and that it is a working copy. */
      err = svn_io_read_version_file (&wc_format,
                                      svn_wc__adm_path (path, FALSE, pool,
                                                        SVN_WC__ADM_FORMAT,
                                                        NULL),
                                      pool);
      if (err)
        {
          /* Should we attempt to distinguish certain errors? */
          svn_error_clear (err);
          return svn_error_createf (SVN_ERR_WC_NOT_DIRECTORY, NULL,
                                    _("'%s' is not a working copy"),
                                    svn_path_local_style (path, pool));
        }

      SVN_ERR (svn_wc__check_format (wc_format,
                                     svn_path_local_style (path, pool),
                                     pool));
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
      lock = adm_access_alloc (svn_wc__adm_access_unlocked, path, pool);
    }

  if (! under_construction)
    {
      lock->wc_format = wc_format;
      if (write_lock)
        SVN_ERR (maybe_upgrade_format (lock, pool));
    }

  if (depth != 0)
    {
      apr_hash_t *entries;
      apr_hash_index_t *hi;
      apr_pool_t *subpool = svn_pool_create (pool);

      /* Reduce depth since we are about to recurse */
      if (depth > 0)
        depth--;
      
      SVN_ERR (svn_wc_entries_read (&entries, lock, FALSE, subpool));

      /* Use a temporary hash until all children have been opened. */
      if (associated)
        lock->set = apr_hash_make (subpool);

      /* Open the tree */
      for (hi = apr_hash_first (subpool, entries); hi; hi = apr_hash_next (hi))
        {
          void *val;
          const svn_wc_entry_t *entry;
          svn_wc_adm_access_t *entry_access;
          const char *entry_path;

          apr_hash_this (hi, NULL, NULL, &val);
          entry = val;
          if (entry->kind != svn_node_dir
              || ! strcmp (entry->name, SVN_WC_ENTRY_THIS_DIR))
            continue;
          entry_path = svn_path_join (lock->path, entry->name, subpool);

          /* Don't use the subpool pool here, the lock needs to persist */
          err = do_open (&entry_access, lock, entry_path, write_lock, depth,
                         FALSE, lock->pool);
          if (err)
            {
              if (err->apr_err != SVN_ERR_WC_NOT_DIRECTORY)
                {
                  /* This closes all the children in temporary hash as well */
                  svn_error_clear (svn_wc_adm_close (lock));
                  svn_pool_destroy (subpool);
                  return err;
                }

              /* It's missing or obstructed, so store a placeholder */
              svn_error_clear (err);
              adm_ensure_set (lock);
              apr_hash_set (lock->set, apr_pstrdup (lock->pool, entry_path),
                            APR_HASH_KEY_STRING, &missing);

              continue;
            }

          /* ### Perhaps we should verify that the parent and child agree
             ### about the URL of the child? */
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

  /* It's important that the cleanup handler is registered *after* at least
     one UTF8 conversion has been done, since such a conversion may create
     the apr_xlate_t object in the pool, and that object must be around
     when the cleanup handler runs.  If the apr_xlate_t cleanup handler
     were to run *before* the access baton cleanup handler, then the access
     baton's handler won't work. */
  apr_pool_cleanup_register (lock->pool, lock, pool_cleanup,
                             pool_cleanup_child);
  *adm_access = lock;
  return SVN_NO_ERROR;
}

/* To preserve API compatibility with Subversion 1.0.0 */
svn_error_t *
svn_wc_adm_open (svn_wc_adm_access_t **adm_access,
                 svn_wc_adm_access_t *associated,
                 const char *path,
                 svn_boolean_t write_lock,
                 svn_boolean_t tree_lock,
                 apr_pool_t *pool)
{
  return svn_wc_adm_open2 (adm_access, associated, path, write_lock,
                           (tree_lock ? -1 : 0), pool);
}

svn_error_t *
svn_wc_adm_open2 (svn_wc_adm_access_t **adm_access,
                  svn_wc_adm_access_t *associated,
                  const char *path,
                  svn_boolean_t write_lock,
                  int depth,
                  apr_pool_t *pool)
{
  return do_open (adm_access, associated, path, write_lock, depth, FALSE,
                  pool);
}

svn_error_t *
svn_wc__adm_pre_open (svn_wc_adm_access_t **adm_access,
                      const char *path,
                      apr_pool_t *pool)
{
  return do_open (adm_access, NULL, path, TRUE, 0, TRUE, pool);
}


/* To preserve API compatibility with Subversion 1.0.0 */
svn_error_t *
svn_wc_adm_probe_open (svn_wc_adm_access_t **adm_access,
                       svn_wc_adm_access_t *associated,
                       const char *path,
                       svn_boolean_t write_lock,
                       svn_boolean_t tree_lock,
                       apr_pool_t *pool)
{
  return svn_wc_adm_probe_open2 (adm_access, associated, path,
                                 write_lock, (tree_lock ? -1 : 0), pool);
}


svn_error_t *
svn_wc_adm_probe_open2 (svn_wc_adm_access_t **adm_access,
                        svn_wc_adm_access_t *associated,
                        const char *path,
                        svn_boolean_t write_lock,
                        int depth,
                        apr_pool_t *pool)
{
  svn_error_t *err;
  const char *dir;
  int wc_format;

  SVN_ERR (probe (&dir, path, &wc_format, pool));

  /* If we moved up a directory, then the path is not a directory, or it
     is not under version control. In either case, the notion of a depth
     does not apply to the provided path. Disable it so that we don't end
     up trying to lock more than we need.  */
  if (dir != path)
    depth = 0;

  err = svn_wc_adm_open2 (adm_access, associated, dir, write_lock, 
                          depth, pool);
  if (err)
    {
      svn_error_t *err2;

      /* If we got an error on the parent dir, that means we failed to
         get an access baton for the child in the first place.  And if
         the reason we couldn't get the child access baton is that the
         child is not a versioned directory, then return an error
         about the child, not the parent. */ 
      svn_node_kind_t child_kind;
      if ((err2 = svn_io_check_path (path, &child_kind, pool)))
        {
          svn_error_compose (err, err2);
          return err;
        }
  
      if ((dir != path)
          && (child_kind == svn_node_dir)
          && (err->apr_err == SVN_ERR_WC_NOT_DIRECTORY))
        {
          svn_error_clear (err);
          return svn_error_createf (SVN_ERR_WC_NOT_DIRECTORY, NULL,
                                    _("'%s' is not a working copy"),
                                    svn_path_local_style (path, pool));
        }
      else
        {
          return err;
        }
    }

  if (wc_format && ! (*adm_access)->wc_format)
    (*adm_access)->wc_format = wc_format;

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__adm_retrieve_internal (svn_wc_adm_access_t **adm_access,
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

  if (*adm_access == &missing)
    *adm_access = NULL;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc_adm_retrieve (svn_wc_adm_access_t **adm_access,
                     svn_wc_adm_access_t *associated,
                     const char *path,
                     apr_pool_t *pool)
{
  SVN_ERR (svn_wc__adm_retrieve_internal (adm_access, associated, path, pool));

  /* Most of the code expects access batons to exist, so returning an error
     generally makes the calling code simpler as it doesn't need to check
     for NULL batons. */
  if (! *adm_access)
    return svn_error_createf (SVN_ERR_WC_NOT_LOCKED, NULL,
                              _("Working copy '%s' is missing or not locked"),
                              svn_path_local_style (path, pool));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc_adm_probe_retrieve (svn_wc_adm_access_t **adm_access,
                           svn_wc_adm_access_t *associated,
                           const char *path,
                           apr_pool_t *pool)
{
  const char *dir;
  int wc_format;

  SVN_ERR (probe (&dir, path, &wc_format, pool));
  SVN_ERR (svn_wc_adm_retrieve (adm_access, associated, dir, pool));

  if (wc_format && ! (*adm_access)->wc_format)
    (*adm_access)->wc_format = wc_format;

  return SVN_NO_ERROR;
}


/* To preserve API compatibility with Subversion 1.0.0 */
svn_error_t *
svn_wc_adm_probe_try (svn_wc_adm_access_t **adm_access,
                      svn_wc_adm_access_t *associated,
                      const char *path,
                      svn_boolean_t write_lock,
                      svn_boolean_t tree_lock,
                      apr_pool_t *pool)
{
  return svn_wc_adm_probe_try2 (adm_access, associated, path, write_lock,
                                (tree_lock ? -1 : 0), pool);
}

svn_error_t *
svn_wc_adm_probe_try2 (svn_wc_adm_access_t **adm_access,
                       svn_wc_adm_access_t *associated,
                       const char *path,
                       svn_boolean_t write_lock,
                       int depth,
                       apr_pool_t *pool)
{
  svn_error_t *err;

  err = svn_wc_adm_probe_retrieve (adm_access, associated, path, pool);

  /* SVN_ERR_WC_NOT_LOCKED would mean there was no access baton for
     path in associated, in which case we want to open an access
     baton and add it to associated. */
  if (err && (err->apr_err == SVN_ERR_WC_NOT_LOCKED))
    {
      svn_error_clear (err);
      err = svn_wc_adm_probe_open2 (adm_access, associated,
                                    path, write_lock, depth,
                                    svn_wc_adm_access_pool (associated));

      /* If the path is not a versioned directory, we just return a
         null access baton with no error.  Note that of the errors we
         do report, the most important (and probably most likely) is
         SVN_ERR_WC_LOCKED.  That error would mean that someone else
         has this area locked, and we definitely want to bail in that
         case. */
      if (err && (err->apr_err == SVN_ERR_WC_NOT_DIRECTORY))
        {
          svn_error_clear (err);
          *adm_access = NULL;
          err = NULL;
        }
    }

  return err;
}

/* A helper for svn_wc_adm_open_anchor.  Add all the access batons in the
   T_ACCESS set, including T_ACCESS, to the P_ACCESS set. */
static void join_batons (svn_wc_adm_access_t *p_access,
                         svn_wc_adm_access_t *t_access,
                         apr_pool_t *pool)
{
  apr_hash_index_t *hi;

  adm_ensure_set (p_access);
  if (! t_access->set)
    {
      t_access->set = p_access->set;
      apr_hash_set (p_access->set, t_access->path, APR_HASH_KEY_STRING,
                    t_access);
      return;
    }

  for (hi = apr_hash_first (pool, t_access->set); hi; hi = apr_hash_next (hi))
    {
      const void *key;
      void *val;
      svn_wc_adm_access_t *adm_access;
      apr_hash_this (hi, &key, NULL, &val);
      adm_access = val;
      if (adm_access != &missing)
        adm_access->set = p_access->set;
      apr_hash_set (p_access->set, key, APR_HASH_KEY_STRING, adm_access);
    }
  t_access->set_owner = FALSE;
}

svn_error_t *
svn_wc_adm_open_anchor (svn_wc_adm_access_t **anchor_access,
                        svn_wc_adm_access_t **target_access,
                        const char **target,
                        const char *path,
                        svn_boolean_t write_lock,
                        int depth,
                        apr_pool_t *pool)
{
  const char *base_name = svn_path_basename (path, pool);

  /* ### This check looks out of place here, perhaps it should be in
     svn_path_canonicalize */
  if ((strcmp (base_name, "..") == 0) || (strcmp (base_name, ".") == 0))
    return svn_error_createf
      (SVN_ERR_WC_BAD_PATH, NULL,
       _("Path '%s' ends in '%s', which is unsupported for this operation"),
       svn_path_local_style (path, pool), base_name);

  if (svn_path_is_empty (path) || ! strcmp (path, "/"))
    {
      SVN_ERR (do_open (anchor_access, NULL, path, write_lock, depth, FALSE,
                        pool));
      *target_access = *anchor_access;
      *target = "";
    }
  else
    {
      svn_error_t *err;
      svn_wc_adm_access_t *p_access, *t_access;
      const char *parent = svn_path_dirname (path, pool);
      svn_error_t *p_access_err = SVN_NO_ERROR;

      /* Try to open parent of PATH to setup P_ACCESS */
      err = do_open (&p_access, NULL, parent, write_lock, 0, FALSE, pool);
      if (err)
        {
          if (err->apr_err == SVN_ERR_WC_NOT_DIRECTORY)
            {
              svn_error_clear (err);
              p_access = NULL;
            }
          else if (write_lock && (err->apr_err == SVN_ERR_WC_LOCKED
                                  || APR_STATUS_IS_EACCES (err->apr_err)))
            {
              /* If P_ACCESS isn't to be returned then a read-only baton
                 will do for now, but keep the error in case we need it. */
              svn_error_t *err2 = do_open (&p_access, NULL, parent, FALSE, 0,
                                           FALSE, pool);
              if (err2)
                {
                  svn_error_clear (err2);
                  return err;
                }
              p_access_err = err;
            }
          else
            return err;
        }

      /* Try to open PATH to setup T_ACCESS */
      err = do_open (&t_access, NULL, path, write_lock, depth, FALSE, pool);
      if (err)
        {
          if (! p_access || err->apr_err != SVN_ERR_WC_NOT_DIRECTORY)
            {
              if (p_access)
                svn_error_clear (do_close (p_access, FALSE));
              svn_error_clear (p_access_err);
              return err;
            }

          svn_error_clear (err);
          t_access = NULL;
        }

      /* At this stage might have P_ACCESS, T_ACCESS or both */

      /* Check for switched or disjoint P_ACCESS and T_ACCESS */
      if (p_access && t_access)
        {
          const svn_wc_entry_t *t_entry, *p_entry, *t_entry_in_p;

          err = svn_wc_entry (&t_entry_in_p, path, p_access, FALSE, pool);
          if (! err)
            err = svn_wc_entry (&t_entry, path, t_access, FALSE, pool);
          if (! err)
            err = svn_wc_entry (&p_entry, parent, p_access, FALSE, pool);
          if (err)
            {
              svn_error_clear (p_access_err);
              svn_error_clear (do_close (p_access, FALSE));
              svn_error_clear (do_close (t_access, FALSE));
              return err;
            }

          /* Disjoint won't have PATH in P_ACCESS, switched will have
             incompatible URLs */
          if (! t_entry_in_p
              ||
              (p_entry->url && t_entry->url
               && (strcmp (svn_path_dirname (t_entry->url, pool), p_entry->url)
                   || strcmp (svn_path_uri_encode (base_name, pool), 
                              svn_path_basename (t_entry->url, pool)))))
            {
              /* Switched or disjoint, so drop P_ACCESS */
              err = do_close (p_access, FALSE);
              if (err)
                {
                  svn_error_clear (p_access_err);
                  svn_error_clear (do_close (t_access, FALSE));
                  return err;
                }
              p_access = NULL;
            }
        }

      if (p_access)
        {
          if (p_access_err)
            {
              /* Need P_ACCESS, so the read-only temporary won't do */
              if (t_access)
                svn_error_clear (do_close (t_access, FALSE));
              svn_error_clear (do_close (p_access, FALSE));
              return p_access_err;
            }
          else if (t_access)
            join_batons (p_access, t_access, pool);
        }
      svn_error_clear (p_access_err);

      if (! t_access)
        {
          const svn_wc_entry_t *t_entry;
          err = svn_wc_entry (&t_entry, path, p_access, FALSE, pool);
          if (err)
            {
              if (p_access)
                svn_error_clear (do_close (p_access, FALSE));
              return err;
            }
          if (t_entry && t_entry->kind == svn_node_dir)
            {
              adm_ensure_set (p_access);
              apr_hash_set (p_access->set, apr_pstrdup (p_access->pool, path),
                            APR_HASH_KEY_STRING, &missing);
            }
        }

      *anchor_access = p_access ? p_access : t_access;
      *target_access = t_access ? t_access : p_access;

      if (! p_access)
        *target = "";
      else
        *target = base_name;
    }

  return SVN_NO_ERROR;
}


/* Does the work of closing the access baton ADM_ACCESS.  Any physical
   locks are removed from the working copy if PRESERVE_LOCK is FALSE, or
   are left if PRESERVE_LOCK is TRUE.  Any associated access batons that
   are direct descendents will also be closed.

   ### FIXME: If the set has a "hole", say it contains locks for the
   ### directories A, A/B, A/B/C/X but not A/B/C then closing A/B will not
   ### reach A/B/C/X .
 */
static svn_error_t *
do_close (svn_wc_adm_access_t *adm_access,
          svn_boolean_t preserve_lock)
{
  apr_hash_index_t *hi;

  if (adm_access->type == svn_wc__adm_access_closed)
    return SVN_NO_ERROR;

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
          const void *key;
          void *val;
          const char *path;
          svn_wc_adm_access_t *associated;
          const char *name;
          apr_hash_this (hi, &key, NULL, &val);
          path = key;
          associated = val;
          name = svn_path_is_child (adm_access->path, path, adm_access->pool);
          if (name && svn_path_is_single_path_component (name))
            {
              if (associated != &missing)
                *(svn_wc_adm_access_t**)apr_array_push (children) = associated;
              /* Deleting current element is allowed and predictable */
              apr_hash_set (adm_access->set, path, APR_HASH_KEY_STRING, NULL);
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
    {
      apr_hash_set (adm_access->set, adm_access->path, APR_HASH_KEY_STRING,
                    NULL);

      assert (! adm_access->set_owner || apr_hash_count (adm_access->set) == 0);
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc_adm_close (svn_wc_adm_access_t *adm_access)
{
  return do_close (adm_access, FALSE);
}

svn_boolean_t
svn_wc_adm_locked (svn_wc_adm_access_t *adm_access)
{
  return adm_access->type == svn_wc__adm_access_write_lock;
}

svn_error_t *
svn_wc__adm_write_check (svn_wc_adm_access_t *adm_access)
{
  if (adm_access->type == svn_wc__adm_access_write_lock)
    {
      if (adm_access->lock_exists)
        {
          /* Check physical lock still exists and hasn't been stolen.  This
             really is paranoia, I have only ever seen one report of this
             triggering (from someone using the 0.25 release) and that was
             never reproduced.  The check accesses the physical filesystem
             so it is expensive, but it only runs when we are going to
             modify the admin area.  If it ever proves to be a bottleneck
             the physical check could be removed, just leaving the logical
             check. */
          svn_boolean_t locked;

          SVN_ERR (svn_wc_locked (&locked, adm_access->path, adm_access->pool));
          if (! locked)
            return svn_error_createf (SVN_ERR_WC_NOT_LOCKED, NULL, 
                                      _("Write-lock stolen in '%s'"),
                                      svn_path_local_style (adm_access->path,
                                                            adm_access->pool));
        }
    }
  else
    {
      return svn_error_createf (SVN_ERR_WC_NOT_LOCKED, NULL, 
                                _("No write-lock in '%s'"),
                                svn_path_local_style (adm_access->path,
                                                      adm_access->pool));
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
    return svn_error_createf (SVN_ERR_WC_LOCKED, NULL,
                              _("Lock file '%s' is not a regular file"),
                              svn_path_local_style (lockfile, pool));
    
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
  if (adm_access->type == svn_wc__adm_access_write_lock)
    {
      svn_node_kind_t kind;
      const char *log_path
        = svn_wc__adm_path (svn_wc_adm_access_path (adm_access),
                            FALSE, pool, SVN_WC__ADM_LOG, NULL);

      /* The presence of a log file demands cleanup */
      SVN_ERR (svn_io_check_path (log_path, &kind, pool));
      *cleanup = (kind == svn_node_file);
    }
  else
    *cleanup = FALSE;

  return SVN_NO_ERROR;
}

/* Ensure that the cache for the pruned hash (no deleted entries) in
   ADM_ACCESS is valid if the full hash is cached.  POOL is used for
   local, short term, memory allocation.

   ### Should this sort of processing be in entries.c? */
static void
prune_deleted (svn_wc_adm_access_t *adm_access,
               apr_pool_t *pool)
{
  if (! adm_access->entries && adm_access->entries_hidden)
    {
      apr_hash_index_t *hi;

      /* I think it will be common for there to be no deleted entries, so
         it is worth checking for that case as we can optimise it. */
      for (hi = apr_hash_first (pool, adm_access->entries_hidden);
           hi;
           hi = apr_hash_next (hi))
        {
          void *val;
          const svn_wc_entry_t *entry;
          apr_hash_this (hi, NULL, NULL, &val);
          entry = val;
          if ((entry->deleted
               && (entry->schedule != svn_wc_schedule_add)
               && (entry->schedule != svn_wc_schedule_replace))
              || entry->absent)
            break;
        }

      if (! hi)
        {
          /* There are no deleted entries, so we can use the full hash */
          adm_access->entries = adm_access->entries_hidden;
          return;
        }

      /* Construct pruned hash without deleted entries */
      adm_access->entries = apr_hash_make (adm_access->pool);
      for (hi = apr_hash_first (pool, adm_access->entries_hidden);
           hi;
           hi = apr_hash_next (hi))
        {
          void *val;
          const void *key;
          const svn_wc_entry_t *entry;

          apr_hash_this (hi, &key, NULL, &val);
          entry = val;
          if (((entry->deleted == FALSE) && (entry->absent == FALSE))
              || (entry->schedule == svn_wc_schedule_add)
              || (entry->schedule == svn_wc_schedule_replace))
            {
              apr_hash_set (adm_access->entries, key,
                            APR_HASH_KEY_STRING, entry);
            }
        }
    }
}


void
svn_wc__adm_access_set_entries (svn_wc_adm_access_t *adm_access,
                                svn_boolean_t show_hidden,
                                apr_hash_t *entries)
{
  if (show_hidden)
    adm_access->entries_hidden = entries;
  else
    adm_access->entries = entries;
}


apr_hash_t *
svn_wc__adm_access_entries (svn_wc_adm_access_t *adm_access,
                            svn_boolean_t show_hidden,
                            apr_pool_t *pool)
{
  if (! show_hidden)
    {
      prune_deleted (adm_access, pool);
      return adm_access->entries;
    }
  else
    return adm_access->entries_hidden;
}


int
svn_wc__adm_wc_format (svn_wc_adm_access_t *adm_access)
{
  return adm_access->wc_format;
}


svn_boolean_t
svn_wc__adm_missing (svn_wc_adm_access_t *adm_access,
                     const char *path)
{
  if (adm_access->set
      && apr_hash_get (adm_access->set, path, APR_HASH_KEY_STRING) == &missing)
    return TRUE;

  return FALSE;
}

