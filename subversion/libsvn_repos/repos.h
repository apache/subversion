/* repos.h : interface to Subversion repository, private to libsvn_repos
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

#ifndef SVN_LIBSVN_REPOS_H
#define SVN_LIBSVN_REPOS_H

#include "apr_pools.h"
#include "apr_hash.h"
#include "svn_fs.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/*** Repository version number. */
#define SVN_REPOS__VERSION     1


/*** Repository layout. ***/

/* The top-level repository dir contains a README and various
   subdirectories.  */
#define SVN_REPOS__README      "README"  /* Explanation for trespassers. */
#define SVN_REPOS__FORMAT      "format"  /* Stores the current version
                                            of the repository. */
#define SVN_REPOS__DB_DIR      "db"      /* Where Berkeley lives. */
#define SVN_REPOS__DAV_DIR     "dav"     /* DAV sandbox. */
#define SVN_REPOS__LOCK_DIR    "locks"   /* Lock files live here. */
#define SVN_REPOS__HOOK_DIR    "hooks"   /* Hook programs. */
#define SVN_REPOS__CONF_DIR    "conf"    /* Configuration files. */

/* Things for which we keep lockfiles. */
#define SVN_REPOS__DB_LOCKFILE "db.lock" /* Our Berkeley lockfile. */

/* In the repository hooks directory, look for these files. */
#define SVN_REPOS__HOOK_START_COMMIT    "start-commit"
#define SVN_REPOS__HOOK_PRE_COMMIT      "pre-commit"
#define SVN_REPOS__HOOK_POST_COMMIT     "post-commit"
#define SVN_REPOS__HOOK_READ_SENTINEL   "read-sentinels"
#define SVN_REPOS__HOOK_WRITE_SENTINEL  "write-sentinels"
#define SVN_REPOS__HOOK_PRE_REVPROP_CHANGE  "pre-revprop-change"
#define SVN_REPOS__HOOK_POST_REVPROP_CHANGE "post-revprop-change"


/* The extension added to the names of example hook scripts. */
#define SVN_REPOS__HOOK_DESC_EXT        ".tmpl"


/* The Repository object, created by svn_repos_open() and
   svn_repos_create(), allocated in POOL. */
struct svn_repos_t
{
  /* A Subversion filesystem object. */
  svn_fs_t *fs;

  /* The path to the repository's top-level directory. */
  char *path;

  /* The path to the repository's dav directory. */
  char *dav_path;

  /* The path to the repository's conf directory. */
  char *conf_path;

  /* The path to the repository's hooks directory. */
  char *hook_path;

  /* The path to the repository's locks directory. */
  char *lock_path;

  /* The path to the Berkeley DB filesystem environment. */
  char *db_path;

  /* A pool, filled with allocated memory, a diving board, and a tube
     slide. */
  apr_pool_t *pool;
};


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_LIBSVN_REPOS_H */


/* 
 * local variables:
 * eval: (load-file "../../tools/dev/svn-dev.el")
 * end:
 */
