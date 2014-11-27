/*
 * wc_mergeinfo.c -- Query and store the mergeinfo.
 *
 * ====================================================================
 *    Licensed to the Apache Software Foundation (ASF) under one
 *    or more contributor license agreements.  See the NOTICE file
 *    distributed with this work for additional information
 *    regarding copyright ownership.  The ASF licenses this file
 *    to you under the Apache License, Version 2.0 (the
 *    "License"); you may not use this file except in compliance
 *    with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing,
 *    software distributed under the License is distributed on an
 *    "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 *    KIND, either express or implied.  See the License for the
 *    specific language governing permissions and limitations
 *    under the License.
 * ====================================================================
 */

/* ==================================================================== */



/*** Includes. ***/

#include "svn_cmdline.h"
#include "svn_pools.h"
#include "svn_client.h"
#include "svn_string.h"
#include "svn_sorts.h"
#include "svn_dirent_uri.h"
#include "svn_props.h"

#include "mergeinfo-normalizer.h"

#include "private/svn_opt_private.h"
#include "private/svn_sorts_private.h"
#include "svn_private_config.h"



typedef struct mergeinfo_t
{
  const char *local_path;
  struct mergeinfo_t *parent;
  svn_mergeinfo_t mergeinfo;
} mergeinfo_t;

static svn_error_t *
parse_mergeinfo(apr_array_header_t **result_p,
                apr_hash_t *props,
                apr_pool_t *result_pool,
                apr_pool_t *scratch_pool)
{
  apr_array_header_t *result = apr_array_make(result_pool,
                                              apr_hash_count(props),
                                              sizeof(mergeinfo_t *));
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);
  apr_hash_index_t *hi;

  for (hi = apr_hash_first(scratch_pool, props); hi; hi = apr_hash_next(hi))
    {
      mergeinfo_t *entry = apr_pcalloc(result_pool, sizeof(*entry));
      svn_mergeinfo_t mergeinfo;
      svn_string_t *mi_string = apr_hash_this_val(hi);

      svn_pool_clear(iterpool);
      SVN_ERR(svn_mergeinfo_parse(&mergeinfo, mi_string->data, scratch_pool));

      entry->local_path = apr_pstrdup(result_pool, apr_hash_this_key(hi));
      entry->mergeinfo = svn_mergeinfo_dup(mergeinfo, result_pool);

      APR_ARRAY_PUSH(result, mergeinfo_t *) = entry;
    }

  svn_pool_destroy(iterpool);
  *result_p = result;

  return SVN_NO_ERROR;
}

static int
compare_mergeinfo(const void *lhs,
                  const void *rhs)
{
  const mergeinfo_t *lhs_mi = *(const mergeinfo_t **)lhs;
  const mergeinfo_t *rhs_mi = *(const mergeinfo_t **)rhs;

  return strcmp(lhs_mi->local_path, rhs_mi->local_path);
}

static svn_error_t *
get_url(void *baton,
        const char *target,
        const svn_client_info2_t *info,
        apr_pool_t *pool)
{
  svn_stringbuf_t *url = baton;
  svn_stringbuf_set(url, info->URL);

  return SVN_NO_ERROR;
}

static svn_error_t *
link_parents(apr_array_header_t *mergeinfo,
             svn_min__cmd_baton_t *baton,
             apr_pool_t *scratch_pool)
{
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);
  int i;

  /* We further down assume that there are is least one entry. */
  if (mergeinfo->nelts == 0)
    return SVN_NO_ERROR;

  /* sort mergeinfo by path */
  svn_sort__array(mergeinfo, compare_mergeinfo);

  /* link all mergeinfo to their parent merge info - if that exists */
  for (i = 1; i < mergeinfo->nelts; ++i)
    {
      mergeinfo_t *entry = APR_ARRAY_IDX(mergeinfo, i, mergeinfo_t *);
      entry->parent = APR_ARRAY_IDX(mergeinfo, i - 1, mergeinfo_t *);

      while (   entry->parent
             && !svn_dirent_is_ancestor(entry->parent->local_path,
                                        entry->local_path))
        entry->parent = entry->parent->parent;
    }

  /* break links for switched paths */
  for (i = 1; i < mergeinfo->nelts; ++i)
    {
      mergeinfo_t *entry = APR_ARRAY_IDX(mergeinfo, i, mergeinfo_t *);
      if (entry->parent)
        {
          const svn_opt_revision_t rev_working = { svn_opt_revision_working };
          svn_stringbuf_t *entry_url, *parent_url;

          svn_pool_clear(iterpool);
          entry_url = svn_stringbuf_create_empty(iterpool);
          parent_url = svn_stringbuf_create_empty(iterpool);

          SVN_ERR(svn_client_info4(entry->local_path, &rev_working,
                                   &rev_working, svn_depth_empty, FALSE,
                                   TRUE, FALSE, NULL, get_url, entry_url,
                                   baton->ctx, iterpool));
          SVN_ERR(svn_client_info4(entry->parent->local_path, &rev_working,
                                   &rev_working, svn_depth_empty, FALSE,
                                   TRUE, FALSE, NULL, get_url, parent_url,
                                   baton->ctx, iterpool));

          if (!svn_uri__is_ancestor(parent_url->data, entry_url->data))
            entry->parent = NULL;
        }
    }

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_min__read_mergeinfo(apr_array_header_t **result,
                        svn_min__cmd_baton_t *baton,
                        apr_pool_t *result_pool,
                        apr_pool_t *scratch_pool)
{
  svn_min__opt_state_t *opt_state = baton->opt_state;
  svn_client_ctx_t *ctx = baton->ctx;

  apr_pool_t *props_pool = svn_pool_create(scratch_pool);
  apr_hash_t *props;

  const svn_opt_revision_t rev_working = { svn_opt_revision_working };

  SVN_ERR(svn_client_propget5(&props, NULL, SVN_PROP_MERGEINFO,
                              baton->local_abspath, &rev_working,
                              &rev_working, NULL,
                              opt_state->depth, NULL, ctx,
                              props_pool, scratch_pool));
  SVN_ERR(parse_mergeinfo(result, props, result_pool, scratch_pool));
  SVN_ERR(link_parents(*result, baton, scratch_pool));

  svn_pool_destroy(props_pool);

  return SVN_NO_ERROR;
}

