/* env.h : managing the BDB environment
 *
 * ====================================================================
 * Copyright (c) 2000-2005 CollabNet.  All rights reserved.
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

#include <apr_strings.h>
#include <apr_hash.h>

#include "svn_path.h"
#include "svn_pools.h"
#include "svn_utf.h"

#include "bdb-err.h"
#include "bdb_compat.h"

#include "env.h"

/* A note about the BDB environment descriptor cache.

   With the advent of DB_REGISTER and BDB-4.4, a process may only open
   the environment handle once.  This means that we must maintain a
   cache of open environment handles, with reference counts.  This
   *also* means that we must set the DB_THREAD flag on the
   environments, otherwise the env handles (and all of libsvn_fs_base)
   won't be thread-safe.

   Before setting DB_THREAD, however, we have to check all of the code
   that reads data from the database without a cursor, and make sure
   it's using either DB_DBT_MALLOC, DB_DBT_REALLOC, or DB_DBT_USERMEM,
   as described in the BDB documentation.

   (Oh, yes -- using DB_THREAD might not work on some systems. But
   then, it's quite probable that threading is seriously broken on
   those systems anyway, so we'll rely on APR_HAS_THREADS.)
*/


/* Allocating an appropriate Berkeley DB environment object.  */

/* BDB error callback.  See bdb_env_t in env.h for more info.
   Note: bdb_error_gatherer is a macro with BDB < 4.3, so be careful how
   you use it! */
static void
bdb_error_gatherer (const DB_ENV *dbenv, const char *baton, const char *msg)
{
  bdb_env_t *bdb = (bdb_env_t *) baton;
  svn_error_t *new_err;

  SVN_BDB_ERROR_GATHERER_IGNORE(dbenv);

  new_err = svn_error_createf(SVN_NO_ERROR, NULL, "bdb: %s", msg);
  if (bdb->pending_errors)
    svn_error_compose(bdb->pending_errors, new_err);
  else
    bdb->pending_errors = new_err;

  if (bdb->user_callback)
    bdb->user_callback(NULL, (char *)msg); /* ### I hate this cast... */
}


/* Pool cleanup for the environment descriptor. */
apr_status_t cleanup_env (void *data)
{
  bdb_env_t *bdb = data;

  svn_error_clear(bdb->pending_errors);

  if (bdb->dbconfig_file)
    apr_file_close(bdb->dbconfig_file);

  return APR_SUCCESS;
}


/* Create a Berkeley DB environment. */
static svn_error_t *
create_env (bdb_env_t **bdbp, const char *path, apr_pool_t *pool)
{
  int db_err;
  bdb_env_t *bdb = apr_pcalloc(pool, sizeof(*bdb));

  /* We must initialize this now, as our callers may assume their bdb
     pointer is valid when checking for errors.  */
  apr_pool_cleanup_register(pool, bdb, cleanup_env, apr_pool_cleanup_null);
  apr_cpystrn(bdb->errpfx_string, BDB_ERRPFX_STRING,
              sizeof(bdb->errpfx_string));

  bdb->path = apr_pstrdup(pool, path);
#if SVN_BDB_PATH_UTF8
  bdb->path_bdb = svn_path_local_style(bdb->path, pool);
#else
  SVN_ERR(svn_utf_cstring_from_utf8(&bdb->path_bdb,
                                    svn_path_local_style(bdb->path, pool),
                                    pool));
#endif

  bdb->pool = pool;
  *bdbp = bdb;

  db_err = db_env_create(&(bdb->env), 0);
  if (!db_err)
    {
      bdb->env->set_errpfx(bdb->env, (char *) bdb);
      /* bdb_error_gatherer is in parens to stop macro expansion. */
      bdb->env->set_errcall(bdb->env, (bdb_error_gatherer));

      /* Needed on Windows in case Subversion and Berkeley DB are using
         different C runtime libraries  */
      db_err = bdb->env->set_alloc(bdb->env, malloc, realloc, free);

      /* If we detect a deadlock, select a transaction to abort at
         random from those participating in the deadlock.  */
      if (!db_err)
        db_err = bdb->env->set_lk_detect(bdb->env, DB_LOCK_RANDOM);
    }
  SVN_BDB_ERR(bdb, db_err);
  return SVN_NO_ERROR;
}



