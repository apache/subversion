/*
 * log-cmd.c -- Display log messages
 *
 * ====================================================================
 * Copyright (c) 2000-2001 CollabNet.  All rights reserved.
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

/* This implements `svn_log_message_receiver_t'. */
static svn_error_t *
log_message_receiver (void *baton,
                      const apr_hash_t *changed_paths,
                      svn_revnum_t rev,
                      const char *author,
                      const char *date,
                      const char *msg,
                      svn_boolean_t last_call)
{
  /* ### todo: we ignore changed_paths for now; the repository isn't
     calculating it anyway, currently. */

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

  printf (SEP_STRING);
  printf ("rev %lu: %s   %s  (size: %d bytes)\n\n",
          rev, author, dbuf, strlen (msg));
  printf ("%s\n", msg);

  if (last_call)
    printf (SEP_STRING);

  /* We don't use the baton at all, since using printf() for output. */

  return SVN_NO_ERROR;
}


svn_error_t *
svn_cl__log (apr_getopt_t *os,
             svn_cl__opt_state_t *opt_state,
             apr_pool_t *pool)
{
  apr_array_header_t *targets;
  svn_client_auth_baton_t *auth_baton;

  targets = svn_cl__args_to_target_array (os, pool);

  /* Build an authentication object to give to libsvn_client. */
  auth_baton = svn_cl__make_auth_baton (opt_state, pool);

  /* Add "." if user passed 0 arguments */
  svn_cl__push_implicit_dot_target(targets, pool);

  SVN_ERR (svn_client_log (auth_baton,
                           targets,
                           opt_state->start_revision,
                           opt_state->end_revision,
                           0,   /* `discover_changed_paths' ignored for now */
                           log_message_receiver,
                           NULL,  /* no receiver baton right now */
                           pool));

  return SVN_NO_ERROR;
}


/* 
 * local variables:
 * eval: (load-file "../../svn-dev.el")
 * end: 
 */
