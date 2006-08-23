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
#include "svn_utf.h"
#include "svn_time.h"
#include "svn_fs.h"
#include "svn_repos.h"
#include "svn_private_config.h" /* for SVN_TEMPLATE_ROOT_DIR */

#include "repos.h"



/* Path accessor functions. */


const char *
svn_repos_path(svn_repos_t *repos, apr_pool_t *pool)
{
  return apr_pstrdup(pool, repos->path);
}


const char *
svn_repos_db_env(svn_repos_t *repos, apr_pool_t *pool)
{
  return apr_pstrdup(pool, repos->db_path);
}


const char *
svn_repos_conf_dir(svn_repos_t *repos, apr_pool_t *pool)
{
  return apr_pstrdup(pool, repos->conf_path);
}


const char *
svn_repos_svnserve_conf(svn_repos_t *repos, apr_pool_t *pool)
{
  return svn_path_join(repos->conf_path, SVN_REPOS__CONF_SVNSERVE_CONF, pool);
}


const char *
svn_repos_lock_dir(svn_repos_t *repos, apr_pool_t *pool)
{
  return apr_pstrdup(pool, repos->lock_path);
}


const char *
svn_repos_db_lockfile(svn_repos_t *repos, apr_pool_t *pool)
{
  return svn_path_join(repos->lock_path, SVN_REPOS__DB_LOCKFILE, pool);
}


const char *
svn_repos_db_logs_lockfile(svn_repos_t *repos, apr_pool_t *pool)
{
  return svn_path_join(repos->lock_path, SVN_REPOS__DB_LOGS_LOCKFILE, pool);
}

const char *
svn_repos_hook_dir(svn_repos_t *repos, apr_pool_t *pool)
{
  return apr_pstrdup(pool, repos->hook_path);
}


const char *
svn_repos_start_commit_hook(svn_repos_t *repos, apr_pool_t *pool)
{
  return svn_path_join(repos->hook_path, SVN_REPOS__HOOK_START_COMMIT, pool);
}


const char *
svn_repos_pre_commit_hook(svn_repos_t *repos, apr_pool_t *pool)
{
  return svn_path_join(repos->hook_path, SVN_REPOS__HOOK_PRE_COMMIT, pool);
}


const char *
svn_repos_pre_lock_hook(svn_repos_t *repos, apr_pool_t *pool)
{
  return svn_path_join(repos->hook_path, SVN_REPOS__HOOK_PRE_LOCK, pool);
}


const char *
svn_repos_pre_unlock_hook(svn_repos_t *repos, apr_pool_t *pool)
{
  return svn_path_join(repos->hook_path, SVN_REPOS__HOOK_PRE_UNLOCK, pool);
}

const char *
svn_repos_post_lock_hook(svn_repos_t *repos, apr_pool_t *pool)
{
  return svn_path_join(repos->hook_path, SVN_REPOS__HOOK_POST_LOCK, pool);
}


const char *
svn_repos_post_unlock_hook(svn_repos_t *repos, apr_pool_t *pool)
{
  return svn_path_join(repos->hook_path, SVN_REPOS__HOOK_POST_UNLOCK, pool);
}


const char *
svn_repos_post_commit_hook(svn_repos_t *repos, apr_pool_t *pool)
{
  return svn_path_join(repos->hook_path, SVN_REPOS__HOOK_POST_COMMIT, pool);
}


const char *
svn_repos_pre_revprop_change_hook(svn_repos_t *repos, apr_pool_t *pool)
{
  return svn_path_join(repos->hook_path, SVN_REPOS__HOOK_PRE_REVPROP_CHANGE,
                       pool);
}


const char *
svn_repos_post_revprop_change_hook(svn_repos_t *repos, apr_pool_t *pool)
{
  return svn_path_join(repos->hook_path, SVN_REPOS__HOOK_POST_REVPROP_CHANGE,
                       pool);
}


static svn_error_t *
create_repos_dir(const char *path, apr_pool_t *pool)
{
  svn_error_t *err;

  err = svn_io_dir_make(path, APR_OS_DEFAULT, pool);
  if (err && (APR_STATUS_IS_EEXIST(err->apr_err)))
    {
      svn_boolean_t is_empty;

      svn_error_clear(err);

      SVN_ERR(svn_io_dir_empty(&is_empty, path, pool));

      if (is_empty)
        err = NULL;
      else
        err = svn_error_createf(SVN_ERR_DIR_NOT_EMPTY, 0,
                                _("'%s' exists and is non-empty"),
                                path);
    }

  return err;
}

static const char * bdb_lock_file_contents = 
  "DB lock file, representing locks on the versioned filesystem."
  APR_EOL_STR
  APR_EOL_STR
  "All accessors -- both readers and writers -- of the repository's"
  APR_EOL_STR
  "Berkeley DB environment take out shared locks on this file, and"
  APR_EOL_STR
  "each accessor removes its lock when done.  If and when the DB"
  APR_EOL_STR
  "recovery procedure is run, the recovery code takes out an"
  APR_EOL_STR
  "exclusive lock on this file, so we can be sure no one else is"
  APR_EOL_STR
  "using the DB during the recovery."
  APR_EOL_STR
  APR_EOL_STR
  "You should never have to edit or remove this file."
  APR_EOL_STR;

static const char * bdb_logs_lock_file_contents = 
  "DB logs lock file, representing locks on the versioned filesystem logs."
  APR_EOL_STR
  APR_EOL_STR
  "All log manipulators of the repository's Berkeley DB environment"
  APR_EOL_STR
  "take out exclusive locks on this file to ensure that only one"
  APR_EOL_STR
  "accessor manipulates the logs at a time."
  APR_EOL_STR
  APR_EOL_STR
  "You should never have to edit or remove this file."
  APR_EOL_STR;

static const char * pre12_compat_unneeded_file_contents = 
  "This file is not used by Subversion 1.3.x or later."
  APR_EOL_STR
  "However, its existence is required for compatibility with"
  APR_EOL_STR
  "Subversion 1.2.x or earlier."
  APR_EOL_STR;

/* Create the DB logs lockfile. */
static svn_error_t *
create_db_logs_lock(svn_repos_t *repos, apr_pool_t *pool) {
  const char *contents;
  const char *lockfile_path;

  lockfile_path = svn_repos_db_logs_lockfile(repos, pool);
  if (strcmp(repos->fs_type, SVN_FS_TYPE_BDB) == 0)
    contents = bdb_logs_lock_file_contents;
  else
    contents = pre12_compat_unneeded_file_contents;

  SVN_ERR_W(svn_io_file_create(lockfile_path, contents, pool),
            _("Creating db logs lock file"));

  return SVN_NO_ERROR;
}

