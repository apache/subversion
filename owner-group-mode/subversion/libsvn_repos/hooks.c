/* hooks.c : running repository hooks and sentinels
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

#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include <apr_pools.h>
#include <apr_file_io.h>

#include "svn_error.h"
#include "svn_path.h"
#include "svn_repos.h"
#include "repos.h"
#include "svn_private_config.h"

/* In the code below, "hook" is sometimes used indiscriminately to
   mean either hook or sentinel.  */



/*** Hook drivers. ***/

/* NAME, CMD and ARGS are the name, path to and arguments for the hook
   program that is to be run.  If READ_ERRSTREAM is TRUE then the hook's
   exit status will be checked, and if an error occurred the hook's stderr
   output will be added to the returned error.

   If READ_ERRSTREAM is FALSE the hook's exit status will be ignored.

   If STDIN_HANDLE is non-null, pass it as the hook's stdin, else pass
   no stdin to the hook. */
static svn_error_t *
run_hook_cmd (const char *name,
              const char *cmd,
              const char **args,
              svn_boolean_t read_errstream,
              apr_file_t *stdin_handle,
              apr_pool_t *pool)
{
  apr_file_t *read_errhandle, *write_errhandle, *null_handle;
  apr_status_t apr_err;
  svn_error_t *err;
  int exitcode;
  apr_exit_why_e exitwhy;

  /* Create a pipe to access stderr of the child. */
  apr_err = apr_file_pipe_create(&read_errhandle, &write_errhandle, pool);
  if (apr_err)
    return svn_error_wrap_apr
      (apr_err, _("Can't create pipe for hook '%s'"), cmd);

  /* Redirect stdout to the null device */
  apr_err = apr_file_open (&null_handle, SVN_NULL_DEVICE_NAME, APR_WRITE,
                           APR_OS_DEFAULT, pool);
  if (apr_err)
    return svn_error_wrap_apr
      (apr_err, _("Can't create null stdout for hook '%s'"), cmd);

  err = svn_io_run_cmd (".", cmd, args, &exitcode, &exitwhy, FALSE,
                        stdin_handle, null_handle, write_errhandle, pool);

  /* This seems to be done automatically if we pass the third parameter of
     apr_procattr_child_in/out_set(), but svn_io_run_cmd()'s interface does
     not support those parameters. We need to close the write end of the
     pipe so we don't hang on the read end later, if we need to read it. */
  apr_err = apr_file_close (write_errhandle);
  if (!err && apr_err)
    return svn_error_wrap_apr
      (apr_err, _("Error closing write end of stderr pipe"));

  /* Function failed. */
  if (err)
    {
      err = svn_error_createf
        (SVN_ERR_REPOS_HOOK_FAILURE, err, _("Failed to run '%s' hook"), cmd);
    }

  if (!err)
    {
      /* Command failed. */
      if (! APR_PROC_CHECK_EXIT (exitwhy) || exitcode != 0)
        {
          svn_stringbuf_t *error;

          if (read_errstream)
            {
              /* Read the file's contents into a stringbuf, allocated
                 in POOL. */
              SVN_ERR (svn_stringbuf_from_aprfile (&error, read_errhandle, 
                                                   pool));
              err = svn_error_createf
                (SVN_ERR_REPOS_HOOK_FAILURE, err,
                 _("'%s' hook failed with error output:\n%s"),
                 name, error->data);
            }
          else
            {
              err = svn_error_createf
                (SVN_ERR_REPOS_HOOK_FAILURE, err,
                 _("'%s' hook failed; no error output available"), name);
            }
        }
    }

  /* Hooks are fallible, and so hook failure is "expected" to occur at
     times.  When such a failure happens we still want to close the pipe
     and null file */
  apr_err = apr_file_close (read_errhandle);
  if (!err && apr_err)
    return svn_error_wrap_apr
      (apr_err, _("Error closing read end of stderr pipe"));

  apr_err = apr_file_close (null_handle);
  if (!err && apr_err)
    return svn_error_wrap_apr (apr_err, _("Error closing null file"));

  return err;
}


/* Create a temporary file F that will automatically be deleted when it is
   closed.  Fill it with VALUE, and leave it open and rewound, ready to be
   read from. */
