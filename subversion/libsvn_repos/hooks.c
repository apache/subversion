/* hooks.c : running repository hooks and sentinels
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

#include <stdio.h>
#include <string.h>

#include "apr_pools.h"
#include "apr_file_io.h"

#include "svn_pools.h"
#include "svn_error.h"
#include "svn_path.h"
#include "svn_delta.h"
#include "svn_fs.h"
#include "svn_repos.h"


/* In the code below, "hook" is sometimes used indiscriminately to
   mean either hook or sentinel.  */


/*** Hook/sentinel file parsing. ***/

/* Read the next non-comment line from HOOK_FILE, setting *CMD_P to
   the command with its arguments, ready to be run by system().
   HOOK_FILE is already open, and is not closed.

   When constructing arguments, expand "$user" to USER, "$rev" to REV,
   "$txn" to TXN_NAME, and "$repos" to REPOS.  If expansion is
   attempted on a null USER, TXN_NAME, or REV, then return
   SVN_ERR_REPOS_HOOK_FAILURE.

   After the last line of HOOK_FILE has been read, the next call sets
   *CMD_P to null.

   *CMD_P is allocated in POOL, which is also used for any temporary
   allocations.  */
static svn_error_t *
read_hook_line (char **cmd_p,
                apr_file_t *hook_file,
                const char *repos,
                const char *user,
                const char *rev,
                const char *txn_name,
                apr_pool_t *pool)
{
  char buf[APR_PATH_MAX];
  apr_size_t idx, len;
  char c;
  int i;
  apr_status_t apr_err;
  apr_array_header_t *args = apr_array_make (pool, 4, sizeof (*cmd_p));
  const char *hook_file_path;  /* for error msgs */
  
  int
    done = 0,
    escaped = 0,
    commented = 0,
    suppress_expansion = 0,
    suppress_comment_effect = 0,
    suppress_escape_effect = 0;
              
  /* Get the hook's file name, for use in error messages. */
  apr_err = apr_file_name_get (&hook_file_path, hook_file);
  if (! APR_STATUS_IS_SUCCESS (apr_err))
    return svn_error_create
      (apr_err, 0, NULL, pool,
       "read_hook_line: error getting hook file name");
    
 restart:

  idx = 0;
  done = 0;
  commented = 0;
  while (1)
    {
      apr_err = apr_file_getc (&c, hook_file);

      if ((! APR_STATUS_IS_SUCCESS (apr_err))
          && (APR_STATUS_IS_EOF (apr_err)))   /* reached end of file */
        {
          if (idx > 0)
            {
              c = '\n';  /* fake a newline */
              done = 1;
            }
          else
            *cmd_p = NULL;

          return SVN_NO_ERROR;
        }
      else if (! APR_STATUS_IS_SUCCESS (apr_err))   /* error other than eof */
        {
          return svn_error_createf 
            (apr_err, 0, NULL, pool,
             "read_hook_line: error reading line from `%s'", hook_file_path);
        }

      /* Else, we got another char in the line. */

      /* Sanity check: is this line overly long? */
      if (idx >= APR_PATH_MAX)
        return svn_error_createf 
          (apr_err, 0, NULL, pool,
           "read_hook_line: line too long in `%s'", hook_file_path);
        
      if (escaped)
        {
          /* The char before this one was backslash, the escape
             character.  That means this char has been escaped;
             depending on what it is, we might have to handle it
             specially. */

          switch (c)
            {
            case '\n':
              c = ' ';
            case '$':
              suppress_expansion = 1;
            case '#':
              suppress_comment_effect = 1;
            case '\\':
              suppress_escape_effect = 1;
            }

          escaped = 0;
        }

      /* Proceed, having handled or prepared any escapes on this char. */

      /* ### todo: when have test suite from Mike, change this to
         check for `\\' outside the switch below, and if it is an
         escape, then just read the next char right away.  Then we can
         get rid of the whole `escaped' switch block above. 

         Likewise, check for comment char.  If have it, then call a
         new function, eat_to_end_of_line(), then restart.  There's no
         reason this whole state machine has to be done inline and
         unrolled, the way it is right now.

         We can do expansion the same way: see a dollar sign, expand
         right away.  That will give us more flexible syntax in the
         conf file anyway. */

      switch (c)
        {
        case '\\':
          if (suppress_escape_effect)
            {
              suppress_escape_effect = 0;
              goto add_it;
            }
          else
            escaped = 1;
          break;
          
        case '#':
          if (suppress_comment_effect)
            {
              suppress_comment_effect = 0;
              goto add_it;
            }
          else
            commented = 1;
          break;
          
        case '\n':
          done = 1;
        /* fallthru */
        case ' ':
        case '\t':
          if (idx > 0)   /* convert the buffered data  */
            {
              buf[idx] = '\0';

              if (! suppress_expansion)
                {
                  /* ### todo: error check expanded length here */

                  if (strcmp (buf, "$user") == 0)
                    {
                      if (user)
                        strcpy (buf, user);
                      else
                        goto expansion_error;
                    }
                  else if (strcmp (buf, "$txn") == 0)
                    {
                      if (txn_name)
                        strcpy (buf, txn_name);
                      else
                        goto expansion_error;
                    }
                  else if (strcmp (buf, "$rev") == 0)
                    {
                      if (rev)
                        strcpy (buf, rev);
                      else
                        goto expansion_error;
                    }
                  else if (strcmp (buf, "$repos") == 0)
                    {
                      if (repos)
                        strcpy (buf, repos);
                      else
                        goto expansion_error;
                    }
                  else if (buf[0] == '$')
                    {
                    expansion_error:
                      return svn_error_createf
                        (SVN_ERR_REPOS_HOOK_FAILURE, 0, NULL, pool,
                         "read_hook_line: error expanding `%s' in `%s'",
                         buf, hook_file_path);
                    }
                }

              *((const char **)apr_array_push (args))
                = apr_pstrdup (pool, buf);

              /* Reset. */
              idx = 0;
              suppress_expansion = 0;
            }

          break;

        default:
        add_it:
          if (! commented)
            buf[idx++] = c;
        }

      if (done)
        break;
    }

  /* If no uncommented text here, move on to the next line. */
  if (args->nelts == 0)
    goto restart;

  /* Compute the length needed. */
  len = 0;
  for (i = 0; i < args->nelts; i++)
    len += (strlen (((char **) args->elts)[i])) + 1;

  /* Allocate it. */
  *cmd_p = apr_pcalloc (pool, len);

  /* Copy the command and args. */
  for (i = 0; i < args->nelts; i++)
    {
      const char *this = ((char **) args->elts)[i];
      strcat (*cmd_p, this);
      strcat (*cmd_p, " ");
    }

  /* Change the last space into a null terminator. */
  (*cmd_p)[len - 1] = '\0';

  return SVN_NO_ERROR;
}


