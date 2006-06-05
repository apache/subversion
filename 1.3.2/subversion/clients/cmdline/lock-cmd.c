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
get_comment (const char **comment, svn_client_ctx_t *ctx,
             svn_cl__opt_state_t *opt_state, apr_pool_t *pool)
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
      *comment = NULL;
      return SVN_NO_ERROR;
    }

  /* Translate to UTF8/LF. */
#if APR_CHARSET_EBCDIC
  /* On ebcdic platforms a file used to set a lock message may be encoded in
   * ebcdic.  This presents a host of problems re how to detect this case.
   * Obtaining the file's CCSID is not easy, and even if it were it may not
   * always be accurate (e.g. a CCSID 37 file copied via a mapped drive in
   * Windows Explorer has a 1252 CCSID).  To avoid problems and keep things
   * relatively simple, the ebcdic port currently requires that file data used
   * for a lock message be encoded in utf-8 only.
   * 
   * With -F args restricted to utf-8 there's nothing to translate re
   * encoding, but line endings may be inconsistent so translation is still
   * needed.  The problem is if opt_state->encoding is passed,
   * svn_subst_translate_string will attempt to convert propval from a
   * native string to utf-8, corrupting it on the iSeries where
   * native == ebcdic != subset of utf-8.  So "1208" is passed causing no
   * encoding conversion, but producing uniform LF line endings.
   * 
   * See svn_utf_cstring_to_utf8_ex for why a string representation of a
   * CCSID is used rather than "UTF-8". */
  if (opt_state->filedata)
    SVN_ERR (svn_subst_translate_string (&comment_string, comment_string,
                                         "1208", pool));
  else
#endif
    SVN_ERR (svn_subst_translate_string (&comment_string, comment_string,
                                       opt_state->encoding, pool));
  *comment = comment_string->data;

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
  apr_array_header_t *targets;
  const char *comment;

  SVN_ERR (svn_opt_args_to_target_array2 (&targets, os,
                                          opt_state->targets, pool));

  /* We only support locking files, so '.' is not valid. */
  if (! targets->nelts)
    return svn_error_create (SVN_ERR_CL_ARG_PARSING_ERROR, 0, NULL);

  /* Get comment. */
  SVN_ERR (get_comment (&comment, ctx, opt_state, pool));

  svn_cl__get_notifier (&ctx->notify_func2, &ctx->notify_baton2, FALSE,
                        FALSE, FALSE, pool);

  SVN_ERR (svn_client_lock (targets, comment, opt_state->force,
                            ctx, pool));

  return SVN_NO_ERROR;
}
