/*
 * status.c:  return the status of a working copy dirent
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
#include <apr_strings.h>
#include <apr_pools.h>
#include <apr_hash.h>

#include "client.h"

#include "svn_wc.h"
#include "svn_delta.h"
#include "svn_client.h"
#include "svn_string.h"
#include "svn_error.h"
#include "svn_path.h"
#include "svn_test.h"
#include "svn_io.h"



/*** Getting update information ***/

struct status_baton
{
  svn_boolean_t deleted_in_repos;          /* target is deleted in repos */
  svn_wc_status_func_t real_status_func;   /* real status function */
  void *real_status_baton;                 /* real status baton */
};

/* A status callback function which wraps the *real* status
   function/baton.   This sucker takes care of any status tweaks we
   need to make (such as noting that the target of the status is
   missing from HEAD in the repository).  */
static void
tweak_status (void *baton,
              const char *path,
              svn_wc_status_t *status)
{
  struct status_baton *sb = baton;

  /* If we know that the target was deleted in HEAD of the repository,
     we need to note that fact in all the status structures that come
     through here. */
  if (sb->deleted_in_repos)
    status->repos_text_status = svn_wc_status_deleted;

  /* Call the real status function/baton. */
  sb->real_status_func (sb->real_status_baton, path, status);
}



/*** Public Interface. ***/


