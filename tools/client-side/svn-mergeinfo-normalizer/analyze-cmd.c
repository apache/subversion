/*
 * analyze-cmd.c -- Print which MI can be elided, which one can not and why
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
#include "svn_dirent_uri.h"
#include "svn_hash.h"
#include "svn_path.h"
#include "svn_pools.h"
#include "svn_sorts.h"
#include "private/svn_fspath.h"
#include "private/svn_sorts_private.h"

#include "mergeinfo-normalizer.h"

#include "svn_private_config.h"


/*** Code. ***/

static svn_error_t *
remove_obsolete_lines(svn_ra_session_t *session,
                      svn_mergeinfo_t mergeinfo,
                      apr_pool_t *scratch_pool)
{
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);
  apr_array_header_t *to_remove
    = apr_array_make(scratch_pool, 16, sizeof(const char *));

  int i;
  apr_hash_index_t *hi;
  for (hi = apr_hash_first(scratch_pool, mergeinfo);
       hi;
       hi = apr_hash_next(hi))
    {
      const char *path = apr_hash_this_key(hi);
      svn_node_kind_t kind;

      SVN_ERR_ASSERT(*path == '/');
      SVN_ERR(svn_ra_check_path(session, path + 1, SVN_INVALID_REVNUM, &kind,
                                scratch_pool));
      if (kind == svn_node_none)
        APR_ARRAY_PUSH(to_remove, const char *) = path;
    }

  svn_sort__array(to_remove, svn_sort_compare_paths);
  if (to_remove->nelts)
    {
      SVN_ERR(svn_cmdline_printf(iterpool,
                            _("    %d branches don't exist in HEAD:\n"),
                            to_remove->nelts));

      for (i = 0; i < to_remove->nelts; ++i)
        {
          const char *path;
          svn_pool_clear(iterpool);

          path = APR_ARRAY_IDX(to_remove, i, const char *);
          svn_hash_sets(mergeinfo, path, NULL);

          SVN_ERR(svn_cmdline_printf(iterpool, _("    %s\n"), path));
        }
    }
  else
    {
      SVN_ERR(svn_cmdline_printf(iterpool,
                             _("    All branches still exist in HEAD.\n")));
    }

  SVN_ERR(svn_cmdline_printf(iterpool, _("\n")));
  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

static svn_rangelist_t *
find_reverse_ranges(svn_rangelist_t *ranges,
                    apr_pool_t *result_pool)
{
  svn_rangelist_t *result = apr_array_make(result_pool, 0, ranges->elt_size);

  int i;
  for (i = 0; i < ranges->nelts; ++i)
    {
      const svn_merge_range_t *range
        = APR_ARRAY_IDX(ranges, i, const svn_merge_range_t *);

      if (range->start >= range->end)
        APR_ARRAY_PUSH(result, const svn_merge_range_t *) = range;
    }

  return result;
}

static svn_error_t *
print_ranges(svn_rangelist_t *ranges,
             const char *title,
             apr_pool_t *scratch_pool)
{
  svn_string_t *string;

  SVN_ERR(svn_rangelist_to_string(&string, ranges, scratch_pool));
  SVN_ERR(svn_cmdline_printf(scratch_pool, _("        %s%s\n"),
                             title, string->data));

  return SVN_NO_ERROR;
}

