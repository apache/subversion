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
#include "cl.h"

#include "svn_private_config.h"
#include "private/svn_client_private.h"


/*** Code. ***/

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

/* Draw a diagram (by printing text to the console) summarizing the state
 * of merging between two branches, given the merge description
 * indicated by YCA, BASE, RIGHT, TARGET, REINTEGRATE_LIKE. */
static svn_error_t *
mergeinfo_diagram(svn_client__pathrev_t *yca,
                  svn_client__pathrev_t *base,
                  svn_client__pathrev_t *right,
                  svn_client__pathrev_t *target,
                  svn_boolean_t target_is_wc,
                  svn_boolean_t reintegrate_like,
                  apr_pool_t *pool)
{
  /* The graph occupies 4 rows of text, and the annotations occupy
   * another 2 rows above and 2 rows below.  The graph is constructed
   * from left to right in discrete sections ("columns"), each of which
   * can have a different width (measured in characters).  Each element in
   * the array is either a text string of the appropriate width, or can
   * be NULL to draw a blank cell. */
#define ROWS 8
#define COLS 4
  const char *g[ROWS][COLS] = {{0}};
  int col_width[COLS];
  int row, col;

  /* The YCA (that is, the branching point).  And an ellipsis, because we
   * don't show information about earlier merges */
  g[0][0] = apr_psprintf(pool, "  %-8ld  ", yca->rev);
  g[1][0] =     "  |         ";
  if (strcmp(yca->url, right->url) == 0)
    {
      g[2][0] = "-------| |--";
      g[3][0] = "   \\        ";
      g[4][0] = "    \\       ";
      g[5][0] = "     --| |--";
    }
  else if (strcmp(yca->url, target->url) == 0)
    {
      g[2][0] = "     --| |--";
      g[3][0] = "    /       ";
      g[4][0] = "   /        ";
      g[5][0] = "-------| |--";
    }
  else
    {
      g[2][0] = "     --| |--";
      g[3][0] = "... /       ";
      g[4][0] = "    \\       ";
      g[5][0] = "     --| |--";
    }

  /* The last full merge */
  if ((base->rev > yca->rev) && reintegrate_like)
    {
      g[2][2] = "---------";
      g[3][2] = "  /      ";
      g[4][2] = " /       ";
      g[5][2] = "---------";
      g[6][2] = "|        ";
      g[7][2] = apr_psprintf(pool, "%-8ld ", base->rev);
    }
  else if (base->rev > yca->rev)
    {
      g[0][2] = apr_psprintf(pool, "%-8ld ", base->rev);
      g[1][2] = "|        ";
      g[2][2] = "---------";
      g[3][2] = " \\       ";
      g[4][2] = "  \\      ";
      g[5][2] = "---------";
    }
  else
    {
      g[2][2] = "---------";
      g[3][2] = "         ";
      g[4][2] = "         ";
      g[5][2] = "---------";
    }

  /* The tips of the branches */
    {
      g[0][3] = apr_psprintf(pool, "%-8ld", right->rev);
      g[1][3] = "|       ";
      g[2][3] = "-       ";
      g[3][3] = "        ";
      g[4][3] = "        ";
      g[5][3] = "-       ";
      g[6][3] = "|       ";
      g[7][3] = target_is_wc ? "WC      "
                             : apr_psprintf(pool, "%-8ld", target->rev);
    }

  /* Find the width of each column, so we know how to print blank cells */
  for (col = 0; col < COLS; col++)
    {
      col_width[col] = 0;
      for (row = 0; row < ROWS; row++)
        {
          if (g[row][col] && (strlen(g[row][col]) > col_width[col]))
            col_width[col] = strlen(g[row][col]);
        }
    }

  /* Column headings */
  SVN_ERR(svn_cmdline_fputs(
            _("    youngest  last               repos.\n"
              "    common    full     tip of    path of\n"
              "    ancestor  merge    branch    branch\n"
              "\n"),
            stdout, pool));

  /* Print the diagram, row by row */
  for (row = 0; row < ROWS; row++)
    {
      SVN_ERR(svn_cmdline_fputs("  ", stdout, pool));
      for (col = 0; col < COLS; col++)
        {
          if (g[row][col])
            {
              SVN_ERR(svn_cmdline_fputs(g[row][col], stdout, pool));
            }
          else
            {
              /* Print <column-width> spaces */
              SVN_ERR(svn_cmdline_printf(pool, "%*s", col_width[col], ""));
            }
        }
      if (row == 2)
        SVN_ERR(svn_cmdline_printf(pool, "  %s",
                svn_client__pathrev_relpath(right, pool)));
      if (row == 5)
        SVN_ERR(svn_cmdline_printf(pool, "  %s",
                svn_client__pathrev_relpath(target, pool)));
      SVN_ERR(svn_cmdline_fputs("\n", stdout, pool));
    }

  return SVN_NO_ERROR;
}

