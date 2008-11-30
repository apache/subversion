/*
 * status.c:  return the status of a working copy dirent
 *
 * ====================================================================
 * Copyright (c) 2000-2007 CollabNet.  All rights reserved.
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

#include "svn_pools.h"
#include "client.h"

#include "svn_path.h"
#include "svn_delta.h"
#include "svn_client.h"
#include "svn_error.h"
#include "svn_hash.h"

#include "svn_private_config.h"
#include "private/svn_wc_private.h"


/*** Getting update information ***/

/* Baton for tweak_status.  It wraps a bit of extra functionality
   around the received status func/baton, so we can remember if the
   target was deleted in HEAD and tweak incoming status structures
   accordingly. */
struct status_baton
{
  svn_boolean_t deleted_in_repos;          /* target is deleted in repos */
  apr_hash_t *changelist_hash;             /* keys are changelist names */
  svn_wc_status_func3_t real_status_func;  /* real status function */
  void *real_status_baton;                 /* real status baton */
};

/* A status callback function which wraps the *real* status
   function/baton.   This sucker takes care of any status tweaks we
   need to make (such as noting that the target of the status is
   missing from HEAD in the repository).

   This implements the 'svn_wc_status_func3_t' function type. */
static svn_error_t *
tweak_status(void *baton,
             const char *path,
             svn_wc_status2_t *status,
             apr_pool_t *pool)
{
  struct status_baton *sb = baton;

  /* If we know that the target was deleted in HEAD of the repository,
     we need to note that fact in all the status structures that come
     through here. */
  if (sb->deleted_in_repos)
    status->repos_text_status = svn_wc_status_deleted;

  /* If the status item has an entry, but doesn't belong to one of the
     changelists our caller is interested in, we filter our this status
     transmission.  */
  if (! SVN_WC__CL_MATCH(sb->changelist_hash, status->entry))
    return SVN_NO_ERROR;

  /* Call the real status function/baton. */
  return sb->real_status_func(sb->real_status_baton, path, status, pool);
}

/* A baton for our reporter that is used to collect locks. */
typedef struct report_baton_t {
  const svn_ra_reporter3_t* wrapped_reporter;
  void *wrapped_report_baton;
  /* The common ancestor URL of all paths included in the report. */
  char *ancestor;
  void *set_locks_baton;
  svn_client_ctx_t *ctx;
  /* Pool to store locks in. */
  apr_pool_t *pool;
} report_baton_t;

/* Implements svn_ra_reporter3_t->set_path. */
static svn_error_t *
reporter_set_path(void *report_baton, const char *path,
                  svn_revnum_t revision, svn_depth_t depth,
                  svn_boolean_t start_empty, const char *lock_token,
                  apr_pool_t *pool)
{
  report_baton_t *rb = report_baton;

  return rb->wrapped_reporter->set_path(rb->wrapped_report_baton, path,
                                        revision, depth, start_empty,
                                        lock_token, pool);
}

/* Implements svn_ra_reporter3_t->delete_path. */
static svn_error_t *
reporter_delete_path(void *report_baton, const char *path, apr_pool_t *pool)
{
  report_baton_t *rb = report_baton;

  return rb->wrapped_reporter->delete_path(rb->wrapped_report_baton, path,
                                           pool);
}

/* Implements svn_ra_reporter3_t->link_path. */
static svn_error_t *
reporter_link_path(void *report_baton, const char *path, const char *url,
                   svn_revnum_t revision, svn_depth_t depth,
                   svn_boolean_t start_empty,
                   const char *lock_token, apr_pool_t *pool)
{
  report_baton_t *rb = report_baton;
  const char *ancestor;
  apr_size_t len;

  ancestor = svn_path_get_longest_ancestor(url, rb->ancestor, pool);

  /* If we got a shorter ancestor, truncate our current ancestor.
     Note that svn_path_get_longest_ancestor will allocate its return
     value even if it identical to one of its arguments. */
  len = strlen(ancestor);
  if (len < strlen(rb->ancestor))
    rb->ancestor[len] = '\0';

  return rb->wrapped_reporter->link_path(rb->wrapped_report_baton, path, url,
                                         revision, depth, start_empty,
                                         lock_token, pool);
}