/* Create the DB lockfile. */
static svn_error_t *
create_db_lock(svn_repos_t *repos, apr_pool_t *pool) {
  const char *contents;
  const char *lockfile_path;

  lockfile_path = svn_repos_db_lockfile(repos, pool);
  if (strcmp(repos->fs_type, SVN_FS_TYPE_BDB) == 0)
    contents = bdb_lock_file_contents;
  else
    contents = pre12_compat_unneeded_file_contents;
    
  SVN_ERR_W(svn_io_file_create(lockfile_path, contents, pool),
            _("Creating db lock file"));
    
  return SVN_NO_ERROR;
}

static svn_error_t *
create_locks(svn_repos_t *repos, apr_pool_t *pool)
{
  /* Create the locks directory. */
  SVN_ERR_W(create_repos_dir(repos->lock_path, pool),
            _("Creating lock dir"));

  SVN_ERR(create_db_lock(repos, pool));
  SVN_ERR(create_db_logs_lock(repos, pool));

  return SVN_NO_ERROR;
}


#define HOOKS_ENVIRONMENT_TEXT                                          \
  "# The hook program typically does not inherit the environment of"    \
  APR_EOL_STR                                                           \
  "# its parent process.  For example, a common problem is for the"     \
  APR_EOL_STR                                                           \
  "# PATH environment variable to not be set to its usual value, so"    \
  APR_EOL_STR                                                           \
  "# that subprograms fail to launch unless invoked via absolute path." \
  APR_EOL_STR                                                           \
  "# If you're having unexpected problems with a hook program, the"     \
  APR_EOL_STR                                                           \
  "# culprit may be unusual (or missing) environment variables."        \
  APR_EOL_STR

#define PREWRITTEN_HOOKS_TEXT                                           \
  "# For more examples and pre-written hooks, see those in"             \
  APR_EOL_STR                                                           \
  "# the Subversion repository at"                                      \
  APR_EOL_STR                                                           \
  "# http://svn.collab.net/repos/svn/trunk/tools/hook-scripts/ and"     \
  APR_EOL_STR                                                           \
  "# http://svn.collab.net/repos/svn/trunk/contrib/hook-scripts/"       \
  APR_EOL_STR                                                           \


