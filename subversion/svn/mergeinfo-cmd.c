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

#include "private/svn_client_private.h"
#include "private/svn_opt_private.h"
#include "svn_private_config.h"


/*** Code. ***/

struct print_log_rev_baton_t
{
  int count;
};

/* Implements the svn_mergeinfo_receiver_t interface. */
static svn_error_t *
print_log_rev(const svn_client_merged_rev_t *info,
              void *baton,
              apr_pool_t *pool)
{
  struct print_log_rev_baton_t *b = baton;
  const char *kind;

  /* Don't print too much unless the user wants a verbose listing */
  if (++b->count >= 5)
    {
      if (b->count == 5)
        SVN_ERR(svn_cmdline_printf(pool, "  ...\n"));
      return SVN_NO_ERROR;
    }

  /* Identify this source-rev as "original change" or "no-op" or "a merge" */
  if (info->is_merge)
    {
      kind = "merge";
    }
  else if (info->content_modified)
    {
      kind = "operative (at least on some paths)";
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

/* */
static svn_boolean_t
location_is_resolved(const svn_client_target_t *location)
{
  return (location->repos_uuid != NULL);
}

/* */
static const char *
target_for_display(const svn_client_target_t *target,
                   apr_pool_t *pool)
{
  SVN_ERR_ASSERT_NO_RETURN(location_is_resolved(target));
  if (target->revision.kind == svn_opt_revision_working)
    {
      SVN_ERR_ASSERT_NO_RETURN(target->peg_revision.kind == svn_opt_revision_working);
      return apr_psprintf(pool, "^/%s (working copy)",
                          target->repos_relpath);
    }
  if (target->revision.kind == svn_opt_revision_base)
    {
      SVN_ERR_ASSERT_NO_RETURN(target->peg_revision.kind == svn_opt_revision_base);
      return apr_psprintf(pool, "^/%s (wc base)",
                          target->repos_relpath);
    }
  return apr_psprintf(pool, "^/%s (r%ld)",
                      target->repos_relpath, target->repos_revnum);
}

/* Return TRUE iff SOURCE and TARGET refer to the same repository branch. */
static svn_boolean_t
targets_are_same_branch(svn_client_target_t *source,
                        svn_client_target_t *target,
                        apr_pool_t *pool)
{
  return (strcmp(source->repos_relpath, target->repos_relpath) == 0);
}

/* Find the preferred "parent" branch.  At the moment, returns the
 * copyfrom path (at peg rev r1170000). */
static svn_error_t *
find_source_branch(svn_client_target_t **source_p,
                   svn_client_target_t *target,
                   svn_client_ctx_t *ctx,
                   apr_pool_t *pool)
{
  apr_array_header_t *suggestions;
  const char *copyfrom_url;
  svn_opt_revision_t peg_revision = { svn_opt_revision_number, { 1170000 } };

  /* This isn't properly doc'd, but the first result it gives is the
   * copyfrom source URL. */
  SVN_ERR(svn_client_suggest_merge_sources(&suggestions,
                                           target->path_or_url,
                                           &target->peg_revision,
                                           ctx, pool));
  copyfrom_url = APR_ARRAY_IDX(suggestions, 0, const char *);

  SVN_ERR(svn_client__target(source_p, copyfrom_url, &peg_revision, pool));
  (*source_p)->revision.kind = svn_opt_revision_unspecified;

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
  const char *marker;
  /* Default to depth infinity. */
  svn_depth_t depth = opt_state->depth == svn_depth_unknown
    ? svn_depth_infinity : opt_state->depth;
  struct print_log_rev_baton_t log_rev_baton;

  SVN_ERR(svn_cl__args_to_target_array_print_reserved(&targets, os,
                                                      opt_state->targets,
                                                      ctx, FALSE, pool));

  if (targets->nelts > 2)
    return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                            _("Too many arguments given"));

  /* Locate the target branch: the second argument or this dir. */
  if (targets->nelts == 2)
    {
      SVN_ERR(svn_client__parse_target(&target,
                                       APR_ARRAY_IDX(targets, 1, const char *),
                                       pool));
      target->revision.kind = svn_opt_revision_unspecified;
    }
  else
    {
      svn_opt_revision_t peg_revision = { svn_opt_revision_working, { 0 } };

      SVN_ERR(svn_client__target(&target, "", &peg_revision, pool));
      target->revision.kind = svn_opt_revision_working;
    }

  SVN_ERR(svn_client__resolve_target_location(target, ctx, pool));

  /* Locate the source branch: the first argument or automatic. */
  if (targets->nelts >= 1)
    {
      SVN_ERR(svn_client__parse_target(&source,
                                       APR_ARRAY_IDX(targets, 0, const char *),
                                       pool));
      source->revision.kind = svn_opt_revision_unspecified;

      /* If no peg-rev was attached to the source URL, assume HEAD. */
      if (source->peg_revision.kind == svn_opt_revision_unspecified)
        source->peg_revision.kind = svn_opt_revision_head;
    }
  else
    {
      printf("Assuming source branch is copied-from source of target branch.\n");
      SVN_ERR(find_source_branch(&source, target, ctx, pool));
    }

  SVN_ERR(svn_client__resolve_target_location(source, ctx, pool));

  printf("Source branch: %s\n", target_for_display(source, pool));
  printf("Target branch: %s\n", target_for_display(target, pool));

  if (targets_are_same_branch(source, target, pool))
    {
      return svn_error_create(SVN_ERR_CLIENT_NOT_READY_TO_MERGE, NULL,
                              _("Source and target are the same branch"));
    }
  SVN_ERR(svn_client__check_branch_root_marker(&marker, source, target,
                                               ctx, pool));
  if (marker == NULL)
    {
      printf("warning: Source and target are not marked as branches.\n");
    }
  else
    {
      printf("Source and target are marked as branches of the same project.\n");
    }

  /* If no peg-rev was attached to a URL target, then assume HEAD; if
     no peg-rev was attached to a non-URL target, then assume BASE. */
  if (target->peg_revision.kind == svn_opt_revision_unspecified)
    {
      if (svn_path_is_url(target->path_or_url))
        target->peg_revision.kind = svn_opt_revision_head;
      else
        target->peg_revision.kind = svn_opt_revision_base;
    }

  printf(_("Merged revisions:\n"));
  log_rev_baton.count = 0;
  SVN_ERR(svn_client_mergeinfo_log2(TRUE /* finding_merged */,
                                    target, source,
                                    print_log_rev, &log_rev_baton,
                                    NULL, ctx, pool));

  printf(_("Eligible revisions:\n"));
  log_rev_baton.count = 0;
  SVN_ERR(svn_client_mergeinfo_log2(FALSE /* finding_merged */,
                                    target, source,
                                    print_log_rev, &log_rev_baton,
                                    NULL, ctx, pool));

  return SVN_NO_ERROR;
}
