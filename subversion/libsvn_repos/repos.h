/* repos.h : interface to Subversion repository, private to libsvn_repos
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

#ifndef SVN_LIBSVN_REPOS_H
#define SVN_LIBSVN_REPOS_H

#include <apr_pools.h>
#include <apr_hash.h>

#include "svn_fs.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/* Repository format number.
   
   Formats 0, 1 and 2 were pre-1.0.

   Format 3 was current for 1.0 through to 1.3.

   Format 4 was an abortive experiment during the development of the
   locking feature in the lead up to 1.2.
   
   Format 5 was new in 1.4, and is the first format which may contain
   BDB or FSFS filesystems with a FS format other than 1, since prior
   formats are accepted by some versions of Subversion which do not
   pay attention to the FS format number.
*/
#define SVN_REPOS__FORMAT_NUMBER         5
#define SVN_REPOS__FORMAT_NUMBER_LEGACY  3


/*** Repository layout. ***/

/* The top-level repository dir contains a README and various
   subdirectories.  */
#define SVN_REPOS__README      "README.txt" /* Explanation for trespassers. */
#define SVN_REPOS__FORMAT      "format"     /* Stores the current version
                                               of the repository. */
#define SVN_REPOS__DB_DIR      "db"         /* Where Berkeley lives. */
#define SVN_REPOS__DAV_DIR     "dav"        /* DAV sandbox. */
#define SVN_REPOS__LOCK_DIR    "locks"      /* Lock files live here. */
#define SVN_REPOS__HOOK_DIR    "hooks"      /* Hook programs. */
#define SVN_REPOS__CONF_DIR    "conf"       /* Configuration files. */

/* Things for which we keep lockfiles. */
#define SVN_REPOS__DB_LOCKFILE "db.lock" /* Our Berkeley lockfile. */
#define SVN_REPOS__DB_LOGS_LOCKFILE "db-logs.lock" /* BDB logs lockfile. */

/* In the repository hooks directory, look for these files. */
#define SVN_REPOS__HOOK_START_COMMIT    "start-commit"
#define SVN_REPOS__HOOK_PRE_COMMIT      "pre-commit"
#define SVN_REPOS__HOOK_POST_COMMIT     "post-commit"
#define SVN_REPOS__HOOK_READ_SENTINEL   "read-sentinels"
#define SVN_REPOS__HOOK_WRITE_SENTINEL  "write-sentinels"
#define SVN_REPOS__HOOK_PRE_REVPROP_CHANGE  "pre-revprop-change"
#define SVN_REPOS__HOOK_POST_REVPROP_CHANGE "post-revprop-change"
#define SVN_REPOS__HOOK_PRE_LOCK        "pre-lock"
#define SVN_REPOS__HOOK_POST_LOCK       "post-lock"
#define SVN_REPOS__HOOK_PRE_UNLOCK      "pre-unlock"
#define SVN_REPOS__HOOK_POST_UNLOCK     "post-unlock"


/* The extension added to the names of example hook scripts. */
#define SVN_REPOS__HOOK_DESC_EXT        ".tmpl"


/* The configuration file for svnserve, in the repository conf directory. */
#define SVN_REPOS__CONF_SVNSERVE_CONF "svnserve.conf"

/* In the svnserve default configuration, these are the suggested
   locations for the passwd and authz files (in the repository conf
   directory), and we put example templates there. */ 
#define SVN_REPOS__CONF_PASSWD "passwd"
#define SVN_REPOS__CONF_AUTHZ "authz"

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

  /* The format number of this repository. */
  int format;

  /* The FS backend in use within this repository. */
  const char *fs_type;
};


/*** Hook-running Functions ***/

/* Run the start-commit hook for REPOS.  Use POOL for any temporary
   allocations.  If the hook fails, return SVN_ERR_REPOS_HOOK_FAILURE.

   USER is the authenticated name of the user starting the commit.  */
svn_error_t *
svn_repos__hooks_start_commit(svn_repos_t *repos,
                              const char *user,
                              apr_pool_t *pool);

/* Run the pre-commit hook for REPOS.  Use POOL for any temporary
   allocations.  If the hook fails, return SVN_ERR_REPOS_HOOK_FAILURE.  

   TXN_NAME is the name of the transaction that is being committed.  */
svn_error_t *
svn_repos__hooks_pre_commit(svn_repos_t *repos,
                            const char *txn_name,
                            apr_pool_t *pool);

