/*
 * checkout.c:  wrappers around wc checkout functionality
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
#include "svn_time.h"
#include "client.h"



/*** Public Interfaces. ***/


svn_error_t *
svn_client__checkout_internal (const char *URL,
                               const char *path,
                               const svn_opt_revision_t *revision,
                               svn_boolean_t recurse,
                               svn_boolean_t *timestamp_sleep,
                               svn_client_ctx_t *ctx,
                               apr_pool_t *pool)
{
  svn_wc_traversal_info_t *traversal_info = svn_wc_init_traversal_info (pool);
  svn_error_t *err = NULL;
  svn_revnum_t revnum;
  svn_boolean_t sleep_here = FALSE;
  svn_boolean_t *use_sleep = timestamp_sleep ? timestamp_sleep : &sleep_here;

  /* Sanity check.  Without these, the checkout is meaningless. */
  assert (path != NULL);
  assert (URL != NULL);

  /* Fulfill the docstring promise of svn_client_checkout: */
  if ((revision->kind != svn_opt_revision_number)
      && (revision->kind != svn_opt_revision_date)
      && (revision->kind != svn_opt_revision_head))
    return svn_error_create (SVN_ERR_CLIENT_BAD_REVISION, NULL,
                             "Bogus revision passed to svn_client_checkout");

  /* Canonicalize the URL. */
  URL = svn_path_canonicalize (URL, pool);

    {
      void *ra_baton, *session;
      svn_ra_plugin_t *ra_lib;
      svn_node_kind_t kind;

      /* Get the RA vtable that matches URL. */
      SVN_ERR (svn_ra_init_ra_libs (&ra_baton, pool));
      SVN_ERR (svn_ra_get_ra_library (&ra_lib, ra_baton, URL, pool));

      /* Open an RA session to URL. Note that we do not have an admin area
         for storing temp files.  We do, however, want to store auth data
         after the checkout builds the WC. */
      SVN_ERR (svn_client__open_ra_session (&session, ra_lib, URL, path,
                                            NULL, NULL, FALSE, TRUE,
                                            ctx, pool));

      SVN_ERR (svn_client__get_revision_number
               (&revnum, ra_lib, session, revision, path, pool));

      SVN_ERR (ra_lib->check_path (session, "", revnum, &kind, pool));
      if (kind == svn_node_none)
        return svn_error_createf (SVN_ERR_RA_ILLEGAL_URL, NULL,
                                  "Source URL doesn't exist: %s.", URL);

      SVN_ERR (svn_io_check_path (path, &kind, pool));

      if (kind == svn_node_none)
        {
          /* Bootstrap: create an incomplete working-copy root dir.  Its
             entries file should only have an entry for THIS_DIR with a
             URL, revnum, and an 'incomplete' flag.  */
          SVN_ERR (svn_io_make_dir_recursively (path, pool));          
          SVN_ERR (svn_wc_ensure_adm (path, URL, revnum, pool));
          
          /* Have update fix the incompleteness. */
          err = svn_client_update (path, revision, recurse, ctx, pool);
        }
      else if (kind == svn_node_dir)
        {
          int wc_format;
          const svn_wc_entry_t *entry;
          svn_wc_adm_access_t *adm_access;

          SVN_ERR (svn_wc_check_wc (path, &wc_format, pool));
          if (! wc_format)
            {
              /* Make the unversioned directory into a versioned one. */
              SVN_ERR (svn_wc_ensure_adm (path, URL, revnum, pool));
              err = svn_client_update (path, revision, recurse, ctx, pool);
              goto done;
            }

          /* Get PATH's entry. */
          SVN_ERR (svn_wc_adm_open (&adm_access, NULL, path,
                                    FALSE, FALSE, pool));
          SVN_ERR (svn_wc_entry (&entry, path, adm_access, FALSE, pool));
          SVN_ERR (svn_wc_adm_close (adm_access));

          /* If PATH's existing URL matches the incoming one, then
             just update.  This allows 'svn co' to restart an
             interrupted checkout. */
          if (entry->url && (strcmp (entry->url, URL) == 0))
            {
              err = svn_client_update (path, revision, recurse, ctx, pool);
            }
          else
            {
              const char *errmsg;
              errmsg = apr_psprintf 
                (pool,
                 "'%s' is already a working copy for a different URL", path);
              if (entry->incomplete)
                errmsg = apr_pstrcat
                  (pool, errmsg, "; run 'svn update' to complete it.", NULL);

              return svn_error_create (SVN_ERR_WC_OBSTRUCTED_UPDATE, NULL,
                                       errmsg);
            }
        }
      else
        {
          return svn_error_createf (SVN_ERR_WC_NODE_KIND_CHANGE, NULL,
                                    "'%s' is already a file/something else.",
                                    path);
        }

    done:
      if (err)
        {
          /* Don't rely on the error handling to handle the sleep later, do
             it now */
          svn_sleep_for_timestamps ();
          return err;
        }
      *use_sleep = TRUE;
    }      
  
  /* We handle externals after the initial checkout is complete, so
     that fetching external items (and any errors therefrom) doesn't
     delay the primary checkout.

     ### Should we really do externals if recurse is false?
  */
  SVN_ERR (svn_client__handle_externals (traversal_info, FALSE, use_sleep,
                                         ctx, pool));
  if (sleep_here)
    svn_sleep_for_timestamps ();

  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_checkout (const char *URL,
                     const char *path,
                     const svn_opt_revision_t *revision,
                     svn_boolean_t recurse,
                     svn_client_ctx_t *ctx,
                     apr_pool_t *pool)
{
  return svn_client__checkout_internal (URL, path, revision, recurse, NULL, ctx,
                                        pool);
}
