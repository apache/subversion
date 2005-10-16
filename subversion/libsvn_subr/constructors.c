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
#include "svn_client.h"


svn_commit_info_t *
svn_create_commit_info (apr_pool_t *pool)
{
  svn_commit_info_t *commit_info
    = apr_pcalloc (pool, sizeof (*commit_info));

  commit_info->revision = SVN_INVALID_REVNUM;
  /* All other fields were initialized to NULL above. */

  return commit_info;
}

svn_commit_info_t *
svn_commit_info_dup (const svn_commit_info_t *src_commit_info,
                     apr_pool_t *pool)
{
  svn_commit_info_t *dst_commit_info = svn_create_commit_info (pool);

  dst_commit_info->date = src_commit_info->date
    ? apr_pstrdup (pool, src_commit_info->date) : NULL;
  dst_commit_info->author = src_commit_info->author
    ? apr_pstrdup (pool, src_commit_info->author) : NULL;
  dst_commit_info->revision = src_commit_info->revision;

  return dst_commit_info;
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

/** 
 * Reallocate the members of @a prop using @a pool.
 */
static void
svn_prop_member_dup (svn_prop_t *prop, apr_pool_t *pool)
{
  if (prop->name)
    prop->name = apr_pstrdup (pool, prop->name);
  if (prop->value)
    prop->value = svn_string_dup (prop->value, pool);
}

svn_prop_t *
svn_prop_dup (const svn_prop_t *prop, apr_pool_t *pool)
{
  svn_prop_t *new_prop = apr_palloc (pool, sizeof (*new_prop));

  *new_prop = *prop;

  svn_prop_member_dup (new_prop, pool);
  
  return new_prop;
}

/** 
 * Duplicate a @a hash containing (char * -> svn_string_t *) key/value
 * pairs using @a pool.
 */
static apr_hash_t *
svn_string_hash_dup (apr_hash_t *hash, apr_pool_t *pool)
{
  apr_hash_index_t *hi;
  const char *key;
  apr_ssize_t klen;
  svn_string_t *val;
  apr_hash_t *new_hash = apr_hash_make (pool);
  for (hi = apr_hash_first (pool, hash); hi; hi = apr_hash_next (hi) )
    {
      apr_hash_this (hi, (const void *) &key, &klen, (void *) &val);
      key = apr_pstrdup (pool, key);
      val = svn_string_dup (val, pool);
      apr_hash_set (hash, key, klen, val);
    }
  return new_hash;
}

svn_client_proplist_item_t *
svn_client_proplist_item_dup (const svn_client_proplist_item_t *item,
                              apr_pool_t * pool)
{
  svn_client_proplist_item_t *new_item
    = apr_pcalloc (pool, sizeof (*new_item));

  if (item->node_name) 
    new_item->node_name = svn_stringbuf_dup (item->node_name, pool);

  if (item->prop_hash)
    new_item->prop_hash = svn_string_hash_dup (item->prop_hash, pool);

  return new_item;
}

/** 
 * Duplicate an @a array of svn_prop_t items using @a pool.
 */
static apr_array_header_t *
svn_prop_array_dup (const apr_array_header_t *array, apr_pool_t *pool)
{
  int i;
  apr_array_header_t *new_array = apr_array_copy (pool, array);
  for (i = 0; i < new_array->nelts; ++i)
    {
      svn_prop_t *elt = &APR_ARRAY_IDX(new_array, i, svn_prop_t);
      svn_prop_member_dup (elt, pool);
    }
  return new_array;
}

svn_client_commit_item2_t *
svn_client_commit_item2_dup (const svn_client_commit_item2_t *item,
                             apr_pool_t *pool)
{
  svn_client_commit_item2_t *new_item = apr_palloc (pool, sizeof (*new_item));

  *new_item = *item;

  if (new_item->path)
    new_item->path = apr_pstrdup (pool, new_item->path);

  if (new_item->url)
    new_item->url = apr_pstrdup (pool, new_item->url);

  if (new_item->copyfrom_url)   
    new_item->copyfrom_url = apr_pstrdup (pool, new_item->copyfrom_url);

  if (new_item->wcprop_changes)
    new_item->wcprop_changes = svn_prop_array_dup (new_item->wcprop_changes, 
                                                   pool);

  return new_item;
}
