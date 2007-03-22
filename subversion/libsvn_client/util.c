/*
 * util.c :  utility functions for the libsvn_client library
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

#include <assert.h>
#include <apr_pools.h>
#include <apr_strings.h>

#include "svn_pools.h"
#include "svn_string.h"
#include "svn_error.h"
#include "svn_props.h"
#include "svn_path.h"
#include "svn_mergeinfo.h"
#include "svn_client.h"

#include "private/svn_wc_private.h"

#include "client.h"

#include "svn_private_config.h"

/* Duplicate a HASH containing (char * -> svn_string_t *) key/value
   pairs using POOL. */
static apr_hash_t *
string_hash_dup(apr_hash_t *hash, apr_pool_t *pool)
{
  apr_hash_index_t *hi;
  const void *key;
  apr_ssize_t klen;
  void *val;
  apr_hash_t *new_hash = apr_hash_make(pool);
  for (hi = apr_hash_first(pool, hash); hi; hi = apr_hash_next(hi))
    {
      apr_hash_this(hi, &key, &klen, &val);
      key = apr_pstrdup(pool, key);
      val = svn_string_dup(val, pool);
      apr_hash_set(new_hash, key, klen, val);
    }
  return new_hash;
}

svn_error_t *
svn_client_commit_item_create(const svn_client_commit_item3_t **item,
                              apr_pool_t *pool)
{
  *item = apr_pcalloc(pool, sizeof(svn_client_commit_item3_t));
  return SVN_NO_ERROR;
}

svn_client_commit_item3_t *
svn_client_commit_item3_dup(const svn_client_commit_item3_t *item,
                            apr_pool_t *pool)
{
  svn_client_commit_item3_t *new_item = apr_palloc(pool, sizeof(*new_item));

  *new_item = *item;

  if (new_item->path)
    new_item->path = apr_pstrdup(pool, new_item->path);

  if (new_item->url)
    new_item->url = apr_pstrdup(pool, new_item->url);

  if (new_item->copyfrom_url)
    new_item->copyfrom_url = apr_pstrdup(pool, new_item->copyfrom_url);

  if (new_item->incoming_prop_changes)
    new_item->incoming_prop_changes =
      svn_prop_array_dup(new_item->incoming_prop_changes, pool);

  if (new_item->outgoing_prop_changes)
    new_item->outgoing_prop_changes =
      svn_prop_array_dup(new_item->outgoing_prop_changes, pool);

  return new_item;
}

svn_client_commit_item2_t *
svn_client_commit_item2_dup(const svn_client_commit_item2_t *item,
                            apr_pool_t *pool)
{
  svn_client_commit_item2_t *new_item = apr_palloc(pool, sizeof(*new_item));

  *new_item = *item;

  if (new_item->path)
    new_item->path = apr_pstrdup(pool, new_item->path);

  if (new_item->url)
    new_item->url = apr_pstrdup(pool, new_item->url);

  if (new_item->copyfrom_url)
    new_item->copyfrom_url = apr_pstrdup(pool, new_item->copyfrom_url);

  if (new_item->wcprop_changes)
    new_item->wcprop_changes = svn_prop_array_dup(new_item->wcprop_changes,
                                                  pool);

  return new_item;
}

svn_client_proplist_item_t *
svn_client_proplist_item_dup(const svn_client_proplist_item_t *item,
                             apr_pool_t * pool)
{
  svn_client_proplist_item_t *new_item
    = apr_pcalloc(pool, sizeof(*new_item));

  if (item->node_name)
    new_item->node_name = svn_stringbuf_dup(item->node_name, pool);

  if (item->prop_hash)
    new_item->prop_hash = string_hash_dup(item->prop_hash, pool);

  return new_item;
}

