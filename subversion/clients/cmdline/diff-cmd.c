/*
 * diff-cmd.c -- Display context diff of a file
 *
 * ====================================================================
 * Copyright (c) 2000-2003 CollabNet.  All rights reserved.
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

/* An svn_opt_subcommand_t to handle the 'diff' command.
   This implements the `svn_opt_subcommand_t' interface. */
svn_error_t *
svn_cl__diff (apr_getopt_t *os,
              void *baton,
              apr_pool_t *pool)
{
  svn_cl__opt_state_t *opt_state = baton;
  apr_array_header_t *options;
  apr_array_header_t *targets;
  svn_client_auth_baton_t *auth_baton;
  apr_file_t *outfile, *errfile;
  apr_status_t status;
  int i;

  auth_baton = svn_cl__make_auth_baton (opt_state, pool);
  options = svn_cstring_split (opt_state->extensions, " \t\n\r", TRUE, pool);

  /* Get an apr_file_t representing stdout and stderr, which is where
     we'll have the external 'diff' program print to. */
  if ((status = apr_file_open_stdout (&outfile, pool)))
    return svn_error_create (status, NULL, "can't open stdout");
  if ((status = apr_file_open_stderr (&errfile, pool)))
    return svn_error_create (status, NULL, "can't open stderr");
  
  if ((opt_state->start_revision.kind == svn_opt_revision_unspecified)
      && (opt_state->end_revision.kind == svn_opt_revision_unspecified))
    {
      /* No '-r' was supplied, so this is either the form 
         'svn diff URL1@N URL2@M', or 'svn diff wcpath ...' */

      const char *target1, *target2;

      SVN_ERR (svn_opt_args_to_target_array (&targets, os, 
                                             opt_state->targets,
                                             &(opt_state->start_revision),
                                             &(opt_state->end_revision),
                                             TRUE, /* extract @revs */ pool));

      svn_opt_push_implicit_dot_target (targets, pool);

      target1 = ((const char **) (targets->elts))[0];

      if (svn_path_is_url (target1))
        {
          /* The form 'svn diff URL1@N URL2@M'. */

          /* The @revs have already been parsed out if they were
             present, and assigned to start_revision and end_revision.
             If not present, we set HEAD as default. */
          if (opt_state->start_revision.kind ==svn_opt_revision_unspecified)
            opt_state->start_revision.kind = svn_opt_revision_head;
          if (opt_state->end_revision.kind == svn_opt_revision_unspecified)
            opt_state->end_revision.kind = svn_opt_revision_head;

          if (targets->nelts < 2)
            return svn_error_create (SVN_ERR_CL_ARG_PARSING_ERROR,
                                     NULL, "Second URL is required.");
          
          target2 = ((const char **) (targets->elts))[1];
          
          /* Notice that we're passing DIFFERENT paths to
             svn_client_diff.  This is the only use-case which does so! */
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
          /* The form 'svn diff wcpath1 wcpath2 ...' */
          
          opt_state->start_revision.kind = svn_opt_revision_base;
          opt_state->end_revision.kind = svn_opt_revision_working;

          for (i = 0; i < targets->nelts; ++i)
            {
              const char *target = ((const char **) (targets->elts))[i];
              
              /* We're running diff on each TARGET independently;  also
                 notice that we pass TARGET twice, since we're always
                 comparing it to itself.  */
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
        }      
    }
  else
    {
      /* This is the form 'svn diff -rN[:M] path1 path2 ...' 

         The code in main.c has already parsed '-r' and filled in
         start_revision and (possibly) end_revision for us.
      */

      SVN_ERR (svn_opt_args_to_target_array (&targets, os, 
                                             opt_state->targets,
                                             &(opt_state->start_revision),
                                             &(opt_state->end_revision),
                                             FALSE, /* don't extract @revs */
                                             pool)); 

      svn_opt_push_implicit_dot_target (targets, pool);
      
      for (i = 0; i < targets->nelts; ++i)
        {
          const char *target = ((const char **) (targets->elts))[i];
  
          if (opt_state->end_revision.kind == svn_opt_revision_unspecified)
            {
              /* The user specified only '-r N'.  Therefore, each path
                 -must- be a working copy path.  No URLs allowed! */        
              if (svn_path_is_url (target))
                return svn_error_createf (SVN_ERR_CL_ARG_PARSING_ERROR,
                                          NULL, "You passed only one "
                                          "revision, but %s is a URL. "
                                          "URLs require two revisions.",
                                          target);

              /* URL or not, if the 2nd revision wasn't given by the
                 user, they must want to compare the 1st repsository
                 revision to their working files. */
              opt_state->end_revision.kind = svn_opt_revision_working;
            }
        
          /* We're running diff on each TARGET independently;  also
             notice that we pass TARGET twice, since we're always
             comparing it to itself.  */
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
    }
  
  return SVN_NO_ERROR;
}
