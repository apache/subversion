/* repos.c : repository creation; shared and exclusive repository locking
 *
 * ====================================================================
 * Copyright (c) 2000-2003 CollabNet.  All rights reserved.
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
#include <apr_file_io.h>

#include "svn_pools.h"
#include "svn_error.h"
#include "svn_path.h"
#include "svn_fs.h"
#include "svn_repos.h"
#include "svn_config.h"
#include "svn_private_config.h" /* for SVN_TEMPLATE_ROOT_DIR */

#include "repos.h"


/* When creating the on-disk structure for a repository, we will look for
   a builtin template of this name.  */
#define DEFAULT_TEMPLATE_NAME "default"



/* Path accessor functions. */


const char *
svn_repos_path (svn_repos_t *repos, apr_pool_t *pool)
{
  return apr_pstrdup (pool, repos->path);
}


const char *
svn_repos_db_env (svn_repos_t *repos, apr_pool_t *pool)
{
  return apr_pstrdup (pool, repos->db_path);
}


const char *
svn_repos_lock_dir (svn_repos_t *repos, apr_pool_t *pool)
{
  return apr_pstrdup (pool, repos->lock_path);
}


const char *
svn_repos_db_lockfile (svn_repos_t *repos, apr_pool_t *pool)
{
  return svn_path_join (repos->lock_path, SVN_REPOS__DB_LOCKFILE, pool);
}


const char *
svn_repos_hook_dir (svn_repos_t *repos, apr_pool_t *pool)
{
  return apr_pstrdup (pool, repos->hook_path);
}


const char *
svn_repos_start_commit_hook (svn_repos_t *repos, apr_pool_t *pool)
{
  return svn_path_join (repos->hook_path, SVN_REPOS__HOOK_START_COMMIT, pool);
}


const char *
svn_repos_pre_commit_hook (svn_repos_t *repos, apr_pool_t *pool)
{
  return svn_path_join (repos->hook_path, SVN_REPOS__HOOK_PRE_COMMIT, pool);
}


const char *
svn_repos_post_commit_hook (svn_repos_t *repos, apr_pool_t *pool)
{
  return svn_path_join (repos->hook_path, SVN_REPOS__HOOK_POST_COMMIT, pool);
}


const char *
svn_repos_pre_revprop_change_hook (svn_repos_t *repos, apr_pool_t *pool)
{
  return svn_path_join (repos->hook_path, SVN_REPOS__HOOK_PRE_REVPROP_CHANGE,
                        pool);
}


const char *
svn_repos_post_revprop_change_hook (svn_repos_t *repos, apr_pool_t *pool)
{
  return svn_path_join (repos->hook_path, SVN_REPOS__HOOK_POST_REVPROP_CHANGE,
                        pool);
}


static svn_error_t *
create_repos_dir (const char *path, apr_pool_t *pool)
{
  svn_error_t *err;

  err = svn_io_dir_make (path, APR_OS_DEFAULT, pool);
  if (err && (APR_STATUS_IS_EEXIST (err->apr_err)))
    {
      svn_boolean_t is_empty;

      svn_error_clear (err);

      SVN_ERR (svn_io_dir_empty (&is_empty, path, pool));

      if (is_empty)
        err = NULL;
      else
        err = svn_error_createf (SVN_ERR_DIR_NOT_EMPTY, 0,
                                 "'%s' exists and is non-empty",
                                 path);
    }

  return err;
}


static svn_error_t *
create_locks (svn_repos_t *repos, const char *path, apr_pool_t *pool)
{
  apr_status_t apr_err;

  /* Create the locks directory. */
  SVN_ERR_W (create_repos_dir (path, pool),
             "creating lock dir");

  /* Create the DB lockfile under that directory. */
  {
    apr_file_t *f = NULL;
    apr_size_t written;
    const char *contents;
    const char *lockfile_path;

    lockfile_path = svn_repos_db_lockfile (repos, pool);
    SVN_ERR_W (svn_io_file_open (&f, lockfile_path,
                                 (APR_WRITE | APR_CREATE | APR_EXCL),
                                 APR_OS_DEFAULT,
                                 pool),
               "creating lock file");
    
    contents = 
      "DB lock file, representing locks on the versioned filesystem.\n"
      "\n"
      "All accessors -- both readers and writers -- of the repository's\n"
      "Berkeley DB environment take out shared locks on this file, and\n"
      "each accessor removes its lock when done.  If and when the DB\n"
      "recovery procedure is run, the recovery code takes out an\n"
      "exclusive lock on this file, so we can be sure no one else is\n"
      "using the DB during the recovery.\n"
      "\n"
      "You should never have to edit or remove this file.\n";
    
    apr_err = apr_file_write_full (f, contents, strlen (contents), &written);
    if (apr_err)
      return svn_error_createf (apr_err, NULL,
                                "writing lock file '%s'", lockfile_path);
    
    apr_err = apr_file_close (f);
    if (apr_err)
      return svn_error_createf (apr_err, NULL,
                                "closing lock file '%s'", lockfile_path);
  }

  return SVN_NO_ERROR;
}