/* Run the post-commit hook for REPOS.  Use POOL for any temporary
   allocations.  If the hook fails, run SVN_ERR_REPOS_HOOK_FAILURE.

   REV is the revision that was created as a result of the commit.  */
svn_error_t *
svn_repos__hooks_post_commit(svn_repos_t *repos,
                             svn_revnum_t rev,
                             apr_pool_t *pool);

/* Run the pre-revprop-change hook for REPOS.  Use POOL for any
   temporary allocations.  If the hook fails, return
   SVN_ERR_REPOS_HOOK_FAILURE.  

   REV is the revision whose property is being changed.
   AUTHOR is the authenticated name of the user changing the prop.
   NAME is the name of the property being changed.  
   NEW_VALUE is the new value of the property.
   ACTION is indicates if the property is being 'A'dded, 'M'odified,
   or 'D'eleted.

   The pre-revprop-change hook will have the new property value
   written to its stdin.  If the property is being deleted, no data
   will be written. */
svn_error_t *
svn_repos__hooks_pre_revprop_change(svn_repos_t *repos,
                                    svn_revnum_t rev,
                                    const char *author,
                                    const char *name,
                                    const svn_string_t *new_value,
                                    char action,
                                    apr_pool_t *pool);

/* Run the pre-revprop-change hook for REPOS.  Use POOL for any
   temporary allocations.  If the hook fails, return
   SVN_ERR_REPOS_HOOK_FAILURE. 

   REV is the revision whose property was changed.
   AUTHOR is the authenticated name of the user who changed the prop.
   NAME is the name of the property that was changed, and OLD_VALUE is
   that property's value immediately before the change, or null if
   none.  ACTION indicates if the property was 'A'dded, 'M'odified,
   or 'D'eleted.

   The old value will be passed to the post-revprop hook on stdin.  If
   the property is being created, no data will be written. */
svn_error_t *
svn_repos__hooks_post_revprop_change(svn_repos_t *repos,
                                     svn_revnum_t rev,
                                     const char *author,
                                     const char *name,
                                     svn_string_t *old_value,
                                     char action,
                                     apr_pool_t *pool);

/* Run the pre-lock hook for REPOS.  Use POOL for any temporary
   allocations.  If the hook fails, return SVN_ERR_REPOS_HOOK_FAILURE.  

   PATH is the path being locked, USERNAME is the person doing it.  */
svn_error_t *
svn_repos__hooks_pre_lock(svn_repos_t *repos,
                          const char *path,
                          const char *username,
                          apr_pool_t *pool);

/* Run the post-lock hook for REPOS.  Use POOL for any temporary
   allocations.  If the hook fails, return SVN_ERR_REPOS_HOOK_FAILURE.  

   PATHS is an array of paths being locked, USERNAME is the person
   who did it.  */
svn_error_t *
svn_repos__hooks_post_lock(svn_repos_t *repos,
                           apr_array_header_t *paths,
                           const char *username,
                           apr_pool_t *pool);

/* Run the pre-unlock hook for REPOS.  Use POOL for any temporary
   allocations.  If the hook fails, return SVN_ERR_REPOS_HOOK_FAILURE.  
   
   PATH is the path being unlocked, USERNAME is the person doing it.  */
svn_error_t *
svn_repos__hooks_pre_unlock(svn_repos_t *repos,
                            const char *path,
                            const char *username,
                            apr_pool_t *pool);

/* Run the post-unlock hook for REPOS.  Use POOL for any temporary
   allocations.  If the hook fails, return SVN_ERR_REPOS_HOOK_FAILURE.  
   
   PATHS is an array of paths being unlocked, USERNAME is the person
   who did it.  */
svn_error_t *
svn_repos__hooks_post_unlock(svn_repos_t *repos,
                             apr_array_header_t *paths,
                             const char *username,
                             apr_pool_t *pool);


/*** Utility Functions ***/

/* Set *CHANGED_P to TRUE if ROOT1/PATH1 and ROOT2/PATH2 have
   different contents, FALSE if they have the same contents.
   Use POOL for temporary allocation. */
svn_error_t *
svn_repos__compare_files(svn_boolean_t *changed_p,
                         svn_fs_root_t *root1,
                         const char *path1,
                         svn_fs_root_t *root2,
                         const char *path2,
                         apr_pool_t *pool);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_LIBSVN_REPOS_H */
