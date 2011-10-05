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

/* Set *CONTENT_MODIFIED if so, else set *MERGEINFO_CHANGED if so, else set
 * both to FALSE. */
static svn_error_t *
has_merge_prop_change(svn_boolean_t *content_modified,
                      svn_boolean_t *mergeinfo_changed,
                      apr_hash_t *changed_paths,
                      apr_pool_t *pool)
{
  apr_hash_index_t *hi;

  *content_modified = FALSE;
  *mergeinfo_changed = FALSE;

  for (hi = apr_hash_first(pool, changed_paths); hi; hi = apr_hash_next(hi))
    {
      const char *path = svn__apr_hash_index_key(hi);
      svn_log_changed_path2_t *cp = svn__apr_hash_index_val(hi);

      SVN_DBG(("%c %s%s %c %s\n", cp->action, svn_tristate__to_word(cp->text_modified), svn_tristate__to_word(cp->props_modified), cp->node_kind == svn_node_dir ? 'D' : 'f', path));
      if (cp->action == 'A' || cp->action == 'D'
          || cp->text_modified == svn_tristate_true)
        {
          *content_modified = TRUE;
          return SVN_NO_ERROR;
        }
    }
  return SVN_NO_ERROR;
}

/* Implements the svn_log_entry_receiver_t interface. */
static svn_error_t *
print_log_rev(void *baton,
              svn_log_entry_t *log_entry,
              apr_pool_t *pool)
{
  svn_boolean_t content_modified;
  svn_boolean_t mergeinfo_changed;
  const char *kind;

  /* Identify this source-rev as "original change" or "no-op" or "a merge" */
  SVN_ERR(has_merge_prop_change(&content_modified, &mergeinfo_changed,
                                log_entry->changed_paths2, pool));
  if (content_modified)
    {
      kind = "operative (at least on some paths)";
    }
  else if (mergeinfo_changed)
    {
      kind = "merge";
    }
  else
    {
      /* ### No-op revs aren't currently sent to this callback function at
       * all, but later we may use this function on such revs. */
      kind = "no-op";
    }
  SVN_ERR(svn_cmdline_printf(pool, "r%ld%s%s%s -- %s\n", log_entry->revision,
                             log_entry->non_inheritable ? "*" : " ",
                             log_entry->subtractive_merge ? " (reverse)" : "",
                             log_entry->has_children ? " (has children)" : "",
                             kind));
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
      return apr_psprintf(pool, "^/%s (wc base = r%ld)",
                          target->repos_relpath, target->repos_revnum);
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

  /* This isn't properly doc'd, but the first result it gives is the
   * copyfrom source URL. */
  SVN_ERR(svn_client_suggest_merge_sources(&suggestions,
                                           target->path_or_url,
                                           &target->peg_revision,
                                           ctx, pool));
  copyfrom_url = APR_ARRAY_IDX(suggestions, 0, const char *);

  SVN_ERR(svn_client__target(source_p, copyfrom_url, pool));
  (*source_p)->peg_revision.kind = svn_opt_revision_number;
  (*source_p)->peg_revision.value.number = 1170000;
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
  /* Default to depth empty. */
  svn_depth_t depth = opt_state->depth == svn_depth_unknown
    ? svn_depth_infinity : opt_state->depth;

  SVN_ERR(svn_cl__args_to_target_array_print_reserved(&targets, os,
                                                      opt_state->targets,
                                                      ctx, FALSE, pool));

  if (targets->nelts > 2)
    return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                            _("Too many arguments given"));

  /* Locate the target branch: the second argument or this dir. */
  if (targets->nelts == 2)
    {
      SVN_ERR(svn_client__target(&target, "", pool));
      SVN_ERR(svn_opt_parse_path(&target->peg_revision, &target->path_or_url,
                                 APR_ARRAY_IDX(targets, 1, const char *),
                                 pool));
      target->revision.kind = svn_opt_revision_unspecified;
    }
  else
    {
      SVN_ERR(svn_client__target(&target, "", pool));
      target->peg_revision.kind = svn_opt_revision_working;
      target->revision.kind = svn_opt_revision_working;
    }

  SVN_ERR(svn_client__resolve_target_location(target, ctx, pool));

  /* Locate the source branch: the first argument or automatic. */
  if (targets->nelts >= 1)
    {
      const char *path_or_url;
      svn_opt_revision_t peg_revision;

      SVN_ERR(svn_opt_parse_path(&peg_revision, &path_or_url,
                                 APR_ARRAY_IDX(targets, 0, const char *), pool));
      SVN_ERR(svn_client__target(&source, path_or_url, pool));
      source->peg_revision = peg_revision;
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
  SVN_ERR(svn_client_mergeinfo_log(TRUE /* finding_merged */,
                                   target->path_or_url,
                                   &target->peg_revision,
                                   source->path_or_url,
                                   &source->peg_revision,
                                   print_log_rev, NULL,
                                   TRUE, depth, NULL, ctx,
                                   pool));

  printf(_("Eligible revisions:\n"));
  SVN_ERR(svn_client_mergeinfo_log(FALSE /* finding_merged */,
                                   target->path_or_url,
                                   &target->peg_revision,
                                   source->path_or_url,
                                   &source->peg_revision,
                                   print_log_rev, NULL,
                                   TRUE, depth, NULL, ctx,
                                   pool));

  return SVN_NO_ERROR;
}
