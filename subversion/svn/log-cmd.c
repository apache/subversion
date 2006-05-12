/*
 * log-cmd.c -- Display log messages
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

#define APR_WANT_STRFUNC
#define APR_WANT_STDIO
#include <apr_want.h>

#include "svn_client.h"
#include "svn_string.h"
#include "svn_path.h"
#include "svn_error.h"
#include "svn_sorts.h"
#include "svn_xml.h"
#include "svn_time.h"
#include "svn_cmdline.h"
#include "cl.h"

#include "svn_private_config.h"


/*** Code. ***/

/* Baton for log_message_receiver() and log_message_receiver_xml(). */
struct log_receiver_baton
{
  /* Check for cancellation on each invocation of a log receiver. */
  svn_cancel_func_t cancel_func;
  void *cancel_baton;

  /* Don't print log message body nor its line count. */
  svn_boolean_t omit_log_message;
};


/* The separator between log messages. */
#define SEP_STRING \
  "------------------------------------------------------------------------\n"


/* Implement `svn_log_message_receiver_t', printing the logs in
 * a human-readable and machine-parseable format.  
 *
 * BATON is of type `struct log_receiver_baton'.
 *
 * First, print a header line.  Then if CHANGED_PATHS is non-null,
 * print all affected paths in a list headed "Changed paths:\n",
 * immediately following the header line.  Then print a newline
 * followed by the message body, unless BATON->omit_log_message is true.
 *
 * Here are some examples of the output:
 *
 * $ svn log -r1847:1846
 * ------------------------------------------------------------------------
 * rev 1847:  cmpilato | Wed 1 May 2002 15:44:26 | 7 lines
 * 
 * Fix for Issue #694.
 * 
 * * subversion/libsvn_repos/delta.c
 *   (delta_files): Rework the logic in this function to only call
 * send_text_deltas if there are deltas to send, and within that case,
 * only use a real delta stream if the caller wants real text deltas.
 * 
 * ------------------------------------------------------------------------
 * rev 1846:  whoever | Wed 1 May 2002 15:23:41 | 1 line
 *   
 * imagine an example log message here
 * ------------------------------------------------------------------------
 * 
 * Or:
 *
 * $ svn log -r1847:1846 -v
 * ------------------------------------------------------------------------
 * rev 1847:  cmpilato | Wed 1 May 2002 15:44:26 | 7 lines
 * Changed paths:
 *    M /trunk/subversion/libsvn_repos/delta.c
 * 
 * Fix for Issue #694.
 * 
 * * subversion/libsvn_repos/delta.c
 *   (delta_files): Rework the logic in this function to only call
 * send_text_deltas if there are deltas to send, and within that case,
 * only use a real delta stream if the caller wants real text deltas.
 * 
 * ------------------------------------------------------------------------
 * rev 1846:  whoever | Wed 1 May 2002 15:23:41 | 1 line
 * Changed paths:
 *    M /trunk/notes/fs_dumprestore.txt
 *    M /trunk/subversion/libsvn_repos/dump.c
 *   
 * imagine an example log message here
 * ------------------------------------------------------------------------
 * 
 * Or:
 *
 * $ svn log -r1847:1846 -q
 * ------------------------------------------------------------------------
 * rev 1847:  cmpilato | Wed 1 May 2002 15:44:26
 * ------------------------------------------------------------------------
 * rev 1846:  whoever | Wed 1 May 2002 15:23:41
 * ------------------------------------------------------------------------
 *
 * Or:
 *
 * $ svn log -r1847:1846 -qv
 * ------------------------------------------------------------------------
 * rev 1847:  cmpilato | Wed 1 May 2002 15:44:26
 * Changed paths:
 *    M /trunk/subversion/libsvn_repos/delta.c
 * ------------------------------------------------------------------------
 * rev 1846:  whoever | Wed 1 May 2002 15:23:41
 * Changed paths:
 *    M /trunk/notes/fs_dumprestore.txt
 *    M /trunk/subversion/libsvn_repos/dump.c
 * ------------------------------------------------------------------------
 *
 */