static svn_error_t *
create_hooks (svn_repos_t *repos, const char *path, apr_pool_t *pool)
{
  const char *this_path, *contents;
  apr_status_t apr_err;
  apr_file_t *f;
  apr_size_t written;

  /* Create the hook directory. */
  SVN_ERR_W (create_repos_dir (path, pool),
             "creating hook directory");

  /*** Write a default template for each standard hook file. */

  /* Start-commit hook. */
  {
    this_path = apr_psprintf (pool, "%s%s",
                              svn_repos_start_commit_hook (repos, pool),
                              SVN_REPOS__HOOK_DESC_EXT);
    
    SVN_ERR_W (svn_io_file_open (&f, this_path,
                                 (APR_WRITE | APR_CREATE | APR_EXCL),
                                 APR_OS_DEFAULT,
                                 pool),
               "creating hook file");
    
    contents = 
      "#!/bin/sh"
      APR_EOL_STR
      APR_EOL_STR
      "# START-COMMIT HOOK"
      APR_EOL_STR
      "#"
      APR_EOL_STR
      "# The start-commit hook is invoked before a Subversion txn is created"
      APR_EOL_STR
      "# in the process of doing a commit.  Subversion runs this hook"
      APR_EOL_STR
      "# by invoking a program (script, executable, binary, etc.) named"
      APR_EOL_STR
      "# '" 
      SVN_REPOS__HOOK_START_COMMIT
      "' (for which this file is a template)"
      APR_EOL_STR
      "# with the following ordered arguments:"
      APR_EOL_STR
      "#"
      APR_EOL_STR
      "#   [1] REPOS-PATH   (the path to this repository)"
      APR_EOL_STR
      "#   [2] USER         (the authenticated user attempting to commit)"
      APR_EOL_STR
      "#"
      APR_EOL_STR
      "# If the hook program exits with success, the commit continues; but"
      APR_EOL_STR
      "# if it exits with failure (non-zero), the commit is stopped before"
      APR_EOL_STR
      "# even a Subversion txn is created."
      APR_EOL_STR
      "#"
      APR_EOL_STR
      "# On a Unix system, the normal procedure is to have "
      "'"
      SVN_REPOS__HOOK_START_COMMIT
      "'" 
      APR_EOL_STR
      "# invoke other programs to do the real work, though it may do the"
      APR_EOL_STR
      "# work itself too."
      APR_EOL_STR
      "#"
      APR_EOL_STR
      "# Note that"
      " '" SVN_REPOS__HOOK_START_COMMIT "' "
      "must be executable by the user(s) who will"
      APR_EOL_STR
      "# invoke it (typically the user httpd runs as), and that user must"
      APR_EOL_STR
      "# have filesystem-level permission to access the repository."
      APR_EOL_STR
      "#"
      APR_EOL_STR
      "# On a Windows system, you should name the hook program"
      APR_EOL_STR
      "# '" SVN_REPOS__HOOK_START_COMMIT ".bat' or "
      "'" SVN_REPOS__HOOK_START_COMMIT ".exe',"
      APR_EOL_STR
      "# but the basic idea is the same."
      APR_EOL_STR
      "# "
      APR_EOL_STR
      "# Here is an example hook script, for a Unix /bin/sh interpreter:"
      APR_EOL_STR
      APR_EOL_STR
      "REPOS=\"$1\""
      APR_EOL_STR
      "USER=\"$2\""
      APR_EOL_STR
      APR_EOL_STR
      "commit-allower.pl --repository \"$REPOS\" --user \"$USER\" || exit 1"
      APR_EOL_STR
      "special-auth-check.py --user \"$USER\" --auth-level 3 || exit 1"
      APR_EOL_STR
      APR_EOL_STR
      "# All checks passed, so allow the commit."
      APR_EOL_STR
      "exit 0"
      APR_EOL_STR;

    apr_err = apr_file_write_full (f, contents, strlen (contents), &written);
    if (apr_err)
      return svn_error_createf (apr_err, NULL,
                                "writing hook file '%s'", this_path);

    apr_err = apr_file_close (f);
    if (apr_err)
      return svn_error_createf (apr_err, NULL,
                                "closing hook file '%s'", this_path);
  }  /* end start-commit hook */

  /* Pre-commit hook. */
  {
    this_path = apr_psprintf (pool, "%s%s",
                              svn_repos_pre_commit_hook (repos, pool),
                              SVN_REPOS__HOOK_DESC_EXT);

    SVN_ERR_W (svn_io_file_open (&f, this_path,
                                 (APR_WRITE | APR_CREATE | APR_EXCL),
                                 APR_OS_DEFAULT,
                                 pool),
               "creating hook file");

    contents =
      "#!/bin/sh"
      APR_EOL_STR
      APR_EOL_STR
      "# PRE-COMMIT HOOK"
      APR_EOL_STR
      "#"
      APR_EOL_STR
      "# The pre-commit hook is invoked before a Subversion txn is"
      APR_EOL_STR
      "# committed.  Subversion runs this hook by invoking a program"
      APR_EOL_STR
      "# (script, executable, binary, etc.) named "
      "'" 
      SVN_REPOS__HOOK_PRE_COMMIT "' (for which"
      APR_EOL_STR
      "# this file is a template), with the following ordered arguments:"
      APR_EOL_STR
      "#"
      APR_EOL_STR
      "#   [1] REPOS-PATH   (the path to this repository)"
      APR_EOL_STR
      "#   [2] TXN-NAME     (the name of the txn about to be committed)"
      APR_EOL_STR
      "#"
      APR_EOL_STR
      "# If the hook program exits with success, the txn is committed; but"
      APR_EOL_STR
      "# if it exits with failure (non-zero), the txn is aborted and no"
      APR_EOL_STR
      "# commit takes place.  The hook program can use the 'svnlook'"
      APR_EOL_STR
      "# utility to help it examine the txn."
      APR_EOL_STR
      "#"
      APR_EOL_STR
      "# On a Unix system, the normal procedure is to have "
      "'"
      SVN_REPOS__HOOK_PRE_COMMIT
      "'" 
      APR_EOL_STR
      "# invoke other programs to do the real work, though it may do the"
      APR_EOL_STR
      "# work itself too."
      APR_EOL_STR
      "#"
      APR_EOL_STR
      "#   ***   NOTE: THE HOOK PROGRAM MUST NOT MODIFY THE TXN.    ***"
      APR_EOL_STR
      "#   This is why we recommend using the read-only 'svnlook' utility."
      APR_EOL_STR
      "#   In the future, Subversion may enforce the rule that pre-commit"
      APR_EOL_STR
      "#   hooks should not modify txns, or else come up with a mechanism"
      APR_EOL_STR
      "#   to make it safe to do so (by informing the committing client of"
      APR_EOL_STR
      "#   the changes).  However, right now neither mechanism is"
      APR_EOL_STR
      "#   implemented, so hook writers just have to be careful."
      APR_EOL_STR
      "#"
      APR_EOL_STR
      "# Note that"
      " '" SVN_REPOS__HOOK_PRE_COMMIT "' "
      "must be executable by the user(s) who will"
      APR_EOL_STR
      "# invoke it (typically the user httpd runs as), and that user must"
      APR_EOL_STR
      "# have filesystem-level permission to access the repository."
      APR_EOL_STR
      "#"
      APR_EOL_STR
      "# On a Windows system, you should name the hook program"
      APR_EOL_STR
      "# '" SVN_REPOS__HOOK_PRE_COMMIT ".bat' or "
      "'" SVN_REPOS__HOOK_PRE_COMMIT ".exe',"
      APR_EOL_STR
      "# but the basic idea is the same."
      APR_EOL_STR
      "#"
      APR_EOL_STR
      "# Here is an example hook script, for a Unix /bin/sh interpreter:"
      APR_EOL_STR
      APR_EOL_STR
      "REPOS=\"$1\""
      APR_EOL_STR
      "TXN=\"$2\""
      APR_EOL_STR
      APR_EOL_STR
      "# Make sure that the log message contains some text."
      APR_EOL_STR
      "SVNLOOK=/usr/local/bin/svnlook"
      APR_EOL_STR
      "LOG=`$SVNLOOK log -t \"$TXN\" \"$REPOS\"`"
      APR_EOL_STR
      "echo \"$LOG\" | grep \"[a-zA-Z0-9]\" > /dev/null || exit 1"
      APR_EOL_STR
      APR_EOL_STR
      "# Check that the author of this commit has the rights to perform"
      APR_EOL_STR
      "# the commit on the files and directories being modified."
      APR_EOL_STR
      "commit-access-control.pl \"$REPOS\" \"$TXN\" commit-access-control.cfg "
      "|| exit 1"
      APR_EOL_STR
      APR_EOL_STR
      "# All checks passed, so allow the commit."
      APR_EOL_STR
      "exit 0"
      APR_EOL_STR;
    
    apr_err = apr_file_write_full (f, contents, strlen (contents), &written);
    if (apr_err)
      return svn_error_createf (apr_err, NULL,
                                "writing hook file '%s'", this_path);

    apr_err = apr_file_close (f);
    if (apr_err)
      return svn_error_createf (apr_err, NULL,
                                "closing hook file '%s'", this_path);
  }  /* end pre-commit hook */


  /* Pre-revprop-change hook. */
  {
    this_path = apr_psprintf (pool, "%s%s",
                              svn_repos_pre_revprop_change_hook (repos, pool),
                              SVN_REPOS__HOOK_DESC_EXT);

    SVN_ERR_W (svn_io_file_open (&f, this_path,
                                 (APR_WRITE | APR_CREATE | APR_EXCL),
                                 APR_OS_DEFAULT,
                                 pool),
               "creating hook file");

    contents =
      "#!/bin/sh"
      APR_EOL_STR
      APR_EOL_STR
      "# PRE-REVPROP-CHANGE HOOK"
      APR_EOL_STR
      "#"
      APR_EOL_STR
      "# The pre-revprop-change hook is invoked before a revision property"
      APR_EOL_STR
      "# is modified.  Subversion runs this hook by invoking a program"
      APR_EOL_STR
      "# (script, executable, binary, etc.) named "
      "'" 
      SVN_REPOS__HOOK_PRE_REVPROP_CHANGE "' (for which"
      APR_EOL_STR
      "# this file is a template), with the following ordered arguments:"
      APR_EOL_STR
      "#"
      APR_EOL_STR
      "#   [1] REPOS-PATH   (the path to this repository)"
      APR_EOL_STR
      "#   [2] REVISION     (the revision being tweaked)"
      APR_EOL_STR
      "#   [3] USER         (the username of the person tweaking the property)"
      APR_EOL_STR
      "#   [4] PROPNAME     (the property being set on the revision)"
      APR_EOL_STR
      "#"
      APR_EOL_STR
      "#   [STDIN] PROPVAL  ** the property value is passed via STDIN."
      APR_EOL_STR
      "#"
      APR_EOL_STR
      "# If the hook program exits with success, the propchange happens; but"
      APR_EOL_STR
      "# if it exits with failure (non-zero), the propchange doesn't happen."
      APR_EOL_STR
      "# The hook program can use the 'svnlook' utility to examine the "
      APR_EOL_STR
      "# existing value of the revision property."
      APR_EOL_STR
      "#"
      APR_EOL_STR
      "# WARNING: unlike other hooks, this hook MUST exist for revision"
      APR_EOL_STR
      "# properties to be changed.  If the hook does not exist, Subversion "
      APR_EOL_STR
      "# will behave as if the hook were present, but failed.  The reason"
      APR_EOL_STR
      "# for this is that revision properties are UNVERSIONED, meaning that"
      APR_EOL_STR
      "# a successful propchange is destructive;  the old value is gone"
      APR_EOL_STR
      "# forever.  We recommend the hook back up the old value somewhere."
      APR_EOL_STR
      "#"      
      APR_EOL_STR
      "# On a Unix system, the normal procedure is to have "
      "'"
      SVN_REPOS__HOOK_PRE_REVPROP_CHANGE
      "'" 
      APR_EOL_STR
      "# invoke other programs to do the real work, though it may do the"
      APR_EOL_STR
      "# work itself too."
      APR_EOL_STR
      "#"
      APR_EOL_STR
      "# Note that"
      " '" SVN_REPOS__HOOK_PRE_REVPROP_CHANGE "' "
      "must be executable by the user(s) who will"
      APR_EOL_STR
      "# invoke it (typically the user httpd runs as), and that user must"
      APR_EOL_STR
      "# have filesystem-level permission to access the repository."
      APR_EOL_STR
      "#"
      APR_EOL_STR
      "# On a Windows system, you should name the hook program"
      APR_EOL_STR
      "# '" SVN_REPOS__HOOK_PRE_REVPROP_CHANGE ".bat' or "
      "'" SVN_REPOS__HOOK_PRE_REVPROP_CHANGE ".exe',"
      APR_EOL_STR
      "# but the basic idea is the same."
      APR_EOL_STR
      "#"
      APR_EOL_STR
      "# Here is an example hook script, for a Unix /bin/sh interpreter:"
      APR_EOL_STR
      APR_EOL_STR
      "REPOS=\"$1\""
      APR_EOL_STR
      "REV=\"$2\""
      APR_EOL_STR
      "USER=\"$3\""
      APR_EOL_STR
      "PROPNAME=\"$4\""
      APR_EOL_STR
      APR_EOL_STR
      "if [ \"$PROPNAME\" = \"svn:log\" ]; then exit 0; fi"
      APR_EOL_STR
      "exit 1"
      APR_EOL_STR;
    
    apr_err = apr_file_write_full (f, contents, strlen (contents), &written);
    if (apr_err)
      return svn_error_createf (apr_err, NULL,
                                "writing hook file '%s'", this_path);

    apr_err = apr_file_close (f);
    if (apr_err)
      return svn_error_createf (apr_err, NULL,
                                "closing hook file '%s'", this_path);
  }  /* end pre-revprop-change hook */


  /* Post-commit hook. */
  {
    this_path = apr_psprintf (pool, "%s%s",
                              svn_repos_post_commit_hook (repos, pool),
                              SVN_REPOS__HOOK_DESC_EXT);

    SVN_ERR_W (svn_io_file_open (&f, this_path,
                                 (APR_WRITE | APR_CREATE | APR_EXCL),
                                 APR_OS_DEFAULT,
                                 pool),
               "creating hook file");
    
    contents =
      "#!/bin/sh"
      APR_EOL_STR
      APR_EOL_STR
      "# POST-COMMIT HOOK"
      APR_EOL_STR
      "#"
      APR_EOL_STR
      "# The post-commit hook is invoked after a commit. Subversion runs"
      APR_EOL_STR
      "# this hook by invoking a program (script, executable, binary,"
      APR_EOL_STR
      "# etc.) named '" 
      SVN_REPOS__HOOK_POST_COMMIT 
      "' (for which"
      APR_EOL_STR
      "# this file is a template) with the following ordered arguments:"
      APR_EOL_STR
      "#"
      APR_EOL_STR
      "#   [1] REPOS-PATH   (the path to this repository)"
      APR_EOL_STR
      "#   [2] REV          (the number of the revision just committed)"
      APR_EOL_STR
      "#"
      APR_EOL_STR
      "# Because the commit has already completed and cannot be undone,"
      APR_EOL_STR
      "# the exit code of the hook program is ignored.  The hook program"
      APR_EOL_STR
      "# can use the 'svnlook' utility to help it examine the"
      APR_EOL_STR
      "# newly-committed tree."
      APR_EOL_STR
      "#"
      APR_EOL_STR
      "# On a Unix system, the normal procedure is to have "
      "'"
      SVN_REPOS__HOOK_POST_COMMIT
      "'" 
      APR_EOL_STR
      "# invoke other programs to do the real work, though it may do the"
      APR_EOL_STR
      "# work itself too."
      APR_EOL_STR
      "#"
      APR_EOL_STR
      "# Note that"
      " '" SVN_REPOS__HOOK_POST_COMMIT "' "
      "must be executable by the user(s) who will"
      APR_EOL_STR
      "# invoke it (typically the user httpd runs as), and that user must"
      APR_EOL_STR
      "# have filesystem-level permission to access the repository."
      APR_EOL_STR
      "#"
      APR_EOL_STR
      "# On a Windows system, you should name the hook program"
      APR_EOL_STR
      "# '" SVN_REPOS__HOOK_POST_COMMIT ".bat' or "
      "'" SVN_REPOS__HOOK_POST_COMMIT ".exe',"
      APR_EOL_STR
      "# but the basic idea is the same."
      APR_EOL_STR
      "# "
      APR_EOL_STR
      "# Here is an example hook script, for a Unix /bin/sh interpreter:"
      APR_EOL_STR
      APR_EOL_STR
      "REPOS=\"$1\""
      APR_EOL_STR
      "REV=\"$2\""
      APR_EOL_STR
      APR_EOL_STR
      "commit-email.pl \"$REPOS\" \"$REV\" commit-watchers@example.org"
      APR_EOL_STR
      "log-commit.py --repository \"$REPOS\" --revision \"$REV\""
      APR_EOL_STR;

    apr_err = apr_file_write_full (f, contents, strlen (contents), &written);
    if (apr_err)
      return svn_error_createf (apr_err, NULL,
                                "writing hook file '%s'", this_path);

    apr_err = apr_file_close (f);
    if (apr_err)
      return svn_error_createf (apr_err, NULL,
                                "closing hook file '%s'", this_path);
  } /* end post-commit hook */


  /* Post-revprop-change hook. */
  {
    this_path = apr_psprintf (pool, "%s%s",
                              svn_repos_post_revprop_change_hook (repos, pool),
                              SVN_REPOS__HOOK_DESC_EXT);

    SVN_ERR_W (svn_io_file_open (&f, this_path,
                                 (APR_WRITE | APR_CREATE | APR_EXCL),
                                 APR_OS_DEFAULT,
                                 pool),
               "creating hook file");
    
    contents =
      "#!/bin/sh"
      APR_EOL_STR
      APR_EOL_STR
      "# POST-REVPROP-CHANGE HOOK"
      APR_EOL_STR
      "#"
      APR_EOL_STR
      "# The post-revprop-change hook is invoked after a revision property"
      APR_EOL_STR
      "# has been changed. Subversion runs this hook by invoking a program"
      APR_EOL_STR
      "# (script, executable, binary, etc.) named '"
      SVN_REPOS__HOOK_POST_REVPROP_CHANGE 
      "'"
      APR_EOL_STR
      "# (for which this file is a template), with the following ordered"
      APR_EOL_STR
      "# arguments:"
      APR_EOL_STR
      "#"
      APR_EOL_STR
      "#   [1] REPOS-PATH   (the path to this repository)"
      APR_EOL_STR
      "#   [2] REV          (the revision that was tweaked)"
      APR_EOL_STR
      "#   [3] USER         (the username of the person tweaking the property)"
      APR_EOL_STR
      "#   [4] PROPNAME     (the property that was changed)"
      APR_EOL_STR
      "#"
      APR_EOL_STR
      "# Because the propchange has already completed and cannot be undone,"
      APR_EOL_STR
      "# the exit code of the hook program is ignored.  The hook program"
      APR_EOL_STR
      "# can use the 'svnlook' utility to help it examine the"
      APR_EOL_STR
      "# new property value."
      APR_EOL_STR
      "#"
      APR_EOL_STR
      "# On a Unix system, the normal procedure is to have "
      "'"
      SVN_REPOS__HOOK_POST_REVPROP_CHANGE
      "'" 
      APR_EOL_STR
      "# invoke other programs to do the real work, though it may do the"
      APR_EOL_STR
      "# work itself too."
      APR_EOL_STR
      "#"
      APR_EOL_STR
      "# Note that"
      " '" SVN_REPOS__HOOK_POST_REVPROP_CHANGE "' "
      "must be executable by the user(s) who will"
      APR_EOL_STR
      "# invoke it (typically the user httpd runs as), and that user must"
      APR_EOL_STR
      "# have filesystem-level permission to access the repository."
      APR_EOL_STR
      "#"
      APR_EOL_STR
      "# On a Windows system, you should name the hook program"
      APR_EOL_STR
      "# '" SVN_REPOS__HOOK_POST_REVPROP_CHANGE ".bat' or "
      "'" SVN_REPOS__HOOK_POST_REVPROP_CHANGE ".exe',"
      APR_EOL_STR
      "# but the basic idea is the same."
      APR_EOL_STR
      "# "
      APR_EOL_STR
      "# Here is an example hook script, for a Unix /bin/sh interpreter:"
      APR_EOL_STR
      APR_EOL_STR
      "REPOS=\"$1\""
      APR_EOL_STR
      "REV=\"$2\""
      APR_EOL_STR
      "USER=\"$3\""
      APR_EOL_STR
      "PROPNAME=\"$4\""
      APR_EOL_STR
      APR_EOL_STR
      "propchange-email.pl \"$REPOS\" \"$REV\" \"$USER\" \"$PROPNAME\" watchers@example.org"
      APR_EOL_STR;

    apr_err = apr_file_write_full (f, contents, strlen (contents), &written);
    if (apr_err)
      return svn_error_createf (apr_err, NULL,
                                "writing hook file '%s'", this_path);

    apr_err = apr_file_close (f);
    if (apr_err)
      return svn_error_createf (apr_err, NULL,
                                "closing hook file '%s'", this_path);
  } /* end post-revprop-change hook */

  return SVN_NO_ERROR;
}


