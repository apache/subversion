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
  apr_hash_t *hash;
  svn_wc_status_func_t status_func;
  void *status_baton;
};

/* A faux status callback function for stashing STATUS item in a hash
   keyed on PATH, and then passes the STATUS on through to a real
   STATUS_FUNC.  This is merely for the purposes of verifying that we
   don't call the STATUS_FUNC for the same path more than once.  */
static void
hash_stash (void *baton,
            const char *path,
            svn_wc_status_t *status)
{
  struct status_baton *sb = baton;
  assert (! apr_hash_get (sb->hash, path, APR_HASH_KEY_STRING));
  apr_hash_set (sb->hash, apr_pstrdup (apr_hash_pool_get (sb->hash), path), 
                APR_HASH_KEY_STRING, (void *)1);
  sb->status_func (sb->status_baton, path, status);
}



/*** Public Interface. ***/


svn_error_t *
svn_client_status (svn_revnum_t *youngest,
                   const char *path,
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
  const svn_delta_editor_t *editor;
  void *edit_baton;
  svn_ra_plugin_t *ra_lib;  
  const svn_wc_entry_t *entry;
  struct status_baton sb;

  sb.status_func = status_func;
  sb.status_baton = status_baton;
  sb.hash = apr_hash_make (pool);

  /* Need to lock the tree as even a non-recursive status requires the
     immediate directories to be locked. */
  SVN_ERR (svn_wc_adm_probe_open (&adm_access, NULL, path, FALSE, TRUE, pool));

  /* Get the entry for this path.  If the item is unversioned, we
     can't really do a full status report on it, so we'll just call
     svn_wc_status().  */
  SVN_ERR (svn_wc_entry (&entry, path, adm_access, FALSE, pool));
  if (! entry)
    {
      svn_wc_status_t *status;
      SVN_ERR (svn_wc_status (&status, path, adm_access, pool));
      status_func (status_baton, path, status);
      return SVN_NO_ERROR;
    }

  SVN_ERR (svn_wc_get_status_editor (&editor, &edit_baton, youngest, path, 
                                     adm_access, ctx->config, descend, 
                                     get_all, no_ignore, hash_stash, &sb,
                                     ctx->cancel_func, ctx->cancel_baton,
                                     traversal_info, pool));

  /* If this is a real update, we crawl the working copy and let the
     RA layer drive the editor for real.  Otherwise, we just close the
     edit.  :-) */ 
  if (update)
    {
      void *ra_baton, *session, *report_baton;
      const svn_ra_reporter_t *reporter;
      const char *anchor, *target, *URL;
      svn_wc_adm_access_t *anchor_access;
      svn_node_kind_t kind;

      /* Use PATH to get the update's anchor and targets. */
      SVN_ERR (svn_wc_get_actual_target (path, &anchor, &target, pool));

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
          SVN_ERR (editor->close_edit (edit_baton, pool));

#ifdef STREAMY_STATUS_IN_PROGRESS
          /* This code SHOULD be marking the whole tree under ANCHOR as
             deleted, but that would cause it to be inconsistent with a
             bug that currently runs freely in the working copy status
             editor (see issue #1469).  So, for now, we'll just mark the
             path that corresponds to the ANCHOR as deleted. */
          status_item = apr_hash_get (hash, anchor, APR_HASH_KEY_STRING);
          if (! status_item)
            {
              SVN_ERR (svn_wc_status (&status_item, anchor, adm_access, pool));
              apr_hash_set (hash, 
                            apr_pstrdup (apr_hash_pool_get (hash), anchor), 
                            APR_HASH_KEY_STRING, status_item);
            }
          status_item->repos_text_status = svn_wc_status_deleted;
#endif
        }
      else
        {
          SVN_ERR (ra_lib->do_status (session, &reporter, &report_baton,
                                      target, descend, editor, 
                                      edit_baton, pool));

          /* Drive the reporter structure, describing the revisions
             within PATH.  When we call reporter->finish_report,
             EDITOR will be driven to describe differences between our
             working copy and HEAD. */
          SVN_ERR (svn_wc_crawl_revisions (path, adm_access, reporter, 
                                           report_baton, FALSE, descend, 
                                           NULL, NULL, NULL, pool));
        }
    }
  else
    {
      SVN_ERR (editor->close_edit (edit_baton, pool));
    }

#ifdef STREAMY_STATUS_IN_PROGRESS
  /* If there are svn:externals set, we don't want those to show up as
     unversioned or unrecognized, so patchup the hash.  If callers wants
     all the statuses, we will change unversioned status items that
     are interesting to an svn:externals property to
     svn_wc_status_unversioned, otherwise we'll just remove the status
     item altogether. */
  SVN_ERR (svn_client__recognize_externals (hash, traversal_info, pool));
#endif

  SVN_ERR (svn_wc_adm_close (adm_access));
  return SVN_NO_ERROR;
}