/* Display a summary of the state of merging between the two branches
 * SOURCE_PATH_OR_URL@SOURCE_REVISION and
 * TARGET_PATH_OR_URL@TARGET_REVISION. */
static svn_error_t *
mergeinfo_summary(
                  const char *source_path_or_url,
                  const svn_opt_revision_t *source_revision,
                  const char *target_path_or_url,
                  const svn_opt_revision_t *target_revision,
                  svn_client_ctx_t *ctx,
                  apr_pool_t *pool)
{
  svn_client__symmetric_merge_t *the_merge;
  svn_client__pathrev_t *yca, *base, *right, *target;
  svn_boolean_t target_is_wc, reintegrate_like;

  target_is_wc = (! svn_path_is_url(target_path_or_url))
                 && (target_revision->kind == svn_opt_revision_unspecified
                     || target_revision->kind == svn_opt_revision_working);
  if (target_is_wc)
    SVN_ERR(svn_client__find_symmetric_merge(
              &the_merge,
              source_path_or_url, source_revision,
              target_path_or_url,
              TRUE, TRUE, TRUE,  /* allow_* */
              ctx, pool, pool));
  else
    SVN_ERR(svn_client__find_symmetric_merge_no_wc(
              &the_merge,
              source_path_or_url, source_revision,
              target_path_or_url, target_revision,
              ctx, pool, pool));

  SVN_ERR(svn_client__symmetric_merge_get_locations(
            &yca, &base, &right, &target, the_merge, pool));
  reintegrate_like = svn_client__symmetric_merge_is_reintegrate_like(the_merge);

  SVN_ERR(mergeinfo_diagram(yca, base, right, target,
                            target_is_wc, reintegrate_like,
                            pool));

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
  const char *source, *target;
  svn_opt_revision_t src_peg_revision, tgt_peg_revision;
  /* Default to depth empty. */
  svn_depth_t depth = (opt_state->depth == svn_depth_unknown)
                      ? svn_depth_empty : opt_state->depth;

  SVN_ERR(svn_cl__args_to_target_array_print_reserved(&targets, os,
                                                      opt_state->targets,
                                                      ctx, FALSE, pool));

  /* We expect a single source URL followed by a single target --
     nothing more, nothing less. */
  if (targets->nelts < 1)
    return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                            _("Not enough arguments given"));
  if (targets->nelts > 2)
    return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                            _("Too many arguments given"));

  /* Parse the SOURCE-URL[@REV] argument. */
  SVN_ERR(svn_opt_parse_path(&src_peg_revision, &source,
                             APR_ARRAY_IDX(targets, 0, const char *), pool));

  /* Parse the TARGET[@REV] argument (if provided). */
  if (targets->nelts == 2)
    {
      SVN_ERR(svn_opt_parse_path(&tgt_peg_revision, &target,
                                 APR_ARRAY_IDX(targets, 1, const char *),
                                 pool));
    }
  else
    {
      target = "";
      tgt_peg_revision.kind = svn_opt_revision_unspecified;
    }

  /* If no peg-rev was attached to the source URL, assume HEAD. */
  if (src_peg_revision.kind == svn_opt_revision_unspecified)
    src_peg_revision.kind = svn_opt_revision_head;

  /* If no peg-rev was attached to a URL target, then assume HEAD; if
     no peg-rev was attached to a non-URL target, then assume BASE. */
  if (tgt_peg_revision.kind == svn_opt_revision_unspecified)
    {
      if (svn_path_is_url(target))
        tgt_peg_revision.kind = svn_opt_revision_head;
      else
        tgt_peg_revision.kind = svn_opt_revision_base;
    }

  SVN_ERR_W(svn_cl__check_related_source_and_target(source, &src_peg_revision,
                                                    target, &tgt_peg_revision,
                                                    ctx, pool),
            _("Source and target must be different but related branches"));

  /* Do the real work, depending on the requested data flavor. */
  if (opt_state->show_revs == svn_cl__show_revs_merged)
    {
      SVN_ERR(svn_client_mergeinfo_log2(TRUE, target, &tgt_peg_revision,
                                        source, &src_peg_revision,
                                        &(opt_state->start_revision),
                                        &(opt_state->end_revision),
                                        print_log_rev, NULL,
                                        TRUE, depth, NULL, ctx,
                                        pool));
    }
  else if (opt_state->show_revs == svn_cl__show_revs_eligible)
    {
      SVN_ERR(svn_client_mergeinfo_log2(FALSE, target, &tgt_peg_revision,
                                        source, &src_peg_revision,
                                        &(opt_state->start_revision),
                                        &(opt_state->end_revision),
                                        print_log_rev, NULL,
                                        TRUE, depth, NULL, ctx,
                                        pool));
    }
  else
    {
      SVN_ERR(mergeinfo_summary(source, &src_peg_revision,
                                target, &tgt_peg_revision,
                                ctx, pool));
    }
  return SVN_NO_ERROR;
}
