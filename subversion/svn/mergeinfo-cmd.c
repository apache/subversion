/*
 * mergeinfo-cmd.c -- Query merge-relative info.
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

#include "svn_pools.h"
#include "svn_client.h"
#include "svn_cmdline.h"
#include "svn_path.h"
#include "svn_error.h"
#include "svn_error_codes.h"
#include "svn_types.h"
#include "svn_sorts.h"
#include "cl.h"

#include "private/svn_client_private.h"
#include "private/svn_opt_private.h"
#include "svn_private_config.h"


/*** Code. ***/

struct print_log_rev_baton_t
{
  int count;
};

/* Implements the svn_log_entry_receiver_t interface. */
static svn_error_t *
print_log_rev(void *baton,
              svn_log_entry_t *log_entry,
              apr_pool_t *pool)
{
  if (log_entry->non_inheritable)
    SVN_ERR(svn_cmdline_printf(pool, "r%ld*\n", log_entry->revision));
  else
    SVN_ERR(svn_cmdline_printf(pool, "r%ld\n", log_entry->revision));

  return SVN_NO_ERROR;
}

/* Implements the svn_mergeinfo_receiver_t interface. */
static svn_error_t *
print_merged_rev(const svn_client_merged_rev_t *info,
                 void *baton,
                 apr_pool_t *pool)
{
  struct print_log_rev_baton_t *b = baton;
  const char *kind;

  /* Don't print many entries unless the user wants a verbose listing */
  /* ### For efficiency we should of course use the 'limit' option or
   * implement the ability for this callback to signal a 'break'. */
  if (b->count == 0)
    printf("  warning: The 'no-op', 'merge' and 'change' classifications are currently fake.\n");
  if (++b->count >= 5)
    {
      if (b->count == 5)
        SVN_ERR(svn_cmdline_printf(pool, "  ...\n"));
      return SVN_NO_ERROR;
    }

  /* Identify this source-rev as "original change" or "no-op" or "a merge" */
  if (info->is_merge)
    {
      kind = (info->content_modified ? "merge" : "no-op merge");
    }
  else if (info->content_modified)
    {
      kind = "change (at least on some paths)";
    }
  else
    {
      /* ### No-op revs aren't currently sent to this callback function at
       * all, but later we may use this function on such revs. */
      kind = "no-op";
    }
  SVN_ERR(svn_cmdline_printf(pool, "  r%ld -- %s %s\n", info->revnum,
                             kind, info->misc));
  return SVN_NO_ERROR;
}

/* Return TRUE iff SOURCE and TARGET refer to the same repository branch. */
static svn_boolean_t
targets_are_same_branch(svn_client_target_t *source,
                        svn_client_target_t *target,
                        apr_pool_t *pool)
{
  return (strcmp(source->repos_relpath, target->repos_relpath) == 0);
}

/* */
static const char *
path_relative_to_branch(const char *src_path,
                        apr_array_header_t *src_ranges,
                        svn_client_target_t *source,
                        /*source_segments*/
                        apr_pool_t *scratch_pool)
{
  const char *src_relpath;

  /* ### incomplete: should consider source_location_segments and ranges */
  SVN_ERR_ASSERT_NO_RETURN(src_path[0] == '/');
  src_relpath = svn_relpath_skip_ancestor(source->repos_relpath, src_path + 1);
  if (src_relpath == NULL)
    {
      printf("warning: source path '%s' was not in the source branch\n",
             src_path);
      src_relpath = src_path;
    }
  return src_relpath;
}

/* Pretty-print the mergeinfo recorded on TARGET that pertains to merges
 * from SOURCE. */
