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
#include <apr_strings.h>

#include "svn_types.h"
#include "svn_props.h"
#include "svn_string.h"


svn_commit_info_t *
svn_create_commit_info (apr_pool_t *pool)
{
  svn_commit_info_t *commit_info
    = apr_pcalloc (pool, sizeof (*commit_info));

  commit_info->revision = SVN_INVALID_REVNUM;
  /* All other fields were initialized to NULL above. */

  return commit_info;
}

svn_log_changed_path_t *
svn_log_changed_path_dup (const svn_log_changed_path_t *changed_path,
                          apr_pool_t *pool)
{
  svn_log_changed_path_t *new_changed_path
    = apr_palloc (pool, sizeof (*new_changed_path));

  *new_changed_path = *changed_path;

  if (new_changed_path->copyfrom_path)
    new_changed_path->copyfrom_path =
      apr_pstrdup (pool, new_changed_path->copyfrom_path);

  return new_changed_path;
}

svn_prop_t *
svn_prop_dup (const svn_prop_t *prop, apr_pool_t *pool)
{
  svn_prop_t *new_prop = apr_pcalloc (pool, sizeof (*new_prop));

  if (prop->name)
    new_prop->name = apr_pstrdup (pool, prop->name);
  if (prop->value)
    new_prop->value = svn_string_dup (prop->value, pool);

  return new_prop;
}

