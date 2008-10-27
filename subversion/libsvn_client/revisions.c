/*
 * revisions.c:  discovering revisions
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



#include <apr_pools.h>

#include "svn_error.h"
#include "svn_ra.h"
#include "svn_path.h"
#include "client.h"

#include "svn_private_config.h"
#include "private/svn_wc_private.h"




svn_error_t *
svn_client__get_revision_number(svn_revnum_t *revnum,
                                svn_revnum_t *youngest_rev,
                                svn_ra_session_t *ra_session,
                                const svn_opt_revision_t *revision,
                                const char *path,
                                apr_pool_t *pool)
{
  switch (revision->kind)
    {
    case svn_opt_revision_unspecified:
      *revnum = SVN_INVALID_REVNUM;
      break;

    case svn_opt_revision_number:
      *revnum = revision->value.number;
      break;

    case svn_opt_revision_head:
      /* If our caller provided a value for HEAD that he wants us to
         use, we'll use it.  Otherwise, we have to query the
         repository (and possible return our fetched value in
         *YOUNGEST_REV, too). */
      if (youngest_rev && SVN_IS_VALID_REVNUM(*youngest_rev))
        {
          *revnum = *youngest_rev;
        }
      else
        {
          if (! ra_session)
            return svn_error_create(SVN_ERR_CLIENT_RA_ACCESS_REQUIRED,
                                    NULL, NULL);
          SVN_ERR(svn_ra_get_latest_revnum(ra_session, revnum, pool));
          if (youngest_rev)
            *youngest_rev = *revnum;
        }
      break;

    case svn_opt_revision_committed:
    case svn_opt_revision_working:
    case svn_opt_revision_base:
    case svn_opt_revision_previous:
      {
        svn_wc_adm_access_t *adm_access;
        const svn_wc_entry_t *ent;

        /* Sanity check. */
        if (path == NULL)
          return svn_error_create(SVN_ERR_CLIENT_VERSIONED_PATH_REQUIRED,
                                  NULL, NULL);

        SVN_ERR(svn_wc_adm_probe_open3(&adm_access, NULL, path, FALSE,
                                       0, NULL, NULL, pool));
        SVN_ERR(svn_wc__entry_versioned(&ent, path, adm_access, FALSE, pool));
        SVN_ERR(svn_wc_adm_close2(adm_access, pool));

        if ((revision->kind == svn_opt_revision_base)
            || (revision->kind == svn_opt_revision_working))
          {
            *revnum = ent->revision;
          }
        else
          {
            if (! SVN_IS_VALID_REVNUM(ent->cmt_rev))
              return svn_error_createf(SVN_ERR_CLIENT_BAD_REVISION, NULL,
                                       _("Path '%s' has no committed "
                                         "revision"), path);
            *revnum = ent->cmt_rev;
            if (revision->kind == svn_opt_revision_previous)
              (*revnum)--;
          }
      }
      break;

    case svn_opt_revision_date:
      /* ### When revision->kind == svn_opt_revision_date, is there an
         ### optimization such that we can compare
         ### revision->value->date with the committed-date in the
         ### entries file (or rather, with some range of which
         ### committed-date is one endpoint), and sometimes avoid a
         ### trip over the RA layer?  The only optimizations I can
         ### think of involve examining other entries to build a
         ### timespan across which committed-revision is known to be
         ### the head, but it doesn't seem worth it.  -kff */
      if (! ra_session)
        return svn_error_create(SVN_ERR_CLIENT_RA_ACCESS_REQUIRED, NULL, NULL);
      SVN_ERR(svn_ra_get_dated_revision(ra_session, revnum,
                                        revision->value.date, pool));
      break;

    default:
      return svn_error_createf(SVN_ERR_CLIENT_BAD_REVISION, NULL,
                               _("Unrecognized revision type requested for "
                                 "'%s'"),
                               svn_path_local_style(path, pool));
    }

  /* Final check -- if our caller provided a youngest revision, and
  the number we wound up with is younger than that revision, we need
  to stick to our caller's idea of "youngest". */
  if (youngest_rev
      && SVN_IS_VALID_REVNUM(*youngest_rev)
      && SVN_IS_VALID_REVNUM(*revnum)
      && (*revnum > *youngest_rev))
    *revnum = *youngest_rev;

  return SVN_NO_ERROR;
}
