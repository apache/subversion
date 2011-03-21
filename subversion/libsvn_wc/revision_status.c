/*
 * revision_status.c: report the revision range and status of a working copy
 *
 * ====================================================================
 * Copyright (c) 2003-2004 CollabNet.  All rights reserved.
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

#include "svn_wc.h"

#include "svn_private_config.h"


/* A baton for analyze_status(). */
struct status_baton
{
  svn_wc_revision_status_t *result;           /* where to put the result */
  svn_boolean_t committed;           /* examine last committed revisions */
  const char *wc_path;               /* path whose URL we're looking for */
  const char *wc_url;    /* URL for the path whose URL we're looking for */
  apr_pool_t *pool;         /* pool in which to store alloc-needy things */
};

/* An svn_wc_status_func3_t callback function for analyzing status
   structures. */
static svn_error_t *
analyze_status(void *baton,
               const char *path,
               svn_wc_status2_t *status,
               apr_pool_t *pool)
{
  struct status_baton *sb = baton;

  if (! status->entry)
    return SVN_NO_ERROR;

  /* Added files and file externals have a revision of no interest */
  if (status->text_status != svn_wc_status_added && !status->file_external)
    {
      svn_revnum_t item_rev = (sb->committed
                               ? status->entry->cmt_rev
                               : status->entry->revision);

      if (sb->result->min_rev == SVN_INVALID_REVNUM
          || item_rev < sb->result->min_rev)
        sb->result->min_rev = item_rev;

      if (sb->result->max_rev == SVN_INVALID_REVNUM
          || item_rev > sb->result->max_rev)
        sb->result->max_rev = item_rev;
    }

  sb->result->switched |= status->switched;
  sb->result->modified |= (status->text_status != svn_wc_status_normal);
  sb->result->modified |= (status->prop_status != svn_wc_status_normal
                           && status->prop_status != svn_wc_status_none);
  sb->result->sparse_checkout |= (status->entry->depth != svn_depth_infinity);

  if (sb->wc_path
      && (! sb->wc_url)
      && (strcmp(path, sb->wc_path) == 0)
      && (status->entry))
    sb->wc_url = apr_pstrdup(sb->pool, status->entry->url);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc_revision_status(svn_wc_revision_status_t **result_p,
                       const char *wc_path,
                       const char *trail_url,
                       svn_boolean_t committed,
                       svn_cancel_func_t cancel_func,
                       void *cancel_baton,
                       apr_pool_t *pool)
{
  struct status_baton sb;
  const char *target;
  svn_wc_adm_access_t *anchor_access, *target_access;
  const svn_delta_editor_t *editor;
  void *edit_baton;
  svn_revnum_t edit_revision;
  svn_wc_revision_status_t *result = apr_palloc(pool, sizeof(**result_p));
  *result_p = result;

  /* set result as nil */
  result->min_rev  = SVN_INVALID_REVNUM;
  result->max_rev  = SVN_INVALID_REVNUM;
  result->switched = FALSE;
  result->modified = FALSE;
  result->sparse_checkout = FALSE;

  /* initialize walking baton */
  sb.result = result;
  sb.committed = committed;
  sb.wc_path = wc_path;
  sb.wc_url = NULL;
  sb.pool = pool;

  SVN_ERR(svn_wc_adm_open_anchor(&anchor_access, &target_access, &target,
                                 wc_path, FALSE, -1,
                                 cancel_func, cancel_baton,
                                 pool));

  SVN_ERR(svn_wc_get_status_editor4(&editor, &edit_baton, NULL,
                                    &edit_revision, anchor_access, target,
                                    svn_depth_infinity,
                                    TRUE  /* get_all */,
                                    FALSE /* no_ignore */,
                                    NULL  /* ignore_patterns */,
                                    analyze_status, &sb,
                                    cancel_func, cancel_baton,
                                    NULL  /* traversal_info */,
                                    pool));

  SVN_ERR(editor->close_edit(edit_baton, pool));

  SVN_ERR(svn_wc_adm_close2(anchor_access, pool));

  if ((! result->switched) && (trail_url != NULL))
    {
      /* If the trailing part of the URL of the working copy directory
         does not match the given trailing URL then the whole working
         copy is switched. */
      if (! sb.wc_url)
        {
          result->switched = TRUE;
        }
      else
        {
          apr_size_t len1 = strlen(trail_url);
          apr_size_t len2 = strlen(sb.wc_url);
          if ((len1 > len2) || strcmp(sb.wc_url + len2 - len1, trail_url))
            result->switched = TRUE;
        }
    }

  return SVN_NO_ERROR;
}