static svn_error_t *
log_message_receiver(void *baton,
                     apr_hash_t *changed_paths,
                     svn_revnum_t rev,
                     const char *author,
                     const char *date,
                     const char *msg,
                     apr_pool_t *pool)
{
  struct log_receiver_baton *lb = baton;

  /* Number of lines in the msg. */
  int lines;

  if (lb->cancel_func)
    SVN_ERR(lb->cancel_func(lb->cancel_baton));

  if (rev == 0 && msg == NULL)
    return SVN_NO_ERROR;

  /* ### See http://subversion.tigris.org/issues/show_bug.cgi?id=807
     for more on the fallback fuzzy conversions below. */

  if (author == NULL)
    author = _("(no author)");

  if (date && date[0])
    {
      /* Convert date to a format for humans. */
      apr_time_t time_temp;
      
      SVN_ERR(svn_time_from_cstring(&time_temp, date, pool));
      date = svn_time_to_human_cstring(time_temp, pool);
    }
  else
    date = _("(no date)");

  if (! lb->omit_log_message && msg == NULL)
    msg = "";

  SVN_ERR(svn_cmdline_printf(pool,
                             SEP_STRING "r%ld | %s | %s",
                             rev, author, date));

  if (! lb->omit_log_message)
    {
      lines = svn_cstring_count_newlines(msg) + 1;
      SVN_ERR(svn_cmdline_printf(pool,
                                 (lines != 1)
                                 ? " | %d lines"
                                 : " | %d line", lines));
    }

  SVN_ERR(svn_cmdline_printf(pool, "\n"));

  if (changed_paths)
    {
      apr_array_header_t *sorted_paths;
      int i;

      /* Get an array of sorted hash keys. */
      sorted_paths = svn_sort__hash(changed_paths,
                                    svn_sort_compare_items_as_paths, pool);

      SVN_ERR(svn_cmdline_printf(pool,
                                 _("Changed paths:\n")));
      for (i = 0; i < sorted_paths->nelts; i++)
        {
          svn_sort__item_t *item = &(APR_ARRAY_IDX(sorted_paths, i,
                                                   svn_sort__item_t));
          const char *path = item->key;
          svn_log_changed_path_t *log_item 
            = apr_hash_get(changed_paths, item->key, item->klen);
          const char *copy_data = "";
          
          if (log_item->copyfrom_path 
              && SVN_IS_VALID_REVNUM(log_item->copyfrom_rev))
            {
              copy_data 
                = apr_psprintf(pool, 
                               _(" (from %s:%ld)"),
                               log_item->copyfrom_path,
                               log_item->copyfrom_rev);
            }
          SVN_ERR(svn_cmdline_printf(pool, "   %c %s%s\n",
                                     log_item->action, path,
                                     copy_data));
        }
    }

  if (! lb->omit_log_message)
    {
      /* A blank line always precedes the log message. */
      SVN_ERR(svn_cmdline_printf(pool, "\n%s\n", msg));
    }

  SVN_ERR(svn_cmdline_fflush(stdout));

  return SVN_NO_ERROR;
}


/* This implements `svn_log_message_receiver_t', printing the logs in XML.
 *
 * BATON is of type `struct log_receiver_baton'.
 *
 * Here is an example of the output; note that the "<log>" and
 * "</log>" tags are not emitted by this function:
 * 
 * $ svn log --xml -r 1648:1649
 * <log>
 * <logentry
 *    revision="1648">
 * <author>david</author>
 * <date>2002-04-06T16:34:51.428043Z</date>
 * <msg> * packages/rpm/subversion.spec : Now requires apache 2.0.36.
 * </msg>
 * </logentry>
 * <logentry
 *    revision="1649">
 * <author>cmpilato</author>
 * <date>2002-04-06T17:01:28.185136Z</date>
 * <msg>Fix error handling when the $EDITOR is needed but unavailable.  Ah
 * ... now that&apos;s *much* nicer.
 * 
 * * subversion/clients/cmdline/util.c
 *   (svn_cl__edit_externally): Clean up the &quot;no external editor&quot;
 *   error message.
 *   (svn_cl__get_log_message): Wrap &quot;no external editor&quot; 
 *   errors with helpful hints about the -m and -F options.
 * 
 * * subversion/libsvn_client/commit.c
 *   (svn_client_commit): Actually capture and propogate &quot;no external
 *   editor&quot; errors.</msg>
 * </logentry>
 * </log>
 *
 */