/* The environment descriptor cache. */

/* The global pool used for this cache. */
static apr_pool_t *bdb_cache_pool = NULL;

/* The cache.  The items are bdb_env_t structures. */
static apr_hash_t *bdb_cache = NULL;


/* Iniitalize the environment descriptor cache. */
static void
bdb_cache_init (void)
{
  if (!bdb_cache_pool)
    {
      bdb_cache_pool = svn_pool_create(NULL);
      bdb_cache = apr_hash_make(bdb_cache_pool);
      /* FIXME: INITIALIZE CACHE MUTEX */
    }
}


/* Construct a cache key for the BDB environment at PATH in *KEYP.
   if DBCONFIG_FILE is not NULL, return the opened file handle.
   Allocate from POOL. */
static svn_error_t *
bdb_cache_key (bdb_env_key_t *keyp, apr_file_t **dbconfig_file,
               const char *path, apr_pool_t *pool)
{
  const char *dbcfg_file_name = svn_path_join (path, BDB_CONFIG_FILE, pool);
  apr_file_t *dbcfg_file;
  apr_status_t apr_err;
  apr_finfo_t finfo;

  SVN_ERR(svn_io_file_open(&dbcfg_file, dbcfg_file_name,
                           APR_READ, APR_OS_DEFAULT, pool));

  apr_err = apr_file_info_get(&finfo, APR_FINFO_DEV | APR_FINFO_INODE,
                              dbcfg_file);
  if (apr_err)
    return svn_error_wrap_apr(apr_err, "FIXME:");

  /* Make sure that any padding in the key is always cleared, so that
     the key's hash deterministic. */
  memset(keyp, 0, sizeof *keyp);
  keyp->device = finfo.device;
  keyp->inode = finfo.inode;

  if (dbconfig_file)
    *dbconfig_file = dbcfg_file;
  else
    apr_file_close(dbcfg_file);

  return SVN_NO_ERROR;
}


/* Find a BDB environment in the cache.
   Return the environment's panic state in *PANICP.

   Note: You MUST acquire the cache mutex before calling this function.
*/
static bdb_env_t *
bdb_cache_get (const bdb_env_key_t *keyp, svn_boolean_t *panicp)
{
  bdb_env_t *bdb = apr_hash_get(bdb_cache, keyp, sizeof *keyp);
  if (bdb && bdb->env)
    {
      u_int32_t flags;
      *panicp = !!apr_atomic_read(&bdb->panic);
      if (!*panicp
          && (bdb->env->get_flags(bdb->env, &flags)
              || (flags & DB_PANIC_ENVIRONMENT)))
        {
          /*FIXME:*/fprintf(stderr, "bdb_cache_get(%s): PANIC\n",
                            bdb->path_bdb);

          /* Something is wrong with the environment. */
          apr_atomic_set(&bdb->panic, TRUE);
          *panicp = TRUE;
          bdb = NULL;
        }
    }
  else
    {
      *panicp = FALSE;
    }
  return bdb;
}



/* Close and destroy a BDB environment descriptor. */
static svn_error_t *
bdb_close (bdb_env_t *bdb)
{
  svn_error_t *err = SVN_NO_ERROR;

  /* This bit is delcate; we must propagate the error from
     DB_ENV->close to the caller, and always destroy the pool. */
  int db_err = bdb->env->close(bdb->env, 0);

  /* If automatic database recovery is enabled, ignore DB_RUNRECOVERY
     errors, since they're dealt with eventually by BDB itself. */
  if (db_err && (!SVN_BDB_AUTO_RECOVER || db_err != DB_RUNRECOVERY))
    err = svn_fs_bdb__dberr(bdb, db_err);

  apr_pool_destroy(bdb->pool);
  return err;
}


