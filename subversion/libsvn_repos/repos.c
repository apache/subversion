/* repos.c : repository creation; shared and exclusive repository locking
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
svn_repos_conf_dir (svn_repos_t *repos, apr_pool_t *pool)
{
  return apr_pstrdup (pool, repos->conf_path);
}


const char *
svn_repos_svnserve_conf (svn_repos_t *repos, apr_pool_t *pool)
{
  return svn_path_join (repos->conf_path, SVN_REPOS__CONF_SVNSERVE_CONF, pool);
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
svn_repos_db_logs_lockfile (svn_repos_t *repos, apr_pool_t *pool)
{
  return svn_path_join (repos->lock_path, SVN_REPOS__DB_LOGS_LOCKFILE, pool);
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

/* Create the DB logs lockfile. */
static svn_error_t *
create_db_logs_lock (svn_repos_t *repos, apr_pool_t *pool) {
  const char *contents;
  const char *lockfile_path;

  lockfile_path = svn_repos_db_logs_lockfile (repos, pool);
  contents = 
    "DB logs lock file, representing locks on the versioned filesystem logs.\n"
    "\n"
    "All log manipulators of the repository's\n"
    "Berkeley DB environment take out exclusive locks on this file\n"
    "to ensure that only one accessor manupulates the logs at the time.\n"
    "\n"
    "You should never have to edit or remove this file.\n";

  SVN_ERR_W (svn_io_file_create (lockfile_path, contents, pool),
             "Creating db logs lock file");

  return SVN_NO_ERROR;
}

/* Create the DB lockfile. */
static svn_error_t *
create_db_lock (svn_repos_t *repos, apr_pool_t *pool) {
    const char *contents;
    const char *lockfile_path;

    lockfile_path = svn_repos_db_lockfile (repos, pool);
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
    
  SVN_ERR_W (svn_io_file_create (lockfile_path, contents, pool),
             "Creating db lock file");
    
  return SVN_NO_ERROR;
}

static svn_error_t *
create_locks (svn_repos_t *repos, apr_pool_t *pool)
{
  /* Create the locks directory. */
  SVN_ERR_W (create_repos_dir (repos->lock_path, pool),
             "Creating lock dir");

  SVN_ERR (create_db_lock (repos, pool));
  SVN_ERR (create_db_logs_lock (repos, pool));

  return SVN_NO_ERROR;
}


