/*
 * status.c:  return the status of a working copy dirent
 *
 * ====================================================================
 * Copyright (c) 2000-2004 CollabNet.  All rights reserved.
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

#include "svn_private_config.h"


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
svn_client_status (svn_revnum_t *result_rev,
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
  svn_wc_adm_access_t *anchor_access, *target_access;
  svn_wc_traversal_info_t *traversal_info = svn_wc_init_traversal_info (pool);
  const char *anchor, *target;
  const svn_delta_editor_t *editor;
  void *edit_baton;
  svn_ra_plugin_t *ra_lib;  
  const svn_wc_entry_t *entry;
  struct status_baton sb;
  svn_revnum_t edit_revision = SVN_INVALID_REVNUM;
  svn_error_t *err;

  sb.real_status_func = status_func;
  sb.real_status_baton = status_baton;
  sb.deleted_in_repos = FALSE;

  /* First checks do not require a lock on the working copy.  We will
     reopen the working copy with a lock later. */
  SVN_ERR (svn_wc_adm_probe_open2 (&target_access, NULL, path, 
                                   FALSE, 0, pool));

  /* Get the entry for this path so we can determine our anchor and
     target.  If the path is unversioned, and the caller requested
     that we contact the repository, we error. */
  SVN_ERR (svn_wc_entry (&entry, path, target_access, FALSE, pool));

  /* Close up our ADM area and reopen it with the depth we want. */
  SVN_ERR (svn_wc_adm_close (target_access));

  if (entry)
    SVN_ERR (svn_wc_get_actual_target (path, &anchor, &target, pool));
  else if (! update)
    svn_path_split (path, &anchor, &target, pool);
  else
    return svn_error_createf (SVN_ERR_UNVERSIONED_RESOURCE, NULL,
                              _("'%s' is not under version control"), path);
  
  /* Need to lock the tree.  We lock the anchor first, to not lock too much
     if we have a target.  We need to lock the target and immediate children
     if the status is non-recursive.  Else, we lock the whole hierarchy
     under target.  If we are contacting the repository, we need a recursive
     lock under anchor so that auth data can be stored. */

  if (update)
    {
      SVN_ERR (svn_wc_adm_open2 (&anchor_access, NULL, anchor,
                                 FALSE, -1, pool));
      SVN_ERR (svn_wc_adm_probe_retrieve (&target_access, anchor_access,
                                          path, pool));
    }
  else
    {
      SVN_ERR (svn_wc_adm_probe_open2 (&anchor_access, NULL, anchor, 
                                       FALSE,
                                       *target ? 0 : (descend ? -1 : 1), pool));
      if (*target)
        {
          /* Careful! svn_adm_probe_open2 may lock the parent in certain
             situations, which results in an already locked error.  We use the
             svn_wc_adm_open2 function instead, since we already have the
             parent locked. */
          err = svn_wc_adm_open2 (&target_access, anchor_access, path,
                                  FALSE, (descend || update) ? -1 : 1, pool);
          /* If target is not a versioned directory, then that's fine: we need
             no locking. */
          if (err && err->apr_err == SVN_ERR_WC_NOT_DIRECTORY)
            {
              svn_error_clear (err);
              target_access = anchor_access;
            }
          else
            SVN_ERR (err);
        }
      else
        target_access = anchor_access;
    }

  /* Get the status edit, and use our wrapping status function/baton
     as the callback pair. */
  SVN_ERR (svn_wc_get_status_editor (&editor, &edit_baton, &edit_revision,
                                     anchor_access, target, ctx->config,
                                     descend, get_all, no_ignore, tweak_status,
                                     &sb, ctx->cancel_func, ctx->cancel_baton,
                                     traversal_info, pool));

  /* If we want to know about out-of-dateness, we crawl the working copy and
     let the RA layer drive the editor for real.  Otherwise, we just close the
     edit.  :-) */ 
  if (update)
    {
      void *ra_baton, *session, *report_baton;
      const svn_ra_reporter_t *reporter;
      const char *URL;
      svn_node_kind_t kind;

      /* Get full URL from the ANCHOR. */
      SVN_ERR (svn_wc_entry (&entry, anchor, anchor_access, FALSE, pool));
      if (! entry)
        return svn_error_createf
          (SVN_ERR_UNVERSIONED_RESOURCE, NULL,
           _("'%s' is not under version control"), anchor);
      if (! entry->url)
        return svn_error_createf
          (SVN_ERR_ENTRY_MISSING_URL, NULL,
           _("Entry '%s' has no URL"), anchor);
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
      SVN_ERR (ra_lib->check_path (session, "", SVN_INVALID_REVNUM,
                                   &kind, pool));
      if (kind == svn_node_none)
        {
          /* Our status target does not exist in HEAD of the
             repository.  If we're just adding this thing, that's
             fine.  But if it was previously versioned, then it must
             have been deleted from the repository. */
          if (entry->schedule != svn_wc_schedule_add)
            sb.deleted_in_repos = TRUE;

          /* And now close the edit. */
          SVN_ERR (editor->close_edit (edit_baton, pool));
        }
      else
        {
          svn_revnum_t revnum;
            
          if (revision->kind == svn_opt_revision_head)
            {
              /* Cause the revision number to be omitted from the request,
                 which implies HEAD. */
              revnum = SVN_INVALID_REVNUM;
            }
          else
            {
              /* Get a revision number for our status operation. */
              SVN_ERR (svn_client__get_revision_number
                       (&revnum, ra_lib, session, revision, target, pool));
            }

          /* Do the deed.  Let the RA layer drive the status editor. */
          SVN_ERR (ra_lib->do_status (session, &reporter, &report_baton,
                                      target, revnum, descend, editor, 
                                      edit_baton, pool));

          /* Drive the reporter structure, describing the revisions
             within PATH.  When we call reporter->finish_report,
             EDITOR will be driven to describe differences between our
             working copy and HEAD. */
          SVN_ERR (svn_wc_crawl_revisions (path, target_access, reporter, 
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
                        edit_revision);

  /* If the caller wants the result revision, give it to them. */
  if (result_rev)
    *result_rev = edit_revision;

  /* Close the access baton here, as svn_client__do_external_status()
     calls back into this function and thus will be re-opening the
     working copy. */
  SVN_ERR (svn_wc_adm_close (anchor_access));

  /* If there are svn:externals set, we don't want those to show up as
     unversioned or unrecognized, so patch up the hash.  If caller wants
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