/* This code manages repository locking, which is motivated by the
 * need to support DB_RUN_RECOVERY.  Here's how it works:
 *
 * Every accessor of a repository's database takes out a shared lock
 * on the repository -- both readers and writers get shared locks, and
 * there can be an unlimited number of shared locks simultaneously.
 *
 * Sometimes, a db access returns the error DB_RUN_RECOVERY.  When
 * this happens, we need to run svn_fs_berkeley_recover() on the db
 * with no other accessors present.  So we take out an exclusive lock
 * on the repository.  From the moment we request the exclusive lock,
 * no more shared locks are granted, and when the last shared lock
 * disappears, the exclusive lock is granted.  As soon as we get it,
 * we can run recovery.
 *
 * We assume that once any berkeley call returns DB_RUN_RECOVERY, they
 * all do, until recovery is run.
 */

/* Clear all outstanding locks on ARG, an open apr_file_t *. */
static apr_status_t
clear_and_close (void *arg)
{
  apr_status_t apr_err;
  apr_file_t *f = arg;

  /* Remove locks. */
  apr_err = apr_file_unlock (f);
  if (apr_err)
    return apr_err;

  /* Close the file. */
  apr_err = apr_file_close (f);
  if (apr_err)
    return apr_err;

  return 0;
}


static void
init_repos_dirs (svn_repos_t *repos, const char *path, apr_pool_t *pool)
{
  repos->path = apr_pstrdup (pool, path);
  repos->db_path = svn_path_join (path, SVN_REPOS__DB_DIR, pool);
  repos->dav_path = svn_path_join (path, SVN_REPOS__DAV_DIR, pool);
  repos->hook_path = svn_path_join (path, SVN_REPOS__HOOK_DIR, pool);
  repos->lock_path = svn_path_join (path, SVN_REPOS__LOCK_DIR, pool);
}


