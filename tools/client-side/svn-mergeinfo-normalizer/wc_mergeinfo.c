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
#include "svn_hash.h"

#include "mergeinfo-normalizer.h"

#include "private/svn_fspath.h"
#include "private/svn_opt_private.h"
#include "private/svn_sorts_private.h"
#include "private/svn_subr_private.h"
#include "svn_private_config.h"



/* Our internal mergeinfo structure
 * It decorates the standard svn_mergeinfo_t with path and parent info. */
typedef struct mergeinfo_t
{
  /* The abspath of the working copy node that has this MERGINFO. */
  const char *local_path;

  /* The corresponding FS path. */
  const char *fs_path;

  /* The full URL of that node in the repository. */
  const char *url;

  /* Pointer to the closest parent mergeinfo that we found in the working
   * copy.  May be NULL. */
  struct mergeinfo_t *parent;

  /* All mergeinfo_t* who's PARENT points to this.  May be NULL. */
  apr_array_header_t *children;

  /* The parsed mergeinfo. */
  svn_mergeinfo_t mergeinfo;
} mergeinfo_t;

/* Parse the mergeinfo in PROPS as returned by svn_client_propget5,
 * construct our internal mergeinfo representation, allocated in
 * RESULT_POOL from it and return it *RESULT_P.  Use SCRATCH_POOL for
 * temporary allocations. */
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
      SVN_ERR(svn_mergeinfo_parse(&mergeinfo, mi_string->data, iterpool));

      entry->local_path = apr_pstrdup(result_pool, apr_hash_this_key(hi));
      entry->mergeinfo = svn_mergeinfo_dup(mergeinfo, result_pool);

      APR_ARRAY_PUSH(result, mergeinfo_t *) = entry;
    }

  svn_pool_destroy(iterpool);
  *result_p = result;

  return SVN_NO_ERROR;
}

/* Ordering function comparing two mergeinfo_t * by local abspath. */
static int
compare_mergeinfo(const void *lhs,
                  const void *rhs)
{
  const mergeinfo_t *lhs_mi = *(const mergeinfo_t **)lhs;
  const mergeinfo_t *rhs_mi = *(const mergeinfo_t **)rhs;

  return strcmp(lhs_mi->local_path, rhs_mi->local_path);
}

/* Implements svn_client_info_receiver2_t.
 * Updates the mergeinfo_t * given as BATON with the incoming INFO. */
static svn_error_t *
get_urls(void *baton,
         const char *target,
         const svn_client_info2_t *info,
         apr_pool_t *pool)
{
  mergeinfo_t *mi = baton;
  apr_pool_t *target_pool = apr_hash_pool_get(mi->mergeinfo);
  const char *rel_path = svn_uri_skip_ancestor(info->repos_root_URL,
                                               info->URL, pool);
 
  mi->url = apr_pstrdup(target_pool, info->URL);
  mi->fs_path = svn_fspath__canonicalize(rel_path, target_pool);

  return SVN_NO_ERROR;
}

/* Sort the nodes in MERGEINFO, sub-nodes first, add working copy info to
 * it and link nodes to their respective closest parents.  BATON provides
 * the client context.  SCRATCH_POOL is used for temporaries. */