svn_error_t *
svn_client__path_relative_to_root(const char **rel_path,
                                  const char *path_or_url,
                                  const char *repos_root,
                                  svn_ra_session_t *ra_session,
                                  svn_wc_adm_access_t *adm_access,
                                  apr_pool_t *pool)
{
  svn_error_t *err = SVN_NO_ERROR;
  svn_boolean_t need_wc_cleanup = FALSE;
  svn_boolean_t is_path = !svn_path_is_url(path_or_url);

  /* Old WCs may not provide the repository URL. */
  assert(repos_root != NULL || ra_session != NULL);

  /* If we have a WC path, transform it into a URL for use in
     calculating its path relative to the repository root.

     If we don't already know the repository root, derive it.  If we
     have a WC path, first look in the entries file.  Fall back to
     asking the RA session. */
  if (is_path && repos_root == NULL)
    {
      const svn_wc_entry_t *entry;

      if (adm_access == NULL)
        {
          SVN_ERR(svn_wc_adm_probe_open3(&adm_access, NULL, path_or_url,
                                         FALSE, 0, NULL, NULL, pool));
          need_wc_cleanup = TRUE;
        }
      err = svn_wc__entry_versioned(&entry, path_or_url, adm_access, FALSE,
                                    pool);

      if (err)
        goto cleanup;

      path_or_url = entry->url;
      repos_root = entry->repos;
    }
  if (repos_root == NULL)
    {
      /* We may be operating on a URL, or have been otherwise unable
         to determine the repository root. */
      err = svn_ra_get_repos_root(ra_session, &repos_root, pool);
      if (err)
        goto cleanup;
    }

  /* Calculate the path relative to the repository root. */
  *rel_path = svn_path_is_child(repos_root, path_or_url, pool);

  /* Assure that the path begins with a slash, as the path is NULL if
     the URL is the repository root. */
  *rel_path = svn_path_join("/", *rel_path ? *rel_path : "", pool);
  *rel_path = svn_path_uri_decode(*rel_path, pool);

 cleanup:
  if (need_wc_cleanup)
    {
      if (err == SVN_NO_ERROR)
        err = svn_wc_adm_close(adm_access);
      else
        svn_wc_adm_close(adm_access);
    }
  return err;
}

svn_error_t *
svn_client__get_merge_info_for_path(svn_ra_session_t *ra_session,
                                    apr_hash_t **target_mergeinfo,
                                    const char *rel_path,
                                    svn_revnum_t rev,
                                    apr_pool_t *pool)
{
  apr_hash_t *repos_mergeinfo;
  apr_array_header_t *rel_paths = apr_array_make(pool, 1, sizeof(rel_path));
  APR_ARRAY_PUSH(rel_paths, const char *) = rel_path;
  SVN_ERR(svn_ra_get_merge_info(ra_session, &repos_mergeinfo, rel_paths,
                                rev, TRUE, pool));

  /* Grab only the merge info provided for REL_PATH. */
  if (repos_mergeinfo)
    *target_mergeinfo = apr_hash_get(repos_mergeinfo, rel_path,
                                     APR_HASH_KEY_STRING);
  else
    *target_mergeinfo = NULL;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_client__parse_merge_info(apr_hash_t **mergeinfo,
                             const svn_wc_entry_t *entry,
                             const char *wcpath,
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
                                       wcpath, FALSE, entry, adm_access,
                                       FALSE, ctx, pool));
  propval = apr_hash_get(props, wcpath, APR_HASH_KEY_STRING);
  if (propval)
    SVN_ERR(svn_mergeinfo_parse(propval->data, mergeinfo, pool));
  else
    *mergeinfo = apr_hash_make(pool);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_client__record_wc_merge_info(const char *wcpath,
                                 apr_hash_t *mergeinfo,
                                 svn_wc_adm_access_t *adm_access,
                                 apr_pool_t *pool)
{
  svn_string_t *mergeinfo_str;

  /* Convert the merge info (if any) into text for storage as a
     property value. */
  if (apr_hash_count(mergeinfo) > 0)
    {
      /* The WC will contain merge info. */
      SVN_ERR(svn_mergeinfo__to_string(&mergeinfo_str, mergeinfo, pool));
    }
  else
    {
      mergeinfo_str = NULL;
    }
  
  /* Record the new merge info in the WC. */
  /* ### Later, we'll want behavior more analogous to
     ### svn_client__get_prop_from_wc(). */
  return svn_wc_prop_set2(SVN_PROP_MERGE_INFO, mergeinfo_str, wcpath,
                          adm_access, TRUE /* skip checks */, pool);
}
