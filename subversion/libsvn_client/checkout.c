/*
 * checkout.c:  wrappers around wc checkout functionality
 *
 * ====================================================================
 * Copyright (c) 2000 CollabNet.  All rights reserved.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution.  The terms
 * are also available at http://subversion.tigris.org/license-1.html.
 * If newer versions of this license are posted there, you may use a
 * newer version instead, at your option.
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

/* ben sez: this is a single, readable routine now -- not sliced into
   a million onion layers anymore.  I maintain that it no longer makes
   sense to share code with svn_client_update();  other than fetching
   the same wc editor, the logic of these routines has started
   diverging (remember that `update' needs to report state to the RA
   layer.) */

/* Perform a checkout from URL, providing pre- and post-checkout hook
   editors and batons (BEFORE_EDITOR, BEFORE_EDIT_BATON /
   AFTER_EDITOR, AFTER_EDIT_BATON).

   PATH will be the root directory of your checked out working copy.

   If XML_SRC is NULL, then the checkout will come from the repository
   and subdir specified by URL.  An invalid REVISION will cause the
   "latest" tree to be fetched, while a valid REVISION will fetch a
   specific tree.

   If XML_SRC is non-NULL, it is an xml file to check out from; in
   this case, the working copy will record the URL as artificial
   ancestry information.  An invalid REVISION implies that the
   revision *must* be present in the <delta-pkg> tag, while a valid
   REVISION will be simply be stored in the wc. (Note:  a <delta-pkg>
   revision will *always* override the one passed in.)

   This operation will use the provided memory POOL. */

svn_error_t *
svn_client_checkout (const svn_delta_edit_fns_t *before_editor,
                     void *before_edit_baton,
                     const svn_delta_edit_fns_t *after_editor,
                     void *after_edit_baton,
                     svn_string_t *URL,
                     svn_string_t *path,
                     svn_revnum_t revision,
                     svn_string_t *xml_src,
                     apr_pool_t *pool)
{
  const svn_delta_edit_fns_t *checkout_editor;
  void *checkout_edit_baton;

  /* Sanity check.  Without these, the checkout is meaningless. */
  assert (path != NULL);
  assert (URL != NULL);

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
      void *ra_baton, *session;
      svn_ra_plugin_t *ra_lib;

      /* Get the RA vtable that matches URL. */
      SVN_ERR (svn_ra_init_ra_libs (&ra_baton, pool));
      SVN_ERR (svn_ra_get_ra_library (&ra_lib, ra_baton, URL->data, pool));

      /* Open an RA session to URL */
      SVN_ERR (ra_lib->open (&session, URL, pool));
      
      /* Tell RA to do a checkout of REVISION; if we pass an invalid
         revnum, that means RA will fetch the latest revision.  */

      /* ben sez:  todo:  update RA interface here to take REVISION. */
      SVN_ERR (ra_lib->do_checkout (session,
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
