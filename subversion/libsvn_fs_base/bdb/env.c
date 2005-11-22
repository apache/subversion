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


svn_error_t *
svn_fs_bdb__path_from_utf8 (const char **path_bdb,
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


int
svn_fs_bdb__open_env (DB_ENV **env, const char *path,
                      u_int32_t flags, int mode)
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

  BDB_ERR((*env)->open(*env, path, flags, mode));

#if SVN_BDB_AUTO_COMMIT
  /* Assert the BDB_AUTO_COMMIT flag on the opened environment. This
     will force all operations on the environment (and handles that
     are opened within the environment) to be transactional. */

  BDB_ERR((*env)->set_flags(*env, SVN_BDB_AUTO_COMMIT, 1));
#endif

  return 0;
}


int
svn_fs_bdb__close_env (DB_ENV *env)
{
  /* XXX TODO: Maintain the env handle cache. */

  return env->close(env, 0);
}
