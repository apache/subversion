/*
 * lock.c:  routines for locking working copy subdirectories.
 *
 * ====================================================================
 *    Licensed to the Subversion Corporation (SVN Corp.) under one
 *    or more contributor license agreements.  See the NOTICE file
 *    distributed with this work for additional information
 *    regarding copyright ownership.  The SVN Corp. licenses this file
 *    to you under the Apache License, Version 2.0 (the
 *    "License"); you may not use this file except in compliance
 *    with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing,
 *    software distributed under the License is distributed on an
 *    "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 *    KIND, either express or implied.  See the License for the
 *    specific language governing permissions and limitations
 *    under the License.
 * ====================================================================
 */

#include <apr_pools.h>
#include <apr_time.h>

#include "svn_pools.h"
#include "svn_dirent_uri.h"
#include "svn_path.h"
#include "svn_sorts.h"
#include "svn_hash.h"
#include "svn_types.h"

#include "wc.h"
#include "adm_files.h"
#include "lock.h"
#include "props.h"
#include "log.h"
#include "entries.h"
#include "wc_db.h"

#include "svn_private_config.h"
#include "private/svn_wc_private.h"
#include "private/svn_debug.h"




struct svn_wc_adm_access_t
{
  /* PATH to directory which contains the administrative area */
  const char *path;

  /* And the absolute form of the path.  */
  const char *abspath;

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

  /* Handle to the administrative database. */
  svn_wc__db_t *db;

  /* Was the DB provided to us? If so, then we'll never close it.  */
  svn_boolean_t db_provided;

  /* ENTRIES_HIDDEN is all cached entries including those in
     state deleted or state absent. It may be NULL. */
  apr_hash_t *entries_all;

  /* POOL is used to allocate cached items, they need to persist for the
     lifetime of this access baton */
  apr_pool_t *pool;

};


/* This is a placeholder used in the set hash to represent missing
   directories.  Only its address is important, it contains no useful
   data. */
static const svn_wc_adm_access_t missing;
#define IS_MISSING(lock) ((lock) == &missing)


/* ### these functions are here for forward references. generally, they're
   ### here to avoid the code churn from moving the definitions.  */

static svn_error_t *
do_close(svn_wc_adm_access_t *adm_access, svn_boolean_t preserve_lock,
         apr_pool_t *scratch_pool);

static svn_error_t *
add_to_shared(svn_wc_adm_access_t *lock, apr_pool_t *scratch_pool);