static svn_error_t *
link_parents(apr_array_header_t *mergeinfo,
             svn_min__cmd_baton_t *baton,
             apr_pool_t *scratch_pool)
{
  apr_pool_t *result_pool = mergeinfo->pool;
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);
  int i;

  /* We further down assume that there are is least one entry. */
  if (mergeinfo->nelts == 0)
    return SVN_NO_ERROR;

  /* sort mergeinfo by path */
  svn_sort__array(mergeinfo, compare_mergeinfo);

  /* add URL info */
  for (i = 0; i < mergeinfo->nelts; ++i)
    {
      mergeinfo_t *entry = APR_ARRAY_IDX(mergeinfo, i, mergeinfo_t *);
      const svn_opt_revision_t rev_working = { svn_opt_revision_working };

      svn_pool_clear(iterpool);
      SVN_ERR(svn_client_info4(entry->local_path, &rev_working,
                               &rev_working, svn_depth_empty, FALSE,
                               TRUE, FALSE, NULL, get_urls, entry,
                               baton->ctx, iterpool));
    }

  /* link all mergeinfo to their parent merge info - if that exists */
  for (i = 1; i < mergeinfo->nelts; ++i)
    {
      mergeinfo_t *entry = APR_ARRAY_IDX(mergeinfo, i, mergeinfo_t *);
      entry->parent = APR_ARRAY_IDX(mergeinfo, i - 1, mergeinfo_t *);

      while (   entry->parent
             && !svn_dirent_is_ancestor(entry->parent->local_path,
                                        entry->local_path))
        entry->parent = entry->parent->parent;

      /* Reverse pointer. */
      if (entry->parent)
        {
          if (!entry->parent->children)
            entry->parent->children
              = apr_array_make(result_pool, 4, sizeof(svn_mergeinfo_t));

          APR_ARRAY_PUSH(entry->parent->children, svn_mergeinfo_t)
            = entry->mergeinfo;
        }
    }

  /* break links for switched paths */
  for (i = 1; i < mergeinfo->nelts; ++i)
    {
      mergeinfo_t *entry = APR_ARRAY_IDX(mergeinfo, i, mergeinfo_t *);
      if (entry->parent)
        {
          if (!svn_uri__is_ancestor(entry->parent->url, entry->url))
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

  /* Pools for temporary data - to be cleaned up asap as they
   * significant amounts of it. */
  apr_pool_t *props_pool = svn_pool_create(scratch_pool);
  apr_pool_t *props_scratch_pool = svn_pool_create(scratch_pool);
  apr_hash_t *props;

  const svn_opt_revision_t rev_working = { svn_opt_revision_working };

  if (!baton->opt_state->quiet)
    SVN_ERR(svn_cmdline_printf(scratch_pool,
                              _("Scanning working copy %s ...\n"),
                              baton->local_abspath));

  SVN_ERR(svn_client_propget5(&props, NULL, SVN_PROP_MERGEINFO,
                              baton->local_abspath, &rev_working,
                              &rev_working, NULL,
                              opt_state->depth, NULL, ctx,
                              props_pool, props_scratch_pool));
  svn_pool_destroy(props_scratch_pool);

  SVN_ERR(parse_mergeinfo(result, props, result_pool, scratch_pool));
  svn_pool_destroy(props_pool);

  SVN_ERR(link_parents(*result, baton, scratch_pool));

  if (!baton->opt_state->quiet)
    SVN_ERR(svn_min__print_mergeinfo_stats(*result, scratch_pool));

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

      /* Make common base path cover the wc's FS path. */
      if (result == NULL)
        result = apr_pstrdup(result_pool, entry->fs_path);
      else if (!svn_dirent_is_ancestor(result, entry->fs_path))
        result = svn_dirent_get_longest_ancestor(result, entry->fs_path,
                                                 result_pool);

      /* Cover the branch FS paths mentioned in the mergeinfo. */
      for (hi = apr_hash_first(scratch_pool, entry->mergeinfo);
           hi;
           hi = apr_hash_next(hi))
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

void
svn_min__get_mergeinfo_pair(const char **fs_path,
                            const char **parent_path,
                            const char **subtree_relpath,
                            svn_mergeinfo_t *parent_mergeinfo,
                            svn_mergeinfo_t *subtree_mergeinfo,
                            apr_array_header_t **siblings_mergeinfo,
                            apr_array_header_t *mergeinfo,
                            int idx)
{
  mergeinfo_t *entry;
  if (idx < 0 || mergeinfo->nelts <= idx)
    {
      *fs_path = "";
      *parent_path = "";
      *subtree_relpath = "";
      *parent_mergeinfo = NULL;
      *subtree_mergeinfo = NULL;
      *siblings_mergeinfo = NULL;

      return;
    }

  entry = APR_ARRAY_IDX(mergeinfo, idx, mergeinfo_t *);
  *fs_path = entry->fs_path;
  *subtree_mergeinfo = entry->mergeinfo;

  if (!entry->parent)
    {
      *parent_path = entry->local_path;
      *subtree_relpath = "";
      *parent_mergeinfo = NULL;
      *siblings_mergeinfo = NULL;

      return;
    }

  *parent_path = entry->parent->local_path;
  *subtree_relpath = svn_dirent_skip_ancestor(entry->parent->local_path,
                                              entry->local_path);
  *parent_mergeinfo = entry->parent->mergeinfo;
  *siblings_mergeinfo = entry->parent->children;
}

svn_mergeinfo_t
svn_min__get_mergeinfo(apr_array_header_t *mergeinfo,
                       int idx)
{
  SVN_ERR_ASSERT_NO_RETURN(idx >= 0 && idx < mergeinfo->nelts);
  return APR_ARRAY_IDX(mergeinfo, idx, mergeinfo_t *)->mergeinfo;
}

svn_error_t *
svn_min__sibling_ranges(apr_hash_t **sibling_ranges,
                        apr_array_header_t *sibling_mergeinfo,
                        const char *parent_path,
                        svn_rangelist_t *relevant_ranges,
                        apr_pool_t *result_pool,
                        apr_pool_t *scratch_pool)
{
  int i;
  apr_hash_t *result = svn_hash__make(result_pool);
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);

  for (i = 0; i < sibling_mergeinfo->nelts; ++i)
    {
      svn_mergeinfo_t mergeinfo;
      apr_hash_index_t *hi;

      svn_pool_clear(iterpool);
      mergeinfo = APR_ARRAY_IDX(sibling_mergeinfo, i, svn_mergeinfo_t);

      for (hi = apr_hash_first(iterpool, mergeinfo);
           hi;
           hi = apr_hash_next(hi))
        {
          const char *path = apr_hash_this_key(hi);
          if (svn_dirent_is_ancestor(parent_path, path))
            {
              svn_rangelist_t *common, *ranges = apr_hash_this_val(hi);
              SVN_ERR(svn_rangelist_intersect(&common, ranges,
                                              relevant_ranges, TRUE,
                                              result_pool));

              if (common->nelts)
                {
                  svn_hash_sets(result, apr_pstrdup(result_pool, path),
                                common);
                }
            }
        }
    }

  svn_pool_destroy(iterpool);
  *sibling_ranges = result;

  return SVN_NO_ERROR;
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

      /* If the mergeinfo is empty, keep the NULL PROPVAL to actually
       * delete the property. */
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
svn_min__remove_empty_mergeinfo(apr_array_header_t *mergeinfo)
{
  int i;
  int dest;

  for (i = 0, dest = 0; i < mergeinfo->nelts; ++i)
    {
      mergeinfo_t *entry = APR_ARRAY_IDX(mergeinfo, i, mergeinfo_t *);
      if (apr_hash_count(entry->mergeinfo))
        {
          APR_ARRAY_IDX(mergeinfo, dest, mergeinfo_t *) = entry;
          ++dest;
        }
    }

  mergeinfo->nelts = dest;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_min__print_mergeinfo_stats(apr_array_header_t *wc_mergeinfo,
                               apr_pool_t *scratch_pool)
{
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);

  int branch_count = 0;
  int range_count = 0;

  /* Aggregate numbers. */
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

  /* Show them. */
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

