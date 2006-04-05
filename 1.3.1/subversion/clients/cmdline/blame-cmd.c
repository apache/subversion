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
#include "svn_xml.h"
#include "svn_time.h"
#include "svn_ebcdic.h"
#include "cl.h"

#include "svn_private_config.h"

typedef struct
{
  svn_cl__opt_state_t *opt_state;
  svn_stream_t *out;
  svn_stringbuf_t *sbuf;
} blame_baton_t;

#define AUTHOR_STR \
        "\x61\x75\x74\x68\x6f\x72"
        /* "author" */

#define BLAME_STR \
        "\x62\x6c\x61\x6d\x65"
        /* "blame" */

#define COMMIT_STR \
        "\x63\x6f\x6d\x6d\x69\x74"
        /* "commit" */

#define DATE_STR \
        "\x64\x61\x74\x65"
        /* "date" */

#define ENTRY_STR \
        "\x65\x6e\x74\x72\x79"
        /* "entry" */

#define FIVE_SPACE_MINUS_STR \
        "\x20\x20\x20\x20\x20\x2d"
        /* "     -" */

#define LINE_NUMBER_STR \
        "\x6c\x69\x6e\x65\x2d\x6e\x75\x6d\x62\x65\x72"
        /* "line-number" */

#define NINE_SPACE_MINUS_STR \
        "\x20\x20\x20\x20\x20\x20\x20\x20\x20\x2d"
        /* "         -" */

#define PATH_STR \
        "\x70\x61\x74\x68"
        /* "path" */

#define REVISION_STR \
        "\x72\x65\x76\x69\x73\x69\x6f\x6e"
        /* "revision" */

#define TARGET_STR \
        "\x74\x61\x72\x67\x65\x74"
        /* "target" */

/*** Code. ***/

/* This implements the svn_client_blame_receiver_t interface, printing
   XML to stdout. */
static svn_error_t *
blame_receiver_xml (void *baton,
                    apr_int64_t line_no,
                    svn_revnum_t revision,
                    const char *author,
                    const char *date,
                    const char *line,
                    apr_pool_t *pool)
{
  svn_stringbuf_t *sb = ((blame_baton_t *) baton)->sbuf;

  /* "<entry ...>" */
  /* line_no is 0-based, but the rest of the world is probably Pascal
     programmers, so we make them happy and output 1-based line numbers. */
  svn_xml_make_open_tag (&sb, pool, svn_xml_normal, ENTRY_STR,
                         LINE_NUMBER_STR,
                         APR_PSPRINTF2 (pool, "%" APR_INT64_T_FMT,
                                        line_no + 1),
                         NULL);

  if (SVN_IS_VALID_REVNUM (revision))
    {
      /* "<commit ...>" */
      svn_xml_make_open_tag (&sb, pool, svn_xml_normal, COMMIT_STR,
                             REVISION_STR,
                             APR_PSPRINTF2 (pool, "%ld", revision), NULL);

      /* "<author>xx</author>" */
      svn_cl__xml_tagged_cdata (&sb, pool, AUTHOR_STR, author);

      /* "<date>xx</date>" */
      svn_cl__xml_tagged_cdata (&sb, pool, DATE_STR, date);

      /* "</commit>" */
      svn_xml_make_close_tag (&sb, pool, COMMIT_STR);
    }

  /* "</entry>" */
  svn_xml_make_close_tag (&sb, pool, ENTRY_STR);

  SVN_ERR (svn_cl__error_checked_fputs (sb->data, stdout));
  svn_stringbuf_setempty (sb);

  return SVN_NO_ERROR;
}


/* This implements the svn_client_blame_receiver_t interface. */
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
                        ? APR_PSPRINTF2 (pool, "%6ld", revision)
                        : FIVE_SPACE_MINUS_STR;
#if APR_CHARSET_EBCDIC
  static svn_error_t *err;