static svn_error_t *
remove_lines(svn_min__log_t *log,
             const char *relpath,
             svn_mergeinfo_t parent_mergeinfo,
             svn_mergeinfo_t subtree_mergeinfo,
             apr_pool_t *scratch_pool)
{
  apr_pool_t *iterpool;
  apr_hash_index_t *hi;
  apr_hash_t *processed;
  svn_boolean_t needs_header = TRUE;

  if (apr_hash_count(subtree_mergeinfo) == 0)
    return SVN_NO_ERROR;

  iterpool = svn_pool_create(scratch_pool);
  processed = apr_hash_make(scratch_pool);

  SVN_ERR(svn_cmdline_printf(iterpool,
                             _("    Try to elide remaining branches:\n")));

  for (hi = apr_hash_first(scratch_pool, parent_mergeinfo);
       hi;
       hi = apr_hash_next(hi))
    {
      const char *parent_path, *subtree_path;
      svn_rangelist_t *parent_ranges, *subtree_ranges, *reverse_ranges;
      svn_rangelist_t *subtree_only, *parent_only;
      svn_rangelist_t *operative_outside_subtree, *operative_in_subtree;

      svn_pool_clear(iterpool);

      parent_path = apr_hash_this_key(hi);
      subtree_path = svn_fspath__join(parent_path, relpath, scratch_pool);
      parent_ranges = apr_hash_this_val(hi);
      subtree_ranges = svn_hash_gets(subtree_mergeinfo, subtree_path);

      if (!subtree_ranges)
        continue;

      svn_hash_sets(processed, subtree_path, subtree_path);

      reverse_ranges = find_reverse_ranges(subtree_ranges, iterpool);
      if (reverse_ranges->nelts)
        {
          SVN_ERR(svn_cmdline_printf(iterpool,
                                  _("    Reverse range(s) found for %s:\n"),
                                  subtree_path));
          SVN_ERR(print_ranges(reverse_ranges, "", iterpool));
          continue;
        }

      SVN_ERR(svn_rangelist_diff(&parent_only, &subtree_only,
                                 parent_ranges, subtree_ranges, TRUE,
                                 iterpool));
      subtree_only
        = svn_min__operative(log, subtree_path, subtree_only, iterpool);

      if (!subtree_only->nelts && !parent_only->nelts)
        {
          SVN_ERR(svn_cmdline_printf(iterpool,
                                  _("    elide redundant branch %s\n"),
                                  subtree_path));
          svn_hash_sets(subtree_mergeinfo, subtree_path, NULL);
          continue;
        }

      operative_outside_subtree
        = svn_min__operative_outside_subtree(log, parent_path, subtree_path,
                                             subtree_only, iterpool);
      operative_in_subtree
        = svn_min__operative(log, subtree_path, parent_only, iterpool);

      if (operative_outside_subtree->nelts || operative_in_subtree->nelts)
        {
          SVN_ERR(svn_cmdline_printf(iterpool,
                                     _("    CANNOT elide branch %s\n"),
                                     subtree_path));
          if (operative_outside_subtree->nelts)
            SVN_ERR(print_ranges(operative_outside_subtree,
                                 _("revisions not movable to parent: "),
                                 iterpool));
          if (operative_in_subtree->nelts)
            SVN_ERR(print_ranges(operative_in_subtree,
                                 _("revisions missing in sub-node: "),
                                 iterpool));
        }
      else
        {
          SVN_ERR(svn_cmdline_printf(iterpool,
                                     _("    elide branch %s\n"),
                                     subtree_path));
          if (subtree_only->nelts)
            SVN_ERR(print_ranges(subtree_only,
                                 _("revisions moved to parent: "),
                                 iterpool));
          if (parent_only->nelts)
            SVN_ERR(print_ranges(parent_only,
                                 _("revisions inoperative in sub-node: "),
                                 iterpool));

          SVN_ERR(svn_rangelist_merge2(parent_ranges, subtree_only,
                                       parent_ranges->pool, iterpool));
          svn_hash_sets(subtree_mergeinfo, subtree_path, NULL);
        }
    }


  for (hi = apr_hash_first(scratch_pool, subtree_mergeinfo);
       hi;
       hi = apr_hash_next(hi))
    {
      const char *path = apr_hash_this_key(hi);
      svn_pool_clear(iterpool);

      if (!svn_hash_gets(processed, path))
        {
          if (needs_header)
            {
              SVN_ERR(svn_cmdline_printf(scratch_pool,
                           _("\n    Branches not mentioned in parent:\n")));
              needs_header = FALSE;
            }

          SVN_ERR(svn_cmdline_printf(iterpool, ("    %s\n"), path));
        }
    }

  SVN_ERR(svn_cmdline_printf(iterpool, "\n"));
  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

static svn_error_t *
analyze(svn_ra_session_t *session,
        apr_array_header_t *wc_mergeinfo,
        svn_min__log_t *log,
        apr_pool_t *scratch_pool)
{
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);

  int i;
  for (i = wc_mergeinfo->nelts - 1; i >= 0; --i)
    {
      const char *parent_path;
      const char *relpath;
      svn_mergeinfo_t parent_mergeinfo;
      svn_mergeinfo_t subtree_mergeinfo;

      if (svn_min__get_mergeinfo_pair(&parent_path, &relpath,
                                      &parent_mergeinfo, &subtree_mergeinfo,
                                      wc_mergeinfo, i))
        {
          SVN_ERR(svn_cmdline_printf(iterpool,
                                     _("Trying to elide mergeinfo from path\n"
                                       "    %s\n"
                                       "    into mergeinfo at path\n"
                                       "    %s\n\n"),
                                     svn_dirent_join(parent_path, relpath,
                                                     iterpool),
                                     parent_path));
        }
      else
        {
          parent_mergeinfo = NULL;
          subtree_mergeinfo = svn_min__get_mergeinfo(wc_mergeinfo, i);

          SVN_ERR(svn_cmdline_printf(iterpool,
                                     _("Trying to elide mergeinfo at path\n"
                                       "    %s\n\n"),
                                     svn_min__get_mergeinfo_path(wc_mergeinfo,
                                                                 i)));
        }

      svn_pool_clear(iterpool);

      subtree_mergeinfo = svn_mergeinfo_dup(subtree_mergeinfo, iterpool);
      SVN_ERR(remove_obsolete_lines(session, subtree_mergeinfo, iterpool));

      if (parent_mergeinfo)
        {
          parent_mergeinfo = svn_mergeinfo_dup(parent_mergeinfo, iterpool);
          SVN_ERR(remove_lines(log, relpath, parent_mergeinfo,
                                subtree_mergeinfo, iterpool));
        }

      if (apr_hash_count(subtree_mergeinfo))
        {
          apr_hash_index_t *hi;

          if (parent_mergeinfo)
            SVN_ERR(svn_cmdline_printf(iterpool,
                      _("    Sub-tree merge info cannot be elided due to "
                        "the following branches:\n")));
          else
            SVN_ERR(svn_cmdline_printf(iterpool,
                      _("    Merge info kept for the following branches:\n")));

          for (hi = apr_hash_first(scratch_pool, subtree_mergeinfo);
                hi;
                hi = apr_hash_next(hi))
            {
              const char *branch = apr_hash_this_key(hi);
              SVN_ERR(svn_cmdline_printf(iterpool, _("    %s\n"), branch));
            }

          SVN_ERR(svn_cmdline_printf(iterpool, _("\n")));
        }
      else
        {
          SVN_ERR(svn_cmdline_printf(iterpool,
                    _("    All sub-tree mergeinfo can be elided.\n\n")));
        }
    }

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

/* This implements the `svn_opt_subcommand_t' interface. */
svn_error_t *
svn_min__analyze(apr_getopt_t *os,
                 void *baton,
                 apr_pool_t *pool)
{
  svn_min__cmd_baton_t *cmd_baton = baton;
  apr_pool_t *iterpool = svn_pool_create(pool);
  apr_pool_t *subpool = svn_pool_create(pool);

  int i;
  for (i = 0; i < cmd_baton->opt_state->targets->nelts; i++)
    {
      svn_ra_session_t *session;
      apr_array_header_t *wc_mergeinfo;
      svn_min__log_t *log;
      const char *url;
      const char *common_path;

      svn_pool_clear(iterpool);
      SVN_ERR(svn_min__add_wc_info(baton, i, iterpool, subpool));
      SVN_ERR(svn_client_open_ra_session2(&session, cmd_baton->repo_root,
                                          NULL, cmd_baton->ctx, iterpool,
                                          subpool));

      /* scan working copy */
      svn_pool_clear(subpool);
      SVN_ERR(svn_cmdline_printf(subpool, _("Scanning working copy %s ...\n"),
                                 cmd_baton->local_abspath));
      SVN_ERR(svn_min__read_mergeinfo(&wc_mergeinfo, cmd_baton, iterpool,
                                      subpool));
      SVN_ERR(svn_min__print_mergeinfo_stats(wc_mergeinfo, subpool));

      /* fetch log */
      svn_pool_clear(subpool);
      common_path = svn_min__common_parent(wc_mergeinfo, subpool, subpool);
      SVN_ERR_ASSERT(*common_path == '/');

      url = svn_path_url_add_component2(cmd_baton->repo_root,
                                        common_path + 1,
                                        subpool);
      SVN_ERR(svn_cmdline_printf(subpool, _("Fetching log for %s ...\n"),
                                 url));
      SVN_ERR(svn_min__log(&log, url, cmd_baton, iterpool, subpool));
      SVN_ERR(svn_min__print_log_stats(log, subpool));

      /* actual analysis */
      svn_pool_clear(subpool);
      SVN_ERR(analyze(session, wc_mergeinfo, log, subpool));
    }

  svn_pool_destroy(subpool);
  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}
