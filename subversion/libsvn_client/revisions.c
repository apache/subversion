/*
 * revisions.c:  discovering revisions
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



#include <apr_pools.h>

#include "svn_error.h"
#include "svn_ra.h"
#include "svn_wc.h"
#include "svn_path.h"
#include "client.h"

#include "svn_private_config.h"




svn_error_t *
svn_client__get_revision_number(svn_revnum_t *revnum,
                                svn_ra_session_t *ra_session,
                                const svn_opt_revision_t *revision,
                                const char *path,
                                apr_pool_t *pool)
{
  /* ### When revision->kind == svn_opt_revision_date, is there an
     optimization such that we can compare revision->value->date with
     the committed-date in the entries file (or rather, with some
     range of which committed-date is one endpoint), and sometimes
     avoid a trip over the RA layer?  The only optimizations I can
     think of involve examining other entries to build a timespan
     across which committed-revision is known to be the head, but it
     doesn't seem worth it.  -kff */

  /* Sanity check. */
  if (ra_session == NULL
      && ((revision->kind == svn_opt_revision_date)
          || (revision->kind == svn_opt_revision_head)))
    {
      return svn_error_create
        (SVN_ERR_CLIENT_RA_ACCESS_REQUIRED, NULL, NULL);
    }

  if (revision->kind == svn_opt_revision_number)
    *revnum = revision->value.number;
  else if (revision->kind == svn_opt_revision_date)
    SVN_ERR(svn_ra_get_dated_revision(ra_session, revnum,
                                      revision->value.date, pool));
  else if (revision->kind == svn_opt_revision_head)
    SVN_ERR(svn_ra_get_latest_revnum(ra_session, revnum, pool));
  else if (revision->kind == svn_opt_revision_unspecified)
    *revnum = SVN_INVALID_REVNUM;
  else if ((revision->kind == svn_opt_revision_committed)
           || (revision->kind == svn_opt_revision_working)
           || (revision->kind == svn_opt_revision_base)
           || (revision->kind == svn_opt_revision_previous))
    {
      svn_wc_adm_access_t *adm_access; /* ### FIXME local */
      const svn_wc_entry_t *ent;

      /* Sanity check. */
      if (path == NULL)
        return svn_error_create
          (SVN_ERR_CLIENT_VERSIONED_PATH_REQUIRED, NULL, NULL);

      SVN_ERR(svn_wc_adm_probe_open3(&adm_access, NULL, path, FALSE,
                                     0, NULL, NULL, pool));
      SVN_ERR(svn_wc_entry(&ent, path, adm_access, FALSE, pool));
      SVN_ERR(svn_wc_adm_close(adm_access));

      if (! ent)
        return svn_error_createf
        (SVN_ERR_UNVERSIONED_RESOURCE, NULL,
	 _("'%s' is not under version control"),
         svn_path_local_style(path, pool));
      
      if ((revision->kind == svn_opt_revision_base)
          || (revision->kind == svn_opt_revision_working))
        *revnum = ent->revision;
      else
        {
          if (! SVN_IS_VALID_REVNUM(ent->cmt_rev))
            return svn_error_createf(SVN_ERR_CLIENT_BAD_REVISION, NULL,
                                     _("Path '%s' has no committed revision"),
                                     path);
          *revnum = ent->cmt_rev;
          if (revision->kind == svn_opt_revision_previous)
            (*revnum)--;
        }
    }
  else
    return svn_error_createf
      (SVN_ERR_CLIENT_BAD_REVISION, NULL,
       _("Unrecognized revision type requested for '%s'"),
       svn_path_local_style(path, pool));
  
  return SVN_NO_ERROR;
}


svn_boolean_t
svn_client__compare_revisions(svn_opt_revision_t *revision1,
                              svn_opt_revision_t *revision2)
{
  if ((revision1->kind != revision2->kind)
      || ((revision1->kind == svn_opt_revision_number)
          && (revision1->value.number != revision2->value.number))
      || ((revision1->kind == svn_opt_revision_date)
          && (revision1->value.date != revision2->value.date)))
    return FALSE;

  /* Else. */
  return TRUE;
}


svn_boolean_t
svn_client__revision_is_local(const svn_opt_revision_t *revision)
{
  if ((revision->kind == svn_opt_revision_unspecified)
      || (revision->kind == svn_opt_revision_head)
      || (revision->kind == svn_opt_revision_number)
      || (revision->kind == svn_opt_revision_date))
    return FALSE;
  else
    return TRUE;
}
