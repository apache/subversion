/*
 * get_rev_deleted.c :  ra_serf get_revision_deleted API implementation.
 *
 * ====================================================================
 * Copyright (c) 2008 CollabNet.  All rights reserved.
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


#include "../libsvn_ra/ra_loader.h"
#include "ra_serf.h"

svn_error_t *
svn_ra_serf__get_revision_deleted(svn_ra_session_t *session,
                                  const char *path,
                                  svn_revnum_t peg_revision,
                                  svn_revnum_t end_revision,
                                  svn_revnum_t *revision_deleted,
                                  apr_pool_t *pool)
{
  return svn_error_create(SVN_ERR_RA_NOT_IMPLEMENTED, NULL, NULL);
}
