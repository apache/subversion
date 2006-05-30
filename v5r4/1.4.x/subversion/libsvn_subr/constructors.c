/*
 * constructors.c :  Constructors for various data structures.
 *
 * ====================================================================
 * Copyright (c) 2005-2006 CollabNet.  All rights reserved.
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
svn_create_commit_info(apr_pool_t *pool)
{
  svn_commit_info_t *commit_info
    = apr_pcalloc(pool, sizeof(*commit_info));

  commit_info->revision = SVN_INVALID_REVNUM;
  /* All other fields were initialized to NULL above. */

  return commit_info;
}

svn_commit_info_t *
svn_commit_info_dup(const svn_commit_info_t *src_commit_info,
                    apr_pool_t *pool)
{
  svn_commit_info_t *dst_commit_info = svn_create_commit_info(pool);

  dst_commit_info->date = src_commit_info->date
    ? apr_pstrdup(pool, src_commit_info->date) : NULL;
  dst_commit_info->author = src_commit_info->author
    ? apr_pstrdup(pool, src_commit_info->author) : NULL;
  dst_commit_info->revision = src_commit_info->revision;
  dst_commit_info->post_commit_err = src_commit_info->post_commit_err
    ? apr_pstrdup(pool, src_commit_info->post_commit_err) : NULL;

  return dst_commit_info;
}

svn_log_changed_path_t *
svn_log_changed_path_dup(const svn_log_changed_path_t *changed_path,
                         apr_pool_t *pool)
{
  svn_log_changed_path_t *new_changed_path
    = apr_palloc(pool, sizeof(*new_changed_path));

  *new_changed_path = *changed_path;

  if (new_changed_path->copyfrom_path)
    new_changed_path->copyfrom_path =
      apr_pstrdup(pool, new_changed_path->copyfrom_path);

  return new_changed_path;
}

/**
 * Reallocate the members of PROP using POOL.
 */
static void
svn_prop__members_dup(svn_prop_t *prop, apr_pool_t *pool)
{
  if (prop->name)
    prop->name = apr_pstrdup(pool, prop->name);
  if (prop->value)
    prop->value = svn_string_dup(prop->value, pool);
}

svn_prop_t *
svn_prop_dup(const svn_prop_t *prop, apr_pool_t *pool)
{
  svn_prop_t *new_prop = apr_palloc(pool, sizeof(*new_prop));

  *new_prop = *prop;

  svn_prop__members_dup(new_prop, pool);

  return new_prop;
}

apr_array_header_t *
svn_prop_array_dup(const apr_array_header_t *array, apr_pool_t *pool)
{
  int i;
  apr_array_header_t *new_array = apr_array_copy(pool, array);
  for (i = 0; i < new_array->nelts; ++i)
    {
      svn_prop_t *elt = &APR_ARRAY_IDX(new_array, i, svn_prop_t);
      svn_prop__members_dup(elt, pool);
    }
  return new_array;
}

svn_dirent_t *
svn_dirent_dup(const svn_dirent_t *dirent,
               apr_pool_t *pool)
{
  svn_dirent_t *new_dirent = apr_palloc(pool, sizeof(*new_dirent));

  *new_dirent = *dirent;

  new_dirent->last_author = apr_pstrdup(pool, dirent->last_author);

  return new_dirent;
}
