/*
 * lock-cmd.c -- LOck a working copy path in the repository.
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

/* ==================================================================== */



/*** Includes. ***/

#include "svn_pools.h"
#include "svn_client.h"
#include "svn_subst.h"
#include "svn_path.h"
#include "svn_error.h"
#include "svn_cmdline.h"
#include "cl.h"
#include "svn_private_config.h"


/*** Code. ***/

/* Get a lock comment, allocate it in POOL and store it in *COMMENT. */
static svn_error_t *
get_comment (const char **comment, const char **tmp_file,
             svn_client_ctx_t *ctx, svn_cl__opt_state_t *opt_state,
             const char *base_dir, apr_pool_t *pool)
{
  svn_string_t *comment_string;

  if (opt_state->filedata)
    {
      /* Get it from the -F argument. */
      if (strlen (opt_state->filedata->data) < opt_state->filedata->len)
        {
          /* A message containing a zero byte can't be represented as a C
             string. */
          return svn_error_create (SVN_ERR_CL_BAD_LOG_MESSAGE, NULL,
                                   _("Lock comment contains a zero byte"));
        }
      comment_string = svn_string_create (opt_state->filedata->data, pool);

    }
  else if (opt_state->message)
    {
      /* Get if from the -m option. */
      comment_string = svn_string_create (opt_state->message, pool);
    }
  else
    {
      /* Invoke the editor. */
      /* ### Should we do this in a loop like for the commit message? */
      comment_string = svn_string_create ("", pool);
      SVN_ERR (svn_cl__edit_externally (&comment_string, tmp_file,
                                        opt_state->editor_cmd, base_dir,
                                        comment_string, "svn-lock",
                                        ctx->config, TRUE, NULL,
                                        pool));
      if (comment_string)
        *comment = comment_string->data;
      else
        *comment = NULL;

      /* The above translates the string to UTF8/LF for us, so we are ready
         now. */
      return SVN_NO_ERROR;
    }

  /* Translate to UTF8/LF. */
  SVN_ERR (svn_subst_translate_string (&comment_string, comment_string,
                                       opt_state->encoding, pool));
  *comment = comment_string->data;

  return SVN_NO_ERROR;
}

struct lock_baton
{
  svn_boolean_t had_print_error;
  apr_pool_t *pool;
};

/* This callback is called by the client layer with BATON, the PATH
 * being locked, and the LOCK itself.
 */
static svn_error_t *
print_lock_info (void *baton,
                 const char *path,
                 svn_boolean_t do_lock,
                 const svn_lock_t *lock,
                 svn_error_t *ra_err)
{
  svn_error_t *err = NULL;
  struct lock_baton *lb = baton;

  if (ra_err)
    svn_handle_error (ra_err, stderr, FALSE);
  else
    err = svn_cmdline_printf (lb->pool, _("'%s' locked by user '%s'.\n"),
                              path, lock->owner);

  if (err)
    {
      /* Print if it is the first error. */
      if (!lb->had_print_error)
        {
          lb->had_print_error = TRUE;
          svn_handle_error (err, stderr, FALSE);
        }
      svn_error_clear (err);
    }

  return SVN_NO_ERROR;
}


/* This implements the `svn_opt_subcommand_t' interface. */
svn_error_t *
svn_cl__lock (apr_getopt_t *os,
             void *baton,
             apr_pool_t *pool)
{
  svn_cl__opt_state_t *opt_state = ((svn_cl__cmd_baton_t *) baton)->opt_state;
  svn_client_ctx_t *ctx = ((svn_cl__cmd_baton_t *) baton)->ctx;
  apr_array_header_t *targets, *locks;
  const char *base_dir;
  const char *comment;
  const char *tmp_file = NULL;
  svn_error_t *err;
  struct lock_baton lb;

  SVN_ERR (svn_opt_args_to_target_array2 (&targets, os,
                                          opt_state->targets, pool));

  /* We only support locking files, so '.' is not valid. */
  if (! targets->nelts)
    return svn_error_create (SVN_ERR_CL_ARG_PARSING_ERROR, 0, NULL);

  /* Put lock comment file in directory of first target. */
  base_dir = svn_path_dirname (APR_ARRAY_IDX (targets, 0, const char *), pool);

  /* Get comment. */
  SVN_ERR (get_comment (&comment, &tmp_file, ctx, opt_state, base_dir, pool));

  lb.had_print_error = FALSE;
  lb.pool = pool;
  err = svn_client_lock (&locks, targets, comment, opt_state->force,
                         print_lock_info, &lb, ctx, pool);
  if (err && tmp_file)
    svn_error_compose
      (err,
       svn_error_create (err->apr_err,
                         svn_error_createf (err->apr_err, NULL,
                                            "   '%s'", tmp_file),
                         _("Your lock comment was left in "
                           "a temporary file:")));

  if (tmp_file)
    SVN_ERR (svn_io_remove_file (tmp_file, pool));

  return err;
}
