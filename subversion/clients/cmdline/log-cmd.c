/*
 * log-cmd.c -- Display log messages
 *
 * ====================================================================
 * Copyright (c) 2000-2002 CollabNet.  All rights reserved.
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
#include <apr_want.h>

#include "svn_wc.h"
#include "svn_client.h"
#include "svn_string.h"
#include "svn_path.h"
#include "svn_delta.h"
#include "svn_error.h"
#include "svn_pools.h"
#include "svn_sorts.h"
#include "svn_xml.h"
#include "svn_time.h"
#include "svn_utf.h"
#include "cl.h"


/*** Code. ***/

/* Helper for log_message_receiver(). 
 *
 * Return the number of lines in MSG, allowing any kind of newline
 * termination (CR, CRLF, or LFCR), even inconsistent.  The minimum
 * number of lines in MSG is 1 -- even the empty string is considered
 * to have one line, due to the way we print log messages.
 */
static int
num_lines (const char *msg)
{
  int count = 1;
  const char *p;

  for (p = msg; *p; p++)
    {
      if (*p == '\n')
        {
          count++;
          if (*(p + 1) == '\r')
            p++;
        }
      else if (*p == '\r')
        {
          count++;
          if (*(p + 1) == '\n')
            p++;
        }
    }

  return count;
}


/* Baton for log_message_receiver(). */
struct log_message_receiver_baton
{
  svn_boolean_t first_call;
};


/* This implements `svn_log_message_receiver_t', printing the logs in
 * a human-readable and machine-parseable format.  BATON is of type
 * `struct log_message_receiver_baton *'.
 *
 * Here is an example of the output:
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
 * And so on.
 */
static svn_error_t *
log_message_receiver (void *baton,
                      apr_hash_t *changed_paths,
                      svn_revnum_t rev,
                      const char *author,
                      const char *date,
                      const char *msg,
                      apr_pool_t *pool)
{
  struct log_message_receiver_baton *lb = baton;
  const char *author_native, *date_native, *msg_native;

  /* Number of lines in the msg. */
  int lines;

  /* Date string for humans. */
  const char *dbuf;

  if (rev == 0)
    {
      printf ("No commits in repository.\n");
      return SVN_NO_ERROR;
    }

  SVN_ERR (svn_utf_cstring_from_utf8 (&author_native, author, pool));
  SVN_ERR (svn_utf_cstring_from_utf8 (&date_native, date, pool));
  SVN_ERR (svn_utf_cstring_from_utf8 (&msg_native, msg, pool));

  {
    /* Convert date to a format for humans. */
    apr_time_t time_temp;

    SVN_ERR (svn_time_from_nts (&time_temp, date, pool));
    dbuf = svn_time_to_human_nts(time_temp, pool);
  }

#define SEP_STRING \
  "------------------------------------------------------------------------\n"

  if (lb->first_call)
    {
      printf (SEP_STRING);
      lb->first_call = 0;
    }

  lines = num_lines (msg_native);
  printf ("rev %" SVN_REVNUM_T_FMT ":  %s | %s | %d line%s\n",
          rev, author_native, dbuf, lines, (lines > 1) ? "s" : "");

  if (changed_paths)
    {
      apr_array_header_t *sorted_paths;
      int i;

      /* Get an array of sorted hash keys. */
      sorted_paths = apr_hash_sorted_keys (changed_paths,
                                           svn_sort_compare_items_as_paths, 
                                           pool);

      /* Note: This is the only place we need a pool, and therefore
         one might think we could just get it via
         apr_hash_pool_get().  However, that accessor will never be
         able to qualify its hash table parameter with `const',
         because it is a read/write accessor defined by
         APR_POOL_DECLARE_ACCESSOR().  Since I still hold out hopes of
         one day being able to constify `changed_paths' -- only some
         bizarre facts about apr_hash_first() currently prevent it --
         might as well just have the baton w/ pool ready right now, so
         it doesn't become an issue later. */

      printf ("Changed paths:\n");
      for (i = 0; i < sorted_paths->nelts; i++)
        {
          svn_item_t *item = &(APR_ARRAY_IDX (sorted_paths, i, svn_item_t));
          const char *path_native, *path = item->key;
          char action = (char) ((int) apr_hash_get (changed_paths, 
                                                    item->key, item->klen));
          SVN_ERR (svn_utf_cstring_from_utf8 (&path_native, path, pool));
          printf ("   %c %s\n", action, path_native);
        }
    }
  printf ("\n");  /* A blank line always precedes the log message. */
  printf ("%s\n", msg_native);
  printf (SEP_STRING);

  return SVN_NO_ERROR;
}