/* Implements svn_ra_reporter3_t->finish_report. */
static svn_error_t *
reporter_finish_report(void *report_baton, apr_pool_t *pool)
{
  report_baton_t *rb = report_baton;
  svn_ra_session_t *ras;
  apr_hash_t *locks;
  const char *repos_root;
  apr_pool_t *subpool = svn_pool_create(pool);
  svn_error_t *err = SVN_NO_ERROR;

  /* Open an RA session to our common ancestor and grab the locks under it.
   */
  SVN_ERR(svn_client__open_ra_session_internal(&ras, rb->ancestor, NULL,
                                               NULL, NULL, FALSE, TRUE,
                                               rb->ctx, subpool));

  /* The locks need to live throughout the edit.  Note that if the
     server doesn't support lock discovery, we'll just not do locky
     stuff. */
  err = svn_ra_get_locks(ras, &locks, "", rb->pool);
  if (err && ((err->apr_err == SVN_ERR_RA_NOT_IMPLEMENTED)
              || (err->apr_err == SVN_ERR_UNSUPPORTED_FEATURE)))
    {
      svn_error_clear(err);
      err = SVN_NO_ERROR;
      locks = apr_hash_make(rb->pool);
    }
  SVN_ERR(err);

  SVN_ERR(svn_ra_get_repos_root2(ras, &repos_root, rb->pool));

  /* Close the RA session. */
  svn_pool_destroy(subpool);

  SVN_ERR(svn_wc_status_set_repos_locks(rb->set_locks_baton, locks,
                                        repos_root, rb->pool));

  return rb->wrapped_reporter->finish_report(rb->wrapped_report_baton, pool);
}

/* Implements svn_ra_reporter3_t->abort_report. */
static svn_error_t *
reporter_abort_report(void *report_baton, apr_pool_t *pool)
{
  report_baton_t *rb = report_baton;

  return rb->wrapped_reporter->abort_report(rb->wrapped_report_baton, pool);
}

/* A reporter that keeps track of the common URL ancestor of all paths in
   the WC and fetches repository locks for all paths under this ancestor. */
static svn_ra_reporter3_t lock_fetch_reporter = {
  reporter_set_path,
  reporter_delete_path,
  reporter_link_path,
  reporter_finish_report,
  reporter_abort_report
};


/*** Public Interface. ***/


