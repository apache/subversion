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

#include <assert.h>

#include <apr_strings.h>
#include <apr_hash.h>
#include <apr_version.h>

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


/* The cache key for a Berkeley DB environment descriptor.  This is a
   combination of the device ID and INODE number of the Berkeley DB
   config file.

   XXX FIXME: Although the dev+inode combination is supposed do be
   unique, apparently that's not always the case with some remote
   filesystems.  We /should/ be safe using this as a unique hash key,
   because the database must be on a local filesystem.  We can hope,
   anyway. */
typedef struct
{
  apr_dev_t device;
  apr_ino_t inode;
} bdb_env_key_t;

#if APR_MAJOR_VERSION == 0
#define apr_atomic_read32 apr_atomic_read
#define apr_atomic_set32  apr_atomic_set
#endif

/* The cached Berkeley DB environment descriptor. */
struct bdb_env_t
{
  /* XXX TODO: Make this structure thread-safe. */

  /**************************************************************************/
  /* Error Reporting */

  /* Berkeley DB returns extended error info by callback before returning
     an error code from the failing function.  The callback baton type is a
     string, not an arbitrary struct, so we prefix our struct with a valid
     string, to avoid problems should BDB ever try to interpret our baton as
     a string.  Initializers of this structure must strcpy the value of
     BDB_ERRPFX_STRING into this array.  */
  char errpfx_string[sizeof(BDB_ERRPFX_STRING)];

  /* Extended error information. */
  bdb_error_info_t error_info;

  /**************************************************************************/
  /* BDB Environment Cache */

  /* The Berkeley DB environment. */
  DB_ENV *env;

  /* The home path of this environment; a canonical SVN path ecoded in
     UTF-8 and allocated from this decriptor's pool. */
  const char *path;

  /* The home path of this environment, in the form expected by BDB. */
  const char *path_bdb;

  /* The reference count for this environment handle; this is
     essentially the difference between the number of calls to
     svn_fs_bdb__open and svn_fs_bdb__close. */
  unsigned refcount;

  /* If this flag is TRUE, someone has detected that the environment
     descriptor is in a panicked state and should be removed from the
     cache.

     Note 1: Once this flag is set, it must not be cleared again.

     Note 2: Unlike other fields in this structure, this field is not
             protected by the cache mutex on threaded platforms, and
             should only be accesses via the apr_atomic functions. */
#if APR_MAJOR_VERSION == 0
  apr_atomic_t panic;
#else
  apr_uint32_t panic;
#endif

  /* The key for the environment descriptor cache. */
  bdb_env_key_t key;

  /* The handle of the open DB_CONFIG file.

     We keep the DB_CONFIG file open in this process as long as the
     environment handle itself is open.  On Windows, this guarantees
     that the cache key remains unique; here's what the Windows SDK
     docs have to say about the file index (interpreted as the INODE
     number by APR):

        "This value is useful only while the file is open by at least
        one process.  If no processes have it open, the index may
        change the next time the file is opened."

     Now, we certainly don't want a unique key to change while it's
     being used, do we... */
  apr_file_t *dbconfig_file;

  /* The pool associated with this environment descriptor.

     Because the descriptor has a life of its own, the structure and
     any data associated with it are allocated from their own global
     pool. */
  apr_pool_t *pool;

};


static svn_error_t *
convert_bdb_error (bdb_env_t *bdb, int db_err)
{
  if (db_err)
    {
      bdb_env_baton_t bdb_baton;
      bdb_baton.env = bdb->env;
      bdb_baton.bdb = bdb;
      bdb_baton.error_info = &bdb->error_info;
      SVN_BDB_ERR(&bdb_baton, db_err);
    }
  return SVN_NO_ERROR;
}



/* Allocating an appropriate Berkeley DB environment object.  */

/* BDB error callback.  See bdb_error_info_t in env.h for more info.
   Note: bdb_error_gatherer is a macro with BDB < 4.3, so be careful how
   you use it! */
static void
bdb_error_gatherer (const DB_ENV *dbenv, const char *baton, const char *msg)
{
  bdb_error_info_t *error_info =/*FIXME:*/&((bdb_env_t *) baton)->error_info;
  svn_error_t *new_err;

  SVN_BDB_ERROR_GATHERER_IGNORE(dbenv);

  new_err = svn_error_createf(SVN_NO_ERROR, NULL, "bdb: %s", msg);
  if (error_info->pending_errors)
    svn_error_compose(error_info->pending_errors, new_err);
  else
    error_info->pending_errors = new_err;

  if (error_info->user_callback)
    error_info->user_callback(NULL, (char *)msg); /* ### I hate this cast... */
}