static svn_error_t *
log_message_receiver_xml(void *baton,
                         apr_hash_t *changed_paths,
                         svn_revnum_t rev,
                         const char *author,
                         const char *date,
                         const char *msg,
                         apr_pool_t *pool)
{
  struct log_receiver_baton *lb = baton;
  /* Collate whole log message into sb before printing. */
  svn_stringbuf_t *sb = svn_stringbuf_create("", pool);
  char *revstr;

  if (lb->cancel_func)
    SVN_ERR(lb->cancel_func(lb->cancel_baton));

  if (rev == 0 && msg == NULL)
    return SVN_NO_ERROR;

  revstr = apr_psprintf(pool, "%ld", rev);
  /* <logentry revision="xxx"> */
  svn_xml_make_open_tag(&sb, pool, svn_xml_normal, "logentry",
                        "revision", revstr, NULL);

  /* <author>xxx</author> */
  svn_cl__xml_tagged_cdata(&sb, pool, "author", author);

  /* Print the full, uncut, date.  This is machine output. */
  /* According to the docs for svn_log_message_receiver_t, either
     NULL or the empty string represents no date.  Avoid outputting an
     empty date element. */
  if (date && date[0] == '\0')
    date = NULL;
  /* <date>xxx</date> */
  svn_cl__xml_tagged_cdata(&sb, pool, "date", date);

  if (changed_paths)
    {
      apr_hash_index_t *hi;
      char *path;

      /* <paths> */
      svn_xml_make_open_tag(&sb, pool, svn_xml_normal, "paths",
                            NULL);
      
      for (hi = apr_hash_first(pool, changed_paths);
           hi != NULL;
           hi = apr_hash_next(hi))
        {
          void *val;
          char action[2];
          svn_log_changed_path_t *log_item;
          
          apr_hash_this(hi, (void *) &path, NULL, &val);
          log_item = val;

          action[0] = log_item->action;
          action[1] = '\0';
          if (log_item->copyfrom_path
              && SVN_IS_VALID_REVNUM(log_item->copyfrom_rev))
            {
              /* <path action="X" copyfrom-path="xxx" copyfrom-rev="xxx"> */
              svn_stringbuf_t *escpath = svn_stringbuf_create("", pool);
              svn_xml_escape_attr_cstring(&escpath,
                                          log_item->copyfrom_path, pool);
              revstr = apr_psprintf(pool, "%ld", 
                                    log_item->copyfrom_rev);
              svn_xml_make_open_tag(&sb, pool, svn_xml_protect_pcdata, "path",
                                    "action", action,
                                    "copyfrom-path", escpath->data,
                                    "copyfrom-rev", revstr, NULL);
            }
          else
            {
              /* <path action="X"> */
              svn_xml_make_open_tag(&sb, pool, svn_xml_protect_pcdata, "path",
                                    "action", action, NULL);
            }
          /* xxx</path> */
          svn_xml_escape_cdata_cstring(&sb, path, pool);
          svn_xml_make_close_tag(&sb, pool, "path");
        }

      /* </paths> */
      svn_xml_make_close_tag(&sb, pool, "paths");
    }

  if (! lb->omit_log_message)
    {
      if (msg == NULL)
        msg = "";

      /* <msg>xxx</msg> */
      svn_cl__xml_tagged_cdata(&sb, pool, "msg", msg);
    }

  /* </logentry> */
  svn_xml_make_close_tag(&sb, pool, "logentry");

  SVN_ERR(svn_cl__error_checked_fputs(sb->data, stdout));

  return SVN_NO_ERROR;
}


