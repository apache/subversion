/*
 * mergeinfo.c :  merge history functions for the libsvn_client library
 *
 * ====================================================================
 * Copyright (c) 2006-2007 CollabNet.  All rights reserved.
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

#include "svn_pools.h"
#include "svn_string.h"
#include "svn_error.h"
#include "svn_props.h"
#include "svn_mergeinfo.h"
#include "svn_sorts.h"
#include "svn_client.h"

#include "private/svn_mergeinfo_private.h"
#include "private/svn_wc_private.h"
#include "client.h"
#include "mergeinfo.h"
#include "svn_private_config.h"

svn_error_t *
svn_client__get_repos_mergeinfo(svn_ra_session_t *ra_session,
                                apr_hash_t **target_mergeinfo,
                                const char *rel_path,
                                svn_revnum_t rev,
                                svn_mergeinfo_inheritance_t inherit,
                                apr_pool_t *pool)
{
  apr_hash_t *repos_mergeinfo;
  apr_array_header_t *rel_paths = apr_array_make(pool, 1, sizeof(rel_path));
  APR_ARRAY_PUSH(rel_paths, const char *) = rel_path;
  SVN_ERR(svn_ra_get_mergeinfo(ra_session, &repos_mergeinfo, rel_paths, rev,
                               inherit, pool));

  /* Grab only the mergeinfo provided for REL_PATH. */
  if (repos_mergeinfo)
    *target_mergeinfo = apr_hash_get(repos_mergeinfo, rel_path,
                                     APR_HASH_KEY_STRING);
  else
    *target_mergeinfo = NULL;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_client__parse_mergeinfo(apr_hash_t **mergeinfo,
                            const svn_wc_entry_t *entry,
                            const char *wcpath,
                            svn_boolean_t pristine,
                            svn_wc_adm_access_t *adm_access,
                            svn_client_ctx_t *ctx,
                            apr_pool_t *pool)
{
  apr_hash_t *props = apr_hash_make(pool);
  const svn_string_t *propval;

  /* ### Use svn_wc_prop_get() would actually be sufficient for now.
     ### DannyB thinks that later we'll need behavior more like
     ### svn_client__get_prop_from_wc(). */
  SVN_ERR(svn_client__get_prop_from_wc(props, SVN_PROP_MERGE_INFO,
                                       wcpath, pristine, entry, adm_access,
                                       FALSE, ctx, pool));
  propval = apr_hash_get(props, wcpath, APR_HASH_KEY_STRING);
  if (propval)
    SVN_ERR(svn_mergeinfo_parse(mergeinfo, propval->data, pool));
  else
    *mergeinfo = NULL;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_client__record_wc_mergeinfo(const char *wcpath,
                                apr_hash_t *mergeinfo,
                                svn_wc_adm_access_t *adm_access,
                                apr_pool_t *pool)
{
  svn_string_t *mergeinfo_str;

  /* Convert the mergeinfo (if any) into text for storage as a
     property value. */
  if (mergeinfo)
    {
      /* The WC will contain mergeinfo. */
      SVN_ERR(svn_mergeinfo__to_string(&mergeinfo_str, mergeinfo, pool));
    }
  else
    {
      mergeinfo_str = NULL;
    }
  
  /* Record the new mergeinfo in the WC. */
  /* ### Later, we'll want behavior more analogous to
     ### svn_client__get_prop_from_wc(). */
  return svn_wc_prop_set2(SVN_PROP_MERGE_INFO, mergeinfo_str, wcpath,
                          adm_access, TRUE /* skip checks */, pool);
}

svn_error_t *
svn_client__elide_mergeinfo_for_tree(apr_hash_t *children_with_mergeinfo,
                                     svn_wc_adm_access_t *adm_access,
                                     svn_client_ctx_t *ctx,
                                     apr_pool_t *pool)
{
  int i;
  apr_pool_t *iterpool = svn_pool_create(pool);
  apr_array_header_t *sorted_children =
    svn_sort__hash(children_with_mergeinfo, svn_sort_compare_items_as_paths,
                   pool);

  /* sorted_children is in depth first order.  To minimize
     svn_client__elide_mergeinfo()'s crawls up the working copy from
     each child, run through the array backwards, effectively doing a
     right-left post-order traversal. */
  for (i = sorted_children->nelts -1; i >= 0; i--)
    {
      const svn_wc_entry_t *child_entry;
      const char *child_wcpath;
      svn_sort__item_t *item = &APR_ARRAY_IDX(sorted_children, i,
                                              svn_sort__item_t);
      apr_pool_clear(iterpool);
      child_wcpath = item->key;
      SVN_ERR(svn_wc__entry_versioned(&child_entry, child_wcpath, adm_access,
                                      FALSE, iterpool));
      SVN_ERR(svn_client__elide_mergeinfo(child_wcpath, NULL, child_entry,
                                          adm_access, ctx, iterpool));
    }

  apr_pool_destroy(iterpool);
  return SVN_NO_ERROR;
}
