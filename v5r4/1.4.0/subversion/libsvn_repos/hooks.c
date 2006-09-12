/* hooks.c : running repository hooks
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

#ifdef AS400
#include <apr_portable.h>
#include <spawn.h>
#include <fcntl.h>
#endif

#include "svn_error.h"
#include "svn_path.h"
#include "svn_repos.h"
#include "svn_utf.h"
#include "repos.h"
#include "svn_private_config.h"



/*** Hook drivers. ***/

/* NAME, CMD and ARGS are the name, path to and arguments for the hook
   program that is to be run.  If READ_ERRSTREAM is TRUE then the hook's
   exit status will be checked, and if an error occurred the hook's stderr
   output will be added to the returned error.

   If READ_ERRSTREAM is FALSE the hook's stderr output will be discarded.

   If STDIN_HANDLE is non-null, pass it as the hook's stdin, else pass
   no stdin to the hook. */
static svn_error_t *
run_hook_cmd(const char *name,
             const char *cmd,
             const char **args,
             svn_boolean_t read_errstream,
             apr_file_t *stdin_handle,
             apr_pool_t *pool)
#ifndef AS400
{
  apr_file_t *read_errhandle, *write_errhandle, *null_handle;
  apr_status_t apr_err;
  svn_error_t *err;
  int exitcode;
  apr_exit_why_e exitwhy;
  apr_proc_t cmd_proc;

  /* Create a pipe to access stderr of the child. */
  apr_err = apr_file_pipe_create(&read_errhandle, &write_errhandle, pool);
  if (apr_err)
    return svn_error_wrap_apr
      (apr_err, _("Can't create pipe for hook '%s'"), cmd);

  /* Pipes are inherited by default, but we don't want that, since
     APR will duplicate the write end of the pipe for the child process.
     Not closing the read end is harmless, but if the write end is inherited,
     it will be inherited by grandchildren as well.  This causes problems
     if a hook script puts long-running jobs in the background.  Even if
     they redirect stderr to something else, the write end of our pipe will
     still be open, causing us to block. */
  apr_err = apr_file_inherit_unset(read_errhandle);
  if (apr_err)
    return svn_error_wrap_apr
      (apr_err, _("Can't make pipe read handle non-inherited for hook '%s'"),
       cmd);

  apr_err = apr_file_inherit_unset(write_errhandle);
  if (apr_err)
    return svn_error_wrap_apr
      (apr_err, _("Can't make pipe write handle non-inherited for hook '%s'"),
       cmd);


  /* Redirect stdout to the null device */
  apr_err = apr_file_open(&null_handle, SVN_NULL_DEVICE_NAME, APR_WRITE,
                          APR_OS_DEFAULT, pool);
  if (apr_err)
    return svn_error_wrap_apr
      (apr_err, _("Can't create null stdout for hook '%s'"), cmd);

  err = svn_io_start_cmd(&cmd_proc, ".", cmd, args, FALSE,
                         stdin_handle, null_handle, write_errhandle, pool);

  /* This seems to be done automatically if we pass the third parameter of
     apr_procattr_child_in/out_set(), but svn_io_run_cmd()'s interface does
     not support those parameters. We need to close the write end of the
     pipe so we don't hang on the read end later, if we need to read it. */
  apr_err = apr_file_close(write_errhandle);
  if (!err && apr_err)
    return svn_error_wrap_apr
      (apr_err, _("Error closing write end of stderr pipe"));

  if (err)
    {
      err = svn_error_createf
        (SVN_ERR_REPOS_HOOK_FAILURE, err, _("Failed to start '%s' hook"), cmd);
    }
  else
    {
      svn_stringbuf_t *native_error;
      const char *error;
      svn_error_t *err2;

      err2 = svn_stringbuf_from_aprfile(&native_error, read_errhandle, pool);

      err = svn_io_wait_for_cmd(&cmd_proc, cmd, &exitcode, &exitwhy, pool);
      if (! err)
        {
          if (! APR_PROC_CHECK_EXIT(exitwhy) || exitcode != 0)
            {
              if (read_errstream && ! err2)
                {
                  err2 = svn_utf_cstring_to_utf8(&error, native_error->data,
                                                 pool);
                  if (! err2)
                    err = svn_error_createf
                      (SVN_ERR_REPOS_HOOK_FAILURE, err,
                       _("'%s' hook failed with error output:\n%s"),
                       name, error);
                }
              else
                {
                  err = svn_error_createf
                    (SVN_ERR_REPOS_HOOK_FAILURE, err,
                     _("'%s' hook failed; no error output available"), name);
                }
            }
        }
      if (err2)
        {
          if (err)
            svn_error_clear(err2);
          else
            err = err2;
        }
    }

  /* Hooks are fallible, and so hook failure is "expected" to occur at
     times.  When such a failure happens we still want to close the pipe
     and null file */
  apr_err = apr_file_close(read_errhandle);
  if (!err && apr_err)
    return svn_error_wrap_apr
      (apr_err, _("Error closing read end of stderr pipe"));

  apr_err = apr_file_close(null_handle);
  if (!err && apr_err)
    return svn_error_wrap_apr(apr_err, _("Error closing null file"));

  return err;
}
#else /* Run hooks with spawn() on OS400. */
#define AS400_BUFFER_SIZE 256
{
  const char *script_stderr_utf8 = "";
  const char **native_args;
  int fd_map[3], stderr_pipe[2], exitcode;
  svn_stringbuf_t *script_output = svn_stringbuf_create("", pool);
  pid_t child_pid, wait_rv;
  apr_size_t args_arr_size = 0, i;
  struct inheritance xmp_inherit = {0};
#pragma convert(0)
  /* Despite the UTF support in V5R4 a few functions still require
   * EBCDIC args. */
  char *xmp_envp[2] = {"QIBM_USE_DESCRIPTOR_STDIO=Y", NULL};
  const char *dev_null_ebcdic = SVN_NULL_DEVICE_NAME;
#pragma convert(1208)

  /* Find number of elements in args array. */
  while (args[args_arr_size] != NULL)
    args_arr_size++;

  /* Allocate memory for the native_args string array plus one for
   * the ending null element. */
  native_args = apr_palloc(pool, sizeof(char *) * args_arr_size + 1);

  /* Convert UTF-8 args to EBCDIC for use by spawn(). */
  for (i = 0; args[i] != NULL; i++)
    {
      SVN_ERR(svn_utf_cstring_from_utf8_ex2((const char**)(&(native_args[i])),
                                            args[i], (const char *)0,
                                            pool));
    }

  /* Make the last element in the array a NULL pointer as required
   * by spawn. */
  native_args[args_arr_size] = NULL;

  /* Map stdin. */
  if (stdin_handle)
    {
      /* Get OS400 file descriptor of APR stdin file and map it. */
      if (apr_os_file_get(&fd_map[0], stdin_handle))
        {
          return svn_error_createf(SVN_ERR_EXTERNAL_PROGRAM, NULL,
                                   "Error converting APR file to OS400 "
                                   "type for hook script '%s'", cmd);
        }
    }
  else
    {
      fd_map[0] = open(dev_null_ebcdic, O_RDONLY);
      if (fd_map[0] == -1)

        return svn_error_createf(SVN_ERR_EXTERNAL_PROGRAM, NULL,
                                 "Error opening /dev/null for hook "
                                 "script '%s'", cmd);
    }


  /* Map stdout. */
  fd_map[1] = open(dev_null_ebcdic, O_WRONLY);
  if (fd_map[1] == -1)
    return svn_error_createf(SVN_ERR_EXTERNAL_PROGRAM, NULL,
                             "Error opening /dev/null for hook script '%s'",
                             cmd);

  /* Map stderr. */
  if (read_errstream)
    {
      /* Get pipe for hook's stderr. */
      if (pipe(stderr_pipe) != 0)
        {
          return svn_error_createf(SVN_ERR_EXTERNAL_PROGRAM, NULL,
                                   "Can't create stderr pipe for "
                                   "hook '%s'", cmd);
        }
      fd_map[2] = stderr_pipe[1];
    }
  else
    {
      /* Just dump stderr to /dev/null if we don't want it. */
      fd_map[2] = open(dev_null_ebcdic, O_WRONLY);
      if (fd_map[2] == -1)
        return svn_error_createf(SVN_ERR_EXTERNAL_PROGRAM, NULL,
                                 "Error opening /dev/null for hook "
                                 "script '%s'", cmd);
    }

  /* Spawn the hook command. */
  child_pid = spawn(native_args[0], 3, fd_map, &xmp_inherit, native_args,
                    xmp_envp);
  if (child_pid == -1)
    {
      return svn_error_createf(SVN_ERR_EXTERNAL_PROGRAM, NULL,
                               "Error spawning process for hook script '%s'",
                               cmd);
    }

  /* Close the stdout file descriptor. */
  if (close(fd_map[1]) == -1)
    return svn_error_createf(SVN_ERR_EXTERNAL_PROGRAM, NULL,
                             "Error closing write end of stdout pipe to "
                             "hook script '%s'", cmd);

  /* Close the write end of the stderr pipe so any subsequent reads
   * don't hang. */  
  if (close(fd_map[2]) == -1)
    return svn_error_createf(SVN_ERR_EXTERNAL_PROGRAM, NULL,
                             "Error closing write end of stderr pipe to "
                             "hook script '%s'", cmd);

  while (read_errstream)
    {
      int rc;

      svn_stringbuf_ensure(script_output,
                           script_output->len + AS400_BUFFER_SIZE + 1);

      rc = read(stderr_pipe[0],
                &(script_output->data[script_output->len]),
                AS400_BUFFER_SIZE);

      if (rc == -1)
        {
          return svn_error_createf(SVN_ERR_EXTERNAL_PROGRAM, NULL,
                                   "Error reading stderr of hook "
                                   "script '%s'", cmd);
        }

      script_output->len += rc;

      /* If read() returned 0 then EOF was found and we are done reading
       * stderr. */
      if (rc == 0)
        {
          /* Null terminate the stringbuf. */
          script_output->data[script_output->len] = '\0';
          break;
        }
    }

  /* Close the read end of the stderr pipe. */
  if (read_errstream && close(stderr_pipe[0]) == -1)
    return svn_error_createf(SVN_ERR_EXTERNAL_PROGRAM, NULL,
                             "Error closing read end of stderr "
                             "pipe to hook script '%s'", cmd);

  /* Wait for the child process to complete. */
  wait_rv = waitpid(child_pid, &exitcode, 0);
  if (wait_rv == -1)
    {
      return svn_error_createf(SVN_ERR_EXTERNAL_PROGRAM, NULL,
                               "Error waiting for process completion of "
                               "hook script '%s'", cmd);
    }

  if (!svn_stringbuf_isempty(script_output))
    {
      /* OS400 scripts produce EBCDIC stderr, so convert it. */
      SVN_ERR(svn_utf_cstring_to_utf8_ex2(&script_stderr_utf8,
                                          script_output->data,
                                          (const char*)0, pool));
    }

  if (WIFEXITED(exitcode))
    {
      if (WEXITSTATUS(exitcode))
        {
          if (read_errstream)
            {
              return svn_error_createf(SVN_ERR_EXTERNAL_PROGRAM, NULL,
                                       "'%s' hook failed with error "
                                       "output:\n%s", name,
                                       script_stderr_utf8);
            }
          else
            {
              return svn_error_createf(SVN_ERR_EXTERNAL_PROGRAM, NULL,
                                       "'%s' hook failed; no error output "
                                       "available", name);
            }
        }
      else
        /* Success! */
        return SVN_NO_ERROR;
    }
  else if (WIFSIGNALED(exitcode))
    {
      return svn_error_createf(SVN_ERR_EXTERNAL_PROGRAM, NULL,
                               "Process '%s' failed because of an "
                               "uncaught terminating signal", cmd);
    }
  else if (WIFEXCEPTION(exitcode))
    {
      return svn_error_createf(SVN_ERR_EXTERNAL_PROGRAM, NULL,
                               "Process '%s' failed unexpectedly with "
                               "OS400 exception %d", cmd,
                               WEXCEPTNUMBER(exitcode));
    }
  else if (WIFSTOPPED(exitcode))
    {
      return svn_error_createf(SVN_ERR_EXTERNAL_PROGRAM, NULL,
                               "Process '%s' stopped unexpectedly by "
                               "signal %d", cmd, WSTOPSIG(exitcode));
    }
  else
    {
      return svn_error_createf(SVN_ERR_EXTERNAL_PROGRAM, NULL,
                               "Process '%s' failed unexpectedly", cmd);
    }
}
#endif /* AS400 */


