/*
 * blame-cmd.c -- Display blame information
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


/*** Includes. ***/

#include "svn_client.h"
#include "svn_error.h"
#include "svn_path.h"
#include "svn_pools.h"
#include "svn_cmdline.h"
#include "svn_time.h"
#include "cl.h"

#include "svn_private_config.h"

typedef struct
{
  svn_cl__opt_state_t *opt_state;
  svn_stream_t *out;
} blame_baton_t;


/*** Code. ***/
static svn_error_t *
blame_receiver (void *baton,
                apr_int64_t line_no,
                svn_revnum_t revision,
                const char *author,
                const char *date,
                const char *line,
                apr_pool_t *pool)
{
  svn_cl__opt_state_t *opt_state =
    ((blame_baton_t *) baton)->opt_state;
  svn_stream_t *out = ((blame_baton_t *)baton)->out;
  apr_time_t atime;
  const char *time_utf8;
  const char *time_stdout;
  const char *rev_str = SVN_IS_VALID_REVNUM (revision) 
                        ? apr_psprintf (pool, "%6ld", revision)
                        : "     -";
  
  if (opt_state->verbose)
    {
      if (date)
        {
          SVN_ERR (svn_time_from_cstring (&atime, date, pool));
          time_utf8 = svn_time_to_human_cstring (atime, pool);
          SVN_ERR (svn_cmdline_cstring_from_utf8 (&time_stdout, time_utf8,
                                                  pool));
        } else
          /* ### This is a 44 characters long string. It assumes the current
             format of svn_time_to_human_cstring and also 3 letter
             abbreviations for the month and weekday names.  Else, the
             line contents will be misaligned. */
          time_stdout = "                                           -";
      return svn_stream_printf (out, pool, "%s %10s %s %s\n", rev_str, 
                                author ? author : "         -", 
                                time_stdout , line);
    }
  else
    {
      return svn_stream_printf (out, pool, "%s %10s %s\n", rev_str, 
                                author ? author : "         -", line);
    }
}
 

/* This implements the `svn_opt_subcommand_t' interface. */
svn_error_t *
svn_cl__blame (apr_getopt_t *os,
               void *baton,
               apr_pool_t *pool)
{
  svn_cl__opt_state_t *opt_state = ((svn_cl__cmd_baton_t *) baton)->opt_state;
  svn_client_ctx_t *ctx = ((svn_cl__cmd_baton_t *) baton)->ctx;
  apr_pool_t *subpool;
  apr_array_header_t *targets;
  svn_stream_t *out;
  blame_baton_t bl;
  int i;
  svn_boolean_t is_head_or_base = FALSE;

  SVN_ERR (svn_opt_args_to_target_array (&targets, os, 
                                         opt_state->targets,
                                         &opt_state->start_revision,
                                         &opt_state->end_revision,
                                         FALSE, pool));

  /* Blame needs a file on which to operate. */
  if (! targets->nelts)
    return svn_error_create (SVN_ERR_CL_ARG_PARSING_ERROR, 0, NULL);

  if (opt_state->end_revision.kind == svn_opt_revision_unspecified)
    {
      if (opt_state->start_revision.kind != svn_opt_revision_unspecified)
        {
          /* In the case that -rX was specified, we actually want to set the
             range to be -r1:X. */

          opt_state->end_revision = opt_state->start_revision;
          opt_state->start_revision.kind = svn_opt_revision_number;
          opt_state->start_revision.value.number = 1;
        }
      else
        is_head_or_base = TRUE;
    }

  if (opt_state->start_revision.kind == svn_opt_revision_unspecified)
    {
      opt_state->start_revision.kind = svn_opt_revision_number;
      opt_state->start_revision.value.number = 1;
    }

  SVN_ERR (svn_stream_for_stdout (&out, pool));
  bl.opt_state = opt_state;
  bl.out = out;
  
  subpool = svn_pool_create (pool);

  for (i = 0; i < targets->nelts; i++)
    {
      svn_error_t *err;
      const char *target = ((const char **) (targets->elts))[i];
      const char *truepath;
      svn_opt_revision_t peg_revision;
      
      svn_pool_clear (subpool);
      SVN_ERR (svn_cl__check_cancel (ctx->cancel_baton));
      if (is_head_or_base)
        {
          if (svn_path_is_url (target))
            opt_state->end_revision.kind = svn_opt_revision_head;
          else
            opt_state->end_revision.kind = svn_opt_revision_base;
        }

      /* Check for a peg revision. */
      SVN_ERR (svn_opt_parse_path (&peg_revision, &truepath, target,
                                   subpool));
               
      err = svn_client_blame2 (truepath,
                               &peg_revision,
                               &opt_state->start_revision,
                               &opt_state->end_revision,
                               blame_receiver,
                               &bl,
                               ctx,
                               subpool);
      if (err)
        {
          if (err->apr_err == SVN_ERR_CLIENT_IS_BINARY_FILE)
            {
              SVN_ERR (svn_cmdline_printf (subpool,
                                           _("Skipping binary file: '%s'\n"),
                                           target));
              svn_error_clear (err);
            }
          else
            {
              return err;
            }
        }
    }
  svn_pool_destroy (subpool);

  return SVN_NO_ERROR;
}