static svn_error_t *
create_hooks (svn_repos_t *repos, apr_pool_t *pool)
{
  const char *this_path, *contents;

  /* Create the hook directory. */
  SVN_ERR_W (create_repos_dir (repos->hook_path, pool),
             "Creating hook directory");

  /*** Write a default template for each standard hook file. */

  /* Start-commit hook. */
  {
    this_path = apr_psprintf (pool, "%s%s",
                              svn_repos_start_commit_hook (repos, pool),
                              SVN_REPOS__HOOK_DESC_EXT);
    
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
      "# The default working directory for the invocation is undefined, so"
      APR_EOL_STR
      "# the program should set one explicitly if it cares."
      APR_EOL_STR
      "#"
      APR_EOL_STR
      "# If the hook program exits with success, the commit continues; but"
      APR_EOL_STR
      "# if it exits with failure (non-zero), the commit is stopped before"
      APR_EOL_STR
      "# a Subversion txn is created, and STDERR is returned to the client."
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

    SVN_ERR_W (svn_io_file_create (this_path, contents, pool),
              "Creating start-commit hook");
  }  /* end start-commit hook */

  /* Pre-commit hook. */
  {
    this_path = apr_psprintf (pool, "%s%s",
                              svn_repos_pre_commit_hook (repos, pool),
                              SVN_REPOS__HOOK_DESC_EXT);

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
      "# The default working directory for the invocation is undefined, so"
      APR_EOL_STR
      "# the program should set one explicitly if it cares."
      APR_EOL_STR
      "#"
      APR_EOL_STR
      "# If the hook program exits with success, the txn is committed; but"
      APR_EOL_STR
      "# if it exits with failure (non-zero), the txn is aborted, no commit"
      APR_EOL_STR
      "# takes place, and STDERR is returned to the client.   The hook"
      APR_EOL_STR
      "# program can use the 'svnlook' utility to help it examine the txn."
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
      "#   ***  NOTE: THE HOOK PROGRAM MUST NOT MODIFY THE TXN, EXCEPT  ***"
      APR_EOL_STR
      "#   ***  FOR REVISION PROPERTIES (like svn:log or svn:author).   ***"
      APR_EOL_STR
      "#"
      APR_EOL_STR
      "#   This is why we recommend using the read-only 'svnlook' utility."
      APR_EOL_STR
      "#   In the future, Subversion may enforce the rule that pre-commit"
      APR_EOL_STR
      "#   hooks should not modify the versioned data in txns, or else come"
      APR_EOL_STR
      "#   up with a mechanism to make it safe to do so (by informing the"
      APR_EOL_STR
      "#   committing client of the changes).  However, right now neither"
      APR_EOL_STR
      "#   mechanism is implemented, so hook writers just have to be careful."
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
      "SVNLOOK=" SVN_BINARY_DIR "/svnlook"
      APR_EOL_STR
      "$SVNLOOK log -t \"$TXN\" \"$REPOS\" | \\"
      APR_EOL_STR
      "   grep \"[a-zA-Z0-9]\" > /dev/null || exit 1"
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
    
    SVN_ERR_W (svn_io_file_create (this_path, contents, pool),
               "Creating pre-commit hook");
  }  /* end pre-commit hook */


  /* Pre-revprop-change hook. */
  {
    this_path = apr_psprintf (pool, "%s%s",
                              svn_repos_pre_revprop_change_hook (repos, pool),
                              SVN_REPOS__HOOK_DESC_EXT);

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
    
    SVN_ERR_W (svn_io_file_create (this_path, contents, pool),
              "Creating pre-revprop-change hook");
  }  /* end pre-revprop-change hook */


  /* Post-commit hook. */
  {
    this_path = apr_psprintf (pool, "%s%s",
                              svn_repos_post_commit_hook (repos, pool),
                              SVN_REPOS__HOOK_DESC_EXT);

    contents =
      "#!/bin/sh"
      APR_EOL_STR
      APR_EOL_STR
      "# POST-COMMIT HOOK"
      APR_EOL_STR
      "#"
      APR_EOL_STR
      "# The post-commit hook is invoked after a commit.  Subversion runs"
      APR_EOL_STR
      "# this hook by invoking a program (script, executable, binary, etc.)"
      APR_EOL_STR
      "# named '"
      SVN_REPOS__HOOK_POST_COMMIT 
      "' (for which this file is a template) with the "
      APR_EOL_STR
      "# following ordered arguments:"
      APR_EOL_STR
      "#"
      APR_EOL_STR
      "#   [1] REPOS-PATH   (the path to this repository)"
      APR_EOL_STR
      "#   [2] REV          (the number of the revision just committed)"
      APR_EOL_STR
      "#"
      APR_EOL_STR
      "# The default working directory for the invocation is undefined, so"
      APR_EOL_STR
      "# the program should set one explicitly if it cares."
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

    SVN_ERR_W (svn_io_file_create (this_path, contents, pool),
               "Creating post-commit hook");
  } /* end post-commit hook */


  /* Post-revprop-change hook. */
  {
    this_path = apr_psprintf (pool, "%s%s",
                              svn_repos_post_revprop_change_hook (repos, pool),
                              SVN_REPOS__HOOK_DESC_EXT);

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
      "propchange-email.pl \"$REPOS\" \"$REV\" \"$USER\" \"$PROPNAME\" "
      "watchers@example.org"
      APR_EOL_STR;

    SVN_ERR_W (svn_io_file_create (this_path, contents, pool),
               "Creating post-revprop-change hook");
  } /* end post-revprop-change hook */

  return SVN_NO_ERROR;
}