static svn_error_t *
create_temp_file (apr_file_t **f, const svn_string_t *value, apr_pool_t *pool)
{
  const char *dir, *fname;
  apr_off_t offset = 0;

  SVN_ERR (svn_io_temp_dir (&dir, pool));
  SVN_ERR (svn_io_open_unique_file (f, &fname,
                                    svn_path_join (dir, "hook-input", pool),
                                    "", TRUE /* delete on close */, pool));
  SVN_ERR (svn_io_file_write_full (*f, value->data, value->len, NULL, pool));
  SVN_ERR (svn_io_file_seek (*f, APR_SET, &offset, pool));
  return SVN_NO_ERROR;
}


/* Check if the HOOK program exists and is a file or a symbolic link, using
   POOL for temporary allocations. 

   If the hook exists but is a broken symbolic link, set *BROKEN_LINK
   to TRUE, else if the hook program exists set *BROKEN_LINK to FALSE.

   Return the hook program if found, else return NULL and don't touch
   *BROKEN_LINK.
*/
static const char*
check_hook_cmd (const char *hook, svn_boolean_t *broken_link, apr_pool_t *pool)
{
  static const char* const check_extns[] = {
#ifdef WIN32
  /* For WIN32 we need to check with an added extension(s). */
    ".exe", ".cmd", ".bat",  /* ### Any other extensions? */
#else
    "",
#endif
    NULL
  };

  const char *const *extn;
  svn_error_t *err = NULL;
  svn_boolean_t is_special;
  for (extn = check_extns; *extn; ++extn)
    {
      const char *const hook_path =
        (**extn ? apr_pstrcat (pool, hook, *extn, 0) : hook);
      
      svn_node_kind_t kind;
      if (!(err = svn_io_check_resolved_path (hook_path, &kind, pool))
          && kind == svn_node_file)
        {
          *broken_link = FALSE;
          return hook_path;
        }
      svn_error_clear(err);
      if (!(err = svn_io_check_special_path (hook_path, &kind, &is_special,
                                             pool))
          && is_special == TRUE)
        {
          *broken_link = TRUE;
          return hook_path;
        }
      svn_error_clear(err);
    }
  return NULL;
}


/* Return an error for the failure of HOOK due to a broken symlink. */
static svn_error_t *
hook_symlink_error (const char *hook)
{
  return svn_error_createf
    (SVN_ERR_REPOS_HOOK_FAILURE, NULL,
     _("Failed to run '%s' hook; broken symlink"), hook);
}

svn_error_t *
svn_repos__hooks_start_commit (svn_repos_t *repos,
                               const char *user,
                               apr_pool_t *pool)
{
  const char *hook = svn_repos_start_commit_hook (repos, pool);
  svn_boolean_t broken_link;
  
  if ((hook = check_hook_cmd (hook, &broken_link, pool)) && broken_link)
    {
      return hook_symlink_error (hook);
    }
  else if (hook)
    {
      const char *args[4];

      args[0] = hook;
      args[1] = svn_repos_path (repos, pool);
      args[2] = user ? user : "";
      args[3] = NULL;

      SVN_ERR (run_hook_cmd ("start-commit", hook, args, TRUE, NULL, pool));
    }

  return SVN_NO_ERROR;
}


svn_error_t  *
svn_repos__hooks_pre_commit (svn_repos_t *repos,
                             const char *txn_name,
                             apr_pool_t *pool)
{
  const char *hook = svn_repos_pre_commit_hook (repos, pool);
  svn_boolean_t broken_link;

  if ((hook = check_hook_cmd (hook, &broken_link, pool)) && broken_link)
    {
      return hook_symlink_error (hook);
    }
  else if (hook)
    {
      const char *args[4];

      args[0] = hook;
      args[1] = svn_repos_path (repos, pool);
      args[2] = txn_name;
      args[3] = NULL;

      SVN_ERR (run_hook_cmd ("pre-commit", hook, args, TRUE, NULL, pool));
    }

  return SVN_NO_ERROR;
}


svn_error_t  *
svn_repos__hooks_post_commit (svn_repos_t *repos,
                              svn_revnum_t rev,
                              apr_pool_t *pool)
{
  const char *hook = svn_repos_post_commit_hook (repos, pool);
  svn_boolean_t broken_link;

  if ((hook = check_hook_cmd (hook, &broken_link, pool)) && broken_link)
    {
      return hook_symlink_error (hook);
    }
  else if (hook)
    {
      const char *args[4];

      args[0] = hook;
      args[1] = svn_repos_path (repos, pool);
      args[2] = apr_psprintf (pool, "%ld", rev);
      args[3] = NULL;

      SVN_ERR (run_hook_cmd ("post-commit", hook, args, FALSE, NULL, pool));
    }

  return SVN_NO_ERROR;
}