svn_error_t *
svn_wc__internal_check_wc(int *wc_format,
                          svn_wc__db_t *db,
                          const char *local_abspath,
                          apr_pool_t *scratch_pool)
{
  svn_error_t *err;

  err = svn_wc__db_temp_get_format(wc_format, db, local_abspath, scratch_pool);
  if (err)
    {
      svn_node_kind_t kind;

      if (err->apr_err != SVN_ERR_WC_MISSING)
        return svn_error_return(err);
      svn_error_clear(err);

      /* ### the stuff below seems to be redundant. get_format() probably
         ### does all this.
         ###
         ### investigate all callers. DEFINITELY keep in mind the
         ### svn_wc_check_wc() entrypoint.
      */

      /* If the format file does not exist or path not directory, then for
         our purposes this is not a working copy, so return 0. */
      *wc_format = 0;

      /* Check path itself exists. */
      SVN_ERR(svn_io_check_path(local_abspath, &kind, scratch_pool));
      if (kind == svn_node_none)
        {
          return svn_error_createf(APR_ENOENT, NULL, _("'%s' does not exist"),
                                   svn_dirent_local_style(local_abspath,
                                                          scratch_pool));
        }
    }

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc_check_wc2(int *wc_format,
                 svn_wc_context_t *wc_ctx,
                 const char *local_abspath,
                 apr_pool_t *pool)
{
  return svn_error_return(
    svn_wc__internal_check_wc(wc_format, wc_ctx->db, local_abspath, pool));
}


/* Create a physical lock file in the admin directory for ADM_ACCESS.  */
static svn_error_t *
create_lock(const char *path, apr_pool_t *scratch_pool)
{
  const char *lock_path = svn_wc__adm_child(path, SVN_WC__ADM_LOCK,
                                            scratch_pool);
  svn_error_t *err;
  apr_file_t *file;

  err = svn_io_file_open(&file, lock_path,
                         APR_WRITE | APR_CREATE | APR_EXCL,
                         APR_OS_DEFAULT,
                         scratch_pool);
  if (err == NULL)
    return svn_io_file_close(file, scratch_pool);

  if (APR_STATUS_IS_EEXIST(err->apr_err))
    {
      svn_error_clear(err);
      return svn_error_createf(SVN_ERR_WC_LOCKED, NULL,
                               _("Working copy '%s' locked"),
                               svn_dirent_local_style(path, scratch_pool));
    }

  return err;
}


/* An APR pool cleanup handler.  This handles access batons that have not
   been closed when their pool gets destroyed.  The physical locks
   associated with such batons remain in the working copy if they are
   protecting a log file. */
static apr_status_t
pool_cleanup(void *p)
{
  svn_wc_adm_access_t *lock = p;
  svn_boolean_t present;
  svn_error_t *err;

  if (lock->type == svn_wc__adm_access_closed)
    return SVN_NO_ERROR;

  err = svn_wc__logfile_present(&present, lock->abspath, lock->pool);
  if (!err)
    err = do_close(lock, present /* preserve_lock */, lock->pool);

  /* ### Is this the correct way to handle the error? */
  if (err)
    {
      apr_status_t apr_err = err->apr_err;
      svn_error_clear(err);
      return apr_err;
    }
  else
    return APR_SUCCESS;
}

/* An APR pool cleanup handler.  This is a child handler, it removes the
   main pool handler. */
static apr_status_t
pool_cleanup_child(void *p)
{
  svn_wc_adm_access_t *lock = p;
  apr_pool_cleanup_kill(lock->pool, lock, pool_cleanup);
  return APR_SUCCESS;
}


/* Allocate from POOL, initialise and return an access baton. TYPE and PATH
   are used to initialise the baton.  If STEAL_LOCK, steal the lock if path
   is already locked */
static svn_error_t *
adm_access_alloc(svn_wc_adm_access_t **adm_access,
                 enum svn_wc__adm_access_type type,
                 const char *path,
                 svn_wc__db_t *db,
                 svn_boolean_t db_provided,
                 svn_boolean_t steal_lock,
                 apr_pool_t *result_pool,
                 apr_pool_t *scratch_pool)
{
  svn_error_t *err;
  svn_wc_adm_access_t *lock = apr_palloc(result_pool, sizeof(*lock));

  lock->type = type;
  lock->entries_all = NULL;
  lock->db = db;
  lock->db_provided = db_provided;
  lock->lock_exists = FALSE;
  lock->path = apr_pstrdup(result_pool, path);
  lock->pool = result_pool;

  SVN_ERR(svn_dirent_get_absolute(&lock->abspath, path, result_pool));

  *adm_access = lock;

  if (type == svn_wc__adm_access_write_lock)
    {
      err = create_lock(path, scratch_pool);

      if (err)
        {
          if (err->apr_err != SVN_ERR_WC_LOCKED || !steal_lock)
            return svn_error_return(err);

          svn_error_clear(err);
        }

      lock->lock_exists = TRUE;
    }

  err = add_to_shared(lock, scratch_pool);

  if (err)
    return svn_error_compose_create(
                err,
                svn_wc__remove_adm_file(lock->abspath, SVN_WC__ADM_LOCK,
                                        scratch_pool));

  /* ### does this utf8 thing really/still apply??  */
  /* It's important that the cleanup handler is registered *after* at least
     one UTF8 conversion has been done, since such a conversion may create
     the apr_xlate_t object in the pool, and that object must be around
     when the cleanup handler runs.  If the apr_xlate_t cleanup handler
     were to run *before* the access baton cleanup handler, then the access
     baton's handler won't work. */
  apr_pool_cleanup_register(lock->pool, lock, pool_cleanup,
                            pool_cleanup_child);

  return SVN_NO_ERROR;
}


static svn_error_t *
alloc_db(svn_wc__db_t **db,
         svn_config_t *config,
         apr_pool_t *result_pool,
         apr_pool_t *scratch_pool)
{
  svn_wc__db_openmode_t mode;

  /* ### need to determine MODE based on callers' needs.  */
  mode = svn_wc__db_openmode_default;
  SVN_ERR(svn_wc__db_open(db, mode, config, TRUE, TRUE,
                          result_pool, scratch_pool));

  return SVN_NO_ERROR;
}


static svn_error_t *
add_to_shared(svn_wc_adm_access_t *lock, apr_pool_t *scratch_pool)
{
  /* ### sometimes we replace &missing with a now-valid lock.  */
  {
    svn_wc_adm_access_t *prior = svn_wc__db_temp_get_access(lock->db,
                                                            lock->abspath,
                                                            scratch_pool);
    if (IS_MISSING(prior))
      SVN_ERR(svn_wc__db_temp_close_access(lock->db, lock->abspath,
                                           (svn_wc_adm_access_t *)&missing,
                                           scratch_pool));
  }

  svn_wc__db_temp_set_access(lock->db, lock->abspath, lock,
                             scratch_pool);

  return SVN_NO_ERROR;
}


static svn_wc_adm_access_t *
get_from_shared(const char *abspath,
                svn_wc__db_t *db,
                apr_pool_t *scratch_pool)
{
  /* We closed the DB when it became empty. ABSPATH is not present.  */
  if (db == NULL)
    return NULL;
  return svn_wc__db_temp_get_access(db, abspath, scratch_pool);
}


static svn_error_t *
probe(svn_wc__db_t *db,
      const char **dir,
      const char *path,
      apr_pool_t *pool)
{
  svn_node_kind_t kind;
  int wc_format = 0;

  SVN_ERR(svn_io_check_path(path, &kind, pool));
  if (kind == svn_node_dir)
    {
      const char *local_abspath;

      SVN_ERR(svn_dirent_get_absolute(&local_abspath, path, pool));
      SVN_ERR(svn_wc__internal_check_wc(&wc_format, db, local_abspath, pool));
    }

  /* a "version" of 0 means a non-wc directory */
  if (kind != svn_node_dir || wc_format == 0)
    {
      /* Passing a path ending in "." or ".." to svn_dirent_dirname() is
         probably always a bad idea; certainly it is in this case.
         Unfortunately, svn_dirent_dirname()'s current signature can't
         return an error, so we have to insert the protection in this
         caller, ideally the API needs a change.  See issue #1617. */
      const char *base_name = svn_dirent_basename(path, pool);
      if ((strcmp(base_name, "..") == 0)
          || (strcmp(base_name, ".") == 0))
        {
          return svn_error_createf
            (SVN_ERR_WC_BAD_PATH, NULL,
             _("Path '%s' ends in '%s', "
               "which is unsupported for this operation"),
             svn_dirent_local_style(path, pool), base_name);
        }

      *dir = svn_dirent_dirname(path, pool);
    }
  else
    *dir = path;

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__adm_steal_write_lock(svn_wc_adm_access_t **adm_access,
                             svn_wc__db_t *db,
                             const char *path,
                             apr_pool_t *result_pool,
                             apr_pool_t *scratch_pool)
{
  SVN_ERR(adm_access_alloc(adm_access, svn_wc__adm_access_write_lock, path,
                           db, TRUE, TRUE, result_pool, scratch_pool));
  
  /* We used to attempt to upgrade the working copy here, but now we let
     it slide.  Our sole caller is svn_wc_cleanup3(), which will itself
     worry about upgrading.  */

  return SVN_NO_ERROR;
}


static svn_error_t *
open_single(svn_wc_adm_access_t **adm_access,
            const char *path,
            svn_boolean_t write_lock,
            svn_wc__db_t *db,
            svn_boolean_t db_provided,
            apr_pool_t *result_pool,
            apr_pool_t *scratch_pool)
{
  const char *local_abspath;
  int wc_format = 0;
  svn_error_t *err;
  svn_wc_adm_access_t *lock;

  SVN_ERR(svn_dirent_get_absolute(&local_abspath, path, scratch_pool));
  err = svn_wc__internal_check_wc(&wc_format, db, local_abspath, scratch_pool);
  if (wc_format == 0 || (err && APR_STATUS_IS_ENOENT(err->apr_err)))
    {
      return svn_error_createf(SVN_ERR_WC_NOT_WORKING_COPY, err,
                               _("'%s' is not a working copy"),
                               svn_dirent_local_style(path, scratch_pool));
    }
  SVN_ERR(err);

  /* The format version must match exactly. Note that wc_db will perform
     an auto-upgrade if allowed. If it does *not*, then it has decided a
     manual upgrade is required.

     Note: if it decided on a manual upgrade, then we "should" never even
     reach this code. An error should have been raised earlier.  */
  if (wc_format != SVN_WC__VERSION)
    {
      return svn_error_createf(SVN_ERR_WC_UPGRADE_REQUIRED, NULL,
                               _("Working copy format of '%s' is too old (%d); "
                                 "please run 'svn upgrade'"),
                               svn_dirent_local_style(path, scratch_pool),
                               wc_format);
    }

  /* Need to create a new lock */
  SVN_ERR(adm_access_alloc(&lock,
                           write_lock
                             ? svn_wc__adm_access_write_lock
                             : svn_wc__adm_access_unlocked,
                           path, db, db_provided, FALSE, result_pool,
                           scratch_pool));

  /* ### recurse was here */
  *adm_access = lock;

  return SVN_NO_ERROR;
}


static svn_error_t *
close_single(svn_wc_adm_access_t *adm_access,
             svn_boolean_t preserve_lock,
             apr_pool_t *scratch_pool)
{
  if (adm_access->type == svn_wc__adm_access_closed)
    return SVN_NO_ERROR;

  /* Physically unlock if required */
  if (adm_access->type == svn_wc__adm_access_write_lock)
    {
      if (adm_access->lock_exists && !preserve_lock)
        {
          /* Remove the physical lock in the admin directory for
             PATH. It is acceptable for the administrative area to
             have disappeared, such as when the directory is removed
             from the working copy.  It is an error for the lock to
             have disappeared if the administrative area still exists. */

          svn_error_t *err = svn_wc__remove_adm_file(adm_access->path,
                                                     SVN_WC__ADM_LOCK,
                                                     scratch_pool);
          if (err)
            {
              if (svn_wc__adm_area_exists(adm_access, scratch_pool))
                return err;
              svn_error_clear(err);
            }

          adm_access->lock_exists = FALSE;
        }
    }

  /* Reset to prevent further use of the lock. */
  adm_access->type = svn_wc__adm_access_closed;

  /* Detach from set */
  SVN_ERR(svn_wc__db_temp_close_access(adm_access->db, adm_access->abspath,
                                       adm_access, scratch_pool));

  /* Possibly close the underlying wc_db. */
  if (!adm_access->db_provided)
    {
      apr_hash_t *opened = svn_wc__db_temp_get_all_access(adm_access->db,
                                                          scratch_pool);
      if (apr_hash_count(opened) == 0)
        {
          SVN_ERR(svn_wc__db_close(adm_access->db));
          adm_access->db = NULL;
        }
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__adm_available(svn_boolean_t *available,
                      svn_wc__db_kind_t *kind,
                      svn_boolean_t *obstructed,
                      svn_wc__db_t *db,
                      const char *local_abspath,
                      apr_pool_t *scratch_pool)
{
  svn_wc__db_status_t status;
  svn_depth_t depth;

  *available = TRUE;
  if (obstructed)
    *obstructed = FALSE;
  if (kind)
    *kind = svn_wc__db_kind_unknown;

  SVN_ERR(svn_wc__db_read_info(&status, kind, NULL, NULL, NULL, NULL, NULL,
                               NULL, NULL, NULL, &depth, NULL, NULL, NULL,
                               NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                               NULL, NULL, NULL, NULL, NULL, NULL,
                               db, local_abspath, scratch_pool, scratch_pool));

  if (status == svn_wc__db_status_obstructed ||
      status == svn_wc__db_status_obstructed_add ||
      status == svn_wc__db_status_obstructed_delete)
    {
      *available = FALSE;
      if (obstructed)
        *obstructed = TRUE;
    }
  else if (status == svn_wc__db_status_absent ||
           status == svn_wc__db_status_excluded ||
           status == svn_wc__db_status_not_present ||
           depth == svn_depth_exclude)
    {
      *available = FALSE;
    }

  return SVN_NO_ERROR;
}
/* This is essentially the guts of svn_wc_adm_open3.
 *
 * If the working copy is already locked, return SVN_ERR_WC_LOCKED; if
 * it is not a versioned directory, return SVN_ERR_WC_NOT_DIRECTORY.
 */
static svn_error_t *
do_open(svn_wc_adm_access_t **adm_access,
        const char *path,
        svn_wc__db_t *db,
        svn_boolean_t db_provided,
        apr_array_header_t *rollback,
        svn_boolean_t write_lock,
        int levels_to_lock,
        svn_cancel_func_t cancel_func,
        void *cancel_baton,
        apr_pool_t *result_pool,
        apr_pool_t *scratch_pool)
{
  svn_wc_adm_access_t *lock;
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);

  SVN_ERR(open_single(&lock, path, write_lock, db, db_provided,
                      result_pool, iterpool));

  /* Add self to the rollback list in case of error.  */
  APR_ARRAY_PUSH(rollback, svn_wc_adm_access_t *) = lock;

  if (levels_to_lock != 0)
    {
      const apr_array_header_t *children;
      const char *local_abspath = svn_wc__adm_access_abspath(lock);
      int i;

      /* Reduce levels_to_lock since we are about to recurse */
      if (levels_to_lock > 0)
        levels_to_lock--;

      SVN_ERR(svn_wc__db_read_children(&children, db, local_abspath,
                                       scratch_pool, iterpool));

      /* Open the tree */
      for (i = 0; i < children->nelts; i++)
        {
          const char *node_abspath;
          svn_wc__db_kind_t kind;
          svn_boolean_t available, obstructed;
          const char *name = APR_ARRAY_IDX(children, i, const char *);

          svn_pool_clear(iterpool);

          /* See if someone wants to cancel this operation. */
          if (cancel_func)
            SVN_ERR(cancel_func(cancel_baton));

          node_abspath = svn_dirent_join(local_abspath, name, iterpool);

          SVN_ERR(svn_wc__adm_available(&available,
                                        &kind,
                                        &obstructed,
                                        db,
                                        node_abspath,
                                        scratch_pool));

          if (kind != svn_wc__db_kind_dir)
            continue;

          if (available)
            {
              const char *node_path = svn_dirent_join(path, name, iterpool);
              svn_wc_adm_access_t *node_access;

              SVN_ERR(do_open(&node_access, node_path, db, db_provided,
                              rollback, write_lock, levels_to_lock,
                              cancel_func, cancel_baton,
                              lock->pool, iterpool));
              /* node_access has been registered in DB, so we don't need
                 to do anything with it.  */
            }
          else if (obstructed)
            {
              svn_wc__db_temp_set_access(lock->db, node_abspath,
                                         (svn_wc_adm_access_t *)&missing,
                                         iterpool);
            }
        }
    }
  svn_pool_destroy(iterpool);

  *adm_access = lock;

  return SVN_NO_ERROR;
}


static svn_error_t *
open_all(svn_wc_adm_access_t **adm_access,
         const char *path,
         svn_wc__db_t *db,
         svn_boolean_t db_provided,
         svn_boolean_t write_lock,
         int levels_to_lock,
         svn_cancel_func_t cancel_func,
         void *cancel_baton,
         apr_pool_t *pool)
{
  apr_array_header_t *rollback;
  svn_error_t *err;

  rollback = apr_array_make(pool, 10, sizeof(svn_wc_adm_access_t *));

  err = do_open(adm_access, path, db, db_provided, rollback,
                write_lock, levels_to_lock,
                cancel_func, cancel_baton, pool, pool);
  if (err)
    {
      int i;

      for (i = rollback->nelts; i--; )
        {
          svn_wc_adm_access_t *lock = APR_ARRAY_IDX(rollback, i,
                                                    svn_wc_adm_access_t *);
          SVN_ERR_ASSERT(!IS_MISSING(lock));

          svn_error_clear(close_single(lock, FALSE /* preserve_lock */, pool));
        }
    }

  return err;
}


svn_error_t *
svn_wc_adm_open3(svn_wc_adm_access_t **adm_access,
                 svn_wc_adm_access_t *associated,
                 const char *path,
                 svn_boolean_t write_lock,
                 int levels_to_lock,
                 svn_cancel_func_t cancel_func,
                 void *cancel_baton,
                 apr_pool_t *pool)
{
  svn_wc__db_t *db;
  svn_boolean_t db_provided;

  /* Make sure that ASSOCIATED has a set of access batons, so that we can
     glom a reference to self into it. */
  if (associated)
    {
      const char *abspath;
      svn_wc_adm_access_t *lock;

      SVN_ERR(svn_dirent_get_absolute(&abspath, path, pool));
      lock = get_from_shared(abspath, associated->db, pool);
      if (lock && !IS_MISSING(lock))
        /* Already locked.  The reason we don't return the existing baton
           here is that the user is supposed to know whether a directory is
           locked: if it's not locked call svn_wc_adm_open, if it is locked
           call svn_wc_adm_retrieve.  */
        return svn_error_createf(SVN_ERR_WC_LOCKED, NULL,
                                 _("Working copy '%s' locked"),
                                 svn_dirent_local_style(path, pool));
      db = associated->db;
      db_provided = associated->db_provided;
    }
  else
    {
      /* Any baton creation is going to need a shared structure for holding
         data across the entire set. The caller isn't providing one, so we
         do it here.  */
      /* ### we could optimize around levels_to_lock==0, but much of this
         ### is going to be simplified soon anyways.  */
      SVN_ERR(alloc_db(&db, NULL /* ### config. need! */, pool, pool));
      db_provided = FALSE;
    }

  return open_all(adm_access, path, db, db_provided,
                  write_lock, levels_to_lock, cancel_func, cancel_baton, pool);
}


svn_error_t *
svn_wc_adm_probe_open3(svn_wc_adm_access_t **adm_access,
                       svn_wc_adm_access_t *associated,
                       const char *path,
                       svn_boolean_t write_lock,
                       int levels_to_lock,
                       svn_cancel_func_t cancel_func,
                       void *cancel_baton,
                       apr_pool_t *pool)
{
  svn_error_t *err;
  const char *dir;

  if (associated == NULL)
    {
      svn_wc__db_t *db;

      /* Ugh. Too bad about having to open a DB.  */
      SVN_ERR(svn_wc__db_open(&db, svn_wc__db_openmode_readonly,
                              NULL /* ### config */, TRUE, TRUE, pool, pool));
      err = probe(db, &dir, path, pool);
      svn_error_clear(svn_wc__db_close(db));
      SVN_ERR(err);
    }
  else
    {
      SVN_ERR(probe(associated->db, &dir, path, pool));
    }

  /* If we moved up a directory, then the path is not a directory, or it
     is not under version control. In either case, the notion of
     levels_to_lock does not apply to the provided path.  Disable it so
     that we don't end up trying to lock more than we need.  */
  if (dir != path)
    levels_to_lock = 0;

  err = svn_wc_adm_open3(adm_access, associated, dir, write_lock,
                         levels_to_lock, cancel_func, cancel_baton, pool);
  if (err)
    {
      svn_error_t *err2;

      /* If we got an error on the parent dir, that means we failed to
         get an access baton for the child in the first place.  And if
         the reason we couldn't get the child access baton is that the
         child is not a versioned directory, then return an error
         about the child, not the parent. */
      svn_node_kind_t child_kind;
      if ((err2 = svn_io_check_path(path, &child_kind, pool)))
        {
          svn_error_compose(err, err2);
          return err;
        }

      if ((dir != path)
          && (child_kind == svn_node_dir)
          && (err->apr_err == SVN_ERR_WC_NOT_WORKING_COPY))
        {
          svn_error_clear(err);
          return svn_error_createf(SVN_ERR_WC_NOT_WORKING_COPY, NULL,
                                   _("'%s' is not a working copy"),
                                   svn_dirent_local_style(path, pool));
        }

      return err;
    }

  return SVN_NO_ERROR;
}


svn_wc_adm_access_t *
svn_wc__adm_retrieve_internal2(svn_wc__db_t *db,
                               const char *abspath,
                               apr_pool_t *scratch_pool)
{
  svn_wc_adm_access_t *adm_access = get_from_shared(abspath, db, scratch_pool);

  /* If the entry is marked as "missing", then return nothing.  */
  if (IS_MISSING(adm_access))
    adm_access = NULL;

  return adm_access;
}


svn_error_t *
svn_wc__adm_retrieve_internal(svn_wc_adm_access_t **adm_access,
                              svn_wc_adm_access_t *associated,
                              const char *path,
                              apr_pool_t *pool)
{
  if (strcmp(associated->path, path) == 0)
    {
      *adm_access = associated;
    }
  else
    {
      const char *abspath;

      SVN_ERR(svn_dirent_get_absolute(&abspath, path, pool));

      *adm_access = svn_wc__adm_retrieve_internal2(associated->db, abspath,
                                                   pool);

      /* Relative paths can play stupid games with the lookup, and we might
         try to return the wrong baton. Look for that case, and zap it.

         The specific case observed happened during "svn status .. -u -v".
         svn_wc_status2() would do dirname("..") returning "". When that
         came into this function, we'd map it to an absolute path and find
         it in the DB, then return it. The (apparently) *desired* behavior
         is to not find "" in the set of batons.

         Sigh.  */
      if (*adm_access != NULL
          && strcmp(path, (*adm_access)->path) != 0)
        *adm_access = NULL;
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc_adm_retrieve(svn_wc_adm_access_t **adm_access,
                    svn_wc_adm_access_t *associated,
                    const char *path,
                    apr_pool_t *pool)
{
  SVN_ERR(svn_wc__adm_retrieve_internal(adm_access, associated, path, pool));

  /* Most of the code expects access batons to exist, so returning an error
     generally makes the calling code simpler as it doesn't need to check
     for NULL batons. */
  if (! *adm_access)
    {
      /* We are going to send a SVN_ERR_WC_NOT_LOCKED, but let's provide
         a bit more information to our caller */

      const char *local_abspath;
      svn_wc__db_kind_t kind = svn_wc__db_kind_unknown;
      svn_node_kind_t wckind;
      svn_error_t *err;

      err = svn_io_check_path(path, &wckind, pool);

      /* If we can't check the path, we can't make a good error
         message.  */
      if (err)
        {
          return svn_error_createf(SVN_ERR_WC_NOT_LOCKED, err,
                                   _("Unable to check path existence for '%s'"),
                                   svn_dirent_local_style(path, pool));
        }

      SVN_ERR(svn_dirent_get_absolute(&local_abspath, path, pool));

      if (associated)
        {
          err = svn_wc__db_read_kind(&kind, svn_wc__adm_get_db(associated),
                                     local_abspath, TRUE, pool);

          if (err)
            {
              kind = svn_wc__db_kind_unknown;
              svn_error_clear(err);
            }
        }

      if (kind == svn_wc__db_kind_dir && wckind == svn_node_file)
        {
          err = svn_error_createf(
                   SVN_ERR_WC_NOT_WORKING_COPY, NULL,
                   _("Expected '%s' to be a directory but found a file"),
                   svn_dirent_local_style(path, pool));

          return svn_error_create(SVN_ERR_WC_NOT_LOCKED, err, err->message);
        }
      else if (kind != svn_wc__db_kind_dir && kind != svn_wc__db_kind_unknown)
        {
          err = svn_error_createf(
                   SVN_ERR_WC_NOT_WORKING_COPY, NULL,
                   _("Can't retrieve an access baton for non-directory '%s'"),
                   svn_dirent_local_style(path, pool));

          return svn_error_create(SVN_ERR_WC_NOT_LOCKED, err, err->message);
        }
      else if (kind == svn_wc__db_kind_unknown || wckind == svn_node_none)
        {
          err = svn_error_createf(SVN_ERR_WC_PATH_NOT_FOUND, NULL,
                                  _("Directory '%s' is missing"),
                                  svn_dirent_local_style(path, pool));

          return svn_error_create(SVN_ERR_WC_NOT_LOCKED, err, err->message);
        }

      /* If all else fails, return our useless generic error.  */
      return svn_error_createf(SVN_ERR_WC_NOT_LOCKED, NULL,
                               _("Working copy '%s' is not locked"),
                               svn_dirent_local_style(path, pool));
    }

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc_adm_probe_retrieve(svn_wc_adm_access_t **adm_access,
                          svn_wc_adm_access_t *associated,
                          const char *path,
                          apr_pool_t *pool)
{
  const char *dir;
  const char *local_abspath;
  svn_wc__db_kind_t kind;
  svn_error_t *err;

  SVN_ERR_ASSERT(associated != NULL);

  SVN_ERR(svn_dirent_get_absolute(&local_abspath, path, pool));
  SVN_ERR(svn_wc__db_read_kind(&kind, associated->db, local_abspath, TRUE, pool));

  if (kind == svn_wc__db_kind_dir)
    dir = path;
  else if (kind != svn_wc__db_kind_unknown)
    dir = svn_dirent_dirname(path, pool);
  else
    /* Not a versioned item, probe it */
    SVN_ERR(probe(associated->db, &dir, path, pool));

  err = svn_wc_adm_retrieve(adm_access, associated, dir, pool);
  if (err && err->apr_err == SVN_ERR_WC_NOT_LOCKED)
    {
      /* We'll receive a NOT LOCKED error for various reasons,
         including the reason we'll actually want to test for:
         The path is a versioned directory, but missing, in which case
         we want its parent's adm_access (which holds minimal data
         on the child) */
      svn_error_clear(err);
      SVN_ERR(probe(associated->db, &dir, path, pool));
      SVN_ERR(svn_wc_adm_retrieve(adm_access, associated, dir, pool));
    }
  else
    return svn_error_return(err);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc_adm_probe_try3(svn_wc_adm_access_t **adm_access,
                      svn_wc_adm_access_t *associated,
                      const char *path,
                      svn_boolean_t write_lock,
                      int levels_to_lock,
                      svn_cancel_func_t cancel_func,
                      void *cancel_baton,
                      apr_pool_t *pool)
{
  svn_error_t *err;

  err = svn_wc_adm_probe_retrieve(adm_access, associated, path, pool);

  /* SVN_ERR_WC_NOT_LOCKED would mean there was no access baton for
     path in associated, in which case we want to open an access
     baton and add it to associated. */
  if (err && (err->apr_err == SVN_ERR_WC_NOT_LOCKED))
    {
      svn_error_clear(err);
      err = svn_wc_adm_probe_open3(adm_access, associated,
                                   path, write_lock, levels_to_lock,
                                   cancel_func, cancel_baton,
                                   svn_wc_adm_access_pool(associated));

      /* If the path is not a versioned directory, we just return a
         null access baton with no error.  Note that of the errors we
         do report, the most important (and probably most likely) is
         SVN_ERR_WC_LOCKED.  That error would mean that someone else
         has this area locked, and we definitely want to bail in that
         case. */
      if (err && (err->apr_err == SVN_ERR_WC_NOT_WORKING_COPY))
        {
          svn_error_clear(err);
          *adm_access = NULL;
          err = NULL;
        }
    }

  return err;
}


static svn_error_t *
child_is_disjoint(svn_boolean_t *disjoint,
                  svn_wc__db_t *db,
                  const char *local_abspath,
                  apr_pool_t *scratch_pool)
{
  const svn_wc_entry_t *t_entry;
  const svn_wc_entry_t *p_entry;
  const svn_wc_entry_t *t_entry_in_p;
  const char *parent_abspath = svn_dirent_dirname(local_abspath, scratch_pool);
  const char *expected_url;
  svn_error_t *err;

  err = svn_wc__get_entry(&t_entry_in_p, db, local_abspath, FALSE,
                          svn_node_dir, TRUE, scratch_pool, scratch_pool);

  if (err && (err->apr_err == SVN_ERR_WC_MISSING || 
              (err->apr_err == SVN_ERR_WC_PATH_NOT_FOUND)))
    {
      /* Parent doesn't know about the child.  */
      svn_error_clear(err);
      *disjoint = TRUE;
      return SVN_NO_ERROR;
    }
  else
    SVN_ERR(err);

  SVN_ERR(svn_wc__get_entry(&p_entry, db, parent_abspath, FALSE, svn_node_dir,
                            FALSE, scratch_pool, scratch_pool));
  if (p_entry->url == NULL)
    {
      *disjoint = FALSE;
      return SVN_NO_ERROR;
    }
  expected_url = svn_path_url_add_component2(p_entry->url,
                                             svn_dirent_basename(local_abspath,
                                                                 scratch_pool),
                                             scratch_pool);

  SVN_ERR(svn_wc__get_entry(&t_entry, db, local_abspath, FALSE, svn_node_dir,
                            FALSE, scratch_pool, scratch_pool));

  /* Is the child switched?  */
  *disjoint = t_entry->url && (strcmp(t_entry->url, expected_url) != 0);

  return SVN_NO_ERROR;
}


static svn_error_t *
open_anchor(svn_wc_adm_access_t **anchor_access,
            svn_wc_adm_access_t **target_access,
            const char **target,
            svn_wc__db_t *db,
            svn_boolean_t db_provided,
            const char *path,
            svn_boolean_t write_lock,
            int levels_to_lock,
            svn_cancel_func_t cancel_func,
            void *cancel_baton,
            apr_pool_t *pool)
{
  const char *base_name = svn_dirent_basename(path, pool);

  /* Any baton creation is going to need a shared structure for holding
     data across the entire set. The caller isn't providing one, so we
     do it here.  */
  /* ### we could maybe skip the shared struct for levels_to_lock==0, but
     ### given that we need DB for format detection, may as well keep this.
     ### in any case, much of this is going to be simplified soon anyways.  */
  if (!db_provided)
    SVN_ERR(alloc_db(&db, NULL /* ### config. need! */, pool, pool));

  if (svn_path_is_empty(path)
      || svn_dirent_is_root(path, strlen(path))
      || ! strcmp(base_name, ".."))
    {
      SVN_ERR(open_all(anchor_access, path, db, db_provided,
                       write_lock, levels_to_lock,
                       cancel_func, cancel_baton, pool));
      *target_access = *anchor_access;
      *target = "";
    }
  else
    {
      svn_error_t *err;
      svn_wc_adm_access_t *p_access = NULL;
      svn_wc_adm_access_t *t_access = NULL;
      const char *parent = svn_dirent_dirname(path, pool);
      const char *local_abspath;
      svn_error_t *p_access_err = SVN_NO_ERROR;

      SVN_ERR(svn_dirent_get_absolute(&local_abspath, path, pool));

      /* Try to open parent of PATH to setup P_ACCESS */
      err = open_single(&p_access, parent, write_lock, db, db_provided,
                        pool, pool);
      if (err)
        {
          const char *abspath = svn_dirent_dirname(local_abspath, pool);
          svn_wc_adm_access_t *existing_adm = svn_wc__db_temp_get_access(db, abspath, pool);

          if (IS_MISSING(existing_adm))
            svn_wc__db_temp_clear_access(db, abspath, pool);
          else
            SVN_ERR_ASSERT(existing_adm == NULL);

          if (err->apr_err == SVN_ERR_WC_NOT_WORKING_COPY)
            {
              svn_error_clear(err);
              p_access = NULL;
            }
          else if (write_lock && (err->apr_err == SVN_ERR_WC_LOCKED
                                  || APR_STATUS_IS_EACCES(err->apr_err)))
            {
              /* If P_ACCESS isn't to be returned then a read-only baton
                 will do for now, but keep the error in case we need it. */
              svn_error_t *err2 = open_single(&p_access, parent, FALSE,
                                              db, db_provided, pool, pool);
              if (err2)
                {
                  svn_error_clear(err2);
                  return err;
                }
              p_access_err = err;
            }
          else
            return err;
        }

      /* Try to open PATH to setup T_ACCESS */
      err = open_all(&t_access, path, db, db_provided, write_lock,
                     levels_to_lock, cancel_func, cancel_baton, pool);
      if (err)
        {
          if (p_access == NULL)
            {
              /* Couldn't open the parent or the target. Bail out.  */
              svn_error_clear(p_access_err);
              return err;
            }

          if (err->apr_err != SVN_ERR_WC_NOT_WORKING_COPY)
            {
              if (p_access)
                svn_error_clear(svn_wc_adm_close2(p_access, pool));
              svn_error_clear(p_access_err);
              return err;
            }

          /* This directory is not under version control. Ignore it.  */
          svn_error_clear(err);
          t_access = NULL;
        }

      /* At this stage might have P_ACCESS, T_ACCESS or both */

      /* Check for switched or disjoint P_ACCESS and T_ACCESS */
      if (p_access && t_access)
        {
          svn_boolean_t disjoint;

          err = child_is_disjoint(&disjoint, db, local_abspath, pool);
          if (err)
            {
              svn_error_clear(p_access_err);
              svn_error_clear(svn_wc_adm_close2(p_access, pool));
              svn_error_clear(svn_wc_adm_close2(t_access, pool));
              return err;
            }

          if (disjoint)
            {
              /* Switched or disjoint, so drop P_ACCESS. Don't close any
                 descendents, or we might blast the child.  */
              err = close_single(p_access, FALSE /* preserve_lock */, pool);
              if (err)
                {
                  svn_error_clear(p_access_err);
                  svn_error_clear(svn_wc_adm_close2(t_access, pool));
                  return err;
                }
              p_access = NULL;
            }
        }

      /* We have a parent baton *and* we have an error related to opening
         the baton. That means we have a readonly baton, but that isn't
         going to work for us. (p_access would have been set to NULL if
         a writable parent baton is not required)  */
      if (p_access && p_access_err)
        {
          if (t_access)
            svn_error_clear(svn_wc_adm_close2(t_access, pool));
          svn_error_clear(svn_wc_adm_close2(p_access, pool));
          return p_access_err;
        }
      svn_error_clear(p_access_err);

      if (! t_access)
        {
          const svn_wc_entry_t *t_entry;

          err = svn_wc_entry(&t_entry, path, p_access, FALSE, pool);
          if (err)
            {
              svn_error_clear(svn_wc_adm_close2(p_access, pool));
              return err;
            }
          if (t_entry && t_entry->kind == svn_node_dir)
            {
              const char *abspath;

              /* Child PATH is missing.  */
              SVN_ERR(svn_dirent_get_absolute(&abspath, path, pool));
              svn_wc__db_temp_set_access(db, abspath,
                                         (svn_wc_adm_access_t *)&missing,
                                         pool);
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


svn_error_t *
svn_wc_adm_open_anchor(svn_wc_adm_access_t **anchor_access,
                       svn_wc_adm_access_t **target_access,
                       const char **target,
                       const char *path,
                       svn_boolean_t write_lock,
                       int levels_to_lock,
                       svn_cancel_func_t cancel_func,
                       void *cancel_baton,
                       apr_pool_t *pool)
{
  return svn_error_return(open_anchor(anchor_access, target_access, target,
                                      NULL, FALSE, path, write_lock,
                                      levels_to_lock, cancel_func,
                                      cancel_baton, pool));
}

svn_error_t *
svn_wc__adm_open_anchor_in_context(svn_wc_adm_access_t **anchor_access,
                                   svn_wc_adm_access_t **target_access,
                                   const char **target,
                                   svn_wc_context_t *wc_ctx,
                                   const char *path,
                                   svn_boolean_t write_lock,
                                   int levels_to_lock,
                                   svn_cancel_func_t cancel_func,
                                   void *cancel_baton,
                                   apr_pool_t *pool)
{
  return svn_error_return(open_anchor(anchor_access, target_access, target,
                                      wc_ctx->db, TRUE, path, write_lock,
                                      levels_to_lock, cancel_func,
                                      cancel_baton, pool));
}

svn_error_t *
svn_wc__adm_retrieve_from_context(svn_wc_adm_access_t **adm_access,
                                  svn_wc_context_t *wc_ctx,
                                  const char *local_abspath,
                                  apr_pool_t *pool)
{
  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  *adm_access = svn_wc__adm_retrieve_internal2(wc_ctx->db, 
                                               local_abspath, 
                                               pool);

  return SVN_NO_ERROR;
}

/* Does the work of closing the access baton ADM_ACCESS.  Any physical
   locks are removed from the working copy if PRESERVE_LOCK is FALSE, or
   are left if PRESERVE_LOCK is TRUE.  Any associated access batons that
   are direct descendants will also be closed.
 */
static svn_error_t *
do_close(svn_wc_adm_access_t *adm_access,
         svn_boolean_t preserve_lock,
         apr_pool_t *scratch_pool)
{
  svn_wc_adm_access_t *look;

  if (adm_access->type == svn_wc__adm_access_closed)
    return SVN_NO_ERROR;

  /* If we are part of the shared set, then close descendant batons.  */
  look = get_from_shared(adm_access->abspath, adm_access->db, scratch_pool);
  if (look != NULL)
    {
      apr_hash_t *opened;
      apr_hash_index_t *hi;

      /* Gather all the opened access batons from the DB.  */
      opened = svn_wc__db_temp_get_all_access(adm_access->db, scratch_pool);

      /* Close any that are descendents of this baton.  */
      for (hi = apr_hash_first(scratch_pool, opened);
           hi;
           hi = apr_hash_next(hi))
        {
          const void *key;
          void *val;
          const char *path;
          const char *abspath;
          svn_wc_adm_access_t *child;

          apr_hash_this(hi, &key, NULL, &val);
          abspath = key;
          child = val;
          path = child->path;

          if (IS_MISSING(child))
            {
              /* We don't close the missing entry, but get rid of it from
                 the set. */
              svn_wc__db_temp_clear_access(adm_access->db, abspath,
                                           scratch_pool);
              continue;
            }

          if (! svn_dirent_is_ancestor(adm_access->path, path)
              || strcmp(adm_access->path, path) == 0)
            continue;

          SVN_ERR(close_single(child, preserve_lock, scratch_pool));
        }
    }

  return close_single(adm_access, preserve_lock, scratch_pool);
}

svn_error_t *
svn_wc_adm_close2(svn_wc_adm_access_t *adm_access, apr_pool_t *scratch_pool)
{
  return do_close(adm_access, FALSE, scratch_pool);
}

svn_boolean_t
svn_wc_adm_locked(const svn_wc_adm_access_t *adm_access)
{
  return adm_access->type == svn_wc__adm_access_write_lock;
}

svn_error_t *
svn_wc__adm_write_check(const svn_wc_adm_access_t *adm_access,
                        apr_pool_t *scratch_pool)
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

          SVN_ERR(svn_wc_locked(&locked, adm_access->path, scratch_pool));
          if (! locked)
            return svn_error_createf(SVN_ERR_WC_NOT_LOCKED, NULL,
                                     _("Write-lock stolen in '%s'"),
                                     svn_dirent_local_style(adm_access->path,
                                                            scratch_pool));
        }
    }
  else
    {
      return svn_error_createf(SVN_ERR_WC_NOT_LOCKED, NULL,
                               _("No write-lock in '%s'"),
                               svn_dirent_local_style(adm_access->path,
                                                      scratch_pool));
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc_locked(svn_boolean_t *locked, const char *path, apr_pool_t *pool)
{
  svn_node_kind_t kind;
  const char *lockfile = svn_wc__adm_child(path, SVN_WC__ADM_LOCK, pool);

  SVN_ERR(svn_io_check_path(lockfile, &kind, pool));
  if (kind == svn_node_file)
    *locked = TRUE;
  else if (kind == svn_node_none)
    *locked = FALSE;
  else
    return svn_error_createf(SVN_ERR_WC_LOCKED, NULL,
                             _("Lock file '%s' is not a regular file"),
                             svn_dirent_local_style(lockfile, pool));

  return SVN_NO_ERROR;
}


const char *
svn_wc_adm_access_path(const svn_wc_adm_access_t *adm_access)
{
  return adm_access->path;
}


const char *
svn_wc__adm_access_abspath(const svn_wc_adm_access_t *adm_access)
{
  return adm_access->abspath;
}


apr_pool_t *
svn_wc_adm_access_pool(const svn_wc_adm_access_t *adm_access)
{
  return adm_access->pool;
}


void
svn_wc__adm_access_set_entries(svn_wc_adm_access_t *adm_access,
                               apr_hash_t *entries)
{
  adm_access->entries_all = entries;
}


apr_hash_t *
svn_wc__adm_access_entries(svn_wc_adm_access_t *adm_access)
{
  return adm_access->entries_all;
}


svn_wc__db_t *
svn_wc__adm_get_db(const svn_wc_adm_access_t *adm_access)
{
  return adm_access->db;
}


svn_boolean_t
svn_wc__adm_missing(svn_wc__db_t *db,
                    const char *local_abspath,
                    apr_pool_t *scratch_pool)
{
  const svn_wc_adm_access_t *look;
  svn_boolean_t available, obstructed;
  svn_wc__db_kind_t kind;

  look = get_from_shared(local_abspath, db, scratch_pool);

  if (look != NULL)
    return IS_MISSING(look);

  /* When we switch to a single database an access baton can't be
     missing, but until then it can. But if there are no access batons we
     would always return FALSE. 
     For this case we check if an access baton could be opened

*/

  /* This check must match the check in do_open() */
  svn_error_clear(svn_wc__adm_available(&available, &kind, &obstructed,
                                        db, local_abspath,
                                        scratch_pool));

  return (kind == svn_wc__db_kind_dir) && !available && obstructed;
}


/* Extend the scope of the svn_wc_adm_access_t * passed in as WALK_BATON
   for its entire WC tree.  An implementation of
   svn_wc_entry_callbacks2_t's found_entry() API. */
static svn_error_t *
extend_lock_found_entry(const char *path,
                        const svn_wc_entry_t *entry,
                        void *walk_baton,
                        apr_pool_t *pool)
{
  /* If PATH is a directory, and it's not already locked, lock it all
     the way down to its leaf nodes. */
  if (entry->kind == svn_node_dir &&
      strcmp(entry->name, SVN_WC_ENTRY_THIS_DIR) != 0)
    {
      svn_wc_adm_access_t *anchor_access = walk_baton, *adm_access;
      svn_boolean_t write_lock =
        (anchor_access->type == svn_wc__adm_access_write_lock);
      svn_error_t *err = svn_wc_adm_probe_try3(&adm_access, anchor_access,
                                               path, write_lock, -1,
                                               NULL, NULL, pool);
      if (err)
        {
          if (err->apr_err == SVN_ERR_WC_LOCKED)
            /* Good!  The directory is *already* locked... */
            svn_error_clear(err);
          else
            return err;
        }
    }
  return SVN_NO_ERROR;
}


/* WC entry walker callbacks for svn_wc__adm_extend_lock_to_tree(). */
static const svn_wc_entry_callbacks2_t extend_lock_walker =
  {
    extend_lock_found_entry,
    svn_wc__walker_default_error_handler
  };


svn_error_t *
svn_wc__adm_extend_lock_to_tree(svn_wc_adm_access_t *adm_access,
                                apr_pool_t *pool)
{
  return svn_wc_walk_entries3(adm_access->path, adm_access,
                              &extend_lock_walker, adm_access,
                              svn_depth_infinity, FALSE, NULL, NULL, pool);
}

svn_error_t *
svn_wc__adm_open_in_context(svn_wc_adm_access_t **adm_access,
                            svn_wc_context_t *wc_ctx,
                            const char *path,
                            svn_boolean_t write_lock,
                            int levels_to_lock,
                            svn_cancel_func_t cancel_func,
                            void *cancel_baton,
                            apr_pool_t *pool)
{
  SVN_ERR_ASSERT(wc_ctx != NULL);

  SVN_ERR(open_all(adm_access, path, wc_ctx->db, TRUE, write_lock,
                   levels_to_lock, cancel_func, cancel_baton, pool));

  return SVN_NO_ERROR;
}

/* The following is largely cribbed from svn_wc_adm_probe_open3(). */
svn_error_t *
svn_wc__adm_probe_in_context(svn_wc_adm_access_t **adm_access,
                             svn_wc_context_t *wc_ctx,
                             const char *path,
                             svn_boolean_t write_lock,
                             int levels_to_lock,
                             svn_cancel_func_t cancel_func,
                             void *cancel_baton,
                             apr_pool_t *pool)
{
  const char *dir;
  svn_error_t *err;

  SVN_ERR_ASSERT(wc_ctx != NULL);

  SVN_ERR(probe(wc_ctx->db, &dir, path, pool));

  /* If we moved up a directory, then the path is not a directory, or it
     is not under version control. In either case, the notion of
     levels_to_lock does not apply to the provided path.  Disable it so
     that we don't end up trying to lock more than we need.  */
  if (dir != path)
    levels_to_lock = 0;
    
  err = svn_wc__adm_open_in_context(adm_access, wc_ctx, dir, write_lock,
                                    levels_to_lock, cancel_func, cancel_baton,
                                    pool);

  if (err)
    {
      svn_error_t *err2;

      /* If we got an error on the parent dir, that means we failed to
         get an access baton for the child in the first place.  And if
         the reason we couldn't get the child access baton is that the
         child is not a versioned directory, then return an error
         about the child, not the parent. */
      svn_node_kind_t child_kind;
      if ((err2 = svn_io_check_path(path, &child_kind, pool)))
        {
          svn_error_compose(err, err2);
          return err;
        }

      if ((dir != path)
          && (child_kind == svn_node_dir)
          && (err->apr_err == SVN_ERR_WC_NOT_WORKING_COPY))
        {
          svn_error_clear(err);
          return svn_error_createf(SVN_ERR_WC_NOT_WORKING_COPY, NULL,
                                   _("'%s' is not a working copy"),
                                   svn_dirent_local_style(path, pool));
        }

      return err;
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__temp_get_relpath(const char **rel_path,
                         svn_wc__db_t *db,
                         const char *local_abspath,
                         apr_pool_t *result_pool,
                         apr_pool_t *scratch_pool)
{
  const char *suffix = "";
  const char *abspath = local_abspath;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  while (TRUE)
  {
    svn_wc_adm_access_t *adm_access =
        svn_wc__adm_retrieve_internal2(db, abspath, scratch_pool);

    if (adm_access != NULL)
      {
        *rel_path = svn_dirent_join(svn_wc_adm_access_path(adm_access),
                                    suffix, result_pool);
        return SVN_NO_ERROR;
      }

    if (svn_dirent_is_root(abspath, strlen(abspath)))
      {
        /* Not found, so no problem calling it abspath */
        *rel_path = apr_pstrdup(result_pool, local_abspath);
        return SVN_NO_ERROR;
      }

    suffix = svn_dirent_join(suffix, svn_dirent_basename(local_abspath, NULL),
                             scratch_pool);
    abspath = svn_dirent_dirname(abspath, scratch_pool);
  }
}

