/*
 * clientstring.c:  client string handling for http headers.
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

/* ==================================================================== */

/*** Includes. ***/
#include <apr_pools.h>
#include <apr_strings.h>

#include "svn_error.h"
#include "svn_error_codes.h"
#include "svn_ra.h"


/* The default client name string */
static const char default_ra_client_name[] = "SVN";

/* The client name string that is actually used. */
static const char *ra_client_name = default_ra_client_name;



const char *
svn_ra_get_client_namestring()
{
  return ra_client_name;
}


svn_error_t *
svn_ra_set_client_namestring(const char *name)
{
  apr_pool_t *rootpool;
  if (apr_pool_create(&rootpool, NULL))
    abort();

  ra_client_name = apr_pstrdup(rootpool, name);

  return SVN_NO_ERROR;
}
