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

static svn_error_t *
log_message_receiver (void *baton,
                      const apr_hash_t *changed_paths,
                      svn_revnum_t revision,
                      const char *author,
                      const char *date,
                      const char *message,
                      svn_boolean_t last_call)
{
  /* ### todo: we ignore changed_paths for now; the repository isn't
     calculating it anyway, currently. */

  printf ("--------------------------------------------------------\n");
  printf ("Revision %lu\n", revision);
  printf ("%s\n", author);
  printf ("%s\n", date);
  printf ("%d\n", strlen (message));  /* auto-parsebility useful here? */
  printf ("%s\n", message);

  /* ### todo We don't use baton at all, since we can do everything
     with printf's, and we also ignore last_call.  If the latter
     continues to be ignored, maybe the parameter should be nixed. */

  return SVN_NO_ERROR;
}


svn_error_t *
svn_cl__log (apr_getopt_t *os,
             svn_cl__opt_state_t *opt_state,
             apr_pool_t *pool)
{
  apr_hash_t *paths = apr_hash_make (pool);
  apr_array_header_t *targets;
  int i;
  svn_client_auth_baton_t *auth_baton;

  targets = svn_cl__args_to_target_array (os, pool);

  /* Build an authentication object to give to libsvn_client. */
  auth_baton = svn_cl__make_auth_baton (opt_state, pool);

  /* Add "." if user passed 0 arguments */
  svn_cl__push_implicit_dot_target(targets, pool);

  for (i = 0; i < targets->nelts; i++)
    {
      svn_stringbuf_t *target = ((svn_stringbuf_t **) (targets->elts))[i];
      apr_hash_set (paths, target->data, target->len, (void *) 1);
    }

  /* ### todo: having built a path hash, we then ignore it, as no one
     is using it yet. */

  SVN_ERR (svn_client_log (auth_baton,
                           paths,
                           opt_state->start_revision,
                           opt_state->end_revision,
                           0,   /* discover_changed_paths, ignored for now */
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