static svn_error_t *
create_conf (svn_repos_t *repos, apr_pool_t *pool)
{
  SVN_ERR_W (create_repos_dir (repos->conf_path, pool),
             "Creating conf directory");

  /* Write a default template for svnserve.conf. */
  {
    static const char * const svnserve_conf_contents =
      "### This file controls the configuration of the svnserve daemon, if you"
      APR_EOL_STR
      "### use it to allow access to this repository.  (If you only allow"
      APR_EOL_STR
      "### access through http: and/or file: URLs, then this file is"
      APR_EOL_STR
      "### irrelevant.)"
      APR_EOL_STR
      APR_EOL_STR
      "### Visit http://subversion.tigris.org/ for more information."
      APR_EOL_STR
      APR_EOL_STR
      "# [general]"
      APR_EOL_STR
      "### These options control access to the repository for unauthenticated"
      APR_EOL_STR
      "### and authenticated users.  Valid values are \"write\", \"read\","
      APR_EOL_STR
      "### and \"none\".  The sample settings below are the defaults."
      APR_EOL_STR
      "# anon-access = read"
      APR_EOL_STR
      "# auth-access = write"
      APR_EOL_STR
      "### The password-db option controls the location of the password"
      APR_EOL_STR
      "### database file.  Unless you specify a path starting with a /,"
      APR_EOL_STR
      "### the file's location is relative to the conf directory."
      APR_EOL_STR
      "### The format of the password database is similar to this file."
      APR_EOL_STR
      "### It contains one section labelled [users]. The name and"
      APR_EOL_STR
      "### password for each user follow, one account per line. The"
      APR_EOL_STR
      "### format is"
      APR_EOL_STR
      "###    USERNAME = PASSWORD"
      APR_EOL_STR
      "### Please note that both the user name and password are case"
      APR_EOL_STR
      "### sensitive. There is no default for the password file."
      APR_EOL_STR
      "# password-db = passwd"
      APR_EOL_STR
      "### This option specifies the authentication realm of the repository."
      APR_EOL_STR
      "### If two repositories have the same authentication realm, they should"
      APR_EOL_STR
      "### have the same password database, and vice versa.  The default realm"
      APR_EOL_STR
      "### is repository's uuid."
      APR_EOL_STR
      "# realm = My First Repository"
      APR_EOL_STR;

    SVN_ERR_W (svn_io_file_create (svn_repos_svnserve_conf (repos, pool),
                                   svnserve_conf_contents, pool),
               "Creating svnserve.conf file");
  }

  return SVN_NO_ERROR;
}

