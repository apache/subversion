/*
 * diff.c: Compare working copy with text-base or repository.
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

  nargs = diff_cmd_baton->options->nelts;
  if (nargs)
    args = apr_palloc(subpool, nargs*sizeof(char*));
  else 
    args = NULL;

  i = 0;
  if (diff_cmd_baton->options->nelts)
    {
      /* Add the user specified diff options to the diff command line. */
      int j;
      for (j = 0; j < diff_cmd_baton->options->nelts; ++j)
        args[i++]
          = ((svn_stringbuf_t **)(diff_cmd_baton->options->elts))[j]->data;
    }
  assert (i==nargs);

  /* ### TODO: This printf is NOT "my final answer" -- placeholder for
     real work to be done. */ 
  if (label)
    apr_file_printf (outhandle, "Index: %s\n", label->data);
  else
    apr_file_printf (outhandle, "Index: %s\n", path1->data);
  apr_file_printf (outhandle, "===================================================================\n");

  SVN_ERR(svn_io_run_diff 
    (".", args, nargs, label ? label->data : NULL, path1->data, path2->data, 
     &exitcode, outhandle, errhandle, subpool));

  /* TODO: Handle exit code == 2 (i.e. errors with diff) here */
  
  /* TODO: someday we'll need to worry about whether we're going to need to
     write a diff plug-in mechanism that makes use of the two paths,
     instead of just blindly running SVN_CLIENT_DIFF.
  */

  svn_pool_destroy (subpool);

  return SVN_NO_ERROR;
}



/*** Public Interface. ***/

/* Display context diffs

     There are five cases:
        1. path is not an URL and start_revision != end_revision
        2. path is not an URL and start_revision == end_revision
        3. path is an URL and start_revision != end_revision
        4. path is an URL and start_revision == end_revision
        5. path is not an URL and no revisions given

     With only one distinct revision the working copy provides the other.
     When path is an URL there is no working copy. Thus

       1: compare repository versions for URL coresponding to working copy
       2: compare working copy against repository version
       3: compare repository versions for URL
       4: nothing to do.
       5: compare working copy against text-base

     Case 4 is not as stupid as it looks, for example it may occur if the
     user specifies two dates that resolve to the same revision.

   ### TODO: Non-zero is not a good test for valid dates. Really all the
   revision/date stuff needs to be reworked using a unifying revision
   structure with validity flags etc.
*/

svn_error_t *
svn_client_diff (svn_stringbuf_t *path,
                 const apr_array_header_t *diff_options,
                 svn_client_auth_baton_t *auth_baton,
                 svn_revnum_t start_revision,
                 apr_time_t start_date,
                 svn_revnum_t end_revision,
                 apr_time_t end_date,
                 svn_boolean_t recurse,
                 apr_pool_t *pool)
{
  svn_string_t path_str;
  svn_boolean_t path_is_url;
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

  /* If both a revision and a date/time are specified, this is an error.
     They mostly likely contradict one another. */
  if ((SVN_IS_VALID_REVNUM(start_revision) && start_date)
      || (SVN_IS_VALID_REVNUM(end_revision) && end_date))
    return
      svn_error_create(SVN_ERR_CL_MUTUALLY_EXCLUSIVE_ARGS, 0, NULL, pool,
                       "Cannot specify _both_ revision and time.");

  diff_cmd_baton.options = diff_options;
  diff_cmd_baton.pool = pool;

  /* Determine if the target we have been given is a path or an URL */
  path_str.data = path->data;
  path_str.len = path->len;
  path_is_url = svn_path_is_url (&path_str);

  if (path_is_url)
    {
      URL = path;
      /* Need to set anchor since the ra callbacks use it for creating
         temporary files. */
      /* ### TODO: Need apr temp file support */
      anchor = svn_stringbuf_create(".", pool);
      target = NULL;
    }
  else
    {
      SVN_ERR (svn_wc_get_actual_target (path, &anchor, &target, pool));
      SVN_ERR (svn_wc_entry (&entry, anchor, pool));
      URL = svn_stringbuf_create (entry->url->data, pool);
    }

  /* ### TODO: awful revision handling */
  if (!path_is_url
      && !SVN_IS_VALID_REVNUM(start_revision) && end_revision == 1
      && !start_date && !end_date)
    {
      /* Not an url and no revisions, this is the 'quick' diff that does
         not contact the repository and simply uses the text base */
      return svn_wc_diff (anchor,
                          target,
                          svn_client__diff_cmd, &diff_cmd_baton,
                          recurse,
                          pool);
    }

  /* Establish the RA session */
  SVN_ERR (svn_ra_init_ra_libs (&ra_baton, pool));
  SVN_ERR (svn_ra_get_ra_library (&ra_lib, ra_baton, URL->data, pool));
  SVN_ERR (svn_client__get_ra_callbacks (&ra_callbacks, &cb_baton,
                                         auth_baton,
                                         anchor,
                                         TRUE,
                                         path_is_url ? FALSE : TRUE,
                                         pool));
  SVN_ERR (ra_lib->open (&session, URL, ra_callbacks, cb_baton, pool));

  if (start_date)
    SVN_ERR (ra_lib->get_dated_revision (session, &start_revision, start_date));
  if (end_date)
    SVN_ERR (ra_lib->get_dated_revision (session, &end_revision, end_date));

  /* ### TODO: Warn the user? */
  if (path_is_url && start_revision == end_revision)
    return SVN_NO_ERROR;

  if (start_revision == end_revision)
    {
      /* The working copy is involved if only one revision is given */
      SVN_ERR (svn_wc_get_diff_editor (anchor, target,
                                       svn_client__diff_cmd, &diff_cmd_baton,
                                       recurse,
                                       &diff_editor, &diff_edit_baton,
                                       pool));

      SVN_ERR (ra_lib->do_update (session,
                                  &reporter, &report_baton,
                                  end_revision,
                                  target,
                                  recurse,
                                  diff_editor, diff_edit_baton));

      SVN_ERR (svn_wc_crawl_revisions (path, reporter, report_baton,
                                       FALSE, recurse, pool));
    }
  else
    {
      /* Pure repository comparison if two revisions are given */

      /* Open a second session used to request individual file
         contents. Although a session can be used for multiple requests, it
         appears that they must be sequential. Since the first request, for
         the diff, is still being processed the first session cannot be
         reused. This applies to ra_dav, ra_local does not appears to have
         this limitation. */
      void *session2;
      SVN_ERR (ra_lib->open (&session2, URL, ra_callbacks, cb_baton, pool));

      SVN_ERR (svn_client__get_diff_editor (target,
                                            svn_client__diff_cmd,
                                            &diff_cmd_baton,
                                            recurse,
                                            ra_lib, session2,
                                            start_revision,
                                            &diff_editor, &diff_edit_baton,
                                            pool));

      SVN_ERR (ra_lib->do_update (session,
                                  &reporter, &report_baton,
                                  end_revision,
                                  target,
                                  recurse,
                                  diff_editor, diff_edit_baton));

      SVN_ERR (reporter->set_path (report_baton,
                                   svn_stringbuf_create ("", pool),
                                   start_revision));

      SVN_ERR (reporter->finish_report (report_baton));

      SVN_ERR (ra_lib->close (session2));
    }

  SVN_ERR (ra_lib->close (session));

  return SVN_NO_ERROR;
}




/* --------------------------------------------------------------
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end: */