/* Run the hooks or sentinels in HOOK_FILE.  If STOP_IF_FAIL is true,
   then return SVN_ERR_REPOS_HOOK_FAILURE immediately on any hook
   failing, or SVN_NO_ERROR if none failed.  Else if it is false, run
   all hooks no matter what, and return SVN_ERR_REPOS_HOOK_FAILURE if
   any of them failed (nesting errors if there are multiple failures),
   or SVN_NO_ERROR if none failed.

   HOOK_FILE is the full path to a hook or sentinel configuration file
   for FS.  Use svn_fs_start_commit_conf(), svn_fs_pre_commit_conf(),
   etc, to obtain a hook or sentinel file given an fs.

   For each hook's configuration line, expand "$user" to USER, "$rev"
   to a string representation of REV, "$txn" to TXN_NAME, and "$repos"
   to the repository path for FS.  If expansion is attempted on a null
   USER or TXN_NAME, or on an invalid REV, return
   SVN_ERR_REPOS_HOOK_FAILURE immediately without running the hook.  */
static svn_error_t *
run_hook_file (svn_fs_t *fs,
               const char *hook_file,
               svn_boolean_t stop_if_fail,
               const char *user,
               svn_revnum_t rev,
               const char *txn_name,
               apr_pool_t *pool)
{
  svn_error_t *err, *accum_err = NULL;
  apr_status_t apr_err;
  char *cmd;
  apr_file_t *f;
  const char *repos = svn_fs_repository (fs);
  const char *rev_str;

  if (SVN_IS_VALID_REVNUM (rev))
    rev_str = apr_psprintf (pool, "%ld", rev);
  else
    rev_str = NULL;
  
  apr_err = apr_file_open (&f, hook_file, APR_READ, APR_OS_DEFAULT, pool);
  if (apr_err)
    return svn_error_createf (apr_err, 0, NULL, pool,
                              "run_hook_file: opening `%s'", hook_file);
  
  while (1)
    {
      err = read_hook_line (&cmd,
                            f,
                            repos,
                            user,
                            rev_str,
                            txn_name,
                            pool);
      
      if (! cmd)
        break;
      else if (err)
        {
          svn_error_t *new_err
            = svn_error_createf (SVN_ERR_REPOS_HOOK_FAILURE, 0, err, pool,
                                 "run_hook_file: running cmd `%s' "
                                 "from file `%s'",
                                 cmd, hook_file);

          if (accum_err)
            svn_error_compose (accum_err, new_err);
          else
            accum_err = err;

          if (stop_if_fail)
            break;
        }
      else  /* no error reading the command */
        {
          int ret;

          /* ### todo: not ideal, but for now by far the easiest way
             to get what we want. */
          ret = system (cmd);
          if (ret)
            return svn_error_createf (SVN_ERR_REPOS_HOOK_FAILURE,
                                      0, err, pool,
                                      "run_hook_file: running cmd `%s' "
                                      "from file `%s'",
                                      cmd, hook_file);
        }
    }

  apr_err = apr_file_close (f);
  if (apr_err)
    return svn_error_createf (apr_err, 0, NULL, pool,
                              "run_hook_file: closing `%s'", hook_file);

  return accum_err;
}