svn_error_t *
svn_client_status (svn_revnum_t *youngest,
                   const char *path,
                   svn_opt_revision_t *revision,
                   svn_wc_status_func_t status_func,
                   void *status_baton,
                   svn_boolean_t descend,
                   svn_boolean_t get_all,
                   svn_boolean_t update,
                   svn_boolean_t no_ignore,
                   svn_client_ctx_t *ctx,
                   apr_pool_t *pool)
{
  svn_wc_adm_access_t *adm_access;
  svn_wc_traversal_info_t *traversal_info = svn_wc_init_traversal_info (pool);
  const char *anchor, *target;
  const svn_delta_editor_t *editor;
  void *edit_baton;
  svn_ra_plugin_t *ra_lib;  
  const svn_wc_entry_t *entry;
  struct status_baton sb;

  sb.real_status_func = status_func;
  sb.real_status_baton = status_baton;
  sb.hash = apr_hash_make (pool);
  sb.deleted_in_repos = FALSE;

  /* Need to lock the tree as even a non-recursive status requires the
     immediate directories to be locked. */
  SVN_ERR (svn_wc_adm_probe_open (&adm_access, NULL, path, 
                                  FALSE, FALSE, pool));

  /* Get the entry for this path so we can determine our anchor and
     target.  If the path is unversioned, and the caller requested
     that we contact the repository, we error. */
  SVN_ERR (svn_wc_entry (&entry, path, adm_access, FALSE, pool));
  if (entry)
    SVN_ERR (svn_wc_get_actual_target (path, &anchor, &target, pool));
  else if (! update)
    svn_path_split (path, &anchor, &target, pool);
  else
    return svn_error_createf (SVN_ERR_ENTRY_NOT_FOUND, NULL,
                              "'%s' is not a versioned resource", path);
  
  /* Close up our ADM area.  We'll be re-opening soon. */
  SVN_ERR (svn_wc_adm_close (adm_access));

  /* Need to lock the tree as even a non-recursive status requires the
     immediate directories to be locked. */
  SVN_ERR (svn_wc_adm_probe_open (&adm_access, NULL, anchor, 
                                  FALSE, TRUE, pool));

  /* Get the status edit, and use our wrapping status function/baton
     as the callback pair. */
  SVN_ERR (svn_wc_get_status_editor (&editor, &edit_baton, youngest,
                                     adm_access, target, ctx->config, descend,
                                     get_all, no_ignore, tweak_status, &sb,
                                     ctx->cancel_func, ctx->cancel_baton,
                                     traversal_info, pool));

  /* If this is a real update, we crawl the working copy and let the
     RA layer drive the editor for real.  Otherwise, we just close the
     edit.  :-) */ 
  if (update)
    {
      void *ra_baton, *session, *report_baton;
      const svn_ra_reporter_t *reporter;
      const char *URL;
      svn_wc_adm_access_t *anchor_access;
      svn_node_kind_t kind;
      svn_revnum_t revnum;

      /* Using pool cleanup to close it. This needs to be recursive so that
         auth data can be stored. */
      if (strlen (anchor) != strlen (path))
        SVN_ERR (svn_wc_adm_open (&anchor_access, NULL, anchor, FALSE, 
                                  TRUE, pool));
      else
        anchor_access = adm_access;

      /* Get full URL from the ANCHOR. */
      SVN_ERR (svn_wc_entry (&entry, anchor, anchor_access, FALSE, pool));
      if (! entry)
        return svn_error_createf
          (SVN_ERR_ENTRY_NOT_FOUND, NULL,
           "svn_client_status: '%s' is not under revision control", anchor);
      if (! entry->url)
        return svn_error_createf
          (SVN_ERR_ENTRY_MISSING_URL, NULL,
           "svn_client_status: entry '%s' has no URL", anchor);
      URL = apr_pstrdup (pool, entry->url);

      /* Get the RA library that handles URL. */
      SVN_ERR (svn_ra_init_ra_libs (&ra_baton, pool));
      SVN_ERR (svn_ra_get_ra_library (&ra_lib, ra_baton, URL, pool));

      /* Open a repository session to the URL. */
      SVN_ERR (svn_client__open_ra_session (&session, ra_lib, URL, anchor,
                                            anchor_access, NULL, TRUE, TRUE, 
                                            ctx, pool));

      /* Verify that URL exists in HEAD.  If it doesn't, this can save
         us a whole lot of hassle; if it does, the cost of this
         request should be minimal compared to the size of getting
         back the average amount of "out-of-date" information. */
      SVN_ERR (ra_lib->check_path (&kind, session, "", 
                                   SVN_INVALID_REVNUM, pool));
      if (kind == svn_node_none)
        {
          /* Note that our status target has been deleted from HEAD of
             the repository. */
          sb.deleted_in_repos = TRUE;

          /* And now close the edit. */
          SVN_ERR (editor->close_edit (edit_baton, pool));
        }
      else
        {
          svn_wc_adm_access_t *tgt_access;
          
          /* Get a revision number for our status operation. */
          SVN_ERR (svn_client__get_revision_number
                   (&revnum, ra_lib, session, revision, target, pool));

          /* Do the deed.  Let the RA layer drive the status editor. */
          SVN_ERR (ra_lib->do_status (session, &reporter, &report_baton,
                                      target, revnum, descend, editor, 
                                      edit_baton, pool));

          /* Drive the reporter structure, describing the revisions
             within PATH.  When we call reporter->finish_report,
             EDITOR will be driven to describe differences between our
             working copy and HEAD. */
          SVN_ERR (svn_wc_adm_probe_retrieve (&tgt_access, adm_access, 
                                              path, pool));
          SVN_ERR (svn_wc_crawl_revisions (path, tgt_access, reporter, 
                                           report_baton, FALSE, descend, 
                                           FALSE, NULL, NULL, NULL, pool));
        }
    }
  else
    {
      SVN_ERR (editor->close_edit (edit_baton, pool));
    }

  if (ctx->notify_func && update)
    (ctx->notify_func) (ctx->notify_baton,
                        path,
                        svn_wc_notify_status_completed,
                        svn_node_unknown,
                        NULL,
                        svn_wc_notify_state_unknown,
                        svn_wc_notify_state_unknown,
                        *youngest);

  /* Close the access baton here, as svn_client__recognize_externals()
     calls back into this function (and thus will be re-opening the
     working copy. */
  SVN_ERR (svn_wc_adm_close (adm_access));

  /* If there are svn:externals set, we don't want those to show up as
     unversioned or unrecognized, so patchup the hash.  If callers wants
     all the statuses, we will change unversioned status items that
     are interesting to an svn:externals property to
     svn_wc_status_unversioned, otherwise we'll just remove the status
     item altogether. */
  if (descend)
    SVN_ERR (svn_client__do_external_status (traversal_info, status_func,
                                             status_baton, get_all, update,
                                             no_ignore, ctx, pool));

  return SVN_NO_ERROR;
}