/* This implements the `svn_opt_subcommand_t' interface. */
svn_error_t *
svn_cl__log(apr_getopt_t *os,
            void *baton,
            apr_pool_t *pool)
{
  svn_cl__opt_state_t *opt_state = ((svn_cl__cmd_baton_t *) baton)->opt_state;
  svn_client_ctx_t *ctx = ((svn_cl__cmd_baton_t *) baton)->ctx;
  apr_array_header_t *targets;
  struct log_receiver_baton lb;
  const char *target;
  int i;
  svn_opt_revision_t peg_revision;
  const char *true_path;

  SVN_ERR(svn_opt_args_to_target_array2(&targets, os, 
                                        opt_state->targets, pool));

  /* Add "." if user passed 0 arguments */
  svn_opt_push_implicit_dot_target(targets, pool);

  target = APR_ARRAY_IDX(targets, 0, const char *);

  /* Strip peg revision if targets contains an URI. */
  SVN_ERR(svn_opt_parse_path(&peg_revision, &true_path, target, pool));
  APR_ARRAY_IDX(targets, 0, const char *) = true_path;

  if ((opt_state->start_revision.kind != svn_opt_revision_unspecified)
      && (opt_state->end_revision.kind == svn_opt_revision_unspecified))
    {
      /* If the user specified exactly one revision, then start rev is
         set but end is not.  We show the log message for just that
         revision by making end equal to start.

         Note that if the user requested a single dated revision, then
         this will cause the same date to be resolved twice.  The
         extra code complexity to get around this slight inefficiency
         doesn't seem worth it, however.  */

      opt_state->end_revision = opt_state->start_revision;
    }
  else if (opt_state->start_revision.kind == svn_opt_revision_unspecified)
    {
      /* Default to any specified peg revision.  Otherwise, if the
         first target is an URL, then we default to HEAD:0.  Lastly,
         the default is BASE:0 since WC@HEAD may not exist. */
      if (peg_revision.kind == svn_opt_revision_unspecified)
        {
          if (svn_path_is_url(target))
            opt_state->start_revision.kind = svn_opt_revision_head;
          else
            opt_state->start_revision.kind = svn_opt_revision_base;
        }
      else
        opt_state->start_revision = peg_revision;

      if (opt_state->end_revision.kind == svn_opt_revision_unspecified)
        {
          opt_state->end_revision.kind = svn_opt_revision_number;
          opt_state->end_revision.value.number = 0;
        }
    }

  /* Verify that we pass at most one working copy path. */
  if (! svn_path_is_url(target) )
    {
      if (targets->nelts > 1)
        return svn_error_create(SVN_ERR_UNSUPPORTED_FEATURE, NULL,
                                _("When specifying working copy paths, only "
                                  "one target may be given"));
    }
  else
    {
      /* Check to make sure there are no other URLs. */
      for (i = 1; i < targets->nelts; i++)
        {
          target = APR_ARRAY_IDX(targets, i, const char *);
          
          if (svn_path_is_url(target))
            return svn_error_create(SVN_ERR_UNSUPPORTED_FEATURE, NULL,
                                    _("Only relative paths can be specified "
                                      "after a URL"));
        }
    }
  
  lb.cancel_func = ctx->cancel_func;
  lb.cancel_baton = ctx->cancel_baton;
  lb.omit_log_message = opt_state->quiet;
  
  if (! opt_state->quiet)
    svn_cl__get_notifier(&ctx->notify_func2, &ctx->notify_baton2, FALSE,
                         FALSE, FALSE, pool);
  
  if (opt_state->xml)
    {
      /* If output is not incremental, output the XML header and wrap
         everything in a top-level element. This makes the output in
         its entirety a well-formed XML document. */
      if (! opt_state->incremental)
        {
          svn_stringbuf_t *sb = svn_stringbuf_create("", pool);

          /* <?xml version="1.0" encoding="utf-8"?> */
          svn_xml_make_header(&sb, pool);
          
          /* "<log>" */
          svn_xml_make_open_tag(&sb, pool, svn_xml_normal, "log", NULL);

          SVN_ERR(svn_cl__error_checked_fputs(sb->data, stdout));
        }
      
      SVN_ERR(svn_client_log3(targets,
                              &peg_revision,
                              &(opt_state->start_revision),
                              &(opt_state->end_revision),
                              opt_state->limit,
                              opt_state->verbose,
                              opt_state->stop_on_copy,
                              log_message_receiver_xml,
                              &lb,
                              ctx,
                              pool));
      
      if (! opt_state->incremental)
        {
          svn_stringbuf_t *sb = svn_stringbuf_create("", pool);

          /* "</log>" */
          svn_xml_make_close_tag(&sb, pool, "log");

          SVN_ERR(svn_cl__error_checked_fputs(sb->data, stdout));
        }
    }
  else  /* default output format */
    {
      /* ### Ideally, we'd also pass the `quiet' flag through to the
       * repository code, so we wouldn't waste bandwith sending the
       * log message bodies back only to have the client ignore them.
       * However, that's an implementation detail; as far as the user
       * is concerned, the result of 'svn log --quiet' is the same
       * either way.
       */
      SVN_ERR(svn_client_log3(targets,
                              &peg_revision,
                              &(opt_state->start_revision),
                              &(opt_state->end_revision),
                              opt_state->limit,
                              opt_state->verbose,
                              opt_state->stop_on_copy,
                              log_message_receiver,
                              &lb,
                              ctx,
                              pool));

      if (! opt_state->incremental)
        SVN_ERR(svn_cmdline_printf(pool, SEP_STRING));
    }

  return SVN_NO_ERROR;
}
