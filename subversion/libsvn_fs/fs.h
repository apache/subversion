/* fs.h : interface to Subversion filesystem, private to libsvn_fs
 *
 * ====================================================================
 * Copyright (c) 2000-2001 CollabNet.  All rights reserved.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution.  The terms
 * are also available at http://subversion.tigris.org/license-1.html.
 * If newer versions of this license are posted there, you may use a
 * newer version instead, at your option.
 * ====================================================================
 */

#ifndef SVN_LIBSVN_FS_FS_H
#define SVN_LIBSVN_FS_FS_H

#include "db.h"                 /* Berkeley DB interface */
#include "apr_pools.h"
#include "apr_hash.h"
#include "svn_fs.h"


/*** Repository layout. ***/

/* The top-level repository dir contains a README and various
   subdirectories.  */
#define SVN_FS__REPOS_README      "README"  /* Explanation for trespassers. */
#define SVN_FS__REPOS_DB_DIR      "db"      /* Where Berkeley lives. */
#define SVN_FS__REPOS_DAV_DIR     "dav"     /* DAV sandbox. */
#define SVN_FS__REPOS_CONF_DIR    "conf"    /* Hook configuration files. */
#define SVN_FS__REPOS_CUSTOM_DIR  "custom"  /* User space; svn stays out. */

/* In the repository conf directory, look for these files. */
#define SVN_FS__REPOS_HOOK_START_COMMIT    "start-commit"
#define SVN_FS__REPOS_HOOK_PRE_COMMIT      "pre-commit"
#define SVN_FS__REPOS_HOOK_POST_COMMIT     "post-commit"
#define SVN_FS__REPOS_HOOK_READ_SENTINEL   "read-sentinels"
#define SVN_FS__REPOS_HOOK_WRITE_SENTINEL  "write-sentinels"

/* The extension added to the names of example hook scripts. */
#define SVN_FS__REPOS_HOOK_DESC_EXT        ".tmpl"


/*** The filesystem structure.  ***/

struct svn_fs_t {

  /* A pool managing this filesystem.  Freeing this pool must
     completely clean up the filesystem, including any database
     or system resources it holds.  */
  apr_pool_t *pool;

  /* The path to the repository's top-level directory. */
  char *path;

  /* The path to the repository's dav directory. */
  char *dav_path;

  /* The path to the repository's conf directory. */
  char *conf_path;

  /* The path to the Berkeley DB environment. */
  char *env_path;

  /* A Berkeley DB environment for all the filesystem's databases.
     This establishes the scope of the filesystem's transactions.  */
  DB_ENV *env;

  /* The filesystem's various tables.  See `structure' for details.  */
  DB *nodes, *revisions, *transactions, *representations, *strings;

  /* A callback function for printing warning messages, and a baton to
     pass through to it.  */
  svn_fs_warning_callback_t warning;
  void *warning_baton;

  /* A kludge for handling errors noticed by APR pool cleanup functions.

     The APR pool cleanup functions can only return an apr_status_t
     value, not a full svn_error_t value.  This makes it difficult to
     propagate errors detected by fs_cleanup to someone who can handle
     them.

     If FS->cleanup_error is non-zero, it points to a location where
     fs_cleanup should store a pointer to an svn_error_t object, if it
     generates one.  Normally, it's zero, but if the cleanup is
     invoked by code prepared to deal with an svn_error_t object in
     some helpful way, it can create its own svn_error_t *, set it to
     zero, set cleanup_error to point to it, free the pool (thus
     invoking the cleanup), and then check its svn_error_t to see if
     anything went wrong.

     Of course, if multiple errors occur, this will only report one of
     them, but it's better than nothing.  In the case of a cascade,
     the first error message is probably the most helpful, so
     fs_cleanup won't overwrite a pointer to an existing svn_error_t
     if it finds one.  */
  svn_error_t **cleanup_error;
};


#endif /* SVN_LIBSVN_FS_FS_H */



/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