/* Create a temporary file F that will automatically be deleted when it is
   closed.  Fill it with VALUE, and leave it open and rewound, ready to be
   read from. */
static svn_error_t *
create_temp_file(apr_file_t **f, const svn_string_t *value, apr_pool_t *pool)
{
  const char *dir;
  apr_off_t offset = 0;

  SVN_ERR(svn_io_temp_dir(&dir, pool));
  SVN_ERR(svn_io_open_unique_file2(f, NULL,
                                   svn_path_join(dir, "hook-input", pool),
                                   "", svn_io_file_del_on_close, pool));
  SVN_ERR(svn_io_file_write_full(*f, value->data, value->len, NULL, pool));
  SVN_ERR(svn_io_file_seek(*f, APR_SET, &offset, pool));
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
check_hook_cmd(const char *hook, svn_boolean_t *broken_link, apr_pool_t *pool)
{
  static const char* const check_extns[] = {
#ifdef WIN32
  /* For WIN32, we need to check with file name extension(s) added.

     As Windows Scripting Host (.wsf) files can accomodate (at least)
     JavaScript (.js) and VB Script (.vbs) code, extensions for the
     corresponding file types need not be enumerated explicitly. */
    ".exe", ".cmd", ".bat", ".wsf", /* ### Any other extensions? */
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
        (**extn ? apr_pstrcat(pool, hook, *extn, 0) : hook);
      
      svn_node_kind_t kind;
      if (!(err = svn_io_check_resolved_path(hook_path, &kind, pool))
          && kind == svn_node_file)
        {
          *broken_link = FALSE;
          return hook_path;
        }
      svn_error_clear(err);
      if (!(err = svn_io_check_special_path(hook_path, &kind, &is_special,
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
hook_symlink_error(const char *hook)
{
  return svn_error_createf
    (SVN_ERR_REPOS_HOOK_FAILURE, NULL,
     _("Failed to run '%s' hook; broken symlink"), hook);
}

svn_error_t *
svn_repos__hooks_start_commit(svn_repos_t *repos,
                              const char *user,
                              apr_pool_t *pool)
{
  const char *hook = svn_repos_start_commit_hook(repos, pool);
  svn_boolean_t broken_link;
  
  if ((hook = check_hook_cmd(hook, &broken_link, pool)) && broken_link)
    {
      return hook_symlink_error(hook);
    }
  else if (hook)
    {
      const char *args[4];

      args[0] = hook;
      args[1] = svn_repos_path(repos, pool);
      args[2] = user ? user : "";
      args[3] = NULL;

      SVN_ERR(run_hook_cmd("start-commit", hook, args, TRUE, NULL, pool));
    }

  return SVN_NO_ERROR;
}


svn_error_t  *
svn_repos__hooks_pre_commit(svn_repos_t *repos,
                            const char *txn_name,
                            apr_pool_t *pool)
{
  const char *hook = svn_repos_pre_commit_hook(repos, pool);
  svn_boolean_t broken_link;

  if ((hook = check_hook_cmd(hook, &broken_link, pool)) && broken_link)
    {
      return hook_symlink_error(hook);
    }
  else if (hook)
    {
      const char *args[4];

      args[0] = hook;
      args[1] = svn_repos_path(repos, pool);
      args[2] = txn_name;
      args[3] = NULL;

      SVN_ERR(run_hook_cmd("pre-commit", hook, args, TRUE, NULL, pool));
    }

  return SVN_NO_ERROR;
}


svn_error_t  *
svn_repos__hooks_post_commit(svn_repos_t *repos,
                             svn_revnum_t rev,
                             apr_pool_t *pool)
{
  const char *hook = svn_repos_post_commit_hook(repos, pool);
  svn_boolean_t broken_link;

  if ((hook = check_hook_cmd(hook, &broken_link, pool)) && broken_link)
    {
      return hook_symlink_error(hook);
    }
  else if (hook)
    {
      const char *args[4];

      args[0] = hook;
      args[1] = svn_repos_path(repos, pool);
      args[2] = apr_psprintf(pool, "%ld", rev);
      args[3] = NULL;

      SVN_ERR(run_hook_cmd("post-commit", hook, args, TRUE, NULL, pool));
    }

  return SVN_NO_ERROR;
}


svn_error_t  *
svn_repos__hooks_pre_revprop_change(svn_repos_t *repos,
                                    svn_revnum_t rev,
                                    const char *author,
                                    const char *name,
                                    const svn_string_t *new_value,
                                    char action,
                                    apr_pool_t *pool)
{
  const char *hook = svn_repos_pre_revprop_change_hook(repos, pool);
  svn_boolean_t broken_link;

  if ((hook = check_hook_cmd(hook, &broken_link, pool)) && broken_link)
    {
      return hook_symlink_error(hook);
    }
  else if (hook)
    {
      const char *args[7];
      apr_file_t *stdin_handle = NULL;
      char action_string[2];

      /* Pass the new value as stdin to hook */
      if (new_value)
        SVN_ERR(create_temp_file(&stdin_handle, new_value, pool));
      else
        SVN_ERR(svn_io_file_open(&stdin_handle, SVN_NULL_DEVICE_NAME,
                                 APR_READ, APR_OS_DEFAULT, pool));

      action_string[0] = action;
      action_string[1] = '\0';

      args[0] = hook;
      args[1] = svn_repos_path(repos, pool);
      args[2] = apr_psprintf(pool, "%ld", rev);
      args[3] = author ? author : "";
      args[4] = name;
      args[5] = action_string;
      args[6] = NULL;

      SVN_ERR(run_hook_cmd("pre-revprop-change", hook, args, TRUE,
                           stdin_handle, pool));

      SVN_ERR(svn_io_file_close(stdin_handle, pool));
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
svn_repos__hooks_post_revprop_change(svn_repos_t *repos,
                                     svn_revnum_t rev,
                                     const char *author,
                                     const char *name,
                                     svn_string_t *old_value,
                                     char action,
                                     apr_pool_t *pool)
{
  const char *hook = svn_repos_post_revprop_change_hook(repos, pool);
  svn_boolean_t broken_link;
  
  if ((hook = check_hook_cmd(hook, &broken_link, pool)) && broken_link)
    {
      return hook_symlink_error(hook);
    }
  else if (hook)
    {
      const char *args[7];
      apr_file_t *stdin_handle = NULL;
      char action_string[2];

      /* Pass the old value as stdin to hook */
      if (old_value)
        SVN_ERR(create_temp_file(&stdin_handle, old_value, pool));
      else
        SVN_ERR(svn_io_file_open(&stdin_handle, SVN_NULL_DEVICE_NAME,
                                 APR_READ, APR_OS_DEFAULT, pool));

      action_string[0] = action;
      action_string[1] = '\0';

      args[0] = hook;
      args[1] = svn_repos_path(repos, pool);
      args[2] = apr_psprintf(pool, "%ld", rev);
      args[3] = author ? author : "";
      args[4] = name;
      args[5] = action_string;
      args[6] = NULL;

      SVN_ERR(run_hook_cmd("post-revprop-change", hook, args, FALSE,
                           stdin_handle, pool));
      
      SVN_ERR(svn_io_file_close(stdin_handle, pool));
    }

  return SVN_NO_ERROR;
}



svn_error_t  *
svn_repos__hooks_pre_lock(svn_repos_t *repos,
                          const char *path,
                          const char *username,
                          apr_pool_t *pool)
{
  const char *hook = svn_repos_pre_lock_hook(repos, pool);
  svn_boolean_t broken_link;

  if ((hook = check_hook_cmd(hook, &broken_link, pool)) && broken_link)
    {
      return hook_symlink_error(hook);
    }
  else if (hook)
    {
      const char *args[5];

      args[0] = hook;
      args[1] = svn_repos_path(repos, pool);
      args[2] = path;
      args[3] = username;
      args[4] = NULL;

      SVN_ERR(run_hook_cmd("pre-lock", hook, args, TRUE, NULL, pool));
    }

  return SVN_NO_ERROR;
}


svn_error_t  *
svn_repos__hooks_post_lock(svn_repos_t *repos,
                           apr_array_header_t *paths,
                           const char *username,
                           apr_pool_t *pool)
{
  const char *hook = svn_repos_post_lock_hook(repos, pool);
  svn_boolean_t broken_link;
  
  if ((hook = check_hook_cmd(hook, &broken_link, pool)) && broken_link)
    {
      return hook_symlink_error(hook);
    }
  else if (hook)
    {
      const char *args[5];
      apr_file_t *stdin_handle = NULL;
      svn_string_t *paths_str = svn_string_create(svn_cstring_join
                                                  (paths, "\n", pool), 
                                                  pool);

      SVN_ERR(create_temp_file(&stdin_handle, paths_str, pool));

      args[0] = hook;
      args[1] = svn_repos_path(repos, pool);
      args[2] = username;
      args[3] = NULL;
      args[4] = NULL;

      SVN_ERR(run_hook_cmd("post-lock", hook, args, FALSE, 
                           stdin_handle, pool));

      SVN_ERR(svn_io_file_close(stdin_handle, pool));
    }

  return SVN_NO_ERROR;
}


svn_error_t  *
svn_repos__hooks_pre_unlock(svn_repos_t *repos,
                            const char *path,
                            const char *username,
                            apr_pool_t *pool)
{
  const char *hook = svn_repos_pre_unlock_hook(repos, pool);
  svn_boolean_t broken_link;

  if ((hook = check_hook_cmd(hook, &broken_link, pool)) && broken_link)
    {
      return hook_symlink_error(hook);
    }
  else if (hook)
    {
      const char *args[5];

      args[0] = hook;
      args[1] = svn_repos_path(repos, pool);
      args[2] = path;
      args[3] = username ? username : "";
      args[4] = NULL;

      SVN_ERR(run_hook_cmd("pre-unlock", hook, args, TRUE, NULL, pool));
    }

  return SVN_NO_ERROR;
}


svn_error_t  *
svn_repos__hooks_post_unlock(svn_repos_t *repos,
                             apr_array_header_t *paths,
                             const char *username,
                             apr_pool_t *pool)
{
  const char *hook = svn_repos_post_unlock_hook(repos, pool);
  svn_boolean_t broken_link;
  
  if ((hook = check_hook_cmd(hook, &broken_link, pool)) && broken_link)
    {
      return hook_symlink_error(hook);
    }
  else if (hook)
    {
      const char *args[5];
      apr_file_t *stdin_handle = NULL;
      svn_string_t *paths_str = svn_string_create(svn_cstring_join
                                                  (paths, "\n", pool), 
                                                  pool);

      SVN_ERR(create_temp_file(&stdin_handle, paths_str, pool));

      args[0] = hook;
      args[1] = svn_repos_path(repos, pool);
      args[2] = username ? username : "";
      args[3] = NULL;
      args[4] = NULL;

      SVN_ERR(run_hook_cmd("post-unlock", hook, args, FALSE, 
                           stdin_handle, pool));

      SVN_ERR(svn_io_file_close(stdin_handle, pool));
    }

  return SVN_NO_ERROR;
}



/* 
 * vim:ts=4:sw=4:expandtab:tw=80:fo=tcroq 
 * vim:isk=a-z,A-Z,48-57,_,.,-,> 
 * vim:cino=>1s,e0,n0,f0,{.5s,}0,^-.5s,=.5s,t0,+1s,c3,(0,u0,\:0
 */