/* This implements `svn_log_message_receiver_t', printing the logs in
 * XML.  BATON is ignored.
 *
 * Here is an example of the output; note that the "<log>" and
 * "</log>" tags are not emitted by this function:
 * 
 * $ svn log --xml -r1648:1649
 * <log>
 * <logentry
 *    revision="1648">
 * <author>david</author>
 * <date>Sat 6 Apr 2002 16:34:51.428043 (day 096, dst 0, gmt_off -21600)</date>
 * <msg> * packages/rpm/subversion.spec : Now requires apache 2.0.36.
 * </msg>
 * </logentry>
 * <logentry
 *    revision="1649">
 * <author>cmpilato</author>
 * <date>Sat 6 Apr 2002 17:01:28.185136 (day 096, dst 0, gmt_off -21600)</date>
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
log_message_receiver_xml (void *baton,
                          apr_hash_t *changed_paths,
                          svn_revnum_t rev,
                          const char *author,
                          const char *date,
                          const char *msg,
                          apr_pool_t *pool)
{
  /* Collate whole log message into sb before printing. */
  svn_stringbuf_t *sb = svn_stringbuf_create ("", pool);
  char *revstr;

  if (rev == 0)
    return SVN_NO_ERROR;

  revstr = apr_psprintf (pool, "%" SVN_REVNUM_T_FMT, rev);
  /* <logentry revision="xxx"> */
  svn_xml_make_open_tag (&sb, pool, svn_xml_normal, "logentry",
                         "revision", revstr, NULL);

  /* <author>xxx</author> */
  svn_xml_make_open_tag (&sb, pool, svn_xml_protect_pcdata, "author",
                         NULL);
  svn_xml_escape_nts (&sb, author, pool);
  svn_xml_make_close_tag (&sb, pool, "author");

  /* Print the full, uncut, date.  This is machine output. */
  /* <date>xxx</date> */
  svn_xml_make_open_tag (&sb, pool, svn_xml_protect_pcdata, "date",
                         NULL);
  svn_xml_escape_nts (&sb, date, pool);
  svn_xml_make_close_tag (&sb, pool, "date");

  if (changed_paths)
    {
      apr_hash_index_t *hi;
      char *path;

      /* <paths> */
      svn_xml_make_open_tag (&sb, pool, svn_xml_normal, "paths",
                             NULL);
      
      for (hi = apr_hash_first (pool, changed_paths);
           hi != NULL;
           hi = apr_hash_next (hi))
        {
          void *val;
          char action;
          char *actionstr;

          apr_hash_this(hi, (void *) &path, NULL, &val);
          action = (char) ((int) val);

          actionstr = apr_psprintf (pool, "%c", action);
          /* <path action="X">xxx</path> */
          svn_xml_make_open_tag (&sb, pool, svn_xml_protect_pcdata, "path",
                                 "action", actionstr, NULL);
          svn_xml_escape_nts (&sb, path, pool);
          svn_xml_make_close_tag (&sb, pool, "path");
        }

      /* </paths> */
      svn_xml_make_close_tag (&sb, pool, "paths");
    }

  /* <msg>xxx</msg> */
  svn_xml_make_open_tag (&sb, pool, svn_xml_protect_pcdata, "msg", NULL);
  svn_xml_escape_nts (&sb, msg, pool);
  svn_xml_make_close_tag (&sb, pool, "msg");
  
  /* </logentry> */
  svn_xml_make_close_tag (&sb, pool, "logentry");

  printf ("%s", sb->data);

  return SVN_NO_ERROR;
}


svn_error_t *
svn_cl__log (apr_getopt_t *os,
             svn_cl__opt_state_t *opt_state,
             apr_pool_t *pool)
{
  apr_array_header_t *targets;
  svn_client_auth_baton_t *auth_baton;
  struct log_message_receiver_baton lb;

  SVN_ERR (svn_cl__args_to_target_array (&targets, os, opt_state, 
                                         FALSE, pool));

  /* Build an authentication object to give to libsvn_client. */
  auth_baton = svn_cl__make_auth_baton (opt_state, pool);

  /* Add "." if user passed 0 arguments */
  svn_cl__push_implicit_dot_target(targets, pool);

  if ((opt_state->start_revision.kind != svn_client_revision_unspecified)
      && (opt_state->end_revision.kind == svn_client_revision_unspecified))
    {
      /* If the user specified exactly one revision, then start rev is
         set but end is not.  We show the log message for just that
         revision by making end equal to start.

         Note that if the user requested a single dated revision, then
         this will cause the same date to be resolved twice.  The
         extra code complexity to get around this slight inefficiency
         doesn't seem worth it, however.  */

      opt_state->end_revision.kind = opt_state->start_revision.kind;

      opt_state->end_revision.value = opt_state->start_revision.value;

      opt_state->end_revision.value.number
        = opt_state->start_revision.value.number;

      opt_state->end_revision.value.date
        = opt_state->start_revision.value.date;
    }
  else if (opt_state->start_revision.kind == svn_client_revision_unspecified)
    {
      opt_state->start_revision.kind = svn_client_revision_head;

      if (opt_state->end_revision.kind == svn_client_revision_unspecified)
        {
          opt_state->end_revision.kind = svn_client_revision_number;
          opt_state->end_revision.value.number = 1;  /* oldest commit */
        }
    }

  lb.first_call = 1;
  if (opt_state->xml)
    {
      svn_stringbuf_t *sb;
      
      /* The header generation is commented out because it might not
         be certain that the log messages are indeed the advertised
         encoding, UTF-8. The absence of the header should not matter
         to people processing the output, and they should add it
         themselves if storing the output as a fully-formed XML
         document. */
      /* <?xml version="1.0" encoding="utf-8"?> */
      /* svn_xml_make_header (&sb, pool); */
      
      sb = NULL;
      svn_xml_make_open_tag (&sb, pool, svn_xml_normal, "log", NULL);
      printf ("%s", sb->data);  /* "<log>" */
      
      SVN_ERR (svn_client_log (auth_baton,
                               targets,
                               &(opt_state->start_revision),
                               &(opt_state->end_revision),
                               opt_state->verbose,
                               opt_state->strict,
                               log_message_receiver_xml,
                               &lb,
                               pool));
      
      sb = NULL;
      svn_xml_make_close_tag (&sb, pool, "log");
      printf ("%s", sb->data);  /* "</log>" */
    }
  else  /* default output format */
    {
      SVN_ERR (svn_client_log (auth_baton,
                               targets,
                               &(opt_state->start_revision),
                               &(opt_state->end_revision),
                               opt_state->verbose,
                               opt_state->strict,
                               log_message_receiver,
                               &lb,
                               pool));
    }

  return SVN_NO_ERROR;
}


/* 
 * local variables:
 * eval: (load-file "../../../tools/dev/svn-dev.el")
 * end: 
 */