svn_error_t  *
svn_repos__hooks_pre_revprop_change (svn_repos_t *repos,
                                     svn_revnum_t rev,
                                     const char *author,
                                     const char *name,
                                     const svn_string_t *new_value,
                                     char action,
                                     apr_pool_t *pool)
{
  const char *hook = svn_repos_pre_revprop_change_hook (repos, pool);
  svn_boolean_t broken_link;

  if ((hook = check_hook_cmd (hook, &broken_link, pool)) && broken_link)
    {
      return hook_symlink_error (hook);
    }
  else if (hook)
    {
      const char *args[7];
      apr_file_t *stdin_handle = NULL;
      char action_string[2];

      /* Pass the new value as stdin to hook */
      if (new_value)
        SVN_ERR (create_temp_file (&stdin_handle, new_value, pool));
      else
        SVN_ERR (svn_io_file_open (&stdin_handle, SVN_NULL_DEVICE_NAME,
                                   APR_READ, APR_OS_DEFAULT, pool));

      action_string[0] = action;
      action_string[1] = '\0';

      args[0] = hook;
      args[1] = svn_repos_path (repos, pool);
      args[2] = apr_psprintf (pool, "%ld", rev);
      args[3] = author ? author : "";
      args[4] = name;
      args[5] = action_string;
      args[6] = NULL;

      SVN_ERR (run_hook_cmd ("pre-revprop-change", hook, args, TRUE,
                             stdin_handle, pool));

      SVN_ERR (svn_io_file_close (stdin_handle, pool));
    }
  else
    {
      /* If the pre- hook doesn't exist at all, then default to
         MASSIVE PARANOIA.  Changing revision properties is a lossy
         operation; so unless the repository admininstrator has
         *deliberately* created the pre-hook, disallow all changes. */
      return 
        svn_error_create 
        (SVN_ERR_REPOS_DISABLED_FEATURE, NULL,
         _("Repository has not been enabled to accept revision propchanges;\n"
           "ask the administrator to create a pre-revprop-change hook"));
    }

  return SVN_NO_ERROR;
}


svn_error_t  *
svn_repos__hooks_post_revprop_change (svn_repos_t *repos,
                                      svn_revnum_t rev,
                                      const char *author,
                                      const char *name,
                                      svn_string_t *old_value,
                                      char action,
                                      apr_pool_t *pool)
{
  const char *hook = svn_repos_post_revprop_change_hook (repos, pool);
  svn_boolean_t broken_link;
  
  if ((hook = check_hook_cmd (hook, &broken_link, pool)) && broken_link)
    {
      return hook_symlink_error (hook);
    }
  else if (hook)
    {
      const char *args[7];
      apr_file_t *stdin_handle = NULL;
      char action_string[2];

      /* Pass the old value as stdin to hook */
      if (old_value)
        SVN_ERR (create_temp_file (&stdin_handle, old_value, pool));
      else
        SVN_ERR (svn_io_file_open (&stdin_handle, SVN_NULL_DEVICE_NAME,
                                   APR_READ, APR_OS_DEFAULT, pool));

      action_string[0] = action;
      action_string[1] = '\0';

      args[0] = hook;
      args[1] = svn_repos_path (repos, pool);
      args[2] = apr_psprintf (pool, "%ld", rev);
      args[3] = author ? author : "";
      args[4] = name;
      args[5] = action_string;
      args[6] = NULL;

      SVN_ERR (run_hook_cmd ("post-revprop-change", hook, args, FALSE,
                             stdin_handle, pool));
      
      SVN_ERR (svn_io_file_close (stdin_handle, pool));
    }

  return SVN_NO_ERROR;
}



/* 
 * vim:ts=4:sw=4:expandtab:tw=80:fo=tcroq 
 * vim:isk=a-z,A-Z,48-57,_,.,-,> 
 * vim:cino=>1s,e0,n0,f0,{.5s,}0,^-.5s,=.5s,t0,+1s,c3,(0,u0,\:0
 */
