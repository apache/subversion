/*
 * diff.c: Compare working copy with text-base or repository.
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

#include <apr_strings.h>
#include <apr_pools.h>
#include <apr_hash.h>
#include "svn_wc.h"
#include "svn_delta.h"
#include "svn_client.h"
#include "svn_string.h"
#include "svn_error.h"
#include "svn_path.h"
#include "svn_test.h"
#include "svn_io.h"
#include "svn_pools.h"
#include "client.h"
#include "svn_private_config.h"         /* for SVN_CLIENT_DIFF */
#include <assert.h>

struct diff_cmd_baton {
  const apr_array_header_t *options;
  apr_pool_t *pool;
};

/* This is an svn_wc_diff_cmd_t callback */
static svn_error_t *
svn_client__diff_cmd (svn_stringbuf_t *path1,
                      svn_stringbuf_t *path2,
                      svn_stringbuf_t *label,
                      void *baton)
{
  struct diff_cmd_baton *diff_cmd_baton = baton;
  apr_status_t status;
  const char **args;
  int i, nargs, exitcode;
  apr_exit_why_e exitwhy;
  apr_file_t *outhandle, *errhandle;

  /* Use a subpool here because the pool comes right through from the top
     level.*/
  apr_pool_t *subpool = svn_pool_create (diff_cmd_baton->pool);

  /* Get an apr_file_t representing stdout and stderr, which is where we'll
     have the diff program print to. */
  status = apr_file_open_stdout (&outhandle, subpool);
  if (status)
    return svn_error_create (status, 0, NULL, subpool,
                             "error: can't open handle to stdout");
  status = apr_file_open_stderr (&errhandle, subpool);
  if (status)
    return svn_error_create (status, 0, NULL, subpool,
                             "error: can't open handle to stderr");

  /* Execute local diff command on these two paths, print to stdout. */

  nargs = 4; /* Eek! A magic number! It's checked by a later assert */
  if (label)
    nargs += 2; /* Another magic number checked by the later assert */
  if (diff_cmd_baton->options->nelts)
    nargs += diff_cmd_baton->options->nelts;
  else
    ++nargs;
  args = apr_palloc(subpool, nargs*sizeof(char*));

  i = 0;
  args[i++] = SVN_CLIENT_DIFF;  /* the autoconfiscated system diff program */
  if (diff_cmd_baton->options->nelts)
    {
      /* Add the user specified diff options to the diff command line. */
      int j;
      for (j = 0; j < diff_cmd_baton->options->nelts; ++j)
        args[i++]
          = ((svn_stringbuf_t **)(diff_cmd_baton->options->elts))[j]->data;
    }
  else
    {
      /* The user didn't specify any options, default to unified diff. */
      args[i++] = "-u";
    }
  if (label)
    {
      args[i++] = "-L";
      args[i++] = label->data;
    }
  args[i++] = path1->data;
  args[i++] = path2->data;
  args[i++] = NULL;
  assert (i==nargs);

  /* ### TODO: This printf is NOT "my final answer" -- placeholder for
     real work to be done. */ 
  if (label)
    apr_file_printf (outhandle, "Index: %s\n", label->data);
  else
    apr_file_printf (outhandle, "Index: %s\n", path1->data);
  apr_file_printf (outhandle, "===================================================================\n");

  SVN_ERR(svn_io_run_cmd (".", SVN_CLIENT_DIFF, args, &exitcode, &exitwhy, NULL,
                          outhandle, errhandle, subpool));

  /* TODO: Handle exit code == 2 (i.e. errors with diff) here */
  
  /* TODO: someday we'll need to worry about whether we're going to need to
     write a diff plug-in mechanism that makes use of the two paths,
     instead of just blindly running SVN_CLIENT_DIFF.
  */

  svn_pool_destroy (subpool);

  return SVN_NO_ERROR;
}



/*** Public Interface. ***/

svn_error_t *
svn_client_file_diff (svn_stringbuf_t *path,
                      svn_stringbuf_t **pristine_copy_path,
                      apr_pool_t *pool)
{
  svn_error_t *err;

  /* Ask the WC layer to make a tmp copy of the pristine text-base and
     return the path to us.  */
  err = svn_wc_get_pristine_copy_path (path, pristine_copy_path, pool);
  
  /* If the WC fails, or doesn't have a text-base, then ask the RA
     layer to deposit a copy somewhere!  */
  if (err)
    /* TODO:  someday when we have RA working, use it here! */
    return err;
  
  return SVN_NO_ERROR;
}

/* Compare working copy against the repository */
svn_error_t *
svn_client_diff (svn_stringbuf_t *path,
                 const apr_array_header_t *diff_options,
                 svn_client_auth_baton_t *auth_baton,
                 svn_revnum_t revision,
                 apr_time_t tm,
                 svn_boolean_t recurse,
                 apr_pool_t *pool)
{
  svn_stringbuf_t *anchor, *target;
  svn_wc_entry_t *entry;
  svn_stringbuf_t *URL;
  void *ra_baton, *session, *cb_baton;
  svn_ra_plugin_t *ra_lib;
  svn_ra_callbacks_t *ra_callbacks;
  const svn_ra_reporter_t *reporter;
  void *report_baton;
  const svn_delta_edit_fns_t *diff_editor;
  void *diff_edit_baton;
  struct diff_cmd_baton diff_cmd_baton;

  /* If both REVISION and TM are specified, this is an error.
     They mostly likely contradict one another. */
  if ((revision != SVN_INVALID_REVNUM) && tm)
    return
      svn_error_create(SVN_ERR_CL_MUTUALLY_EXCLUSIVE_ARGS, 0, NULL, pool,
                       "Cannot specify _both_ revision and time.");

  SVN_ERR (svn_wc_get_actual_target (path, &anchor, &target, pool));
  SVN_ERR (svn_wc_entry (&entry, anchor, pool));
  URL = svn_stringbuf_create (entry->url->data, pool);

  SVN_ERR (svn_ra_init_ra_libs (&ra_baton, pool));
  SVN_ERR (svn_ra_get_ra_library (&ra_lib, ra_baton, URL->data, pool));

  SVN_ERR (svn_client__get_ra_callbacks (&ra_callbacks, &cb_baton,
                                         auth_baton,
                                         anchor,
                                         TRUE,
                                         TRUE,
                                         pool));
  SVN_ERR (ra_lib->open (&session, URL, ra_callbacks, cb_baton, pool));

  if (tm)
    SVN_ERR (ra_lib->get_dated_revision (session, &revision, tm));

  diff_cmd_baton.options = diff_options;
  diff_cmd_baton.pool = pool;
  SVN_ERR (svn_wc_get_diff_editor (anchor, target,
                                   svn_client__diff_cmd, &diff_cmd_baton,
                                   recurse,
                                   &diff_editor, &diff_edit_baton,
                                   pool));

  SVN_ERR (ra_lib->do_update (session,
                              &reporter, &report_baton,
                              revision,
                              target,
                              recurse,
                              diff_editor, diff_edit_baton));

  SVN_ERR (svn_wc_crawl_revisions (path, reporter, report_baton,
                                   FALSE, recurse, pool));

  SVN_ERR (ra_lib->close (session));

  return SVN_NO_ERROR;
}




/* --------------------------------------------------------------
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end: */


