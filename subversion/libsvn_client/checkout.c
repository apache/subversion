/*
 * checkout.c:  wrappers around wc checkout functionality
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

#include <assert.h>
#include "svn_wc.h"
#include "svn_pools.h"
#include "svn_delta.h"
#include "svn_client.h"
#include "svn_ra.h"
#include "svn_string.h"
#include "svn_types.h"
#include "svn_error.h"
#include "svn_path.h"
#include "svn_io.h"
#include "svn_opt.h"
#include "client.h"



/*** Public Interfaces. ***/


svn_error_t *
svn_client_checkout (svn_wc_notify_func_t notify_func,
                     void *notify_baton,
                     svn_client_auth_baton_t *auth_baton,
                     const char *URL,
                     const char *path,
                     const svn_opt_revision_t *revision,
                     svn_boolean_t recurse,
                     apr_pool_t *pool)
{
  const svn_delta_editor_t *checkout_editor;
  void *checkout_edit_baton;
  svn_wc_traversal_info_t *traversal_info = svn_wc_init_traversal_info (pool);
  svn_error_t *err;
  svn_revnum_t revnum;

  /* Sanity check.  Without these, the checkout is meaningless. */
  assert (path != NULL);
  assert (URL != NULL);

  /* Get revnum set to something meaningful, so we can fetch the
     checkout editor. */
  if (revision->kind == svn_opt_revision_number)
    revnum = revision->value.number; /* do the trivial conversion manually */
  else
    revnum = SVN_INVALID_REVNUM; /* no matter, do real conversion later */

  /* Canonicalize the URL. */
  URL = svn_path_canonicalize_nts (URL, pool);

  /* Fetch the checkout editor.  If REVISION is invalid, that's okay;
     the RA driver will call editor->set_target_revision
     later on. */
  SVN_ERR (svn_wc_get_checkout_editor (path,
                                       URL,
                                       revnum,
                                       recurse,
                                       notify_func,
                                       notify_baton,
                                       &checkout_editor,
                                       &checkout_edit_baton,
                                       traversal_info,
                                       pool));

    {
      void *ra_baton, *session;
      svn_ra_plugin_t *ra_lib;

      /* Get the RA vtable that matches URL. */
      SVN_ERR (svn_ra_init_ra_libs (&ra_baton, pool));
      SVN_ERR (svn_ra_get_ra_library (&ra_lib, ra_baton, URL, pool));

      /* Open an RA session to URL. Note that we do not have an admin area
         for storing temp files.  We do, however, want to store auth data
         after the checkout builds the WC. */
      SVN_ERR (svn_client__open_ra_session (&session, ra_lib, URL, path,
                                            NULL, NULL, TRUE, FALSE, TRUE,
                                            auth_baton, pool));

      SVN_ERR (svn_client__get_revision_number
               (&revnum, ra_lib, session, revision, path, pool));

      /* Tell RA to do a checkout of REVISION; if we pass an invalid
         revnum, that means RA will fetch the latest revision.  */
      err = ra_lib->do_checkout (session,
                                 revnum,
                                 recurse,
                                 checkout_editor,
                                 checkout_edit_baton);
      /* Sleep for one second to ensure timestamp integrity. */
      apr_sleep (APR_USEC_PER_SEC * 1);
      
      if (err)
        return err;

      /* Close the RA session. */
      SVN_ERR (ra_lib->close (session));
    }      
  
  /* We handle externals after the initial checkout is complete, so
     that fetching external items (and any errors therefrom) doesn't
     delay the primary checkout.  */
  SVN_ERR (svn_client__handle_externals
           (traversal_info,
            notify_func, notify_baton,
            auth_baton,
            FALSE,
            pool));

  return SVN_NO_ERROR;
}



/* 
 * local variables:
 * eval: (load-file "../../tools/dev/svn-dev.el")
 * end: */