static svn_error_t *
print_recorded_ranges(svn_client_target_t *target,
                      svn_client_target_t *source,
                      svn_client_ctx_t *ctx,
                      apr_pool_t *scratch_pool)
{
  svn_mergeinfo_catalog_t mergeinfo_cat;
  apr_array_header_t *mergeinfo_cat_sorted;
  int i;

  SVN_ERR(svn_client__get_source_target_mergeinfo(
            &mergeinfo_cat, target->peg, source->peg,
            ctx, scratch_pool, scratch_pool));
  mergeinfo_cat_sorted = svn_sort__hash(mergeinfo_cat,
                                        svn_sort_compare_items_as_paths,
                                        scratch_pool);
  for (i = 0; i < mergeinfo_cat_sorted->nelts; i++)
    {
      svn_sort__item_t *item = &APR_ARRAY_IDX(mergeinfo_cat_sorted, i,
                                              svn_sort__item_t);
      const char *tgt_path = item->key;
      svn_mergeinfo_t mergeinfo = item->value;
      const char *tgt_relpath
        = svn_relpath_skip_ancestor(target->repos_relpath, tgt_path);
      
      if (apr_hash_count(mergeinfo))
        {
          apr_hash_index_t *hi;

          printf("  to target path '%s':\n",
                 tgt_relpath[0] ? tgt_relpath : ".");
          for (hi = apr_hash_first(scratch_pool, mergeinfo);
               hi; hi = apr_hash_next(hi))
            {
              const char *src_path = svn__apr_hash_index_key(hi);
              apr_array_header_t *ranges = svn__apr_hash_index_val(hi);
              const char *ranges_string;
              const char *src_relpath;

              SVN_ERR(svn_cl__rangelist_to_string_abbreviated(
                        &ranges_string, ranges, 4, scratch_pool, scratch_pool));

              /* ### Is it possible (however unlikely) that a single src_path
               * maps to more than one src_relpath because of the source
               * branch root having moved during this range and yet the source
               * node continuing to have the same path? */
              src_relpath = path_relative_to_branch(src_path, ranges, source,
                                                    /*source_segments*/
                                                    scratch_pool);
              printf("    %s", ranges_string);
              if (ranges->nelts >= 4)
                printf(" (%d ranges)", ranges->nelts);
              if (strcmp(src_relpath, tgt_relpath) != 0)
                printf(" from source path\n"
                       "                 '%s'", src_relpath);
              else
                printf(" from same relative path");
              printf("\n");
            }
        }
    }
  return SVN_NO_ERROR;
}

