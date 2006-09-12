/*
 * kitchensink.c :  When no place else seems to fit...
 *
 * ====================================================================
 * Copyright (c) 2006 CollabNet.  All rights reserved.
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
#include <apr_uuid.h>

#include "svn_types.h"


const char *
svn_uuid_generate(apr_pool_t *pool)
{
  apr_uuid_t uuid;
  char *uuid_str = apr_pcalloc(pool, APR_UUID_FORMATTED_LENGTH + 1);
  apr_uuid_get(&uuid);
  apr_uuid_format(uuid_str, &uuid);
  return uuid_str;
}