static svn_error_t *
create_repos_structure (svn_repos_t *repos,
                        const char *path,
                        apr_pool_t *pool)
{
  /* Create the top-level repository directory. */
  SVN_ERR_W (create_repos_dir (path, pool),
             "could not create top-level directory");

  /* Create the DAV sandbox directory.  */
  SVN_ERR_W (create_repos_dir (repos->dav_path, pool),
             "creating DAV sandbox dir");

  /* Create the lock directory.  */
  SVN_ERR (create_locks (repos, repos->lock_path, pool));

  /* Create the hooks directory.  */
  SVN_ERR (create_hooks (repos, repos->hook_path, pool));

  /* Write the top-level README file. */
  {
    apr_status_t apr_err;
    apr_file_t *readme_file = NULL;
    const char *readme_file_name 
      = svn_path_join (path, SVN_REPOS__README, pool);
    static const char * const readme_contents =
      "This is a Subversion repository; use the 'svnadmin' tool to examine"
      APR_EOL_STR
      "it.  Do not add, delete, or modify files here unless you know how"
      APR_EOL_STR
      "to avoid corrupting the repository."
      APR_EOL_STR
      APR_EOL_STR
      "The directory \""
      SVN_REPOS__DB_DIR
      "\" contains a Berkeley DB environment."
      APR_EOL_STR
      "You may need to tweak the values in \""
      SVN_REPOS__DB_DIR
      "/DB_CONFIG\" to match the"
      APR_EOL_STR
      "requirements of your site."
      APR_EOL_STR
      APR_EOL_STR
      "Visit http://subversion.tigris.org/ for more information."
      APR_EOL_STR;

    SVN_ERR (svn_io_file_open (&readme_file, readme_file_name,
                               APR_WRITE | APR_CREATE, APR_OS_DEFAULT,
                               pool));

    apr_err = apr_file_write_full (readme_file, readme_contents,
                                   strlen (readme_contents), NULL);
    if (apr_err)
      return svn_error_createf (apr_err, 0,
                                "writing to '%s'", readme_file_name);
    
    apr_err = apr_file_close (readme_file);
    if (apr_err)
      return svn_error_createf (apr_err, 0,
                                "closing '%s'", readme_file_name);
  }

  /* Write the top-level FORMAT file. */
  SVN_ERR (svn_io_write_version_file 
           (svn_path_join (path, SVN_REPOS__FORMAT, pool),
            SVN_REPOS__VERSION, pool));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_repos_create (svn_repos_t **repos_p,
                  const char *path,
                  const char *unused_1,
                  const char *unused_2,
                  apr_hash_t *config,
                  apr_hash_t *fs_config,
                  apr_pool_t *pool)
{
  svn_repos_t *repos;

  /* Allocate a repository object. */
  repos = apr_pcalloc (pool, sizeof (*repos));

  /* Initialize the repository paths. */
  init_repos_dirs (repos, path, pool);

  /* Create the various files and subdirectories for the repository. */
  SVN_ERR_W (create_repos_structure (repos, path, pool),
             "repository creation failed");
  
  /* Initialize the filesystem object. */
  repos->fs = svn_fs_new (fs_config, pool);

  /* Create a Berkeley DB environment for the filesystem. */
  SVN_ERR (svn_fs_create_berkeley (repos->fs, repos->db_path));

  *repos_p = repos;
  return SVN_NO_ERROR;
}


/* Check if @a path is the root of a repository by checking if the
 * path contains the expected files and directories.  Return TRUE
 * on errors (which would be permission errors, probably) so that
 * we the user will see them after we try to open the repository
 * for real.  */
static svn_boolean_t
check_repos_path (const char *path,
                  apr_pool_t *pool)
{
  svn_node_kind_t kind;
  svn_error_t *err;

  err = svn_io_check_path (svn_path_join (path, SVN_REPOS__FORMAT, pool),
                           &kind, pool);
  if (err)
    {
      svn_error_clear (err);
      return TRUE;
    }
  if (kind != svn_node_file)
    return FALSE;

  err = svn_io_check_path (svn_path_join (path, SVN_REPOS__DB_DIR, pool),
                           &kind, pool);
  if (err)
    {
      svn_error_clear (err);
      return TRUE;
    }
  if (kind != svn_node_dir)
    return FALSE;

  return TRUE;
}


/* Verify that the repository's 'format' file is a suitable version. */
static svn_error_t *
check_repos_version (const char *path,
                     apr_pool_t *pool)
{
  int version;
  svn_error_t *err;

  err = svn_io_read_version_file 
    (&version, svn_path_join (path, SVN_REPOS__FORMAT, pool), pool);
  if (err)
    return svn_error_createf 
      (SVN_ERR_REPOS_UNSUPPORTED_VERSION, err,
       "Expected version '%d' of repository; found no version at all; "
       "is '%s' a valid repository path?",
       SVN_REPOS__VERSION, path);

  if (version != SVN_REPOS__VERSION)
    return svn_error_createf 
      (SVN_ERR_REPOS_UNSUPPORTED_VERSION, NULL,
       "Expected version '%d' of repository; found version '%d'", 
       SVN_REPOS__VERSION, version);

  return SVN_NO_ERROR;
}


/* Set *REPOS_P to a repository at PATH which has been opened with
   some kind of lock.  LOCKTYPE is one of APR_FLOCK_SHARED (for
   standard readers/writers), or APR_FLOCK_EXCLUSIVE (for processes
   that need exclusive access, like db_recover.)  OPEN_FS indicates
   whether the database should be opened and placed into repos->fs.

   Do all allocation in POOL.  When POOL is destroyed, the lock will
   be released as well. */
static svn_error_t *
get_repos (svn_repos_t **repos_p,
           const char *path,
           int locktype,
           svn_boolean_t open_fs,
           apr_pool_t *pool)
{
  apr_status_t apr_err;
  svn_repos_t *repos;

  /* Verify the validity of our repository format. */
  SVN_ERR (check_repos_version (path, pool));

  /* Allocate a repository object. */
  repos = apr_pcalloc (pool, sizeof (*repos));

  /* Initialize the repository paths. */
  init_repos_dirs (repos, path, pool);

  /* Initialize the filesystem object. */
  repos->fs = svn_fs_new (NULL, pool);


  /* Locking. */
  {
    const char *lockfile_path;
    apr_file_t *lockfile_handle;
    apr_int32_t flags;

    /* Get a filehandle for the repository's db lockfile. */
    lockfile_path = svn_repos_db_lockfile (repos, pool);
    flags = APR_READ;
    if (locktype == APR_FLOCK_EXCLUSIVE)
      flags |= APR_WRITE;
    SVN_ERR_W (svn_io_file_open (&lockfile_handle, lockfile_path,
                                 flags, APR_OS_DEFAULT, pool),
               "get_repos: error opening db lockfile");
    
    /* Get some kind of lock on the filehandle. */
    apr_err = apr_file_lock (lockfile_handle, locktype);
    if (apr_err)
      {
        const char *lockname = "unknown";
        if (locktype == APR_FLOCK_SHARED)
          lockname = "shared";
        if (locktype == APR_FLOCK_EXCLUSIVE)
          lockname = "exclusive";
        
        return svn_error_createf
          (apr_err, NULL,
           "get_repos: %s db lock on repository '%s' failed",
           lockname, path);
      }
    
    /* Register an unlock function for the lock. */
    apr_pool_cleanup_register (pool, lockfile_handle, clear_and_close,
                               apr_pool_cleanup_null);
  }

  /* Open up the Berkeley filesystem only after obtaining the lock. */
  if (open_fs)
    SVN_ERR (svn_fs_open_berkeley (repos->fs, repos->db_path));

  *repos_p = repos;
  return SVN_NO_ERROR;
}



const char *
svn_repos_find_root_path (const char *path,
                          apr_pool_t *pool)
{
  const char *candidate = path;

  while (1)
    {
      if (check_repos_path (candidate, pool))
        break;
      if (candidate[0] == '\0' || strcmp(candidate, "/") == 0)
        return NULL;
      candidate = svn_path_dirname (candidate, pool);
    }

  return candidate;
}


svn_error_t *
svn_repos_open (svn_repos_t **repos_p,
                const char *path,
                apr_pool_t *pool)
{
  /* Fetch a repository object initialized with a shared read/write
     lock on the database. */

  SVN_ERR (get_repos (repos_p, path,
                      APR_FLOCK_SHARED,
                      TRUE,     /* open the db into repos->fs. */
                      pool));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_repos_delete (const char *path, 
                  apr_pool_t *pool)
{
  const char *db_path = svn_path_join (path, SVN_REPOS__DB_DIR, pool);

  /* Delete the Berkeley environment... */
  SVN_ERR (svn_fs_delete_berkeley (db_path, pool));

  /* ...then blow away everything else.  */
  SVN_ERR (svn_io_remove_dir (path, pool));

  return SVN_NO_ERROR;
}


svn_fs_t *
svn_repos_fs (svn_repos_t *repos)
{
  if (! repos)
    return NULL;
  return repos->fs;
}


svn_error_t *
svn_repos_recover (const char *path,
                   apr_pool_t *pool)
{
  svn_repos_t *repos;
  apr_pool_t *subpool = svn_pool_create (pool);

  /* Destroy ALL existing svn locks on the repository.  If we're
     recovering, we need to ensure we have exclusive access.  The
     theory is that the caller *knows* that all existing locks are
     'dead' ones, left by dead processes.  (The caller might be a
     human running 'svnadmin recover', or maybe some future repository
     lock daemon.) */
  {
    const char *lockfile_path;
    apr_file_t *lockfile_handle;
    apr_status_t apr_err;
    svn_repos_t *locked_repos;

    /* We're not calling get_repos to fetch a repository structure,
       because this routine actually tries to open the db environment,
       which would hang.   So we replicate a bit of get_repos's code
       here: */
    SVN_ERR (check_repos_version (path, subpool));

    locked_repos = apr_pcalloc (subpool, sizeof (*locked_repos));
    init_repos_dirs (locked_repos, path, subpool);
    
    /* Get a filehandle for the wedged repository's db lockfile. */
    lockfile_path = svn_repos_db_lockfile (locked_repos, subpool);
    SVN_ERR_W (svn_io_file_open (&lockfile_handle, lockfile_path,
                                 APR_READ, APR_OS_DEFAULT, pool),
               "svn_repos_recover: error opening db lockfile");
    
    apr_err = apr_file_unlock (lockfile_handle);
    if (apr_err && ! APR_STATUS_IS_EACCES(apr_err))
      return svn_error_createf
        (apr_err, NULL,
         "svn_repos_recover: failed to delete all locks on repository '%s'.",
         path);

    apr_err = apr_file_close (lockfile_handle);
    if (apr_err)
      return svn_error_createf
        (apr_err, NULL,
         "svn_repos_recover: failed to close lockfile on repository '%s'.",
         path);
  }
  
  /* Fetch a repository object initialized with an EXCLUSIVE lock on
     the database.   This will at least prevent others from trying to
     read or write to it while we run recovery. */
  SVN_ERR (get_repos (&repos, path,
                      APR_FLOCK_EXCLUSIVE,
                      FALSE,    /* don't try to open the db yet. */
                      subpool));

  /* Recover the database to a consistent state. */
  SVN_ERR (svn_fs_berkeley_recover (repos->db_path, subpool));

  /* Close shop and free the subpool, to release the exclusive lock. */
  svn_pool_destroy (subpool);

  return SVN_NO_ERROR;
}

svn_error_t *svn_repos_db_logfiles (apr_array_header_t **logfiles,
                                    const char *path,
                                    svn_boolean_t only_unused,
                                    apr_pool_t *pool)
{
  svn_repos_t *repos;
  int i;

  SVN_ERR (get_repos (&repos, path,
                      APR_FLOCK_SHARED,
                      FALSE,     /* Do not open fs. */
                      pool));

  SVN_ERR (svn_fs_berkeley_logfiles (logfiles,
                                    svn_repos_db_env (repos, pool),
                                    only_unused,
                                    pool));

  if (logfiles == NULL)
    {
      return SVN_NO_ERROR;
    }

  /* Loop, printing log files. */
  for (i = 0; i < (*logfiles)->nelts; i++)
    {
      const char ** log_file = &(APR_ARRAY_IDX (*logfiles, i, const char *));
      *log_file = svn_path_join(SVN_REPOS__DB_DIR, *log_file, pool);
    }

  return SVN_NO_ERROR;
}