svn_error_t *
svn_fs_bdb__close (bdb_env_t *bdb)
{
  /* FIXME: ACQUIRE CACHE MUTEX */

  if (--bdb->refcount != 0)
    {
      /* FIXME: RELEASE CACHE MUTEX */

      /* If the environment is panicked and automatic recovery is not
         enabled, return an appropriate error. */
      if (!SVN_BDB_AUTO_RECOVER && apr_atomic_read(&bdb->panic))
        {
          /*FIXME:*/fprintf(stderr, "svn_fs_bdb__close(%s): PANIC\n",
                            bdb->path_bdb);
          return svn_error_create(SVN_ERR_FS_BERKELEY_DB, NULL,
                                  db_strerror(DB_RUNRECOVERY));
        }
      else
        return SVN_NO_ERROR;
    }

  apr_hash_set(bdb_cache, &bdb->key, sizeof bdb->key, NULL);

  /* FIXME: RELEASE CACHE MUTEX */

  return bdb_close(bdb);
}



/* Open and initialize a BDB environment. */
static svn_error_t *
bdb_open (bdb_env_t *bdb, u_int32_t flags, int mode)
{
  SVN_BDB_ERR(bdb, bdb->env->open(bdb->env, bdb->path_bdb, flags, mode));

#if SVN_BDB_AUTO_COMMIT
  /* Assert the BDB_AUTO_COMMIT flag on the opened environment. This
     will force all operations on the environment (and handles that
     are opened within the environment) to be transactional. */

  SVN_BDB_ERR(bdb, bdb->env->set_flags(bdb->env, SVN_BDB_AUTO_COMMIT, 1));
#endif

  SVN_ERR(bdb_cache_key(&bdb->key, &bdb->dbconfig_file,
                        bdb->path, bdb->pool));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_bdb__open (bdb_env_t **bdbp, const char *path,
                  u_int32_t flags, int mode,
                  apr_pool_t *pool)
{
  /* XXX FIXME: Forbid multiple open of private environment!
     (flags & DB_PRIVATE) */

  svn_error_t *err = SVN_NO_ERROR;
  bdb_env_key_t key;
  bdb_env_t *bdb;
  svn_boolean_t panic;

  bdb_cache_init();
  /* FIXME: ACQUIRE CACHE MUTEX */

  /* We can safely discard the open DB_CONFIG file handle.  If the
     environment descriptor is in the cache, the key's immutability is
     guaranteed.  If it's not, we don't care if the key changes,
     between here and the actual insertion of the newly-created
     environment into the cache, because no other thread can touch the
     cache in the meantime. */
  err = bdb_cache_key(&key, NULL, path, pool);
  if (err)
    {
      /* FIXME: RELEASE CACHE MUTEX */
      return err;
    }
  bdb = bdb_cache_get(&key, &panic);

  if (!bdb)
    {
      err = create_env(&bdb, path, svn_pool_create(bdb_cache_pool));
      if (!err)
        {
          err = bdb_open(bdb, flags, mode);
          if (!err)
            {
              apr_hash_set(bdb_cache, &bdb->key, sizeof bdb->key, bdb);
              bdb->refcount = 1;
            }
          else
            {
              /* Clean up, and we can't do anything about returned errors. */
              svn_error_clear(bdb_close(bdb));
            }
        }
    }
  else if (!panic)
    {
      ++bdb->refcount;
    }

  /* FIXME: RELEASE CACHE MUTEX */

  /* If the environment is panicked, return an appropriate error. */
  if (panic)
    {
      /*FIXME:*/fprintf(stderr, "svn_fs_bdb__open(%s): PANIC\n",
                        bdb->path_bdb);
      err = svn_error_create(SVN_ERR_FS_BERKELEY_DB, NULL,
                             db_strerror(DB_RUNRECOVERY));
    }

  if (!err)
    *bdbp = bdb;
  return err;
}



/* This function doesn't actually open the environment, so it doesn't
   have to look in the cache.  Callers are supposed to own an
   exclusive lock on the filesystem anyway. */
svn_error_t *
svn_fs_bdb__remove (const char *path, apr_pool_t *pool)
{
  bdb_env_t *bdb;

  SVN_ERR(create_env(&bdb, path, pool));
  SVN_BDB_ERR(bdb, bdb->env->remove(bdb->env, bdb->path_bdb, DB_FORCE));

  return SVN_NO_ERROR;
}
