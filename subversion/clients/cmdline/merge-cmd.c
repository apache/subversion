/*
 * merge-cmd.c -- Merging changes into a working copy.
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

/* Parse a working-copy or url PATH, looking for an "@" sign, e.g.

         foo/bar/baz@13
         http://blah/bloo@27

   If an "@" is found, return the two halves in *TRUEPATH and *REV,
   allocating in POOL.

   If no "@" is found, set *TRUEPATH to PATH and set *REV to mean "HEAD".
*/
static svn_error_t *
parse_path (svn_client_revision_t *rev,
            svn_stringbuf_t **truepath,
            svn_stringbuf_t *path,
            apr_pool_t *pool)
{
  int i;
  svn_cl__opt_state_t *os = apr_pcalloc (pool, sizeof(*os));

  /* scanning from right to left, just to be friendly to any
     screwed-up filenames that might *actually* contain @-signs.  :-) */
  for (i = (path->len - 1); i >= 0; i--)
    {
      if (path->data[i] == '@')
        {
          if (svn_cl__parse_revision (os, path->data + i + 1, pool))
            return svn_error_createf (SVN_ERR_CL_ARG_PARSING_ERROR,
                                      0, NULL, pool,
                                      "Syntax error parsing revision \"%s\"",
                                      path->data + 1);

          *truepath = svn_stringbuf_ncreate (path->data, i, pool);
          rev->kind = os->start_revision.kind;
          rev->value = os->start_revision.value;

          return SVN_NO_ERROR;
        }
    }

  /* Didn't find an @-sign. */
  *truepath = path;
  rev->kind = svn_client_revision_head;

  return SVN_NO_ERROR;
}




svn_error_t *
svn_cl__merge (apr_getopt_t *os,
               svn_cl__opt_state_t *opt_state,
               apr_pool_t *pool)
{
  apr_array_header_t *targets;
  svn_client_auth_baton_t *auth_baton;
  svn_stringbuf_t *source1, *source2, *sourcepath1, *sourcepath2, *targetpath;

  auth_baton = svn_cl__make_auth_baton (opt_state, pool);

  targets = svn_cl__args_to_target_array (os, opt_state, pool);
  
  if (targets->nelts < 2)
    {
      svn_cl__subcommand_help ("merge", pool);
      return svn_error_create (SVN_ERR_CL_ARG_PARSING_ERROR, 0, 0, pool,
                               "Merging requires a minimum of two paths.");
    }
  source1 = ((svn_stringbuf_t **) (targets->elts))[0];
  source2 = ((svn_stringbuf_t **) (targets->elts))[1];

  if (targets->nelts >= 3)
    targetpath = ((svn_stringbuf_t **) (targets->elts))[2];
  else
    targetpath = svn_stringbuf_create (".", pool);

  if (opt_state->start_revision.kind != svn_client_revision_unspecified)
    {
      /* a -r was used, so this must be the "alternate" syntax */
      if (opt_state->end_revision.kind == svn_client_revision_unspecified)
        {
          svn_cl__subcommand_help ("merge", pool);
          return svn_error_create (SVN_ERR_CL_ARG_PARSING_ERROR,
                                   0, 0, pool, "Second revision required.");
        }
      sourcepath1 = source1;
      sourcepath2 = source2;
    }
  else
    {
      /* "normal" syntax */
      SVN_ERR (parse_path (&(opt_state->start_revision), &sourcepath1,
                           source1, pool));
      SVN_ERR (parse_path (&(opt_state->end_revision), &sourcepath2,
                           source2, pool));
    }   
  
  printf ("I would now call svn_client_merge with these arguments\n");
  printf ("sourcepath1 = %s\nrevision1 = %ld\n"
          "sourcepath2 = %s\nrevision2 = %ld\ntargetpath = %s\n",
          sourcepath1->data, (long int) opt_state->start_revision.value.number,
          sourcepath2->data, (long int) opt_state->end_revision.value.number,
          targetpath->data);
  fflush (stdout);

  /*

  SVN_ERR (svn_wc_get_actual_target (targetpath, &parent_dir, &entry, pool));
  SVN_ERR (svn_cl__get_trace_update_editor (&trace_editor, &trace_edit_baton,
                                            parent_dir, pool));

  SVN_ERR (svn_client_merge (trace_editor, trace_edit_baton,
                             auth_baton,
                             sourcepath1,
                             &(opt_state->start_revision),
                             sourcepath2,
                             &(opt_state->end_revision),
                             targetpath,
                             opt_state->nonrecursive ? FALSE : TRUE,
                             pool)); 

  */

  return SVN_NO_ERROR;
}


/* 
 * local variables:
 * eval: (load-file "../../../tools/dev/svn-dev.el")
 * end: 
 */