/*** Hook drivers. ***/

/* Run the start-commit hooks for FS, expanding "$user" to USER and
   "$repos" to the repository path for FS.  Use POOL for any temporary
   allocations.  If any of the hooks fail, return the error
   SVN_ERR_REPOS_HOOK_FAILURE.  */
static svn_error_t  *
run_start_commit_hooks (svn_fs_t *fs,
                        const char *user,
                        apr_pool_t *pool)
{
  SVN_ERR (run_hook_file (fs,
                          svn_fs_start_commit_conf (fs, pool),
                          1,
                          user,
                          SVN_INVALID_REVNUM,
                          NULL,
                          pool));

  return SVN_NO_ERROR;
}


/* Run the pre-commit hooks for FS, expanding "$txn" to TXN_NAME.  Use
   POOL for any temporary allocations.  If any of the hooks fail,
   destroy the txn identified by TXN_NAME and return the error
   SVN_ERR_REPOS_HOOK_FAILURE.  */
static svn_error_t  *
run_pre_commit_hooks (svn_fs_t *fs,
                      const char *txn_name,
                      apr_pool_t *pool)
{
  SVN_ERR (run_hook_file (fs,
                          svn_fs_pre_commit_conf (fs, pool),
                          1,
                          NULL,
                          SVN_INVALID_REVNUM,
                          txn_name,
                          pool));

  return SVN_NO_ERROR;
}


/* Run the post-commit hooks for FS, expanding "$rev" to REV.  Use
   POOL for any temporary allocations.  All hooks are run regardless
   of failure, but if any hooks fails, return the error
   SVN_ERR_REPOS_HOOK_FAILURE.  */
static svn_error_t  *
run_post_commit_hooks (svn_fs_t *fs,
                       svn_revnum_t rev,
                       apr_pool_t *pool)
{
  SVN_ERR (run_hook_file (fs,
                          svn_fs_post_commit_conf (fs, pool),
                          0,
                          NULL,
                          rev,
                          NULL,
                          pool));

  return SVN_NO_ERROR;
}



/*** Public interface. ***/

svn_error_t *
svn_repos_fs_commit_txn (const char **conflict_p,
                         svn_revnum_t *new_rev,
                         svn_fs_txn_t *txn)
{
  svn_fs_t *fs = svn_fs_txn_fs (txn);
  apr_pool_t *pool = svn_fs_txn_pool (txn);

  /* Run pre-commit hooks. */
  {
    const char *txn_name;

    SVN_ERR (svn_fs_txn_name (&txn_name, txn, pool));
    SVN_ERR (run_pre_commit_hooks (fs, txn_name, pool));
  }

  /* Commit. */
  SVN_ERR (svn_fs_commit_txn (conflict_p, new_rev, txn));

  /* Run post-commit hooks. */
  SVN_ERR (run_post_commit_hooks (fs, *new_rev, pool));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_repos_fs_begin_txn_for_commit (svn_fs_txn_t **txn_p,
                                   svn_fs_t *fs,
                                   svn_revnum_t rev,
                                   const char *author,
                                   svn_string_t *log_msg,
                                   apr_pool_t *pool)
{
  /* Run start-commit hooks. */
  SVN_ERR (run_start_commit_hooks (fs, author, pool));

  /* Begin the transaction. */
  SVN_ERR (svn_fs_begin_txn (txn_p, fs, rev, pool));

  /* We pass the author and log message to the filesystem by adding
     them as properties on the txn.  Later, when we commit the txn,
     these properties will be copied into the newly created revision. */
  {
    svn_string_t log_prop_name = { SVN_PROP_REVISION_LOG,
                                   sizeof(SVN_PROP_REVISION_LOG) - 1};
    svn_string_t author_prop_name = { SVN_PROP_REVISION_AUTHOR,
                                      sizeof(SVN_PROP_REVISION_AUTHOR) - 1};

    /* User (author). */
    {
      svn_string_t val;
      val.data = author;
      val.len = strlen (author);
      
      SVN_ERR (svn_fs_change_txn_prop (*txn_p, &author_prop_name,
                                       &val, pool));
    }
    
    /* Log message. */
    SVN_ERR (svn_fs_change_txn_prop (*txn_p, &log_prop_name,
                                     log_msg, pool));
  }

  return SVN_NO_ERROR;
}



/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
