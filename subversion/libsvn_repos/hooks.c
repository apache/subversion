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
#include <ctype.h>

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

/* Set *C to the next char from open file F.  
   If hit EOF, set *C to '\n', set *GOT_EOF to 1, and return success.
   If neither EOF nor error, set *GOT_EOF to 0.
   Use POOL for error allocation.  */
static svn_error_t *
read_char (char *c, int *got_eof, apr_file_t *f, apr_pool_t *pool)
{
  apr_status_t apr_err;

  apr_err = apr_file_getc (c, f);

  if (APR_STATUS_IS_EOF (apr_err))
    {
      *c = '\n';
      *got_eof = 1;
    }
  else if (! APR_STATUS_IS_SUCCESS (apr_err))
     return svn_error_create (apr_err, 0, NULL, pool, "read_char");
  else
    *got_eof = 0;

  return SVN_NO_ERROR;
}


/* Eat to end of line in F, including the newline.
   If see EOF, set *GOT_EOF to 1, else set it to 0.
   Use POOL for error allocation.  */
static svn_error_t *
eat_to_eol (int *got_eof, apr_file_t *f, apr_pool_t *pool)
{
  char c;

  do {
    SVN_ERR (read_char (&c, got_eof, f, pool));
  } while (c != '\n');

  return SVN_NO_ERROR;
}


/* Read a variable name from hook file (starting on the first char
   after the `$' sign), and insert its expansion into BUF + *IDX.
   *IDX is incremented by the size of the expanded variable, but if it
   would exceed MAX_IDX, then an error is returned instead.

   Known variable expansions are:

         "repos"   ==>   REPOS
         "user"    ==>   USER
         "rev"     ==>   REV
         "txn"     ==>   TXN_NAME

   If expanding a variable but the expansion given in the right hand
   column is null, return the error SVN_ERR_REPOS_HOOK_FAILURE.  Also,
   return that error if the variable name being expanded does not
   appear in the left hand column at all.
   
   Valid variable names contain only alphanumerics, hyphen, and
   underscore; this is used to stop reading and ungetc() when reach
   the end of the variable.  */
static svn_error_t *
expand (char *buf,
        int *idx,
        int *got_eof,
        int max_idx,
        const char *repos,
        const char *user,
        const char *rev,
        const char *txn_name,
        apr_file_t *hook_file,
        apr_pool_t *pool)
{
  char c;                              /* next char of unexpanded var name */
  char unexpanded_name[APR_PATH_MAX];  /* holds unexpanded var name */
  int unexpanded_len = 0;              /* length of unexpanded var name */
  const char *expansion = NULL;

  while (1)
    {
      SVN_ERR (read_char (&c, got_eof, hook_file, pool));
      
      if ((isalnum (c)) || (c == '-') || (c == '_'))
        {
          unexpanded_name[unexpanded_len++] = c;
          if (unexpanded_len > (APR_PATH_MAX - 1))
            return svn_error_create
              (SVN_ERR_REPOS_HOOK_FAILURE, 0, NULL, pool,
               "expand: var name waaaay too long");
        }
      else  /* hit a char that can't be part of a var name */
        {
          if (! *got_eof)  /* Push the char back if not at eof. */
            {
              apr_status_t apr_err = apr_file_ungetc (c, hook_file);

              if (! (APR_STATUS_IS_SUCCESS (apr_err)))
                return svn_error_create
                  (apr_err, 0, NULL, pool,
                   "expand: error from apr_file_ungetc()");
            }

          unexpanded_name[unexpanded_len] = '\0';
          break;
        }
    }

  /* Now, unexpanded_name[] holds the raw variable name. */

  /* Try to expand it. */
  if (unexpanded_len)
    {
      if (strcmp (unexpanded_name, "repos") == 0)
        expansion = repos;
      else if (strcmp (unexpanded_name, "user") == 0)
        expansion = user;
      else if (strcmp (unexpanded_name, "rev") == 0)
        expansion = rev;
      else if (strcmp (unexpanded_name, "txn") == 0)
        expansion = txn_name;
      else
        return svn_error_createf
          (SVN_ERR_REPOS_HOOK_FAILURE, 0, NULL, pool,
           "expand: cannot expand unknown var `%s'", unexpanded_name);
    }
  else  /* cannot expand empty variable */
    {
      return svn_error_create
        (SVN_ERR_REPOS_HOOK_FAILURE, 0, NULL, pool,
         "expand: cannot expand an empty variable");
    }
  
  /* Check that there was an expansion available for the valid var. */
  if (expansion == NULL)
    return svn_error_createf
      (SVN_ERR_REPOS_HOOK_FAILURE, 0, NULL, pool,
       "expand: no expansion available for var `%s'", unexpanded_name);
    
  /* Check that the expansion is not too long. */
  if (((strlen (expansion)) + *idx) > max_idx)
    return svn_error_createf
      (SVN_ERR_REPOS_HOOK_FAILURE, 0, NULL, pool,
       "expand: expanding var `%s' to `%s' exceeds %d",
       unexpanded_name, expansion, max_idx);

  /* Everything checks out, store the expanded variable. */
  strcpy (buf + *idx, expansion);
  *idx += strlen (expansion);
  
  return SVN_NO_ERROR;
}


