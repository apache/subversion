/*
 * diff-cmd.c -- Display context diff of a file
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

#include "svn_wc.h"
#include "svn_client.h"
#include "svn_string.h"
#include "svn_path.h"
#include "svn_delta.h"
#include "svn_error.h"
#include "svn_types.h"
#include "cl.h"


/*** Code. ***/

/* An svn_cl__cmd_proc_t to handle the 'diff' command. */
svn_error_t *
svn_cl__diff (apr_getopt_t *os,
              svn_cl__opt_state_t *opt_state,
              apr_pool_t *pool)
{
  apr_array_header_t *options;
  apr_array_header_t *targets;
  apr_array_header_t *condensed_targets;
  svn_client_auth_baton_t *auth_baton;
  apr_file_t *outfile, *errfile;
  apr_status_t status;
  int i;

  options = svn_cl__stringlist_to_array (opt_state->extensions, pool);

  targets = svn_cl__args_to_target_array (os, opt_state, TRUE, pool);
  svn_cl__push_implicit_dot_target (targets, pool);
  SVN_ERR (svn_path_remove_redundancies (&condensed_targets,
                                         targets,
                                         pool));

  auth_baton = svn_cl__make_auth_baton (opt_state, pool);

  if (opt_state->start_revision.kind == svn_client_revision_unspecified)
    opt_state->start_revision.kind = svn_client_revision_base;

  if (opt_state->end_revision.kind == svn_client_revision_unspecified)
    opt_state->end_revision.kind = svn_client_revision_working;

  /* Get an apr_file_t representing stdout and stderr, which is where
     we'll have the diff program print to. */
  if ((status = apr_file_open_stdout (&outfile, pool)))
    return svn_error_create (status, 0, NULL, pool, "can't open stdout");
  if ((status = apr_file_open_stderr (&errfile, pool)))
    return svn_error_create (status, 0, NULL, pool, "can't open stderr");

  /* ### To make "svn diff URL1 URL2" work, we have a problem -- how
     to distinguish between these two different behaviors

        $ svn diff http://foo@X http://bar@Y

          and

        $ svn diff -rX:Y foo.c bar.c baz.c sub/qux.c sub/quux.c

     That is, multiple targets might mean we want the diff between
     revisions X and Y for *each* target -- or it might mean we want
     to diff two paths (a branch vs trunk, for example), which might
     or might not be at the same revision.  How do we know the
     intention?

     I think the answer is something like:

        svn_stringbuf_t *target1
          = ((svn_stringbuf_t **) (condensed_targets->elts))[0];
   
        svn_stringbuf_t *target2
          = ((svn_stringbuf_t **) (condensed_targets->elts))[1];
   
        if ((condensed_targets->nelts == 2)
            && ((is_a_url (target1)) || (is_a_url (target2))))
          {
            // start_revision corresponds to target1, end_revision to target2
            SVN_ERR (svn_client_diff (options,
                                      auth_baton,
                                      target1,
                                      &(opt_state->start_revision),
                                      target2,
                                      &(opt_state->end_revision),
                                      opt_state->nonrecursive ? FALSE : TRUE,
                                      outfile,
                                      errfile,
                                      pool));
          }
        else
          {
             for (i = 0; i < condensed_targets->nelts; ++i)
               {
                  etc, etc, see the code immediately below;
               }
          }

    Of course, this assumes we've first fixed svn_client_diff() to
    handle two distinct paths, ahem.
  */

  for (i = 0; i < condensed_targets->nelts; ++i)
    {
      svn_stringbuf_t *target
        = ((svn_stringbuf_t **) (condensed_targets->elts))[i];

      SVN_ERR (svn_client_diff (options,
                                auth_baton,
                                target,
                                &(opt_state->start_revision),
                                target,
                                &(opt_state->end_revision),
                                opt_state->nonrecursive ? FALSE : TRUE,
                                outfile,
                                errfile,
                                pool));
    }

  return SVN_NO_ERROR;
}


/* 
 * local variables:
 * eval: (load-file "../../../tools/dev/svn-dev.el")
 * end: 
 */