/* Pool cleanup for the cached environment descriptor. */
apr_status_t cleanup_env (void *data)
{
  bdb_env_t *bdb = data;

  svn_error_clear(/*FIXME:*/bdb->error_info.pending_errors);

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
  return convert_bdb_error(bdb, db_err);
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
      *panicp = !!apr_atomic_read32(&bdb->panic);
      if (!*panicp
          && (bdb->env->get_flags(bdb->env, &flags)
              || (flags & DB_PANIC_ENVIRONMENT)))
        {
          /*FIXME:*/fprintf(stderr, "bdb_cache_get(%s): PANIC\n",
                            bdb->path_bdb);

          /* Something is wrong with the environment. */
          apr_atomic_set32(&bdb->panic, TRUE);
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
    err = convert_bdb_error(bdb, db_err);

  apr_pool_destroy(bdb->pool);
  return err;
}


svn_error_t *
svn_fs_bdb__close (bdb_env_baton_t *bdb_baton)
{
  /* Neutralize bdb_baton's pool cleanup to prevent double-close. See
     cleanup_env_baton(). */
  bdb_env_t *bdb = bdb_baton->bdb;
  bdb_baton->bdb = NULL;

  assert(bdb_baton->env == bdb->env);
  bdb_baton->env = NULL;

  /* FIXME: ACQUIRE CACHE MUTEX */

  if (--bdb->refcount != 0)
    {
      /* FIXME: RELEASE CACHE MUTEX */

      /* If the environment is panicked and automatic recovery is not
         enabled, return an appropriate error. */
      if (!SVN_BDB_AUTO_RECOVER && apr_atomic_read32(&bdb->panic))
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
  SVN_ERR(convert_bdb_error
          (bdb, bdb->env->open(bdb->env, bdb->path_bdb, flags, mode)));

#if SVN_BDB_AUTO_COMMIT
  /* Assert the BDB_AUTO_COMMIT flag on the opened environment. This
     will force all operations on the environment (and handles that
     are opened within the environment) to be transactional. */

  SVN_ERR(convert_bdb_error
          (bdb, bdb->env->set_flags(bdb->env, SVN_BDB_AUTO_COMMIT, 1)));
#endif

  SVN_ERR(bdb_cache_key(&bdb->key, &bdb->dbconfig_file,
                        bdb->path, bdb->pool));

  return SVN_NO_ERROR;
}


/* Pool cleanup for the environment baton. */
apr_status_t cleanup_env_baton (void *data)
{
  bdb_env_baton_t *bdb_baton = data;

  if (bdb_baton->bdb)
    {
      /*FIXME:*/fprintf(stderr, "cleanup_env_baton(%s)\n",
                        bdb_baton->bdb->path_bdb);
      svn_error_clear(svn_fs_bdb__close(bdb_baton));
    }

  return APR_SUCCESS;
}


svn_error_t *
svn_fs_bdb__open (bdb_env_baton_t **bdb_batonp, const char *path,
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
    {
      *bdb_batonp = apr_palloc(pool, sizeof **bdb_batonp);
      (*bdb_batonp)->env = bdb->env;
      (*bdb_batonp)->bdb = bdb;
      (*bdb_batonp)->error_info = &bdb->error_info;
      apr_pool_cleanup_register(pool, *bdb_batonp, cleanup_env_baton,
                                apr_pool_cleanup_null);
    }
  return err;
}


svn_boolean_t svn_fs_bdb__get_panic (bdb_env_baton_t *bdb_baton)
{
  assert(bdb_baton->env == bdb_baton->bdb->env);
  return !!apr_atomic_read32(&bdb_baton->bdb->panic);
}

void svn_fs_bdb__set_panic (bdb_env_baton_t *bdb_baton)
{
  assert(bdb_baton->env == bdb_baton->bdb->env);
  apr_atomic_set32(&bdb_baton->bdb->panic, TRUE);
}


/* This function doesn't actually open the environment, so it doesn't
   have to look in the cache.  Callers are supposed to own an
   exclusive lock on the filesystem anyway. */
svn_error_t *
svn_fs_bdb__remove (const char *path, apr_pool_t *pool)
{
  bdb_env_t *bdb;

  SVN_ERR(create_env(&bdb, path, pool));
  return convert_bdb_error
    (bdb, bdb->env->remove(bdb->env, bdb->path_bdb, DB_FORCE));

  return SVN_NO_ERROR;
}