svn_error_t *
svn_client_status4(svn_revnum_t *result_rev,
                   const char *path,
                   const svn_opt_revision_t *revision,
                   svn_wc_status_func3_t status_func,
                   void *status_baton,
                   svn_depth_t depth,
                   svn_boolean_t get_all,
                   svn_boolean_t update,
                   svn_boolean_t no_ignore,
                   svn_boolean_t ignore_externals,
                   const apr_array_header_t *changelists,
                   svn_client_ctx_t *ctx,
                   apr_pool_t *pool)
{
  svn_wc_adm_access_t *anchor_access, *target_access;
  svn_wc_traversal_info_t *traversal_info = svn_wc_init_traversal_info(pool);
  const char *anchor, *target;
  const svn_delta_editor_t *editor;
  void *edit_baton, *set_locks_baton;
  const svn_wc_entry_t *entry = NULL;
  struct status_baton sb;
  apr_array_header_t *ignores;
  svn_error_t *err;
  apr_hash_t *changelist_hash = NULL;
  svn_revnum_t edit_revision = SVN_INVALID_REVNUM;

  if (changelists && changelists->nelts)
    SVN_ERR(svn_hash_from_cstring_keys(&changelist_hash, changelists, pool));

  sb.real_status_func = status_func;
  sb.real_status_baton = status_baton;
  sb.deleted_in_repos = FALSE;
  sb.changelist_hash = changelist_hash;

  /* Try to open the target directory. If the target is a file or an
     unversioned directory, open the parent directory instead */
  err = svn_wc_adm_open3(&anchor_access, NULL, path, FALSE,
                         SVN_DEPTH_IS_RECURSIVE(depth) ? -1 : 1,
                         ctx->cancel_func, ctx->cancel_baton,
                         pool);
  if (err && err->apr_err == SVN_ERR_WC_NOT_DIRECTORY)
    {
      svn_error_clear(err);
      SVN_ERR(svn_wc_adm_open_anchor(&anchor_access, &target_access, &target,
                                     path, FALSE,
                                     SVN_DEPTH_IS_RECURSIVE(depth) ? -1 : 1,
                                     ctx->cancel_func, ctx->cancel_baton,
                                     pool));
    }
  else if (!err)
    {
      target = "";
      target_access = anchor_access;
    }
  else
    return err;

  anchor = svn_wc_adm_access_path(anchor_access);

  /* Get the status edit, and use our wrapping status function/baton
     as the callback pair. */
  SVN_ERR(svn_wc_get_default_ignores(&ignores, ctx->config, pool));
  SVN_ERR(svn_wc_get_status_editor4(&editor, &edit_baton, &set_locks_baton,
                                    &edit_revision, anchor_access, target,
                                    depth, get_all, no_ignore, ignores,
                                    tweak_status, &sb, ctx->cancel_func,
                                    ctx->cancel_baton, traversal_info,
                                    pool));

  /* If we want to know about out-of-dateness, we crawl the working copy and
     let the RA layer drive the editor for real.  Otherwise, we just close the
     edit.  :-) */
  if (update)
    {
      svn_ra_session_t *ra_session;
      const char *URL;
      svn_node_kind_t kind;
      svn_boolean_t server_supports_depth;

      /* Get full URL from the ANCHOR. */
      if (! entry)
        SVN_ERR(svn_wc__entry_versioned(&entry, anchor, anchor_access, FALSE,
                                        pool));
      if (! entry->url)
        return svn_error_createf
          (SVN_ERR_ENTRY_MISSING_URL, NULL,
           _("Entry '%s' has no URL"),
           svn_path_local_style(anchor, pool));
      URL = apr_pstrdup(pool, entry->url);

      /* Open a repository session to the URL. */
      SVN_ERR(svn_client__open_ra_session_internal(&ra_session, URL, anchor,
                                                   anchor_access, NULL,
                                                   FALSE, TRUE,
                                                   ctx, pool));

      /* Verify that URL exists in HEAD.  If it doesn't, this can save
         us a whole lot of hassle; if it does, the cost of this
         request should be minimal compared to the size of getting
         back the average amount of "out-of-date" information. */
      SVN_ERR(svn_ra_check_path(ra_session, "", SVN_INVALID_REVNUM,
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
          SVN_ERR(editor->close_edit(edit_baton, pool));
        }
      else
        {
          svn_revnum_t revnum;
          report_baton_t rb;

          if (revision->kind == svn_opt_revision_head)
            {
              /* Cause the revision number to be omitted from the request,
                 which implies HEAD. */
              revnum = SVN_INVALID_REVNUM;
            }
          else
            {
              /* Get a revision number for our status operation. */
              SVN_ERR(svn_client__get_revision_number
                      (&revnum, NULL, ra_session, revision, target, pool));
            }

          /* Do the deed.  Let the RA layer drive the status editor. */
          SVN_ERR(svn_ra_do_status2(ra_session, &rb.wrapped_reporter,
                                    &rb.wrapped_report_baton,
                                    target, revnum, depth, editor,
                                    edit_baton, pool));

          /* Init the report baton. */
          rb.ancestor = apr_pstrdup(pool, URL);
          rb.set_locks_baton = set_locks_baton;
          rb.ctx = ctx;
          rb.pool = pool;

          SVN_ERR(svn_ra_has_capability(ra_session, &server_supports_depth,
                                        SVN_RA_CAPABILITY_DEPTH, pool));

          /* Drive the reporter structure, describing the revisions
             within PATH.  When we call reporter->finish_report,
             EDITOR will be driven to describe differences between our
             working copy and HEAD. */
          SVN_ERR(svn_wc_crawl_revisions4(path, target_access,
                                          &lock_fetch_reporter, &rb, FALSE,
                                          depth, TRUE, (! server_supports_depth),
                                          FALSE, NULL, NULL, NULL, pool));
        }
    }
  else
    {
      SVN_ERR(editor->close_edit(edit_baton, pool));
    }

  if (ctx->notify_func2 && update)
    {
      svn_wc_notify_t *notify
        = svn_wc_create_notify(path, svn_wc_notify_status_completed, pool);
      notify->revision = edit_revision;
      (ctx->notify_func2)(ctx->notify_baton2, notify, pool);
    }

  /* If the caller wants the result revision, give it to them. */
  if (result_rev)
    *result_rev = edit_revision;

  /* Close the access baton here, as svn_client__do_external_status()
     calls back into this function and thus will be re-opening the
     working copy. */
  SVN_ERR(svn_wc_adm_close2(anchor_access, pool));

  /* If there are svn:externals set, we don't want those to show up as
     unversioned or unrecognized, so patch up the hash.  If caller wants
     all the statuses, we will change unversioned status items that
     are interesting to an svn:externals property to
     svn_wc_status_unversioned, otherwise we'll just remove the status
     item altogether.

     We only descend into an external if depth is svn_depth_infinity or
     svn_depth_unknown.  However, there are conceivable behaviors that
     would involve descending under other circumstances; thus, we pass
     depth anyway, so the code will DTRT if we change the conditional
     in the future.
  */
  if (SVN_DEPTH_IS_RECURSIVE(depth) && (! ignore_externals))
    SVN_ERR(svn_client__do_external_status(traversal_info, status_func,
                                           status_baton, depth, get_all,
                                           update, no_ignore, ctx, pool));

  return SVN_NO_ERROR;
}