static svn_error_t *
create_hooks(svn_repos_t *repos, apr_pool_t *pool)
{
  const char *this_path, *contents;

  /* Create the hook directory. */
  SVN_ERR_W(create_repos_dir(repos->hook_path, pool),
            _("Creating hook directory"));

  /*** Write a default template for each standard hook file. */

  /* Start-commit hook. */
  {
    this_path = apr_psprintf(pool, "%s%s",
                             svn_repos_start_commit_hook(repos, pool),
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
      HOOKS_ENVIRONMENT_TEXT
      "# "
      APR_EOL_STR
      "# Here is an example hook script, for a Unix /bin/sh interpreter."
      APR_EOL_STR
      PREWRITTEN_HOOKS_TEXT
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

    SVN_ERR_W(svn_io_file_create(this_path, contents, pool),
              _("Creating start-commit hook"));
  }  /* end start-commit hook */

  /* Pre-commit hook. */
  {
    this_path = apr_psprintf(pool, "%s%s",
                             svn_repos_pre_commit_hook(repos, pool),
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
      HOOKS_ENVIRONMENT_TEXT
      "# "
      APR_EOL_STR
      "# Here is an example hook script, for a Unix /bin/sh interpreter."
      APR_EOL_STR
      PREWRITTEN_HOOKS_TEXT
      APR_EOL_STR
      APR_EOL_STR
      "REPOS=\"$1\""
      APR_EOL_STR
      "TXN=\"$2\""
      APR_EOL_STR
      APR_EOL_STR
      "# Make sure that the log message contains some text."
      APR_EOL_STR
      "SVNLOOK=" SVN_BINDIR "/svnlook"
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
    
    SVN_ERR_W(svn_io_file_create(this_path, contents, pool),
              _("Creating pre-commit hook"));
  }  /* end pre-commit hook */


  /* Pre-revprop-change hook. */
  {
    this_path = apr_psprintf(pool, "%s%s",
                             svn_repos_pre_revprop_change_hook(repos, pool),
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
      "# is added, modified or deleted.  Subversion runs this hook by invoking"
      APR_EOL_STR
      "# a program (script, executable, binary, etc.) named '"
      SVN_REPOS__HOOK_PRE_REVPROP_CHANGE "'" 
      APR_EOL_STR
      "# (for which this file is a template), with the following ordered"
      APR_EOL_STR
      "# arguments:"
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
      "#   [5] ACTION       (the property is being 'A'dded, 'M'odified, or "
      "'D'eleted)"
      APR_EOL_STR
      "#"
      APR_EOL_STR
      "#   [STDIN] PROPVAL  ** the new property value is passed via STDIN."
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
      HOOKS_ENVIRONMENT_TEXT
      "# "
      APR_EOL_STR
      "# Here is an example hook script, for a Unix /bin/sh interpreter."
      APR_EOL_STR
      PREWRITTEN_HOOKS_TEXT
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
      "ACTION=\"$5\""
      APR_EOL_STR
      APR_EOL_STR
      "if [ \"$ACTION\" = \"M\" -a \"$PROPNAME\" = \"svn:log\" ]; "
      "then exit 0; fi"
      APR_EOL_STR
      APR_EOL_STR
      "echo \"Changing revision properties other than svn:log is "
      "prohibited\" >&2"
      APR_EOL_STR
      "exit 1"
      APR_EOL_STR;
    
    SVN_ERR_W(svn_io_file_create(this_path, contents, pool),
              _("Creating pre-revprop-change hook"));
  }  /* end pre-revprop-change hook */


  /* Pre-lock hook. */
  {
    this_path = apr_psprintf(pool, "%s%s",
                             svn_repos_pre_lock_hook(repos, pool),
                             SVN_REPOS__HOOK_DESC_EXT);

    contents =
      "#!/bin/sh"
      APR_EOL_STR
      APR_EOL_STR
      "# PRE-LOCK HOOK"
      APR_EOL_STR
      "#"
      APR_EOL_STR
      "# The pre-lock hook is invoked before an exclusive lock is"
      APR_EOL_STR
      "# created.  Subversion runs this hook by invoking a program "
      APR_EOL_STR
      "# (script, executable, binary, etc.) named "
      "'" 
      SVN_REPOS__HOOK_PRE_LOCK "' (for which"
      APR_EOL_STR
      "# this file is a template), with the following ordered arguments:"
      APR_EOL_STR
      "#"
      APR_EOL_STR
      "#   [1] REPOS-PATH   (the path to this repository)"
      APR_EOL_STR
      "#   [2] PATH         (the path in the repository about to be locked)"
      APR_EOL_STR
      "#   [3] USER         (the user creating the lock)"
      APR_EOL_STR
      "#"
      APR_EOL_STR
      "# The default working directory for the invocation is undefined, so"
      APR_EOL_STR
      "# the program should set one explicitly if it cares."
      APR_EOL_STR
      "#"
      APR_EOL_STR
      "# If the hook program exits with success, the lock is created; but"
      APR_EOL_STR
      "# if it exits with failure (non-zero), the lock action is aborted"
      APR_EOL_STR
      "# and STDERR is returned to the client."
      APR_EOL_STR
      APR_EOL_STR
      "# On a Unix system, the normal procedure is to have "
      "'"
      SVN_REPOS__HOOK_PRE_LOCK
      "'" 
      APR_EOL_STR
      "# invoke other programs to do the real work, though it may do the"
      APR_EOL_STR
      "# work itself too."
      APR_EOL_STR
      "#"
      APR_EOL_STR
      "# Note that"
      " '" SVN_REPOS__HOOK_PRE_LOCK "' "
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
      "# '" SVN_REPOS__HOOK_PRE_LOCK ".bat' or "
      "'" SVN_REPOS__HOOK_PRE_LOCK ".exe',"
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
      "PATH=\"$2\""
      APR_EOL_STR
      "USER=\"$3\""
      APR_EOL_STR
      APR_EOL_STR
      "# If a lock exists and is owned by a different person, don't allow it"
      APR_EOL_STR
      "# to be stolen (e.g., with 'svn lock --force ...')."
      APR_EOL_STR
      APR_EOL_STR
      "# (Maybe this script could send email to the lock owner?)"
      APR_EOL_STR
      "SVNLOOK=" SVN_BINDIR "/svnlook"
      APR_EOL_STR
      "GREP=/bin/grep"
      APR_EOL_STR
      "SED=/bin/sed"
      APR_EOL_STR
      APR_EOL_STR
      "LOCK_OWNER=`$SVNLOOK lock \"$REPOS\" \"$PATH\" | \\"
      APR_EOL_STR
      "            $GREP '^Owner: ' | $SED 's/Owner: //'`"
      APR_EOL_STR
      APR_EOL_STR
      "# If we get no result from svnlook, there's no lock, allow the lock to"
      APR_EOL_STR
      "# happen:"
      APR_EOL_STR
      "if [ \"$LOCK_OWNER\" = \"\" ]; then"
      APR_EOL_STR
      "  exit 0"
      APR_EOL_STR
      "fi"
      APR_EOL_STR
      APR_EOL_STR
      "# If the person locking matches the lock's owner, allow the lock to"
      APR_EOL_STR
      "# happen:"
      APR_EOL_STR
      "if [ \"$LOCK_OWNER\" = \"$USER\" ]; then"
      APR_EOL_STR
      "  exit 0"
      APR_EOL_STR
      "fi"
      APR_EOL_STR
      APR_EOL_STR
      "# Otherwise, we've got an owner mismatch, so return failure:"
      APR_EOL_STR
      "echo \"Error: $PATH already locked by ${LOCK_OWNER}.\" 1>&2"
      APR_EOL_STR
      "exit 1"
      APR_EOL_STR;
    
    SVN_ERR_W(svn_io_file_create(this_path, contents, pool),
              "Creating pre-lock hook");
  }  /* end pre-lock hook */


  /* Pre-unlock hook. */
  {
    this_path = apr_psprintf(pool, "%s%s",
                             svn_repos_pre_unlock_hook(repos, pool),
                             SVN_REPOS__HOOK_DESC_EXT);

    contents =
      "#!/bin/sh"
      APR_EOL_STR
      APR_EOL_STR
      "# PRE-UNLOCK HOOK"
      APR_EOL_STR
      "#"
      APR_EOL_STR
      "# The pre-unlock hook is invoked before an exclusive lock is"
      APR_EOL_STR
      "# destroyed.  Subversion runs this hook by invoking a program "
      APR_EOL_STR
      "# (script, executable, binary, etc.) named "
      "'" 
      SVN_REPOS__HOOK_PRE_UNLOCK "' (for which"
      APR_EOL_STR
      "# this file is a template), with the following ordered arguments:"
      APR_EOL_STR
      "#"
      APR_EOL_STR
      "#   [1] REPOS-PATH   (the path to this repository)"
      APR_EOL_STR
      "#   [2] PATH         (the path in the repository about to be unlocked)"
      APR_EOL_STR
      "#   [3] USER         (the user destroying the lock)"
      APR_EOL_STR
      "#"
      APR_EOL_STR
      "# The default working directory for the invocation is undefined, so"
      APR_EOL_STR
      "# the program should set one explicitly if it cares."
      APR_EOL_STR
      "#"
      APR_EOL_STR
      "# If the hook program exits with success, the lock is destroyed; but"
      APR_EOL_STR
      "# if it exits with failure (non-zero), the unlock action is aborted"
      APR_EOL_STR
      "# and STDERR is returned to the client."
      APR_EOL_STR
      APR_EOL_STR
      "# On a Unix system, the normal procedure is to have "
      "'"
      SVN_REPOS__HOOK_PRE_UNLOCK
      "'" 
      APR_EOL_STR
      "# invoke other programs to do the real work, though it may do the"
      APR_EOL_STR
      "# work itself too."
      APR_EOL_STR
      "#"
      APR_EOL_STR
      "# Note that"
      " '" SVN_REPOS__HOOK_PRE_UNLOCK "' "
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
      "# '" SVN_REPOS__HOOK_PRE_UNLOCK ".bat' or "
      "'" SVN_REPOS__HOOK_PRE_UNLOCK ".exe',"
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
      "PATH=\"$2\""
      APR_EOL_STR
      "USER=\"$3\""
      APR_EOL_STR
      APR_EOL_STR
      "# If a lock is owned by a different person, don't allow it be broken."
      APR_EOL_STR
      "# (Maybe this script could send email to the lock owner?)"
      APR_EOL_STR
      APR_EOL_STR
      "SVNLOOK=" SVN_BINDIR "/svnlook"
      APR_EOL_STR
      "GREP=/bin/grep"
      APR_EOL_STR
      "SED=/bin/sed"
      APR_EOL_STR
      APR_EOL_STR
      "LOCK_OWNER=`$SVNLOOK lock \"$REPOS\" \"$PATH\" | \\"
      APR_EOL_STR
      "            $GREP '^Owner: ' | $SED 's/Owner: //'`"
      APR_EOL_STR
      APR_EOL_STR
      "# If we get no result from svnlook, there's no lock, return success:"
      APR_EOL_STR
      "if [ \"$LOCK_OWNER\" = \"\" ]; then"
      APR_EOL_STR
      "  exit 0"
      APR_EOL_STR
      "fi"
      APR_EOL_STR
      "# If the person unlocking matches the lock's owner, return success:"
      APR_EOL_STR
      "if [ \"$LOCK_OWNER\" = \"$USER\" ]; then"
      APR_EOL_STR
      "  exit 0"
      APR_EOL_STR
      "fi"
      APR_EOL_STR
      APR_EOL_STR
      "# Otherwise, we've got an owner mismatch, so return failure:"
      APR_EOL_STR
      "echo \"Error: $PATH locked by ${LOCK_OWNER}.\" 1>&2"
      APR_EOL_STR
      "exit 1"
      APR_EOL_STR;
    
    SVN_ERR_W(svn_io_file_create(this_path, contents, pool),
              "Creating pre-unlock hook");
  }  /* end pre-unlock hook */



  /* Post-commit hook. */
  {
    this_path = apr_psprintf(pool, "%s%s",
                             svn_repos_post_commit_hook(repos, pool),
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
      HOOKS_ENVIRONMENT_TEXT
      "# "
      APR_EOL_STR
      "# Here is an example hook script, for a Unix /bin/sh interpreter."
      APR_EOL_STR
      PREWRITTEN_HOOKS_TEXT
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

    SVN_ERR_W(svn_io_file_create(this_path, contents, pool),
              _("Creating post-commit hook"));
  } /* end post-commit hook */


  /* Post-lock hook. */
  {
    this_path = apr_psprintf(pool, "%s%s",
                             svn_repos_post_lock_hook(repos, pool),
                             SVN_REPOS__HOOK_DESC_EXT);

    contents =
      "#!/bin/sh"
      APR_EOL_STR
      APR_EOL_STR
      "# POST-LOCK HOOK"
      APR_EOL_STR
      "#"
      APR_EOL_STR
      "# The post-lock hook is run after a path is locked.  Subversion runs"
      APR_EOL_STR
      "# this hook by invoking a program (script, executable, binary, etc.)"
      APR_EOL_STR
      "# named '"
      SVN_REPOS__HOOK_POST_LOCK 
      "' (for which this file is a template) with the "
      APR_EOL_STR
      "# following ordered arguments:"
      APR_EOL_STR
      "#"
      APR_EOL_STR
      "#   [1] REPOS-PATH   (the path to this repository)"
      APR_EOL_STR
      "#   [2] USER         (the user who created the lock)"
      APR_EOL_STR
      "#"
      APR_EOL_STR
      "# The paths that were just locked are passed to the hook via STDIN (as"
      APR_EOL_STR
      "# of Subversion 1.2, only one path is passed per invocation, but the"
      APR_EOL_STR
      "# plan is to pass all locked paths at once, so the hook program"
      APR_EOL_STR
      "# should be written accordingly)."
      APR_EOL_STR
      "#"
      APR_EOL_STR
      "# The default working directory for the invocation is undefined, so"
      APR_EOL_STR
      "# the program should set one explicitly if it cares."
      APR_EOL_STR
      "#"
      APR_EOL_STR
      "# Because the lock has already been created and cannot be undone,"
      APR_EOL_STR
      "# the exit code of the hook program is ignored.  The hook program"
      APR_EOL_STR
      "# can use the 'svnlook' utility to help it examine the"
      APR_EOL_STR
      "# newly-created lock."
      APR_EOL_STR
      "#"
      APR_EOL_STR
      "# On a Unix system, the normal procedure is to have "
      "'"
      SVN_REPOS__HOOK_POST_LOCK
      "'" 
      APR_EOL_STR
      "# invoke other programs to do the real work, though it may do the"
      APR_EOL_STR
      "# work itself too."
      APR_EOL_STR
      "#"
      APR_EOL_STR
      "# Note that"
      " '" SVN_REPOS__HOOK_POST_LOCK "' "
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
      "# '" SVN_REPOS__HOOK_POST_LOCK ".bat' or "
      "'" SVN_REPOS__HOOK_POST_LOCK ".exe',"
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
      "# Send email to interested parties, let them know a lock was created:"
      APR_EOL_STR
      "mailer.py lock \"$REPOS\" \"$USER\" /path/to/mailer.conf"
      APR_EOL_STR;

    SVN_ERR_W(svn_io_file_create(this_path, contents, pool),
              "Creating post-lock hook");
  } /* end post-lock hook */


  /* Post-unlock hook. */
  {
    this_path = apr_psprintf(pool, "%s%s",
                             svn_repos_post_unlock_hook(repos, pool),
                             SVN_REPOS__HOOK_DESC_EXT);

    contents =
      "#!/bin/sh"
      APR_EOL_STR
      APR_EOL_STR
      "# POST-UNLOCK HOOK"
      APR_EOL_STR
      "#"
      APR_EOL_STR
      "# The post-unlock hook runs after a path is unlocked.  Subversion runs"
      APR_EOL_STR
      "# this hook by invoking a program (script, executable, binary, etc.)"
      APR_EOL_STR
      "# named '"
      SVN_REPOS__HOOK_POST_UNLOCK 
      "' (for which this file is a template) with the "
      APR_EOL_STR
      "# following ordered arguments:"
      APR_EOL_STR
      "#"
      APR_EOL_STR
      "#   [1] REPOS-PATH   (the path to this repository)"
      APR_EOL_STR
      "#   [2] USER         (the user who destroyed the lock)"
      APR_EOL_STR
      "#"
      APR_EOL_STR
      "# The paths that were just unlocked are passed to the hook via STDIN"
      APR_EOL_STR
      "# (as of Subversion 1.2, only one path is passed per invocation, but"
      APR_EOL_STR
      "# the plan is to pass all unlocked paths at once, so the hook program"
      APR_EOL_STR
      "# should be written accordingly)."
      APR_EOL_STR
      "#"
      APR_EOL_STR
      "# The default working directory for the invocation is undefined, so"
      APR_EOL_STR
      "# the program should set one explicitly if it cares."
      APR_EOL_STR
      "#"
      APR_EOL_STR
      "# Because the lock has already been destroyed and cannot be undone,"
      APR_EOL_STR
      "# the exit code of the hook program is ignored."
      APR_EOL_STR
      "#"
      APR_EOL_STR
      "# On a Unix system, the normal procedure is to have "
      "'"
      SVN_REPOS__HOOK_POST_UNLOCK
      "'" 
      APR_EOL_STR
      "# invoke other programs to do the real work, though it may do the"
      APR_EOL_STR
      "# work itself too."
      APR_EOL_STR
      "#"
      APR_EOL_STR
      "# Note that"
      " '" SVN_REPOS__HOOK_POST_UNLOCK "' "
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
      "# '" SVN_REPOS__HOOK_POST_UNLOCK ".bat' or "
      "'" SVN_REPOS__HOOK_POST_UNLOCK ".exe',"
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
      "# Send email to interested parties, let them know a lock was removed:"
      APR_EOL_STR
      "mailer.py unlock \"$REPOS\" \"$USER\" /path/to/mailer.conf"
      APR_EOL_STR;

    SVN_ERR_W(svn_io_file_create(this_path, contents, pool),
              "Creating post-unlock hook");
  } /* end post-unlock hook */


  /* Post-revprop-change hook. */
  {
    this_path = apr_psprintf(pool, "%s%s",
                             svn_repos_post_revprop_change_hook(repos, pool),
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
      "# has been added, modified or deleted.  Subversion runs this hook by"
      APR_EOL_STR
      "# invoking a program (script, executable, binary, etc.) named"
      APR_EOL_STR
      "# '" SVN_REPOS__HOOK_POST_REVPROP_CHANGE 
      "' (for which this file is a template), with the"
      APR_EOL_STR
      "# following ordered arguments:"
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
      "#   [5] ACTION       (the property was 'A'dded, 'M'odified, or "
      "'D'eleted)"
      APR_EOL_STR
      "#"
      APR_EOL_STR
      "#   [STDIN] PROPVAL  ** the old property value is passed via STDIN."
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
      HOOKS_ENVIRONMENT_TEXT
      "# "
      APR_EOL_STR
      "# Here is an example hook script, for a Unix /bin/sh interpreter."
      APR_EOL_STR
      PREWRITTEN_HOOKS_TEXT
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
      "ACTION=\"$5\""
      APR_EOL_STR
      APR_EOL_STR
      "propchange-email.pl \"$REPOS\" \"$REV\" \"$USER\" \"$PROPNAME\" "
      "watchers@example.org"
      APR_EOL_STR;

    SVN_ERR_W(svn_io_file_create(this_path, contents, pool),
              _("Creating post-revprop-change hook"));
  } /* end post-revprop-change hook */

  return SVN_NO_ERROR;
}

static svn_error_t *
create_conf(svn_repos_t *repos, apr_pool_t *pool)
{
  SVN_ERR_W(create_repos_dir(repos->conf_path, pool),
            _("Creating conf directory"));

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
      "[general]"
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
      "### Uncomment the line below to use the default password file."
      APR_EOL_STR
      "# password-db = passwd"
      APR_EOL_STR
      "### The authz-db option controls the location of the authorization"
      APR_EOL_STR
      "### rules for path-based access control.  Unless you specify a path"
      APR_EOL_STR
      "### starting with a /, the file's location is relative to the conf"
      APR_EOL_STR
      "### directory.  If you don't specify an authz-db, no path-based access"
      APR_EOL_STR
      "### control is done."
      APR_EOL_STR
      "### Uncomment the line below to use the default authorization file."
      APR_EOL_STR
      "# authz-db = " SVN_REPOS__CONF_AUTHZ
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

    SVN_ERR_W(svn_io_file_create(svn_repos_svnserve_conf(repos, pool),
                                 svnserve_conf_contents, pool),
              _("Creating svnserve.conf file"));
  }

  {
    static const char * const passwd_contents =
      "### This file is an example password file for svnserve."
      APR_EOL_STR
      "### Its format is similar to that of svnserve.conf. As shown in the"
      APR_EOL_STR
      "### example below it contains one section labelled [users]."
      APR_EOL_STR
      "### The name and password for each user follow, one account per line."
      APR_EOL_STR
      APR_EOL_STR
      "[users]"
      APR_EOL_STR
      "# harry = harryssecret"
      APR_EOL_STR
      "# sally = sallyssecret"
      APR_EOL_STR;

    SVN_ERR_W(svn_io_file_create(svn_path_join(repos->conf_path,
                                               SVN_REPOS__CONF_PASSWD,
                                               pool),
                                 passwd_contents, pool),
              _("Creating passwd file"));
  }

  {
    static const char * const authz_contents =
      "### This file is an example authorization file for svnserve."
      APR_EOL_STR
      "### Its format is identical to that of mod_authz_svn authorization"
      APR_EOL_STR
      "### files."
      APR_EOL_STR
      "### As shown below each section defines authorizations for the path and"
      APR_EOL_STR
      "### (optional) repository specified by the section name."
      APR_EOL_STR
      "### The authorizations follow. An authorization line can refer to a"
      APR_EOL_STR
      "### single user, to a group of users defined in a special [groups]"
      APR_EOL_STR
      "### section, or to anyone using the '*' wildcard.  Each definition can"
      APR_EOL_STR
      "### grant read ('r') access, read-write ('rw') access, or no access"
      APR_EOL_STR
      "### ('')."
      APR_EOL_STR
      APR_EOL_STR
      "[groups]"
      APR_EOL_STR
      "# harry_and_sally = harry,sally"
      APR_EOL_STR
      APR_EOL_STR
      "# [/foo/bar]"
      APR_EOL_STR
      "# harry = rw"
      APR_EOL_STR
      "# * ="
      APR_EOL_STR
      APR_EOL_STR
      "# [repository:/baz/fuz]"
      APR_EOL_STR
      "# @harry_and_sally = rw"
      APR_EOL_STR
      "# * = r"
      APR_EOL_STR;

    SVN_ERR_W(svn_io_file_create(svn_path_join(repos->conf_path,
                                               SVN_REPOS__CONF_AUTHZ,
                                               pool),
                                 authz_contents, pool),
              _("Creating authz file"));
  }

  return SVN_NO_ERROR;
}

/* Allocate and return a new svn_repos_t * object, initializing the
   directory pathname members based on PATH.
   The members FS, FORMAT, and FS_TYPE are *not* initialized (they are null),
   and it it the caller's responsibility to fill them in if needed.  */
static svn_repos_t *
create_svn_repos_t(const char *path, apr_pool_t *pool)
{
  svn_repos_t *repos = apr_pcalloc(pool, sizeof(*repos));

  repos->path = apr_pstrdup(pool, path);
  repos->db_path = svn_path_join(path, SVN_REPOS__DB_DIR, pool);
  repos->dav_path = svn_path_join(path, SVN_REPOS__DAV_DIR, pool);
  repos->conf_path = svn_path_join(path, SVN_REPOS__CONF_DIR, pool);
  repos->hook_path = svn_path_join(path, SVN_REPOS__HOOK_DIR, pool);
  repos->lock_path = svn_path_join(path, SVN_REPOS__LOCK_DIR, pool);

  return repos;
}


static svn_error_t *
create_repos_structure(svn_repos_t *repos,
                       const char *path,
                       apr_pool_t *pool)
{
  /* Create the top-level repository directory. */
  SVN_ERR_W(create_repos_dir(path, pool),
            _("Could not create top-level directory"));

  /* Create the DAV sandbox directory.  */
  SVN_ERR_W(create_repos_dir(repos->dav_path, pool),
            _("Creating DAV sandbox dir"));

  /* Create the lock directory.  */
  SVN_ERR(create_locks(repos, pool));

  /* Create the hooks directory.  */
  SVN_ERR(create_hooks(repos, pool));

  /* Create the conf directory.  */
  SVN_ERR(create_conf(repos, pool));

  /* Write the top-level README file. */
  {
    const char * const readme_header =
      "This is a Subversion repository; use the 'svnadmin' tool to examine"
      APR_EOL_STR
      "it.  Do not add, delete, or modify files here unless you know how"
      APR_EOL_STR
      "to avoid corrupting the repository."
      APR_EOL_STR
      APR_EOL_STR;
    const char * const readme_bdb_insert =
      "The directory \""
      SVN_REPOS__DB_DIR
      "\" contains a Berkeley DB environment,"
      APR_EOL_STR
      "you may need to tweak the values in \""
      SVN_REPOS__DB_DIR
      "/DB_CONFIG\" to match the"
      APR_EOL_STR
      "requirements of your site."
      APR_EOL_STR
      APR_EOL_STR;
    const char * const readme_footer =
      "Visit http://subversion.tigris.org/ for more information."
      APR_EOL_STR;
    apr_file_t *f;
    apr_size_t written;

    SVN_ERR(svn_io_file_open(&f,
                             svn_path_join(path, SVN_REPOS__README, pool),
                             (APR_WRITE | APR_CREATE | APR_EXCL),
                             APR_OS_DEFAULT, pool));
    
    SVN_ERR(svn_io_file_write_full(f, readme_header, strlen(readme_header),
                                   &written, pool));
    if (strcmp(repos->fs_type, SVN_FS_TYPE_BDB) == 0)
      SVN_ERR(svn_io_file_write_full(f, readme_bdb_insert,
                                     strlen(readme_bdb_insert),
                                     &written, pool));
    SVN_ERR(svn_io_file_write_full(f, readme_footer, strlen(readme_footer),
                                   &written, pool));

    SVN_ERR(svn_io_file_close(f, pool));
  }

  return SVN_NO_ERROR;
}


/* There is, at present, nothing within the direct responsibility
   of libsvn_repos which requires locking.  For historical compatibility
   reasons, the BDB libsvn_fs backend does not do its own locking, expecting
   libsvn_repos to do the locking for it.  Here we take care of that
   backend-specific requirement. 
   The kind of lock is controlled by EXCLUSIVE and NONBLOCKING.
   The lock is scoped to POOL.  */
static svn_error_t *
lock_repos(svn_repos_t *repos,
           svn_boolean_t exclusive,
           svn_boolean_t nonblocking,
           apr_pool_t *pool)
{
  if (strcmp(repos->fs_type, SVN_FS_TYPE_BDB) == 0)
    {
      svn_error_t *err;
      const char *lockfile_path = svn_repos_db_lockfile(repos, pool);

      err = svn_io_file_lock2(lockfile_path, exclusive, nonblocking, pool);
      if (err != NULL && APR_STATUS_IS_EAGAIN(err->apr_err))
        return err;
      SVN_ERR_W(err, _("Error opening db lockfile"));
    }
  return SVN_NO_ERROR;
}


svn_error_t *
svn_repos_create(svn_repos_t **repos_p,
                 const char *path,
                 const char *unused_1,
                 const char *unused_2,
                 apr_hash_t *config,
                 apr_hash_t *fs_config,
                 apr_pool_t *pool)
{
  svn_repos_t *repos;
  svn_error_t *err;

  /* Allocate a repository object, filling in the format we will create. */
  repos = create_svn_repos_t(path, pool);
  repos->format = SVN_REPOS__FORMAT_NUMBER;

  /* Discover the type of the filesystem we are about to create. */
  if (fs_config)
    {
      repos->fs_type = apr_hash_get(fs_config, SVN_FS_CONFIG_FS_TYPE,
                                    APR_HASH_KEY_STRING);
      if (apr_hash_get(fs_config, SVN_FS_CONFIG_PRE_1_4_COMPATIBLE,
                       APR_HASH_KEY_STRING))
        repos->format = SVN_REPOS__FORMAT_NUMBER_LEGACY;
    }

  if (! repos->fs_type)
    repos->fs_type = DEFAULT_FS_TYPE;

  /* Create the various files and subdirectories for the repository. */
  SVN_ERR_W(create_repos_structure(repos, path, pool),
            _("Repository creation failed"));
  
  /* Lock if needed. */
  SVN_ERR(lock_repos(repos, FALSE, FALSE, pool));

  /* Create an environment for the filesystem. */
  if ((err = svn_fs_create(&repos->fs, repos->db_path, fs_config, pool)))
    {
      /* If there was an error making the filesytem, e.g. unknown/supported
       * filesystem type.  Clean up after ourselves.  Yes this is safe because
       * create_repos_structure will fail if the path existed before we started
       * so we can't accidentally remove a directory that previously existed. */
      svn_error_clear(svn_io_remove_dir(path, pool));
      return err;
    }

  /* This repository is ready.  Stamp it with a format number. */
  SVN_ERR(svn_io_write_version_file 
          (svn_path_join(path, SVN_REPOS__FORMAT, pool),
           repos->format, pool));

  *repos_p = repos;
  return SVN_NO_ERROR;
}


/* Check if @a path is the root of a repository by checking if the
 * path contains the expected files and directories.  Return TRUE
 * on errors (which would be permission errors, probably) so that
 * we the user will see them after we try to open the repository
 * for real.  */
static svn_boolean_t
check_repos_path(const char *path,
                 apr_pool_t *pool)
{
  svn_node_kind_t kind;
  svn_error_t *err;

  err = svn_io_check_path(svn_path_join(path, SVN_REPOS__FORMAT, pool),
                          &kind, pool);
  if (err)
    {
      svn_error_clear(err);
      return TRUE;
    }
  if (kind != svn_node_file)
    return FALSE;

  /* Check the db/ subdir, but allow it to be a symlink (Subversion
     works just fine if it's a symlink). */
  err = svn_io_check_resolved_path
    (svn_path_join(path, SVN_REPOS__DB_DIR, pool), &kind, pool);
  if (err)
    {
      svn_error_clear(err);
      return TRUE;
    }
  if (kind != svn_node_dir)
    return FALSE;

  return TRUE;
}


/* Verify that REPOS's format is suitable.
   Use POOL for temporary allocation. */
static svn_error_t *
check_repos_format(svn_repos_t *repos,
                   apr_pool_t *pool)
{
  int format;
  const char *format_path;

  format_path = svn_path_join(repos->path, SVN_REPOS__FORMAT, pool);
  SVN_ERR(svn_io_read_version_file(&format, format_path, pool));

  if (format != SVN_REPOS__FORMAT_NUMBER &&
      format != SVN_REPOS__FORMAT_NUMBER_LEGACY)
    {
      return svn_error_createf 
        (SVN_ERR_REPOS_UNSUPPORTED_VERSION, NULL,
         _("Expected repository format '%d' or '%d'; found format '%d'"), 
         SVN_REPOS__FORMAT_NUMBER_LEGACY, SVN_REPOS__FORMAT_NUMBER,
         format);
    }

  repos->format = format;

  return SVN_NO_ERROR;
}


/* Set *REPOS_P to a repository at PATH which has been opened.
   See lock_repos() above regarding EXCLUSIVE and NONBLOCKING.
   OPEN_FS indicates whether the Subversion filesystem should be opened,
   the handle being placed into repos->fs.
   Do all allocation in POOL.  */
static svn_error_t *
get_repos(svn_repos_t **repos_p,
          const char *path,
          svn_boolean_t exclusive,
          svn_boolean_t nonblocking,
          svn_boolean_t open_fs,
          apr_pool_t *pool)
{
  svn_repos_t *repos;

  /* Allocate a repository object. */
  repos = create_svn_repos_t(path, pool);

  /* Verify the validity of our repository format. */
  SVN_ERR(check_repos_format(repos, pool));

  /* Discover the FS type. */
  SVN_ERR(svn_fs_type(&repos->fs_type, repos->db_path, pool));

  /* Lock if needed. */
  SVN_ERR(lock_repos(repos, exclusive, nonblocking, pool));

  /* Open up the filesystem only after obtaining the lock. */
  if (open_fs)
    SVN_ERR(svn_fs_open(&repos->fs, repos->db_path, NULL, pool));

  *repos_p = repos;
  return SVN_NO_ERROR;
}



const char *
svn_repos_find_root_path(const char *path,
                         apr_pool_t *pool)
{
  const char *candidate = path;
  const char *decoded;
  svn_error_t *err;

  while (1)
    {
      /* Try to decode the path, so we don't fail if it contains characters
         that aren't supported by the OS filesystem.  The subversion fs
         isn't restricted by the OS filesystem character set. */
      err = svn_utf_cstring_from_utf8(&decoded, candidate, pool);
      if (!err && check_repos_path(candidate, pool))
        break;
      svn_error_clear(err);
      if (candidate[0] == '\0' || strcmp(candidate, "/") == 0)
        return NULL;
      candidate = svn_path_dirname(candidate, pool);
    }

  return candidate;
}


svn_error_t *
svn_repos_open(svn_repos_t **repos_p,
               const char *path,
               apr_pool_t *pool)
{
  /* Fetch a repository object initialized with a shared read/write
     lock on the database. */

  SVN_ERR(get_repos(repos_p, path, FALSE, FALSE, TRUE, pool));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_repos_delete(const char *path, 
                 apr_pool_t *pool)
{
  const char *db_path = svn_path_join(path, SVN_REPOS__DB_DIR, pool);

  /* Delete the filesystem environment... */
  SVN_ERR(svn_fs_delete_fs(db_path, pool));

  /* ...then blow away everything else.  */
  SVN_ERR(svn_io_remove_dir(path, pool));

  return SVN_NO_ERROR;
}


svn_fs_t *
svn_repos_fs(svn_repos_t *repos)
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
svn_repos_recover2(const char *path,
                   svn_boolean_t nonblocking,
                   svn_error_t *(*start_callback)(void *baton),
                   void *start_callback_baton,
                   apr_pool_t *pool)
{
  svn_repos_t *repos;
  apr_pool_t *subpool = svn_pool_create(pool);

  /* Fetch a repository object initialized with an EXCLUSIVE lock on
     the database.   This will at least prevent others from trying to
     read or write to it while we run recovery. */
  SVN_ERR(get_repos(&repos, path, TRUE, nonblocking,
                    FALSE,    /* don't try to open the db yet. */
                    subpool));

  if (start_callback)
    SVN_ERR(start_callback(start_callback_baton));

  /* Recover the database to a consistent state. */
  SVN_ERR(svn_fs_berkeley_recover(repos->db_path, subpool));

  /* Close shop and free the subpool, to release the exclusive lock. */
  svn_pool_destroy(subpool);

  return SVN_NO_ERROR;
}


svn_error_t *
svn_repos_recover(const char *path,
                  apr_pool_t *pool)
{
  return svn_repos_recover2(path, FALSE, NULL, NULL, pool);
}

svn_error_t *svn_repos_db_logfiles(apr_array_header_t **logfiles,
                                   const char *path,
                                   svn_boolean_t only_unused,
                                   apr_pool_t *pool)
{
  svn_repos_t *repos;
  int i;

  SVN_ERR(get_repos(&repos, path,
                    FALSE, FALSE,
                    FALSE,     /* Do not open fs. */
                    pool));

  SVN_ERR(svn_fs_berkeley_logfiles(logfiles,
                                   svn_repos_db_env(repos, pool),
                                   only_unused,
                                   pool));

  /* Loop, printing log files. */
  for (i = 0; i < (*logfiles)->nelts; i++)
    {
      const char ** log_file = &(APR_ARRAY_IDX(*logfiles, i, const char *));
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
 * Copies the repository structure with exception of @c SVN_REPOS__DB_DIR,
 * @c SVN_REPOS__LOCK_DIR and @c SVN_REPOS__FORMAT.
 * Those directories and files are handled separetly.
 * @a baton is a pointer to (struct hotcopy_ctx_t) specifying
 * destination path to copy to and the length of the source path.
 *  
 * @copydoc svn_io_dir_walk()
 */
static svn_error_t *hotcopy_structure(void *baton,
                                      const char *path,
                                      const apr_finfo_t *finfo,
                                      apr_pool_t *pool)
{
  const struct hotcopy_ctx_t *ctx = ((struct hotcopy_ctx_t *) baton);
  const char *sub_path;
  const char *target;

  if (strlen(path) == ctx->src_len)
    {
      sub_path = "";
    } 
  else
    {
      sub_path = &path[ctx->src_len+1];

      /* Check if we are inside db directory and if so skip it */
      if (svn_path_compare_paths
          (svn_path_get_longest_ancestor(SVN_REPOS__DB_DIR, sub_path, pool),
           SVN_REPOS__DB_DIR) == 0)
        return SVN_NO_ERROR;

      if (svn_path_compare_paths
          (svn_path_get_longest_ancestor(SVN_REPOS__LOCK_DIR, sub_path, pool),
           SVN_REPOS__LOCK_DIR) == 0)
        return SVN_NO_ERROR;
      
      if (svn_path_compare_paths
          (svn_path_get_longest_ancestor(SVN_REPOS__FORMAT, sub_path, pool),
           SVN_REPOS__FORMAT) == 0)
        return SVN_NO_ERROR;
    }

  target = svn_path_join(ctx->dest, sub_path, pool);

  if (finfo->filetype == APR_DIR)
    {
      SVN_ERR(create_repos_dir(target, pool));
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
lock_db_logs_file(svn_repos_t *repos,
                  svn_boolean_t exclusive,
                  apr_pool_t *pool)
{
  const char * lock_file = svn_repos_db_logs_lockfile(repos, pool);

  /* Try to create a lock file, in case if it is missing. As in case of the
     repositories created before hotcopy functionality.  */
  svn_error_clear(create_db_logs_lock(repos, pool));

  SVN_ERR(svn_io_file_lock2(lock_file, exclusive, FALSE, pool));

  return SVN_NO_ERROR;
}


/* Make a copy of a repository with hot backup of fs. */
svn_error_t *
svn_repos_hotcopy(const char *src_path,
                  const char *dst_path,
                  svn_boolean_t clean_logs,
                  apr_pool_t *pool)
{
  svn_repos_t *src_repos;
  svn_repos_t *dst_repos;
  struct hotcopy_ctx_t hotcopy_context;
  
  /* Try to open original repository */
  SVN_ERR(get_repos(&src_repos, src_path,
                    FALSE, FALSE,
                    FALSE,    /* don't try to open the db yet. */
                    pool));

  /* If we are going to clean logs, then get an exclusive lock on
     db-logs.lock, to ensure that no one else will work with logs.

     If we are just copying, then get a shared lock to ensure that 
     no one else will clean logs while we copying them */
  
  SVN_ERR(lock_db_logs_file(src_repos, clean_logs, pool));

  /* Copy the repository to a new path, with exception of 
     specially handled directories */

  hotcopy_context.dest = dst_path;
  hotcopy_context.src_len = strlen(src_path);
  SVN_ERR(svn_io_dir_walk(src_path,
                          0,
                          hotcopy_structure,
                          &hotcopy_context,
                          pool));

  /* Prepare dst_repos object so that we may create locks,
     so that we may open repository */

  dst_repos = create_svn_repos_t(dst_path, pool);
  dst_repos->fs_type = src_repos->fs_type;
  dst_repos->format = src_repos->format;

  SVN_ERR(create_locks(dst_repos, pool));

  SVN_ERR(svn_io_dir_make_sgid(dst_repos->db_path, APR_OS_DEFAULT, pool));

  /* Exclusively lock the new repository.  
     No one should be accessing it at the moment */ 
  SVN_ERR(lock_repos(dst_repos, TRUE, FALSE, pool));

  SVN_ERR(svn_fs_hotcopy(src_repos->db_path, dst_repos->db_path,
                         clean_logs, pool));

  /* Destination repository is ready.  Stamp it with a format number. */
  SVN_ERR(svn_io_write_version_file 
          (svn_path_join(dst_repos->path, SVN_REPOS__FORMAT, pool),
           dst_repos->format, pool));

  return SVN_NO_ERROR;
}

/* Return the library version number. */
const svn_version_t *
svn_repos_version(void)
{
  SVN_VERSION_BODY;
}



svn_error_t *
svn_repos_stat(svn_dirent_t **dirent,
               svn_fs_root_t *root,
               const char *path,
               apr_pool_t *pool)
{
  svn_node_kind_t kind;
  svn_dirent_t *ent;
  const char *datestring;
  apr_hash_t *prophash;

  SVN_ERR(svn_fs_check_path(&kind, root, path, pool));
  
  if (kind == svn_node_none)
    {
      *dirent = NULL;
      return SVN_NO_ERROR;
    }

  ent = apr_pcalloc(pool, sizeof(*ent));
  ent->kind = kind;

  if (kind == svn_node_file)
    SVN_ERR(svn_fs_file_length(&(ent->size), root, path, pool));

  SVN_ERR(svn_fs_node_proplist(&prophash, root, path, pool));
  if (apr_hash_count(prophash) > 0)
    ent->has_props = TRUE;
  
  SVN_ERR(svn_repos_get_committed_info(&(ent->created_rev),
                                       &datestring,
                                       &(ent->last_author),
                                       root, path, pool));
  if (datestring)
    SVN_ERR(svn_time_from_cstring(&(ent->time), datestring, pool));

  *dirent = ent;
  return SVN_NO_ERROR;
}
