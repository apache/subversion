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

#include <string.h>

#include "apr_pools.h"
#include "apr_file_io.h"

#include "svn_pools.h"
#include "svn_error.h"
#include "svn_path.h"
#include "svn_delta.h"
#include "svn_fs.h"
#include "svn_repos.h"


/* In the code below, the word "hook" is sometimes used
   indiscriminately to mean either hook or sentinel.  */


/*** Hook/sentinel file parsing. ***/

/* Read the next line from HOOK_FILE, setting *PROG_NAME to the
   command found on that line, *ARGS to a null-terminated array of
   pointers to const char * arguments to the command.

   After the last line of HOOK_FILE has been read, the next call will
   set *PROG_NAME to null.

   When constructing *ARGS, expand "$user" to USER, "$rev" to a string
   representation of REV, "$txn" to TXN_NAME, and "$repos" to REPOS.
   If expansion is attempted on a null USER or TXN_NAME, or on an
   invalid REV, then return SVN_ERR_REPOS_HOOK_FAILURE.  

   *ARGS and *PROG_NAME are allocated in POOL, which is also used for
   any temporary allocations.  */
static svn_error_t *
read_hook_line (const char **prog_name,
                const char * const **args,
                apr_file_t *hook_file,
                const char *repos,
                const char *user,
                svn_revnum_t rev,
                const char *txn_name,
                apr_pool_t *pool)
{
#if 0
  char raw_buf[APR_PATH_MAX];
  char buf[APR_PATH_MAX];
  apr_size_t raw_idx, idx;
  char c;

  apr_status_t apr_err;
  apr_array_header_t *args_ary = apr_array_make (pool, 4, sizeof (*args));
  
  raw_idx = idx = 0;
  while (apr_err = apr_file_getc (&c, hook_file))
    {
      if ((! APR_STATUS_IS_SUCCESS (apr_err))
          && (APR_STATUS_IS_EOF (apr_err)))   /* reached end of file */
        {
          *prog_name = NULL;
          return SVN_NO_ERROR;
        }
      else if (! APR_STATUS_IS_SUCCESS (apr_err))   /* error other than eof */
        {
          char *filename;
          apr_file_name_get (&filename, hook_file);
          return svn_error_createf 
            (apr_err, 0, NULL, pool,
             "read_hook_line: error reading line from `%s'", filename);
        }

      /* Else, we got another char in the line. */

      /* Sanity check: is this line overly long? */
      if (raw_idx >= APR_PATH_MAX)
        {
          char *filename;
          apr_file_name_get (&filename, hook_file);
          return svn_error_createf 
            (apr_err, 0, NULL, pool,
             "read_hook_line: line too long in `%s'", filename);
        }
        
      if (escaped)
        {
          /* The char before this one was backslash, the escape
             character.  That means this char has been escaped;
             depending on what it is, we might have to handle it
             specially. */

          if (c == '\n')
            raw_buf[raw_idx++] = ' ';
          else
            raw_buf[raw_idx++] = c;

          escaped = 0;
          continue;
        }

      /* Else no escape in effect */

      switch (c)
        {
        case '\\':
          escaped = 1;
          break;
          
        case ' ':
        case '\t':
          if (isspace (raw_buf[raw_idx]))
            ;
          else
            raw_buf[raw_idx++] = ' ';
          break;

        case '#':
          commented = 1;
          break;
          
        case '\n':
          raw_buf[raw_idx++] = '\0';
          break;
          
        default:
          raw_buf[raw_idx++] = c;
        }
    }

#endif /* 0 */

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
  return SVN_NO_ERROR;
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
