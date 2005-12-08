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

#include "svn_path.h"
#include "svn_utf.h"

#include "bdb-err.h"
#include "bdb_compat.h"

#include "env.h"



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


/* Create a Berkeley DB environment. */
static int
create_env (bdb_env_t **bdbp, apr_pool_t *pool)
{
  bdb_env_t *bdb = apr_pcalloc(pool, sizeof(*bdb));
  int db_err = db_env_create(&(bdb->env), 0);

  /* We must initialize this now, as our callers may assume their bdb
     pointer is valid when checking for errors.  */
  apr_cpystrn (bdb->errpfx_string,
               BDB_ERRCALL_BATON_ERRPFX_STRING,
               sizeof(bdb->errpfx_string));
  *bdbp = bdb;

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
  return db_err;
}



static svn_error_t *
bdb_path_from_utf8 (const char **path_bdb,
                    const char *path_utf8,
                    apr_pool_t *pool)
{
#if SVN_BDB_PATH_UTF8
  *path_bdb = path_utf8;
#else
  SVN_ERR(svn_utf_cstring_from_utf8(path_bdb, path_utf8, pool));
#endif
  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_bdb__open (bdb_env_t **bdbp, const char *path,
                  u_int32_t flags, int mode,
                  apr_pool_t *pool)
{
  /* XXX TODO:

     With the advent of DB_REGISTER and BDB-4.4, a process may only
     open the environment handle once.  This means that we must
     maintain a cache of open environment handles, with reference
     counts.  This *also* means that we must set the DB_THREAD flag on
     the environments, otherwise the env handles (and all of
     libsvn_fs_base) won't be thread-safe.

     Before setting DB_THREAD, however, we have to check all of the
     code that reads data from the database without a cursor, and make
     sure it's using either DB_DBT_MALLOC, DB_DBT_REALLOC, or
     DB_DBT_USERMEM, as described in the BDB documentation.

     (Oh, yes -- using DB_THREAD might not work on some systems. But
     then, it's quite probable that threading is seriously broken on
     those systems anyway, so we'll rely on APR_HAS_THREADS.)
  */
  const char *path_bdb;
  bdb_env_t *bdb;

  SVN_ERR(bdb_path_from_utf8(&path_bdb, path, pool));
  SVN_BDB_ERR(bdb, create_env(&bdb, pool));
  SVN_BDB_ERR(bdb, bdb->env->open(bdb->env, path_bdb, flags, mode));

#if SVN_BDB_AUTO_COMMIT
  /* Assert the BDB_AUTO_COMMIT flag on the opened environment. This
     will force all operations on the environment (and handles that
     are opened within the environment) to be transactional. */

  SVN_BDB_ERR(bdb, bdb->env->set_flags(bdb->env, SVN_BDB_AUTO_COMMIT, 1));
#endif

  *bdbp = bdb;
  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_bdb__close (bdb_env_t *bdb)
{
  /* XXX TODO: Maintain the env handle cache. */

  SVN_BDB_ERR(bdb, bdb->env->close(bdb->env, 0));
  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_bdb__remove (const char *path, apr_pool_t *pool)
{
  const char *path_bdb;
  bdb_env_t *bdb;

  SVN_ERR(bdb_path_from_utf8(&path_bdb, path, pool));
  SVN_BDB_ERR(bdb, create_env(&bdb, pool));
  SVN_BDB_ERR(bdb, bdb->env->remove (bdb->env, path_bdb, DB_FORCE));

  return SVN_NO_ERROR;
}