#endif
  
  if (opt_state->verbose)
    {
      if (date)
        {
          SVN_ERR (svn_time_from_cstring (&atime, date, pool));
          time_utf8 = svn_time_to_human_cstring (atime, pool);
#if !APR_CHARSET_EBCDIC
          SVN_ERR (svn_cmdline_cstring_from_utf8 (&time_stdout, time_utf8,
                                                  pool));
#else
          time_stdout = time_utf8;
#endif
        } else
          /* ### This is a 44 characters long string. It assumes the current
             format of svn_time_to_human_cstring and also 3 letter
             abbreviations for the month and weekday names.  Else, the
             line contents will be misaligned. */
#if APR_CHARSET_EBCDIC
#pragma convert(1208)
#endif
          time_stdout = "                                           -";
#if APR_CHARSET_EBCDIC
#pragma convert(37)
#endif
#if !APR_CHARSET_EBCDIC
      return svn_stream_printf (out, pool, "%s %10s %s %s\n", rev_str, 
                                author ? author : NINE_SPACE_MINUS_STR, 
                                time_stdout , line);
#else
      /* On ebcdic platforms a versioned text file may be in ebcdic.  In those
       * cases line is obviously ebcdic encoded too.  We can't simply pass
       * line to svn_stream_printf_ebcdic since it expects utf-8 encoded var
       * string args.  For now we simply output the ebcdic line after the
       * blame info.  Note that since svn_client_blame2 parses each line
       * based on SVN_UTF8_NEWLINE_STR that there will only be one big ebcdic
       * line for the blamed file.  After some investigation there does not
       * appear to be an obvious/easy fix for this.
       * 
       * TODO: Handle ebcdic encoded text files properly. */
      err = svn_stream_printf_ebcdic (out, pool, "%s %10s %s ", rev_str, 
                                      author ? author : NINE_SPACE_MINUS_STR, 
                                      time_stdout);
      return err ? err : svn_stream_printf (out, pool, "%s%s", line,
                                            SVN_UTF8_NEWLINE_STR);
#endif
    }
  else
    {
#if !APR_CHARSET_EBCDIC
      return svn_stream_printf (out, pool, "%s %10s %s\n", rev_str, 
                                author ? author : NINE_SPACE_MINUS_STR, line);
#else
      /* On ebcdic platforms line may be encoded in ebcdic - see above. */
      err = svn_stream_printf_ebcdic (out, pool, "%s %10s ", rev_str, 
                                      author ? author : NINE_SPACE_MINUS_STR);
      return err ? err : svn_stream_printf (out, pool, "%s%s", line,
                                            SVN_UTF8_NEWLINE_STR);
#endif
    }
}
 

/* Prints XML header to standard out. */
static svn_error_t *
print_header_xml (apr_pool_t *pool)
{
  svn_stringbuf_t *sb = svn_stringbuf_create ("", pool);

  /* <?xml version="1.0" encoding="utf-8"?> */
  svn_xml_make_header (&sb, pool);

  /* "<blame>" */
  svn_xml_make_open_tag (&sb, pool, svn_xml_normal, BLAME_STR, NULL);

  return svn_cl__error_checked_fputs (sb->data, stdout);
}


/* Prints XML footer to standard out. */
static svn_error_t *
print_footer_xml (apr_pool_t *pool)
{
  svn_stringbuf_t *sb = svn_stringbuf_create ("", pool);

  /* "</blame>" */
  svn_xml_make_close_tag (&sb, pool, BLAME_STR);
  return svn_cl__error_checked_fputs (sb->data, stdout);
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
  blame_baton_t bl;
  int i;
  svn_boolean_t is_head_or_base = FALSE;

  SVN_ERR (svn_opt_args_to_target_array2 (&targets, os,
                                          opt_state->targets, pool));

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

  /* A comment abou the use of svn_stream_t for column-based output,
     and stdio for XML output:

     stdio does newline translations for us.  Since our XML routines
     from svn_xml.h produce text separated with \n, we want that
     translation to happen, making the XML more readable on some
     platforms.

     For the column-based output, we output contents from the file, so
     we don't want stdio to mess with the newlines.  We finish lines
     by \n, but the file might contain \r characters at the end of
     lines, since svn_client_blame() spit lines at \n characters.
     That would lead to CRCRLF line endings on platforms with CRLF
     line endings. */

  if (! opt_state->xml)
    SVN_ERR (svn_stream_for_stdout (&bl.out, pool));
  else
    bl.sbuf = svn_stringbuf_create ("", pool);

  bl.opt_state = opt_state;

  subpool = svn_pool_create (pool);

  if (opt_state->xml)
    {
      if (opt_state->verbose)
        return svn_error_create (SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                                 _("'verbose' option invalid in XML mode"));

      /* If output is not incremental, output the XML header and wrap
         everything in a top-level element.  This makes the output in
         its entirety a well-formed XML document. */
      if (! opt_state->incremental)
        SVN_ERR (print_header_xml (pool));
    }
  else
    {
      if (opt_state->incremental)
        return svn_error_create (SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                                 _("'incremental' option only valid in XML "
                                   "mode"));
    }

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
      if (opt_state->xml)
        {
          /* "<target ...>" */
          /* We don't output this tag immediately, which avoids creating
             a target element if this path is skipped. */
          const char *outpath = truepath;
          if (! svn_path_is_url (target))
            outpath = svn_path_local_style (truepath, subpool);
          svn_xml_make_open_tag (&bl.sbuf, pool, svn_xml_normal, TARGET_STR,
                                 PATH_STR, outpath, NULL);

          err = svn_client_blame2 (truepath,
                                   &peg_revision,
                                   &opt_state->start_revision,
                                   &opt_state->end_revision,
                                   blame_receiver_xml,
                                   &bl,
                                   ctx,
                                   subpool);
        }
      else
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
              svn_error_clear (err);
              SVN_ERR (SVN_CMDLINE_FPRINTF2 (stderr, subpool,
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
          svn_xml_make_close_tag (&(bl.sbuf), pool, TARGET_STR);
          SVN_ERR (svn_cl__error_checked_fputs (bl.sbuf->data, stdout));
        }

      if (opt_state->xml)
        svn_stringbuf_setempty (bl.sbuf);
    }
  svn_pool_destroy (subpool);
  if (opt_state->xml && ! opt_state->incremental)
    SVN_ERR (print_footer_xml (pool));

  return SVN_NO_ERROR;
}
