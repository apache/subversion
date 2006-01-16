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

#include <apr.h>
#if APR_HAS_THREADS
#include <apr_thread_mutex.h>
#include <apr_thread_proc.h>
#include <apr_time.h>
#endif

#include <apr_atomic.h>
#include <apr_strings.h>
#include <apr_hash.h>

#include "svn_path.h"
#include "svn_pools.h"
#include "svn_utf.h"

#include "bdb-err.h"
#include "bdb_compat.h"

#include "env.h"

/* A note about the BDB environment descriptor cache.

   With the advent of DB_REGISTER in BDB-4.4, a process may only open
   an environment handle once.  This means that we must maintain a
   cache of open environment handles, with reference counts.  We
   allocate each environment descriptor (a bdb_env_t) from its own
   pool.  The cache itself (and the cache pool) are shared between
   threads, so all direct or indirect access to the pool is serialized
   with a global mutex.

   Because several threads can now hse the same DB_ENV handle, we must
   use the DB_THREAD flag when opening the environments, otherwise the
   env handles (and all of libsvn_fs_base) won't be thread-safe.

   If we use DB_THREAD, however, all of the code that reads data from
   the database without a cursor must use either DB_DBT_MALLOC,
   DB_DBT_REALLOC, or DB_DBT_USERMEM, as described in the BDB
   documentation.

   (Oh, yes -- using DB_THREAD might not work on some systems. But
   then, it's quite probable that threading is seriously broken on
   those systems anyway, so we'll rely on APR_HAS_THREADS.)
*/


/* The apr_atomic API changed somewhat between apr-0.x and apr-1.x.

   ### Should we move these defines to svn_private_config.h? */
#include <apr_version.h>
#if APR_MAJOR_VERSION > 0
# define svn__atomic_t apr_uint32_t
# define svn__atomic_read(mem) apr_atomic_read32((mem))
# define svn__atomic_set(mem, val) apr_atomic_set32((mem), (val))
#else
# define svn__atomic_t apr_atomic_t
# define svn__atomic_read(mem) apr_atomic_read((mem))
# define svn__atomic_set(mem, val) apr_atomic_set((mem), (val))
#endif /* APR_MAJOR_VERSION */


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

/* The cached Berkeley DB environment descriptor. */
struct bdb_env_t
{
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
#if APR_HAS_THREADS
  apr_threadkey_t *error_info;   /* Points to a bdb_error_info_t. */
#else
  bdb_error_info_t error_info;
#endif

  /**************************************************************************/
  /* BDB Environment Cache */

  /* The Berkeley DB environment. */
  DB_ENV *env;

  /* The flags with which this environment was opened.  Reopening the
     environment with a different set of flags is not allowed.  Trying
     to change the state of the DB_PRIVATE flag is an especially bad
     idea, so svn_fs_bdb__open() forbids any flag changes. */
  u_int32_t flags;