/* This implements the `svn_opt_subcommand_t' interface. */
svn_error_t *
svn_cl__mergeinfo(apr_getopt_t *os,
                  void *baton,
                  apr_pool_t *pool)
{
  svn_cl__opt_state_t *opt_state = ((svn_cl__cmd_baton_t *) baton)->opt_state;
  svn_client_ctx_t *ctx = ((svn_cl__cmd_baton_t *) baton)->ctx;
  apr_array_header_t *targets;
  svn_client_target_t *source;
  svn_client_target_t *target;
  svn_client_peg_t *source_peg;
  svn_client_peg_t *target_peg;

  SVN_ERR(svn_cl__args_to_target_array_print_reserved(&targets, os,
                                                      opt_state->targets,
                                                      ctx, FALSE, pool));

  if (targets->nelts > 2)
    return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                            _("Too many arguments given"));

  /* Locate the target branch: the second argument or this dir. */
  if (targets->nelts == 2)
    {
      SVN_ERR(svn_client__peg_parse(&target_peg,
                                    APR_ARRAY_IDX(targets, 1, const char *),
                                    pool));
    }
  else
    {
      SVN_ERR(svn_client__peg_parse(&target_peg, "", pool));
    }

  /* If no peg-rev was attached to a URL target, then assume HEAD; if
     no peg-rev was attached to a non-URL target, then assume BASE. */
  if (target_peg->peg_revision.kind == svn_opt_revision_unspecified)
    {
      if (svn_path_is_url(target_peg->path_or_url))
        target_peg->peg_revision.kind = svn_opt_revision_head;
      else
        target_peg->peg_revision.kind = svn_opt_revision_base;
    }

  SVN_ERR(svn_client__peg_resolve(&target, NULL, target_peg,
                                  ctx, pool, pool));

  /* Locate the source branch: the first argument or automatic.
   *
   * ### Better, perhaps, to always discover the "default" source branch,
   * and then print a warning if a different default branch was specified.
   *
   * ### Better, perhaps, not to support automatically selecting a
   * default source branch, because a more interesting and expected use
   * for not specifying a source branch would be to show info about
   * all merges that are currently sitting in the target (if it's a
   * working copy).
   */
  if (targets->nelts >= 1)
    {
      SVN_ERR(svn_client__peg_parse(&source_peg,
                                    APR_ARRAY_IDX(targets, 0, const char *),
                                    pool));
      /* If no peg-rev was attached to the source URL, assume HEAD. */
      if (source_peg->peg_revision.kind == svn_opt_revision_unspecified)
        source_peg->peg_revision.kind = svn_opt_revision_head;
    }
  else
    {
      printf("Assuming source branch is copy-source of target branch.\n");
      SVN_ERR(svn_cl__find_merge_source_branch(&source_peg, target_peg, ctx, pool));
    }
  
  SVN_ERR(svn_client__peg_resolve(&source, NULL, source_peg,
                                  ctx, pool, pool));

  if (targets_are_same_branch(source, target, pool))
    {
      return svn_error_create(SVN_ERR_CLIENT_NOT_READY_TO_MERGE, NULL,
                              _("Source and target are the same branch"));
    }

  if (opt_state->show_revs == svn_cl__show_revs_merged
      || opt_state->show_revs == svn_cl__show_revs_eligible)
    {
      /* Print a simple list of revision numbers. This mode is backward-
       * compatible with 1.5 and 1.6. */

      /* Default to depth empty. */
      svn_depth_t depth = (opt_state->depth == svn_depth_unknown)
                          ? svn_depth_infinity : opt_state->depth;

      SVN_ERR(svn_client_mergeinfo_log(
                opt_state->show_revs == svn_cl__show_revs_merged,
                target_peg->path_or_url, &target_peg->peg_revision,
                source_peg->path_or_url, &source_peg->peg_revision,
                print_log_rev, NULL /* baton */,
                TRUE, depth, NULL, ctx, pool));
    }
  else
    {
      /* Summary mode */
      const char *marker;
      struct print_log_rev_baton_t log_rev_baton;

      SVN_ERR(svn_client__check_branch_root_marker(&marker,
                                                   source_peg, target_peg,
                                                   ctx, pool));
      if (marker == NULL)
        {
          printf("warning: Source and target are not marked as branches.\n");
        }
      else
        {
          printf("Branch marker: '%s' (found on both source and target)\n",
                 marker);
        }

      printf("Source branch: %s\n", svn_cl__target_for_display(source, pool));
      printf("Target branch: %s\n", svn_cl__target_for_display(target, pool));
      printf("\n");

      printf(_("Extent of source branch under consideration:\n"));
      printf(  "  %s-%ld\n", "?" /* ### source_oldest_rev */, source->repos_revnum);
      printf("\n");

      printf(_("Revision range(s) recorded as merged:\n"));
      SVN_ERR(print_recorded_ranges(target, source, ctx, pool));
      printf("\n");

      printf(_("Merged revisions:\n"));
      log_rev_baton.count = 0;
      SVN_ERR(svn_client_mergeinfo_log2(TRUE /* finding_merged */,
                                        target_peg, source_peg,
                                        print_merged_rev, &log_rev_baton,
                                        NULL, ctx, pool));

      printf(_("Eligible revisions:\n"));
      log_rev_baton.count = 0;
      SVN_ERR(svn_client_mergeinfo_log2(FALSE /* finding_merged */,
                                        target_peg, source_peg,
                                        print_merged_rev, &log_rev_baton,
                                        NULL, ctx, pool));
    }

  return SVN_NO_ERROR;
}
