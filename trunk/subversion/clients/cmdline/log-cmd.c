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
  apr_pool_t *pool;
};


/* This implements `svn_log_message_receiver_t'. */
static svn_error_t *
log_message_receiver (void *baton,
                      apr_hash_t *changed_paths,
                      svn_revnum_t rev,
                      const char *author,
                      const char *date,
                      const char *msg)
{
  struct log_message_receiver_baton *lb = baton;

  /* Number of lines in the msg. */
  int lines;

  /* As much date as we ever want to see. */
  char dbuf[38];

  /* The result of svn_time_to_string() looks something like this:
   *
   *  "Sat 2 Mar 2002 20:41:01.695108 (day 061, dst 0, gmt_off -21600)"
   *
   * You might think the part before the dot would be constant length,
   * but apparently it's not; so we grab it the hard way.
   */
  {
    const char *p = strchr (date, '.');

    if (p && ((p - date) < (sizeof (dbuf))))
      {
        strncpy (dbuf, date, (p - date));
        dbuf[p - date] = '\0';
      }
    else  /* hmmm, not the format we expected, so use as much as can */
      {
        strncpy (dbuf, date, ((sizeof (dbuf)) - 1));
        dbuf[(sizeof (dbuf)) - 1] = '\0';
      }
  }

#define SEP_STRING \
  "------------------------------------------------------------------------\n"

  if (lb->first_call)
    {
      printf (SEP_STRING);
      lb->first_call = 0;
    }

  lines = num_lines (msg);
  printf ("rev %lu:  %s | %s | %d line%s\n",
          rev, author, dbuf, lines, (lines > 1) ? "s" : "");
  if (changed_paths)
    {
      apr_hash_index_t *hi;
      char *path;

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
      for (hi = apr_hash_first(lb->pool, changed_paths);
           hi != NULL;
           hi = apr_hash_next(hi))
        {
          void *val;
          char action;

          apr_hash_this(hi, (void *) &path, NULL, &val);
          action = (char) ((int) val);
          printf ("   %c %s\n", (action == 'R' ? 'U' : action), path);
        }
    }
  printf ("\n");  /* A blank line always precedes the log message. */
  printf ("%s\n", msg);
  printf (SEP_STRING);

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

  targets = svn_cl__args_to_target_array (os, pool);

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
  lb.pool = pool;
  SVN_ERR (svn_client_log (auth_baton,
                           targets,
                           &(opt_state->start_revision),
                           &(opt_state->end_revision),
                           opt_state->verbose,
                           log_message_receiver,
                           &lb,
                           pool));

  return SVN_NO_ERROR;
}


/* 
 * local variables:
 * eval: (load-file "../../../tools/dev/svn-dev.el")
 * end: 
 */