  /* The home path of this environment; a canonical SVN path encoded in
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
             should only be accesses via the svn__atomic functions. */
  svn__atomic_t panic;

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


#if APR_HAS_THREADS
/* The pool cleanup for the error info. */
static apr_status_t
cleanup_error_info (void *data)
{
  if (data)
    svn_error_clear(((bdb_error_info_t *)data)->pending_errors);
  return APR_SUCCESS;
}


/* Get the thread-specific error info from a bdb_env_t. */
static bdb_error_info_t *
get_error_info (bdb_env_t *bdb)
{
  void *priv;
  apr_threadkey_private_get(&priv, bdb->error_info);
  if (!priv)
    {
      /* Unfortunately we can't rely on the threadkey destructor for
         freeing the thread-specific errors related to the
         environment; APR never calls it on Windows.

         We *can* rely on pool cleanups, even though that means we'll
         create a pool per thread per environment. We can only hope
         that the lifetime of a bdb_env_t is moderately limited. */
      apr_pool_t *pool = svn_pool_create(bdb->pool);
      priv = apr_pcalloc(pool, sizeof(bdb_error_info_t));
      apr_pool_cleanup_register(pool, priv, cleanup_error_info,
                                apr_pool_cleanup_null);
      apr_threadkey_private_set(priv, bdb->error_info);
    }
  return priv;
}
#else
#define get_error_info(bdb) (&(bdb)->error_info)
#endif /* APR_HAS_THREADS */


/* Convert a BDB error to a Subversion error. */
static svn_error_t *
convert_bdb_error (bdb_env_t *bdb, int db_err)
{
  if (db_err)
    {
      bdb_env_baton_t bdb_baton;
      bdb_baton.env = bdb->env;
      bdb_baton.bdb = bdb;
      bdb_baton.error_info = get_error_info(bdb);
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
  bdb_error_info_t *error_info = get_error_info((bdb_env_t *) baton);
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
static apr_status_t
cleanup_env (void *data)
{
  bdb_env_t *bdb = data;

#if APR_HAS_THREADS
  apr_threadkey_private_delete(bdb->error_info);
#else
  /* In a threaded environment, the error info is thread-specific and
     has an associated destructor which clears the errors. */
  svn_error_clear(bdb->error_info.pending_errors);
#endif /* APR_HAS_THREADS */

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

#if APR_HAS_THREADS
  {
    /* We can't use a destructor function, see get_error_info() above... */
    apr_status_t apr_err = apr_threadkey_private_create(&bdb->error_info,
                                                        NULL, pool);
    if (apr_err)
      return svn_error_create(apr_err, NULL,
                              "Can't allocate thread-specific storage"
                              " for the Berkeley DB environment descriptor");
  }
#endif /* APR_HAS_THREADS */

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

#if APR_HAS_THREADS
/* The mutex that protects bdb_cache. */
static apr_thread_mutex_t *bdb_cache_lock = NULL;

/* Magic values for atomic initialization of the environment cache. */
static void *bdb_cache_start_init = &bdb_cache_start_init;
static void *bdb_cache_init_failed = &bdb_cache_init_failed;
static void *bdb_cache_initialized = &bdb_cache_initialized;
static volatile void *bdb_cache_state = NULL;
#endif /* APR_HAS_THREADS */


/* Iniitalize the environment descriptor cache. */
static svn_error_t *
bdb_cache_init (void)
{
  /* We have to initialize the cache exactly once.  Because APR
     doesn't have statically-initialized mutexes, we implement a poor
     man's spinlock using apr_atomic_casptr. */
#if APR_HAS_THREADS
  apr_status_t apr_err;
  void *cache_state = apr_atomic_casptr(&bdb_cache_state,
                                        bdb_cache_start_init, NULL);
#else
  void *cache_state = bdb_cache_pool;
#endif /* APR_HAS_THREADS */

  if (!cache_state)
    {
      bdb_cache_pool = svn_pool_create(NULL);
      bdb_cache = apr_hash_make(bdb_cache_pool);
#if APR_HAS_THREADS
      apr_err = apr_thread_mutex_create(&bdb_cache_lock,
                                        APR_THREAD_MUTEX_DEFAULT,
                                        bdb_cache_pool);
      if (apr_err)
        {
          /* Tell other threads that the initialisation failed. */
          apr_atomic_casptr(&bdb_cache_state,
                            bdb_cache_init_failed,
                            bdb_cache_start_init);
          return svn_error_create(apr_err, NULL,
                                  "Couldn't initialize the cache of"
                                  " Berkeley DB environment descriptors");
        }

      apr_atomic_casptr(&bdb_cache_state,
                        bdb_cache_initialized,
                        bdb_cache_start_init);
#endif /* APR_HAS_THREADS */
    }
#if APR_HAS_THREADS
  /* Wait for whichever thread is initializing the cache to finish. */
  /* XXX FIXME: Should we have a maximum wait here, like we have in
                the Windows file IO spinner? */
  else while (cache_state != bdb_cache_initialized)
    {
      if (cache_state == bdb_cache_init_failed)
        return svn_error_create(SVN_ERR_FS_GENERAL, NULL,
                                "Couldn't initialize the cache of"
                                " Berkeley DB environment descriptors");

      apr_sleep(APR_USEC_PER_SEC / 1000);
      cache_state = apr_atomic_casptr(&bdb_cache_state, NULL, NULL);
    }
#endif /* APR_HAS_THREADS */

  return SVN_NO_ERROR;
}


static APR_INLINE void
acquire_cache_mutex(void)
{
#if APR_HAS_THREADS
  apr_thread_mutex_lock(bdb_cache_lock);
#endif
}


static APR_INLINE void
release_cache_mutex(void)
{
#if APR_HAS_THREADS
  apr_thread_mutex_unlock(bdb_cache_lock);
#endif
}


/* Construct a cache key for the BDB environment at PATH in *KEYP.
   if DBCONFIG_FILE is not NULL, return the opened file handle.
   Allocate from POOL. */
static svn_error_t *
bdb_cache_key (bdb_env_key_t *keyp, apr_file_t **dbconfig_file,
               const char *path, apr_pool_t *pool)
{
  const char *dbcfg_file_name = svn_path_join(path, BDB_CONFIG_FILE, pool);
  apr_file_t *dbcfg_file;
  apr_status_t apr_err;
  apr_finfo_t finfo;

  SVN_ERR(svn_io_file_open(&dbcfg_file, dbcfg_file_name,
                           APR_READ, APR_OS_DEFAULT, pool));

  apr_err = apr_file_info_get(&finfo, APR_FINFO_DEV | APR_FINFO_INODE,
                              dbcfg_file);
  if (apr_err)
    return svn_error_wrap_apr
      (apr_err, "Can't create BDB environment cache key");

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
      *panicp = !!svn__atomic_read(&bdb->panic);
      if (!*panicp
          && (bdb->env->get_flags(bdb->env, &flags)
              || (flags & DB_PANIC_ENVIRONMENT)))
        {
          /* Something is wrong with the environment. */
          svn__atomic_set(&bdb->panic, TRUE);
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
  svn_error_t *err = SVN_NO_ERROR;
  bdb_env_t *bdb = bdb_baton->bdb;

  assert(bdb_baton->env == bdb_baton->bdb->env);
  SVN_ERR(bdb_cache_init());

  /* Neutralize bdb_baton's pool cleanup to prevent double-close. See
     cleanup_env_baton(). */
  bdb_baton->bdb = NULL;

  assert(bdb_baton->env == bdb->env);
  bdb_baton->env = NULL;

  acquire_cache_mutex();

  if (--bdb->refcount != 0)
    {
      release_cache_mutex();

      /* If the environment is panicked and automatic recovery is not
         enabled, return an appropriate error. */
      if (!SVN_BDB_AUTO_RECOVER && svn__atomic_read(&bdb->panic))
        err = svn_error_create(SVN_ERR_FS_BERKELEY_DB, NULL,
                               db_strerror(DB_RUNRECOVERY));
    }
  else
    {
      apr_hash_set(bdb_cache, &bdb->key, sizeof bdb->key, NULL);
      err = bdb_close(bdb);
      release_cache_mutex();
    }
  return err;
}



/* Open and initialize a BDB environment. */
static svn_error_t *
bdb_open (bdb_env_t *bdb, u_int32_t flags, int mode)
{
#if APR_HAS_THREADS
  flags |= DB_THREAD;
#endif
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
static apr_status_t
cleanup_env_baton (void *data)
{
  bdb_env_baton_t *bdb_baton = data;

  if (bdb_baton->bdb)
    svn_error_clear(svn_fs_bdb__close(bdb_baton));

  return APR_SUCCESS;
}


svn_error_t *
svn_fs_bdb__open (bdb_env_baton_t **bdb_batonp, const char *path,
                  u_int32_t flags, int mode,
                  apr_pool_t *pool)
{
  svn_error_t *err = SVN_NO_ERROR;
  bdb_env_key_t key;
  bdb_env_t *bdb;
  svn_boolean_t panic;

  SVN_ERR(bdb_cache_init());
  acquire_cache_mutex();

  /* We can safely discard the open DB_CONFIG file handle.  If the
     environment descriptor is in the cache, the key's immutability is
     guaranteed.  If it's not, we don't care if the key changes,
     between here and the actual insertion of the newly-created
     environment into the cache, because no other thread can touch the
     cache in the meantime. */
  err = bdb_cache_key(&key, NULL, path, pool);
  if (err)
    {
      release_cache_mutex();
      return err;
    }

  bdb = bdb_cache_get(&key, &panic);
  if (panic)
    {
      release_cache_mutex();
      return svn_error_create(SVN_ERR_FS_BERKELEY_DB, NULL,
                              db_strerror(DB_RUNRECOVERY));
    }

  /* Make sure that the environment's open flags haven't changed. */
  if (bdb && bdb->flags != flags)
    {
      release_cache_mutex();

      /* Handle changes to the DB_PRIVATE flag specially */
      if ((flags ^ bdb->flags) & DB_PRIVATE)
        {
          if (flags & DB_PRIVATE)
            return svn_error_create(SVN_ERR_FS_BERKELEY_DB, NULL,
                                    "Reopening a public Berkeley DB"
                                    " environment with private attributes");
          else
            return svn_error_create(SVN_ERR_FS_BERKELEY_DB, NULL,
                                    "Reopening a private Berkeley DB"
                                    " environment with public attributes");
        }

      /* Otherwise return a generic "flags-mismatch" error. */
      return svn_error_create(SVN_ERR_FS_BERKELEY_DB, NULL,
                              "Reopening a Berkeley DB environment"
                              " with different attributes");
    }

  if (!bdb)
    {
      err = create_env(&bdb, path, svn_pool_create(bdb_cache_pool));
      if (!err)
        {
          err = bdb_open(bdb, flags, mode);
          if (!err)
            {
              apr_hash_set(bdb_cache, &bdb->key, sizeof bdb->key, bdb);
              bdb->flags = flags;
              bdb->refcount = 1;
            }
          else
            {
              /* Clean up, and we can't do anything about returned errors. */
              svn_error_clear(bdb_close(bdb));
            }
        }
    }
  else
    {
      ++bdb->refcount;
    }

  release_cache_mutex();

  if (!err)
    {
      *bdb_batonp = apr_palloc(pool, sizeof **bdb_batonp);
      (*bdb_batonp)->env = bdb->env;
      (*bdb_batonp)->bdb = bdb;
      (*bdb_batonp)->error_info = get_error_info(bdb);
      apr_pool_cleanup_register(pool, *bdb_batonp, cleanup_env_baton,
                                apr_pool_cleanup_null);
    }
  return err;
}


svn_boolean_t
svn_fs_bdb__get_panic (bdb_env_baton_t *bdb_baton)
{
  assert(bdb_baton->env == bdb_baton->bdb->env);
  return !!svn__atomic_read(&bdb_baton->bdb->panic);
}

void
svn_fs_bdb__set_panic (bdb_env_baton_t *bdb_baton)
{
  assert(bdb_baton->env == bdb_baton->bdb->env);
  svn__atomic_set(&bdb_baton->bdb->panic, TRUE);
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
}
