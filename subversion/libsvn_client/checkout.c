/*
 * checkout.c:  wrappers around wc checkout functionality
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

#include <assert.h>
#include "svn_wc.h"
#include "svn_delta.h"
#include "svn_client.h"
#include "svn_ra.h"
#include "svn_string.h"
#include "svn_error.h"
#include "svn_path.h"
#include "client.h"



/*** Public Interfaces. ***/


svn_error_t *
svn_client_checkout (const svn_delta_edit_fns_t *before_editor,
                     void *before_edit_baton,
                     const svn_delta_edit_fns_t *after_editor,
                     void *after_edit_baton,
                     svn_client_auth_baton_t *auth_baton,
                     svn_stringbuf_t *URL,
                     svn_stringbuf_t *path,
                     svn_revnum_t revision,
                     apr_time_t tm,
                     svn_stringbuf_t *xml_src,
                     apr_pool_t *pool)
{
  const svn_delta_edit_fns_t *checkout_editor;
  void *checkout_edit_baton;

  /* Sanity check.  Without these, the checkout is meaningless. */
  assert (path != NULL);
  assert (URL != NULL);

  /* Canonicalize the URL. */
  svn_path_canonicalize (URL, svn_path_repos_style);

  /* Fetch the checkout editor.  If REVISION is invalid, that's okay;
     either the RA or XML driver will call editor->set_target_revision
     later on. */
  SVN_ERR (svn_wc_get_checkout_editor (path,
                                       URL,
                                       revision,
                                       &checkout_editor,
                                       &checkout_edit_baton,
                                       pool));

  /* Wrap it up with outside editors. */
  svn_delta_wrap_editor (&checkout_editor, &checkout_edit_baton,
                         before_editor, before_edit_baton,
                         checkout_editor, checkout_edit_baton,
                         after_editor, after_edit_baton, pool);

  /* if using an RA layer */
  if (! xml_src)
    {
      void *ra_baton, *session, *cb_baton;
      svn_ra_plugin_t *ra_lib;
      svn_ra_callbacks_t *ra_callbacks;

      /* Get the RA vtable that matches URL. */
      SVN_ERR (svn_ra_init_ra_libs (&ra_baton, pool));
      SVN_ERR (svn_ra_get_ra_library (&ra_lib, ra_baton, URL->data, pool));

      /* Open an RA session to URL. */
      SVN_ERR (svn_client__get_ra_callbacks (&ra_callbacks, &cb_baton,
                                             auth_baton, path, TRUE,
                                             FALSE, pool));
      SVN_ERR (ra_lib->open (&session, URL,
                             ra_callbacks, cb_baton, pool));

      /* Decide which revision to get: */

      /* If both REVISION and TM are specified, this is an error.
         They mostly likely contradict one another. */
      if ((revision != SVN_INVALID_REVNUM) && tm)
        return
          svn_error_create(SVN_ERR_CL_MUTUALLY_EXCLUSIVE_ARGS, 0, NULL, pool,
                           "Cannot specify _both_ revision and time.");

      /* If only TM is given, convert the time into a revision number. */
      else if (tm)
        SVN_ERR (ra_lib->get_dated_revision (session, &revision, tm));

      /* Tell RA to do a checkout of REVISION; if we pass an invalid
         revnum, that means RA will fetch the latest revision.  */
      SVN_ERR (ra_lib->do_checkout (session,
                                    revision,
                                    checkout_editor,
                                    checkout_edit_baton));

      /* Close the RA session. */
      SVN_ERR (ra_lib->close (session));
    }      
  
  /* else we're checking out from xml */
  else
    {
      apr_status_t apr_err;
      apr_file_t *in = NULL;

      /* Open xml file. */
      apr_err = apr_file_open (&in, xml_src->data, (APR_READ | APR_CREATE),
                               APR_OS_DEFAULT, pool);
      if (apr_err)
        return svn_error_createf (apr_err, 0, NULL, pool,
                                  "unable to open %s", xml_src->data);

      /* Do a checkout by xml-parsing the stream.  An invalid revnum
         means that there will be a revision number in the <delta-pkg>
         tag.  Otherwise, a valid revnum will be stored in the wc,
         assuming there's no <delta-pkg> tag to override it. */
      SVN_ERR (svn_delta_xml_auto_parse (svn_stream_from_aprfile (in, pool),
                                         checkout_editor,
                                         checkout_edit_baton,
                                         URL,
                                         revision,
                                         pool));
      /* Close XML file. */
      apr_file_close (in);
    }

  return SVN_NO_ERROR;
}



/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end: */
