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
diff_cmd (svn_stringbuf_t *path1,
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
svn_client_diff (const apr_array_header_t *diff_options,
                 svn_client_auth_baton_t *auth_baton,
                 const svn_client_revision_t *start,
                 const svn_client_revision_t *end,
                 svn_stringbuf_t *path,
                 svn_boolean_t recurse,
                 apr_pool_t *pool)
{
  svn_revnum_t start_revnum, end_revnum;
  svn_string_t path_str;
  svn_boolean_t path_is_url;
  svn_boolean_t use_admin;
  svn_stringbuf_t *anchor, *target;
  svn_wc_entry_t *entry;
  svn_stringbuf_t *URL;
  void *ra_baton, *session;
  svn_ra_plugin_t *ra_lib;
  const svn_ra_reporter_t *reporter;
  void *report_baton;
  const svn_delta_edit_fns_t *diff_editor;
  void *diff_edit_baton;
  struct diff_cmd_baton diff_cmd_baton;

  /* Sanity check. */
  if ((start->kind == svn_client_revision_unspecified)
      || (end->kind == svn_client_revision_unspecified))
    {
      return svn_error_create
        (SVN_ERR_CLIENT_BAD_REVISION, 0, NULL, pool,
         "svn_client_diff: caller failed to specify any revisions");
    }

  diff_cmd_baton.options = diff_options;
  diff_cmd_baton.pool = pool;

  /* Determine if the target we have been given is a path or an URL */
  path_str.data = path->data;
  path_str.len = path->len;
  path_is_url = svn_path_is_url (&path_str);

  /* if the path is not a URL, then we can use the SVN admin area for
     temporary files. */
  use_admin = !path_is_url;

  if (path_is_url)
    {
      URL = path;
      anchor = NULL;
      target = NULL;
    }
  else
    {
      SVN_ERR (svn_wc_get_actual_target (path, &anchor, &target, pool));
      SVN_ERR (svn_wc_entry (&entry, anchor, pool));
      URL = svn_stringbuf_create (entry->url->data, pool);
    }

  if ((! path_is_url)
      && ((start->kind == svn_client_revision_committed)
          || (start->kind == svn_client_revision_base))
      && (end->kind == svn_client_revision_working))
    {
      /* This is the 'quick' diff that does not contact the repository
         and simply uses the text base. */
      return svn_wc_diff (anchor,
                          target,
                          diff_cmd, &diff_cmd_baton,
                          recurse,
                          pool);
      /* ### todo: see comments issue #422 about how libsvn_client's
         diff implementation prints to stdout.  Apparently same is
         true for libsvn_wc!  Both will need adjusting; strongly
         suspect that the callback abstraction will be exactly what is
         needed to make merge share code -- that is, the same library
         functions will be called, but with different callbacks that
         Do The Right Thing.  Random Thought: these callbacks probably
         won't be svn_delta_edit_fns_t, because there's no depth-first
         requirement and no need to tweak .svn/ metadata.  Can be a
         lot simpler than an editor, therefore */
    }

  /* Else we must contact the repository. */

  /* Establish RA session */
  SVN_ERR (svn_ra_init_ra_libs (&ra_baton, pool));
  SVN_ERR (svn_ra_get_ra_library (&ra_lib, ra_baton, URL->data, pool));

  /* ### TODO: We have to pass null for the base_dir here, since the
     working copy does not match the requested revision.  It might be
     possible to have a special ra session for diff, where get_wc_prop
     cooperates with the editor and returns values when the file is in the
     wc, and null otherwise. */
  SVN_ERR (svn_client__open_ra_session (&session, ra_lib, URL, NULL,
                                        FALSE, FALSE, auth_baton, pool));

  /* ### todo: later, when we take two (or N) targets and a more
     sophisticated indication of their interaction with start and end,
     these calls will need to be rearranged. */
  SVN_ERR (svn_client__get_revision_number
           (&start_revnum, ra_lib, session, start, path->data, pool));
  SVN_ERR (svn_client__get_revision_number
           (&end_revnum, ra_lib, session, end, path->data, pool));

  /* ### todo: For the moment, we'll maintain the two distinct diff
     editors, svn_wc vs svn_client, in order to get this change done
     in a finite amount of time.  Next the two editors will be unified
     into one "lazy" editor that uses local data whenever possible. */

  if (end->kind == svn_client_revision_working)
    {
      /* The working copy is involved in this case. */
      SVN_ERR (svn_wc_get_diff_editor (anchor, target,
                                       diff_cmd, &diff_cmd_baton,
                                       recurse,
                                       &diff_editor, &diff_edit_baton,
                                       pool));

      SVN_ERR (ra_lib->do_update (session,
                                  &reporter, &report_baton,
                                  start_revnum,
                                  target,
                                  recurse,
                                  diff_editor, diff_edit_baton));

      SVN_ERR (svn_wc_crawl_revisions (path, reporter, report_baton,
                                       FALSE, recurse,
                                       NULL, NULL, /* notification is N/A */
                                       pool));
    }
  else   /* ### todo: there may be uncovered cases remaining */
    {
      /* Pure repository comparison. */

      /* Open a second session used to request individual file
         contents. Although a session can be used for multiple requests, it
         appears that they must be sequential. Since the first request, for
         the diff, is still being processed the first session cannot be
         reused. This applies to ra_dav, ra_local does not appears to have
         this limitation. */
      void *session2;

      /* ### TODO: Forcing the base_dir to null. It might be possible to
         use a special ra session that cooperates with the editor to enable
         get_wc_prop to return values when the file is in the wc */
      SVN_ERR (svn_client__open_ra_session (&session2, ra_lib, URL,
                                            NULL,
                                            FALSE, FALSE,
                                            auth_baton, pool));

      SVN_ERR (svn_client__get_diff_editor (target,
                                            diff_cmd,
                                            &diff_cmd_baton,
                                            recurse,
                                            ra_lib, session2,
                                            start_revnum,
                                            &diff_editor, &diff_edit_baton,
                                            pool));

      SVN_ERR (ra_lib->do_update (session,
                                  &reporter, &report_baton,
                                  end_revnum,
                                  target,
                                  recurse,
                                  diff_editor, diff_edit_baton));

      SVN_ERR (reporter->set_path (report_baton, "", start_revnum));

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


