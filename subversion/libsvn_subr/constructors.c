/*
 * constructors.c :  Constructors for various data structures.
 *
 * ====================================================================
 * Copyright (c) 2005 CollabNet.  All rights reserved.
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

#include "svn_types.h"


svn_commit_info_t *
svn_create_commit_info (apr_pool_t *pool)
{
  svn_commit_info_t *commit_info
    = apr_pcalloc (pool, sizeof (*commit_info));

  commit_info->revision = SVN_INVALID_REVNUM;
  /* All other fields were initialized to NULL above. */

  return commit_info;
}

