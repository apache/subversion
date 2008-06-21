/*
 * blame-cmd.c -- Display blame information
 *
 * ====================================================================
 * Copyright (c) 2000-2008 CollabNet.  All rights reserved.
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
#include "svn_xml.h"
#include "svn_time.h"
#include "cl.h"

#include "svn_private_config.h"

typedef struct
{
  svn_cl__opt_state_t *opt_state;
  svn_stream_t *out;
  svn_stringbuf_t *sbuf;
} blame_baton_t;


/*** Code. ***/

/* This implements the svn_client_blame_receiver2_t interface, printing
   XML to stdout. */
static svn_error_t *
blame_receiver_xml(void *baton,
                   apr_int64_t line_no,
                   svn_revnum_t revision,
                   const char *author,
                   const char *date,
                   svn_revnum_t merged_revision,
                   const char *merged_author,
                   const char *merged_date,
                   const char *merged_path,
                   const char *line,
                   apr_pool_t *pool)
{
  svn_cl__opt_state_t *opt_state =
    ((blame_baton_t *) baton)->opt_state;
  svn_stringbuf_t *sb = ((blame_baton_t *) baton)->sbuf;

  /* "<entry ...>" */
  /* line_no is 0-based, but the rest of the world is probably Pascal
     programmers, so we make them happy and output 1-based line numbers. */
  svn_xml_make_open_tag(&sb, pool, svn_xml_normal, "entry",
                        "line-number",
                        apr_psprintf(pool, "%" APR_INT64_T_FMT,
                                     line_no + 1),
                        NULL);

  if (SVN_IS_VALID_REVNUM(revision))
    svn_cl__print_xml_commit(&sb, revision, author, date, pool);

  if (opt_state->use_merge_history && SVN_IS_VALID_REVNUM(merged_revision))
    {
      /* "<merged>" */
      svn_xml_make_open_tag(&sb, pool, svn_xml_normal, "merged",
                            "path", merged_path, NULL);

      svn_cl__print_xml_commit(&sb, merged_revision, merged_author,
                               merged_date, pool);

      /* "</merged>" */
      svn_xml_make_close_tag(&sb, pool, "merged");

    }

  /* "</entry>" */
  svn_xml_make_close_tag(&sb, pool, "entry");

  SVN_ERR(svn_cl__error_checked_fputs(sb->data, stdout));
  svn_stringbuf_setempty(sb);

  return SVN_NO_ERROR;
}


static svn_error_t *
print_line_info(svn_stream_t *out,
                svn_revnum_t revision,
                const char *author,
                const char *date,
                const char *path,
                svn_boolean_t verbose,
                apr_pool_t *pool)
{
  const char *time_utf8;
  const char *time_stdout;
  const char *rev_str = SVN_IS_VALID_REVNUM(revision)
    ? apr_psprintf(pool, "%6ld", revision)
                        : "     -";

  if (verbose)
    {
      if (date)
        {
          SVN_ERR(svn_cl__time_cstring_to_human_cstring(&time_utf8,
                                                        date, pool));
          SVN_ERR(svn_cmdline_cstring_from_utf8(&time_stdout, time_utf8,
                                                pool));
        }
      else
        {
          /* ### This is a 44 characters long string. It assumes the current
             format of svn_time_to_human_cstring and also 3 letter
             abbreviations for the month and weekday names.  Else, the
             line contents will be misaligned. */
          time_stdout = "                                           -";
        }

      SVN_ERR(svn_stream_printf(out, pool, "%s %10s %s ", rev_str,
                                author ? author : "         -",
                                time_stdout));

      if (path)
        SVN_ERR(svn_stream_printf(out, pool, "%-14s ", path));
    }
  else
    {
      return svn_stream_printf(out, pool, "%s %10s ", rev_str,
                               author ? author : "         -");
    }

  return SVN_NO_ERROR;
}

/* This implements the svn_client_blame_receiver2_t interface. */
static svn_error_t *
blame_receiver(void *baton,
               apr_int64_t line_no,
               svn_revnum_t revision,
               const char *author,
               const char *date,
               svn_revnum_t merged_revision,
               const char *merged_author,
               const char *merged_date,
               const char *merged_path,
               const char *line,
               apr_pool_t *pool)
{
  svn_cl__opt_state_t *opt_state =
    ((blame_baton_t *) baton)->opt_state;
  svn_stream_t *out = ((blame_baton_t *)baton)->out;
  svn_boolean_t use_merged = FALSE;

  if (opt_state->use_merge_history)
    {
      /* Choose which revision to use.  If they aren't equal, prefer the
         earliest revision.  Since we do a forward blame, we want to the first
         revision which put the line in its current state, so we use the
         earliest revision.  If we ever switch to a backward blame algorithm,
         we may need to adjust this. */
      if (merged_revision < revision)
        {
          svn_stream_printf(out, pool, "G ");
          use_merged = TRUE;
        }
      else
        svn_stream_printf(out, pool, "  ");
    }

  if (use_merged)
    SVN_ERR(print_line_info(out, merged_revision, merged_author, merged_date,
                            merged_path, opt_state->verbose, pool));
  else
    SVN_ERR(print_line_info(out, revision, author, date, NULL,
                            opt_state->verbose, pool));

  return svn_stream_printf(out, pool, "%s%s", line, APR_EOL_STR);
}


