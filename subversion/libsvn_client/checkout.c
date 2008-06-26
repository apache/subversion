/*
 * checkout.c:  wrappers around wc checkout functionality
 *
 * ====================================================================
 * Copyright (c) 2000-2006 CollabNet.  All rights reserved.
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

#include "svn_pools.h"
#include "svn_wc.h"
#include "svn_client.h"
#include "svn_ra.h"
#include "svn_types.h"
#include "svn_error.h"
#include "svn_path.h"
#include "svn_io.h"
#include "svn_opt.h"
#include "svn_time.h"
#include "client.h"

#include "svn_private_config.h"


/*** Public Interfaces. ***/


svn_error_t *
svn_client__checkout_internal(svn_revnum_t *result_rev,
                              const char *url,
                              const char *path,
                              const svn_opt_revision_t *peg_revision,
                              const svn_opt_revision_t *revision,
                              svn_depth_t depth,
                              svn_boolean_t ignore_externals,
                              svn_boolean_t allow_unver_obstructions,
                              svn_boolean_t *timestamp_sleep,
                              svn_client_ctx_t *ctx,
                              apr_pool_t *pool)
{
  svn_error_t *err = NULL;
  svn_revnum_t revnum;
  svn_boolean_t sleep_here = FALSE;
  svn_boolean_t *use_sleep = timestamp_sleep ? timestamp_sleep : &sleep_here;
  const char *session_url;

  /* Sanity check.  Without these, the checkout is meaningless. */
  if (! path)
    return svn_error_createf
      (SVN_ERR_INCORRECT_PARAMS, NULL,
       _("svn_client__checkout_internal() must be passed a non-NULL path"));
  if (! url)
    return svn_error_createf
      (SVN_ERR_INCORRECT_PARAMS, NULL,
       _("svn_client__checkout_internal() must be passed a non-NULL url"));

  /* Fulfill the docstring promise of svn_client_checkout: */
  if ((revision->kind != svn_opt_revision_number)
      && (revision->kind != svn_opt_revision_date)
      && (revision->kind != svn_opt_revision_head))
    return svn_error_create(SVN_ERR_CLIENT_BAD_REVISION, NULL, NULL);

  /* Canonicalize the URL. */
  url = svn_path_canonicalize(url, pool);

  {
    svn_ra_session_t *ra_session;
    svn_node_kind_t kind;
    const char *uuid, *repos;
    apr_pool_t *session_pool = svn_pool_create(pool);

    /* Get the RA connection. */
    SVN_ERR(svn_client__ra_session_from_path(&ra_session, &revnum,
                                             &session_url, url, NULL,
                                             peg_revision, revision, ctx,
                                             session_pool));

    SVN_ERR(svn_ra_check_path(ra_session, "", revnum, &kind, pool));
    if (kind == svn_node_none)
      return svn_error_createf(SVN_ERR_RA_ILLEGAL_URL, NULL,
                               _("URL '%s' doesn't exist"), session_url);
    else if (kind == svn_node_file)
      return svn_error_createf
        (SVN_ERR_UNSUPPORTED_FEATURE , NULL,
         _("URL '%s' refers to a file, not a directory"), session_url);

    /* Get the repos UUID and root URL. */
    SVN_ERR(svn_ra_get_uuid2(ra_session, &uuid, session_pool));
    SVN_ERR(svn_ra_get_repos_root2(ra_session, &repos, session_pool));

    SVN_ERR(svn_io_check_path(path, &kind, pool));

    /* Finished with the RA session -- close up, but not without
       copying out useful information that needs to survive.  */
    session_url = apr_pstrdup(pool, session_url);
    uuid = (uuid ? apr_pstrdup(pool, uuid) : NULL);
    repos = (repos ? apr_pstrdup(pool, repos) : NULL);
    svn_pool_destroy(session_pool);

    if (kind == svn_node_none)
      {
        /* Bootstrap: create an incomplete working-copy root dir.  Its
           entries file should only have an entry for THIS_DIR with a
           URL, revnum, and an 'incomplete' flag.  */
        SVN_ERR(svn_io_make_dir_recursively(path, pool));
        goto initialize_area;
      }
    else if (kind == svn_node_dir)
      {
        int wc_format;
        const svn_wc_entry_t *entry;
        svn_wc_adm_access_t *adm_access;

        SVN_ERR(svn_wc_check_wc(path, &wc_format, pool));
        if (! wc_format)
          {
          initialize_area:

            if (depth == svn_depth_unknown)
              depth = svn_depth_infinity;

            /* Make the unversioned directory into a versioned one.  */
            SVN_ERR(svn_wc_ensure_adm3(path, uuid, session_url,
                                       repos, revnum, depth, pool));
            /* Have update fix the incompleteness. */
            err = svn_client__update_internal(result_rev, path, revision,
                                              depth, TRUE, ignore_externals,
                                              allow_unver_obstructions,
                                              use_sleep, FALSE,
                                              ctx, pool);
            goto done;
          }

        /* Get PATH's entry. */
        SVN_ERR(svn_wc_adm_open3(&adm_access, NULL, path,
                                 FALSE, 0, ctx->cancel_func,
                                 ctx->cancel_baton, pool));
        SVN_ERR(svn_wc_entry(&entry, path, adm_access, FALSE, pool));
        SVN_ERR(svn_wc_adm_close(adm_access));

        /* If PATH's existing URL matches the incoming one, then
           just update.  This allows 'svn co' to restart an
           interrupted checkout. */
        if (entry->url && (strcmp(entry->url, session_url) == 0))
          {
            err = svn_client__update_internal(result_rev, path, revision,
                                              depth, TRUE, ignore_externals,
                                              allow_unver_obstructions,
                                              use_sleep, FALSE,
                                              ctx, pool);
          }
        else
          {
            const char *errmsg;
            errmsg = apr_psprintf
              (pool,
               _("'%s' is already a working copy for a different URL"),
               svn_path_local_style(path, pool));
            if (entry->incomplete)
              errmsg = apr_pstrcat
                (pool, errmsg, _("; run 'svn update' to complete it"), NULL);

            return svn_error_create(SVN_ERR_WC_OBSTRUCTED_UPDATE, NULL,
                                    errmsg);
          }
      }
    else
      {
        return svn_error_createf
          (SVN_ERR_WC_NODE_KIND_CHANGE, NULL,
           _("'%s' already exists and is not a directory"),
           svn_path_local_style(path, pool));
      }

  done:
    if (err)
      {
        /* Don't rely on the error handling to handle the sleep later, do
           it now */
        svn_sleep_for_timestamps();
        return err;
      }
    *use_sleep = TRUE;
  }

  if (sleep_here)
    svn_sleep_for_timestamps();

  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_checkout3(svn_revnum_t *result_rev,
                     const char *URL,
                     const char *path,
                     const svn_opt_revision_t *peg_revision,
                     const svn_opt_revision_t *revision,
                     svn_depth_t depth,
                     svn_boolean_t ignore_externals,
                     svn_boolean_t allow_unver_obstructions,
                     svn_client_ctx_t *ctx,
                     apr_pool_t *pool)
{
  return svn_client__checkout_internal(result_rev, URL, path, peg_revision,
                                       revision, depth, ignore_externals,
                                       allow_unver_obstructions, NULL, ctx,
                                       pool);
}

svn_error_t *
svn_client_checkout2(svn_revnum_t *result_rev,
                     const char *URL,
                     const char *path,
                     const svn_opt_revision_t *peg_revision,
                     const svn_opt_revision_t *revision,
                     svn_boolean_t recurse,
                     svn_boolean_t ignore_externals,
                     svn_client_ctx_t *ctx,
                     apr_pool_t *pool)
{
  return svn_client__checkout_internal(result_rev, URL, path, peg_revision,
                                       revision,
                                       SVN_DEPTH_INFINITY_OR_FILES(recurse),
                                       ignore_externals, FALSE, NULL, ctx,
                                       pool);
}

svn_error_t *
svn_client_checkout(svn_revnum_t *result_rev,
                    const char *URL,
                    const char *path,
                    const svn_opt_revision_t *revision,
                    svn_boolean_t recurse,
                    svn_client_ctx_t *ctx,
                    apr_pool_t *pool)
{
  svn_opt_revision_t peg_revision;

  peg_revision.kind = svn_opt_revision_unspecified;

  return svn_client__checkout_internal(result_rev, URL, path, &peg_revision,
                                       revision,
                                       SVN_DEPTH_INFINITY_OR_FILES(recurse),
                                       FALSE, FALSE, NULL, ctx, pool);
}
