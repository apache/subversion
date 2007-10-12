/*
 * getlocationsegments.c :  entry point for get_location_segments 
 *                          RA functions for ra_serf
 *
 * ====================================================================
 * Copyright (c) 2007 CollabNet.  All rights reserved.
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



#include <apr_uri.h>
#include <expat.h>
#include <serf.h>

#include "svn_pools.h"
#include "svn_ra.h"

#include "ra_serf.h"


svn_error_t *
svn_ra_serf__get_location_segments(svn_ra_session_t *session,
                                   const char *path,
                                   svn_revnum_t start_rev,
                                   svn_revnum_t end_rev,
                                   svn_location_segment_receiver_t receiver,
                                   void *receiver_baton,
                                   apr_pool_t *pool)
{
  return svn_error_create(SVN_ERR_RA_NOT_IMPLEMENTED, NULL, NULL);
}