static void
init_repos_dirs (svn_repos_t *repos, const char *path, apr_pool_t *pool)
{
  repos->path = apr_pstrdup (pool, path);
  repos->db_path = svn_path_join (path, SVN_REPOS__DB_DIR, pool);
  repos->dav_path = svn_path_join (path, SVN_REPOS__DAV_DIR, pool);
  repos->conf_path = svn_path_join (path, SVN_REPOS__CONF_DIR, pool);
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
             "Could not create top-level directory");

  /* Create the DAV sandbox directory.  */
  SVN_ERR_W (create_repos_dir (repos->dav_path, pool),
             "Creating DAV sandbox dir");

  /* Create the lock directory.  */
  SVN_ERR (create_locks (repos, pool));

  /* Create the hooks directory.  */
  SVN_ERR (create_hooks (repos, pool));

  /* Create the conf directory.  */
  SVN_ERR (create_conf (repos, pool));

  /* Write the top-level README file. */
  {
    const char *readme_file_name 
      = svn_path_join (path, SVN_REPOS__README, pool);
    static const char * const readme_contents =
      "This is a Subversion repository; use the 'svnadmin' tool to examine"
      APR_EOL_STR
      "it.  Do not add, delete, or modify files here unless you know how"
      APR_EOL_STR
      "to avoid corrupting the repository."
      APR_EOL_STR
      /* ### It would be preferable to conditionalize the mention of
         DB_CONFIG below, since it's pointless if this is an FSFS
         repository, but we don't currently have an clear API for
         determining the fs type.  Hence, the conditional is in the
         English, not the code :-). */
      APR_EOL_STR
      "If the directory \""
      SVN_REPOS__DB_DIR
      "\" contains a Berkeley DB environment,"
      APR_EOL_STR
      "you may need to tweak the values in \""
      SVN_REPOS__DB_DIR
      "/DB_CONFIG\" to match the"
      APR_EOL_STR
      "requirements of your site."
      APR_EOL_STR
      APR_EOL_STR
      "Visit http://subversion.tigris.org/ for more information."
      APR_EOL_STR;

    SVN_ERR_W (svn_io_file_create (readme_file_name, readme_contents, pool),
               "Creating readme file");
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
             "Repository creation failed");
  
  /* Create a Berkeley DB environment for the filesystem. */
  SVN_ERR (svn_fs_create (&repos->fs, repos->db_path, fs_config, pool));

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

  /* Check the db/ subdir, but allow it to be a symlink (Subversion
     works just fine if it's a symlink). */
  err = svn_io_check_resolved_path
    (svn_path_join (path, SVN_REPOS__DB_DIR, pool), &kind, pool);
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
  const char *format_path;

  format_path = svn_path_join (path, SVN_REPOS__FORMAT, pool);
  SVN_ERR (svn_io_read_version_file (&version, format_path, pool));

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
           svn_boolean_t exclusive,
           svn_boolean_t nonblocking,
           svn_boolean_t open_fs,
           apr_pool_t *pool)
{
  svn_repos_t *repos;

  /* Verify the validity of our repository format. */
  SVN_ERR (check_repos_version (path, pool));

  /* Allocate a repository object. */
  repos = apr_pcalloc (pool, sizeof (*repos));

  /* Initialize the repository paths. */
  init_repos_dirs (repos, path, pool);

  /* Locking. */
  {
    const char *lockfile_path;
    svn_error_t *err;

    /* Get a filehandle for the repository's db lockfile. */
    lockfile_path = svn_repos_db_lockfile (repos, pool);

    err = svn_io_file_lock2 (lockfile_path, exclusive, nonblocking, pool);
    if (err != NULL && APR_STATUS_IS_EAGAIN(err->apr_err))
      return err;
    SVN_ERR_W (err, "Error opening db lockfile");
  }

  /* Open up the Berkeley filesystem only after obtaining the lock. */
  if (open_fs)
    SVN_ERR (svn_fs_open (&repos->fs, repos->db_path, NULL, pool));

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

  SVN_ERR (get_repos (repos_p, path, FALSE, FALSE, TRUE, pool));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_repos_delete (const char *path, 
                  apr_pool_t *pool)
{
  const char *db_path = svn_path_join (path, SVN_REPOS__DB_DIR, pool);

  /* Delete the Berkeley environment... */
  SVN_ERR (svn_fs_delete_fs (db_path, pool));

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


/* This code uses repository locking, which is motivated by the
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

svn_error_t *
svn_repos_recover2 (const char *path,
                    svn_boolean_t nonblocking,
                    apr_pool_t *pool)
{
  svn_repos_t *repos;
  apr_pool_t *subpool = svn_pool_create (pool);

  /* Fetch a repository object initialized with an EXCLUSIVE lock on
     the database.   This will at least prevent others from trying to
     read or write to it while we run recovery. */
  SVN_ERR (get_repos (&repos, path, TRUE, nonblocking,
                      FALSE,    /* don't try to open the db yet. */
                      subpool));

  /* Recover the database to a consistent state. */
  SVN_ERR (svn_fs_berkeley_recover (repos->db_path, subpool));

  /* Close shop and free the subpool, to release the exclusive lock. */
  svn_pool_destroy (subpool);

  return SVN_NO_ERROR;
}


svn_error_t *
svn_repos_recover (const char *path,
                   apr_pool_t *pool)
{
    return svn_repos_recover2 (path, FALSE, pool);
}

svn_error_t *svn_repos_db_logfiles (apr_array_header_t **logfiles,
                                    const char *path,
                                    svn_boolean_t only_unused,
                                    apr_pool_t *pool)
{
  svn_repos_t *repos;
  int i;

  SVN_ERR (get_repos (&repos, path,
                      FALSE, FALSE,
                      FALSE,     /* Do not open fs. */
                      pool));

  SVN_ERR (svn_fs_berkeley_logfiles (logfiles,
                                    svn_repos_db_env (repos, pool),
                                    only_unused,
                                    pool));

  /* Loop, printing log files. */
  for (i = 0; i < (*logfiles)->nelts; i++)
    {
      const char ** log_file = &(APR_ARRAY_IDX (*logfiles, i, const char *));
      *log_file = svn_path_join(SVN_REPOS__DB_DIR, *log_file, pool);
    }

  return SVN_NO_ERROR;
}

/** Hot copy structure copy context.
 */
struct hotcopy_ctx_t {
  const char *dest;     /* target location to construct */
  unsigned int src_len; /* len of the source path*/
};

/** Called by (svn_io_dir_walk).
 * Copies the repository structure with exception of
 * @c SVN_REPOS__DB_DIR and @c SVN_REPOS__LOCK_DIR.
 * Those directories are handled separetly.
 * @a baton is a pointer to (struct hotcopy_ctx_t) specifying
 * destination path to copy to and the length of the source path.
 *  
 * @copydoc svn_io_dir_walk()
 */
static svn_error_t *hotcopy_structure (void *baton,
                                       const char *path,
                                       const apr_finfo_t *finfo,
                                       apr_pool_t *pool)
{
  const struct hotcopy_ctx_t *ctx = ((struct hotcopy_ctx_t *) baton);
  const char *sub_path;
  const char *target;

  if (strlen (path) == ctx->src_len)
    {
      sub_path = "";
    } 
  else
    {
      sub_path = &path[ctx->src_len+1];

      /* Check if we are inside db directory and if so skip it */
      if (svn_path_compare_paths(
            svn_path_get_longest_ancestor (SVN_REPOS__DB_DIR, sub_path, pool), 
            SVN_REPOS__DB_DIR) == 0)
        return SVN_NO_ERROR;

      if (svn_path_compare_paths(
            svn_path_get_longest_ancestor (SVN_REPOS__LOCK_DIR, 
                                           sub_path, pool),
            SVN_REPOS__LOCK_DIR) == 0)
        return SVN_NO_ERROR;
    }

  target = svn_path_join (ctx->dest, sub_path, pool);

  if (finfo->filetype == APR_DIR)
    {
      SVN_ERR (create_repos_dir (target, pool));
    } 
  else if (finfo->filetype == APR_REG)
    {
    
      SVN_ERR(svn_io_copy_file(path, target, TRUE, pool));
    }

  return SVN_NO_ERROR;
}


/** Obtain a lock on db logs lock file. Create one if it does not exist.
 */
static svn_error_t *
lock_db_logs_file (svn_repos_t *repos,
                   svn_boolean_t exclusive,
                   apr_pool_t *pool)
{
  const char * lock_file = svn_repos_db_logs_lockfile (repos, pool);

  /* Try to create a lock file, in case if it is missing. As in case of the
     repositories created before hotcopy functionality.  */
  svn_error_clear (create_db_logs_lock (repos, pool));

  SVN_ERR (svn_io_file_lock (lock_file, exclusive, pool));

  return SVN_NO_ERROR;
}


/* Make a copy of a repository with hot backup of fs. */
svn_error_t *
svn_repos_hotcopy (const char *src_path,
                   const char *dst_path,
                   svn_boolean_t clean_logs,
                   apr_pool_t *pool)
{
  svn_repos_t *src_repos;
  svn_repos_t *dst_repos;
  struct hotcopy_ctx_t hotcopy_context;
  
  /* Try to open original repository */
  SVN_ERR (get_repos (&src_repos, src_path,
                      FALSE, FALSE,
                      FALSE,    /* don't try to open the db yet. */
                      pool));

  /* If we are going to clean logs, then get an exclusive lock on
     db-logs.lock, to ensure that no one else will work with logs.

     If we are just copying, then get a shared lock to ensure that 
     no one else will clean logs while we copying them */
  
  SVN_ERR (lock_db_logs_file (src_repos, clean_logs, pool));

  /* Copy the repository to a new path, with exception of 
     specially handled directories */

  hotcopy_context.dest = dst_path;
  hotcopy_context.src_len = strlen (src_path);
  SVN_ERR (svn_io_dir_walk (src_path,
                            0,
                            hotcopy_structure,
                            &hotcopy_context,
                            pool));

  /* Prepare dst_repos object so that we may create locks,
     so that we may open repository */

  dst_repos = apr_pcalloc (pool, sizeof (*dst_repos));

  init_repos_dirs (dst_repos, dst_path, pool);

  SVN_ERR (create_locks (dst_repos, pool));

  SVN_ERR (svn_io_dir_make_sgid (dst_repos->db_path, APR_OS_DEFAULT, pool));

  /* Open repository, since before we only initialized the directories. 
     Above is a work around because lock creation functions expect a
     pointer to (svn_repos_t) with initialized paths. */

  /* Exclusively lock the new repository.  
     No one should be accessing it at the moment */ 
  SVN_ERR (get_repos (&dst_repos, dst_path,
                      TRUE, FALSE,
                      FALSE,    /* don't try to open the db yet. */
                      pool));


  SVN_ERR (svn_fs_hotcopy (src_repos->db_path, dst_repos->db_path,
                           clean_logs, pool));

  return SVN_NO_ERROR;
}

/* Return the library version number. */
const svn_version_t *
svn_repos_version (void)
{
  SVN_VERSION_BODY;
}