/* Return 1 if BUF's first non-whitespace character is '#', or if the
   line contains only whitespace characters.  Otherwise, return 0.
   BUF is null-terminated and contains no newlines.  */
static int
is_irrelevant_line (char *buf)
{
  char c;

  while (((c = *buf++) != '\0') && (isspace (c)))
    ;

  if ((c == '#') || (c == '\0'))
    return 1;
  else
    return 0;
}


/* Read the next non-comment line from HOOK_FILE, tossing the newline,
   and set *LINE_P to the command with its arguments, ready to be run
   by system().  HOOK_FILE is already open, and will not be closed
   even if reach eof.

   When constructing arguments, expand "$user" to USER, "$rev" to REV,
   "$txn" to TXN_NAME, and "$repos" to REPOS.  If expansion is
   attempted on a null USER, TXN_NAME, or REV, then return
   SVN_ERR_REPOS_HOOK_FAILURE.

   After the last line of HOOK_FILE has been read, the next call sets
   *LINE_P to null.

   *LINE_P is allocated in POOL, which is also used for any temporary
   allocations.  */
static svn_error_t *
read_hook_line (char **line_p,
                apr_file_t *hook_file,
                const char *repos,
                const char *user,
                const char *rev,
                const char *txn_name,
                apr_pool_t *pool)
{
  char buf[APR_PATH_MAX];
  apr_size_t idx;
  char c;
  apr_status_t apr_err;
  const char *hook_file_path;  /* for error msgs */
  int this_line_done, got_eof;
              
  /* Get the hook's file name, for use in error messages. */
  apr_err = apr_file_name_get (&hook_file_path, hook_file);
  if (! APR_STATUS_IS_SUCCESS (apr_err))
    return svn_error_create
      (apr_err, 0, NULL, pool,
       "read_hook_line: error getting hook file name");
    
 restart:

  /* Reset the parms. */
  this_line_done = 0;
  got_eof = 0;
  idx = 0;
  buf[0] = '\0';
  while (1)
    {
      SVN_ERR (read_char (&c, &got_eof, hook_file, pool));

      switch (c)
        {
        case '\\':       /* read the next char unconditionally */
          SVN_ERR (read_char (&c, &got_eof, hook_file, pool));
          if (c == '\n')
            c = ' ';
          goto nonspecial;
          break;

        case '#':
          SVN_ERR (eat_to_eol (&got_eof, hook_file, pool));
          c = '\n';
          goto nonspecial;
          break;

        case '$':
          SVN_ERR (expand (buf, &idx, &got_eof, APR_PATH_MAX,
                           repos,
                           user,
                           rev,
                           txn_name,
                           hook_file, pool));
          if (got_eof)
            c = '\n';
          break;

        nonspecial:
        default:
          if (c == '\n')
            {
              buf[idx] = '\0';
              this_line_done = 1;
            }
          else
            buf[idx++] = c;
        }

      if (this_line_done)
        break;
    }

  /* Retry or return. */
  if ((idx > 0) && (is_irrelevant_line (buf)))
    {
      if (got_eof)
        *line_p = NULL;
      else
        goto restart;
    }
  else if ((idx == 0) && got_eof)
    *line_p = NULL;
  else if (idx == 0)
    goto restart;
  else
    *line_p = apr_pstrdup (pool, buf);

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




svn_error_t *
svn_repos_fs_begin_txn_for_update (svn_fs_txn_t **txn_p,
                                   svn_fs_t *fs,
                                   svn_revnum_t rev,
                                   const char *author,
                                   apr_pool_t *pool)
{
  /* ### someday, we might run a read-hook here. */

  /* Begin the transaction. */
  SVN_ERR (svn_fs_begin_txn (txn_p, fs, rev, pool));

  /* We pass the author to the filesystem by adding it as a property
     on the txn. */
  {
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
  }

  return SVN_NO_ERROR;
}




/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