/* This implements the `svn_opt_subcommand_t' interface. */
svn_error_t *
svn_cl__blame(apr_getopt_t *os,
              void *baton,
              apr_pool_t *pool)
{
  svn_cl__opt_state_t *opt_state = ((svn_cl__cmd_baton_t *) baton)->opt_state;
  svn_client_ctx_t *ctx = ((svn_cl__cmd_baton_t *) baton)->ctx;
  apr_pool_t *subpool;
  apr_array_header_t *targets;
  blame_baton_t bl;
  int i;
  svn_boolean_t end_revision_unspecified = FALSE;
  svn_diff_file_options_t *diff_options = svn_diff_file_options_create(pool);

  SVN_ERR(svn_cl__args_to_target_array_print_reserved(&targets, os,
                                                      opt_state->targets,
                                                      ctx, pool));

  /* Blame needs a file on which to operate. */
  if (! targets->nelts)
    return svn_error_create(SVN_ERR_CL_INSUFFICIENT_ARGS, 0, NULL);

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
        end_revision_unspecified = TRUE;
    }

  if (opt_state->start_revision.kind == svn_opt_revision_unspecified)
    {
      opt_state->start_revision.kind = svn_opt_revision_number;
      opt_state->start_revision.value.number = 1;
    }

  /* The final conclusion from issue #2431 is that blame info
     is client output (unlike 'svn cat' which plainly cats the file),
     so the EOL style should be the platform local one.
  */
  if (! opt_state->xml)
    SVN_ERR(svn_stream_for_stdout(&bl.out, pool));
  else
    bl.sbuf = svn_stringbuf_create("", pool);

  bl.opt_state = opt_state;

  subpool = svn_pool_create(pool);

  if (opt_state->extensions)
    {
      apr_array_header_t *opts;
      opts = svn_cstring_split(opt_state->extensions, " \t\n\r", TRUE, pool);
      SVN_ERR(svn_diff_file_options_parse(diff_options, opts, pool));
    }

  if (opt_state->xml)
    {
      if (opt_state->verbose)
        return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                                _("'verbose' option invalid in XML mode"));

      /* If output is not incremental, output the XML header and wrap
         everything in a top-level element.  This makes the output in
         its entirety a well-formed XML document. */
      if (! opt_state->incremental)
        SVN_ERR(svn_cl__xml_print_header("blame", pool));
    }
  else
    {
      if (opt_state->incremental)
        return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                                _("'incremental' option only valid in XML "
                                  "mode"));
    }

  for (i = 0; i < targets->nelts; i++)
    {
      svn_error_t *err;
      const char *target = APR_ARRAY_IDX(targets, i, const char *);
      const char *truepath;
      svn_opt_revision_t peg_revision;
      svn_client_blame_receiver2_t receiver;

      svn_pool_clear(subpool);
      SVN_ERR(svn_cl__check_cancel(ctx->cancel_baton));

      /* Check for a peg revision. */
      SVN_ERR(svn_opt_parse_path(&peg_revision, &truepath, target,
                                 subpool));

      if (end_revision_unspecified)
        {
          if (peg_revision.kind != svn_opt_revision_unspecified)
            opt_state->end_revision = peg_revision;
          else if (svn_path_is_url(target))
            opt_state->end_revision.kind = svn_opt_revision_head;
          else
            opt_state->end_revision.kind = svn_opt_revision_base;
        }

      if (opt_state->xml)
        {
          /* "<target ...>" */
          /* We don't output this tag immediately, which avoids creating
             a target element if this path is skipped. */
          const char *outpath = truepath;
          if (! svn_path_is_url(target))
            outpath = svn_path_local_style(truepath, subpool);
          svn_xml_make_open_tag(&bl.sbuf, pool, svn_xml_normal, "target",
                                "path", outpath, NULL);

          receiver = blame_receiver_xml;
        }
      else
        receiver = blame_receiver;

      err = svn_client_blame4(truepath,
                              &peg_revision,
                              &opt_state->start_revision,
                              &opt_state->end_revision,
                              diff_options,
                              opt_state->force,
                              opt_state->use_merge_history,
                              receiver,
                              &bl,
                              ctx,
                              subpool);

      if (err)
        {
          if (err->apr_err == SVN_ERR_CLIENT_IS_BINARY_FILE)
            {
              svn_error_clear(err);
              SVN_ERR(svn_cmdline_fprintf(stderr, subpool,
                                          _("Skipping binary file: '%s'\n"),
                                          target));
            }
          else
            {
              return err;
            }
        }
      else if (opt_state->xml)
        {
          /* "</target>" */
          svn_xml_make_close_tag(&(bl.sbuf), pool, "target");
          SVN_ERR(svn_cl__error_checked_fputs(bl.sbuf->data, stdout));
        }

      if (opt_state->xml)
        svn_stringbuf_setempty(bl.sbuf);
    }
  svn_pool_destroy(subpool);
  if (opt_state->xml && ! opt_state->incremental)
    SVN_ERR(svn_cl__xml_print_footer("blame", pool));

  return SVN_NO_ERROR;
}