const char *
svn_min__common_parent(apr_array_header_t *mergeinfo,
                       apr_pool_t *result_pool,
                       apr_pool_t *scratch_pool)
{
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);
  const char *result = NULL;
  int i;

  for (i = 0; i < mergeinfo->nelts; ++i)
    {
      apr_hash_index_t *hi;
      mergeinfo_t *entry = APR_ARRAY_IDX(mergeinfo, i, mergeinfo_t *);

      svn_pool_clear(iterpool);
      for (hi = apr_hash_first(scratch_pool, entry->mergeinfo);
           hi;
           hi = apr_hash_next(hi))
        if (result == NULL)
          {
            result = apr_pstrdup(result_pool, apr_hash_this_key(hi));
          }
        else
          {
            const char * path = apr_hash_this_key(hi);
            if (!svn_dirent_is_ancestor(result, path))
              result = svn_dirent_get_longest_ancestor(result, path,
                                                       result_pool);
          }
    }

  svn_pool_destroy(iterpool);
  return result;
}

svn_boolean_t
svn_min__get_mergeinfo_pair(const char **parent_path,
                            const char **subtree_relpath,
                            svn_mergeinfo_t *parent_mergeinfo,
                            svn_mergeinfo_t *subtree_mergeinfo,
                            apr_array_header_t *mergeinfo,
                            int idx)
{
  mergeinfo_t *entry;
  if (idx < 0 || mergeinfo->nelts <= idx)
    return FALSE;

  entry = APR_ARRAY_IDX(mergeinfo, idx, mergeinfo_t *);
  if (!entry->parent)
    return FALSE;

  *parent_path = entry->parent->local_path;
  *subtree_relpath = svn_dirent_skip_ancestor(entry->parent->local_path,
                                              entry->local_path);
  *parent_mergeinfo = entry->parent->mergeinfo;
  *subtree_mergeinfo = entry->mergeinfo;

  return TRUE;
}

svn_mergeinfo_t
svn_min__get_mergeinfo(apr_array_header_t *mergeinfo,
                       int idx)
{
  return APR_ARRAY_IDX(mergeinfo, idx, mergeinfo_t *)->mergeinfo;
}

const char *
svn_min__get_mergeinfo_path(apr_array_header_t *mergeinfo,
                            int idx)
{
  return APR_ARRAY_IDX(mergeinfo, idx, mergeinfo_t *)->local_path;
}

svn_error_t *
svn_min__write_mergeinfo(svn_min__cmd_baton_t *baton,
                         apr_array_header_t *mergeinfo,
                         apr_pool_t *scratch_pool)
{
  svn_client_ctx_t *ctx = baton->ctx;

  apr_pool_t *iterpool = svn_pool_create(scratch_pool);
  int i;

  for (i = 0; i < mergeinfo->nelts; ++i)
    {
      mergeinfo_t *entry = APR_ARRAY_IDX(mergeinfo, i, mergeinfo_t *);
      svn_string_t *propval = NULL;
      apr_array_header_t *targets;

      svn_pool_clear(iterpool);

      targets = apr_array_make(iterpool, 1, sizeof(const char *));
      APR_ARRAY_PUSH(targets, const char *) = entry->local_path;

      if (apr_hash_count(entry->mergeinfo))
        SVN_ERR(svn_mergeinfo_to_string(&propval, entry->mergeinfo,
                                        iterpool));

      SVN_ERR(svn_client_propset_local(SVN_PROP_MERGEINFO, propval, targets,
                                       svn_depth_empty, FALSE, NULL, ctx,
                                       iterpool));
    }

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_min__print_mergeinfo_stats(apr_array_header_t *wc_mergeinfo,
                               apr_pool_t *scratch_pool)
{
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);

  int branch_count = 0;
  int range_count = 0;

  int i;
  for (i = 0; i < wc_mergeinfo->nelts; ++i)
    {
      apr_hash_index_t *hi;
      svn_mergeinfo_t mergeinfo = svn_min__get_mergeinfo(wc_mergeinfo, i);

      svn_pool_clear(iterpool);

      branch_count += apr_hash_count(mergeinfo);

      for (hi = apr_hash_first(iterpool, mergeinfo);
           hi;
           hi = apr_hash_next(hi))
        {
          svn_rangelist_t *ranges = apr_hash_this_val(hi);
          range_count += ranges->nelts;
        }
    }

  SVN_ERR(svn_cmdline_printf(scratch_pool,
                             _("    Found mergeinfo on %d nodes.\n"),
                             wc_mergeinfo->nelts));
  SVN_ERR(svn_cmdline_printf(scratch_pool,
                             _("    Found %d branch entries.\n"),
                             branch_count));
  SVN_ERR(svn_cmdline_printf(scratch_pool,
                             _("    Found %d merged revision ranges.\n\n"),
                             range_count));

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

