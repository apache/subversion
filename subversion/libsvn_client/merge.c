/*
 * merge.c: merging
 *
 * ====================================================================
 * Copyright (c) 2000-2007 CollabNet.  All rights reserved.
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

/* ==================================================================== */



/*** Includes ***/

#include <apr_strings.h>
#include <apr_tables.h>
#include <apr_hash.h>
#include "svn_types.h"
#include "svn_hash.h"
#include "svn_wc.h"
#include "svn_delta.h"
#include "svn_diff.h"
#include "svn_dirent_uri.h"
#include "svn_mergeinfo.h"
#include "svn_client.h"
#include "svn_string.h"
#include "svn_error.h"
#include "svn_dirent_uri.h"
#include "svn_path.h"
#include "svn_io.h"
#include "svn_utf.h"
#include "svn_pools.h"
#include "svn_config.h"
#include "svn_props.h"
#include "svn_time.h"
#include "svn_sorts.h"
#include "svn_ra.h"
#include "client.h"
#include "mergeinfo.h"

#include "private/svn_wc_private.h"
#include "private/svn_mergeinfo_private.h"

#include "svn_private_config.h"

/*-----------------------------------------------------------------------*/

/* MERGEINFO MERGE SOURCE NORMALIZATION
 *
 * Nearly any helper function herein that accepts two URL/revision
 * pairs expects one of two things to be true:
 *
 *    1.  that mergeinfo is not being recorded at all for this
 *        operation, or
 *
 *    2.  that the pairs represent two locations along a single line
 *        of version history such that there are no copies in the
 *        history of the object between the locations when treating
 *        the oldest of the two locations as non-inclusive.  In other
 *        words, if there is a copy at all between them, there is only
 *        one copy and its source was the oldest of the two locations.
 *
 * We use svn_ra_get_location_segments() to split a given range of
 * revisions across an object's history into several which obey these
 * rules.  For example, a merge between r19500 and r27567 of
 * Subversion's own /tags/1.4.5 directory gets split into sequential
 * merges of the following location pairs:
 *
 *    [/trunk:19549, /trunk:19523]
 *    (recorded in svn:mergeinfo as /trunk:19500-19523)
 *
 *    [/trunk:19523, /branches/1.4.x:25188]
 *    (recorded in svn:mergeinfo as /branches/1.4.x:19524-25188)
 *
 *    [/branches/1.4.x:25188, /tags/1.4.4@26345]
 *    (recorded in svn:mergeinfo as /tags/1.4.4:25189-26345)
 *
 *    [/tags/1.4.4@26345, /branches/1.4.5@26350]
 *    (recorded in svn:mergeinfo as /branches/1.4.5:26346-26350)
 *
 *    [/branches/1.4.5@26350, /tags/1.4.5@27567]
 *    (recorded in svn:mergeinfo as /tags/1.4.5:26351-27567)
 *
 * Our helper functions would then operate on one of these location
 * pairs at a time.
 */

/* WHICH SVN_CLIENT_MERGE* API DO I WANT?
 *
 * libsvn_client has three public merge APIs; they are all wrappers
 * around the do_merge engine.  Which one to use depends on the number
 * of URLs passed as arguments and whether or not specific merge
 * ranges (-c/-r) are specified.
 *
 *                 1 URL                        2 URLs
 * +----+--------------------------------+---------------------+
 * | -c |       mergeinfo-driven         |                     |
 * | or |        cherrypicking           |                     |
 * | -r |    (svn_client_merge_peg)      |                     |
 * |----+--------------------------------+                     |
 * |    |       mergeinfo-driven         |     unsupported     |
 * |    |  'cherry harvest', i.e. merge  |                     |
 * |    |  all revisions from URL that   |                     |
 * | no |  have not already been merged  |                     |
 * | -c |    (svn_client_merge_peg)      |                     |
 * | or +--------------------------------+---------------------+
 * | -r |      mergeinfo-driven          |   mergeinfo-writing |
 * |    |        whole-branch            |    diff-and-apply   |
 * |    |       heuristic merge          |  (svn_client_merge) |
 * |    | (svn_client_merge_reintegrate) |                     |
 * +----+--------------------------------+---------------------+
 *
 *
 */

/* THE CHILDREN_WITH_MERGEINFO ARRAY
 *
 * Many of the helper functions in this file pass around an
 * apr_array_header_t *CHILDREN_WITH_MERGEINFO.  This is a depth first
 * sorted array filled with svn_client__merge_path_t * describing the
 * merge target and any of its subtrees which have explicit mergeinfo
 * or otherwise need special attention during a merge.
 *
 * During mergeinfo unaware merges, CHILDREN_WITH_MERGEINFO is created by
 * do_mergeinfo_unaware_dir_merge and contains only one element describing
 * a contiguous range to be merged to the WC merge target.
 *
 * During mergeinfo aware merges CHILDREN_WITH_MERGEINFO is created
 * by get_mergeinfo_paths() and outside of that function and its helpers
 * should always meet the criteria dictated in get_mergeinfo_paths()'s doc
 * string.  The elements of CHILDREN_WITH_MERGINFO should never be NULL.
 *
 * For clarification on mergeinfo aware vs. mergeinfo unaware merges, see
 * the doc string for honor_mergeinfo().
 */

/*-----------------------------------------------------------------------*/

/*** Utilities ***/

/* Sanity check -- ensure that we have valid revisions to look at. */
#define ENSURE_VALID_REVISION_KINDS(rev1_kind, rev2_kind) \


/* Return SVN_ERR_UNSUPPORTED_FEATURE if URL's scheme does not
   match the scheme of the url for ADM_ACCESS's path; return
   SVN_ERR_BAD_URL if no scheme can be found for one or both urls;
   otherwise return SVN_NO_ERROR.  Use ADM_ACCESS's pool for
   temporary allocation. */
static svn_error_t *
check_scheme_match(svn_wc_adm_access_t *adm_access, const char *url)
{
  const char *path = svn_wc_adm_access_path(adm_access);
  apr_pool_t *pool = svn_wc_adm_access_pool(adm_access);
  const svn_wc_entry_t *ent;
  const char *idx1, *idx2;

  SVN_ERR(svn_wc_entry(&ent, path, adm_access, FALSE, pool));

  idx1 = strchr(url, ':');
  idx2 = strchr(ent->url, ':');

  if ((idx1 == NULL) && (idx2 == NULL))
    {
      return svn_error_createf
        (SVN_ERR_BAD_URL, NULL,
         _("URLs have no scheme ('%s' and '%s')"), url, ent->url);
    }
  else if (idx1 == NULL)
    {
      return svn_error_createf
        (SVN_ERR_BAD_URL, NULL,
         _("URL has no scheme: '%s'"), url);
    }
  else if (idx2 == NULL)
    {
      return svn_error_createf
        (SVN_ERR_BAD_URL, NULL,
         _("URL has no scheme: '%s'"), ent->url);
    }
  else if (((idx1 - url) != (idx2 - ent->url))
           || (strncmp(url, ent->url, idx1 - url) != 0))
    {
      return svn_error_createf
        (SVN_ERR_UNSUPPORTED_FEATURE, NULL,
         _("Access scheme mixtures not yet supported ('%s' and '%s')"),
         url, ent->url);
    }

  /* else */

  return SVN_NO_ERROR;
}


/*-----------------------------------------------------------------------*/

/*** Repos-Diff Editor Callbacks ***/

typedef struct merge_source_t
{
  /* "left" side URL and revision (inclusive iff youngest) */
  const char *url1;
  svn_revnum_t rev1;

  /* "right" side URL and revision (inclusive iff youngest) */
  const char *url2;
  svn_revnum_t rev2;

} merge_source_t;

typedef struct merge_cmd_baton_t {
  svn_boolean_t force;
  svn_boolean_t dry_run;
  svn_boolean_t record_only;          /* Whether to only record mergeinfo. */
  svn_boolean_t sources_ancestral;    /* Whether the left-side merge source is
                                         an ancestor of the right-side, or
                                         vice-versa (history-wise). */
  svn_boolean_t same_repos;           /* Whether the merge source repository
                                         is the same repository as the
                                         target.  Defaults to FALSE if DRY_RUN
                                         is TRUE.*/
  svn_boolean_t mergeinfo_capable;    /* Whether the merge source server
                                         is capable of Merge Tracking. */
  svn_boolean_t ignore_ancestry;      /* Are we ignoring ancestry (and by
                                         extension, mergeinfo)?  FALSE if
                                         SOURCES_ANCESTRAL is FALSE. */
  svn_boolean_t target_missing_child; /* Whether working copy target of the
                                         merge is missing any immediate
                                         children. */
  svn_boolean_t reintegrate_merge;    /* Whether this is a --reintegrate
                                         merge or not. */
  const char *added_path;             /* Set to the dir path whenever the
                                         dir is added as a child of a
                                         versioned dir (dry-run only) */
  const char *target;                 /* Working copy target of merge,
                                         either absolute or relative to
                                         the current working directory. */

  /* The left and right URLs and revs.  The value of this field changes to
     reflect the merge_source_t *currently* being merged by do_merge(). */
  merge_source_t merge_source;

  /* Rangelist containing single range which describes the gap, if any,
     in the natural history of the merge source currently being processed.
     See http://subversion.tigris.org/issues/show_bug.cgi?id=3432.
     Updated during each call to do_directory_merge().  May be NULL if there
     is no gap. */
  apr_array_header_t *implicit_src_gap;

  svn_client_ctx_t *ctx;              /* Client context for callbacks, etc. */

  /* Whether invocation of the merge_file_added() callback required
     delegation to the merge_file_changed() function for the file
     currently being merged.  This info is used to detect whether a
     file on the left side of a 3-way merge actually exists (important
     because it's created as an empty temp file on disk regardless).*/
  svn_boolean_t add_necessitated_merge;

  /* The list of paths for entries we've deleted, used only when in
     dry_run mode. */
  apr_hash_t *dry_run_deletions;

  /* The list of any paths which remained in conflict after a
     resolution attempt was made.  We track this in-memory, rather
     than just using WC entry state, since the latter doesn't help us
     when in dry_run mode. */
  apr_hash_t *conflicted_paths;

  /* A list of paths which had no explicit mergeinfo prior to the merge but
     got explicit mergeinfo added by the merge.  This is populated by
     merge_change_props() and is allocated in POOL so it is subject to the
     lifetime limitations of POOL.  Is NULL if no paths are found which
     meet the criteria or DRY_RUN is true. */
  apr_hash_t *paths_with_new_mergeinfo;

  /* A list of paths which had explicit mergeinfo prior to the merge but
     had this mergeinfo deleted by the merge.  This is populated by
     merge_change_props() and is allocated in POOL so it is subject to the
     lifetime limitations of POOL.  Is NULL if no paths are found which
     meet the criteria or DRY_RUN is true. */
  apr_hash_t *paths_with_deleted_mergeinfo;

  /* The diff3_cmd in ctx->config, if any, else null.  We could just
     extract this as needed, but since more than one caller uses it,
     we just set it up when this baton is created. */
  const char *diff3_cmd;
  const apr_array_header_t *merge_options;

  /* RA sessions used throughout a merge operation.  Opened/re-parented
     as needed.

     NOTE: During the actual merge editor drive, RA_SESSION1 is used
     for the primary editing and RA_SESSION2 for fetching additional
     information -- as necessary -- from the repository.  So during
     this phase of the merge, you *must not* reparent RA_SESSION1; use
     (temporarily reparenting if you must) RA_SESSION2 instead.  */
  svn_ra_session_t *ra_session1;
  svn_ra_session_t *ra_session2;

  /* During the merge, *USE_SLEEP is set to TRUE if a sleep will be required
     afterwards to ensure timestamp integrity, or unchanged if not. */
  svn_boolean_t *use_sleep;

  /* Pool which has a lifetime limited to one iteration over a given
     merge source, i.e. it is cleared on every call to do_directory_merge()
     or do_file_merge() in do_merge(). */
  apr_pool_t *pool;
} merge_cmd_baton_t;

apr_hash_t *
svn_client__dry_run_deletions(void *merge_cmd_baton)
{
  merge_cmd_baton_t *merge_b = merge_cmd_baton;
  return merge_b->dry_run_deletions;
}

/* Return true iff we're in dry-run mode and WCPATH would have been
   deleted by now if we weren't in dry-run mode.
   Used to avoid spurious notifications (e.g. conflicts) from a merge
   attempt into an existing target which would have been deleted if we
   weren't in dry_run mode (issue #2584).  Assumes that WCPATH is
   still versioned (e.g. has an associated entry). */
static APR_INLINE svn_boolean_t
dry_run_deleted_p(const merge_cmd_baton_t *merge_b, const char *wcpath)
{
  return (merge_b->dry_run &&
          apr_hash_get(merge_b->dry_run_deletions, wcpath,
                       APR_HASH_KEY_STRING) != NULL);
}

/* Return whether any WC path was put in conflict by the merge
   operation corresponding to MERGE_B. */
static APR_INLINE svn_boolean_t
is_path_conflicted_by_merge(merge_cmd_baton_t *merge_b)
{
  return (merge_b->conflicted_paths &&
          apr_hash_count(merge_b->conflicted_paths) > 0);
}

/* Return the node kind of the working version of local path PATH,
 * according to the WC metadata in ENTRY. If ENTRY is null, assume the node
 * is unversioned and so set the kind to 'none'.
 *
 * However, if this is a dry run, set *NODE_KIND to 'none' if the node would
 * already have been deleted by the merge if this were not a dry run. Use
 * MERGE_B to determine the dry-run details. */
static svn_node_kind_t
node_kind_working(const char *path,
                  const merge_cmd_baton_t *merge_b,
                  const svn_wc_entry_t *entry)
{
  if (!entry
      || (entry->schedule == svn_wc_schedule_delete)
      || (merge_b->dry_run && dry_run_deleted_p(merge_b, path))
      || (entry->deleted && entry->schedule != svn_wc_schedule_add))
    return svn_node_none;
  else
    return entry->kind;
}

/* Return the node kind that is found on disk at local path PATH.
 *
 * However, if this is a dry run, set *NODE_KIND to 'none' if the node would
 * already have been deleted by the merge if this were not a dry run. Use
 * MERGE_B to determine the dry-run details. */
static svn_node_kind_t
node_kind_on_disk(const char *path,
                  const merge_cmd_baton_t *merge_b,
                  apr_pool_t *pool)
{
  svn_error_t *err;
  svn_node_kind_t node_kind;

  err = svn_io_check_path(path, &node_kind, pool);
  if (err)
    {
      svn_error_clear(err);
      return svn_node_unknown;
    }
  else if (dry_run_deleted_p(merge_b, path))
    return svn_node_none;
  else
    return node_kind;
}

/* Return a state indicating whether the WC metadata in ENTRY matches the
 * node kind on disk of the local path PATH. If ENTRY is null, assume the
 * node is unversioned. In the case of a dry-run merge, use the disk node
 * kind that would exist if it were not a dry run. Use MERGE_B to determine
 * the dry-run details.
 *
 *   - Return svn_wc_notify_state_inapplicable if the node kind matches.
 *   - Return 'obstructed' if there is a node on disk where none or a
 *     different kind is expected, or if the disk node cannot be read.
 *   - Return 'missing' if there is no node on disk but one is expected. */
static svn_wc_notify_state_t
obstructed_or_missing(const char *path,
                      svn_wc_adm_access_t *adm_access,
                      const merge_cmd_baton_t *merge_b,
                      apr_pool_t *pool)
{
  svn_error_t *err;
  const svn_wc_entry_t *entry;
  svn_node_kind_t kind_expected, kind_on_disk;

  err = svn_wc_entry(&entry, path, adm_access, TRUE, pool);
  if (err)
    {
      svn_error_clear(err);
      entry = NULL;
    }

  if (entry && entry->absent)
    return svn_wc_notify_state_missing;

  kind_expected = node_kind_working(path, merge_b, entry);
  kind_on_disk = node_kind_on_disk(path, merge_b, pool);

  /* If it's a sched-delete directory, change the expected kind to "dir"
   * because the directory should not yet have gone from disk. */
  if (entry && entry->kind == svn_node_dir
      && entry->schedule == svn_wc_schedule_delete
      && kind_on_disk == svn_node_dir)
    kind_expected = svn_node_dir;

  if (kind_expected == kind_on_disk)
    return svn_wc_notify_state_inapplicable;
  else if (kind_on_disk == svn_node_none)
    return svn_wc_notify_state_missing;
  else
    return svn_wc_notify_state_obstructed;
}

/* Create a tree-conflict description in *CONFLICT.
 * See tree_conflict() for function parameters.
 */
static svn_error_t*
make_tree_conflict(svn_wc_conflict_description_t **conflict,
                   merge_cmd_baton_t *merge_b,
                   svn_wc_adm_access_t *adm_access,
                   const char *victim_path,
                   svn_node_kind_t node_kind,
                   svn_wc_conflict_action_t action,
                   svn_wc_conflict_reason_t reason)
{
  const char *src_repos_url;  /* root URL of source repository */
  const char *left_url;
  const char *right_url;
  svn_wc_conflict_version_t *left;
  svn_wc_conflict_version_t *right;

  SVN_ERR(svn_ra_get_repos_root2(merge_b->ra_session1, &src_repos_url,
                                 merge_b->pool));

  /* Construct the source URLs of the victim. */
  {
    const char *child = svn_path_is_child(merge_b->target,
                                          victim_path, merge_b->pool);
    if (child != NULL)
      {
        left_url = svn_path_url_add_component2(merge_b->merge_source.url1,
                                               child, merge_b->pool);
        right_url = svn_path_url_add_component2(merge_b->merge_source.url2,
                                                child, merge_b->pool);
      }
    else
      {
        left_url = merge_b->merge_source.url1;
        right_url = merge_b->merge_source.url2;
      }
  }

  left = svn_wc_conflict_version_create(
           src_repos_url,
           svn_path_is_child(src_repos_url, left_url, merge_b->pool),
           merge_b->merge_source.rev1, node_kind, merge_b->pool);

  right = svn_wc_conflict_version_create(
            src_repos_url,
            svn_path_is_child(src_repos_url, right_url, merge_b->pool),
            merge_b->merge_source.rev2, node_kind, merge_b->pool);

  *conflict = svn_wc_conflict_description_create_tree(
    victim_path, adm_access, node_kind, svn_wc_operation_merge,
    left, right, merge_b->pool);

  (*conflict)->action = action;
  (*conflict)->reason = reason;

  return SVN_NO_ERROR;
}

/* Record a tree conflict in the WC, unless this is a dry run or a record-
 * only merge.
 *
 * The tree conflict, with its victim specified by VICTIM_PATH, is
 * assumed to have happened during a merge using merge baton MERGE_B.
 *
 * ADM_ACCESS must correspond to the victim's parent directory (even if
 * the victim is a directory).
 *
 * NODE_KIND must be the node kind of "old" and "theirs" and "mine";
 * this function cannot cope with node kind clashes.
 * ACTION and REASON correspond to the fields
 * of the same names in svn_wc_tree_conflict_description_t.
 */
static svn_error_t*
tree_conflict(merge_cmd_baton_t *merge_b,
              svn_wc_adm_access_t *adm_access,
              const char *victim_path,
              svn_node_kind_t node_kind,
              svn_wc_conflict_action_t action,
              svn_wc_conflict_reason_t reason)
{
  svn_wc_conflict_description_t *conflict;

  if (merge_b->record_only || merge_b->dry_run)
    return SVN_NO_ERROR;

  /* Construct the new conflict first to get the proper conflict->path */
  SVN_ERR(make_tree_conflict(&conflict, merge_b, adm_access, victim_path,
                             node_kind, action, reason));

  SVN_ERR(svn_wc__add_tree_conflict(conflict, adm_access, merge_b->pool));
  return SVN_NO_ERROR;
}

/* The same as tree_conflict(), but this one is called from
   merge_*_added() and possibly collapses a new tree-conflict
   with an existing one. */
static svn_error_t*
tree_conflict_on_add(merge_cmd_baton_t *merge_b,
                     svn_wc_adm_access_t *adm_access,
                     const char *victim_path,
                     svn_node_kind_t node_kind,
                     svn_wc_conflict_action_t action,
                     svn_wc_conflict_reason_t reason)
{
  svn_wc_conflict_description_t *existing_conflict;
  svn_wc_conflict_description_t *conflict;

  if (merge_b->record_only || merge_b->dry_run)
    return SVN_NO_ERROR;

  /* Construct the new conflict first to get the proper conflict->path,
     and also to compare the new conflict with a possibly existing one. */
  SVN_ERR(make_tree_conflict(&conflict, merge_b, adm_access, victim_path,
                             node_kind, action, reason));

  SVN_ERR(svn_wc__get_tree_conflict(&existing_conflict, conflict->path,
                                    adm_access, merge_b->pool));

  if (existing_conflict == NULL)
    {
      /* There is no existing tree conflict so it is safe to add one. */
      SVN_ERR(svn_wc__add_tree_conflict(conflict, adm_access, merge_b->pool));
    }
  else if (existing_conflict->action == svn_wc_conflict_action_delete &&
           conflict->action == svn_wc_conflict_action_add)
    {
      /* There is already a tree conflict raised by a previous incoming
       * change that attempted to delete the item (whether in this same
       * merge operation or not). Change the existing conflict to note
       * that the incoming change is replacement. */

      /* Remove the existing tree-conflict so we can add a new one.*/
      SVN_ERR(svn_wc__del_tree_conflict(conflict->path,
                                        adm_access,
                                        merge_b->pool));

      /* Preserve the reason which caused the first conflict,
       * re-label the incoming change as 'delete', and update
       * version info for the left version of the conflict. */
      conflict->reason = existing_conflict->reason;
      conflict->action = svn_wc_conflict_action_delete;
      conflict->src_left_version = svn_wc_conflict_version_dup(
                                     existing_conflict->src_left_version,
                                     merge_b->pool);

      SVN_ERR(svn_wc__add_tree_conflict(conflict, adm_access, merge_b->pool));
    }

  /* In any other cases, we don't touch the existing conflict. */
  return SVN_NO_ERROR;
}

/* Set *HONOR_MERGEINFO and *RECORD_MERGEINFO (if non-NULL) based on the
   merge being performed as described in MERGE_B.

   If the merge source server is is capable of merge tracking, the left-side
   merge source is an ancestor of the right-side (or vice-versa), the merge
   source repository is the same repository as the MERGE_B->target, and
   ancestry is being considered then set *HONOR_MERGEINFO to true, otherwise
   set it to false.

   If *HONOR_MERGEINFO is set to TRUE and the merge is not a dry run then set
   *RECORD_MERGEINFO to true, otherwise set it to false.
   **/
static APR_INLINE void
mergeinfo_behavior(svn_boolean_t *honor_mergeinfo_p,
                   svn_boolean_t *record_mergeinfo_p,
                   merge_cmd_baton_t *merge_b)
{
  svn_boolean_t honor_mergeinfo = (merge_b->mergeinfo_capable
                                   && merge_b->sources_ancestral
                                   && merge_b->same_repos
                                   && (! merge_b->ignore_ancestry));

  if (honor_mergeinfo_p)
    *honor_mergeinfo_p = honor_mergeinfo;

  if (record_mergeinfo_p)
    *record_mergeinfo_p = (honor_mergeinfo && (! merge_b->dry_run));
}


/* Helper for filter_self_referential_mergeinfo()

   *MERGEINFO is a non-empty, non-null collection of mergeinfo.

   Remove all mergeinfo from *MERGEINFO that describes revision ranges
   greater than REVISION.  Put a copy of any removed mergeinfo, allocated
   in POOL, into *YOUNGER_MERGEINFO.

   If no mergeinfo is removed from *MERGEINFO then *YOUNGER_MERGEINFO is set
   to NULL.  If all mergeinfo is removed from *MERGEINFO then *MERGEINFO is
   set to NULL.
   */
static svn_error_t*
split_mergeinfo_on_revision(svn_mergeinfo_t *younger_mergeinfo,
                            svn_mergeinfo_t *mergeinfo,
                            svn_revnum_t revision,
                            apr_pool_t *pool)
{
  apr_hash_index_t *hi;

  *younger_mergeinfo = NULL;
  for (hi = apr_hash_first(NULL, *mergeinfo); hi; hi = apr_hash_next(hi))
    {
      int i;
      const void *key;
      void *value;
      apr_array_header_t *rangelist;
      const char *merge_source_path;

      apr_hash_this(hi, &key, NULL, &value);
      rangelist = value;
      merge_source_path = key;

      for (i = 0; i < rangelist->nelts; i++)
        {
          svn_merge_range_t *range =
            APR_ARRAY_IDX(rangelist, i, svn_merge_range_t *);
          if (range->end <= revision)
            {
              /* This entirely of this range is as old or older than
                 REVISION, so leave it in *MERGEINFO. */
              continue;
            }
          else
            {
              /* Since the rangelists in svn_mergeinfo_t's are sorted in
                 increasing order we know that part or all of *this* range
                 and *all* of the remaining ranges in *RANGELIST are younger
                 than REVISION.  Remove the younger rangelists from
                 *MERGEINFO and put them in *YOUNGER_MERGEINFO. */
              int j;
              apr_array_header_t *younger_rangelist =
                apr_array_make(pool, 1, sizeof(svn_merge_range_t *));

              for (j = i; j < rangelist->nelts; j++)
                {
                  svn_merge_range_t *younger_range = svn_merge_range_dup(
                    APR_ARRAY_IDX(rangelist, j, svn_merge_range_t *), pool);

                  /* REVISION might intersect with the first range where
                     range->end > REVISION.  If that is the case then split
                     the current range into two, putting the younger half
                     into *YOUNGER_MERGEINFO and leaving the older half in
                     *MERGEINFO. */
                  if (j == i && range->start + 1 <= revision)
                    younger_range->start = range->end = revision;

                  APR_ARRAY_PUSH(younger_rangelist, svn_merge_range_t *) =
                    younger_range;
                }

              /* So far we've only been manipulating rangelists, now we
                 actually create *YOUNGER_MERGEINFO and then remove the older
                 ranges from *MERGEINFO */
              if (!(*younger_mergeinfo))
                *younger_mergeinfo = apr_hash_make(pool);
              apr_hash_set(*younger_mergeinfo,
                           (const char *)merge_source_path,
                           APR_HASH_KEY_STRING, younger_rangelist);
              SVN_ERR(svn_mergeinfo_remove(mergeinfo, *younger_mergeinfo,
                                           *mergeinfo, pool));
              break; /* ...out of for (i = 0; i < rangelist->nelts; i++) */
            }
        }
    }
  return SVN_NO_ERROR;
}


/* Helper for merge_props_changed().

   *PROPS is an array of svn_prop_t structures representing regular properties
   to be added to the working copy PATH.  ADM_ACCESS and MERGE_B are cascaded
   from the arguments of the same name in merge_props_changed().

   If mergeinfo is not being honored, MERGE_B->SAME_REPOS is true, and
   MERGE_B->REINTEGRATE_MERGE is FALSE do nothing.  Otherwise, if
   MERGE_B->SAME_REPOS is false, then filter out all mergeinfo
   property additions (Issue #3383) from *PROPS.  If MERGE_B->SAME_REPOS is
   true then filter out mergeinfo property additions to PATH when those
   additions refer to the same line of history as PATH as described below.  

   If mergeinfo is being honored and MERGE_B->SAME_REPOS is true
   then examine the added mergeinfo, looking at each range (or single rev)
   of each source path.  If a source_path/range refers to the same line of
   history as PATH (pegged at its base revision), then filter out that range.
   If the entire rangelist for a given path is filtered then filter out the
   path as well.

   If any filtering occurs, set outgoing *PROPS to a shallow copy (allocated
   in POOL) of incoming *PROPS minus the filtered mergeinfo. */
static svn_error_t*
filter_self_referential_mergeinfo(apr_array_header_t **props,
                                  const char *path,
                                  merge_cmd_baton_t *merge_b,
                                  svn_wc_adm_access_t *adm_access,
                                  apr_pool_t *pool)
{
  svn_boolean_t honor_mergeinfo;
  apr_array_header_t *adjusted_props;
  int i;
  const svn_wc_entry_t *target_entry;

  /* If we aren't honoring mergeinfo and this is a merge from the
     same repository, then get outta here.  If this is a reintegrate
     merge or a merge from a foreign repository we still need to
     filter regardless of whether we are honoring mergeinfo or not. */
  mergeinfo_behavior(&honor_mergeinfo, NULL, merge_b);
  if (! honor_mergeinfo
      && merge_b->same_repos
      && ! merge_b->reintegrate_merge)
    return SVN_NO_ERROR;

  /* If this is a merge from the same repository and PATH itself is
     newly added or replaced there is no need to filter. */
  SVN_ERR(svn_wc__entry_versioned(&target_entry, path, adm_access,
                                  FALSE, pool));
  if (merge_b->same_repos
      && (target_entry->schedule == svn_wc_schedule_add
          || target_entry->schedule == svn_wc_schedule_replace))
    return SVN_NO_ERROR;

  adjusted_props = apr_array_make(pool, (*props)->nelts, sizeof(svn_prop_t));
  for (i = 0; i < (*props)->nelts; ++i)
    {
      svn_prop_t *prop = &APR_ARRAY_IDX((*props), i, svn_prop_t);

      /* If this is a merge from a foreign repository we must strip all
         incoming mergeinfo (including mergeinfo deletions).  Otherwise if
         this property isn't mergeinfo or is NULL valued (i.e. prop removal)
         or empty mergeinfo it does not require any special handling.  There
         is nothing to filter out of empty mergeinfo and the concept of
         filtering doesn't apply if we are trying to remove mergeinfo
         entirely. */
      if ((strcmp(prop->name, SVN_PROP_MERGEINFO) == 0)
          && (! merge_b->same_repos))
        {
          /* Issue #3383: We don't want mergeinfo from a foreign repos. */
          continue;
        }
      else if ((strcmp(prop->name, SVN_PROP_MERGEINFO) != 0)
          || (! prop->value)       /* Removal of mergeinfo */
          || (! prop->value->len)) /* Empty mergeinfo */
        {
          APR_ARRAY_PUSH(adjusted_props, svn_prop_t) = *prop;
        }
      else /* Non-empty mergeinfo; filter self-referential mergeinfo out. */
        {
          svn_mergeinfo_t mergeinfo, younger_mergeinfo;
          svn_mergeinfo_t filtered_mergeinfo = NULL;
          svn_mergeinfo_t filtered_younger_mergeinfo = NULL;
          const char *target_url;
          const char *old_url = NULL;

          /* Temporarily reparent our RA session to the merge
             target's URL. */
          SVN_ERR(svn_client_url_from_path(&target_url, path, pool));
          SVN_ERR(svn_client__ensure_ra_session_url(&old_url,
                                                    merge_b->ra_session2,
                                                    target_url, pool));

          /* Parse the incoming mergeinfo to allow easier manipulation. */
          SVN_ERR(svn_mergeinfo_parse(&mergeinfo, prop->value->data, pool));

          /* The working copy target PATH is at base revision
             target_entry->revision.  Divide the incoming mergeinfo into two
             groups.  One where all revision ranges are as old or older than
             target_entry->revision and one where all revision ranges are
             younger.

             Note: You may be wondering why we do this.

             For the incoming mergeinfo "older" than target's base revision we
             can filter out self-referential mergeinfo efficiently using
             svn_client__get_history_as_mergeinfo().  We simply look at PATH's
             natural history as mergeinfo and remove that from any incoming
             mergeinfo.

             For mergeinfo "younger" than the base revision we can't use
             svn_ra_get_location_segments() to look into PATH's future
             history.  Instead we must use svn_client__repos_locations() and
             look at each incoming source/range individually and see if PATH
             at its base revision and PATH at the start of the incoming range
             exist on the same line of history.  If they do then we can filter
             out the incoming range.  But since we have to do this for each
             range there is a substantial performance penalty to pay if the
             incoming ranges are not contiguous, i.e. we call
             svn_client__repos_locations for each discrete range and incur
             the cost of a roundtrip communication with the repository. */
          SVN_ERR(split_mergeinfo_on_revision(&younger_mergeinfo,
                                              &mergeinfo,
                                              target_entry->revision,
                                              pool));

          /* Filter self-referential mergeinfo from younger_mergeinfo. */
          if (younger_mergeinfo)
            {
              apr_hash_index_t *hi;
              const char *merge_source_root_url;

              SVN_ERR(svn_ra_get_repos_root2(merge_b->ra_session2,
                                             &merge_source_root_url, pool));

              for (hi = apr_hash_first(NULL, younger_mergeinfo);
                   hi; hi = apr_hash_next(hi))
                {
                  int j;
                  const void *key;
                  void *value;
                  const char *source_path;
                  apr_array_header_t *rangelist;
                  const char *merge_source_url;
                  apr_array_header_t *adjusted_rangelist =
                    apr_array_make(pool, 0, sizeof(svn_merge_range_t *));

                  apr_hash_this(hi, &key, NULL, &value);
                  source_path = key;
                  rangelist = value;
                  merge_source_url =
                        svn_path_url_add_component2(merge_source_root_url,
                                                    source_path + 1, pool);

                  for (j = 0; j < rangelist->nelts; j++)
                    {
                      svn_error_t *err;
                      svn_opt_revision_t *start_revision;
                      const char *start_url;
                      svn_opt_revision_t peg_rev, rev1_opt, rev2_opt;
                      svn_merge_range_t *range =
                        APR_ARRAY_IDX(rangelist, j, svn_merge_range_t *);

                      peg_rev.kind = svn_opt_revision_number;
                      peg_rev.value.number = target_entry->revision;
                      rev1_opt.kind = svn_opt_revision_number;
                      /* SVN_PROP_MERGEINFO only stores forward merges, so
                         the start range of svn_merge_range_t RANGE is not
                         inclusive. */
                      rev1_opt.value.number = range->start + 1;

                      /* Because the merge source normalization code
                         ensures mergeinfo refers to real locations on
                         the same line of history, there's no need to
                         look at the whole range, just the start. */
                       rev2_opt.kind = svn_opt_revision_unspecified;

                      /* Check if PATH@TARGET_ENTRY->REVISION exists at
                         RANGE->START on the same line of history. */
                      err = svn_client__repos_locations(&start_url,
                                                        &start_revision,
                                                        NULL,
                                                        NULL,
                                                        merge_b->ra_session2,
                                                        target_url,
                                                        &peg_rev,
                                                        &rev1_opt,
                                                        &rev2_opt,
                                                        merge_b->ctx,
                                                        pool);
                      if (err)
                        {
                          if (err->apr_err == SVN_ERR_CLIENT_UNRELATED_RESOURCES
                              || err->apr_err == SVN_ERR_FS_NOT_FOUND
                              || err->apr_err == SVN_ERR_FS_NO_SUCH_REVISION)
                            {
                              /* PATH@TARGET_ENTRY->REVISION didn't exist at
                                 RANGE->START + 1 or is unrelated to the
                                 resource PATH@RANGE->START.  Some of the
                                 requested revisions may not even exist in
                                 the repository; a real possibility since
                                 mergeinfo is hand editable.  In all of these
                                 cases clear and ignore the error and don't
                                 do any filtering.

                                 Note: In this last case it is possible that
                                 we will allow self-referential mergeinfo to
                                 be applied, but fixing it here is potentially
                                 very costly in terms of finding what part of
                                 a range is actually valid.  Simply allowing
                                 the merge to proceed without filtering the
                                 offending range seems the least worst
                                 option. */
                              svn_error_clear(err);
                              err = NULL;
                              APR_ARRAY_PUSH(adjusted_rangelist,
                                             svn_merge_range_t *) = range;
                            }
                          else
                            {
                              return err;
                            }
                         }
                      else
                        {
                          /* PATH@TARGET_ENTRY->REVISION exists on the same
                             line of history at RANGE->START and RANGE->END.
                             Now check that PATH@TARGET_ENTRY->REVISION's path
                             names at RANGE->START and RANGE->END are the same.
                             If the names are not the same then the mergeinfo
                             describing PATH@RANGE->START through
                             PATH@RANGE->END actually belong to some other
                             line of history and we want to record this
                             mergeinfo, not filter it. */
                          if (strcmp(start_url, merge_source_url) != 0)
                            {
                              APR_ARRAY_PUSH(adjusted_rangelist,
                                             svn_merge_range_t *) = range;
                            }
                        }
                        /* else no need to add, this mergeinfo is
                           all on the same line of history. */
                    } /* for (j = 0; j < rangelist->nelts; j++) */

                  /* Add any rangelists for source_path that are not
                     self-referential. */
                  if (adjusted_rangelist->nelts)
                    {
                      if (!filtered_younger_mergeinfo)
                        filtered_younger_mergeinfo = apr_hash_make(pool);
                      apr_hash_set(filtered_younger_mergeinfo, source_path,
                                   APR_HASH_KEY_STRING, adjusted_rangelist);
                    }

                } /* Iteration over each merge source in younger_mergeinfo. */
            } /* if (apr_hash_count(younger_mergeinfo)) */

          /* Filter self-referential mergeinfo from "older" mergeinfo. */
          if (mergeinfo)
            {
              svn_mergeinfo_t implicit_mergeinfo;
              svn_opt_revision_t peg_rev;

              peg_rev.kind = svn_opt_revision_number;
              peg_rev.value.number = target_entry->revision;
              SVN_ERR(svn_client__get_history_as_mergeinfo(
                &implicit_mergeinfo,
                path, &peg_rev,
                target_entry->revision,
                SVN_INVALID_REVNUM,
                merge_b->ra_session2,
                adm_access,
                merge_b->ctx,
                pool));

              /* Remove PATH's implicit mergeinfo from the incoming mergeinfo. */
              SVN_ERR(svn_mergeinfo_remove(&filtered_mergeinfo,
                                           implicit_mergeinfo,
                                           mergeinfo, pool));
            }

          /* If we reparented MERGE_B->RA_SESSION2 above, put it back
             to the original URL. */
          if (old_url)
            SVN_ERR(svn_ra_reparent(merge_b->ra_session2, old_url, pool));

          /* Combine whatever older and younger filtered mergeinfo exists
             into filtered_mergeinfo. */
          if (filtered_mergeinfo && filtered_younger_mergeinfo)
            SVN_ERR(svn_mergeinfo_merge(filtered_mergeinfo,
                                        filtered_younger_mergeinfo, pool));
          else if (filtered_younger_mergeinfo)
            filtered_mergeinfo = filtered_younger_mergeinfo;

          /* If there is any incoming mergeinfo remaining after filtering
             then put it in adjusted_props. */
          if (filtered_mergeinfo && apr_hash_count(filtered_mergeinfo))
            {
              /* Convert filtered_mergeinfo to a svn_prop_t and put it
                 back in the array. */
              svn_string_t *filtered_mergeinfo_str;
              svn_prop_t *adjusted_prop = apr_pcalloc(pool,
                                                      sizeof(*adjusted_prop));
              SVN_ERR(svn_mergeinfo_to_string(&filtered_mergeinfo_str,
                                              filtered_mergeinfo,
                                              pool));
              adjusted_prop->name = SVN_PROP_MERGEINFO;
              adjusted_prop->value = filtered_mergeinfo_str;
              APR_ARRAY_PUSH(adjusted_props, svn_prop_t) = *adjusted_prop;
            }
        }
    }
  *props = adjusted_props;
  return SVN_NO_ERROR;
}

/* An svn_wc_diff_callbacks3_t function.  Used for both file and directory
   property merges. */
static svn_error_t *
merge_props_changed(svn_wc_adm_access_t *adm_access,
                    svn_wc_notify_state_t *state,
                    svn_boolean_t *tree_conflicted,
                    const char *path,
                    const apr_array_header_t *propchanges,
                    apr_hash_t *original_props,
                    void *baton)
{
  apr_array_header_t *props;
  merge_cmd_baton_t *merge_b = baton;
  svn_client_ctx_t *ctx = merge_b->ctx;
  apr_pool_t *subpool = svn_pool_create(merge_b->pool);
  svn_error_t *err;

  if (tree_conflicted)
    *tree_conflicted = FALSE;

  /* Check for an obstructed or missing node on disk. */
  {
    svn_wc_notify_state_t obstr_state;

    obstr_state = obstructed_or_missing(path, adm_access, merge_b, subpool);
    if (obstr_state != svn_wc_notify_state_inapplicable)
      {
        if (state)
          *state = obstr_state;
        svn_pool_destroy(subpool);
        return SVN_NO_ERROR;
      }
  }

  /* ### TODO check tree-conflicts! */

  SVN_ERR(svn_categorize_props(propchanges, NULL, NULL, &props, subpool));

  /* We only want to merge "regular" version properties:  by
     definition, 'svn merge' shouldn't touch any data within .svn/  */
  if (props->nelts)
    {
      int i;

      /* svn_wc_merge_props2() requires ADM_ACCESS to be the access for
         the parent of PATH. Since the advent of merge tracking,
         do_directory_merge() may call this (indirectly) with
         the access for the merge_b->target instead (issue #2781).
         So, if we have the wrong access, get the right one. */
      if (svn_path_compare_paths(svn_wc_adm_access_path(adm_access),
                                 path) != 0)
        SVN_ERR(svn_wc_adm_probe_try3(&adm_access, adm_access, path,
                                      TRUE, -1, ctx->cancel_func,
                                      ctx->cancel_baton, subpool));

      /* If this is a forward merge then don't add new mergeinfo to
         PATH that is already part of PATH's own history, see
         http://svn.haxx.se/dev/archive-2008-09/0006.shtml.  If the
         merge sources are not ancestral then there is no concept of a
         'forward' or 'reverse' merge and we filter unconditionally. */
      if (merge_b->merge_source.rev1 < merge_b->merge_source.rev2
          || !merge_b->sources_ancestral)
        SVN_ERR(filter_self_referential_mergeinfo(&props, path, merge_b,
                                                  adm_access, subpool));

      err = svn_wc_merge_props2(state, path, adm_access, original_props, props,
                                FALSE, merge_b->dry_run, ctx->conflict_func,
                                ctx->conflict_baton, subpool);

      /* If this is not a dry run then make a record in BATON if we find a
         PATH where mergeinfo is added where none existed previously or PATH
         is having its existing mergeinfo deleted. */
      if (!merge_b->dry_run)
        {
          for (i = 0; i < props->nelts; ++i)
            {
              svn_prop_t *prop = &APR_ARRAY_IDX(props, i, svn_prop_t);

              if (strcmp(prop->name, SVN_PROP_MERGEINFO) == 0)
                {
                  /* Does PATH have any working mergeinfo? */
                  svn_string_t *mergeinfo_prop =
                    apr_hash_get(original_props,
                                 SVN_PROP_MERGEINFO,
                                 APR_HASH_KEY_STRING);

                  if (!mergeinfo_prop && prop->value)
                    {
                      /* If BATON->PATHS_WITH_NEW_MERGEINFO needs to be
                         allocated do so in BATON->POOL so it has a
                         sufficient lifetime. */
                      if (!merge_b->paths_with_new_mergeinfo)
                        merge_b->paths_with_new_mergeinfo =
                          apr_hash_make(merge_b->pool);

                      apr_hash_set(merge_b->paths_with_new_mergeinfo,
                                   apr_pstrdup(merge_b->pool, path),
                                   APR_HASH_KEY_STRING, path);
                    }
                  else if (mergeinfo_prop && !prop->value)
                    {
                      /* If BATON->PATHS_WITH_DELETED_MERGEINFO needs to be
                         allocated do so in BATON->POOL so it has a
                         sufficient lifetime. */
                      if (!merge_b->paths_with_deleted_mergeinfo)
                        merge_b->paths_with_deleted_mergeinfo =
                          apr_hash_make(merge_b->pool);

                      apr_hash_set(merge_b->paths_with_deleted_mergeinfo,
                                   apr_pstrdup(merge_b->pool, path),
                                   APR_HASH_KEY_STRING, path);
                    }
                }
            }
        }

      if (err && (err->apr_err == SVN_ERR_ENTRY_NOT_FOUND
                  || err->apr_err == SVN_ERR_UNVERSIONED_RESOURCE))
        {
          /* If the entry doesn't exist in the wc, this is a tree-conflict. */
          if (state)
            *state = svn_wc_notify_state_missing;
          if (tree_conflicted)
            *tree_conflicted = TRUE;
          svn_error_clear(err);
          svn_pool_destroy(subpool);
          return SVN_NO_ERROR;
        }
      else if (err)
        return err;
    }

  svn_pool_destroy(subpool);
  return SVN_NO_ERROR;
}

/* Contains any state collected while resolving conflicts. */
typedef struct
{
  /* The wrapped callback and baton. */
  svn_wc_conflict_resolver_func_t wrapped_func;
  void *wrapped_baton;

  /* The list of any paths which remained in conflict after a
     resolution attempt was made. */
  apr_hash_t **conflicted_paths;

  /* Pool used in notification_receiver() to avoid the iteration
     sub-pool which is passed in, then subsequently destroyed. */
  apr_pool_t *pool;
} conflict_resolver_baton_t;

/* An implementation of the svn_wc_conflict_resolver_func_t interface.
   We keep a record of paths which remain in conflict after any
   resolution attempt from BATON->wrapped_func. */
static svn_error_t *
conflict_resolver(svn_wc_conflict_result_t **result,
                  const svn_wc_conflict_description_t *description,
                  void *baton, apr_pool_t *pool)
{
  svn_error_t *err;
  conflict_resolver_baton_t *conflict_b = baton;

  if (conflict_b->wrapped_func)
    err = (*conflict_b->wrapped_func)(result, description,
                                      conflict_b->wrapped_baton, pool);
  else
    {
      /* If we have no wrapped callback to invoke, then we still need
         to behave like a proper conflict-callback ourselves.  */
      *result = svn_wc_create_conflict_result(svn_wc_conflict_choose_postpone,
                                              NULL, pool);
      err = SVN_NO_ERROR;
    }

  /* Keep a record of paths still in conflict after the resolution attempt. */
  if ((! conflict_b->wrapped_func)
      || (*result && ((*result)->choice == svn_wc_conflict_choose_postpone)))
    {
      const char *conflicted_path = apr_pstrdup(conflict_b->pool,
                                                description->path);

      if (*conflict_b->conflicted_paths == NULL)
        *conflict_b->conflicted_paths = apr_hash_make(conflict_b->pool);

      apr_hash_set(*conflict_b->conflicted_paths, conflicted_path,
                   APR_HASH_KEY_STRING, conflicted_path);
    }

  return err;
}

/* An svn_wc_diff_callbacks3_t function. */
static svn_error_t *
merge_file_changed(svn_wc_adm_access_t *adm_access,
                   svn_wc_notify_state_t *content_state,
                   svn_wc_notify_state_t *prop_state,
                   svn_boolean_t *tree_conflicted,
                   const char *mine,
                   const char *older,
                   const char *yours,
                   svn_revnum_t older_rev,
                   svn_revnum_t yours_rev,
                   const char *mimetype1,
                   const char *mimetype2,
                   const apr_array_header_t *prop_changes,
                   apr_hash_t *original_props,
                   void *baton)
{
  merge_cmd_baton_t *merge_b = baton;
  apr_pool_t *subpool = svn_pool_create(merge_b->pool);
  svn_boolean_t merge_required = TRUE;
  enum svn_wc_merge_outcome_t merge_outcome;

  if (tree_conflicted)
    *tree_conflicted = FALSE;

  /* Easy out:  no access baton means there ain't no merge target */
  if (adm_access == NULL)
    {
      if (content_state)
        *content_state = svn_wc_notify_state_missing;
      if (prop_state)
        *prop_state = svn_wc_notify_state_missing;
      /* Trying to change a file at a non-existing path.
       * Although this is a tree-conflict, it will already have been
       * raised by the merge_dir_opened() callback. Not raising additional tree
       * conflicts for the child nodes inside. */
      svn_pool_destroy(subpool);
      return SVN_NO_ERROR;
    }

  /* Check for an obstructed or missing node on disk. */
  {
    svn_wc_notify_state_t obstr_state;

    obstr_state = obstructed_or_missing(mine, adm_access, merge_b, subpool);
    if (obstr_state != svn_wc_notify_state_inapplicable)
      {
        if (content_state)
          *content_state = obstr_state;
        svn_pool_destroy(subpool);
        return SVN_NO_ERROR;
      }
  }

  /* Other easy outs:  if the merge target isn't under version
     control, or is just missing from disk, fogettaboutit.  There's no
     way svn_wc_merge3() can do the merge. */
  {
    const svn_wc_entry_t *entry;
    svn_node_kind_t kind;

    SVN_ERR(svn_wc_entry(&entry, mine, adm_access, FALSE, subpool));
    SVN_ERR(svn_io_check_path(mine, &kind, subpool));

    /* ### a future thought:  if the file is under version control,
       but the working file is missing, maybe we can 'restore' the
       working file from the text-base, and then allow the merge to run?  */

    if ((! entry) || (kind != svn_node_file))
      {
        /* This is use case 4 described in the paper attached to issue
         * #2282.  See also notes/tree-conflicts/detection.txt
         */
        SVN_ERR(tree_conflict(merge_b, adm_access, mine,
                              svn_node_file,
                              svn_wc_conflict_action_edit,
                              svn_wc_conflict_reason_missing));
        if (tree_conflicted)
          *tree_conflicted = TRUE;
        if (content_state)
          *content_state = svn_wc_notify_state_missing;
        if (prop_state)
          *prop_state = svn_wc_notify_state_missing;
        svn_pool_destroy(subpool);
        return SVN_NO_ERROR;
      }
  }

  /* ### TODO: Thwart attempts to merge into a path that has
     ### unresolved conflicts.  This needs to be smart enough to deal
     ### with tree conflicts!
  if (is_path_conflicted_by_merge(merge_b, mine))
    {
      *content_state = svn_wc_notify_state_conflicted;
      return svn_error_createf(SVN_ERR_WC_FOUND_CONFLICT, NULL,
                               _("Path '%s' is in conflict, and must be "
                                 "resolved before the remainder of the "
                                 "requested merge can be applied"), mine);
    }
  */

  /* This callback is essentially no more than a wrapper around
     svn_wc_merge3().  Thank goodness that all the
     diff-editor-mechanisms are doing the hard work of getting the
     fulltexts! */

  /* Do property merge before text merge so that keyword expansion takes
     into account the new property values. */
  if (prop_changes->nelts > 0)
    {
      svn_boolean_t tree_conflicted2;

      SVN_ERR(merge_props_changed(adm_access, prop_state, &tree_conflicted2,
                                  mine, prop_changes, original_props, baton));

      /* If the prop change caused a tree-conflict, just bail. */
      if (tree_conflicted2)
        {
          if (tree_conflicted != NULL)
            *tree_conflicted = TRUE;

          svn_pool_destroy(subpool);
          return SVN_NO_ERROR;
        }
    }
  else
    if (prop_state)
      *prop_state = svn_wc_notify_state_unchanged;

  if (older)
    {
      svn_boolean_t has_local_mods;
      SVN_ERR(svn_wc_text_modified_p(&has_local_mods, mine, FALSE,
                                     adm_access, subpool));

      /* Special case:  if a binary file's working file is
         exactly identical to the 'left' side of the merge, then don't
         allow svn_wc_merge to produce a conflict.  Instead, just
         overwrite the working file with the 'right' side of the
         merge.  Why'd we check for local mods above?  Because we want
         to do a different notification depending on whether or not
         the file was locally modified.

         Alternately, if the 'left' side of the merge doesn't exist in
         the repository, and the 'right' side of the merge is
         identical to the WC, pretend we did the merge (a no-op). */
      if ((mimetype1 && svn_mime_type_is_binary(mimetype1))
          || (mimetype2 && svn_mime_type_is_binary(mimetype2)))
        {
          /* For adds, the 'left' side of the merge doesn't exist. */
          svn_boolean_t older_revision_exists =
              !merge_b->add_necessitated_merge;
          svn_boolean_t same_contents;
          SVN_ERR(svn_io_files_contents_same_p(&same_contents,
                                               (older_revision_exists ?
                                                older : yours),
                                               mine, subpool));
          if (same_contents)
            {
              if (older_revision_exists && !merge_b->dry_run)
                SVN_ERR(svn_io_file_rename(yours, mine, subpool));
              merge_outcome = svn_wc_merge_merged;
              merge_required = FALSE;
            }
        }

      if (merge_required)
        {
          /* xgettext: the '.working', '.merge-left.r%ld' and
             '.merge-right.r%ld' strings are used to tag onto a file
             name in case of a merge conflict */
          const char *target_label = _(".working");
          const char *left_label = apr_psprintf(subpool,
                                                _(".merge-left.r%ld"),
                                                older_rev);
          const char *right_label = apr_psprintf(subpool,
                                                 _(".merge-right.r%ld"),
                                                 yours_rev);
          conflict_resolver_baton_t conflict_baton = { 0 };

          conflict_baton.wrapped_func = merge_b->ctx->conflict_func;
          conflict_baton.wrapped_baton = merge_b->ctx->conflict_baton;
          conflict_baton.conflicted_paths = &merge_b->conflicted_paths;
          conflict_baton.pool = merge_b->pool;

          SVN_ERR(svn_wc_merge3(&merge_outcome,
                                older, yours, mine, adm_access,
                                left_label, right_label, target_label,
                                merge_b->dry_run, merge_b->diff3_cmd,
                                merge_b->merge_options, prop_changes,
                                conflict_resolver, &conflict_baton,
                                subpool));
        }

      if (content_state)
        {
          if (merge_outcome == svn_wc_merge_conflict)
            *content_state = svn_wc_notify_state_conflicted;
          else if (has_local_mods
                   && merge_outcome != svn_wc_merge_unchanged)
            *content_state = svn_wc_notify_state_merged;
          else if (merge_outcome == svn_wc_merge_merged)
            *content_state = svn_wc_notify_state_changed;
          else if (merge_outcome == svn_wc_merge_no_merge)
            *content_state = svn_wc_notify_state_missing;
          else /* merge_outcome == svn_wc_merge_unchanged */
            *content_state = svn_wc_notify_state_unchanged;
        }
    }

  svn_pool_destroy(subpool);
  return SVN_NO_ERROR;
}

/* An svn_wc_diff_callbacks3_t function. */
static svn_error_t *
merge_file_added(svn_wc_adm_access_t *adm_access,
                 svn_wc_notify_state_t *content_state,
                 svn_wc_notify_state_t *prop_state,
                 svn_boolean_t *tree_conflicted,
                 const char *mine,
                 const char *older,
                 const char *yours,
                 svn_revnum_t rev1,
                 svn_revnum_t rev2,
                 const char *mimetype1,
                 const char *mimetype2,
                 const apr_array_header_t *prop_changes,
                 apr_hash_t *original_props,
                 void *baton)
{
  merge_cmd_baton_t *merge_b = baton;
  apr_pool_t *subpool = svn_pool_create(merge_b->pool);
  svn_node_kind_t kind;
  int i;
  apr_hash_t *file_props;

  /* In most cases, we just leave prop_state as unknown, and let the
     content_state what happened, so we set prop_state here to avoid that
     below. */
  if (prop_state)
    *prop_state = svn_wc_notify_state_unknown;

  if (tree_conflicted)
    *tree_conflicted = FALSE;

  /* Apply the prop changes to a new hash table. */
  file_props = apr_hash_copy(subpool, original_props);
  for (i = 0; i < prop_changes->nelts; ++i)
    {
      const svn_prop_t *prop = &APR_ARRAY_IDX(prop_changes, i, svn_prop_t);

      /* We don't want any DAV wcprops related to this file because
         they'll point to the wrong repository (in the
         merge-from-foreign-repository scenario) or wrong place in the
         right repository (in the same-repos scenario).  So we'll
         strip them.  (Is this a layering violation?)  */
      if (svn_property_kind(NULL, prop->name) == svn_prop_wc_kind)
        continue;

      /* And in the foreign repository merge case, we only want
         regular properties. */
      if ((! merge_b->same_repos)
          && (svn_property_kind(NULL, prop->name) != svn_prop_regular_kind))
        continue;

      /* Issue #3383: We don't want mergeinfo from a foreign repository. */
      if ((! merge_b->same_repos)
          && strcmp(prop->name, SVN_PROP_MERGEINFO) == 0)
        continue;

      apr_hash_set(file_props, prop->name, APR_HASH_KEY_STRING, prop->value);
    }

  /* Easy out:  if we have no adm_access for the parent directory,
     then this portion of the tree-delta "patch" must be inapplicable.
     Send a 'missing' state back;  the repos-diff editor should then
     send a 'skip' notification. */
  if (! adm_access)
    {
      if (merge_b->dry_run && merge_b->added_path
          && svn_path_is_child(merge_b->added_path, mine, subpool))
        {
          if (content_state)
            *content_state = svn_wc_notify_state_changed;
          if (prop_state && apr_hash_count(file_props))
            *prop_state = svn_wc_notify_state_changed;
        }
      else
        *content_state = svn_wc_notify_state_missing;
      /* Trying to add a file at a non-existing path.
       * Although this is a tree-conflict, it will already have been
       * raised by the merge_dir_opened() callback. Not raising additional tree
       * conflicts for the child nodes inside. */
      svn_pool_destroy(subpool);
      return SVN_NO_ERROR;
    }

  /* Check for an obstructed or missing node on disk. */
  {
    svn_wc_notify_state_t obstr_state;

    obstr_state = obstructed_or_missing(mine, adm_access, merge_b, subpool);
    if (obstr_state != svn_wc_notify_state_inapplicable)
      {
        if (content_state)
          *content_state = obstr_state;
        svn_pool_destroy(subpool);
        return SVN_NO_ERROR;
      }
  }

  SVN_ERR(svn_io_check_path(mine, &kind, subpool));
  switch (kind)
    {
    case svn_node_none:
      {
        if (! merge_b->dry_run)
          {
            const char *copyfrom_url;
            svn_revnum_t copyfrom_rev;
            svn_stream_t *new_contents, *new_base_contents;
            apr_hash_t *new_base_props, *new_props;
            svn_wc_conflict_description_t *existing_conflict;

            /* If this is a merge from the same repository as our
               working copy, we handle adds as add-with-history.
               Otherwise, we'll use a pure add. */
            if (merge_b->same_repos)
              {
                const char *child = svn_path_is_child(merge_b->target,
                                                      mine, subpool);
                if (child != NULL)
                  copyfrom_url = svn_path_url_add_component2(
                                               merge_b->merge_source.url2,
                                               child, subpool);
                else
                  copyfrom_url = merge_b->merge_source.url2;
                copyfrom_rev = rev2;
                SVN_ERR(check_scheme_match(adm_access, copyfrom_url));
                new_base_props = file_props;
                new_props = NULL; /* inherit from new_base_props */
                SVN_ERR(svn_stream_open_readonly(&new_base_contents, yours,
                                                 subpool, subpool));
                new_contents = NULL; /* inherit from new_base_contents */
              }
            else
              {
                copyfrom_url = NULL;
                copyfrom_rev = SVN_INVALID_REVNUM;
                new_base_props = apr_hash_make(subpool);
                new_props = file_props;
                new_base_contents = svn_stream_empty(subpool);
                SVN_ERR(svn_stream_open_readonly(&new_contents, yours,
                                                 subpool, subpool));
              }

            SVN_ERR(svn_wc__get_tree_conflict(&existing_conflict,
                                              mine, adm_access,
                                              merge_b->pool));
            if (existing_conflict)
              {
                /* Possibly collapse the existing conflict into a 'replace'
                 * tree conflict. The conflict reason is 'added' because
                 * the now-deleted tree conflict victim must have been
                 * added in the history of the merge target. */
                SVN_ERR(tree_conflict_on_add(merge_b, adm_access, mine,
                                             svn_node_file,
                                             svn_wc_conflict_action_add,
                                             svn_wc_conflict_reason_added));
                if (tree_conflicted)
                  *tree_conflicted = TRUE;
              }
            else
              {
                /* Since 'mine' doesn't exist, and this is
                   'merge_file_added', I hope it's safe to assume that
                   'older' is empty, and 'yours' is the full file.  Merely
                   copying 'yours' to 'mine', isn't enough; we need to get
                   the whole text-base and props installed too, just as if
                   we had called 'svn cp wc wc'. */
                /* ### would be nice to have cancel/notify funcs to pass */
                SVN_ERR(svn_wc_add_repos_file3(
                            mine, adm_access,
                            new_base_contents, new_contents,
                            new_base_props, new_props,
                            copyfrom_url, copyfrom_rev,
                            NULL, NULL, NULL, NULL, subpool));

                /* ### delete 'yours' ? */
              }
          }
        if (content_state)
          *content_state = svn_wc_notify_state_changed;
        if (prop_state && apr_hash_count(file_props))
          *prop_state = svn_wc_notify_state_changed;
      }
      break;
    case svn_node_dir:
      /* The file add the merge wants to carry out is obstructed by
       * a directory, so the file the merge wants to add is a tree
       * conflict victim.
       * See notes about obstructions in notes/tree-conflicts/detection.txt.
       */
      SVN_ERR(tree_conflict_on_add(merge_b, adm_access, mine,
                                   svn_node_file,
                                   svn_wc_conflict_action_add,
                                   svn_wc_conflict_reason_obstructed));
      if (tree_conflicted)
        *tree_conflicted = TRUE;
      if (content_state)
        {
          /* directory already exists, is it under version control? */
          const svn_wc_entry_t *entry;
          SVN_ERR(svn_wc_entry(&entry, mine, adm_access, FALSE, subpool));

          if (entry && dry_run_deleted_p(merge_b, mine))
            *content_state = svn_wc_notify_state_changed;
          else
            /* this will make the repos_editor send a 'skipped' message */
            *content_state = svn_wc_notify_state_obstructed;
        }
      break;
    case svn_node_file:
      {
            if (dry_run_deleted_p(merge_b, mine))
              {
                if (content_state)
                  *content_state = svn_wc_notify_state_changed;
              }
            else
              {
                /* The file add the merge wants to carry out is obstructed by
                 * a versioned file. This file must have been added in the
                 * history of the merge target, hence we flag a tree conflict
                 * with reason 'added'. */
                SVN_ERR(tree_conflict_on_add(
                          merge_b, adm_access, mine, svn_node_file,
                          svn_wc_conflict_action_add,
                          svn_wc_conflict_reason_added));

                if (tree_conflicted)
                  *tree_conflicted = TRUE;
              }
        break;
      }
    default:
      if (content_state)
        *content_state = svn_wc_notify_state_unknown;
      break;
    }

  svn_pool_destroy(subpool);
  return SVN_NO_ERROR;
}

/* Compare the two sets of properties PROPS1 and PROPS2, ignoring the
 * "svn:mergeinfo" property, and noticing only "normal" props. Set *SAME to
 * true if the rest of the properties are identical or false if they differ.
 */
static svn_error_t *
properties_same_p(svn_boolean_t *same,
                  apr_hash_t *props1,
                  apr_hash_t *props2,
                  apr_pool_t *pool)
{
  apr_array_header_t *prop_changes;
  int i, diffs;

  /* Examine the properties that differ */
  SVN_ERR(svn_prop_diffs(&prop_changes, props1, props2, pool));
  diffs = 0;
  for (i = 0; i < prop_changes->nelts; i++)
    {
      const char *pname = APR_ARRAY_IDX(prop_changes, i, svn_prop_t).name;

      /* Count the properties we're interested in; ignore the rest */
      if (svn_wc_is_normal_prop(pname)
          && strcmp(pname, SVN_PROP_MERGEINFO) != 0)
        diffs++;
    }
  *same = (diffs == 0);
  return SVN_NO_ERROR;
}

/* Compare the file OLDER (together with its normal properties in
 * ORIGINAL_PROPS which may also contain WC props and entry props) and MINE
 * (with its properties obtained from its WC admin area ADM_ACCESS). Set
 * *SAME to true if they are the same or false if they differ, ignoring
 * the "svn:mergeinfo" property, and ignoring differences in keyword
 * expansion and end-of-line style. */
static svn_error_t *
files_same_p(svn_boolean_t *same,
             const char *older,
             apr_hash_t *original_props,
             const char *mine,
             svn_wc_adm_access_t *adm_access,
             apr_pool_t *pool)
{
  apr_hash_t *working_props;

  SVN_ERR(svn_wc_prop_list(&working_props, mine, adm_access, pool));

  /* Compare the properties */
  SVN_ERR(properties_same_p(same, original_props, working_props, pool));
  if (*same)
    {
      svn_boolean_t modified;

      /* Compare the file content, translating 'mine' to 'normal' form. */
      SVN_ERR(svn_wc__versioned_file_modcheck(&modified, mine, adm_access,
                                              older, TRUE, pool));
      *same = !modified;
    }

  return SVN_NO_ERROR;
}

/* An svn_wc_diff_callbacks3_t function. */
static svn_error_t *
merge_file_deleted(svn_wc_adm_access_t *adm_access,
                   svn_wc_notify_state_t *state,
                   svn_boolean_t *tree_conflicted,
                   const char *mine,
                   const char *older,
                   const char *yours,
                   const char *mimetype1,
                   const char *mimetype2,
                   apr_hash_t *original_props,
                   void *baton)
{
  merge_cmd_baton_t *merge_b = baton;
  apr_pool_t *subpool = svn_pool_create(merge_b->pool);
  svn_node_kind_t kind;

  if (*tree_conflicted)
    tree_conflicted = FALSE;

  /* Easy out:  if we have no adm_access for the parent directory,
     then this portion of the tree-delta "patch" must be inapplicable.
     Send a 'missing' state back;  the repos-diff editor should then
     send a 'skip' notification. */
  if (! adm_access)
    {
      if (state)
        *state = svn_wc_notify_state_missing;
      svn_pool_destroy(subpool);
      return SVN_NO_ERROR;
    }

  /* Check for an obstructed or missing node on disk. */
  {
    svn_wc_notify_state_t obstr_state;

    obstr_state = obstructed_or_missing(mine, adm_access, merge_b, subpool);
    if (obstr_state != svn_wc_notify_state_inapplicable)
      {
        if (state)
          *state = obstr_state;
        svn_pool_destroy(subpool);
        return SVN_NO_ERROR;
      }
  }

  SVN_ERR(svn_io_check_path(mine, &kind, subpool));
  switch (kind)
    {
    case svn_node_file:
      {
        svn_boolean_t same;

        /* If the files are identical, attempt deletion */
        SVN_ERR(files_same_p(&same, older, original_props, mine, adm_access,
                             subpool));
        if (same || merge_b->force || merge_b->record_only /* ### why? */)
          {
            /* Passing NULL for the notify_func and notify_baton because
               repos_diff.c:delete_entry() will do it for us. */
            SVN_ERR(svn_client__wc_delete(mine, adm_access, TRUE,
                                          merge_b->dry_run, FALSE, NULL, NULL,
                                          merge_b->ctx, subpool));
            if (state)
              *state = svn_wc_notify_state_changed;
          }
        else
          {
            /* The files differ, so raise a conflict instead of deleting */

            /* This might be use case 5 described in the paper attached to issue
             * #2282.  See also notes/tree-conflicts/detection.txt
             */
            SVN_ERR(tree_conflict(merge_b, adm_access, mine,
                                  svn_node_file,
                                  svn_wc_conflict_action_delete,
                                  svn_wc_conflict_reason_edited));
            if (tree_conflicted)
              *tree_conflicted = TRUE;

            if (state)
              *state = svn_wc_notify_state_obstructed;
          }
      }
      break;
    case svn_node_dir:
      /* The file deletion the merge wants to carry out is obstructed by
       * a directory, so the file the merge wants to delete is a tree
       * conflict victim.
       * See notes about obstructions in notes/tree-conflicts/detection.txt.
       */
      SVN_ERR(tree_conflict(merge_b, adm_access, mine,
                            svn_node_file,
                            svn_wc_conflict_action_delete,
                            svn_wc_conflict_reason_obstructed));
      if (tree_conflicted)
        *tree_conflicted = TRUE;
      if (state)
        *state = svn_wc_notify_state_obstructed;
      break;
    case svn_node_none:
      /* The file deleted in the diff does not exist at the current URL.
       *
       * This is use case 6 described in the paper attached to issue
       * #2282.  See also notes/tree-conflicts/detection.txt
       */
      SVN_ERR(tree_conflict(merge_b, adm_access, mine,
                            svn_node_file,
                            svn_wc_conflict_action_delete,
                            svn_wc_conflict_reason_deleted));
      if (tree_conflicted)
        *tree_conflicted = TRUE;
      if (state)
        *state = svn_wc_notify_state_missing;
      break;
    default:
      if (state)
        *state = svn_wc_notify_state_unknown;
      break;
    }

  svn_pool_destroy(subpool);
  return SVN_NO_ERROR;
}

/* An svn_wc_diff_callbacks3_t function. */
static svn_error_t *
merge_dir_added(svn_wc_adm_access_t *adm_access,
                svn_wc_notify_state_t *state,
                svn_boolean_t *tree_conflicted,
                const char *path,
                svn_revnum_t rev,
                void *baton)
{
  merge_cmd_baton_t *merge_b = baton;
  apr_pool_t *subpool = svn_pool_create(merge_b->pool);
  svn_node_kind_t kind;
  const svn_wc_entry_t *entry;
  const char *copyfrom_url = NULL, *child;
  svn_revnum_t copyfrom_rev = SVN_INVALID_REVNUM;

  if (tree_conflicted)
    *tree_conflicted = FALSE;

  /* Easy out:  if we have no adm_access for the parent directory,
     then this portion of the tree-delta "patch" must be inapplicable.
     Send a 'missing' state back;  the repos-diff editor should then
     send a 'skip' notification. */
  if (! adm_access)
    {
      if (state)
        {
          if (merge_b->dry_run && merge_b->added_path
              && svn_path_is_child(merge_b->added_path, path, subpool))
            *state = svn_wc_notify_state_changed;
          else
            *state = svn_wc_notify_state_missing;
        }
      /* Trying to add a directory at a non-existing path.
       * Although this is a tree-conflict, it will already have been
       * raised by the merge_dir_opened() callback. Not raising additional tree
       * conflicts for the child nodes inside. */
      svn_pool_destroy(subpool);
      return SVN_NO_ERROR;
    }

  child = svn_path_is_child(merge_b->target, path, subpool);
  SVN_ERR_ASSERT(child != NULL);

  /* If this is a merge from the same repository as our working copy,
     we handle adds as add-with-history.  Otherwise, we'll use a pure
     add. */
  if (merge_b->same_repos)
    {
      copyfrom_url = svn_path_url_add_component2(merge_b->merge_source.url2,
                                                 child, subpool);
      copyfrom_rev = rev;
      SVN_ERR(check_scheme_match(adm_access, copyfrom_url));
    }

  /* Find the version-control state of this path */
  SVN_ERR(svn_wc_entry(&entry, path, adm_access, TRUE, subpool));

  SVN_ERR(svn_io_check_path(path, &kind, subpool));

  /* Check for an obstructed or missing node on disk. */
  {
    svn_wc_notify_state_t obstr_state;

    obstr_state = obstructed_or_missing(path, adm_access, merge_b, subpool);

    /* In this case of adding a directory, we have an exception to the usual
     * "skip if it's inconsistent" rule. If the directory exists on disk
     * unexpectedly, we simply make it versioned, because we can do so without
     * risk of destroying data. Only skip if it is versioned but unexpectedly
     * missing from disk, or is unversioned but obstructed by a node of the
     * wrong kind. */
    if (obstr_state == svn_wc_notify_state_missing
        || (obstr_state == svn_wc_notify_state_obstructed
            && kind == svn_node_file))
      {
        if (state)
          *state = obstr_state;
        svn_pool_destroy(subpool);
        return SVN_NO_ERROR;
      }
  }

  /* Switch on the on-disk state of this path */
  switch (kind)
    {
    case svn_node_none:
      /* Unversioned or schedule-delete */
      if (merge_b->dry_run)
        merge_b->added_path = apr_pstrdup(merge_b->pool, path);
      else
        {
          SVN_ERR(svn_io_make_dir_recursively(path, subpool));
          SVN_ERR(svn_wc_add3(path, adm_access, svn_depth_infinity,
                              copyfrom_url, copyfrom_rev,
                              merge_b->ctx->cancel_func,
                              merge_b->ctx->cancel_baton,
                              NULL, NULL, /* don't pass notification func! */
                              subpool));

        }
      if (state)
        *state = svn_wc_notify_state_changed;
      break;
    case svn_node_dir:
      /* Adding an unversioned directory doesn't destroy data */
      if (! entry || entry->schedule == svn_wc_schedule_delete)
        {
          /* The dir is not known to Subversion, or is schedule-delete.
           * We will make it schedule-add. */
          if (!merge_b->dry_run)
            SVN_ERR(svn_wc_add3(path, adm_access, svn_depth_infinity,
                                copyfrom_url, copyfrom_rev,
                                merge_b->ctx->cancel_func,
                                merge_b->ctx->cancel_baton,
                                NULL, NULL, /* no notification func! */
                                subpool));
          else
            merge_b->added_path = apr_pstrdup(merge_b->pool, path);
          if (state)
            *state = svn_wc_notify_state_changed;
        }
      else
        {
          /* The dir is known to Subversion as already existing. */
          if (dry_run_deleted_p(merge_b, path))
            {
              if (state)
                *state = svn_wc_notify_state_changed;
            }
          else
            {
              /* This is a tree conflict. */
              SVN_ERR(tree_conflict_on_add(merge_b, adm_access, path,
                                           svn_node_dir,
                                           svn_wc_conflict_action_add,
                                           svn_wc_conflict_reason_added));
              if (tree_conflicted)
                *tree_conflicted = TRUE;
              if (state)
                *state = svn_wc_notify_state_obstructed;
            }
        }
      break;
    case svn_node_file:
      if (merge_b->dry_run)
        merge_b->added_path = NULL;

      if (entry && dry_run_deleted_p(merge_b, path))
        {
          /* ### TODO: Retain record of this dir being added to
             ### avoid problems from subsequent edits which try to
             ### add children. */
          if (state)
            *state = svn_wc_notify_state_changed;
        }
      else
        {
          /* Obstructed: we can't add a dir because there's a file here
           * (whatever the entry says should be here). */
          SVN_ERR(tree_conflict_on_add(merge_b, adm_access, path,
                                       svn_node_dir,
                                       svn_wc_conflict_action_add,
                                       svn_wc_conflict_reason_obstructed));
          if (tree_conflicted)
            *tree_conflicted = TRUE;
          if (state)
            *state = svn_wc_notify_state_obstructed;
        }
      break;
    default:
      if (merge_b->dry_run)
        merge_b->added_path = NULL;
      if (state)
        *state = svn_wc_notify_state_unknown;
      break;
    }

  svn_pool_destroy(subpool);
  return SVN_NO_ERROR;
}

/* An svn_wc_diff_callbacks3_t function. */
static svn_error_t *
merge_dir_deleted(svn_wc_adm_access_t *adm_access,
                  svn_wc_notify_state_t *state,
                  svn_boolean_t *tree_conflicted,
                  const char *path,
                  void *baton)
{
  merge_cmd_baton_t *merge_b = baton;
  apr_pool_t *subpool = svn_pool_create(merge_b->pool);
  svn_node_kind_t kind;
  const svn_wc_entry_t *entry;
  svn_wc_adm_access_t *parent_access;
  const char *parent_path;
  svn_error_t *err;

  if (tree_conflicted)
    *tree_conflicted = FALSE;

  /* Easy out:  if we have no adm_access for the parent directory,
     then this portion of the tree-delta "patch" must be inapplicable.
     Send a 'missing' state back;  the repos-diff editor should then
     send a 'skip' notification. */
  if (! adm_access)
    {
      if (state)
        *state = svn_wc_notify_state_missing;
      /* Trying to delete a directory at a non-existing path.
       * Although this is a tree-conflict, it will already have been
       * raised by the merge_dir_opened() callback. Not raising additional tree
       * conflicts for the child nodes inside. */
      svn_pool_destroy(subpool);
      return SVN_NO_ERROR;
    }

  /* Find the version-control state of this path */
  SVN_ERR(svn_wc_entry(&entry, path, adm_access, TRUE, subpool));

  /* Check for an obstructed or missing node on disk. */
  {
    svn_wc_notify_state_t obstr_state;

    obstr_state = obstructed_or_missing(path, adm_access, merge_b, subpool);
    if (obstr_state != svn_wc_notify_state_inapplicable)
      {
        if (state)
          *state = obstr_state;
        svn_pool_destroy(subpool);
        return SVN_NO_ERROR;
      }
  }

  /* Switch on the on-disk state of this path */
  SVN_ERR(svn_io_check_path(path, &kind, subpool));
  switch (kind)
    {
    case svn_node_dir:
      {
        if (entry && (entry->schedule != svn_wc_schedule_delete))
          {
            /* ### TODO: Before deleting, we should ensure that this dir
               tree is equal to the one we're being asked to delete.
               If not, mark this directory as a tree conflict victim,
               because this could be use case 5 as described in
               notes/tree-conflicts/detection.txt.
             */

            svn_path_split(path, &parent_path, NULL, subpool);
            SVN_ERR(svn_wc_adm_retrieve(&parent_access, adm_access, parent_path,
                                        subpool));
            /* Passing NULL for the notify_func and notify_baton because
               repos_diff.c:delete_entry() will do it for us. */
            err = svn_client__wc_delete(path, parent_access, merge_b->force,
                                        merge_b->dry_run, FALSE,
                                        NULL, NULL,
                                        merge_b->ctx, subpool);
            if (err)
              {
                svn_error_clear(err);

                /* If the attempt to delete an existing directory failed,
                 * the directory has local modifications (e.g. locally added
                 * files, or property changes). Flag a tree conflict. */
                SVN_ERR(tree_conflict(merge_b, adm_access, path,
                                      svn_node_dir,
                                      svn_wc_conflict_action_delete,
                                      svn_wc_conflict_reason_edited));
                if (tree_conflicted)
                  *tree_conflicted = TRUE;
                if (state)
                  *state = svn_wc_notify_state_conflicted;
              }
            else
              {
                if (state)
                  *state = svn_wc_notify_state_changed;
              }
          }
        else
          {
            /* Dir is already not under version control at this path. */
            /* Raise a tree conflict. */
            SVN_ERR(tree_conflict(merge_b, adm_access, path,
                                  svn_node_dir,
                                  svn_wc_conflict_action_delete,
                                  svn_wc_conflict_reason_deleted));
            if (tree_conflicted)
              *tree_conflicted = TRUE;
          }
      }
      break;
    case svn_node_file:
      if (state)
        *state = svn_wc_notify_state_obstructed;
      break;
    case svn_node_none:
      /* Dir is already non-existent. This is use case 6 as described in
       * notes/tree-conflicts/detection.txt.
       * This case was formerly treated as no-op. */
      SVN_ERR(tree_conflict(merge_b, adm_access, path,
                            svn_node_dir,
                            svn_wc_conflict_action_delete,
                            svn_wc_conflict_reason_deleted));
      if (tree_conflicted)
        *tree_conflicted = TRUE;
      if (state)
        *state = svn_wc_notify_state_missing;
      break;
    default:
      if (state)
        *state = svn_wc_notify_state_unknown;
      break;
    }

  svn_pool_destroy(subpool);
  return SVN_NO_ERROR;
}

/* An svn_wc_diff_callbacks3_t function. */
static svn_error_t *
merge_dir_opened(svn_wc_adm_access_t *adm_access,
                 svn_boolean_t *tree_conflicted,
                 const char *path,
                 svn_revnum_t rev,
                 void *baton)
{
  if (tree_conflicted)
    *tree_conflicted = FALSE;

  if (adm_access == NULL)
    {
      /* Trying to open a directory at a non-existing path.
       * Although this is a tree-conflict, it will already have been
       * raised by the merge_dir_opened() callback on the topmost nonexisting
       * ancestor, where an adm_access was still present. Not raising
       * additional tree conflicts for the child nodes inside. */
      /* ### TODO: Verify that this holds true for explicit targets that
       * # point deep into a nonexisting subtree. */
      return SVN_NO_ERROR;
    }

  /* Detect a tree-conflict, if any. */
  {
    merge_cmd_baton_t *merge_b = baton;
    apr_pool_t *subpool = svn_pool_create(merge_b->pool);
    svn_node_kind_t kind;
    const svn_wc_entry_t *entry;

    /* Find the version-control and on-disk states of this path */
    SVN_ERR(svn_wc_entry(&entry, path, adm_access, TRUE, subpool));
    SVN_ERR(svn_io_check_path(path, &kind, subpool));

    /* If we're trying to open a directory that's not a directory,
     * raise a tree conflict. */
    if (!entry || entry->schedule == svn_wc_schedule_delete
        || kind != svn_node_dir)
      {
        SVN_ERR(tree_conflict(merge_b, adm_access, path,
                              svn_node_dir,
                              svn_wc_conflict_action_edit,
                              svn_wc_conflict_reason_deleted));
        if (tree_conflicted)
          *tree_conflicted = TRUE;
      }

    svn_pool_destroy(subpool);
  }

  return SVN_NO_ERROR;
}

/* An svn_wc_diff_callbacks3_t function. */
static svn_error_t *
merge_dir_closed(svn_wc_adm_access_t *adm_access,
                 svn_wc_notify_state_t *contentstate,
                 svn_wc_notify_state_t *propstate,
                 svn_boolean_t *tree_conflicted,
                 const char *path,
                 void *baton)
{
  if (contentstate)
    *contentstate = svn_wc_notify_state_unknown;
  if (propstate)
    *propstate = svn_wc_notify_state_unknown;
  if (tree_conflicted)
    *tree_conflicted = FALSE;

  /* Nothing to be done. */

  return SVN_NO_ERROR;
}

/* The main callback table for 'svn merge'.  */
static const svn_wc_diff_callbacks3_t
merge_callbacks =
  {
    merge_file_changed,
    merge_file_added,
    merge_file_deleted,
    merge_dir_added,
    merge_dir_deleted,
    merge_props_changed,
    merge_dir_opened,
    merge_dir_closed
  };


/*-----------------------------------------------------------------------*/

/*** Merge Notification ***/


/* Contains any state collected while receiving path notifications. */
typedef struct
{
  /* The wrapped callback and baton. */
  svn_wc_notify_func2_t wrapped_func;
  void *wrapped_baton;

  /* The number of notifications received. */
  apr_uint32_t nbr_notifications;

  /* The number of operative notifications received. */
  apr_uint32_t nbr_operative_notifications;

  /* The list of merged paths; either absolute or relative to the
     current working directory */
  apr_hash_t *merged_paths;

  /* The list of any skipped paths, which should be examined and
     cleared after each invocation of the callback.  The paths
     are  either absolute or relative to the current working
     directory. */
  apr_hash_t *skipped_paths;

  /* A list of the root paths of any added subtrees which might require
     their own explicit mergeinfo.  The paths are either absolute or
     relative to the current working directory. */
  apr_hash_t *added_paths;

  /* Flag indicating whether it is a single file merge or not. */
  svn_boolean_t is_single_file_merge;

  /* Depth first ordered list of paths that needs special care while merging.
     This defaults to NULL. For 'same_url' merge alone we set it to
     proper array. This is used by notification_receiver to put a
     merge notification begin lines. */
  apr_array_header_t *children_with_mergeinfo;

  /* The index in CHILDREN_WITH_MERGEINFO where we found the nearest ancestor
     for merged path. Default value is '-1'.*/
  int cur_ancestor_index;

  /* We use this to make a decision on merge begin line notifications. */
  merge_cmd_baton_t *merge_b;

  /* Pool used in notification_receiver() to avoid the iteration
     sub-pool which is passed in, then subsequently destroyed. */
  apr_pool_t *pool;

} notification_receiver_baton_t;


/* Finds a nearest ancestor in CHILDREN_WITH_MERGEINFO for PATH. If
   PATH_IS_OWN_ANCESTOR is TRUE then a child in CHILDREN_WITH_MERGEINFO
   where child->path == PATH is considered PATH's ancestor.  If FALSE,
   then child->path must be a proper ancestor of PATH.

   CHILDREN_WITH_MERGEINFO is expected to be sorted in Depth first
   order of path. Nearest ancestor's index from
   CHILDREN_WITH_MERGEINFO is returned. */
static int
find_nearest_ancestor(apr_array_header_t *children_with_mergeinfo,
                      svn_boolean_t path_is_own_ancestor,
                      const char *path)
{
  int i;
  int ancestor_index = 0;

  /* This if condition is not needed as this function should be used
     from the context of same_url merge where CHILDREN_WITH_MERGEINFO
     will not be NULL and of size atleast 1. We have this if condition
     just to protect the wrong caller. */
  if (!children_with_mergeinfo)
    return 0;
  for (i = 0; i < children_with_mergeinfo->nelts; i++)
    {
      svn_client__merge_path_t *child =
        APR_ARRAY_IDX(children_with_mergeinfo, i, svn_client__merge_path_t *);
      if (svn_path_is_ancestor(child->path, path)
          && (path_is_own_ancestor
              || svn_path_compare_paths(child->path, path) != 0))
        ancestor_index = i;
    }
  return ancestor_index;
}


#define IS_OPERATIVE_NOTIFICATION(notify)  \
                    (notify->content_state == svn_wc_notify_state_conflicted \
                     || notify->content_state == svn_wc_notify_state_merged  \
                     || notify->content_state == svn_wc_notify_state_changed \
                     || notify->prop_state == svn_wc_notify_state_conflicted \
                     || notify->prop_state == svn_wc_notify_state_merged     \
                     || notify->prop_state == svn_wc_notify_state_changed    \
                     || notify->action == svn_wc_notify_update_add \
                     || notify->action == svn_wc_notify_tree_conflict)

/* Our svn_wc_notify_func2_t wrapper.*/
static void
notification_receiver(void *baton, const svn_wc_notify_t *notify,
                      apr_pool_t *pool)
{
  notification_receiver_baton_t *notify_b = baton;
  svn_boolean_t is_operative_notification = FALSE;

  /* Is the notification the result of a real operative merge? */
  if (IS_OPERATIVE_NOTIFICATION(notify))
    {
      notify_b->nbr_operative_notifications++;
      is_operative_notification = TRUE;
    }

  /* If our merge sources are ancestors of one another... */
  if (notify_b->merge_b->sources_ancestral)
    {
      notify_b->nbr_notifications++;

      /* See if this is an operative directory merge. */
      if (!(notify_b->is_single_file_merge) && is_operative_notification)
        {
          /* Find NOTIFY->PATH's nearest ancestor in
             NOTIFY->CHILDREN_WITH_MERGEINFO.  Normally we consider a child in
             NOTIFY->CHILDREN_WITH_MERGEINFO representing PATH to be an
             ancestor of PATH, but if this is a deletion of PATH then the
             notification must be for a proper ancestor of PATH.  This ensures
             we don't get notifications like:

               --- Merging rX into 'PARENT/CHILD'
               D    PARENT/CHILD

             But rather:

               --- Merging rX into 'PARENT'
               D    PARENT/CHILD
          */
          int new_nearest_ancestor_index =
            find_nearest_ancestor(
              notify_b->children_with_mergeinfo,
              notify->action != svn_wc_notify_update_delete,
              notify->path);

          if (new_nearest_ancestor_index != notify_b->cur_ancestor_index)
            {
              svn_client__merge_path_t *child =
                APR_ARRAY_IDX(notify_b->children_with_mergeinfo,
                              new_nearest_ancestor_index,
                              svn_client__merge_path_t *);
              notify_b->cur_ancestor_index = new_nearest_ancestor_index;
              if (!child->absent && child->remaining_ranges->nelts > 0
                  && !(new_nearest_ancestor_index == 0
                       && child->remaining_ranges == 0))
                {
                  svn_wc_notify_t *notify_merge_begin;
                  notify_merge_begin =
                    svn_wc_create_notify(child->path,
                                         notify_b->merge_b->same_repos
                                           ? svn_wc_notify_merge_begin
                                           : svn_wc_notify_foreign_merge_begin,
                                         pool);
                  notify_merge_begin->merge_range =
                    APR_ARRAY_IDX(child->remaining_ranges, 0,
                                  svn_merge_range_t *);
                  if (notify_b->wrapped_func)
                    (*notify_b->wrapped_func)(notify_b->wrapped_baton,
                                              notify_merge_begin, pool);
                }
            }
        }

      if (notify->content_state == svn_wc_notify_state_merged
          || notify->content_state == svn_wc_notify_state_changed
          || notify->prop_state == svn_wc_notify_state_merged
          || notify->prop_state == svn_wc_notify_state_changed
          || notify->action == svn_wc_notify_update_add)
        {
          const char *merged_path = apr_pstrdup(notify_b->pool, notify->path);

          if (notify_b->merged_paths == NULL)
            notify_b->merged_paths = apr_hash_make(notify_b->pool);

          apr_hash_set(notify_b->merged_paths, merged_path,
                       APR_HASH_KEY_STRING, merged_path);
        }

      if (notify->action == svn_wc_notify_skip)
        {
          const char *skipped_path = apr_pstrdup(notify_b->pool, notify->path);

          if (notify_b->skipped_paths == NULL)
            notify_b->skipped_paths = apr_hash_make(notify_b->pool);

          apr_hash_set(notify_b->skipped_paths, skipped_path,
                       APR_HASH_KEY_STRING, skipped_path);
        }
      if (notify->action == svn_wc_notify_update_add)
        {
          svn_boolean_t is_root_of_added_subtree = FALSE;
          const char *added_path = apr_pstrdup(notify_b->pool, notify->path);
          const char *added_path_parent = NULL;

          /* Stash the root path of any added subtrees. */
          if (notify_b->added_paths == NULL)
            {
              notify_b->added_paths = apr_hash_make(notify_b->pool);
              is_root_of_added_subtree = TRUE;
            }
          else
            {
              added_path_parent = svn_path_dirname(added_path, pool);
              if (!apr_hash_get(notify_b->added_paths, added_path_parent,
                                APR_HASH_KEY_STRING))
                is_root_of_added_subtree = TRUE;
            }
          if (is_root_of_added_subtree)
            apr_hash_set(notify_b->added_paths, added_path,
                         APR_HASH_KEY_STRING, added_path);
        }
    }
  /* Otherwise, our merge sources aren't ancestors of one another. */
  else if (!(notify_b->is_single_file_merge)
           && notify_b->nbr_operative_notifications == 1
           && is_operative_notification)
    {
      svn_wc_notify_t *notify_merge_begin;
      notify_merge_begin =
        svn_wc_create_notify(notify_b->merge_b->target,
                             notify_b->merge_b->same_repos
                               ? svn_wc_notify_merge_begin
                               : svn_wc_notify_foreign_merge_begin,
                             pool);
      if (notify_b->wrapped_func)
        (*notify_b->wrapped_func)(notify_b->wrapped_baton, notify_merge_begin,
                                  pool);
    }

  if (notify_b->wrapped_func)
    (*notify_b->wrapped_func)(notify_b->wrapped_baton, notify, pool);
}

/* Helper for the numerous times we need to allocate and initialize
   a rangelist with one element.

   Return a rangelist allocated in POOL with one svn_merge_range_t *
   element, also allocated in POOL and defined by START, END, and
   INHERITABLE. */
static apr_array_header_t *
init_rangelist(svn_revnum_t start,
               svn_revnum_t end,
               svn_boolean_t inheritable,
               apr_pool_t *pool)
{
  apr_array_header_t *rangelist =
    apr_array_make(pool, 1, sizeof(svn_merge_range_t *));
  svn_merge_range_t *range = apr_pcalloc(pool, sizeof(*range));

  range->start = start;
  range->end = end;
  range->inheritable = inheritable;
  APR_ARRAY_PUSH(rangelist, svn_merge_range_t *) = range;
  return rangelist;
}

/* Helper for calculate_remaining_ranges() when that function is operating on
   CHILD, a subtree of the merge target.  Like calculate_remaining_ranges()
   this function should only be called when honoring mergeinfo.

   CHILD, PARENT, MERGEINFO_PATH, REVISION1, REVISION2, and CTX are all
   cascaded from filter_merged_revisions() - see that function for more
   information on each.  In particular, note that PARENT must have been
   processed already by this function.  More specifically, this means that
   PARENT->REMAINING_RANGES must already be populated -- it can be an empty
   rangelist but cannot be NULL.  PRIMARY_URL is the younger of the
   url1@revision1 and url2@revision2 arguments to calculate_remaining_ranges().
   RA_SESSION is the session for PRIMARY_URL.

   Since this function is only invoked for subtrees of the merge target, the
   guarantees afforded by normalize_merge_sources() don't apply - see the
   'MERGEINFO MERGE SOURCE NORMALIZATION' comment at the top of this file.
   Therefore it is possible that PRIMARY_URL@REVISION1 and
   PRIMARY_URL@REVISION2 don't describe the endpoints of an unbroken line of
   history.  The purpose of this helper is to identify these cases of broken
   history and adjust CHILD->REMAINING_RANGES in such a way we don't later try
   to describe nonexistent path/revisions to the merge report editor -- see
   drive_merge_report_editor().

   If PRIMARY_URL@REVISION1 and PRIMARY_URL@REVISION2 describe an unbroken
   line of history then do nothing and leave CHILD->REMAINING_RANGES as-is.

   If neither PRIMARY_URL@REVISION1 nor PRIMARY_URL@REVISION2 exist then
   there is nothing to merge to CHILD->PATH so set CHILD->REMAINING_RANGES
   equal to PARENT->REMAINING_RANGES.  This will cause the subtree to
   effectively ignore CHILD -- see 'Note: If the first svn_merge_range_t...'
   in drive_merge_report_editor()'s doc string.

   If PRIMARY_URL@REVISION1 *xor* PRIMARY_URL@REVISION2 exist then we take the
   subset of REVISION1:REVISION2 in CHILD->REMAINING_RANGES at which
   PRIMARY_URL doesn't exist and set that subset equal to
   PARENT->REMAINING_RANGES' intersection with that non-existent range.  Why?
   Because this causes CHILD->REMAINING_RANGES to be identical to
   PARENT->REMAINING_RANGES for revisions between REVISION1 and REVISION2 at
   which PRIMARY_URL doesn't exist.  As mentioned above this means that
   drive_merge_report_editor() won't attempt to describe these non-existent
   subtree path/ranges to the reporter (which would break the merge).

   If the preceeding paragraph wasn't terribly clear then what follows spells
   out this function's behavior a bit more explicitly:

   For forward merges (REVISION1 < REVISION2)

     If PRIMARY_URL@REVISION1 exists but PRIMARY_URL@REVISION2 doesn't, then
     find the revision 'N' in which PRIMARY_URL@REVISION1 was deleted.  Leave
     the subset of CHILD->REMAINING_RANGES that intersects with
     REVISION1:(N - 1) as-is and set the subset of CHILD->REMAINING_RANGES
     that intersects with (N - 1):REVISION2 equal to PARENT->REMAINING_RANGES'
     intersection with (N - 1):REVISION2.

     If PRIMARY_URL@REVISION1 doesn't exist but PRIMARY_URL@REVISION2 does,
     then find the revision 'M' in which PRIMARY_URL@REVISION2 came into
     existence.  Leave the subset of CHILD->REMAINING_RANGES that intersects with
     (M - 1):REVISION2 as-is and set the subset of CHILD->REMAINING_RANGES
     that intersects with REVISION1:(M - 1) equal to PARENT->REMAINING_RANGES'
     intersection with REVISION1:(M - 1).

   For reverse merges (REVISION1 > REVISION2)

     If PRIMARY_URL@REVISION1 exists but PRIMARY_URL@REVISION2 doesn't, then
     find the revision 'N' in which PRIMARY_URL@REVISION1 came into existence.
     Leave the subset of CHILD->REMAINING_RANGES that intersects with
     REVISION2:(N - 1) as-is and set the subset of CHILD->REMAINING_RANGES
     that intersects with (N - 1):REVISION1 equal to PARENT->REMAINING_RANGES'
     intersection with (N - 1):REVISION1.

     If PRIMARY_URL@REVISION1 doesn't exist but PRIMARY_URL@REVISION2 does,
     then find the revision 'M' in which PRIMARY_URL@REVISION2 came into
     existence.  Leave the subset of CHILD->REMAINING_RANGES that intersects with
     REVISION2:(M - 1) as-is and set the subset of CHILD->REMAINING_RANGES
     that intersects with (M - 1):REVISION1 equal to PARENT->REMAINING_RANGES'
     intersection with REVISION1:(M - 1).

   All the allocations are made from POOL. */
static svn_error_t *
adjust_deleted_subtree_ranges(svn_client__merge_path_t *child,
                              svn_client__merge_path_t *parent,
                              const char *mergeinfo_path,
                              svn_revnum_t revision1,
                              svn_revnum_t revision2,
                              const char *primary_url,
                              svn_ra_session_t *ra_session,
                              svn_client_ctx_t *ctx,
                              apr_pool_t *pool)
{
  svn_boolean_t is_rollback = revision2 < revision1;
  svn_revnum_t younger_rev = is_rollback ? revision1 : revision2;
  svn_revnum_t peg_rev = younger_rev;
  svn_revnum_t older_rev = is_rollback ? revision2 : revision1;
  svn_revnum_t revision_primary_url_deleted = SVN_INVALID_REVNUM;
  apr_array_header_t *segments;
  const char *rel_source_path;
  const char *session_url;
  svn_error_t *err;
  apr_pool_t *subpool = svn_pool_create(pool);

  SVN_ERR_ASSERT(parent->remaining_ranges);

  /* We want to know about PRIMARY_URL@peg_rev, but we need PRIMARY_URL's
     path relative to RA_SESSION's URL. */
  SVN_ERR(svn_ra_get_session_url(ra_session, &session_url, subpool));
  SVN_ERR(svn_client__path_relative_to_root(&rel_source_path,
                                            primary_url,
                                            session_url,
                                            FALSE,
                                            ra_session,
                                            NULL,
                                            subpool));
  err = svn_client__repos_location_segments(&segments, ra_session,
                                            rel_source_path, peg_rev,
                                            younger_rev, older_rev, ctx,
                                            subpool);

  /* If PRIMARY_URL@peg_rev doesn't exist then
      svn_client__repos_location_segments() typically returns an
      SVN_ERR_FS_NOT_FOUND error, but if it doesn't exist for a
      forward merge over ra_neon then we get SVN_ERR_RA_DAV_REQUEST_FAILED.
      http://subversion.tigris.org/issues/show_bug.cgi?id=3137 fixed some of
      the cases where different RA layers returned different error codes to
      signal the "path not found"...but it looks like there is more to do. */
  if (err)
    {
      if (err->apr_err == SVN_ERR_FS_NOT_FOUND
          || err->apr_err == SVN_ERR_RA_DAV_REQUEST_FAILED)
        {
          /* PRIMARY_URL@peg_rev doesn't exist.  Check if PRIMARY_URL@older_rev
             exists, if neither exist then the editor can simply ignore this
             subtree. */
          svn_node_kind_t kind;

          svn_error_clear(err);
          err = NULL;
          SVN_ERR(svn_ra_check_path(ra_session, rel_source_path,
                                    older_rev, &kind, subpool));
          if (kind == svn_node_none)
            {
              /* Neither PRIMARY_URL@peg_rev nor PRIMARY_URL@older_rev exist,
                 so there is nothing to merge.  Set CHILD->REMAINING_RANGES
                 identical to PARENT's. */
              child->remaining_ranges =
                svn_rangelist_dup(parent->remaining_ranges, subpool);
            }
          else
            {
              apr_array_header_t *exists_rangelist, *deleted_rangelist;

              /* PRIMARY_URL@older_rev exists, so it was deleted at some
                 revision prior to peg_rev, find that revision. */
              SVN_ERR(svn_ra_get_deleted_rev(ra_session, rel_source_path,
                                             older_rev, younger_rev,
                                             &revision_primary_url_deleted,
                                             subpool));

              /* PRIMARY_URL@older_rev exists and PRIMARY_URL@peg_rev doesn't,
                 so svn_ra_get_deleted_rev() should always find the revision
                 PRIMARY_URL@older_rev was deleted. */
              SVN_ERR_ASSERT(SVN_IS_VALID_REVNUM(
                revision_primary_url_deleted));

              /* If this is a reverse merge reorder CHILD->REMAINING_RANGES and
                 PARENT->REMAINING_RANGES so both will work with the
                 svn_rangelist_* APIs below. */
              if (is_rollback)
                {
                  /* svn_rangelist_reverse operates in place so it's safe
                     to use our subpool. */
                  SVN_ERR(svn_rangelist_reverse(child->remaining_ranges,
                                                subpool));
                  SVN_ERR(svn_rangelist_reverse(parent->remaining_ranges,
                                                subpool));
                }

              /* Create a rangelist describing the range PRIMARY_URL@older_rev
                 exists and find the intersection of that and
                 CHILD->REMAINING_RANGES. */
              exists_rangelist =
                init_rangelist(older_rev, revision_primary_url_deleted - 1,
                               TRUE, subpool);
              SVN_ERR(svn_rangelist_intersect(&(child->remaining_ranges),
                                              exists_rangelist,
                                              child->remaining_ranges,
                                              FALSE, subpool));

              /* Create a second rangelist describing the range beginning when
                 PRIMARY_URL@older_rev was deleted until younger_rev.  Then
                 find the intersection of that and PARENT->REMAINING_RANGES.
                 Finally merge this rangelist with the rangelist above and
                 store the result in CHILD->REMANING_RANGES. */
              deleted_rangelist =
                init_rangelist(revision_primary_url_deleted - 1, peg_rev,
                               TRUE, subpool);
              SVN_ERR(svn_rangelist_intersect(&deleted_rangelist,
                                              deleted_rangelist,
                                              parent->remaining_ranges,
                                              FALSE, subpool));

              SVN_ERR(svn_rangelist_merge(&(child->remaining_ranges),
                                          deleted_rangelist, subpool));

              /* Return CHILD->REMAINING_RANGES and PARENT->REMAINING_RANGES
                 to reverse order if necessary. */
              if (is_rollback)
                {
                  SVN_ERR(svn_rangelist_reverse(child->remaining_ranges,
                                                subpool));
                  SVN_ERR(svn_rangelist_reverse(parent->remaining_ranges,
                                                subpool));
                }
            }
        }
      else
        {
          return err;
        }
    }
  else /* PRIMARY_URL@peg_rev exists. */
    {
      apr_array_header_t *exists_rangelist, *non_existent_rangelist;
      svn_location_segment_t *segment =
        APR_ARRAY_IDX(segments, (segments->nelts - 1),
                      svn_location_segment_t *);

      /* We know PRIMARY_URL@peg_rev exists as the call to
         svn_client__repos_location_segments() succeeded.  If there is only
         one segment that starts at oldest_rev then we know that
         PRIMARY_URL@oldest_rev:PRIMARY_URL@peg_rev describes an unbroken
         line of history, so there is nothing more to adjust in
         CHILD->REMAINING_RANGES. */
      if (segment->range_start == older_rev)
        {
          svn_pool_destroy(subpool);
          return SVN_NO_ERROR;
        }

      /* If this is a reverse merge reorder CHILD->REMAINING_RANGES and
         PARENT->REMAINING_RANGES so both will work with the
         svn_rangelist_* APIs below. */
      if (is_rollback)
        {
          SVN_ERR(svn_rangelist_reverse(child->remaining_ranges, subpool));
          SVN_ERR(svn_rangelist_reverse(parent->remaining_ranges, subpool));
        }

      /* Since segment doesn't span older_rev:peg_rev we know
         PRIMARY_URL@peg_rev didn't come into existence until
         segment->range_start + 1.  Create a rangelist describing
         range where PRIMARY_URL exists and find the intersection of that
         range and CHILD->REMAINING_RANGELIST. */
      exists_rangelist = init_rangelist(segment->range_start, peg_rev,
                                        TRUE, subpool);
      SVN_ERR(svn_rangelist_intersect(&(child->remaining_ranges),
                                      exists_rangelist,
                                      child->remaining_ranges,
                                      FALSE, subpool));

      /* Create a second rangelist describing the range before
         PRIMARY_URL@peg_rev came into existence and find the intersection of
         that range and PARENT->REMAINING_RANGES.  Then merge that rangelist
         with exists_rangelist and store the result in
         CHILD->REMANING_RANGES. */
      non_existent_rangelist = init_rangelist(older_rev, segment->range_start,
                                              TRUE, subpool);
      SVN_ERR(svn_rangelist_intersect(&non_existent_rangelist,
                                      non_existent_rangelist,
                                      parent->remaining_ranges,
                                      FALSE, subpool));

      SVN_ERR(svn_rangelist_merge(&(child->remaining_ranges),
                                  non_existent_rangelist, subpool));

      /* Return CHILD->REMAINING_RANGES and PARENT->REMAINING_RANGES
         to reverse order if necessary. */
      if (is_rollback)
        {
          SVN_ERR(svn_rangelist_reverse(child->remaining_ranges, subpool));
          SVN_ERR(svn_rangelist_reverse(parent->remaining_ranges, subpool));
        }
    }

  /* Make a lasting copy of CHILD->REMAINING_RANGES using POOL. */
  child->remaining_ranges = svn_rangelist_dup(child->remaining_ranges, pool);
  svn_pool_destroy(subpool);
  return SVN_NO_ERROR;
}

/*-----------------------------------------------------------------------*/

/*** Determining What Remains To Be Merged ***/

/* Get explicit and/or implicit mergeinfo for the working copy path
   TARGET_WCPATH with the corresponding ENTRY.

   If RECORDED_MERGEINFO is not NULL then set *RECORDED_MERGEINFO
   to TARGET_WCPATH's explicit or inherited mergeinfo as dictated by
   INHERIT.  If RECORDED_MERGEINFO is NULL then ENTRY is ignored and
   may be NULL.

   If IMPLICIT_MERGEINFO is not NULL then set *IMPLICIT_MERGEINFO
   to TARGET_WCPATH's implicit mergeinfo (a.k.a. natural history).

   If INDIRECT is not NULL set *INDIRECT to TRUE if *RECORDED_MERGEINFO
   is inherited and not explicit.  If RECORDED_MERGEINFO is NULL then
   INDIRECT is ignored.

   If IMPLICIT_MERGEINFO is not NULL then START and END are limits on the
   the natural history sought, must both be valid revision numbers, and
   START must be greater than END. */
static svn_error_t *
get_full_mergeinfo(svn_mergeinfo_t *recorded_mergeinfo,
                   svn_mergeinfo_t *implicit_mergeinfo,
                   const svn_wc_entry_t *entry,
                   svn_boolean_t *indirect,
                   svn_mergeinfo_inheritance_t inherit,
                   svn_ra_session_t *ra_session,
                   const char *target_wcpath,
                   svn_revnum_t start,
                   svn_revnum_t end,
                   svn_wc_adm_access_t *adm_access,
                   svn_client_ctx_t *ctx,
                   apr_pool_t *pool)
{
  /* First, we get the real mergeinfo. */
  if (recorded_mergeinfo)
    {
      svn_boolean_t inherited;
      SVN_ERR(svn_client__get_wc_or_repos_mergeinfo(recorded_mergeinfo, entry,
                                                    &inherited, FALSE,
                                                    inherit, NULL,
                                                    target_wcpath,
                                                    adm_access, ctx, pool));
      if (indirect)
        *indirect = inherited;
    }

  if (implicit_mergeinfo)
    {
      const char *session_url = NULL, *url;
      svn_revnum_t target_rev;
      svn_opt_revision_t peg_revision;
      apr_pool_t *sesspool = NULL;

      /* Assert that we have sane input. */
      SVN_ERR_ASSERT(SVN_IS_VALID_REVNUM(start)
                 && SVN_IS_VALID_REVNUM(end)
                 && (start > end));

      peg_revision.kind = svn_opt_revision_working;
      SVN_ERR(svn_client__derive_location(&url, &target_rev, target_wcpath,
                                          &peg_revision, ra_session,
                                          adm_access, ctx, pool));

      if (target_rev <= end)
        {
          /* We're asking about a range outside our natural history
             altogether.  That means our implicit mergeinfo is empty. */
          *implicit_mergeinfo = apr_hash_make(pool);
          return SVN_NO_ERROR;
        }

      /* Temporarily point our RA_SESSION at our target URL so we can
         fetch so-called "implicit mergeinfo" (that is, natural history). */
      if (ra_session)
        {
          SVN_ERR(svn_client__ensure_ra_session_url(&session_url, ra_session,
                                                    url, pool));
        }
      else
        {
          sesspool = svn_pool_create(pool);
          SVN_ERR(svn_client__open_ra_session_internal(&ra_session, url,
                                                       NULL, NULL, NULL,
                                                       FALSE, TRUE,
                                                       ctx, sesspool));
        }

      /* Our underlying APIs can't yet handle the case where the peg
         revision isn't the youngest of the three revisions.  So we'll
         just verify that the source in the peg revision is related to the
         the source in the youngest requested revision (which is all the
         underlying APIs would do in this case right now anyway). */
      if (target_rev < start)
        {
          const char *start_url;
          svn_opt_revision_t requested, unspec, pegrev, *start_revision;
          unspec.kind = svn_opt_revision_unspecified;
          requested.kind = svn_opt_revision_number;
          requested.value.number = start;
          pegrev.kind = svn_opt_revision_number;
          pegrev.value.number = target_rev;

          SVN_ERR(svn_client__repos_locations(&start_url, &start_revision,
                                              NULL, NULL, ra_session, url,
                                              &pegrev, &requested,
                                              &unspec, ctx, pool));
          /* ### FIXME: Having a low-brain moment.  Shouldn't we check
             that START_URL matches our session URL at this point?  */
          target_rev = start;
        }

      /* Fetch the implicit mergeinfo. */
      peg_revision.kind = svn_opt_revision_number;
      peg_revision.value.number = target_rev;
      SVN_ERR(svn_client__get_history_as_mergeinfo(implicit_mergeinfo, url,
                                                   &peg_revision, start, end,
                                                   ra_session, NULL, ctx,
                                                   pool));

      /* If we created an RA_SESSION above, destroy it.  Otherwise, if
         reparented an existing session, point it back where it was when
         we were called. */
      if (sesspool)
        {
          svn_pool_destroy(sesspool);
        }
      else if (session_url)
        {
          SVN_ERR(svn_ra_reparent(ra_session, session_url, pool));
        }
    } /*if (implicit_mergeinfo) */

  return SVN_NO_ERROR;
}

/* Helper for ensure_implicit_mergeinfo().

   PARENT, CHILD, REVISION1, REVISION2, RA_SESSION, ADM_ACCESS, and CTX
   are all cascaded from the arguments of the same names in
   ensure_implicit_mergeinfo().  PARENT and CHILD must both exist, i.e.
   this function should never be called where CHILD is the merge target.

   If PARENT->IMPLICIT_MERGEINFO is NULL, obtain it from the server.

   Set CHILD->IMPLICIT_MERGEINFO to the mergeinfo inherited from
   PARENT->IMPLICIT_MERGEINFO.  CHILD->IMPLICIT_MERGEINFO is allocated
   in POOL.
   */
static svn_error_t *
inherit_implicit_mergeinfo_from_parent(svn_client__merge_path_t *parent,
                                       svn_client__merge_path_t *child,
                                       svn_revnum_t revision1,
                                       svn_revnum_t revision2,
                                       svn_ra_session_t *ra_session,
                                       svn_wc_adm_access_t *adm_access,
                                       svn_client_ctx_t *ctx,
                                       apr_pool_t *pool)
{
  const char *path_diff, *common_ancestor;

  /* This only works on subtrees! */
  SVN_ERR_ASSERT(parent);
  SVN_ERR_ASSERT(child);

  /* While PARENT must exist, it is possible we've deferred
     getting its implicit mergeinfo.  If so get it now. */
  if (!parent->implicit_mergeinfo)
    SVN_ERR(get_full_mergeinfo(NULL, &(parent->implicit_mergeinfo),
                               NULL, NULL, svn_mergeinfo_inherited,
                               ra_session, child->path,
                               MAX(revision1, revision2),
                               MIN(revision1, revision2),
                               adm_access, ctx, pool));

  /* Let CHILD inherit PARENT's implicit mergeinfo. */
  child->implicit_mergeinfo = apr_hash_make(pool);

  /* Find the longest common ancestor path*/
  common_ancestor = svn_dirent_get_longest_ancestor(child->path,
                                                    parent->path, pool);

  /* PARENT->PATH better be an ancestor of CHILD->PATH! */
  SVN_ERR_ASSERT(common_ancestor);

  /* Calculate the pathwise difference between the longest common ancestor
     and CHILD->PATH. */
  path_diff = apr_pstrdup(pool, (child->path + strlen(common_ancestor)));

  if (path_diff[0] == '/') /* Remove any leading '/'. */
    path_diff++;

  SVN_ERR(svn_client__adjust_mergeinfo_source_paths(
    child->implicit_mergeinfo, path_diff,
    parent->implicit_mergeinfo, pool));
  return SVN_NO_ERROR;
}

/* Helper of filter_merged_revisions().

   If we have deferred obtaining CHILD->IMPLICIT_MERGEINFO, then get
   it now, allocating it in RESULT_POOL.  If CHILD_INHERITS_PARENT is true
   then set CHILD->IMPLICIT_MERGEINFO to the mergeinfo inherited from
   PARNET->IMPLICIT_MERGEINFO, otherwise contact the repository.  Use
   SCRATCH_POOL for all temporary allocations.

   PARENT, CHILD, ENTRY, REVISION1, REVISION2, RA_SESSION, ADM_ACCESS, and
   CTX are all cascased from the arguments of the same name in
   filter_merged_revisions() and the same conditions for that function
   hold here. */
static svn_error_t *
ensure_implicit_mergeinfo(svn_client__merge_path_t *parent,
                          svn_client__merge_path_t *child,
                          svn_boolean_t child_inherits_parent,
                          const svn_wc_entry_t *entry,
                          svn_revnum_t revision1,
                          svn_revnum_t revision2,
                          svn_ra_session_t *ra_session,
                          svn_wc_adm_access_t *adm_access,
                          svn_client_ctx_t *ctx,
                          apr_pool_t *result_pool,
                          apr_pool_t *scratch_pool)
{
  /* If we haven't already found CHILD->IMPLICIT_MERGEINFO then
     contact the server to get it. */

  if (child->implicit_mergeinfo)
    return SVN_NO_ERROR;

  if (child_inherits_parent)
    SVN_ERR(inherit_implicit_mergeinfo_from_parent(parent,
                                                   child,
                                                   revision1,
                                                   revision2,
                                                   ra_session,
                                                   adm_access,
                                                   ctx,
                                                   result_pool));
  else
    SVN_ERR(get_full_mergeinfo(NULL,
                               &(child->implicit_mergeinfo),
                               entry, NULL,
                               svn_mergeinfo_inherited,
                               ra_session, child->path,
                               MAX(revision1, revision2),
                               MIN(revision1, revision2),
                               adm_access, ctx, result_pool));

  return SVN_NO_ERROR;
}

/* Helper for calculate_remaining_ranges().

   Initialize CHILD->REMAINING_RANGES to a rangelist representing the
   requested merge of REVISION1:REVISION2 from MERGEINFO_PATH to CHILD->PATH.
   ENTRY is the entry corresponding to CHILD->PATH.

   For forward merges remove any ranges from CHILD->REMAINING_RANGES that
   have already been merged to CHILD->PATH per TARGET_MERGEINFO or
   CHILD->IMPLICIT_MERGEINFO.  For reverse merges remove any ranges from
   CHILD->REMAINING_RANGES that have not alreay been merged to CHILD->PATH.

   CHILD represents a working copy path which is the merge target or one of
   the target's subtrees.  If not NULL, PARENT is CHILD's nearest path-wise
   ancestor - see 'THE CHILDREN_WITH_MERGEINFO ARRAY'.

   If the function needs to consider CHILD->IMPLICIT_MERGEINFO and
   CHILD_INHERITS_IMPLICIT is true, then set CHILD->IMPLICIT_MERGEINFO to the
   mergeinfo inherited from PARENT->IMPLICIT_MERGEINFO.  Otherwise contact
   the repository for CHILD->IMPLICIT_MERGEINFO.

   NOTE: If PARENT is present then this function must have previously been
   called for PARENT, i.e. if populate_remaining_ranges() is calling this
   function for a set of svn_client__merge_path_t* the calls must be made
   in depth-first order.

   MERGEINFO_PATH is the merge source relative to the repository root.

   REVISION1 and REVISION2 describe the merge range requested from
   MERGEINFO_PATH.

   TARGET_MERGEINFO is the CHILD->PATHS's explicit or inherited mergeinfo.
   TARGET_MERGEINFO should be NULL if there is no explicit or inherited
   mergeinfo on CHILD->PATH or an empty hash if CHILD->PATH has empty
   mergeinfo.

   NOTE: This should only be called when honoring mergeinfo.

   NOTE: Like calculate_remaining_ranges() if PARENT is present then this
   function must have previously been called for PARENT.
*/
static svn_error_t *
filter_merged_revisions(svn_client__merge_path_t *parent,
                        svn_client__merge_path_t *child,
                        const svn_wc_entry_t *entry,
                        const char *mergeinfo_path,
                        svn_mergeinfo_t target_mergeinfo,
                        svn_revnum_t revision1,
                        svn_revnum_t revision2,
                        svn_boolean_t child_inherits_implicit,
                        svn_ra_session_t *ra_session,
                        svn_wc_adm_access_t *adm_access,
                        svn_client_ctx_t *ctx,
                        apr_pool_t *pool)
{
  apr_array_header_t *requested_rangelist, *target_rangelist,
    *target_implicit_rangelist, *explicit_rangelist;
  apr_pool_t *subpool = svn_pool_create(pool);

  /* Convert REVISION1 and REVISION2 to a rangelist.

     Note: Talking about a requested merge range's inheritability
     doesn't make much sense, but as we are using svn_merge_range_t
     to describe it we need to pick *something*.  Since all the
     rangelist manipulations in this function either don't consider
     inheritance by default or we are requesting that they don't (i.e.
     svn_rangelist_remove and svn_rangelist_intersect) then we could
     set the inheritability as FALSE, it won't matter either way. */
  requested_rangelist = init_rangelist(revision1, revision2, TRUE, subpool);

  /* Now filter out revisions that have already been merged to CHILD. */

  if (revision1 > revision2) /* This is a reverse merge. */
    {
      apr_array_header_t *added_rangelist, *deleted_rangelist;

      /* The revert range and will need to be reversed for
         our svn_rangelist_* APIs to work properly. */
      SVN_ERR(svn_rangelist_reverse(requested_rangelist, subpool));

      if (target_mergeinfo)
        target_rangelist = apr_hash_get(target_mergeinfo,
                                        mergeinfo_path, APR_HASH_KEY_STRING);
      else
        target_rangelist = NULL;
          
      if (target_rangelist)
        {
          /* Return the intersection of the revs which are both already
             represented by CHILD's explicit or inherited mergeinfo.

             We don't consider inheritance when determining intersecting
             ranges.  If we *did* consider inheritance, then our calculation
             would be wrong.  For example, if the CHILD->REMAINING_RANGES is
             5:3 and TARGET_RANGELIST is r5* (non-inheritable) then the
             intersection would be r4.  And that would be wrong as we clearly
             want to reverse merge both r4 and r5 in this case.  Ignoring the
             ranges' inheritance results in an intersection of r4-5.

             You might be wondering about ENTRY's children, doesn't the above
             imply that we will reverse merge r4-5 from them?  Nope, this is
             safe to do because any path whose parent has non-inheritable
             ranges is always considered a subtree with differing mergeinfo
             even if that path has no explicit mergeinfo prior to the
             merge -- See condition 3 in the doc string for
             merge.c:get_mergeinfo_paths(). */
          SVN_ERR(svn_rangelist_intersect(&explicit_rangelist,
                                          target_rangelist,
                                          requested_rangelist,
                                          FALSE, subpool));
        }
      else
        {
          explicit_rangelist =
            apr_array_make(pool, 0, sizeof(svn_merge_range_t *));
        }

      /* Was any part of the requested reverse merge not accounted for in
         CHILD's explicit or inherited mergeinfo? */
      SVN_ERR(svn_rangelist_diff(&deleted_rangelist, &added_rangelist,
                                 requested_rangelist, explicit_rangelist,
                                 FALSE, subpool));

      if (deleted_rangelist->nelts == 0)
        {
          /* The whole of REVISION1:REVISION2 was represented in CHILD's
             explicit/inherited mergeinfo, allocate CHILD's remaining
             ranges in POOL and then we are done. */
          SVN_ERR(svn_rangelist_reverse(requested_rangelist, subpool));
          child->remaining_ranges = svn_rangelist_dup(requested_rangelist,
                                                      pool);
        }
      else /* We need to check CHILD's implicit mergeinfo. */
        {
          apr_array_header_t *implicit_rangelist;

          SVN_ERR(ensure_implicit_mergeinfo(parent,
                                            child,
                                            child_inherits_implicit,
                                            entry,
                                            revision1,
                                            revision2,
                                            ra_session,
                                            adm_access,
                                            ctx,
                                            pool,
                                            subpool));

          target_implicit_rangelist = apr_hash_get(child->implicit_mergeinfo,
                                                   mergeinfo_path,
                                                   APR_HASH_KEY_STRING);

          if (target_implicit_rangelist)
            SVN_ERR(svn_rangelist_intersect(&implicit_rangelist,
                                            target_implicit_rangelist,
                                            requested_rangelist,
                                            FALSE, pool));
          else
            implicit_rangelist = apr_array_make(subpool, 0,
                                                sizeof(svn_merge_range_t *));

          SVN_ERR(svn_rangelist_merge(&implicit_rangelist,
                                      explicit_rangelist, subpool));
          SVN_ERR(svn_rangelist_reverse(implicit_rangelist, subpool));
          child->remaining_ranges = svn_rangelist_dup(implicit_rangelist,
                                                      pool);
        }
    }
  else /* This is a forward merge */
    {
      if (target_mergeinfo)
        target_rangelist = apr_hash_get(target_mergeinfo, mergeinfo_path,
                                        APR_HASH_KEY_STRING);
      else
        target_rangelist = NULL;

      /* See earlier comment preceeding svn_rangelist_intersect() for
         why we don't consider inheritance here. */
      if (target_rangelist)
        {
          SVN_ERR(svn_rangelist_remove(&explicit_rangelist,
                                       target_rangelist,
                                       requested_rangelist, FALSE, subpool));
        }
      else
        {
          explicit_rangelist = svn_rangelist_dup(requested_rangelist,
                                                 subpool);
        }

      if (explicit_rangelist->nelts == 0)
        {
          child->remaining_ranges =
            apr_array_make(pool, 0, sizeof(svn_merge_range_t *));
        }
      else
/* ### TODO:  Which evil shall we choose?
   ###
   ### If we allow all forward-merges not already found in recorded
   ### mergeinfo, we destroy the ability to, say, merge the whole of a
   ### branch to the trunk while automatically ignoring the revisions
   ### common to both.  That's bad.
   ###
   ### If we allow only forward-merges not found in either recorded
   ### mergeinfo or implicit mergeinfo (natural history), then the
   ### previous scenario works great, but we can't reverse-merge a
   ### previous change made to our line of history and then remake it
   ### (because the reverse-merge will leave no mergeinfo trace, and
   ### the remake-it attempt will still find the original change in
   ### natural mergeinfo.  But you know, that we happen to use 'merge'
   ### for revision undoing is somewhat unnatural anyway, so I'm
   ### finding myself having little interest in caring too much about
   ### this.  That said, if we had a way of storing reverse merge
   ### ranges, we'd be in good shape either way.
*/
#ifdef SVN_MERGE__ALLOW_ALL_FORWARD_MERGES_FROM_SELF
        {
          /* ### Don't consider implicit mergeinfo. */
          child->remaining_ranges = svn_rangelist_dup(explicit_rangelist,
                                                      pool);
        }
#else
        {
          /* Based on CHILD's TARGET_MERGEINFO there are ranges to merge.
             Check CHILD's implicit mergeinfo to see if these remaining
             ranges are represented there. */
          SVN_ERR(ensure_implicit_mergeinfo(parent,
                                            child,
                                            child_inherits_implicit,
                                            entry,
                                            revision1,
                                            revision2,
                                            ra_session,
                                            adm_access,
                                            ctx,
                                            pool,
                                            subpool));

          target_implicit_rangelist = apr_hash_get(child->implicit_mergeinfo,
                                                   mergeinfo_path,
                                                   APR_HASH_KEY_STRING);
          if (target_implicit_rangelist)
            SVN_ERR(svn_rangelist_remove(&(child->remaining_ranges),
                                         target_implicit_rangelist,
                                         explicit_rangelist,
                                         FALSE, pool));
          else
            child->remaining_ranges = svn_rangelist_dup(explicit_rangelist,
                                                        pool);
        }
    }
#endif

  svn_pool_destroy(subpool);
  return SVN_NO_ERROR;
}

/* Helper for do_file_merge and do_directory_merge (by way of
   populate_remaining_ranges() for the latter).

   Determine what portions of URL1@REVISION1 -> URL2@REVISION2 have already
   been merged to CHILD->PATH and populate CHILD->REMAINING_RANGES with the
   ranges that still need merging.

   URL1, REVISION1, URL2, REVISION2, ADM_ACCESS, and CTX are all cascaded
   from the caller's arguments of the same names.  Note that this means URL1,
   REVISION1, URL2, and REVISION2 adhere to the requirements noted in
   `MERGEINFO MERGE SOURCE NORMALIZATION'.

   TARGET_MERGEINFO is the working mergeinfo on CHILD.

   RA_SESSION is the session for, and SOURCE_ROOT_URL is the repository root
   for, the younger of URL1@REVISION1 and URL2@REVISION2.

   If IS_SUBTREE is FALSE then CHILD describes the merge target and the
   requirements around the values of URL1, REVISION1, URL2, and REVISION2
   described in 'MERGEINFO MERGE SOURCE NORMALIZATION' hold.  If IS_SUBTREE
   is TRUE then CHILD describes some subtree of a merge target and these
   normalization conditions do not necessarily hold.  IS_SUBTREE should
   always be FALSE when calling from do_file_merge().

   If the function needs to consider CHILD->IMPLICIT_MERGEINFO and
   CHILD_INHERITS_IMPLICIT is true, then set CHILD->IMPLICIT_MERGEINFO to the
   mergeinfo inherited from PARENT->IMPLICIT_MERGEINFO.  Otherwise contact
   the repository for CHILD->IMPLICIT_MERGEINFO.

   If IS_SUBTREE is FALSE then PARENT is ignored, otherwise PARENT must
   represent the nearest working copy ancestor of CHILD.

   If not null, IMPLICIT_SRC_GAP is the gap, if any, in the natural history
   of URL1@REVISION1:URL2@REVISION2, see merge_cmd_baton_t.implicit_src_gap.

   NOTE: This should only be called when honoring mergeinfo.

   NOTE: If PARENT is present then this function must have previously been
   called for PARENT, i.e. if populate_remaining_ranges() is calling this
   function for a set of svn_client__merge_path_t* the calls must be made
   in depth-first order.

   NOTE: When performing reverse merges, return
   SVN_ERR_CLIENT_NOT_READY_TO_MERGE if URL1@REVISION1, URL2@REVISION2, and
   ENTRY are all on the same line of history but ENTRY-REVISION is older than
   the REVISION1-REVISION2 range, see comment re issue #2973 below.
*/
static svn_error_t *
calculate_remaining_ranges(svn_client__merge_path_t *parent,
                           svn_client__merge_path_t *child,
                           const char *source_root_url,
                           const char *url1,
                           svn_revnum_t revision1,
                           const char *url2,
                           svn_revnum_t revision2,
                           svn_mergeinfo_t target_mergeinfo,
                           apr_array_header_t *implicit_src_gap,
                           svn_boolean_t is_subtree,
                           svn_boolean_t child_inherits_implicit,
                           svn_ra_session_t *ra_session,
                           const svn_wc_entry_t *entry,
                           svn_wc_adm_access_t *adm_access,
                           svn_client_ctx_t *ctx,
                           apr_pool_t *pool)
{
  const char *mergeinfo_path;
  const char *primary_url = (revision1 < revision2) ? url2 : url1;
  svn_mergeinfo_t adjusted_target_mergeinfo = NULL;

  /* Determine which of the requested ranges to consider merging... */
  SVN_ERR(svn_client__path_relative_to_root(&mergeinfo_path, primary_url,
                                            source_root_url, TRUE,
                                            ra_session, NULL, pool));

  /* Consider: CHILD might have explicit mergeinfo '/MERGEINFO_PATH:M-N'
     where M-N fall into the gap in URL1@REVISION1:URL2@REVISION2's natural
     history allowed by 'MERGEINFO MERGE SOURCE NORMALIZATION'.  If this is
     the case, then '/MERGEINFO_PATH:N' actually refers to a completely
     different line of history than URL1@REVISION1:URL2@REVISION2 and we
     *don't* want to consider those revisions merged already. */
  if (implicit_src_gap && child->pre_merge_mergeinfo)
    {
      apr_array_header_t *explicit_mergeinfo_gap_ranges =
        apr_hash_get(child->pre_merge_mergeinfo, mergeinfo_path,
                     APR_HASH_KEY_STRING);

      if (explicit_mergeinfo_gap_ranges)
        {
          svn_mergeinfo_t gap_mergeinfo = apr_hash_make(pool);
          
          apr_hash_set(gap_mergeinfo, mergeinfo_path, APR_HASH_KEY_STRING,
                       implicit_src_gap);
          SVN_ERR(svn_mergeinfo__remove2(&adjusted_target_mergeinfo,
                                         gap_mergeinfo, target_mergeinfo,
                                         FALSE, pool, pool));
        }
    }
  else
    {
      adjusted_target_mergeinfo = target_mergeinfo;
    }

  /* Initialize CHILD->REMAINING_RANGES and filter out revisions already
     merged (or, in the case of reverse merges, ranges not yet merged). */
  SVN_ERR(filter_merged_revisions(parent, child, entry, mergeinfo_path,
                                  adjusted_target_mergeinfo,
                                  revision1, revision2,
                                  child_inherits_implicit,
                                  ra_session, adm_access, ctx, pool));

  if (is_subtree)
    {
      apr_array_header_t *deleted_rangelist, *added_rangelist;
      svn_boolean_t is_rollback = revision2 < revision1;

      /* If this is a reverse merge reorder CHILD->REMAINING_RANGES
         so it will work with the svn_rangelist_diff API. */
      if (is_rollback)
        {
          SVN_ERR(svn_rangelist_reverse(child->remaining_ranges, pool));
          SVN_ERR(svn_rangelist_reverse(parent->remaining_ranges, pool));
        }

      SVN_ERR(svn_rangelist_diff(&deleted_rangelist, &added_rangelist,
                                 child->remaining_ranges,
                                 parent->remaining_ranges,
                                 TRUE, pool));

      if (is_rollback)
        {
          SVN_ERR(svn_rangelist_reverse(child->remaining_ranges, pool));
          SVN_ERR(svn_rangelist_reverse(parent->remaining_ranges, pool));
        }

      /* If CHILD is the merge target we then know that primary_url,
         REVISION1, and REVISION2 are provided by normalize_merge_sources()
         -- see 'MERGEINFO MERGE SOURCE NORMALIZATION'.  Due to this
         normalization we know that primary_url@REVISION1 and
         primary_url@REVISION2 describe an unbroken line of history such
         that the entire range described by REVISION1:REVISION2 can
         potentially be merged to CHILD.  So we simply convert REVISION1 and
         REVISION2 to a rangelist and proceed to the filtering of merged
         revisions.

         But if CHILD is a subtree we don't have the same guarantees about
         primary_url, REVISION1, and REVISION2 as we do for the merge target.
         primary_url@REVSION1 and/or primary_url@REVSION2 might not exist.

         If one or both doesn't exist, then adjust CHILD->REMAINING_RANGES
         such that we don't later try to describe invalid subtrees in
         drive_merge_report_editor(), as that will break the merge.
         If CHILD has the same remaining ranges as PARENT however, then
         there is no need to make these adjustments, since
         drive_merge_report_editor() won't attempt to describe CHILD in this
         case, see the 'Note' in drive_merge_report_editor's docstring. */
      if (deleted_rangelist->nelts || added_rangelist->nelts)
        SVN_ERR(adjust_deleted_subtree_ranges(child, parent,
                                              mergeinfo_path,
                                              revision1, revision2,
                                              primary_url, ra_session,
                                              ctx, pool));
    }

  /* Issue #2973 -- from the continuing series of "Why, since the advent of
     merge tracking, allowing merges into mixed rev and locally modified
     working copies isn't simple and could be considered downright evil".

     If reverse merging a range to the WC path represented by ENTRY, from
     that path's own history, where the path inherits no locally modified
     mergeinfo from its WC parents (i.e. there is no uncommitted merge to
     the WC), and the path's working revision is older than the range, then
     the merge will always be a no-op.  This is because we only allow reverse
     merges of ranges in the path's explicit or natural mergeinfo and a
     reverse merge from the path's future history obviously isn't going to be
     in either, hence the no-op.

     The problem is two-fold.  First, in a mixed rev WC, the change we
     want to revert might actually be to some child of the target path
     which is at a younger working revision.  Sure, we can merge directly
     to that child or update the WC or even use --ignore-ancestry and then
     successfully run the reverse merge, but that gets to the second
     problem: Those courses of action are not very obvious.  Before 1.5 if
     a user committed a change that didn't touch the commit target, then
     immediately decided to revert that change via a reverse merge it would
     just DTRT.  But with the advent of merge tracking the user gets a no-op.

     So in the name of user friendliness, return an error suggesting a helpful
     course of action.
  */
  if (((child->remaining_ranges)->nelts == 0)
      && (revision2 < revision1)
      && (entry->revision <= revision2))
    {
      /* Hmmm, an inoperative reverse merge from the "future".  If it is
         from our own future return a helpful error. */
      svn_error_t *err;
      const char *start_url;
      svn_opt_revision_t requested, unspec, pegrev, *start_revision;
      unspec.kind = svn_opt_revision_unspecified;
      requested.kind = svn_opt_revision_number;
      requested.value.number = entry->revision;
      pegrev.kind = svn_opt_revision_number;
      pegrev.value.number = revision1;

      err = svn_client__repos_locations(&start_url, &start_revision,
                                        NULL, NULL, ra_session, url1,
                                        &pegrev, &requested,
                                        &unspec, ctx, pool);
      if (err)
        {
          if (err->apr_err == SVN_ERR_FS_NOT_FOUND
              || err->apr_err == SVN_ERR_CLIENT_UNRELATED_RESOURCES)
            svn_error_clear(err);
          else
            return err;
        }
      else if (strcmp(start_url, entry->url) == 0)
        {
          return svn_error_create(SVN_ERR_CLIENT_NOT_READY_TO_MERGE, NULL,
                                  _("Cannot reverse-merge a range from a "
                                    "path's own future history; try "
                                    "updating first"));
        }
    }

  return SVN_NO_ERROR;
}

/* Helper for populate_remaining_ranges().

   URL1, REVISION1, URL2, REVISION2, RA_SESSION, MERGE_SRC_CANON_PATH,
   and MERGE_B are all cascaded from the arguments of the same name in
   populate_remaining_ranges().  MERGE_SRC_CANON_PATH is the absolute
   repository path of URL2.

   Note: The following comments assume a forward merge, i.e.
   REVISION1 < REVISION2.  If this is a reverse merge then all the following
   comments still apply, but with URL1 switched with URL2 and REVISION1
   switched with REVISION2.

   Like populate_remaining_ranges(), URL1@REVISION1:URL2@REVISION2 must adhere
   to the restrictions documented in 'MERGEINFO MERGE SOURCE NORMALIZATION'.
   These restrictions allow for a *single* gap, URL@GAP_REV1:URL2@GAP_REV2,
   (where REVISION1 < GAP_REV1 <= GAP_REV2 < REVISION2) in
   URL1@REVISION1:URL2@REVISION2 if URL2@REVISION2 was copied from
   URL1@REVISION1.  If such a gap exists, set *GAP_START and *GAP_END to the
   starting and ending revisions of the gap.  Otherwise set both to
   SVN_INVALID_REVNUM.

   For example, if the natural history of URL@2:URL@9 is 'trunk/:2,7-9' this
   would indicate that trunk@7 was copied from trunk@2.  This function would
   return GAP_START:GAP_END of 2:6 in this case.  Note that a path 'trunk'
   might exist at r3-6, but it would not be on the same line of history as
   trunk@9. */
static svn_error_t *
find_gaps_in_merge_source_history(svn_revnum_t *gap_start,
                                  svn_revnum_t *gap_end,
                                  const char *merge_src_canon_path,
                                  const char *url1,
                                  svn_revnum_t revision1,
                                  const char *url2,
                                  svn_revnum_t revision2,
                                  svn_ra_session_t *ra_session,
                                  merge_cmd_baton_t *merge_b,
                                  apr_pool_t *scratch_pool,
                                  apr_pool_t *result_pool)
{
  svn_mergeinfo_t implicit_src_mergeinfo;
  svn_opt_revision_t peg_rev;
  svn_revnum_t young_rev = MAX(revision1, revision2);
  svn_revnum_t old_rev = MIN(revision1, revision2);
  apr_array_header_t *rangelist;
  const char *url = (revision2 < revision1) ? url1 : url2;

  /* Start by assuming there is no gap. */
  *gap_start = *gap_end = SVN_INVALID_REVNUM;

  /* Get URL1@REVISION1:URL2@REVISION2 as mergeinfo. */
  peg_rev.kind = svn_opt_revision_number;
  peg_rev.value.number = young_rev;
  SVN_ERR(svn_client__get_history_as_mergeinfo(&implicit_src_mergeinfo, url,
                                               &peg_rev, young_rev, old_rev,
                                               ra_session, NULL,
                                               merge_b->ctx, scratch_pool));

  rangelist = apr_hash_get(implicit_src_mergeinfo,
                           merge_src_canon_path,
                           APR_HASH_KEY_STRING);

  if (rangelist) /* ### Can we ever not find a rangelist? */
    {
      /* A gap in natural history can result from either a copy or
         a rename.  If from a copy then history as mergeinfo will look
         something like this:
         
           '/trunk:X,Y-Z'
         
         If from a rename it will look like this:

           '/trunk_old_name:X'
           '/trunk_new_name:Y-Z'
      
        In both cases the gap, if it exists, is M-N, where M = X + 1 and
        N = Y - 1.

        Note that per the rules of 'MERGEINFO MERGE SOURCE NORMALIZATION' we
        should never have multiple gaps, e.g. if we see anything like the
        following then something is quite wrong:

            '/trunk_old_name:A,B-C'
            '/trunk_new_name:D-E'
      */

      if (rangelist->nelts > 1) /* Copy */
        {
          /* As mentioned above, multiple gaps *shouldn't* be possible. */
          SVN_ERR_ASSERT(apr_hash_count(implicit_src_mergeinfo) == 1);

          *gap_start = MIN(revision1, revision2);
          *gap_end = (APR_ARRAY_IDX(rangelist,
                                    rangelist->nelts - 1,
                                    svn_merge_range_t *))->start;
        }
      else if (apr_hash_count(implicit_src_mergeinfo) > 1) /* Rename */
        {
          apr_array_header_t *requested_rangelist =
            init_rangelist(MIN(revision1, revision2),
                           MAX(revision1, revision2),
                           TRUE, scratch_pool);
          apr_array_header_t *implicit_rangelist =
            apr_array_make(scratch_pool, 2, sizeof(svn_merge_range_t *));
          apr_array_header_t *gap_rangelist;
          apr_hash_index_t *hi;

          for (hi = apr_hash_first(scratch_pool, implicit_src_mergeinfo);
               hi;
               hi = apr_hash_next(hi))
            {
              void *value;

              apr_hash_this(hi, NULL, NULL, &value);
              SVN_ERR(svn_rangelist_merge(&implicit_rangelist, value,
                                          scratch_pool));
            }
          SVN_ERR(svn_rangelist_remove(&gap_rangelist, implicit_rangelist,
                                       requested_rangelist, FALSE,
                                       scratch_pool));

          /* If there is anything left it is the gap. */
          if (gap_rangelist->nelts)
            {
              svn_merge_range_t *gap_range =
                APR_ARRAY_IDX(gap_rangelist, 0, svn_merge_range_t *);

              *gap_start = gap_range->start;
              *gap_end = gap_range->end;
            }
        }
    }

  return SVN_NO_ERROR;
}

/* Helper for do_directory_merge().

   For each child in CHILDREN_WITH_MERGEINFO, populate that
   child's remaining_ranges list.  CHILDREN_WITH_MERGEINFO is expected
   to be sorted in depth first order and each child must be processed in
   that order.  All persistent allocations are from POOL.  The inheritability
   of all calculated ranges is TRUE.

   If HONOR_MERGEINFO is set, this function will actually try to be
   intelligent about populating remaining_ranges list.  Otherwise, it
   will claim that each child has a single remaining range, from
   revision1, to revision2.

   Note that if REVISION1 > REVISION2, then each child's remaining_ranges
   member does not adhere to the API rules for rangelists described in
   svn_mergeinfo.h -- See svn_client__merge_path_t.

   See `MERGEINFO MERGE SOURCE NORMALIZATION' for more requirements
   around the values of URL1, REVISION1, URL2, and REVISION2.
*/
static svn_error_t *
populate_remaining_ranges(apr_array_header_t *children_with_mergeinfo,
                          const char *source_root_url,
                          const char *url1,
                          svn_revnum_t revision1,
                          const char *url2,
                          svn_revnum_t revision2,
                          svn_boolean_t honor_mergeinfo,
                          svn_ra_session_t *ra_session,
                          const char *parent_merge_src_canon_path,
                          svn_wc_adm_access_t *adm_access,
                          merge_cmd_baton_t *merge_b,
                          apr_pool_t *pool)
{
  apr_pool_t *iterpool;
  int merge_target_len = strlen(merge_b->target);
  int i;
  svn_revnum_t gap_start, gap_end;
  svn_boolean_t child_inherits_implicit;
  svn_client__merge_path_t *parent;
  int parent_index;

  iterpool = svn_pool_create(pool);

  /* If we aren't honoring mergeinfo or this is a --record-only merge,
     we'll make quick work of this by simply adding dummy REVISION1:REVISION2
     ranges for all children. */
  if (! honor_mergeinfo || merge_b->record_only)
    {
      for (i = 0; i < children_with_mergeinfo->nelts; i++)
        {
          svn_client__merge_path_t *child =
            APR_ARRAY_IDX(children_with_mergeinfo, i,
                          svn_client__merge_path_t *);
          svn_merge_range_t *range = apr_pcalloc(pool, sizeof(*range));

          range->start = revision1;
          range->end = revision2;
          range->inheritable = TRUE;

          child->remaining_ranges =
            apr_array_make(pool, 1, sizeof(svn_merge_range_t *));
          APR_ARRAY_PUSH(child->remaining_ranges, svn_merge_range_t *) = range;

          parent = NULL;
          svn_pool_clear(iterpool);

          /* Issue #3646 'record-only merges create self-referential
             mergeinfo'.  Get the merge target's implicit mergeinfo (natural
             history).  We'll use it later to avoid setting self-referential
             mergeinfo -- see filter_natural_history_from_mergeinfo(). */
          if (i == 0) /* First item is always the merge target. */
            {
              SVN_ERR(get_full_mergeinfo(NULL, &(child->implicit_mergeinfo),
                                         NULL, NULL,
                                         svn_mergeinfo_inherited, ra_session,
                                         child->path,
                                         MAX(revision1, revision2),
                                         MIN(revision1, revision2),
                                         adm_access, merge_b->ctx, pool));
            }
          else
            {
              const svn_wc_entry_t *entry;
            
              /* Issue #3443 - Subtrees of the merge target can inherit
                 their parent's implicit mergeinfo in most cases. */
              parent_index = find_nearest_ancestor(children_with_mergeinfo,
                                                   FALSE, child->path);
              parent = APR_ARRAY_IDX(children_with_mergeinfo, parent_index,
                                     svn_client__merge_path_t *);
              /* If CHILD is a subtree then its parent must be in
                 CHILDREN_WITH_MERGEINFO, see the global comment
                 'THE CHILDREN_WITH_MERGEINFO ARRAY'. */
              SVN_ERR_ASSERT(parent);
 
              child_inherits_implicit = (parent && !child->switched);
              SVN_ERR(svn_wc_entry(&entry, child->path, adm_access, TRUE, pool));
              SVN_ERR(ensure_implicit_mergeinfo(parent, child,
                                                child_inherits_implicit,
                                                entry, revision1, revision2,
                                                ra_session, adm_access,
                                                merge_b->ctx, pool, iterpool));
            }
        }
      return SVN_NO_ERROR;
    }

  /* If, in the merge source's history, there was a copy from a older
     revision, then URL2 won't exist at some range M:N, where
     REVISION1 < M < N < REVISION2. The rules of 'MERGEINFO MERGE SOURCE
     NORMALIZATION' allow this, but we must ignore these gaps when
     calculating what ranges remain to be merged from
     URL1@REVISION1:URL2@REVISION2.  If we don't and try to merge any part
     of URL2@M:URL2@N we would break the editor since no part of that
     actually exists.  See http://svn.haxx.se/dev/archive-2008-11/0618.shtml.

     Find the gaps in the merge target's history, if any.  Eventually
     we will adjust CHILD->REMAINING_RANGES such that we don't describe
     non-existent paths to the editor. */
  SVN_ERR(find_gaps_in_merge_source_history(&gap_start, &gap_end,
                                            parent_merge_src_canon_path,
                                            url1, revision1,
                                            url2, revision2,
                                            ra_session, merge_b,
                                            iterpool, pool));

  /* Stash any gap in the merge command baton, we'll need it later when
     recording mergeinfo describing this merge. */
  if (SVN_IS_VALID_REVNUM(gap_start) && SVN_IS_VALID_REVNUM(gap_end))
    merge_b->implicit_src_gap = init_rangelist(gap_start, gap_end, TRUE,
                                               pool);

  for (i = 0; i < children_with_mergeinfo->nelts; i++)
    {
      const char *child_repos_path;
      const svn_wc_entry_t *child_entry;
      const char *child_url1, *child_url2;
      svn_client__merge_path_t *child =
        APR_ARRAY_IDX(children_with_mergeinfo, i, svn_client__merge_path_t *);

      parent = NULL;

      /* If the path is absent don't do subtree merge either. */
      SVN_ERR_ASSERT(child);
      if (child->absent)
        continue;

      svn_pool_clear(iterpool);

      if (strlen(child->path) == merge_target_len)
        child_repos_path = "";
      else
        child_repos_path = child->path +
          (merge_target_len ? merge_target_len + 1 : 0);
      child_url1 = svn_path_url_add_component2(url1, child_repos_path,
                                               iterpool);
      child_url2 = svn_path_url_add_component2(url2, child_repos_path,
                                               iterpool);

      SVN_ERR(svn_wc__entry_versioned(&child_entry, child->path, adm_access,
                                      FALSE, iterpool));

      /* Get the explicit/inherited mergeinfo for CHILD.  If CHILD is the
         merge target then also get its implicit mergeinfo.  Otherwise defer
         this until we know it is absolutely necessary, since it requires an
         expensive round trip communication with the server. */
      SVN_ERR(get_full_mergeinfo(
        &(child->pre_merge_mergeinfo),
        /* Get implicit only for merge target. */
        (i == 0) ? &(child->implicit_mergeinfo) : NULL,
        child_entry,
        &(child->indirect_mergeinfo),
        svn_mergeinfo_inherited, ra_session,
        child->path,
        MAX(revision1, revision2),
        MIN(revision1, revision2),
        adm_access, merge_b->ctx, pool));

      /* If CHILD isn't the merge target find its parent. */
      if (i > 0)
        {
          parent_index = find_nearest_ancestor(children_with_mergeinfo,
                                                   FALSE, child->path);
          parent = APR_ARRAY_IDX(children_with_mergeinfo, parent_index,
                                 svn_client__merge_path_t *);
          /* If CHILD is a subtree then its parent must be in
             CHILDREN_WITH_MERGEINFO, see the global comment
             'THE CHILDREN_WITH_MERGEINFO ARRAY'. */
          SVN_ERR_ASSERT(parent);
        }

      /* Issue #3443 - Can CHILD inherit PARENT's implicit mergeinfo, saving
         us from having to ask the repos?  The only time we can't do this is if
         CHILD is the merge target and so there is no PARENT to inherit from
         or if CHILD is the root of a switched subtree, in which case PARENT
         exists but is not CHILD's repository parent. */
      child_inherits_implicit = (parent && !child->switched);

      SVN_ERR(calculate_remaining_ranges(parent, child,
                                         source_root_url,
                                         child_url1, revision1,
                                         child_url2, revision2,
                                         child->pre_merge_mergeinfo,
                                         merge_b->implicit_src_gap,
                                         i > 0, /* is subtree */
                                         child_inherits_implicit,
                                         ra_session, child_entry,
                                         adm_access, merge_b->ctx,
                                         pool));

      /* Deal with any gap in URL1@REVISION1:URL2@REVISION2's natural history.

         If the gap is a proper subset of CHILD->REMAINING_RANGES then we can
         safely ignore it since we won't describe this path/rev pair.
         
         If the gap exactly matches or is a superset of a range in
         CHILD->REMAINING_RANGES then we must remove that range so we don't
         attempt to describe non-existent paths via the reporter, this will
         break the editor and our merge.

         If the gap adjoins or overlaps a range in CHILD->REMAINING_RANGES
         then we must *add* the gap so we span the missing revisions. */
      if (child->remaining_ranges->nelts
          && merge_b->implicit_src_gap)
        {
          int j;
          svn_revnum_t start, end;
          svn_boolean_t proper_subset = FALSE;
          svn_boolean_t equals = FALSE;
          svn_boolean_t overlaps_or_adjoins = FALSE;

          /* If this is a reverse merge reorder CHILD->REMAINING_RANGES
              so it will work with the svn_rangelist_* APIs below. */
          if (revision1 > revision2)
            svn_rangelist_reverse(child->remaining_ranges, iterpool);

          for (j = 0; j < child->remaining_ranges->nelts; j++)
            {
              start = (APR_ARRAY_IDX(child->remaining_ranges, j,
                                     svn_merge_range_t *))->start;
              end = (APR_ARRAY_IDX(child->remaining_ranges, j,
                                   svn_merge_range_t *))->end;
              if ((start <= gap_start && gap_end < end)
                  || (start < gap_start && gap_end <= end))
                {
                  proper_subset = TRUE;
                  break;
                }
              else if ((gap_start == start) && (end == gap_end))
                {
                  equals = TRUE;
                  break;
                }
              else if (gap_start <= end && start <= gap_end)  /* intersect */
                {
                  overlaps_or_adjoins = TRUE;
                  break;
                }
            }

          if (!proper_subset)
            {
              /* We need to make adjustements.  Remove from, or add the gap
                 to, CHILD->REMAINING_RANGES as appropriate. */

              if (overlaps_or_adjoins)
                SVN_ERR(svn_rangelist_merge(&(child->remaining_ranges),
                                            merge_b->implicit_src_gap,
                                            pool));
              else /* equals == TRUE */
                SVN_ERR(svn_rangelist_remove(&(child->remaining_ranges),
                                             merge_b->implicit_src_gap,
                                             child->remaining_ranges, FALSE,
                                             pool));
            }

          if (revision1 > revision2) /* Reverse merge */
            svn_rangelist_reverse(child->remaining_ranges, iterpool);
        }
    }

  svn_pool_destroy(iterpool);
  return SVN_NO_ERROR;
}


/*-----------------------------------------------------------------------*/

/*** Other Helper Functions ***/

/* Helper for record_mergeinfo_for_dir_merge().

   Adjust, in place, the inheritability of the ranges in RANGELIST to
   describe a merge of RANGELIST into WC_WCPATH at depth DEPTH.  ENTRY
   is the entry for WC_PATH.
   
   WC_PATH_IS_MERGE_TARGET is true if WC_PATH is the target of the merge,
   otherwise WC_PATH is a subtree.

   WC_PATH_HAS_MISSING_CHILD is true if WC_PATH is missing an immediate child
   because the child is switched or absent from the WC, or due to a sparse
   checkout -- see get_mergeinfo_paths(). */
static svn_error_t *
calculate_merge_inheritance(apr_array_header_t *rangelist,
                            const char *wc_path,
                            const svn_wc_entry_t *entry,
                            svn_boolean_t wc_path_is_merge_target,
                            svn_boolean_t wc_path_has_missing_child,
                            svn_depth_t depth)
{
  if (entry->kind == svn_node_file)
    {
      /* Files *never* have non-inheritable mergeinfo. */
      svn_rangelist__set_inheritance(rangelist, TRUE);
    }
  else if (entry->kind == svn_node_dir)
    {
      if (wc_path_is_merge_target)
        {
          if (wc_path_has_missing_child
              || depth == svn_depth_files
              || depth == svn_depth_empty)
            svn_rangelist__set_inheritance(rangelist, FALSE);
          else /* depth == svn_depth_files || depth == svn_depth_empty */
            svn_rangelist__set_inheritance(rangelist, TRUE);
        }
      else /* WC_PATH is a directory subtree of the target. */
        {
          if (wc_path_has_missing_child
              || depth == svn_depth_immediates)
            svn_rangelist__set_inheritance(rangelist, FALSE);
          else /* depth == infinity */
            svn_rangelist__set_inheritance(rangelist, TRUE);  
        }
    }
  return SVN_NO_ERROR;
}

/* Calculate the new mergeinfo for the target tree rooted at TARGET_WCPATH
   based on MERGES (a mapping of WC paths to rangelists representing
   a merge from the source REPOS_REL_PATH).

   If RESULT_CATALOG is NULL, then record the new mergeinfo in the WC (at,
   and possibly below, TARGET_WCPATH).

   If RESULT_CATALOG is not NULL, then don't record the new mergeinfo on the
   WC, but instead record it in RESULT_CATALOG, with key TARGET_WCPATH and the
   value the new mergeinfo for that path.  If TARGET_WCPATH is already
   present in RESULT_CATALOG, then merge the new mergeinfo together with
   the existing mergeinfo and store the result in RESULT_CATALOG.
   Allocate additions to RESULT_CATALOG in pool which RESULT_CATALOG was
   created in. */
static svn_error_t *
update_wc_mergeinfo(svn_mergeinfo_catalog_t result_catalog,
                    const char *target_wcpath, const svn_wc_entry_t *entry,
                    const char *repos_rel_path, apr_hash_t *merges,
                    svn_boolean_t is_rollback,
                    svn_wc_adm_access_t *adm_access,
                    svn_client_ctx_t *ctx, apr_pool_t *pool)
{
  apr_pool_t *subpool = svn_pool_create(pool);
  const char *rel_path;
  svn_mergeinfo_catalog_t mergeinfo;
  apr_hash_index_t *hi;

  /* Combine the mergeinfo for the revision range just merged into
     the WC with its on-disk mergeinfo. */
  for (hi = apr_hash_first(pool, merges); hi; hi = apr_hash_next(hi))
    {
      const void *key;
      void *value;
      const char *path;
      apr_array_header_t *ranges, *rangelist;
      size_t len;
      svn_error_t *err;

      svn_pool_clear(subpool);

      apr_hash_this(hi, &key, NULL, &value);
      path = key;
      ranges = value;

      /* As some of the merges may've changed the WC's mergeinfo, get
         a fresh copy before using it to update the WC's mergeinfo. */
      err = svn_client__parse_mergeinfo(&mergeinfo, entry, path, FALSE,
                                        adm_access, ctx, subpool);
      /* If a directory PATH was skipped because it is missing or was
         obstructed by an unversioned item then there's nothing we can
         do with that, so skip it. */
      if (err)
        {
          if (err->apr_err == SVN_ERR_WC_NOT_LOCKED)
            {
              svn_error_clear(err);
              continue;
            }
          else
            {
              return err;
            }
        }

      /* If we are attempting to set empty revision range override mergeinfo
         on a path with no explicit mergeinfo, we first need the pristine
         mergeinfo that path inherits. */
      if (mergeinfo == NULL && ranges->nelts == 0)
        {
          svn_boolean_t inherited;
          SVN_ERR(svn_client__get_wc_mergeinfo(&mergeinfo, &inherited, TRUE,
                                               svn_mergeinfo_nearest_ancestor,
                                               entry, path, NULL, NULL,
                                               adm_access, ctx, subpool));
        }

      if (mergeinfo == NULL)
        mergeinfo = apr_hash_make(subpool);

      /* ASSUMPTION: "target_wcpath" is always both a parent and
         prefix of "path". */
      len = strlen(target_wcpath);
      if (len < strlen(path))
        {
          const char *path_relative_to_target = len?(path + len + 1):(path);
          rel_path = apr_pstrcat(subpool, repos_rel_path, "/",
                                 path_relative_to_target, NULL);
        }
      else
        rel_path = repos_rel_path;
      rangelist = apr_hash_get(mergeinfo, rel_path, APR_HASH_KEY_STRING);
      if (rangelist == NULL)
        rangelist = apr_array_make(subpool, 0, sizeof(svn_merge_range_t *));

      if (is_rollback)
        {
          ranges = svn_rangelist_dup(ranges, subpool);
          SVN_ERR(svn_rangelist_reverse(ranges, subpool));
          SVN_ERR(svn_rangelist_remove(&rangelist, ranges, rangelist,
                                       FALSE,
                                       subpool));
        }
      else
        {
          SVN_ERR(svn_rangelist_merge(&rangelist, ranges,
                                      subpool));
        }
      /* Update the mergeinfo by adjusting the path's rangelist. */
      apr_hash_set(mergeinfo, rel_path, APR_HASH_KEY_STRING, rangelist);

      if (is_rollback && apr_hash_count(mergeinfo) == 0)
        mergeinfo = NULL;

      svn_mergeinfo__remove_empty_rangelists(mergeinfo, pool);

      if (result_catalog)
        {
          svn_mergeinfo_t existing_mergeinfo =
            apr_hash_get(result_catalog, target_wcpath, APR_HASH_KEY_STRING);
          apr_pool_t *result_catalog_pool = apr_hash_pool_get(result_catalog);

          if (existing_mergeinfo)
            SVN_ERR(svn_mergeinfo_merge(mergeinfo, existing_mergeinfo,
                                        result_catalog_pool));
          apr_hash_set(result_catalog,
                       apr_pstrdup(result_catalog_pool, target_wcpath),
                       APR_HASH_KEY_STRING,
                       svn_mergeinfo_dup(mergeinfo, result_catalog_pool));
        }
      else
        {
          err = svn_client__record_wc_mergeinfo(path, mergeinfo,
                                                adm_access, subpool);

          if (err && err->apr_err == SVN_ERR_ENTRY_NOT_FOUND)
            {
              /* PATH isn't just missing, it's not even versioned as far
                 as this working copy knows.  But it was included in
                 MERGES, which means that the server knows about it.
                 Likely we don't have access to the source due to authz
                 restrictions.  For now just clear the error and
                 continue...

                 ### TODO:  Set non-inheritable mergeinfo on PATH's immediate
                 ### parent and normal mergeinfo on PATH's siblings which we
                 ### do have access to. */
              svn_error_clear(err);
            }
          else
            SVN_ERR(err);
        }
    }

  svn_pool_destroy(subpool);
  return SVN_NO_ERROR;
}

/* Helper for record_mergeinfo_for_dir_merge().

   Record override mergeinfo on any paths skipped during a merge.

   Set empty mergeinfo on each path in NOTIFY_B->SKIPPED_PATHS so the path
   does not incorrectly inherit mergeinfo that will later be describing
   the merge.

   MERGEINFO_PATH, ADM_ACCESS, NOTIFY_B, and MERGE_B are all cascased from
   arguments of the same name in the caller.

   TARGET_ENTRY is the entry from MERGE_B->TARGET.  IS_ROLLBACK is true if
   the caller is recording a reverse merge and false otherwise.  RANGELIST
   is the set of revisions being merged from MERGEINFO_PATH to
   MERGE_B->TARGET. */
static svn_error_t *
record_skips(const char *mergeinfo_path,
             const svn_wc_entry_t *target_entry,
             apr_array_header_t *rangelist,
             svn_boolean_t is_rollback,
             svn_wc_adm_access_t *adm_access,
             notification_receiver_baton_t *notify_b,
             merge_cmd_baton_t *merge_b,
             apr_pool_t *pool)
{
  apr_size_t nbr_skips = (notify_b->skipped_paths != NULL ?
                          apr_hash_count(notify_b->skipped_paths) : 0);

  if (nbr_skips > 0)
    {
      apr_hash_index_t *hi;
      apr_hash_t *merges = apr_hash_make(pool);

      /* Override the mergeinfo for child paths which weren't
         actually merged. */
      for (hi = apr_hash_first(NULL, notify_b->skipped_paths); hi;
           hi = apr_hash_next(hi))
        {
          const void *skipped_path;
          svn_wc_status2_t *status;

          apr_hash_this(hi, &skipped_path, NULL, NULL);

          /* Before we override, make sure this is a versioned path, it
             might be an unversioned obstruction. */
          SVN_ERR(svn_wc_status2(&status, skipped_path, adm_access, pool));
          if (status->text_status == svn_wc_status_none
              || status->text_status == svn_wc_status_unversioned)
            continue;

          /* Add an empty range list for this path.

             ### TODO: This works fine for a file path skipped because it is
             ### missing as long as the file's parent directory is present.
             ### But missing directory paths skipped are not handled yet,
             ### see issue #2915.
             
             ### TODO: An empty range is fine if the skipped path doesn't
             ### inherit any mergeinfo from a parent, but if it does
             ### we need to account for that.  See issue #3440
             ### http://subversion.tigris.org/issues/show_bug.cgi?id=3440. */
          apr_hash_set(merges, (const char *) skipped_path,
                       APR_HASH_KEY_STRING,
                       apr_array_make(pool, 0, sizeof(svn_merge_range_t)));

          if (nbr_skips < notify_b->nbr_notifications)
            /* ### Use RANGELIST as the mergeinfo for all children of
               ### this path which were not also explicitly
               ### skipped? */
            ;
        }
      SVN_ERR(update_wc_mergeinfo(NULL, merge_b->target, target_entry,
                                  mergeinfo_path, merges,
                                  is_rollback, adm_access,
                                  merge_b->ctx, pool));
    }
  return SVN_NO_ERROR;
}

/* Create and return an error structure appropriate for the unmerged
   revisions range(s). */
static APR_INLINE svn_error_t *
make_merge_conflict_error(const char *target_wcpath,
                          svn_merge_range_t *r,
                          apr_pool_t *pool)
{
  return svn_error_createf
    (SVN_ERR_WC_FOUND_CONFLICT, NULL,
     _("One or more conflicts were produced while merging r%ld:%ld into\n"
       "'%s' --\n"
       "resolve all conflicts and rerun the merge to apply the remaining\n"
       "unmerged revisions"),
     r->start, r->end, svn_path_local_style(target_wcpath, pool));
}

/* Remove the element at IDX from the array ARR.
   If IDX is not a valid element of ARR do nothing. */
static void
remove_element_from_array(apr_array_header_t *arr,
                          int idx)
{
  /* Do we have a valid index? */
  if (idx >= 0 && idx < arr->nelts)
    {
      if (idx == (arr->nelts - 1))
        {
          /* Deleting the last or only element in an array is easy. */
          apr_array_pop(arr);
        }
      else
        {
          memmove(arr->elts + arr->elt_size * idx,
                  arr->elts + arr->elt_size * (idx + 1),
                  arr->elt_size * (arr->nelts - 1 - idx));
          --(arr->nelts);
        }
    }
}

/* Helper for do_directory_merge().

   TARGET_WCPATH is a directory and CHILDREN_WITH_MERGEINFO is filled
   with paths (svn_client__merge_path_t *) arranged in depth first order,
   which have mergeinfo set on them or meet one of the other criteria
   defined in get_mergeinfo_paths().  Remove any paths absent from disk
   or scheduled for deletion from CHILDREN_WITH_MERGEINFO which are equal to
   or are descendants of TARGET_WCPATH by setting those children to NULL.
   Also remove the path from the NOTIFY_B->SKIPPED_PATHS hash. */
static void
remove_absent_children(const char *target_wcpath,
                       apr_array_header_t *children_with_mergeinfo,
                       notification_receiver_baton_t *notify_b)
{
  /* Before we try to override mergeinfo for skipped paths, make sure
     the path isn't absent due to authz restrictions, because there's
     nothing we can do about those. */
  int i;
  for (i = 0; i < children_with_mergeinfo->nelts; i++)
    {
      svn_client__merge_path_t *child =
        APR_ARRAY_IDX(children_with_mergeinfo,
                      i, svn_client__merge_path_t *);
      if ((child->absent || child->scheduled_for_deletion)
          && svn_path_is_ancestor(target_wcpath, child->path))
        {
          if (notify_b->skipped_paths)
            apr_hash_set(notify_b->skipped_paths, child->path,
              APR_HASH_KEY_STRING, NULL);
          remove_element_from_array(children_with_mergeinfo, i--);
        }
    }
}

/* Helper for do_directory_merge() to handle the case were a merge editor
   drive removes explicit mergeinfo from a subtree of the merge target.

   MERGE_B, NOTIFY_B are cascaded from the arguments of the same name in
   do_directory_merge().  If MERGE_B->DRY_RUN is true do nothing, if it is
   false then for each path (if any) in MERGE_B->PATHS_WITH_DELETED_MERGEINFO
   remove that path from NOTIFY_B->CHILDREN_WITH_MERGEINFO by setting that
   child to NULL.  The one exception is for the merge target itself,
   MERGE_B->TARGET, this must always be present in
   NOTIFY_B->CHILDREN_WITH_MERGEINFO so this is never removed by this
   function. */
static void
remove_children_with_deleted_mergeinfo(merge_cmd_baton_t *merge_b,
                                       notification_receiver_baton_t *notify_b)
{
  if (!merge_b->dry_run && merge_b->paths_with_deleted_mergeinfo)
    {
      int i;
      /* NOTIFY_B->CHILDREN_WITH_MERGEINFO[0] is the always the merge target
         so start at the first child. */
      for (i = 1; i < notify_b->children_with_mergeinfo->nelts; i++)
        {
          svn_client__merge_path_t *child =
            APR_ARRAY_IDX(notify_b->children_with_mergeinfo,
                          i, svn_client__merge_path_t *);
          if (apr_hash_get(merge_b->paths_with_deleted_mergeinfo,
                           child->path,
                           APR_HASH_KEY_STRING))
            {
              remove_element_from_array(notify_b->children_with_mergeinfo,
                                        i--);
            }
        }
    }
}

/* Helper for do_directory_merge().

   Set up the diff editor report to merge URL1@REVISION1 to URL2@REVISION2
   into TARGET_WCPATH and drive it.

   If mergeinfo is not being honored based on MERGE_B, see the doc string for
   mergeinfo_behavior() for how this is determined, then ignore
   CHILDREN_WITH_MERGEINFO and merge the diff between URL1@REVISION1 and
   URL2@REVISION2 to TARGET_WCPATH.

   If mergeinfo is being honored then perform a history-aware merge,
   describing TARGET_WCPATH and its subtrees to the reporter in such as way as
   to avoid repeating merges already performed per the mergeinfo and natural
   history of TARGET_WCPATH and its subtrees.

   The ranges that still need to be merged to the TARGET_WCPATH and its
   subtrees are described in CHILDREN_WITH_MERGEINFO, an array of
   svn_client__merge_path_t * -- see 'THE CHILDREN_WITH_MERGEINFO ARRAY'
   comment at the top of this file for more info.  Note that it is possible
   TARGET_WCPATH and/or some of its subtrees need only a subset, or no part,
   of REVISION1:REVISION2 to be merged.  Though there is little point to
   calling this function if TARGET_WCPATH and all its subtrees have already
   had URL1@REVISION1 to URL2@REVISION2 merged, this will work but is a no-op.

   REVISION1 and REVISION2 must be bound by the set of remaining_ranges
   fields in CHILDREN_WITH_MERGEINFO's elements, specifically:

   For forward merges (REVISION1 < REVISION2):

     1) The first svn_merge_range_t * element of each child's remaining_ranges
        array must meet one of the following conditions:

        a) The range's start field is greater than or equal to REVISION2.

        b) The range's end field is REVISION2.

     2) Among all the ranges that meet condition 'b' the oldest start
        revision must equal REVISION1.

   For reverse merges (REVISION1 > REVISION2):

     1) The first svn_merge_range_t * element of each child's remaining_ranges
        array must meet one of the following conditions:

        a) The range's start field is less than or equal to REVISION2.

        b) The range's end field is REVISION2.

     2) Among all the ranges that meet condition 'b' the youngest start
        revision must equal REVISION1.

   Note: If the first svn_merge_range_t * element of some subtree child's
   remaining_ranges array is the same as the first range of that child's
   nearest path-wise ancestor, then the subtree child *will not* be described
   to the reporter.

   DEPTH, NOTIFY_B, ADM_ACCESS, and MERGE_B are cascasded from
   do_directory_merge(), see that function for more info.  CALLBACKS are the
   svn merge versions of the svn_wc_diff_callbacks3_t callbacks invoked by
   the editor.

   If MERGE_B->sources_ancestral is set, then URL1@REVISION1 must be a
   historical ancestor of URL2@REVISION2, or vice-versa (see
   `MERGEINFO MERGE SOURCE NORMALIZATION' for more requirements around
   the values of URL1, REVISION1, URL2, and REVISION2 in this case).
*/
static svn_error_t *
drive_merge_report_editor(const char *target_wcpath,
                          const char *url1,
                          svn_revnum_t revision1,
                          const char *url2,
                          svn_revnum_t revision2,
                          apr_array_header_t *children_with_mergeinfo,
                          svn_depth_t depth,
                          notification_receiver_baton_t *notify_b,
                          svn_wc_adm_access_t *adm_access,
                          const svn_wc_diff_callbacks3_t *callbacks,
                          merge_cmd_baton_t *merge_b,
                          apr_pool_t *pool)
{
  const svn_ra_reporter3_t *reporter;
  const svn_delta_editor_t *diff_editor;
  void *diff_edit_baton;
  void *report_baton;
  svn_revnum_t target_start;
  svn_boolean_t honor_mergeinfo;
  const char *old_sess2_url;
  svn_boolean_t is_rollback = revision1 > revision2;

  mergeinfo_behavior(&honor_mergeinfo, NULL, merge_b);

  /* Start with a safe default starting revision for the editor and the
     merge target. */
  target_start = revision1;

  /* If we are honoring mergeinfo the starting revision for the merge target
     might not be REVISION1, in fact the merge target might not need *any*
     part of REVISION1:REVISION2 merged -- Instead some subtree of the target
     needs REVISION1:REVISION2 -- So get the right starting revision for the
     target. */
  if (honor_mergeinfo)
    {
      svn_client__merge_path_t *child;

      /* CHILDREN_WITH_MERGEINFO must always exist if we are honoring
         mergeinfo and must have at least one element (describing the
         merge target). */
      SVN_ERR_ASSERT(children_with_mergeinfo);
      SVN_ERR_ASSERT(children_with_mergeinfo->nelts);

      /* Get the merge target's svn_client__merge_path_t, which is always
         the first in the array due to depth first sorting requirement,
         see 'THE CHILDREN_WITH_MERGEINFO ARRAY'. */
      child = APR_ARRAY_IDX(children_with_mergeinfo, 0,
                            svn_client__merge_path_t *);
      SVN_ERR_ASSERT(child);
      if (child->remaining_ranges->nelts == 0)
        {
          /* The merge target doesn't need anything merged. */
          target_start = revision2;
        }
      else
        {
          /* The merge target has remaining revisions to merge.  These
             ranges may fully or partially overlap the range described
             by REVISION1:REVISION2 or may not intersect that range at
             all. */
          svn_merge_range_t *range =
            APR_ARRAY_IDX(child->remaining_ranges, 0,
                          svn_merge_range_t *);
          if ((!is_rollback && range->start > revision2)
              || (is_rollback && range->start < revision2))
            {
              /* Merge target's first remaining range doesn't intersect. */
              target_start = revision2;
            }
          else
            {
              /* Merge target's first remaining range partially or
                 fully overlaps. */
              target_start = range->start;
            }
        }
    }

      /* Temporarily point our second RA session to URL1, too.  We use
         this to request individual file contents. */
      SVN_ERR(svn_client__ensure_ra_session_url(&old_sess2_url,
                                                merge_b->ra_session2,
                                                url1, pool));
    
  /* Get the diff editor and a reporter with which to, ultimately,
     drive it. */
  SVN_ERR(svn_client__get_diff_editor(target_wcpath, adm_access, callbacks,
                                      merge_b, depth, merge_b->dry_run,
                                      merge_b->ra_session2, revision1,
                                      notification_receiver, notify_b,
                                      merge_b->ctx->cancel_func,
                                      merge_b->ctx->cancel_baton,
                                      &diff_editor, &diff_edit_baton,
                                      pool));
  SVN_ERR(svn_ra_do_diff3(merge_b->ra_session1,
                          &reporter, &report_baton, revision2,
                          "", depth, merge_b->ignore_ancestry,
                          TRUE,  /* text_deltas */
                          url2, diff_editor, diff_edit_baton, pool));

  /* Drive the reporter. */
  SVN_ERR(reporter->set_path(report_baton, "", target_start, depth,
                             FALSE, NULL, pool));
  if (honor_mergeinfo && children_with_mergeinfo)
    {
      /* Describe children with mergeinfo overlapping this merge
         operation such that no repeated diff is retrieved for them from
         the repository. */
      apr_size_t target_wcpath_len = strlen(target_wcpath);
      int i;

      /* Start with CHILDREN_WITH_MERGEINFO[1], CHILDREN_WITH_MERGEINFO[0]
         is always the merge target (TARGET_WCPATH). */
      for (i = 1; i < children_with_mergeinfo->nelts; i++)
        {
          svn_merge_range_t *range;
          const char *child_repos_path;
          svn_client__merge_path_t *parent;
          svn_client__merge_path_t *child =
            APR_ARRAY_IDX(children_with_mergeinfo, i,
                          svn_client__merge_path_t *);
          int parent_index;
          svn_boolean_t nearest_parent_is_target;

          SVN_ERR_ASSERT(child);
          if (child->absent)
            continue;

          /* Find this child's nearest wc ancestor with mergeinfo. */
          parent_index = find_nearest_ancestor(children_with_mergeinfo,
                                               FALSE, child->path);
          parent = APR_ARRAY_IDX(children_with_mergeinfo, parent_index,
                                 svn_client__merge_path_t *);

          /* Note if the child's parent is the merge target. */
          nearest_parent_is_target = (parent_index == 0);

          /* If a subtree needs the same range applied as its nearest parent
             with mergeinfo or neither the subtree nor this parent need
             REVISION1:REVISION2 merged, then we don't need to describe the
             subtree separately.  In the latter case this could break the
             editor if child->path didn't exist at REVISION2 and we attempt
             to describe it via a reporter set_path call. */
          if (child->remaining_ranges->nelts)
            {
              range = APR_ARRAY_IDX(child->remaining_ranges, 0,
                                    svn_merge_range_t *);
              if ((!is_rollback && range->start > revision2)
                  || (is_rollback && range->start < revision2))
                {
                  /* This child's first remaining range comes after the range
                     we are currently merging, so skip it. We expect to get
                     to it in a subsequent call to this function. */
                  continue;
                }
              else if (parent->remaining_ranges->nelts)
                {
                   svn_merge_range_t *parent_range =
                    APR_ARRAY_IDX(parent->remaining_ranges, 0,
                                  svn_merge_range_t *);
                   svn_merge_range_t *child_range =
                    APR_ARRAY_IDX(child->remaining_ranges, 0,
                                  svn_merge_range_t *);
                  if (parent_range->start == child_range->start)
                    continue; /* Subtree needs same range as parent. */
                }
            }
          else /* child->remaining_ranges->nelts == 0*/
            {
              /* If both the subtree and its parent need no ranges applied
                 consider that as the "same ranges" and don't describe
                 the subtree. */
              if (parent->remaining_ranges->nelts == 0)
                continue;
            }

          /* Ok, we really need to describe this subtree as it needs different
             ranges applied than its nearest working copy parent. */
          child_repos_path = child->path +
            (target_wcpath_len ? target_wcpath_len + 1 : 0);

          if ((child->remaining_ranges->nelts == 0)
              || (is_rollback && (range->start < revision2))
              || (!is_rollback && (range->start > revision2)))
            {
              /* Nothing to merge to this child.  We'll claim we have
                 it up to date so the server doesn't send us
                 anything. */
              SVN_ERR(reporter->set_path(report_baton, child_repos_path,
                                         revision2, depth, FALSE,
                                         NULL, pool));
            }
          else
            {
              SVN_ERR(reporter->set_path(report_baton, child_repos_path,
                                         range->start, depth, FALSE,
                                         NULL, pool));
            }
        }
    }
  SVN_ERR(reporter->finish_report(report_baton, pool));

  /* Point the merge baton's second session back where it was. */
  if (old_sess2_url)
    SVN_ERR(svn_ra_reparent(merge_b->ra_session2, old_sess2_url, pool));

  /* Caller must call svn_sleep_for_timestamps() */
  *(merge_b->use_sleep) = TRUE;

  return SVN_NO_ERROR;
}

/* Iterate over each svn_client__merge_path_t * element in
   CHILDREN_WITH_MERGEINFO and find the most inclusive start revision
   among those element's first remaining_ranges element.

   If IS_ROLLBACK is true the youngest revision is considered the "most
   inclusive" otherwise the oldest revision is.

   If none of CHILDREN_WITH_MERGEINFO's elements have any remaining ranges
   return SVN_INVALID_REVNUM. */
static svn_revnum_t
get_most_inclusive_start_rev(apr_array_header_t *children_with_mergeinfo,
                             svn_boolean_t is_rollback)
{
  int i;
  svn_revnum_t start_rev = SVN_INVALID_REVNUM;

  for (i = 0; i < children_with_mergeinfo->nelts; i++)
    {
      svn_client__merge_path_t *child =
        APR_ARRAY_IDX(children_with_mergeinfo, i, svn_client__merge_path_t *);
      svn_merge_range_t *range;

      if ((! child) || child->absent)
        continue;
      if (! child->remaining_ranges->nelts)
        continue;
      range = APR_ARRAY_IDX(child->remaining_ranges, 0, svn_merge_range_t *);
      if ((i == 0) && (range->start == range->end))
        continue;
      if ((start_rev == SVN_INVALID_REVNUM)
          || (is_rollback && (range->start > start_rev))
          || ((! is_rollback) && (range->start < start_rev)))
        start_rev = range->start;
    }
  return start_rev;
}

/* Iterate over each svn_client__merge_path_t * element in
   CHILDREN_WITH_MERGEINFO and find the most inclusive end revision
   among those element's first remaining_ranges element.

   If IS_ROLLBACK is true the oldest revision is considered the "most
   inclusive" otherwise the youngest revision is.

   If none of CHILDREN_WITH_MERGEINFO's elements have any remaining ranges
   return SVN_INVALID_REVNUM. */
static svn_revnum_t
get_most_inclusive_end_rev(apr_array_header_t *children_with_mergeinfo,
                           svn_boolean_t is_rollback)
{
  int i;
  svn_revnum_t end_rev = SVN_INVALID_REVNUM;

  for (i = 0; i < children_with_mergeinfo->nelts; i++)
    {
      svn_client__merge_path_t *child =
        APR_ARRAY_IDX(children_with_mergeinfo, i, svn_client__merge_path_t *);
      if (!child || child->absent)
        continue;
      if (child->remaining_ranges->nelts > 0)
        {
          svn_merge_range_t *range = APR_ARRAY_IDX(child->remaining_ranges, 0,
                                                   svn_merge_range_t *);
          if ((end_rev == SVN_INVALID_REVNUM)
              || (is_rollback && (range->end > end_rev))
              || ((! is_rollback) && (range->end < end_rev)))
            end_rev = range->end;
        }
    }
  return end_rev;
}

/* If first item in each child of CHILDREN_WITH_MERGEINFO's
   remaining_ranges is inclusive of END_REV, Slice the first range in
   to two at END_REV. All the allocations are persistent and allocated
   from POOL. */
static void
slice_remaining_ranges(apr_array_header_t *children_with_mergeinfo,
                       svn_boolean_t is_rollback, svn_revnum_t end_rev,
                       apr_pool_t *pool)
{
  int i;
  for (i = 0; i < children_with_mergeinfo->nelts; i++)
    {
      svn_client__merge_path_t *child =
                                     APR_ARRAY_IDX(children_with_mergeinfo, i,
                                                   svn_client__merge_path_t *);
      if (!child || child->absent)
        continue;
      if (child->remaining_ranges->nelts > 0)
        {
          svn_merge_range_t *range = APR_ARRAY_IDX(child->remaining_ranges, 0,
                                                   svn_merge_range_t *);
          if ((is_rollback && (range->start > end_rev)
               && (range->end < end_rev))
              || (!is_rollback && (range->start < end_rev)
                  && (range->end > end_rev)))
            {
              int j;
              svn_merge_range_t *split_range1, *split_range2;
              apr_array_header_t *orig_remaining_ranges =
                                                     child->remaining_ranges;
              split_range1 = svn_merge_range_dup(range, pool);
              split_range2 = svn_merge_range_dup(range, pool);
              split_range1->end = end_rev;
              split_range2->start = end_rev;
              child->remaining_ranges =
                     apr_array_make(pool, (child->remaining_ranges->nelts + 1),
                                    sizeof(svn_merge_range_t *));
              APR_ARRAY_PUSH(child->remaining_ranges,
                             svn_merge_range_t *) = split_range1;
              APR_ARRAY_PUSH(child->remaining_ranges,
                             svn_merge_range_t *) = split_range2;
              for (j = 1; j < orig_remaining_ranges->nelts; j++)
                {
                  svn_merge_range_t *orig_range =
                                     APR_ARRAY_IDX(orig_remaining_ranges, j,
                                                   svn_merge_range_t *);
                  APR_ARRAY_PUSH(child->remaining_ranges,
                                 svn_merge_range_t *) = orig_range;
                }
            }
        }
    }
}

/* Helper for do_directory_merge().

   For each child in CHILDREN_WITH_MERGEINFO remove the first remaining_ranges
   svn_merge_range_t *element of the child if that range has an end revision
   equal to REVISION.

   If a range is removed from a child's remaining_ranges array, allocate the
   new remaining_ranges array in POOL.

   ### TODO: We should have remaining_ranges in reverse order to avoid
   ### recreating and reallocationg the remaining_ranges every time we want
   ### to remove the first range.  If the ranges were reversed we could simply
   ### pop the last element in the array. */
static void
remove_first_range_from_remaining_ranges(svn_revnum_t revision,
                                         apr_array_header_t
                                           *children_with_mergeinfo,
                                         apr_pool_t *pool)
{
  int i, j;
  for (i = 0; i < children_with_mergeinfo->nelts; i++)
    {
      svn_client__merge_path_t *child =
                                APR_ARRAY_IDX(children_with_mergeinfo, i,
                                              svn_client__merge_path_t *);
      if (!child || child->absent)
        continue;
      if (child->remaining_ranges->nelts > 0)
        {
          svn_merge_range_t *first_range =
            APR_ARRAY_IDX(child->remaining_ranges, 0, svn_merge_range_t *);
          if (first_range->end == revision)
            {
              apr_array_header_t *orig_remaining_ranges =
                child->remaining_ranges;
              child->remaining_ranges =
                apr_array_make(pool, (child->remaining_ranges->nelts - 1),
                               sizeof(svn_merge_range_t *));
              for (j = 1; j < orig_remaining_ranges->nelts; j++)
                {
                  svn_merge_range_t *range =
                    APR_ARRAY_IDX(orig_remaining_ranges,
                                  j,
                                  svn_merge_range_t *);
                  APR_ARRAY_PUSH(child->remaining_ranges,
                                 svn_merge_range_t *) = range;
                }
            }
        }
    }
}

/* Get a file's content and properties from the repository.
   Set *FILENAME to the local path to a new temporary file holding its text,
   and set *PROPS to a new hash of its properties.

   RA_SESSION is a session whose current root is the URL of the file itself,
   and REV is the revision to get.

   The new temporary file will be created as a sibling of WC_TARGET.
   WC_TARGET should be the local path to the working copy of the file, but
   it does not matter whether anything exists on disk at this path as long
   as WC_TARGET's parent directory exists.

   All allocation occurs in POOL.

   ### TODO: Create the temporary file under .svn/tmp/ instead of next to
   the working file.
*/
static svn_error_t *
single_file_merge_get_file(const char **filename,
                           svn_ra_session_t *ra_session,
                           apr_hash_t **props,
                           svn_revnum_t rev,
                           const char *wc_target,
                           apr_pool_t *pool)
{
  svn_stream_t *stream;

  SVN_ERR(svn_stream_open_unique(&stream, filename,
                                 svn_path_dirname(wc_target, pool),
                                 svn_io_file_del_none, pool, pool));
  SVN_ERR(svn_ra_get_file(ra_session, "", rev,
                          stream, NULL, props, pool));
  return svn_stream_close(stream);
}


/* Send a notification specific to a single-file merge if the states
   indicate there's something worth reporting.

   If *HEADER_SENT is not set and HEADER_NOTIFICATION is not NULL, then
   send the header notification before sending the state notification,
   and set *HEADER_SENT to TRUE. */
static APR_INLINE void
single_file_merge_notify(void *notify_baton,
                         const char *target_wcpath,
                         svn_wc_notify_action_t action,
                         svn_wc_notify_state_t text_state,
                         svn_wc_notify_state_t prop_state,
                         svn_wc_notify_t *header_notification,
                         svn_boolean_t *header_sent,
                         apr_pool_t *pool)
{
  svn_wc_notify_t *notify = svn_wc_create_notify(target_wcpath, action, pool);
  notify->kind = svn_node_file;
  notify->content_state = text_state;
  notify->prop_state = prop_state;
  if (notify->content_state == svn_wc_notify_state_missing)
    notify->action = svn_wc_notify_skip;

  if (IS_OPERATIVE_NOTIFICATION(notify)
      && header_notification
      && (! *header_sent))
    {
      notification_receiver(notify_baton, header_notification, pool);
      *header_sent = TRUE;
    }
  notification_receiver(notify_baton, notify, pool);
}


/* A baton for get_mergeinfo_walk_cb. */
struct get_mergeinfo_walk_baton
{
  /* Access for the tree being walked. */
  svn_wc_adm_access_t *base_access;
  /* Array of paths that have explicit mergeinfo and/or are switched. */
  apr_array_header_t *children_with_mergeinfo;
  /* Merge source canonical path. */
  const char* merge_src_canon_path;

  /* Information on the merge cascaded from do_directory_merge() */
  const char* merge_target_path;
  const char *source_root_url;
  const char* url1;
  const char* url2;
  svn_revnum_t revision1;
  svn_revnum_t revision2;

  /* merge depth requested. */
  svn_depth_t depth;

  /* RA session and client context cascaded from do_directory_merge() */
  svn_ra_session_t *ra_session;
  svn_client_ctx_t *ctx;

  /* Pool from which to allocate new elements of CHILDREN_WITH_MERGEINFO. */
  apr_pool_t *pool;
};


/* svn_wc_entry_callbacks2_t found_entry() callback for get_mergeinfo_paths.

   Given PATH, its corresponding ENTRY, and WB, where WB is the WALK_BATON
   of type "struct get_mergeinfo_walk_baton *":  If PATH is switched,
   has explicit working svn:mergeinfo, is missing a child due to a sparse
   checkout, is absent from disk, is scheduled for deletion, or if the walk
   is being done as part of a reverse merge, then create a
   svn_client__merge_path_t *representing *PATH, allocated in
   WB->CHILDREN_WITH_MERGEINFO->POOL, and push it onto the
   WB->CHILDREN_WITH_MERGEINFO array. */
static svn_error_t *
get_mergeinfo_walk_cb(const char *path,
                      const svn_wc_entry_t *entry,
                      void *walk_baton,
                      apr_pool_t *pool)
{
  struct get_mergeinfo_walk_baton *wb = walk_baton;
  const svn_string_t *propval;
  svn_boolean_t switched = FALSE;
  svn_boolean_t has_mergeinfo = FALSE;
  svn_boolean_t path_is_merge_target =
    !svn_path_compare_paths(path, wb->merge_target_path);
  const char *parent_path = svn_path_dirname(path, pool);

  /* TODO(#2843) How to deal with a excluded item on merge? */

  /* We're going to receive dirents twice;  we want to ignore the
     first one (where it's a child of a parent dir), and only use
     the second one (where we're looking at THIS_DIR).  The exception
     is absent dirs, these only come through once, so continue. */
  if ((entry->kind == svn_node_dir)
      && (strcmp(entry->name, SVN_WC_ENTRY_THIS_DIR) != 0)
      && !entry->absent)
    return SVN_NO_ERROR;

  /* Ignore the entry if it does not exist at the time of interest. */
  if (entry->deleted)
    return SVN_NO_ERROR;

  if (entry->absent || entry->schedule == svn_wc_schedule_delete)
    {
      propval = NULL;
      switched = FALSE;
    }
  else
    {
      SVN_ERR(svn_wc_prop_get(&propval, SVN_PROP_MERGEINFO, path,
                              wb->base_access, pool));
      if (propval)
        has_mergeinfo = TRUE;

      /* Regardless of whether PATH has explicit mergeinfo or not, we must
         determine if PATH is switched.  This is so get_mergeinfo_paths()
         can later tweak PATH's parent to reflect a missing child (implying it
         needs non-inheritable mergeinfo ranges) and PATH's siblings so they
         get their own complete set of mergeinfo. */
      SVN_ERR(svn_wc__path_switched(path, &switched, entry, pool));
    }

  /* Store PATHs with explict mergeinfo, which are switched, are missing
     children due to a sparse checkout, are scheduled for deletion are absent
     from the WC, are first level sub directories relative to merge target if
     depth is immediates, and/or are file children of the merge target if
     depth is files. */
  if (path_is_merge_target
      || has_mergeinfo
      || entry->schedule == svn_wc_schedule_delete
      || switched
      || entry->depth == svn_depth_empty
      || entry->depth == svn_depth_files
      || entry->absent
      || ((wb->depth == svn_depth_immediates) &&
          (entry->kind == svn_node_dir) &&
          (strcmp(parent_path, wb->merge_target_path) == 0))
      || ((wb->depth == svn_depth_files) &&
          (entry->kind == svn_node_file) &&
          (strcmp(parent_path, wb->merge_target_path) == 0))
          )
    {
      svn_client__merge_path_t *child =
        apr_pcalloc(wb->pool, sizeof(*child));
      child->path = apr_pstrdup(wb->pool, path);
      child->missing_child = (entry->depth == svn_depth_empty
                              || entry->depth == svn_depth_files
                              || ((wb->depth == svn_depth_immediates) &&
                                  (entry->kind == svn_node_dir) &&
                                  (strcmp(parent_path,
                                          wb->merge_target_path) == 0)));
      child->switched = switched;
      child->absent = entry->absent;
      child->scheduled_for_deletion =
        entry->schedule == svn_wc_schedule_delete;
      if (propval
          && strstr(propval->data, SVN_MERGEINFO_NONINHERITABLE_STR))
        child->has_noninheritable = TRUE;

      /* A little trickery: If PATH doesn't have any mergeinfo or has
         only inheritable mergeinfo, we still describe it as having
         non-inheritable mergeinfo if it is missing a child.  Why?  Because
         the mergeinfo we'll add to PATH as a result of the merge will need
         to be non-inheritable (since PATH is missing children) and doing
         this now allows get_mergeinfo_paths() to properly account for PATH's
         other children. */
      if (!child->has_noninheritable
          && (entry->depth == svn_depth_empty
              || entry->depth == svn_depth_files))
      child->has_noninheritable = TRUE;

      APR_ARRAY_PUSH(wb->children_with_mergeinfo,
                     svn_client__merge_path_t *) = child;
    }

  return SVN_NO_ERROR;
}

/* svn_wc_entry_callbacks2_t handle_error() callback for
   get_mergeinfo_paths().

   Squelch ERR by returning SVN_NO_ERROR if ERR is caused by a missing
   path (i.e. SVN_ERR_WC_PATH_NOT_FOUND) or an unversioned path
   (i.e. SVN_ERR_WC_NOT_LOCKED). */
static svn_error_t *
get_mergeinfo_error_handler(const char *path,
                            svn_error_t *err,
                            void *walk_baton,
                            apr_pool_t *pool)
{
  svn_error_t *root_err = svn_error_root_cause(err);
  if (root_err == SVN_NO_ERROR)
    return err;

  switch (root_err->apr_err)
    {
    case SVN_ERR_WC_PATH_NOT_FOUND:
    case SVN_ERR_WC_NOT_LOCKED:
      svn_error_clear(err);
      return SVN_NO_ERROR;

    default:
      return err;
    }
}

/* Compare two svn_client__merge_path_t elements **A and **B, given the
   addresses of pointers to them. Return an integer less than, equal to, or
   greater than zero if A sorts before, the same as, or after B, respectively.
   This is a helper for qsort() and bsearch() on an array of such elements. */
static int
compare_merge_path_t_as_paths(const void *a,
                              const void *b)
{
  const svn_client__merge_path_t *child1
    = *((const svn_client__merge_path_t * const *) a);
  const svn_client__merge_path_t *child2
    = *((const svn_client__merge_path_t * const *) b);

  return svn_path_compare_paths(child1->path, child2->path);
}

/* Return a pointer to the element of CHILDREN_WITH_MERGEINFO whose path
 * is PATH, or return NULL if there is no such element. */
static svn_client__merge_path_t *
get_child_with_mergeinfo(const apr_array_header_t *children_with_mergeinfo,
                         const char *path)
{
  svn_client__merge_path_t merge_path;
  svn_client__merge_path_t *key;
  svn_client__merge_path_t **pchild;

  merge_path.path = path;
  key = &merge_path;
  pchild = bsearch(&key, children_with_mergeinfo->elts,
                   children_with_mergeinfo->nelts,
                   children_with_mergeinfo->elt_size,
                   compare_merge_path_t_as_paths);
  return pchild ? *pchild : NULL;
}

/* Insert a deep copy of INSERT_ELEMENT into the CHILDREN_WITH_MERGEINFO
   array at its correct position.  Allocate the new storage in POOL.
   CHILDREN_WITH_MERGEINFO is a depth first sorted array of
   (svn_client__merge_path_t *). */
static void
insert_child_to_merge(apr_array_header_t *children_with_mergeinfo,
                      const svn_client__merge_path_t *insert_element,
                      apr_pool_t *pool)
{
  int insert_index;
  const svn_client__merge_path_t *new_element;

  /* Find where to insert the new element */
  insert_index =
    svn_sort__bsearch_lower_bound(&insert_element, children_with_mergeinfo,
                                  compare_merge_path_t_as_paths);

  new_element = svn_client__merge_path_dup(insert_element, pool);
  svn_sort__array_insert(&new_element, children_with_mergeinfo, insert_index);
}

/* Helper for get_mergeinfo_paths().

   CHILDREN_WITH_MERGEINFO, MERGE_CMD_BATON, DEPTH, ADM_ACCESS, and POOL are
   all cascaded from the arguments of the same name to get_mergeinfo_paths().

   *CHILD is the element in in CHILDREN_WITH_MERGEINFO that
   get_mergeinfo_paths() is iterating over and *CURR_INDEX is index for
   *CHILD.

   If CHILD->PATH is equal to MERGE_CMD_BATON->TARGET do nothing.  Else if
   CHILD->PATH is switched or absent then make sure its immediate (as opposed
   to nearest) parent in CHILDREN_WITH_MERGEINFO is marked as missing a
   child.  If the immediate parent does not exist in CHILDREN_WITH_MERGEINFO
   then create it (and increment *CURR_INDEX so that caller don't process the
   inserted element).  Also ensure that CHILD->PATH's siblings which are not
   already present in CHILDREN_WITH_MERGEINFO are also added to the array,
   limited by DEPTH (e.g. don't add directory siblings of a switched file).
   Use POOL for temporary allocations only, any new CHILDREN_WITH_MERGEINFO
   elements are allocated in POOL. */
static svn_error_t *
insert_parent_and_sibs_of_sw_absent_del_entry(
                                   apr_array_header_t *children_with_mergeinfo,
                                   merge_cmd_baton_t *merge_cmd_baton,
                                   int *curr_index,
                                   svn_client__merge_path_t *child,
                                   svn_depth_t depth,
                                   svn_wc_adm_access_t *adm_access,
                                   apr_pool_t *pool)
{
  svn_client__merge_path_t *parent;
  const char *parent_path = svn_path_dirname(child->path, pool);
  apr_hash_t *entries;
  apr_hash_index_t *hi;
  svn_wc_adm_access_t *parent_access;

  if (!(child->absent
          || (child->switched
              && strcmp(merge_cmd_baton->target, child->path) != 0)))
    return SVN_NO_ERROR;

  parent = get_child_with_mergeinfo(children_with_mergeinfo, parent_path);
  if (parent)
    {
      parent->missing_child = TRUE;
    }
  else
    {
      /* Create a new element to insert into CHILDREN_WITH_MERGEINFO. */
      parent = apr_pcalloc(pool, sizeof(*parent));
      parent->path = apr_pstrdup(pool, parent_path);
      parent->missing_child = TRUE;
      /* Insert PARENT into CHILDREN_WITH_MERGEINFO. */
      insert_child_to_merge(children_with_mergeinfo, parent, pool);
      /* Increment for loop index so we don't process the inserted element. */
      (*curr_index)++;
    } /*(parent == NULL) */

  /* Add all of PARENT's non-missing children that are not already present.*/
  SVN_ERR(svn_wc_adm_probe_try3(&parent_access, adm_access, parent->path,
                                TRUE, -1, merge_cmd_baton->ctx->cancel_func,
                                merge_cmd_baton->ctx->cancel_baton, pool));
  SVN_ERR(svn_wc_entries_read(&entries, parent_access, FALSE, pool));
  for (hi = apr_hash_first(pool, entries); hi; hi = apr_hash_next(hi))
    {
      const void *key;
      svn_client__merge_path_t *sibling_of_missing;
      const char *child_path;

      apr_hash_this(hi, &key, NULL, NULL);

      if (strcmp(key, SVN_WC_ENTRY_THIS_DIR) == 0)
        continue;

      /* Does this child already exist in CHILDREN_WITH_MERGEINFO? */
      child_path = svn_path_join(parent->path, key, pool);
      sibling_of_missing = get_child_with_mergeinfo(children_with_mergeinfo,
                                                    child_path);
      /* Create the missing child and insert it into CHILDREN_WITH_MERGEINFO.*/
      if (!sibling_of_missing)
        {
          /* Don't add directory children if DEPTH is svn_depth_files. */
          if (depth == svn_depth_files)
            {
              const svn_wc_entry_t *child_entry;
              SVN_ERR(svn_wc_entry(&child_entry, child_path,
                                   adm_access, FALSE, pool));
              if (child_entry->kind != svn_node_file)
                continue;
            }

          sibling_of_missing = apr_pcalloc(pool,
                                           sizeof(*sibling_of_missing));
          sibling_of_missing->path = apr_pstrdup(pool,
                                                 child_path);
          insert_child_to_merge(children_with_mergeinfo, sibling_of_missing,
                                pool);
        }
    }
  return SVN_NO_ERROR;
}

/* Helper for do_directory_merge()

   If HONOR_MERGEINFO is TRUE, then perform a depth first walk of the working
   copy tree rooted at MERGE_CMD_BATON->TARGET (with the corresponding ENTRY).
   Create an svn_client__merge_path_t * for any path which meets one or more
   of the following criteria:

     1) Path has working svn:mergeinfo.
     2) Path is switched.
     3) Path is a subtree of the merge target (i.e. is not equal to
        MERGE_CMD_BATON->TARGET) and has no mergeinfo of its own but its
        parent has mergeinfo with non-inheritable ranges.  If this isn't a
        dry-run and the merge is between differences in the same repository,
        then this function will set working mergeinfo on the path equal to
        the mergeinfo inheritable from its parent.
     4) Path has an immediate child (or children) missing from the WC because
        the child is switched or absent from the WC, or due to a sparse
        checkout.
     5) Path has a sibling (or siblings) missing from the WC because the
        sibling is switched, absent, schduled for deletion, or missing due to
        a sparse checkout.
     6) Path is absent from disk due to an authz restriction.
     7) Path is equal to MERGE_CMD_BATON->TARGET.
     8) Path is an immediate *directory* child of MERGE_CMD_BATON->TARGET and
        DEPTH is svn_depth_immediates.
     9) Path is an immediate *file* child of MERGE_CMD_BATON->TARGET and
        DEPTH is svn_depth_files.
    10) do_directory_merge() is processing a reverse merge and path has any
        working svn:mergeinfo.

   If HONOR_MERGEINFO is FALSE, then create an svn_client__merge_path_t * only
   for MERGE_CMD_BATON->TARGET (i.e. only criteria 7 is applied).

   Store the svn_client__merge_path_t *'s in *CHILDREN_WITH_MERGEINFO in
   depth-first order based on the svn_client__merge_path_t *s path member as
   sorted by svn_path_compare_paths().  Set the remaining_ranges field of each
   element to NULL.

   Note: Since the walk is rooted at MERGE_CMD_BATON->TARGET, the latter is
   guaranteed to be in *CHILDREN_WITH_MERGEINFO and due to the depth-first
   ordering it is guaranteed to be the first element in
   *CHILDREN_WITH_MERGEINFO.

   MERGE_COMMAND_BATON, URL1, URL2, REVISION1, REVISION2, and ADM_ACCESS are
   cascaded from the arguments of the same name in do_directory_merge().

   RA_SESSION is the session for, and SOURCE_ROOT_URL is the repository root
   for, the younger of URL1@REVISION1 and URL2@REVISION2.

   MERGE_SRC_CANON_PATH is the path of of the younger of URL1@REVISION1 and
   URL2@REVISION2, relative to SOURCE_ROOT_URL (with a leading '/').


*/
static svn_error_t *
get_mergeinfo_paths(apr_array_header_t *children_with_mergeinfo,
                    merge_cmd_baton_t *merge_cmd_baton,
                    const char* merge_src_canon_path,
                    const svn_wc_entry_t *entry,
                    const char *source_root_url,
                    const char *url1,
                    const char *url2,
                    svn_revnum_t revision1,
                    svn_revnum_t revision2,
                    svn_boolean_t honor_mergeinfo,
                    svn_ra_session_t *ra_session,
                    svn_wc_adm_access_t *adm_access,
                    svn_depth_t depth,
                    apr_pool_t *pool)
{
  int i;
  static const svn_wc_entry_callbacks2_t walk_callbacks =
    { get_mergeinfo_walk_cb, get_mergeinfo_error_handler };
  struct get_mergeinfo_walk_baton wb = { 0 };

  wb.base_access = adm_access;
  wb.children_with_mergeinfo = children_with_mergeinfo;
  wb.merge_src_canon_path = merge_src_canon_path;
  wb.merge_target_path = merge_cmd_baton->target;
  wb.source_root_url = source_root_url;
  wb.url1 = url1;
  wb.url2 = url2;
  wb.revision1 = revision1;
  wb.revision2 = revision2;
  wb.depth = depth;
  wb.ra_session = ra_session;
  wb.ctx = merge_cmd_baton->ctx;
  wb.pool = pool;

  /* Cover cases 1), 2), and 6), 7), 8), 9), and 10) by walking the WC to get
     all paths which have mergeinfo and/or are switched or are absent from
     disk or is the target of the merge. */
  SVN_ERR(svn_wc_walk_entries3(merge_cmd_baton->target, adm_access,
                               &walk_callbacks, &wb,
                               /* If we are not honoring mergeinfo just
                                  do a depth empty walk so all we put in
                                  *CHILDREN_WITH_MERGEINFO is
                                  MERGE_CMD_BATON->TARGET. */
                               honor_mergeinfo ? depth : svn_depth_empty,
                               TRUE,
                               merge_cmd_baton->ctx->cancel_func,
                               merge_cmd_baton->ctx->cancel_baton,
                               pool));

  /* CHILDREN_WITH_MERGEINFO must be in depth first order, but
     svn_wc_walk_entries3() relies on svn_wc_entries_read() which means the
     paths at a given directory level are not in any particular order.  Also,
     we may need to add elements to the array to cover case 3) through 5) from
     the docstring.  If so, it is more efficient to find and insert these
     paths if the sibling paths are in a guaranteed depth-first order.  For
     the first reason we sort the array, for the second reason we do it now
     rather than at the end of this function. */
  qsort(children_with_mergeinfo->elts,
        children_with_mergeinfo->nelts,
        children_with_mergeinfo->elt_size,
        compare_merge_path_t_as_paths);

  /* If DEPTH isn't empty then cover cases 3), 4), and 5), possibly adding
     elements to CHILDREN_WITH_MERGEINFO. */
  if (honor_mergeinfo && depth > svn_depth_empty)
    {
      apr_pool_t *iterpool = svn_pool_create(pool);

      for (i = 0; i < children_with_mergeinfo->nelts; i++)
        {
          svn_client__merge_path_t *child =
            APR_ARRAY_IDX(children_with_mergeinfo, i,
                          svn_client__merge_path_t *);
          svn_pool_clear(iterpool);

          /* Case 3) Where merging to a path with a switched child the path
             gets non-inheritable mergeinfo for the merge range performed and
             the child gets its own set of mergeinfo.  If the switched child
             later "returns", e.g. a switched path is unswitched, the child
             may not have any explicit mergeinfo.  If the initial merge is
             repeated we don't want to repeat the merge for the path, but we
             do want to repeat it for the previously switched child.  To
             ensure this we check if all of CHILD's non-missing children have
             explicit mergeinfo (they should already be present in
             CHILDREN_WITH_MERGEINFO if they do).  If not,
             add the children without mergeinfo to CHILDREN_WITH_MERGEINFO so
             do_directory_merge() will merge them independently.

             But that's not enough!  Since do_directory_merge() performs
             the merges on the paths in CHILDREN_WITH_MERGEINFO in a depth
             first manner it will merge the previously switched path's parent
             first.  As part of this merge it will update the parent's
             previously non-inheritable mergeinfo and make it inheritable
             (since it notices the path has no missing children), then when
             do_directory_merge() finally merges the previously missing
             child it needs to get mergeinfo from the child's nearest
             ancestor, but since do_directory_merge() already tweaked that
             mergeinfo, removing the non-inheritable flag, it appears that the
             child already has been merged to.  To prevent this we set
             override mergeinfo on the child now, before any merging is done,
             so it has explicit mergeinfo that reflects only CHILD's
             inheritable mergeinfo. */

          if (child->has_noninheritable)
            {
              apr_hash_t *entries;
              apr_hash_index_t *hi;
              svn_wc_adm_access_t *child_access;
              SVN_ERR(svn_wc_adm_probe_try3(
                &child_access, adm_access,
                child->path, TRUE, -1,
                merge_cmd_baton->ctx->cancel_func,
                merge_cmd_baton->ctx->cancel_baton,
                iterpool));
              SVN_ERR(svn_wc_entries_read(&entries, child_access, FALSE,
                                          iterpool));
              for (hi = apr_hash_first(iterpool, entries); hi;
                   hi = apr_hash_next(hi))
                {
                  const void *key;
                  svn_client__merge_path_t *child_of_noninheritable;
                  const char *child_path;

                  apr_hash_this(hi, &key, NULL, NULL);

                  if (strcmp(key, SVN_WC_ENTRY_THIS_DIR) == 0)
                    continue;

                  /* Does this child already exist in CHILDREN_WITH_MERGEINFO?
                     If not, create it and insert it into
                     CHILDREN_WITH_MERGEINFO and set override mergeinfo on
                     it. */
                  child_path = svn_path_join(child->path, key, iterpool);
                  child_of_noninheritable =
                    get_child_with_mergeinfo(children_with_mergeinfo,
                                             child_path);
                  if (!child_of_noninheritable)
                    {
                      /* Don't add directory children if DEPTH
                         is svn_depth_files. */
                      if (depth == svn_depth_files)
                        {
                          const svn_wc_entry_t *child_entry;
                          SVN_ERR(svn_wc_entry(&child_entry, child_path,
                                               adm_access, FALSE, iterpool));
                          if (child_entry->kind != svn_node_file)
                            continue;
                        }
                      /* else DEPTH is infinity or immediates so we want both
                         directory and file children. */

                      child_of_noninheritable =
                        apr_pcalloc(pool,
                                    sizeof(*child_of_noninheritable));
                      child_of_noninheritable->path =
                        apr_pstrdup(pool,
                                    child_path);
                      insert_child_to_merge(children_with_mergeinfo,
                                            child_of_noninheritable,
                                            pool);
                      if (!merge_cmd_baton->dry_run
                          && merge_cmd_baton->same_repos)
                        {
                          svn_boolean_t inherited;
                          svn_mergeinfo_t mergeinfo;
                          const svn_wc_entry_t *child_entry;

                          SVN_ERR(svn_wc__entry_versioned(
                            &child_entry, child_of_noninheritable->path,
                            adm_access, FALSE, iterpool));

                          SVN_ERR(svn_client__get_wc_mergeinfo
                                  (&mergeinfo, &inherited, FALSE,
                                   svn_mergeinfo_nearest_ancestor,
                                   child_entry, child_of_noninheritable->path,
                                   merge_cmd_baton->target, NULL, adm_access,
                                   merge_cmd_baton->ctx, iterpool));

                          SVN_ERR(svn_client__record_wc_mergeinfo(
                            child_of_noninheritable->path, mergeinfo,
                            adm_access, iterpool));
                        }
                    }
                }
            }
          /* Case 4 and 5 are handled by the following function. */
          SVN_ERR(insert_parent_and_sibs_of_sw_absent_del_entry(
            children_with_mergeinfo, merge_cmd_baton, &i, child,
            depth, adm_access, pool));
        } /* i < children_with_mergeinfo->nelts */
      svn_pool_destroy(iterpool);
    } /* honor_mergeinfo */

  return SVN_NO_ERROR;
}


/* Implements the svn_log_entry_receiver_t interface.
 *
 * BATON is an 'apr_array_header_t *'.  Copy LOG_ENTRY->revision (in
 * BATON's pool, not POOL) and push the copy onto BATON.  Thus, a
 * series of invocations of this callback accumulates the
 * corresponding set of revisions into BATON.
 */
static svn_error_t *
log_changed_revs(void *baton,
                  svn_log_entry_t *log_entry,
                  apr_pool_t *pool)
{
  apr_array_header_t *revs = baton;
  svn_revnum_t *revision = apr_palloc(revs->pool, sizeof(*revision));
  *revision = log_entry->revision;
  APR_ARRAY_PUSH(revs, svn_revnum_t *) = revision;
  return SVN_NO_ERROR;
}


/* Set *OPERATIVE_RANGES_P to an array of svn_merge_range_t * merge
   range objects copied wholesale from RANGES which have the property
   that in some revision within that range the object identified by
   RA_SESSION was modified (if by "modified" we mean "'svn log' would
   return that revision).  *OPERATIVE_RANGES_P is allocated from the
   same pool as RANGES, and the ranges within it are shared with
   RANGES, too.

   *OPERATIVE_RANGES_P may be the same as RANGES (that is, the output
   parameter is set only after the input is no longer used).

   Use POOL for temporary allocations.  */
static svn_error_t *
remove_noop_merge_ranges(apr_array_header_t **operative_ranges_p,
                         svn_ra_session_t *ra_session,
                         apr_array_header_t *ranges,
                         apr_pool_t *pool)
{
  int i;
  svn_revnum_t oldest_rev = SVN_INVALID_REVNUM;
  svn_revnum_t youngest_rev = SVN_INVALID_REVNUM;
  svn_revnum_t oldest_changed_rev, youngest_changed_rev;
  apr_array_header_t *changed_revs =
    apr_array_make(pool, ranges->nelts, sizeof(svn_revnum_t *));
  apr_array_header_t *operative_ranges =
    apr_array_make(ranges->pool, ranges->nelts, ranges->elt_size);
  apr_array_header_t *log_targets =
    apr_array_make(pool, 1, sizeof(const char *));
  APR_ARRAY_PUSH(log_targets, const char *) = "";

  /* Find the revision extremes of the RANGES we have. */
  for (i = 0; i < ranges->nelts; i++)
    {
      svn_merge_range_t *r = APR_ARRAY_IDX(ranges, i, svn_merge_range_t *);
      svn_revnum_t max_rev = MAX(r->start, r->end);
      svn_revnum_t min_rev = MIN(r->start, r->end) + 1;

      if ((! SVN_IS_VALID_REVNUM(youngest_rev)) || (max_rev > youngest_rev))
        youngest_rev = max_rev;
      if ((! SVN_IS_VALID_REVNUM(oldest_rev)) || (min_rev < oldest_rev))
        oldest_rev = min_rev;
    }

  /* Get logs across those ranges, recording which revisions hold
     changes to our object's history. */
  SVN_ERR(svn_ra_get_log2(ra_session, log_targets, youngest_rev,
                          oldest_rev, 0, FALSE, FALSE, FALSE,
                          apr_array_make(pool, 0, sizeof(const char *)),
                          log_changed_revs, changed_revs, pool));

  /* Are there *any* changes? */
  if (changed_revs->nelts)
    {
      /* Our list of changed revisions should be in youngest-to-oldest
         order. */
      youngest_changed_rev = *(APR_ARRAY_IDX(changed_revs,
                                             0, svn_revnum_t *));
      oldest_changed_rev = *(APR_ARRAY_IDX(changed_revs,
                                           changed_revs->nelts - 1,
                                           svn_revnum_t *));

      /* Now, copy from RANGES to *OPERATIVE_RANGES, filtering out ranges
         that aren't operative (by virtue of not having any revisions
         represented in the CHANGED_REVS array). */
      for (i = 0; i < ranges->nelts; i++)
        {
          svn_merge_range_t *range = APR_ARRAY_IDX(ranges, i,
                                                   svn_merge_range_t *);
          svn_revnum_t range_min = MIN(range->start, range->end) + 1;
          svn_revnum_t range_max = MAX(range->start, range->end);
          int j;

          /* If the merge range is entirely outside the range of changed
             revisions, we've no use for it. */
          if ((range_min > youngest_changed_rev)
              || (range_max < oldest_changed_rev))
            continue;

          /* Walk through the changed_revs to see if any of them fall
             inside our current range. */
          for (j = 0; j < changed_revs->nelts; j++)
            {
              svn_revnum_t *changed_rev =
                APR_ARRAY_IDX(changed_revs, j, svn_revnum_t *);
              if ((*changed_rev >= range_min) && (*changed_rev <= range_max))
                {
                  APR_ARRAY_PUSH(operative_ranges, svn_merge_range_t *) =
                    range;
                  break;
                }
            }
        }
    }

  *operative_ranges_p = operative_ranges;
  return SVN_NO_ERROR;
}


/*-----------------------------------------------------------------------*/

/*** Merge Source Normalization ***/

/* qsort-compatible sort routine, rating merge_source_t * objects to
   be in descending (youngest-to-oldest) order based on their ->rev1
   component. */
static int
compare_merge_source_ts(const void *a,
                        const void *b)
{
  svn_revnum_t a_rev = ((const merge_source_t *)a)->rev1;
  svn_revnum_t b_rev = ((const merge_source_t *)b)->rev1;
  if (a_rev == b_rev)
    return 0;
  return a_rev < b_rev ? 1 : -1;
}

/* Set *MERGE_SOURCE_TS_P to a list of merge sources generated by
   slicing history location SEGMENTS with a given requested merge
   RANGE.  Use SOURCE_ROOT_URL for full source URL calculation.

   Order the merge sources in *MERGE_SOURCE_TS_P from oldest to
   youngest. */
static svn_error_t *
combine_range_with_segments(apr_array_header_t **merge_source_ts_p,
                            svn_merge_range_t *range,
                            apr_array_header_t *segments,
                            const char *source_root_url,
                            apr_pool_t *pool)
{
  apr_array_header_t *merge_source_ts =
    apr_array_make(pool, 1, sizeof(merge_source_t *));
  svn_revnum_t minrev = MIN(range->start, range->end) + 1;
  svn_revnum_t maxrev = MAX(range->start, range->end);
  svn_boolean_t subtractive = (range->start > range->end);
  int i;

  for (i = 0; i < segments->nelts; i++)
    {
      svn_location_segment_t *segment =
        APR_ARRAY_IDX(segments, i, svn_location_segment_t *);
      merge_source_t *merge_source;
      const char *path1 = NULL;
      svn_revnum_t rev1;

      /* If this segment doesn't overlap our range at all, or
         represents a gap, ignore it. */
      if ((segment->range_end < minrev)
          || (segment->range_start > maxrev)
          || (! segment->path))
        continue;

      /* If our range spans a segment boundary, we have to point our
         merge_source_t's path1 to the path of the immediately older
         segment, else it points to the same location as its path2.  */
      rev1 = MAX(segment->range_start, minrev) - 1;
      if (minrev <= segment->range_start)
        {
          if (i > 0)
            {
              path1 = (APR_ARRAY_IDX(segments, i - 1,
                                     svn_location_segment_t *))->path;
            }
          /* If we've backed PATH1 up into a segment gap, let's back
             it up further still to the segment before the gap.  We'll
             have to adjust rev1, too. */
          if ((! path1) && (i > 1))
            {
              path1 = (APR_ARRAY_IDX(segments, i - 2,
                                     svn_location_segment_t *))->path;
              rev1 = (APR_ARRAY_IDX(segments, i - 2,
                                    svn_location_segment_t *))->range_end;
            }
        }
      else
        {
          path1 = apr_pstrdup(pool, segment->path);
        }

      /* If we don't have two valid paths, we won't know what to do
         when merging.  This could happen if someone requested a merge
         where the source didn't exist in a particular revision or
         something.  The merge code would probably bomb out anyway, so
         we'll just *not* create a merge source in this case. */
      if (! (path1 && segment->path))
        continue;

      /* Build our merge source structure. */
      merge_source = apr_pcalloc(pool, sizeof(*merge_source));
      merge_source->url1 = svn_path_url_add_component2(source_root_url,
                                                       path1,
                                                       pool);
      merge_source->url2 = svn_path_url_add_component2(source_root_url,
                                                       segment->path,
                                                       pool);
      merge_source->rev1 = rev1;
      merge_source->rev2 = MIN(segment->range_end, maxrev);

      /* If this is subtractive, reverse the whole calculation. */
      if (subtractive)
        {
          svn_revnum_t tmprev = merge_source->rev1;
          const char *tmpurl = merge_source->url1;
          merge_source->rev1 = merge_source->rev2;
          merge_source->url1 = merge_source->url2;
          merge_source->rev2 = tmprev;
          merge_source->url2 = tmpurl;
        }

      APR_ARRAY_PUSH(merge_source_ts, merge_source_t *) = merge_source;
    }

  /* If this was a subtractive merge, and we created more than one
     merge source, we need to reverse the sort ordering of our sources. */
  if (subtractive && (merge_source_ts->nelts > 1))
    qsort(merge_source_ts->elts, merge_source_ts->nelts,
          merge_source_ts->elt_size, compare_merge_source_ts);

  *merge_source_ts_p = merge_source_ts;
  return SVN_NO_ERROR;
}

/* Set *MERGE_SOURCES to an array of merge_source_t * objects, each
   holding the paths and revisions needed to fully describe a range of
   requested merges; order the objects from oldest to youngest.

   Determine the requested merges by examining SOURCE (and its
   associated URL, SOURCE_URL) and PEG_REVISION (which specifies the
   line of history from which merges will be pulled) and
   RANGES_TO_MERGE (a list of svn_opt_revision_range_t's which provide
   revision ranges).

   If PEG_REVISION is unspecified, treat that it as HEAD.

   SOURCE_ROOT_URL is the root URL of the source repository.

   Use RA_SESSION -- whose session URL matches SOURCE_URL -- to answer
   historical questions.

   CTX is a client context baton.

   Use POOL for all allocation.

   See `MERGEINFO MERGE SOURCE NORMALIZATION' for more on the
   background of this function.
*/
static svn_error_t *
normalize_merge_sources(apr_array_header_t **merge_sources_p,
                        const char *source,
                        const char *source_url,
                        const char *source_root_url,
                        const svn_opt_revision_t *peg_revision,
                        const apr_array_header_t *ranges_to_merge,
                        svn_ra_session_t *ra_session,
                        svn_client_ctx_t *ctx,
                        apr_pool_t *pool)
{
  svn_revnum_t youngest_rev = SVN_INVALID_REVNUM;
  svn_revnum_t peg_revnum;
  svn_revnum_t oldest_requested = SVN_INVALID_REVNUM;
  svn_revnum_t youngest_requested = SVN_INVALID_REVNUM;
  svn_revnum_t trim_revision = SVN_INVALID_REVNUM;
  svn_opt_revision_t youngest_opt_rev;
  apr_array_header_t *merge_range_ts, *segments;
  apr_pool_t *subpool;
  int i;
  youngest_opt_rev.kind = svn_opt_revision_head;

  /* Initialize our return variable. */
  *merge_sources_p = apr_array_make(pool, 1, sizeof(merge_source_t *));

  /* Resolve our PEG_REVISION to a real number. */
  SVN_ERR(svn_client__get_revision_number(&peg_revnum, &youngest_rev,
                                          ra_session, peg_revision,
                                          source, pool));
  if (! SVN_IS_VALID_REVNUM(peg_revnum))
    return svn_error_create(SVN_ERR_CLIENT_BAD_REVISION, NULL, NULL);

  /* Create a list to hold svn_merge_range_t's. */
  merge_range_ts = apr_array_make(pool, ranges_to_merge->nelts,
                                  sizeof(svn_merge_range_t *));

  subpool = svn_pool_create(pool);
  for (i = 0; i < ranges_to_merge->nelts; i++)
    {
      svn_revnum_t range_start_rev, range_end_rev;
      svn_opt_revision_t *range_start =
        &((APR_ARRAY_IDX(ranges_to_merge, i,
                         svn_opt_revision_range_t *))->start);
      svn_opt_revision_t *range_end =
        &((APR_ARRAY_IDX(ranges_to_merge, i,
                         svn_opt_revision_range_t *))->end);

      svn_pool_clear(subpool);

      /* Resolve revisions to real numbers, validating as we go. */
      if ((range_start->kind == svn_opt_revision_unspecified)
          || (range_end->kind == svn_opt_revision_unspecified))
        return svn_error_create(SVN_ERR_CLIENT_BAD_REVISION, NULL,
                                _("Not all required revisions are specified"));
      SVN_ERR(svn_client__get_revision_number(&range_start_rev, &youngest_rev,
                                              ra_session, range_start,
                                              source, subpool));
      SVN_ERR(svn_client__get_revision_number(&range_end_rev, &youngest_rev,
                                              ra_session, range_end,
                                              source, subpool));

      /* If this isn't a no-op range... */
      if (range_start_rev != range_end_rev)
        {
          /* ...then create an svn_merge_range_t object for it. */
          svn_merge_range_t *range = apr_pcalloc(pool, sizeof(*range));
          range->start = range_start_rev;
          range->end = range_end_rev;
          range->inheritable = TRUE;

          /* Add our merge range to our list thereof. */
          APR_ARRAY_PUSH(merge_range_ts, svn_merge_range_t *) = range;
        }
    }

  /* No ranges to merge?  No problem. */
  if (merge_range_ts->nelts == 0)
    return SVN_NO_ERROR;

  /* Find the extremes of the revisions across our set of ranges. */
  for (i = 0; i < merge_range_ts->nelts; i++)
    {
      svn_merge_range_t *range =
        APR_ARRAY_IDX(merge_range_ts, i, svn_merge_range_t *);
      svn_revnum_t minrev = MIN(range->start, range->end);
      svn_revnum_t maxrev = MAX(range->start, range->end);

      /* Keep a running tally of the oldest and youngest requested
         revisions. */
      if ((! SVN_IS_VALID_REVNUM(oldest_requested))
          || (minrev < oldest_requested))
        oldest_requested = minrev;
      if ((! SVN_IS_VALID_REVNUM(youngest_requested))
          || (maxrev > youngest_requested))
        youngest_requested = maxrev;
    }

  /* ### FIXME:  Our underlying APIs can't yet handle the case where
     the peg revision isn't the youngest of the three revisions.  So
     we'll just verify that the source in the peg revision is related
     to the the source in the youngest requested revision (which is
     all the underlying APIs would do in this case right now anyway). */
  if (peg_revnum < youngest_requested)
    {
      const char *start_url;
      svn_opt_revision_t requested, unspec, pegrev, *start_revision;
      unspec.kind = svn_opt_revision_unspecified;
      requested.kind = svn_opt_revision_number;
      requested.value.number = youngest_requested;
      pegrev.kind = svn_opt_revision_number;
      pegrev.value.number = peg_revnum;

      SVN_ERR(svn_client__repos_locations(&start_url, &start_revision,
                                          NULL, NULL,
                                          ra_session, source_url,
                                          &pegrev, &requested,
                                          &unspec, ctx, pool));
      peg_revnum = youngest_requested;
    }

  /* Fetch the locations for our merge range span. */
  SVN_ERR(svn_client__repos_location_segments(&segments,
                                              ra_session, "",
                                              peg_revnum,
                                              youngest_requested,
                                              oldest_requested,
                                              ctx, pool));

  /* See if we fetched enough history to do the job.  "Surely we did,"
     you say.  "After all, we covered the entire requested merge
     range."  Yes, that's true, but if our first segment doesn't
     extend back to the oldest request revision, we've got a special
     case to deal with.  Or if the first segment represents a gap,
     that's another special case.  */
  trim_revision = SVN_INVALID_REVNUM;
  if (segments->nelts)
    {
      svn_location_segment_t *segment =
        APR_ARRAY_IDX(segments, 0, svn_location_segment_t *);

      /* If the first segment doesn't start with the OLDEST_REQUESTED
         revision, we'll need to pass a trim revision to our range
         cruncher. */
      if (segment->range_start != oldest_requested)
        {
          trim_revision = segment->range_start;
        }

      /* Else, if the first segment has no path (and therefore is a
         gap), then we'll fetch the copy source revision from the
         second segment (provided there is one, of course) and use it
         to prepend an extra pathful segment to our list.

         ### We could avoid this bit entirely if we'd passed
         ### SVN_INVALID_REVNUM instead of OLDEST_REQUESTED to
         ### svn_client__repos_location_segments(), but that would
         ### really penalize clients hitting pre-1.5 repositories with
         ### the typical small merge range request (because of the
         ### lack of a node-origins cache in the repository).  */
      else if (! segment->path)
        {
          if (segments->nelts > 1)
            {
              svn_location_segment_t *segment2 =
                APR_ARRAY_IDX(segments, 1, svn_location_segment_t *);
              const char *copyfrom_path, *segment_url;
              svn_revnum_t copyfrom_rev;
              svn_opt_revision_t range_start_rev;
              range_start_rev.kind = svn_opt_revision_number;
              range_start_rev.value.number = segment2->range_start;

              segment_url = svn_path_url_add_component2(source_root_url,
                                                        segment2->path, pool);
              SVN_ERR(svn_client__get_copy_source(segment_url,
                                                  &range_start_rev,
                                                  &copyfrom_path,
                                                  &copyfrom_rev,
                                                  ctx, pool));
              /* Got copyfrom data?  Fix up the first segment to cover
                 back to COPYFROM_REV + 1, and then prepend a new
                 segment covering just COPYFROM_REV. */
              if (copyfrom_path && SVN_IS_VALID_REVNUM(copyfrom_rev))
                {
                  svn_location_segment_t *new_segment =
                    apr_pcalloc(pool, sizeof(*new_segment));
                  /* Skip the leading '/'. */
                  new_segment->path = (*copyfrom_path == '/')
                    ? copyfrom_path + 1 : copyfrom_path;
                  new_segment->range_start = copyfrom_rev;
                  new_segment->range_end = copyfrom_rev;
                  segment->range_start = copyfrom_rev + 1;
                  APR_ARRAY_PUSH(segments, svn_location_segment_t *) = NULL;
                  memmove(segments->elts + segments->elt_size,
                          segments->elts,
                          segments->elt_size * (segments->nelts - 1));
                  APR_ARRAY_IDX(segments, 0, svn_location_segment_t *) =
                    new_segment;
                }
            }
        }
    }

  /* For each range in our requested range set, try to determine the
     path(s) associated with that range.  */
  for (i = 0; i < merge_range_ts->nelts; i++)
    {
      svn_merge_range_t *range =
        APR_ARRAY_IDX(merge_range_ts, i, svn_merge_range_t *);
      apr_array_header_t *merge_sources;
      int j;

      if (SVN_IS_VALID_REVNUM(trim_revision))
        {
          /* If the youngest of the range revisions predates the trim
             revision, discard the range. */
          if (MAX(range->start, range->end) < trim_revision)
            continue;

          /* Otherwise, if either of oldest of the range revisions predates
             the trim revision, update the range revision to be equal
             to the trim revision. */
          if (range->start < trim_revision)
            range->start = trim_revision;
          if (range->end < trim_revision)
            range->end = trim_revision;
        }

      /* Copy the resulting merge sources into master list thereof. */
      SVN_ERR(combine_range_with_segments(&merge_sources, range,
                                          segments, source_root_url, pool));
      for (j = 0; j < merge_sources->nelts; j++)
        {
          APR_ARRAY_PUSH(*merge_sources_p, merge_source_t *) =
            APR_ARRAY_IDX(merge_sources, j, merge_source_t *);
        }
    }

  return SVN_NO_ERROR;
}


/*-----------------------------------------------------------------------*/

/*** Merge Workhorse Functions ***/

/* Helper for do_directory_merge() and do_file_merge() which filters out a
   path's own natural history from the mergeinfo describing a merge.

   Given the natural history IMPLICIT_MERGEINFO of some wc merge target path,
   the repository-relative merge source path SOURCE_REL_PATH, and the
   requested merge range REQUESTED_RANGE from SOURCE_REL_PATH, remove any
   portion of REQUESTED_RANGE which is already described in
   IMPLICIT_MERGEINFO.  Store the result in *FILTERED_RANGELIST.

   This function only filters natural history for mergeinfo that will be
   *added* during a forward merge.  Removing natural history from explicit
   mergeinfo is harmless.  If REQUESTED_RANGE describes a reverse merge,
   then *FILTERED_RANGELIST is simply populated with one range described
   by REQUESTED_RANGE.  *FILTERED_RANGELIST is never NULL.

   Allocate *FILTERED_RANGELIST in POOL. */
static svn_error_t *
filter_natural_history_from_mergeinfo(apr_array_header_t **filtered_rangelist,
                                      const char *source_rel_path,
                                      svn_mergeinfo_t implicit_mergeinfo,
                                      svn_merge_range_t *requested_range,
                                      apr_pool_t *pool)
{
  /* Make the REQUESTED_RANGE into a rangelist. */
  apr_array_header_t *requested_rangelist =
    apr_array_make(pool, 0, sizeof(svn_merge_range_t *));
  APR_ARRAY_PUSH(requested_rangelist, svn_merge_range_t *) =
    svn_merge_range_dup(requested_range, pool);

  *filtered_rangelist = NULL;

  /* For forward merges: If the IMPLICIT_MERGEINFO already describes ranges
     associated with SOURCE_REL_PATH then filter those ranges out. */
  if (implicit_mergeinfo
      && (requested_range->start < requested_range->end))
    {
      apr_array_header_t *implied_rangelist =
        apr_hash_get(implicit_mergeinfo, source_rel_path,
                     APR_HASH_KEY_STRING);

      if (implied_rangelist)
        SVN_ERR(svn_rangelist_remove(filtered_rangelist,
                                     implied_rangelist,
                                     requested_rangelist,
                                     FALSE, pool));
    }

  /* If no filtering was performed the filtered rangelist is
     simply the requested rangelist.*/
  if (! (*filtered_rangelist))
    *filtered_rangelist = requested_rangelist;

  return SVN_NO_ERROR;
}

/* The single-file, simplified version of do_directory_merge(), which see for
   parameter descriptions.

   Additional parameters:

   If SOURCES_RELATED is set, the "left" and "right" sides of the
   merge source are historically related (ancestors, uncles, second
   cousins thrice removed, etc...).  (This is used to simulate the
   history checks that the repository logic does in the directory case.)

   If mergeinfo is being recorded to describe this merge, and RESULT_CATALOG
   is not NULL, then don't record the new mergeinfo on the TARGET_WCPATH,
   but instead record it in RESULT_CATALOG, where the key is TARGET_WCPATH
   and the value is the new mergeinfo for that path.  Allocate additions
   to RESULT_CATALOG in pool which RESULT_CATALOG was created in.

   Note: MERGE_B->RA_SESSION1 must be associated with URL1 and
   MERGE_B->RA_SESSION2 with URL2.
*/
static svn_error_t *
do_file_merge(svn_mergeinfo_catalog_t result_catalog,
              const char *url1,
              svn_revnum_t revision1,
              const char *url2,
              svn_revnum_t revision2,
              const char *target_wcpath,
              svn_boolean_t sources_related,
              svn_wc_adm_access_t *adm_access,
              notification_receiver_baton_t *notify_b,
              merge_cmd_baton_t *merge_b,
              apr_pool_t *pool)
{
  apr_hash_t *props1, *props2;
  const char *tmpfile1, *tmpfile2;
  const char *mimetype1, *mimetype2;
  svn_string_t *pval;
  apr_array_header_t *propchanges, *remaining_ranges;
  svn_wc_notify_state_t prop_state = svn_wc_notify_state_unknown;
  svn_wc_notify_state_t text_state = svn_wc_notify_state_unknown;
  svn_boolean_t tree_conflicted = FALSE;
  svn_client_ctx_t *ctx = merge_b->ctx;
  const char *mergeinfo_path;
  svn_merge_range_t range;
  svn_mergeinfo_t target_mergeinfo;
  const svn_wc_entry_t *entry;
  svn_merge_range_t *conflicted_range = NULL;
  int i;
  svn_boolean_t indirect = FALSE;
  apr_pool_t *subpool;
  svn_boolean_t is_rollback = (revision1 > revision2);
  const char *primary_url = is_rollback ? url1 : url2;
  svn_boolean_t honor_mergeinfo, record_mergeinfo;
  svn_client__merge_path_t *merge_target = NULL;

  mergeinfo_behavior(&honor_mergeinfo, &record_mergeinfo, merge_b);

  /* Note that this is a single-file merge. */
  notify_b->is_single_file_merge = TRUE;

  /* Ensure that the adm_access we're playing with is our TARGET_WCPATH's
     parent, as required by some of underlying helper functions. */
  SVN_ERR(svn_wc_adm_probe_try3(&adm_access, adm_access, target_wcpath,
                                TRUE, -1, merge_b->ctx->cancel_func,
                                merge_b->ctx->cancel_baton,
                                pool));

  SVN_ERR(svn_wc__entry_versioned(&entry, target_wcpath, adm_access, FALSE,
                                  pool));

  range.start = revision1;
  range.end = revision2;
  range.inheritable = TRUE;
  if (honor_mergeinfo)
    {
      const char *source_root_url;
      merge_target = apr_pcalloc(pool, sizeof(*merge_target));

      SVN_ERR(svn_ra_get_repos_root2(merge_b->ra_session1,
                                     &source_root_url, pool));
      SVN_ERR(svn_client__path_relative_to_root(&mergeinfo_path, primary_url,
                                                source_root_url, TRUE, NULL,
                                                NULL, pool));

      /* Fetch mergeinfo (temporarily reparenting ra_session1 to
         working copy target URL). */
      SVN_ERR(svn_ra_reparent(merge_b->ra_session1, entry->url, pool));
      SVN_ERR(get_full_mergeinfo(&target_mergeinfo,
                                 &(merge_target->implicit_mergeinfo),
                                 entry, &indirect, svn_mergeinfo_inherited,
                                 merge_b->ra_session1, target_wcpath,
                                 MAX(revision1, revision2),
                                 MIN(revision1, revision2),
                                 adm_access, ctx, pool));

      SVN_ERR(svn_ra_reparent(merge_b->ra_session1, url1, pool));

      /* Calculate remaining merges unless this is a record only merge.
         In that case the remaining range is the whole range described
         by REVISION1:REVISION2. */
      if (!merge_b->record_only)
        {
          SVN_ERR(calculate_remaining_ranges(NULL, merge_target,
                                             source_root_url,
                                             url1, revision1, url2, revision2,
                                             target_mergeinfo,
                                             merge_b->implicit_src_gap, FALSE,
                                             FALSE, merge_b->ra_session1,
                                             entry, adm_access, ctx, pool));
          remaining_ranges = merge_target->remaining_ranges;
        }
    }

  /* The simple cases where our remaining range is REVISION1:REVISION2. */
  if (!honor_mergeinfo || merge_b->record_only)
    {
      remaining_ranges = apr_array_make(pool, 1, sizeof(&range));
      APR_ARRAY_PUSH(remaining_ranges, svn_merge_range_t *) = &range;
    }

  subpool = svn_pool_create(pool);

  if (!merge_b->record_only)
    {
      apr_array_header_t *ranges_to_merge = remaining_ranges;

      /* If we have ancestrally related sources and more than one
         range to merge, eliminate no-op ranges before going through
         the effort of downloading the many copies of the file
         required to do these merges (two copies per range). */
      if (merge_b->sources_ancestral && (remaining_ranges->nelts > 1))
        {
          const char *old_sess_url = NULL;
          SVN_ERR(svn_client__ensure_ra_session_url(&old_sess_url,
                                                    merge_b->ra_session1,
                                                    primary_url, subpool));
          SVN_ERR(remove_noop_merge_ranges(&ranges_to_merge,
                                           merge_b->ra_session1,
                                           remaining_ranges, subpool));
          if (old_sess_url)
            SVN_ERR(svn_ra_reparent(merge_b->ra_session1, old_sess_url,
                                    subpool));
          svn_pool_clear(subpool);
        }

      for (i = 0; i < ranges_to_merge->nelts; i++)
        {
          svn_wc_notify_t *n;
          svn_boolean_t header_sent = FALSE;
          svn_error_t *err = SVN_NO_ERROR;
          svn_ra_session_t *ra_session1, *ra_session2;

          /* When using this merge range, account for the exclusivity of
             its low value (which is indicated by this operation being a
             merge vs. revert). */
          svn_merge_range_t *r = APR_ARRAY_IDX(ranges_to_merge, i,
                                               svn_merge_range_t *);

          svn_pool_clear(subpool);

          n = svn_wc_create_notify(target_wcpath,
                                   merge_b->same_repos
                                     ? svn_wc_notify_merge_begin
                                     : svn_wc_notify_foreign_merge_begin,
                                   subpool);
          if (merge_b->sources_ancestral)
            n->merge_range = r;

          /* Issue #3174: If we are honoring mergeinfo, then URL1, URL2,
             REVISION1, and REVISION2 meet the conditions described in
             'MERGEINFO MERGE SOURCE NORMALIZATION'.  This means that
             URL1@REVISION1 may be the copy source of URL2@REVISION2.
             If this is the case, then URL1 != URL2.  Since
             MERGE_B->RA_SESSION1 is always opened with URL1, the only time
             we can safely call single_file_merge_get_file() with that RA
             session is for REVISION1 (or REVISION2 if this is a reverse
             merge). */
          ra_session1 = merge_b->ra_session1;
          ra_session2 = merge_b->ra_session2;
          if (honor_mergeinfo && strcmp(url1, url2) != 0)
            {
              if (!is_rollback && r->start != revision1)
                ra_session1 = ra_session2; /* Use URL2's RA session. */
              else if (is_rollback && r->end != revision2)
                ra_session2 = ra_session1; /* Use URL1's RA session. */
            }

          /* While we currently don't allow it, in theory we could be
             fetching two fulltexts from two different repositories here. */
          SVN_ERR(single_file_merge_get_file(&tmpfile1, ra_session1,
                                             &props1, r->start, target_wcpath,
                                             subpool));
          SVN_ERR(single_file_merge_get_file(&tmpfile2, ra_session2,
                                             &props2, r->end, target_wcpath,
                                             subpool));

          /* Discover any svn:mime-type values in the proplists */
          pval = apr_hash_get(props1, SVN_PROP_MIME_TYPE,
                              strlen(SVN_PROP_MIME_TYPE));
          mimetype1 = pval ? pval->data : NULL;

          pval = apr_hash_get(props2, SVN_PROP_MIME_TYPE,
                              strlen(SVN_PROP_MIME_TYPE));
          mimetype2 = pval ? pval->data : NULL;

          /* Deduce property diffs. */
          SVN_ERR(svn_prop_diffs(&propchanges, props2, props1, subpool));

          /* If we aren't ignoring ancestry, then we've already done
             ancestry relatedness checks.  If we are ignoring ancestry, or
             our sources are known to be related, then we can do
             text-n-props merge; otherwise, we have to do a delete-n-add
             merge.  */
          if (! (merge_b->ignore_ancestry || sources_related))
            {
              /* Delete... */
              SVN_ERR(merge_file_deleted(adm_access,
                                         &text_state,
                                         &tree_conflicted,
                                         target_wcpath,
                                         tmpfile1,
                                         tmpfile2,
                                         mimetype1, mimetype2,
                                         props1,
                                         merge_b));
              single_file_merge_notify(notify_b, target_wcpath,
                                       tree_conflicted
                                         ? svn_wc_notify_tree_conflict
                                         : svn_wc_notify_update_delete,
                                       text_state,
                                       svn_wc_notify_state_unknown,
                                       n, &header_sent, subpool);

              /* ...plus add... */
              SVN_ERR(merge_file_added(adm_access,
                                       &text_state, &prop_state,
                                       &tree_conflicted,
                                       target_wcpath,
                                       tmpfile1,
                                       tmpfile2,
                                       r->start,
                                       r->end,
                                       mimetype1, mimetype2,
                                       propchanges, props1,
                                       merge_b));
              single_file_merge_notify(notify_b, target_wcpath,
                                       tree_conflicted
                                         ? svn_wc_notify_tree_conflict
                                         : svn_wc_notify_update_add,
                                       text_state, prop_state, n,
                                       &header_sent, subpool);
              /* ... equals replace. */
            }
          else
            {
              SVN_ERR(merge_file_changed(adm_access,
                                         &text_state, &prop_state,
                                         &tree_conflicted,
                                         target_wcpath,
                                         tmpfile1,
                                         tmpfile2,
                                         r->start,
                                         r->end,
                                         mimetype1, mimetype2,
                                         propchanges, props1,
                                         merge_b));
              single_file_merge_notify(notify_b, target_wcpath,
                                       tree_conflicted
                                         ? svn_wc_notify_tree_conflict
                                         : svn_wc_notify_update_update,
                                       text_state, prop_state, n,
                                       &header_sent, subpool);
            }

          /* Ignore if temporary file not found. It may have been renamed. */
          /* (This is where we complain about missing Lisp, or better yet,
             Python...) */
          err = svn_io_remove_file(tmpfile1, subpool);
          if (err && ! APR_STATUS_IS_ENOENT(err->apr_err))
            return err;
          svn_error_clear(err);
          err = svn_io_remove_file(tmpfile2, subpool);
          if (err && ! APR_STATUS_IS_ENOENT(err->apr_err))
            return err;
          svn_error_clear(err);

          if ((i < (ranges_to_merge->nelts - 1))
              && is_path_conflicted_by_merge(merge_b))
            {
              conflicted_range = r;
              break;
            }
        }
    } /* !merge_b->record_only */

  /* Record updated WC mergeinfo to account for our new merges, minus
     any unresolved conflicts and skips.  We use the original
     REMAINING_RANGES here instead of the possibly-pared-down
     RANGES_TO_MERGE because we want to record all the requested
     merge ranges, include the noop ones.  */
  if (record_mergeinfo && remaining_ranges->nelts)
    {
      apr_array_header_t *filtered_rangelist;

      /* Filter any ranges from TARGET_WCPATH's own history, there is no
         need to record this explicitly in mergeinfo, it is already part
         of TARGET_WCPATH's natural history (implicit mergeinfo). */
      SVN_ERR(filter_natural_history_from_mergeinfo(
        &filtered_rangelist,
        mergeinfo_path,
        merge_target->implicit_mergeinfo,
        &range, subpool));

      /* Only record mergeinfo if there is something other than
         self-referential mergeinfo, but don't record mergeinfo if
         TARGET_WCPATH was skipped. */
      if (filtered_rangelist->nelts
          && (!notify_b->skipped_paths
              || (apr_hash_count(notify_b->skipped_paths) == 0)))
        {
          apr_hash_t *merges = apr_hash_make(subpool);

          /* If merge target has indirect mergeinfo set it before
             recording the first merge range. */
          if (indirect)
            SVN_ERR(svn_client__record_wc_mergeinfo(target_wcpath,
                                                    target_mergeinfo,
                                                    adm_access, subpool));

          apr_hash_set(merges, target_wcpath, APR_HASH_KEY_STRING,
                       filtered_rangelist);
          SVN_ERR(update_wc_mergeinfo(result_catalog, target_wcpath, entry,
                                      mergeinfo_path, merges, is_rollback,
                                      adm_access, ctx, subpool));
        }
    }

  svn_pool_destroy(subpool);

  /* Caller must call svn_sleep_for_timestamps() */
  *(merge_b->use_sleep) = TRUE;

  /* If our multi-pass merge terminated early due to conflicts, return
     that fact as an error. */
  if (conflicted_range)
    return make_merge_conflict_error(target_wcpath, conflicted_range, pool);

  return SVN_NO_ERROR;
}

/* Helper for do_directory_merge() to handle the case were a merge editor
   drive adds explicit mergeinfo to a path which didn't have any explicit
   mergeinfo previously.

   MERGE_B, NOTIFY_B, ADM_ACCESS are cascaded from the arguments of the same
   name in do_directory_merge().  Should be called only after
   do_directory_merge() has called populate_remaining_ranges() and populated
   the remaining_ranges field of each child in
   NOTIFY_B->CHILDREN_WITH_MERGEINFO (i.e. the remaining_ranges fields can be
   empty but never NULL).

   If MERGE_B->DRY_RUN is true do nothing, if it is false then
   for each path (if any) in MERGE_B->PATHS_WITH_NEW_MERGEINFO merge that
   path's inherited mergeinfo (if any) with its working explicit mergeinfo
   and set that as the path's new explicit mergeinfo.  Then add an
   svn_client__merge_path_t * element representing the path to
   NOTIFY_B->CHILDREN_WITH_MERGEINFO if it isn't already present.  All fields
   in any elements added to NOTIFY_B->CHILDREN_WITH_MERGEINFO are initialized
   to FALSE/NULL with the exception of 'path' and 'remaining_ranges'.  The
   latter is set to a rangelist equal to the remaining_ranges of the path's
   nearest path-wise ancestor in NOTIFY_B->CHILDREN_WITH_MERGEINFO.

   Any elements added to NOTIFY_B->CHILDREN_WITH_MERGEINFO are allocated
   in POOL. */
static svn_error_t *
process_children_with_new_mergeinfo(merge_cmd_baton_t *merge_b,
                                    notification_receiver_baton_t *notify_b,
                                    svn_wc_adm_access_t *adm_access,
                                    apr_pool_t *pool)
{
  if (merge_b->paths_with_new_mergeinfo && !merge_b->dry_run)
    {
      apr_pool_t *iterpool = svn_pool_create(pool);
      apr_hash_index_t *hi;

      /* Iterate over each path with explicit mergeinfo added by the merge. */
      for (hi = apr_hash_first(NULL, merge_b->paths_with_new_mergeinfo);
           hi;
           hi = apr_hash_next(hi))
        {
          const void *key;
          const char *path_with_new_mergeinfo;
          const char *old_session_url = NULL;
          svn_mergeinfo_t path_inherited_mergeinfo;
          svn_mergeinfo_t path_explicit_mergeinfo;
          const svn_wc_entry_t *path_entry;
          svn_boolean_t indirect;
          svn_client__merge_path_t *new_child;

          apr_pool_clear(iterpool);
          apr_hash_this(hi, &key, NULL, NULL);
          path_with_new_mergeinfo = key;
          SVN_ERR(svn_wc__entry_versioned(&path_entry,
                                          path_with_new_mergeinfo,
                                          adm_access, FALSE,
                                          iterpool));

          /* Get the path's new explicit mergeinfo... */
          SVN_ERR(svn_client__get_wc_mergeinfo(&path_explicit_mergeinfo,
                                               &indirect, FALSE,
                                               svn_mergeinfo_explicit,
                                               path_entry,
                                               path_with_new_mergeinfo,
                                               NULL, NULL, adm_access,
                                               merge_b->ctx, iterpool));
          /* ...there *should* always be explicit mergeinfo at this point
             but you can't be too careful. */
          if (path_explicit_mergeinfo)
            {
              /* Temporarily reparent MERGE_B->RA_SESSION2 in case we need to
                 contact the repository for inherited mergeinfo. */
              SVN_ERR(svn_client__ensure_ra_session_url(&old_session_url,
                                                        merge_b->ra_session2,
                                                        path_entry->url,
                                                        iterpool));
              /* Get the mergeinfo the path would have inherited before
                 the merge. */
              SVN_ERR(svn_client__get_wc_or_repos_mergeinfo(
                &path_inherited_mergeinfo,
                path_entry,
                &indirect,
                FALSE,
                svn_mergeinfo_nearest_ancestor, /* We only want inherited MI */
                merge_b->ra_session2,
                path_with_new_mergeinfo,
                adm_access,
                merge_b->ctx,
                iterpool));

              /* If the path inherited any mergeinfo then merge that with the
                 explicit mergeinfo and record the result as the path's new
                 explicit mergeinfo. */
              if (path_inherited_mergeinfo)
                {
                  SVN_ERR(svn_mergeinfo_merge(path_explicit_mergeinfo,
                                              path_inherited_mergeinfo,
                                              iterpool));
                  SVN_ERR(svn_client__record_wc_mergeinfo(path_with_new_mergeinfo,
                                                          path_explicit_mergeinfo,
                                                          adm_access, iterpool));
                }

              /* If the path is not in NOTIFY_B->CHILDREN_WITH_MERGEINFO
                 then add it. */
              new_child =
                get_child_with_mergeinfo(notify_b->children_with_mergeinfo,
                                         path_with_new_mergeinfo);
              if (!new_child)
                {
                  int parent_index =
                    find_nearest_ancestor(notify_b->children_with_mergeinfo,
                                          FALSE, path_with_new_mergeinfo);
                  svn_client__merge_path_t *parent =
                    APR_ARRAY_IDX(notify_b->children_with_mergeinfo,
                                  parent_index,
                                  svn_client__merge_path_t *);
                  new_child = apr_pcalloc(pool, sizeof(*new_child));
                  new_child->path = apr_pstrdup(pool,
                                                path_with_new_mergeinfo);

                  /* If path_with_new_mergeinfo is the merge target itself
                     then it should already be in
                     NOTIFY_B->CHILDREN_WITH_MERGEINFO per the criteria of
                     get_mergeinfo_paths() and we shouldn't be in this block.
                     If path_with_new_mergeinfo is a subtree then it must have
                     a parent in NOTIFY_B->CHILDREN_WITH_MERGEINFO if only
                     the merge target itself...so if we don't find a parent
                     the caller has done something quite wrong. */
                  SVN_ERR_ASSERT(parent);
                  SVN_ERR_ASSERT(parent->remaining_ranges);

                  /* Set the path's remaining_ranges equal to its parent's. */
                  new_child->remaining_ranges = svn_rangelist_dup(
                     parent->remaining_ranges, pool);
                  insert_child_to_merge(notify_b->children_with_mergeinfo,
                                        new_child, pool);
                }
            }
          /* Return MERGE_B->RA_SESSION2 to its initial state if we
             reparented it. */
          if (old_session_url)
            SVN_ERR(svn_ra_reparent(merge_b->ra_session2, old_session_url,
                                    iterpool));
        }
      svn_pool_destroy(iterpool);
    }
  return SVN_NO_ERROR;
}

/* Helper for do_directory_merge() when performing mergeinfo unaware merges.

   Merge the diff of URL1@REVISION1:URL2@REVISION2 to TARGET_DIR_WCPATH.

   URL1, REVISION1, URL2, REVISION2, ADM_ACCESS, DEPTH, NOTIFY_B, and MERGE_B
   are all cascaded from do_directory_merge's arguments of the same names.

   NOTE: This is a very thin wrapper around drive_merge_report_editor() and
   exists only to populate NOTIFY_B->CHILREN_WITH_MERGEINFO with the single
   element expected during mergeinfo unaware merges..
*/
static svn_error_t *
do_mergeinfo_unaware_dir_merge(const char *url1,
                               svn_revnum_t revision1,
                               const char *url2,
                               svn_revnum_t revision2,
                               const char *target_dir_wcpath,
                               svn_wc_adm_access_t *adm_access,
                               svn_depth_t depth,
                               notification_receiver_baton_t *notify_b,
                               merge_cmd_baton_t *merge_b,
                               apr_pool_t *pool)
{
  /* Initialize NOTIFY_B->CHILDREN_WITH_MERGEINFO and populate it with
     one element describing the merge of REVISION1:REVISION2 to
     TARGET_DIR_WCPATH. */
  svn_client__merge_path_t *item = apr_pcalloc(pool, sizeof(*item));
  svn_merge_range_t *itemrange = apr_pcalloc(pool, sizeof(*itemrange));
  apr_array_header_t *remaining_ranges = apr_array_make(
    pool, 1, sizeof(svn_merge_range_t *));

  itemrange->start = revision1;
  itemrange->end = revision2;
  itemrange->inheritable = TRUE;

  APR_ARRAY_PUSH(remaining_ranges, svn_merge_range_t *) = itemrange;

  item->path = apr_pstrdup(pool, target_dir_wcpath);
  item->remaining_ranges = remaining_ranges;
  APR_ARRAY_PUSH(notify_b->children_with_mergeinfo,
                 svn_client__merge_path_t *) = item;
  return drive_merge_report_editor(target_dir_wcpath,
                                   url1, revision1, url2, revision2,
                                   NULL, depth, notify_b,
                                   adm_access, &merge_callbacks,
                                   merge_b, pool);
}

/* Helper for do_directory_merge().

   If RESULT_CATALOG is NULL then record mergeinfo describing a merge of
   MERGED_RANGE->START:MERGED_RANGE->END from the repository relative path
   MERGEINFO_PATH to the merge target (and possibly its subtrees) described
   by NOTIFY_B->CHILDREN_WITH_MERGEINFO -- see the global comment
   'THE CHILDREN_WITH_MERGEINFO ARRAY'.  Obviously this should only
   be called if recording mergeinfo -- see doc string for
   mergeinfo_behavior().

   If RESULT_CATALOG is not NULL, then don't record the new mergeinfo on the
   WC, but instead record it in RESULT_CATALOG, where the keys are
   working copy paths and the values are the new mergeinfos for each.
   The WC path keys are either absolute or relative to the current working
   directory in the same way NOTIFY_B->CHILDREN_WITH_MERGEINFO's
   svn_client__merge_path_t's PATH member is.
   Allocate additions to RESULT_CATALOG in pool which RESULT_CATALOG was
   created in.

   TARGET_ENTRY, DEPTH, NOTIFY_B, MERGE_B, and ADM_ACCESS are all cascaded
   from do_directory_merge's arguments of the same names.
*/
static svn_error_t *
record_mergeinfo_for_dir_merge(svn_mergeinfo_catalog_t result_catalog,
                               const svn_wc_entry_t *target_entry,
                               svn_merge_range_t *merged_range,
                               const char *mergeinfo_path,
                               svn_depth_t depth,
                               notification_receiver_baton_t *notify_b,
                               merge_cmd_baton_t *merge_b,
                               svn_wc_adm_access_t *adm_access,
                               apr_pool_t *pool)
{
  int i;
  int merge_target_len = strlen(merge_b->target);
  svn_boolean_t is_rollback = (merged_range->start > merged_range->end);

  /* Update the WC mergeinfo here to account for our new
     merges, minus any unresolved conflicts and skips. */

  /* We need a scratch pool for iterations below. */
  apr_pool_t *iterpool = svn_pool_create(pool);

  /* Remove absent children at or under TARGET_WCPATH from
     NOTIFY_B->SKIPPED_PATHS and NOTIFY_B->CHILDREN_WITH_MERGEINFO
     before we calculate the merges performed. */
  remove_absent_children(merge_b->target,
                         notify_b->children_with_mergeinfo, notify_b);

  for (i = 0; i < notify_b->children_with_mergeinfo->nelts; i++)
    {
      const char *child_repos_path;
      const char *child_merge_src_canon_path;
      const svn_wc_entry_t *child_entry;
      apr_array_header_t *child_merge_rangelist;
      apr_hash_t *child_merges;
      svn_client__merge_path_t *child =
                     APR_ARRAY_IDX(notify_b->children_with_mergeinfo, i,
                                   svn_client__merge_path_t *);
      SVN_ERR_ASSERT(child);
      if (child->absent)
        continue;

      svn_pool_clear(iterpool);

      if (strlen(child->path) == merge_target_len)
        child_repos_path = "";
      else
        child_repos_path = child->path +
          (merge_target_len ? merge_target_len + 1 : 0);
      child_merge_src_canon_path = svn_path_join(mergeinfo_path,
                                                 child_repos_path,
                                                 iterpool);
      SVN_ERR(svn_wc__entry_versioned(&child_entry, child->path,
                                      adm_access, FALSE, iterpool));

      /* Filter any ranges from each child's natural history before
         setting mergeinfo describing the merge. */
      SVN_ERR(filter_natural_history_from_mergeinfo(
        &child_merge_rangelist, child_merge_src_canon_path,
        child->implicit_mergeinfo, merged_range, iterpool));

      if (child_merge_rangelist->nelts == 0)
        continue;

      /* If we are here we know we will be recording some mergeinfo, but
         before we do set override mergeinfo on skipped paths so they
         don't incorrectly inherit the mergeinfo we are about to set.
         We only need to do this once. */
      if (i == 0)
        SVN_ERR(record_skips(mergeinfo_path,
                             target_entry, child_merge_rangelist,
                             is_rollback, adm_access,
                             notify_b, merge_b, iterpool));

      SVN_ERR(calculate_merge_inheritance(child_merge_rangelist, child->path,
                                          child_entry, i == 0,
                                          child->missing_child,
                                          depth));

      /* If CHILD has indirect mergeinfo set it before
         recording the first merge range. */
      if (child->indirect_mergeinfo)
        SVN_ERR(svn_client__record_wc_mergeinfo(child->path,
                                                child->pre_merge_mergeinfo,
                                                adm_access,
                                                iterpool));
      if (merge_b->implicit_src_gap)
        {
          /* If this is a reverse merge reorder CHILD->REMAINING_RANGES
             so it will work with the svn_rangelist_remove API. */
          if (is_rollback)
            SVN_ERR(svn_rangelist_reverse(child_merge_rangelist, iterpool));

            SVN_ERR(svn_rangelist_remove(&child_merge_rangelist,
                                         merge_b->implicit_src_gap,
                                         child_merge_rangelist, FALSE,
                                         iterpool));
          if (is_rollback)
            SVN_ERR(svn_rangelist_reverse(child_merge_rangelist, iterpool));
        }

      child_merges = apr_hash_make(iterpool);
      apr_hash_set(child_merges, child->path, APR_HASH_KEY_STRING,
                   child_merge_rangelist);
      SVN_ERR(update_wc_mergeinfo(result_catalog, child->path, child_entry,
                                  child_merge_src_canon_path,
                                  child_merges, is_rollback,
                                  adm_access, merge_b->ctx, iterpool));

      /* Elide explicit subtree mergeinfo. */
      if (i > 0)
        {
          svn_boolean_t in_switched_subtree = FALSE;

          if (child->switched)
            in_switched_subtree = TRUE;
          else if (i > 1)
            {
              /* Check if CHILD is part of a switched subtree */
              svn_client__merge_path_t *parent;
              int j = i - 1;
              for (; j > 0; j--)
                {
                  parent = APR_ARRAY_IDX(notify_b->children_with_mergeinfo,
                                         j, svn_client__merge_path_t *);
                  if (parent
                      && parent->switched
                      && svn_path_is_ancestor(parent->path, child->path))
                    {
                      in_switched_subtree = TRUE;
                      break;
                    }
                }
            }

          /* Allow mergeinfo on switched subtrees to elide to the
             repository. Otherwise limit elision to the merge target
             for now.  do_directory_merge() will eventually try to
             elide that when the merge is complete. */
          SVN_ERR(svn_client__elide_mergeinfo(
            child->path,
            in_switched_subtree ? NULL : merge_b->target,
            child_entry, adm_access, merge_b->ctx, iterpool));
        }
    } /* (i = 0; i < notify_b->children_with_mergeinfo->nelts; i++) */

  svn_pool_destroy(iterpool);
  return SVN_NO_ERROR;
}

/* Helper for do_directory_merge().

   Similar to record_mergeinfo_for_dir_merge in that it records mergeinfo
   describing a merge of MERGED_RANGE->START:MERGED_RANGE->END from the
   repository relative path MERGEINFO_PATH to the directory represented by
   TARGET_ENTRY.  Unlike record_mergeinfo_for_dir_merge() though, this
   funtion only records mergeinfo on *new* subtrees added by the merge which
   are children of paths with non-inheritable ranges or which have missing
   siblings - see criteria 3 and 5 in the doc string for get_mergeinfo_paths.
   See also issue #2829
   http://subversion.tigris.org/issues/show_bug.cgi?id=2829#desc14.

   DEPTH, NOTIFY_B, MERGE_B, and ADM_ACCESS are all cascaded from
   do_directory_merge's arguments of the same names.
*/
static svn_error_t *
record_mergeinfo_for_added_subtrees(svn_merge_range_t *merged_range,
                                    const char *mergeinfo_path,
                                    svn_depth_t depth,
                                    notification_receiver_baton_t *notify_b,
                                    merge_cmd_baton_t *merge_b,
                                    svn_wc_adm_access_t *adm_access,
                                    apr_pool_t *pool)
{
  /* If no paths were added by the merge then we have nothing to do. */
  if (notify_b->added_paths)
    {
      apr_pool_t *iterpool = svn_pool_create(pool);
      apr_hash_index_t *hi;

      for (hi = apr_hash_first(NULL, notify_b->added_paths); hi;
           hi = apr_hash_next(hi))
        {
          const void *key;
          const char *added_path;
          const svn_string_t *added_path_parent_propval;

          apr_hash_this(hi, &key, NULL, NULL);
          added_path = key;

          apr_pool_clear(iterpool);

          /* Rather than using svn_client__get_wc_mergeinfo() and
             analyzing the mergeinfo it returns to determine if
             ADDED_PATH's parent has non-inheritable mergeinfo, it is
             much simpler to just get the svn_string_t representation
             of the svn:mergeinfo prop and look for the '*'
             non-inheritable marker. */
          SVN_ERR(svn_wc_prop_get(&added_path_parent_propval,
                                  SVN_PROP_MERGEINFO,
                                  svn_dirent_dirname(added_path, iterpool),
                                  adm_access, iterpool));
          if (added_path_parent_propval
              && strstr(added_path_parent_propval->data,
                        SVN_MERGEINFO_NONINHERITABLE_STR))
            {
              /* ADDED_PATH's immediate parent has non-inheritable
                 mergeinfo. */
              svn_client__merge_path_t *target_merge_path =
                APR_ARRAY_IDX(notify_b->children_with_mergeinfo, 0,
                              svn_client__merge_path_t *);
              svn_boolean_t inherited;
              svn_merge_range_t *rng;
              svn_mergeinfo_t merge_mergeinfo, added_path_mergeinfo;
              apr_array_header_t *rangelist;
              const svn_wc_entry_t *entry;
              const char *rel_added_path,
                *abs_added_path, *abs_target_path,
                *added_path_mergeinfo_path;

              SVN_ERR(svn_wc__entry_versioned(&entry, added_path,
                                              adm_access, FALSE,
                                              iterpool));

              /* Calculate the mergeinfo resulting from this merge. */
              merge_mergeinfo = apr_hash_make(iterpool);
              rangelist = apr_array_make(iterpool, 1,
                                         sizeof(svn_merge_range_t *));
              rng = svn_merge_range_dup(merged_range, iterpool);
              if (entry->kind == svn_node_file)
                rng->inheritable = TRUE;
              else
                rng->inheritable =
                  (!(depth == svn_depth_infinity
                     || depth == svn_depth_immediates));
              APR_ARRAY_PUSH(rangelist, svn_merge_range_t *) = rng;

              /* Create the new mergeinfo path for
                 added_path's mergeinfo. */
              SVN_ERR(svn_path_get_absolute(&abs_target_path,
                                            target_merge_path->path,
                                            iterpool));
              SVN_ERR(svn_path_get_absolute(&abs_added_path,
                                            added_path,
                                            iterpool));

              /* abs_added_path had better be a child of abs_target_path
                 or something is *really* wrong. */

              rel_added_path = svn_path_is_child(abs_target_path,
                                                 abs_added_path,
                                                 iterpool);
              SVN_ERR_ASSERT(rel_added_path);
              added_path_mergeinfo_path = svn_path_join(mergeinfo_path,
                                                        rel_added_path,
                                                        iterpool);
              apr_hash_set(merge_mergeinfo,
                           added_path_mergeinfo_path,
                           APR_HASH_KEY_STRING, rangelist);

              /* Get any explicit mergeinfo the added path has. */
              SVN_ERR(svn_client__get_wc_mergeinfo(
                &added_path_mergeinfo, &inherited, FALSE,
                svn_mergeinfo_explicit, entry, added_path,
                NULL, NULL, adm_access, merge_b->ctx, iterpool));

              /* Combine the explict mergeinfo on the added path (if any)
                 with the mergeinfo for this merge. */
              if (added_path_mergeinfo)
                SVN_ERR(svn_mergeinfo_merge(merge_mergeinfo,
                                            added_path_mergeinfo,
                                            iterpool));
              SVN_ERR(svn_client__record_wc_mergeinfo(added_path,
                                                      merge_mergeinfo,
                                                      adm_access,
                                                      iterpool));
            }
        }
    }
  return SVN_NO_ERROR;
}

/* Helper for do_merge() when the merge target is a directory.

   Perform a merge of changes between URL1@REVISION1 and URL2@REVISION2
   to the working copy path represented by TARGET_ENTRY and ADM_ACCESS.
   URL1, URL2, and TARGET_ENTRY all represent directories -- for the
   single file case, the caller should use do_file_merge().

   MERGE_B is the merge_cmd_baton_t created by do_merge() that describes
   the merge being performed.  If MERGE_B->sources_ancestral is set, then
   URL1@REVISION1 must be a historical ancestor of URL2@REVISION2, or
   vice-versa (see `MERGEINFO MERGE SOURCE NORMALIZATION' for more
   requirements around the values of URL1, REVISION1, URL2, and REVISION2
   in this case).

   If mergeinfo is being recorded to describe this merge, and RESULT_CATALOG
   is not NULL, then don't record the new mergeinfo on the WC, but instead
   record it in RESULT_CATALOG, where the keys are working copy paths and the
   values are the new mergeinfos for each.  The WC path keys are either
   absolute or relative to the current working directory in the same way
   NOTIFY_B->CHILDREN_WITH_MERGEINFO's svn_client__merge_path_t's PATH member
   is.  Allocate additions to RESULT_CATALOG in pool which RESULT_CATALOG was
   created in.

   Handle DEPTH as documented for svn_client_merge3().

   NOTE: This is a wrapper around drive_merge_report_editor() which
   handles the complexities inherent to situations where a given
   directory's children may have intersecting merges (because they
   meet one or more of the criteria described in get_mergeinfo_paths()).
*/
static svn_error_t *
do_directory_merge(svn_mergeinfo_catalog_t result_catalog,
                   const char *url1,
                   svn_revnum_t revision1,
                   const char *url2,
                   svn_revnum_t revision2,
                   const svn_wc_entry_t *target_entry,
                   svn_wc_adm_access_t *adm_access,
                   svn_depth_t depth,
                   notification_receiver_baton_t *notify_b,
                   merge_cmd_baton_t *merge_b,
                   apr_pool_t *pool)
{
  svn_error_t *err = SVN_NO_ERROR;
  svn_merge_range_t range;
  svn_ra_session_t *ra_session;
  const char *target_wcpath = svn_wc_adm_access_path(adm_access);
  svn_client__merge_path_t *target_merge_path;
  svn_boolean_t is_rollback = (revision1 > revision2);
  const char *primary_url = is_rollback ? url1 : url2;
  const char *source_root_url, *mergeinfo_path;
  svn_boolean_t honor_mergeinfo, record_mergeinfo;
  svn_boolean_t same_urls = (strcmp(url1, url2) == 0);

  mergeinfo_behavior(&honor_mergeinfo, &record_mergeinfo, merge_b);

  /* Initialize NOTIFY_B->CHILDREN_WITH_MERGEINFO. See the comment
     'THE CHILDREN_WITH_MERGEINFO ARRAY' at the start of this file. */
  notify_b->children_with_mergeinfo =
    apr_array_make(pool, 0, sizeof(svn_client__merge_path_t *));

  /* If we are not honoring mergeinfo we can skip right to the
     business of merging changes! */
  if (!honor_mergeinfo)
    return do_mergeinfo_unaware_dir_merge(url1, revision1, url2, revision2,
                                          target_wcpath, adm_access, depth,
                                          notify_b, merge_b, pool);

  /*** If we get here, we're dealing with related sources from the
       same repository as the target -- merge tracking might be
       happenin'! ***/

  /* Point our RA_SESSION to the URL of our youngest merge source side. */
  ra_session = is_rollback ? merge_b->ra_session1 : merge_b->ra_session2;

  /* Fill NOTIFY_B->CHILDREN_WITH_MERGEINFO with child paths (const
     svn_client__merge_path_t *) which might have intersecting merges
     because they meet one or more of the criteria described in
     get_mergeinfo_paths(). Here the paths are arranged in a depth
     first order. */
  SVN_ERR(svn_ra_get_repos_root2(ra_session, &source_root_url, pool));
  SVN_ERR(svn_client__path_relative_to_root(&mergeinfo_path, primary_url,
                                            source_root_url, TRUE, NULL,
                                            NULL, pool));
  SVN_ERR(get_mergeinfo_paths(notify_b->children_with_mergeinfo, merge_b,
                              mergeinfo_path, target_entry, source_root_url,
                              url1, url2, revision1, revision2,
                              honor_mergeinfo, ra_session, adm_access,
                              depth, pool));

  /* The first item from the NOTIFY_B->CHILDREN_WITH_MERGEINFO is always
     the target thanks to depth-first ordering. */
  target_merge_path = APR_ARRAY_IDX(notify_b->children_with_mergeinfo, 0,
                                    svn_client__merge_path_t *);
  merge_b->target_missing_child = target_merge_path->missing_child;

  /* If we are honoring mergeinfo, then for each item in
     NOTIFY_B->CHILDREN_WITH_MERGEINFO, we need to calculate what needs to be
     merged, and then merge it.  Otherwise, we just merge what we were asked
     to merge across the whole tree.  */
  SVN_ERR(populate_remaining_ranges(notify_b->children_with_mergeinfo,
                                    source_root_url,
                                    url1, revision1, url2, revision2,
                                    honor_mergeinfo,
                                    ra_session, mergeinfo_path,
                                    adm_access, merge_b, pool));

  /* Always start with a range which describes the most inclusive merge
     possible, i.e. REVISION1:REVISION2. */
  range.start = revision1;
  range.end = revision2;
  range.inheritable = TRUE;

  if (honor_mergeinfo && !merge_b->record_only)
    {
      svn_revnum_t start_rev, end_rev;

      /* The merge target target_wcpath and/or its subtrees may not need all
         of REVISION1:REVISION2 applied.  So examine
         NOTIFY_B->CHILDREN_WITH_MERGEINFO to find the oldest starting
         revision that actually needs to be merged (for reverse merges this is
         the youngest starting revision). */
      start_rev =
        get_most_inclusive_start_rev(notify_b->children_with_mergeinfo,
                                     is_rollback);

      /* Is there anything to merge? */
      if (SVN_IS_VALID_REVNUM(start_rev))
        {
          apr_pool_t *iterpool;

          /* Adjust range to describe the start of our most
             inclusive merge. */
          range.start = start_rev;

          /* Now examine NOTIFY_B->CHILDREN_WITH_MERGEINFO to find the youngest
             ending revision that actually needs to be merged (for reverse
             merges this is the oldest starting revision). */
           end_rev =
             get_most_inclusive_end_rev(notify_b->children_with_mergeinfo,
                                        is_rollback);

          /* While END_REV is valid, do the following:

             1. Tweak each NOTIFY_B->CHILDREN_WITH_MERGEINFO element so that
                the element's remaing_ranges member has as it's first element
                a range that ends with end_rev.

             2. Starting with start_rev, call drive_merge_report_editor()
                on MERGE_B->target for start_rev:end_rev.

             3. Remove the first element from each
                NOTIFY_B->CHILDREN_WITH_MERGEINFO element's remaining_ranges
                member.

             4. Again examine NOTIFY_B->CHILDREN_WITH_MERGEINFO to find the most
                inclusive starting revision that actually needs to be merged and
                update start_rev.  This prevents us from needlessly contacting the
                repository and doing a diff where we describe the entire target
                tree as *not* needing any of the requested range.  This can happen
                whenever we have mergeinfo with gaps in it for the merge source.

             5. Again examine NOTIFY_B->CHILDREN_WITH_MERGEINFO to find the most
                inclusive ending revision that actually needs to be merged and
                update end_rev.

             6. Lather, rinse, repeat.
          */
          iterpool = svn_pool_create(pool);
          while (end_rev != SVN_INVALID_REVNUM)
            {
              svn_revnum_t next_end_rev;
              const char *real_url1 = url1, *real_url2 = url2;
              const char *old_sess1_url = NULL, *old_sess2_url = NULL;

              svn_pool_clear(iterpool);

              /* Use persistent pool while playing with remaining_ranges. */
              slice_remaining_ranges(notify_b->children_with_mergeinfo,
                                     is_rollback, end_rev, pool);
              notify_b->cur_ancestor_index = -1;

              /* URL1@REVISION1 is a real location; URL2@REVISION2 is a
                 real location -- that much we know (thanks to the merge
                 source normalization code).  But for revisions between
                 them, the URLs might differ.  Here are the rules:

                   * If URL1 == URL2, then all URLs between REVISION1 and
                     REVISION2 also match URL1/URL2.

                   * If URL1 != URL2, then:

                       * If REVISION1 < REVISION2, only REVISION1 maps to
                         URL1.  The revisions between REVISION1+1 and
                         REVISION2 (inclusive) map to URL2.

                       * If REVISION1 > REVISION2, Only REVISION2 maps to
                         URL2.  The revisions between REVISION1 and
                         REVISION2+1 (inclusive) map to URL1.

                 We need to adjust our URLs accordingly, here.
              */
              if (! same_urls)
                {
                  if (is_rollback && (end_rev != revision2))
                    {
                      real_url2 = url1;
                      SVN_ERR(svn_client__ensure_ra_session_url
                              (&old_sess2_url, merge_b->ra_session2,
                               real_url2, iterpool));
                    }
                  if ((! is_rollback) && (start_rev != revision1))
                    {
                      real_url1 = url2;
                      SVN_ERR(svn_client__ensure_ra_session_url
                              (&old_sess1_url, merge_b->ra_session1,
                               real_url1, iterpool));
                    }
                }
              SVN_ERR(drive_merge_report_editor(
                merge_b->target,
                real_url1, start_rev,
                real_url2, end_rev,
                notify_b->children_with_mergeinfo,
                depth, notify_b, adm_access,
                &merge_callbacks, merge_b,
                iterpool));
              if (old_sess1_url)
                SVN_ERR(svn_ra_reparent(merge_b->ra_session1,
                                        old_sess1_url, iterpool));
              if (old_sess2_url)
                SVN_ERR(svn_ra_reparent(merge_b->ra_session2,
                                        old_sess2_url, iterpool));

              /* If any paths picked up explicit mergeinfo as a result of
                 the merge we need to make sure any mergeinfo those paths
                 inherited is recorded and then add these paths to
                 MERGE_B->CHILDREN_WITH_MERGEINFO.*/
              SVN_ERR(process_children_with_new_mergeinfo(merge_b, notify_b,
                                                          adm_access, pool));

              /* If any subtrees had their explicit mergeinfo deleted as a
                 result of the merge then remove these paths from
                 MERGE_B->CHILDREN_WITH_MERGEINFO since there is no need
                 to consider these subtrees for subsequent editor drives
                 nor do we want to record mergeinfo on them describing
                 the merge itself. */
              remove_children_with_deleted_mergeinfo(merge_b, notify_b);

              /* Prepare for the next iteration (if any). */
              remove_first_range_from_remaining_ranges(
                end_rev, notify_b->children_with_mergeinfo, pool);
              next_end_rev =
                get_most_inclusive_end_rev(notify_b->children_with_mergeinfo,
                                           is_rollback);
              if ((next_end_rev != SVN_INVALID_REVNUM)
                  && is_path_conflicted_by_merge(merge_b))
                {
                  svn_merge_range_t conflicted_range;
                  conflicted_range.start = start_rev;
                  conflicted_range.end = end_rev;
                  err = make_merge_conflict_error(merge_b->target,
                                                  &conflicted_range, pool);
                  range.end = end_rev;
                  break;
                }
              start_rev =
                get_most_inclusive_start_rev(notify_b->children_with_mergeinfo,
                                             is_rollback);
              end_rev = next_end_rev;
            }
          svn_pool_destroy(iterpool);
        }
    }
  else
    {
      if (!merge_b->record_only)
        {
          /* Reset cur_ancestor_index to -1 so that subsequent cherry
             picked revision ranges will be notified upon subsequent
             operative merge. */
          notify_b->cur_ancestor_index = -1;

          SVN_ERR(drive_merge_report_editor(merge_b->target,
                                            url1, revision1, url2, revision2,
                                            NULL,
                                            depth, notify_b, adm_access,
                                            &merge_callbacks, merge_b,
                                            pool));
        }
    }

  /* Record mergeinfo where appropriate.*/
  if (record_mergeinfo)
    {
      SVN_ERR(record_mergeinfo_for_dir_merge(result_catalog,
                                             target_entry,
                                             &range,
                                             mergeinfo_path,
                                             depth,
                                             notify_b,
                                             merge_b,
                                             adm_access,
                                             pool));

      /* If a path has an immediate parent with non-inheritable mergeinfo at
         this point, then it meets criteria 3 or 5 described in
         get_mergeinfo_paths' doc string.  For paths which exist prior to a
         merge explicit mergeinfo has already been set.  But for paths added
         during the merge this is not the case.  The path might have explicit
         mergeinfo from the merge source, but no mergeinfo yet exists
         describing *this* merge.  So the added path has either incomplete
         explicit mergeinfo or inherits incomplete mergeinfo from its
         immediate parent (if any, the parent might have only non-inheritable
         ranges in which case the path simply inherits empty mergeinfo).

         So here we look at the root path of each subtree added during the
         merge and set explicit mergeinfo on it if it meets the aforementioned
         conditions. */
      SVN_ERR(record_mergeinfo_for_added_subtrees(&range,
                                                  mergeinfo_path,
                                                  depth,
                                                  notify_b,
                                                  merge_b,
                                                  adm_access,
                                                  pool));
    }

  return err;
}

/** Ensure that *RA_SESSION is opened to URL, either by reusing
 * *RA_SESSION if it is non-null and already opened to URL's
 * repository, or by allocating a new *RA_SESSION in POOL.
 * (RA_SESSION itself cannot be null, of course.)
 *
 * CTX is used as for svn_client__open_ra_session_internal().
 */
static svn_error_t *
ensure_ra_session_url(svn_ra_session_t **ra_session,
                      const char *url,
                      svn_client_ctx_t *ctx,
                      apr_pool_t *pool)
{
  svn_error_t *err = SVN_NO_ERROR;

  if (*ra_session)
    {
      const char *old_session_url;
      err = svn_client__ensure_ra_session_url(&old_session_url,
                                              *ra_session,
                                              url,
                                              pool);
    }

  /* SVN_ERR_RA_ILLEGAL_URL is raised when url doesn't point to the same
     repository as ra_session. */
  if (! *ra_session || (err && err->apr_err == SVN_ERR_RA_ILLEGAL_URL))
    {
      svn_error_clear(err);
      err = svn_client__open_ra_session_internal(ra_session, url,
                                                 NULL, NULL, NULL,
                                                 FALSE, TRUE, ctx, pool);
    }
  SVN_ERR(err);

  return SVN_NO_ERROR;
}

/* Drive a merge of MERGE_SOURCES into working copy path TARGET (with
   associated TARGET_ENTRY and ADM_ACCESS baton).

   If SOURCES_ANCESTRAL is set, then for every merge source in
   MERGE_SOURCES, the "left" and "right" side of the merge source are
   ancestrally related.  (See 'MERGEINFO MERGE SOURCE NORMALIZATION'
   for more on what that means and how it matters.)

   If SOURCES_RELATED is set, the "left" and "right" sides of the
   merge source are historically related (ancestors, uncles, second
   cousins thrice removed, etc...).  (This is passed through to
   do_file_merge() to simulate the history checks that the repository
   logic does in the directory case.)

   SAME_REPOS is TRUE iff the merge sources live in the same
   repository as the one from which the target working copy has been
   checked out.

   If mergeinfo is being recorded to describe this merge, and RESULT_CATALOG
   is not NULL, then don't record the new mergeinfo on the WC, but instead
   record it in RESULT_CATALOG, where the keys are working copy paths and the
   values are the new mergeinfos for each.  The WC path keys are absolute if
   TARGET is absolute and relative to the current working directory otherwise.
   Allocate additions to RESULT_CATALOG in pool which RESULT_CATALOG was
   created in.

   FORCE, DRY_RUN, RECORD_ONLY, IGNORE_ANCESTRY, DEPTH, MERGE_OPTIONS,
   and CTX are as described in the docstring for svn_client_merge_peg3().

   REINTEGRATE_MERGE is TRUE if this is a reintegrate merge.

   *USE_SLEEP will be set TRUE if a sleep is required to ensure timestamp
   integrity, *USE_SLEEP will be unchanged if no sleep is required.
*/
static svn_error_t *
do_merge(svn_mergeinfo_catalog_t result_catalog,
         apr_array_header_t *merge_sources,
         const char *target,
         const svn_wc_entry_t *target_entry,
         svn_wc_adm_access_t *adm_access,
         svn_boolean_t sources_ancestral,
         svn_boolean_t sources_related,
         svn_boolean_t same_repos,
         svn_boolean_t ignore_ancestry,
         svn_boolean_t force,
         svn_boolean_t dry_run,
         svn_boolean_t record_only,
         svn_boolean_t reintegrate_merge,
         svn_depth_t depth,
         const apr_array_header_t *merge_options,
         svn_boolean_t *use_sleep,
         svn_client_ctx_t *ctx,
         apr_pool_t *pool)
{
  apr_pool_t *subpool = svn_pool_create(pool);
  merge_cmd_baton_t merge_cmd_baton;
  notification_receiver_baton_t notify_baton;
  svn_config_t *cfg;
  const char *diff3_cmd;
  int i;
  svn_boolean_t checked_mergeinfo_capability = FALSE;
  svn_ra_session_t *ra_session1 = NULL, *ra_session2 = NULL;

  /* Check from some special conditions when in record-only mode
     (which is a merge-tracking thing). */
  if (record_only)
    {
      /* We can't do a record-only merge if the sources aren't related. */
      if (! sources_ancestral)
        return svn_error_create(SVN_ERR_INCORRECT_PARAMS, NULL,
                                _("Use of two URLs is not compatible with "
                                  "mergeinfo modification"));

      /* We can't do a record-only merge if the sources aren't from
         the same repository as the target. */
      if (! same_repos)
        return svn_error_create(SVN_ERR_INCORRECT_PARAMS, NULL,
                                _("Merge from foreign repository is not "
                                  "compatible with mergeinfo modification"));

      /* If this is a dry-run record-only merge, there's nothing to do. */
      if (dry_run)
        return SVN_NO_ERROR;
    }

  /* Ensure a known depth. */
  if (depth == svn_depth_unknown)
    depth = svn_depth_infinity;

  /* Set up the diff3 command, so various callers don't have to. */
  cfg = ctx->config ? apr_hash_get(ctx->config, SVN_CONFIG_CATEGORY_CONFIG,
                                   APR_HASH_KEY_STRING) : NULL;
  svn_config_get(cfg, &diff3_cmd, SVN_CONFIG_SECTION_HELPERS,
                 SVN_CONFIG_OPTION_DIFF3_CMD, NULL);


  /* Build the merge context baton (or at least the parts of it that
     don't need to be reset for each merge source).  */
  merge_cmd_baton.force = force;
  merge_cmd_baton.dry_run = dry_run;
  merge_cmd_baton.record_only = record_only;
  merge_cmd_baton.ignore_ancestry = ignore_ancestry;
  merge_cmd_baton.same_repos = same_repos;
  merge_cmd_baton.mergeinfo_capable = FALSE;
  merge_cmd_baton.sources_ancestral = sources_ancestral;
  merge_cmd_baton.ctx = ctx;
  merge_cmd_baton.target_missing_child = FALSE;
  merge_cmd_baton.reintegrate_merge = reintegrate_merge;
  merge_cmd_baton.target = target;
  merge_cmd_baton.pool = subpool;
  merge_cmd_baton.merge_options = merge_options;
  merge_cmd_baton.diff3_cmd = diff3_cmd;
  merge_cmd_baton.use_sleep = use_sleep;

  /* Build the notification receiver baton. */
  notify_baton.wrapped_func = ctx->notify_func2;
  notify_baton.wrapped_baton = ctx->notify_baton2;
  notify_baton.nbr_notifications = 0;
  notify_baton.nbr_operative_notifications = 0;
  notify_baton.merged_paths = NULL;
  notify_baton.skipped_paths = NULL;
  notify_baton.added_paths = NULL;
  notify_baton.is_single_file_merge = FALSE;
  notify_baton.children_with_mergeinfo = NULL;
  notify_baton.cur_ancestor_index = -1;
  notify_baton.merge_b = &merge_cmd_baton;
  notify_baton.pool = pool;

  for (i = 0; i < merge_sources->nelts; i++)
    {
      merge_source_t *merge_source =
        APR_ARRAY_IDX(merge_sources, i, merge_source_t *);
      const char *url1, *url2;
      svn_revnum_t rev1, rev2;

      svn_pool_clear(subpool);

      /* Convenience variables. */
      url1 = merge_source->url1;
      url2 = merge_source->url2;
      rev1 = merge_source->rev1;
      rev2 = merge_source->rev2;

      /* Sanity check:  if our left- and right-side merge sources are
         the same, there's nothing to here. */
      if ((strcmp(url1, url2) == 0) && (rev1 == rev2))
        continue;

      /* Establish RA sessions to our URLs, reuse where possible. */
      SVN_ERR(ensure_ra_session_url(&ra_session1, url1, ctx, pool));
      SVN_ERR(ensure_ra_session_url(&ra_session2, url2, ctx, pool));

      /* Populate the portions of the merge context baton that need to
         be reset for each merge source iteration. */
      merge_cmd_baton.merge_source = *merge_source;
      merge_cmd_baton.implicit_src_gap = NULL;
      merge_cmd_baton.added_path = NULL;
      merge_cmd_baton.add_necessitated_merge = FALSE;
      merge_cmd_baton.dry_run_deletions =
        dry_run ? apr_hash_make(subpool) : NULL;
      merge_cmd_baton.conflicted_paths = NULL;
      merge_cmd_baton.paths_with_new_mergeinfo = NULL;
      merge_cmd_baton.paths_with_deleted_mergeinfo = NULL;
      merge_cmd_baton.ra_session1 = ra_session1;
      merge_cmd_baton.ra_session2 = ra_session2;

      /* Populate the portions of the merge context baton that require
         an RA session to set, but shouldn't be reset for each iteration. */
      if (! checked_mergeinfo_capability)
        {
          SVN_ERR(svn_ra_has_capability(ra_session1,
                                        &merge_cmd_baton.mergeinfo_capable,
                                        SVN_RA_CAPABILITY_MERGEINFO, subpool));
          checked_mergeinfo_capability = TRUE;
        }

      /* Call our merge helpers based on entry kind. */
      if (target_entry->kind == svn_node_file)
        {
          SVN_ERR(do_file_merge(result_catalog,
                                url1, rev1, url2, rev2, target,
                                sources_related, adm_access, &notify_baton,
                                &merge_cmd_baton, subpool));
        }
      else if (target_entry->kind == svn_node_dir)
        {
          SVN_ERR(do_directory_merge(result_catalog,
                                     url1, rev1, url2, rev2, target_entry,
                                     adm_access, depth, &notify_baton,
                                     &merge_cmd_baton, subpool));
        }

      /* The final mergeinfo on TARGET_WCPATH may itself elide. */
      if (! dry_run)
        SVN_ERR(svn_client__elide_mergeinfo(target, NULL, target_entry,
                                            adm_access, ctx, subpool));
    }

  /* Let everyone know we're finished here. */
  if (ctx->notify_func2)
    {
      svn_wc_notify_t *notify
        = svn_wc_create_notify(target, svn_wc_notify_merge_completed,
                               subpool);
      notify->kind = svn_node_none;
      notify->content_state = notify->prop_state
        = svn_wc_notify_state_inapplicable;
      notify->lock_state = svn_wc_notify_lock_state_inapplicable;
      notify->revision = SVN_INVALID_REVNUM;
      (*ctx->notify_func2)(ctx->notify_baton2, notify, pool);
    }

  svn_pool_destroy(subpool);
  return SVN_NO_ERROR;
}

/* Perform a two-URL merge between URLs which are related, but neither
   is a direct ancestor of the other.  This first does a real two-URL
   merge (unless this is record-only), followed by record-only merges
   to represent the changed mergeinfo.

   The merge is between URL1@REV1 (in URL1_RA_SESSION1) and URL2@REV2 (in
   URL2_RA_SESSION2); YC_REV is their youngest common ancestor.
   SOURCE_REPOS_ROOT and WC_REPOS_ROOT are the repository roots of the
   source URL and the target working copy.  ENTRY is the wc entry for
   TARGET_WCPATH.  Other arguments are as in all of the public merge
   APIs.

   *USE_SLEEP will be set TRUE if a sleep is required to ensure timestamp
   integrity, *USE_SLEEP will be unchanged if no sleep is required.
 */
static svn_error_t *
merge_cousins_and_supplement_mergeinfo(const char *target_wcpath,
                                       const svn_wc_entry_t *entry,
                                       svn_wc_adm_access_t *adm_access,
                                       svn_ra_session_t *URL1_ra_session,
                                       svn_ra_session_t *URL2_ra_session,
                                       const char *URL1,
                                       svn_revnum_t rev1,
                                       const char *URL2,
                                       svn_revnum_t rev2,
                                       svn_revnum_t yc_rev,
                                       const char *source_repos_root,
                                       const char *wc_repos_root,
                                       svn_depth_t depth,
                                       svn_boolean_t ignore_ancestry,
                                       svn_boolean_t force,
                                       svn_boolean_t record_only,
                                       svn_boolean_t dry_run,
                                       const apr_array_header_t *merge_options,
                                       svn_boolean_t *use_sleep,
                                       svn_client_ctx_t *ctx,
                                       apr_pool_t *pool)
{
  svn_opt_revision_range_t *range;
  apr_array_header_t *remove_sources, *add_sources, *ranges;
  svn_opt_revision_t peg_revision;
  svn_boolean_t same_repos;

  if (strcmp(wc_repos_root, source_repos_root) != 0)
    {
      const char *source_repos_uuid;
      const char *wc_repos_uuid;

      SVN_ERR(svn_ra_get_uuid2(URL1_ra_session, &source_repos_uuid, pool));
      if (entry)
        wc_repos_uuid = entry->uuid;
      else
        SVN_ERR(svn_client_uuid_from_url(&wc_repos_uuid, wc_repos_root,
                                         ctx, pool));
      same_repos = (strcmp(wc_repos_uuid, source_repos_uuid) == 0);
    }
  else
    same_repos = TRUE;

  peg_revision.kind = svn_opt_revision_number;

  range = apr_pcalloc(pool, sizeof(*range));
  range->start.kind = svn_opt_revision_number;
  range->start.value.number = rev1;
  range->end.kind = svn_opt_revision_number;
  range->end.value.number = yc_rev;
  ranges = apr_array_make(pool, 2, sizeof(svn_opt_revision_range_t *));
  APR_ARRAY_PUSH(ranges, svn_opt_revision_range_t *) = range;
  peg_revision.value.number = rev1;
  SVN_ERR(normalize_merge_sources(&remove_sources, URL1, URL1,
                                  source_repos_root, &peg_revision,
                                  ranges, URL1_ra_session, ctx, pool));

  range = apr_pcalloc(pool, sizeof(*range));
  range->start.kind = svn_opt_revision_number;
  range->start.value.number = yc_rev;
  range->end.kind = svn_opt_revision_number;
  range->end.value.number = rev2;
  ranges = apr_array_make(pool, 2, sizeof(svn_opt_revision_range_t *));
  APR_ARRAY_PUSH(ranges, svn_opt_revision_range_t *) = range;
  peg_revision.value.number = rev2;
  SVN_ERR(normalize_merge_sources(&add_sources, URL2, URL2,
                                  source_repos_root, &peg_revision,
                                  ranges, URL2_ra_session, ctx, pool));

  /* If this isn't a record-only merge, we'll first do a stupid
     point-to-point merge... */
  if (! record_only)
    {
      merge_source_t *faux_source;
      apr_array_header_t *faux_sources =
        apr_array_make(pool, 1, sizeof(merge_source_t *));
      faux_source = apr_pcalloc(pool, sizeof(*faux_source));
      faux_source->url1 = URL1;
      faux_source->url2 = URL2;
      faux_source->rev1 = rev1;
      faux_source->rev2 = rev2;
      APR_ARRAY_PUSH(faux_sources, merge_source_t *) = faux_source;
      SVN_ERR(do_merge(NULL, faux_sources, target_wcpath, entry, adm_access,
                       FALSE, TRUE, same_repos,
                       ignore_ancestry, force, dry_run, FALSE, TRUE,
                       depth, merge_options, use_sleep, ctx, pool));
    }
  else if (! same_repos)
    {
      return svn_error_create(SVN_ERR_INCORRECT_PARAMS, NULL,
                              _("Merge from foreign repository is not "
                                "compatible with mergeinfo modification"));
    }

  /* ... and now, if we're doing the mergeinfo thang, we execute a
     pair of record-only merges using the real sources we've
     calculated.  (We know that each tong in our fork of our merge
     source history tree has an ancestral relationship with the common
     ancestral, so we force ancestral=TRUE here.)

     Issue #3648: We don't actually perform these two record-only merges
     on the WC at first, but rather see what each would do and store that
     in two mergeinfo catalogs.  We then merge the catalogs together and
     then record the result in the WC.  This prevents the second record
     only merge from removing legitimate mergeinfo history, from the same
     source, that was made in prior merges. */
  if (same_repos)
    {
      svn_mergeinfo_catalog_t add_result_catalog = apr_hash_make(pool);
      svn_mergeinfo_catalog_t remove_result_catalog = apr_hash_make(pool);

      SVN_ERR(do_merge(add_result_catalog, add_sources, target_wcpath, entry,
                       adm_access, TRUE, TRUE, same_repos,
                       ignore_ancestry, force, dry_run, TRUE, TRUE,
                       depth, merge_options, use_sleep, ctx, pool));
      SVN_ERR(do_merge(remove_result_catalog, remove_sources, target_wcpath,
                       entry, adm_access, TRUE, TRUE, same_repos,
                       ignore_ancestry, force, dry_run, TRUE, TRUE,
                       depth, merge_options, use_sleep, ctx, pool));
      SVN_ERR(svn_mergeinfo__catalog_merge(add_result_catalog,
                                           remove_result_catalog,
                                           pool, pool));

      if (apr_hash_count(add_result_catalog))
        {
          int i;
          apr_array_header_t *sorted_cat;

          sorted_cat = svn_sort__hash(add_result_catalog,
                                      svn_sort_compare_items_as_paths, pool);
          for (i = 0; i < sorted_cat->nelts; i++)
            {
              svn_sort__item_t elt = APR_ARRAY_IDX(sorted_cat, i,
                                                   svn_sort__item_t);
              svn_error_t *err = svn_client__record_wc_mergeinfo(elt.key,
                                                                 elt.value,
                                                                 adm_access,
                                                                 pool);

              if (err && err->apr_err == SVN_ERR_ENTRY_NOT_FOUND)
                {
                  /* PATH isn't just missing, it's not even versioned as far
                     as this working copy knows.  But it was included in
                     MERGES, which means that the server knows about it.
                     Likely we don't have access to the source due to authz
                     restrictions.  For now just clear the error and
                     continue... */
                  svn_error_clear(err);
                }
              else
                SVN_ERR(err);
                }
        }
    }
  return SVN_NO_ERROR;
}

/*-----------------------------------------------------------------------*/

/*** Public APIs ***/

svn_error_t *
svn_client_merge3(const char *source1,
                  const svn_opt_revision_t *revision1,
                  const char *source2,
                  const svn_opt_revision_t *revision2,
                  const char *target_wcpath,
                  svn_depth_t depth,
                  svn_boolean_t ignore_ancestry,
                  svn_boolean_t force,
                  svn_boolean_t record_only,
                  svn_boolean_t dry_run,
                  const apr_array_header_t *merge_options,
                  svn_client_ctx_t *ctx,
                  apr_pool_t *pool)
{
  svn_wc_adm_access_t *adm_access;
  const svn_wc_entry_t *entry;
  const char *URL1, *URL2;
  svn_revnum_t rev1, rev2;
  svn_boolean_t related = FALSE, ancestral = FALSE;
  const char *wc_repos_root, *source_repos_root;
  svn_revnum_t youngest_rev = SVN_INVALID_REVNUM;
  svn_ra_session_t *ra_session1, *ra_session2;
  apr_array_header_t *merge_sources;
  merge_source_t *merge_source;
  svn_opt_revision_t working_rev;
  svn_error_t *err;
  svn_boolean_t use_sleep = FALSE;
  const char *yc_path = NULL;
  svn_revnum_t yc_rev = SVN_INVALID_REVNUM;
  apr_pool_t *sesspool;
  svn_boolean_t same_repos;
  const char *source_repos_uuid1, *source_repos_uuid2;

  /* Sanity check our input -- we require specified revisions. */
  if ((revision1->kind == svn_opt_revision_unspecified)
      || (revision2->kind == svn_opt_revision_unspecified))
    return svn_error_create(SVN_ERR_CLIENT_BAD_REVISION, NULL,
                            _("Not all required revisions are specified"));

  /* ### FIXME: This function really ought to do a history check on
     the left and right sides of the merge source, and -- if one is an
     ancestor of the other -- just call svn_client_merge_peg3() with
     the appropriate args. */

  /* If source1 or source2 are paths, we need to get the underlying
     URL from the wc and save the initial path we were passed so we
     can use it as a path parameter (either in the baton or not).
     otherwise, the path will just be NULL, which means we won't be
     able to figure out some kind of revision specifications, but in
     that case it won't matter, because those ways of specifying a
     revision are meaningless for a url. */
  SVN_ERR(svn_client_url_from_path(&URL1, source1, pool));
  if (! URL1)
    return svn_error_createf(SVN_ERR_ENTRY_MISSING_URL, NULL,
                             _("'%s' has no URL"),
                             svn_path_local_style(source1, pool));

  SVN_ERR(svn_client_url_from_path(&URL2, source2, pool));
  if (! URL2)
    return svn_error_createf(SVN_ERR_ENTRY_MISSING_URL, NULL,
                             _("'%s' has no URL"),
                             svn_path_local_style(source2, pool));

  /* Open an admistrative session with the working copy. */
  SVN_ERR(svn_wc_adm_probe_open3(&adm_access, NULL, target_wcpath,
                                 ! dry_run, -1, ctx->cancel_func,
                                 ctx->cancel_baton, pool));

  /* Fetch the target's entry. */
  SVN_ERR(svn_wc__entry_versioned(&entry, target_wcpath, adm_access,
                                  FALSE, pool));

  /* Determine the working copy target's repository root URL. */
  working_rev.kind = svn_opt_revision_working;
  SVN_ERR(svn_client__get_repos_root(&wc_repos_root, target_wcpath,
                                     &working_rev, adm_access, ctx, pool));

  /* Open some RA sessions to our merge source sides. */
  sesspool = svn_pool_create(pool);
  SVN_ERR(svn_client__open_ra_session_internal(&ra_session1,
                                               URL1, NULL, NULL, NULL,
                                               FALSE, TRUE, ctx, sesspool));
  SVN_ERR(svn_client__open_ra_session_internal(&ra_session2,
                                               URL2, NULL, NULL, NULL,
                                               FALSE, TRUE, ctx, sesspool));

  /* Resolve revisions to real numbers. */
  SVN_ERR(svn_client__get_revision_number(&rev1, &youngest_rev, ra_session1,
                                          revision1, NULL, sesspool));
  SVN_ERR(svn_client__get_revision_number(&rev2, &youngest_rev, ra_session2,
                                          revision2, NULL, sesspool));

  SVN_ERR(svn_ra_get_uuid2(ra_session1, &source_repos_uuid1, pool));
  SVN_ERR(svn_ra_get_uuid2(ra_session2, &source_repos_uuid2, pool));

  /* We can't do a diff between different repositories. */
  if (strcmp(source_repos_uuid1, source_repos_uuid2) != 0)
    return svn_error_createf(SVN_ERR_RA_UUID_MISMATCH, NULL,
                             _("'%s' isn't in the same repository as '%s'"),
                             URL1, URL2);

  /* Get the repository root URL from one of our sessions. */
  SVN_ERR(svn_ra_get_repos_root2(ra_session1, &source_repos_root, sesspool));

  /* Do our working copy and sources come from the same repository? */
  if (strcmp(wc_repos_root, source_repos_root) != 0)
    {
      const char *wc_repos_uuid;

      if (entry)
        wc_repos_uuid = entry->uuid;
      else
        SVN_ERR(svn_client_uuid_from_url(&wc_repos_uuid, wc_repos_root,
                                         ctx, pool));
      same_repos = (strcmp(wc_repos_uuid, source_repos_uuid1) == 0);
    }
  else
    same_repos = TRUE;

  /* Unless we're ignoring ancestry, see if the two sources are related.  */
  if (! ignore_ancestry)
    SVN_ERR(svn_client__get_youngest_common_ancestor(&yc_path, &yc_rev,
                                                     URL1, rev1,
                                                     URL2, rev2,
                                                     ctx, pool));

  /* Check for a youngest common ancestor.  If we have one, we'll be
     doing merge tracking.

     So, given a requested merge of the differences between A and
     B, and a common ancestor of C, we will find ourselves in one of
     four positions, and four different approaches:

        A == B == C   there's nothing to merge

        A == C != B   we merge the changes between A (or C) and B

        B == C != A   we merge the changes between B (or C) and A

        A != B != C   we merge the changes between A and B without
                      merge recording, then record-only two merges:
                      from A to C, and from C to B
  */
  if (yc_path && SVN_IS_VALID_REVNUM(yc_rev))
    {
      apr_array_header_t *ranges;
      svn_opt_revision_range_t *range;
      svn_opt_revision_t peg_revision;
      peg_revision.kind = svn_opt_revision_number;

      /* Note that our merge sources are related. */
      related = TRUE;

      /* Make YC_PATH into a full URL. */
      yc_path = svn_path_url_add_component2(source_repos_root, yc_path, pool);

      /* If the common ancestor matches the right side of our merge,
         then we only need to reverse-merge the left side. */
      if ((strcmp(yc_path, URL2) == 0) && (yc_rev == rev2))
        {
          ancestral = TRUE;
          range = apr_pcalloc(pool, sizeof(*range));
          range->start.kind = svn_opt_revision_number;
          range->start.value.number = rev1;
          range->end.kind = svn_opt_revision_number;
          range->end.value.number = yc_rev;
          ranges = apr_array_make(pool, 2, sizeof(svn_opt_revision_range_t *));
          APR_ARRAY_PUSH(ranges, svn_opt_revision_range_t *) = range;
          peg_revision.value.number = rev1;
          SVN_ERR(normalize_merge_sources(&merge_sources, URL1, URL1,
                                          source_repos_root, &peg_revision,
                                          ranges, ra_session1, ctx, pool));
        }
      /* If the common ancestor matches the left side of our merge,
         then we only need to merge the right side. */
      else if ((strcmp(yc_path, URL1) == 0) && (yc_rev == rev1))
        {
          ancestral = TRUE;
          range = apr_pcalloc(pool, sizeof(*range));
          range->start.kind = svn_opt_revision_number;
          range->start.value.number = yc_rev;
          range->end.kind = svn_opt_revision_number;
          range->end.value.number = rev2;
          ranges = apr_array_make(pool, 2, sizeof(svn_opt_revision_range_t *));
          APR_ARRAY_PUSH(ranges, svn_opt_revision_range_t *) = range;
          peg_revision.value.number = rev2;
          SVN_ERR(normalize_merge_sources(&merge_sources, URL2, URL2,
                                          source_repos_root, &peg_revision,
                                          ranges, ra_session2, ctx, pool));
        }
      /* And otherwise, we need to do both: reverse merge the left
         side, and merge the right. */
      else
        {
          err = merge_cousins_and_supplement_mergeinfo(target_wcpath, entry,
                                                       adm_access,
                                                       ra_session1,
                                                       ra_session2,
                                                       URL1, rev1,
                                                       URL2, rev2,
                                                       yc_rev,
                                                       source_repos_root,
                                                       wc_repos_root,
                                                       depth,
                                                       ignore_ancestry, force,
                                                       record_only, dry_run,
                                                       merge_options,
                                                       &use_sleep, ctx,
                                                       pool);
          if (err)
            {
              if (use_sleep)
                svn_io_sleep_for_timestamps(target_wcpath, pool);

              return err;
            }

          /* Close our temporary RA sessions (this could've happened
             after the second call to normalize_merge_sources() inside
             the merge_cousins_and_supplement_mergeinfo() routine). */
          svn_pool_destroy(sesspool);

          return svn_wc_adm_close2(adm_access, pool);
        }
    }
  else
    {
      /* Build a single-item merge_source_t array. */
      merge_sources = apr_array_make(pool, 1, sizeof(merge_source_t *));
      merge_source = apr_pcalloc(pool, sizeof(*merge_source));
      merge_source->url1 = URL1;
      merge_source->url2 = URL2;
      merge_source->rev1 = rev1;
      merge_source->rev2 = rev2;
      APR_ARRAY_PUSH(merge_sources, merge_source_t *) = merge_source;
    }

  /* Close our temporary RA sessions. */
  svn_pool_destroy(sesspool);

  err = do_merge(NULL, merge_sources, target_wcpath, entry, adm_access,
                 ancestral, related, same_repos,
                 ignore_ancestry, force, dry_run,
                 record_only, FALSE, depth, merge_options,
                 &use_sleep, ctx, pool);

  if (use_sleep)
    svn_io_sleep_for_timestamps(target_wcpath, pool);

  if (err)
    return err;

  return svn_wc_adm_close2(adm_access, pool);
}


/* If TARGET_WCPATH does not reflect a single-revision,
   svn_depth_infinity, pristine, unswitched working copy -- in other
   words, a subtree found in a single revision -- raise
   SVN_ERR_CLIENT_NOT_READY_TO_MERGE. */
static svn_error_t *
ensure_wc_reflects_repository_subtree(const char *target_wcpath,
                                      svn_client_ctx_t *ctx,
                                      apr_pool_t *pool)
{
  svn_wc_revision_status_t *wc_stat;

  /* Get a WC summary with min/max revisions set to the BASE revision. */
  SVN_ERR(svn_wc_revision_status(&wc_stat, target_wcpath, NULL, FALSE,
                                 ctx->cancel_func, ctx->cancel_baton, pool));

  if (wc_stat->switched)
    return svn_error_create(SVN_ERR_CLIENT_NOT_READY_TO_MERGE, NULL,
                            _("Cannot reintegrate into a working copy "
                              "with a switched subtree"));

  if (wc_stat->sparse_checkout)
    return svn_error_create(SVN_ERR_CLIENT_NOT_READY_TO_MERGE, NULL,
                            _("Cannot reintegrate into a working copy "
                              "not entirely at infinite depth"));

  if (wc_stat->modified)
    return svn_error_create(SVN_ERR_CLIENT_NOT_READY_TO_MERGE, NULL,
                            _("Cannot reintegrate into a working copy "
                              "that has local modifications"));

  if (! (SVN_IS_VALID_REVNUM(wc_stat->min_rev)
         && SVN_IS_VALID_REVNUM(wc_stat->max_rev)))
    return svn_error_create(SVN_ERR_CLIENT_NOT_READY_TO_MERGE, NULL,
                            _("Cannot determine revision of working copy"));

  if (wc_stat->min_rev != wc_stat->max_rev)
    return svn_error_create(SVN_ERR_CLIENT_NOT_READY_TO_MERGE, NULL,
                            _("Cannot reintegrate into mixed-revision "
                              "working copy; try updating first"));

  return SVN_NO_ERROR;
}

/* Given a mergeinfo catalog UNMERGED_HISTORY representing the history
   (as mergeinfo) from a merge target that is not represented in the merge
   source, check (using TARGET_RA_SESSION, which points to
   MERGE_TARGET_REPOS_REL_PATH) that all of the ranges in the catalog's
   mergeinfos are "phantoms": that is, their corresponding path did not
   change in any of their revisions.  Raises SVN_ERR_CLIENT_NOT_READY_TO_MERGE
   if any are not phantoms.  Temporary allocations in POOL.
 */
static svn_error_t *
ensure_all_missing_ranges_are_phantoms(const char *target_repos_rel_path,
                                       svn_ra_session_t *target_ra_session,
                                       svn_mergeinfo_catalog_t unmerged_history,
                                       apr_pool_t *pool)
{
  apr_hash_index_t *hi1, *hi2;
  apr_pool_t *iterpool = svn_pool_create(pool);
  apr_size_t target_repos_rel_len = strlen(target_repos_rel_path);

  for (hi1 = apr_hash_first(pool, unmerged_history); hi1;
       hi1 = apr_hash_next(hi1))
    {
      const void *key;
      void *value;
      const char *catalog_path;
      svn_mergeinfo_t history_as_mergeinfo;

      apr_hash_this(hi1, &key, NULL, &value);
      catalog_path = key;
      history_as_mergeinfo = value;

      for (hi2 = apr_hash_first(pool, history_as_mergeinfo); hi2;
           hi2 = apr_hash_next(hi2))
        {
          const char *path;
          apr_array_header_t *rangelist;
          int i;

          apr_hash_this(hi2, &key, NULL, &value);
          path = key;
          rangelist = value;

          /* Mergeinfo source paths are always repository absolute, so adjust
             the path so it is relative to TARGET_RA_SESSION. */
          path = path + target_repos_rel_len + 1;
          if (path[0] == '/')
            path++;

          for (i = 0; i < rangelist->nelts; i++)
            {
              svn_merge_range_t *range = APR_ARRAY_IDX(rangelist, i,
                                                       svn_merge_range_t *);
              svn_dirent_t *dirent;

              /* This function should not receive any "rollback"
                 ranges. */
              SVN_ERR_ASSERT(range->start < range->end);

              svn_pool_clear(iterpool);

              SVN_ERR(svn_ra_stat(target_ra_session,
                                  path,
                                  range->end,
                                  &dirent,
                                  iterpool));

              if (svn_merge_range_contains_rev(range, dirent->created_rev))
                {
                  const char *full_url;

                  svn_pool_destroy(iterpool);

                  SVN_ERR(svn_ra_get_session_url(target_ra_session, &full_url,
                                                 pool));
                  full_url = svn_path_url_add_component2(full_url, path, pool);
                  return svn_error_createf(SVN_ERR_CLIENT_NOT_READY_TO_MERGE,
                                           NULL,
                                           _("At least one revision (r%ld) "
                                             "not yet merged from '%s'"),
                                           dirent->created_rev, full_url);
                }
            }
        }
    }

  svn_pool_destroy(iterpool);
  return SVN_NO_ERROR;
}


/* Helper for calculate_left_hand_side() which produces a mergeinfo catalog
   describing what parts of of the reintegrate target have not previously been
   merged to the reintegrate source.

   SOURCE_CATALOG is the collection of explicit mergeinfo on
   SOURCE_REPOS_REL_PATH@SOURCE_REV and all its children, i.e. the mergeinfo
   catalog for the reintegrate source.

   TARGET_SEGMENTS_HASH is a hash of (const char *) paths mapped to
   (apr_array_header_t *) arrays of svn_location_segment_t *.  Each of these
   path keys represent a path in the reintegrate target, relative to the
   repository root, which has explicit mergeinfo and/or is the reintegrate
   target itself.  The segment arrays contain the natural history of each
   path@TARGET_REV.  Effectively this is the mergeinfo catalog on the
   reintegrate target.

   YC_ANCESTOR_REV is the revision of the youngest common ancestor of the
   reintegrate source and the reintegrate target.

   SOURCE_REPOS_REL_PATH is the path of the reintegrate source relative to
   the root of the repository.  TARGET_REPOS_REL_PATH is the path of the
   reintegrate target relative to the root of the repository.

   TARGET_REV is the working revision the entire WC tree rooted at
   TARGET_REPOS_REL_PATH is at.  SOURCE_REV is the peg revision of the
   reintegrate source.

   SOURCE_RA_SESSION is a session opened to the SOURCE_REPOS_REL_PATH
   and TARGET_RA_SESSION is open to TARGET_REPOS_REL_PATH.

   For each path/segment in TARGET_SEGMENTS_HASH check that the history that
   segment represents is contained in either the explicit mergeinfo for the
   corresponding path in SOURCE_CATALOG, the corresponding path's inherited
   mergeinfo (if no explicit mergeinfo for the path is found in
   SOURCE_CATALOG), or the corresponding path's natural history.  Populate
   *UNMERGED_TO_SOURCE_CATALOG with the corresponding source paths mapped to
   the mergeinfo from the target's natural history which is *not* found.  Also
   include any mergeinfo from SOURCE_CATALOG which explicitly describes the
   target's history but for which *no* path/segment was found in
   TARGET_SEGMENTS_HASH.

   If no part of TARGET_SEGMENTS_HASH is found in SOURCE_CATALOG set
   *NEVER_SYNCHED to TRUE and set *YOUNGEST_MERGED_REV to SVN_INVALID_REVNUM.
   Otherwise set *NEVER_SYNCHED to FALSE, *YOUNGEST_MERGED_REV to the youngest
   revision previously merged from the target to the source, and filter
   *UNMERGED_TO_SOURCE_CATALOG so that it contains no ranges greater than
   *YOUNGEST_MERGED_REV.

   *UNMERGED_TO_SOURCE_CATALOG is (deeply) allocated in POOL. */
static svn_error_t *
find_unmerged_mergeinfo(svn_mergeinfo_catalog_t *unmerged_to_source_catalog,
                        svn_boolean_t *never_synched,
                        svn_revnum_t *youngest_merged_rev,
                        svn_revnum_t yc_ancestor_rev,
                        svn_mergeinfo_catalog_t source_catalog,
                        apr_hash_t *target_segments_hash,
                        const char *source_repos_rel_path,
                        const char *target_repos_rel_path,
                        svn_revnum_t target_rev,
                        svn_revnum_t source_rev,
                        svn_ra_session_t *source_ra_session,
                        svn_ra_session_t *target_ra_session,
                        svn_client_ctx_t *ctx,
                        apr_pool_t *pool)
{
  apr_hash_index_t *hi;
  svn_mergeinfo_catalog_t new_catalog = apr_hash_make(pool);
  svn_mergeinfo_t target_history_as_mergeinfo, source_history_as_mergeinfo;
  svn_mergeinfo_t common_mergeinfo;
  apr_pool_t *subpool = svn_pool_create(pool);
  apr_pool_t *iterpool = svn_pool_create(subpool);
  svn_revnum_t old_rev, young_rev;
  apr_size_t source_repos_rel_len = strlen(source_repos_rel_path);

  *never_synched = TRUE;
  *youngest_merged_rev = SVN_INVALID_REVNUM;

  /* Examine the natural history of each path in the reintegrate target
     with explicit mergeinfo. */
  for (hi = apr_hash_first(subpool, target_segments_hash);
       hi;
       hi = apr_hash_next(hi))
    {
      const void *key;
      void *val;
      const char *path, *source_path;
      apr_array_header_t *segments;
      const char *source_path_rel_to_session;
      svn_mergeinfo_t source_mergeinfo, filtered_mergeinfo;

      svn_pool_clear(iterpool);
      apr_hash_this(hi, &key, NULL, &val);
      path = key;
      segments = val;

      source_path = path + strlen(target_repos_rel_path);
      if (source_path[0] == '/') /* Remove leading '/' for svn_path_join. */
        source_path++;
      source_path = svn_path_join(source_repos_rel_path, source_path,
        iterpool);
      source_path_rel_to_session = source_path + source_repos_rel_len;
      if (source_path_rel_to_session[0] == '/')
        source_path_rel_to_session++;

      /* Convert this target path's natural history into mergeinfo. */
      SVN_ERR(svn_client__mergeinfo_from_segments(&target_history_as_mergeinfo,
                                                  segments,
                                                  iterpool));

      /* Remove any target history that is also part of the source's history,
         i.e. their common ancestry.  By definition this has already been
         "merged" from the target to the source.  If the source has explict
         self referential mergeinfo it would intersect with the target's
         history below, making it appear that some merges had been done from
         the target to the source, when this might not actually be the case. */
      SVN_ERR(svn_mergeinfo__filter_mergeinfo_by_ranges(
        &target_history_as_mergeinfo, target_history_as_mergeinfo,
        source_rev, yc_ancestor_rev, iterpool));

      /* Look for any explicit mergeinfo on the source path corresponding to
         the target path.  If we find any remove that from SOURCE_CATALOG.
         When this iteration over TARGET_SEGMENTS_HASH is complete all that
         should be left in SOURCE_CATALOG are subtrees that have explicit
         mergeinfo on the reintegrate source where there is no corresponding
         explicit mergeinfo on the reintegrate target. */
      source_mergeinfo = apr_hash_get(source_catalog, source_path,
                                      APR_HASH_KEY_STRING);
      if (source_mergeinfo)
        {
          svn_mergeinfo_t explicit_source_target_history_intersection;

          apr_hash_set(source_catalog, source_path, APR_HASH_KEY_STRING,
                       NULL);
          /* If there is an intersection between the *explicit* mergeinfo on
             this source path and the corresponding target's history then we
             know that at least one merge was done from the target to the
             source. */
          SVN_ERR(svn_mergeinfo_intersect(
            &explicit_source_target_history_intersection,
            source_mergeinfo, target_history_as_mergeinfo,
            iterpool));
          if (apr_hash_count(explicit_source_target_history_intersection))
            {
              *never_synched = FALSE;
              /* Keep track of the youngest revision merged from the
                 target to the source. */
              SVN_ERR(svn_mergeinfo__get_range_endpoints(
                &young_rev, &old_rev,
                explicit_source_target_history_intersection,
                iterpool));
              if (!SVN_IS_VALID_REVNUM(*youngest_merged_rev)
                  || (young_rev > *youngest_merged_rev))
                *youngest_merged_rev = young_rev;
            }
        }
      else
        {
          /* There is no mergeinfo on source_path *or* source_path doesn't
             exist at all.  If simply doesn't exist we can ignore it
             altogether. */
          svn_node_kind_t kind;
          svn_mergeinfo_catalog_t subtree_catalog;
          apr_array_header_t *source_repos_rel_path_as_array;
          SVN_ERR(svn_ra_check_path(source_ra_session,
                                    source_path_rel_to_session,
                                    source_rev, &kind, iterpool));
          if (kind == svn_node_none)
              continue;
          /* Else source_path does exist though it has no explicit mergeinfo.
             Find its inherited mergeinfo.  If it doesn't have any then simply
             set source_mergeinfo to an empty hash. */
          source_repos_rel_path_as_array =
            apr_array_make(iterpool, 1, sizeof(const char *));
          APR_ARRAY_PUSH(source_repos_rel_path_as_array, const char *)
            = source_path_rel_to_session;
          SVN_ERR(svn_ra_get_mergeinfo(source_ra_session, &subtree_catalog,
                                       source_repos_rel_path_as_array,
                                       source_rev, svn_mergeinfo_inherited,
                                       FALSE, iterpool));
          if (subtree_catalog)
            source_mergeinfo = apr_hash_get(subtree_catalog, source_path,
                                            APR_HASH_KEY_STRING);

          /* A path might not have any inherited mergeinfo either. */
          if (!source_mergeinfo)
            source_mergeinfo = apr_hash_make(iterpool);
        }

      /* Get the source path's natural history and convert it to mergeinfo.
         Then merge that natural history into source path's explicit
         or inherited mergeinfo. */
      SVN_ERR(svn_client__repos_location_segments(&segments,
                                                  source_ra_session,
                                                  source_path_rel_to_session,
                                                  source_rev, source_rev,
                                                  SVN_INVALID_REVNUM,
                                                  ctx, iterpool));
      SVN_ERR(svn_client__mergeinfo_from_segments(&source_history_as_mergeinfo,
                                                  segments, iterpool));
      SVN_ERR(svn_mergeinfo_merge(source_mergeinfo,
                                  source_history_as_mergeinfo, iterpool));

      /* Now source_mergeinfo represents everything we know about
         source_path's history.  Now we need to know what part, if any, of the
         corresponding target's history is *not* part of source_path's total
         history; because it is neither shared history nor was it ever merged
         from the target to the source. */
      SVN_ERR(svn_mergeinfo_intersect(&common_mergeinfo,
                                      source_mergeinfo,
                                      target_history_as_mergeinfo,
                                      iterpool));

      /* Use subpool rather than iterpool because filtered_mergeinfo is
         going into new_catalog below and needs to last to the end of
         this function. */
      SVN_ERR(svn_mergeinfo_remove(&filtered_mergeinfo,
                                   common_mergeinfo,
                                   target_history_as_mergeinfo,
                                   subpool));

      /* As with svn_mergeinfo_intersect above, we need to use subpool
         rather than iterpool. */
      apr_hash_set(new_catalog,
                   apr_pstrdup(subpool, source_path),
                   APR_HASH_KEY_STRING,
                   filtered_mergeinfo);
    }

  /* Are there any subtrees with explicit mergeinfo still left in the merge
     source where there was no explicit mergeinfo for the corresponding path
     in the merge target?  If so, add the intersection of those path's
     mergeinfo and the corresponding target path's mergeinfo to
     new_catalog. */
  if (apr_hash_count(source_catalog))
    {
      for (hi = apr_hash_first(subpool, source_catalog);
           hi;
           hi = apr_hash_next(hi))
        {
          const void *key;
          void *val;
          const char *source_path;
          const char *source_path_rel_to_session;
          svn_mergeinfo_t source_mergeinfo, filtered_mergeinfo;
          const char *target_path;
          apr_array_header_t *segments;
          svn_error_t *err;

          svn_pool_clear(iterpool);
          apr_hash_this(hi, &key, NULL, &val);
          source_path = key;
          source_mergeinfo = val;

          target_path = source_path + source_repos_rel_len;
          if (target_path[0] == '/') /* Remove leading '/' for svn_path_join */
            target_path++;

          source_path_rel_to_session = source_path + source_repos_rel_len;
          if (source_path_rel_to_session[0] == '/')
            source_path_rel_to_session++;

          err = svn_client__repos_location_segments(&segments,
                                                    target_ra_session,
                                                    target_path,
                                                    target_rev, target_rev,
                                                    SVN_INVALID_REVNUM,
                                                    ctx, iterpool);
          if (err)
            {
              if (err->apr_err == SVN_ERR_FS_NOT_FOUND
                  || err->apr_err == SVN_ERR_RA_DAV_REQUEST_FAILED)
                {
                  /* This path with explicit mergeinfo in the source doesn't
                     exist on the target. */
                  svn_error_clear(err);
                  err = NULL;
                }
              else
                {
                  return err;
                }
            }
          else
            {
              svn_mergeinfo_t explicit_source_target_history_intersection;

              SVN_ERR(svn_client__mergeinfo_from_segments(
                &target_history_as_mergeinfo, segments, iterpool));

              /* If there is an intersection between the *explicit* mergeinfo
                 on this source path and the corresponding target's history
                 then we know that at least one merge was done from the target
                 to the source. */
              SVN_ERR(svn_mergeinfo_intersect(
                &explicit_source_target_history_intersection,
                source_mergeinfo, target_history_as_mergeinfo,
                                                          iterpool));
              if (apr_hash_count(explicit_source_target_history_intersection))
                {
                  *never_synched = FALSE;
                  /* Keep track of the youngest revision merged from the
                     target to the source. */
                  SVN_ERR(svn_mergeinfo__get_range_endpoints(
                    &young_rev, &old_rev,
                    explicit_source_target_history_intersection, iterpool));
                  if (!SVN_IS_VALID_REVNUM(*youngest_merged_rev)
                      || (young_rev > *youngest_merged_rev))
                    *youngest_merged_rev = young_rev;
                }

              /* Get the source path's natural history and convert it to
                 mergeinfo.  Then merge that natural history into source
                 path's explicit or inherited mergeinfo. */
              SVN_ERR(svn_client__repos_location_segments(
                &segments,
                source_ra_session,
                source_path_rel_to_session,
                target_rev,
                target_rev,
                SVN_INVALID_REVNUM,
                ctx, iterpool));
              SVN_ERR(svn_client__mergeinfo_from_segments(
                &source_history_as_mergeinfo, segments, iterpool));
              SVN_ERR(svn_mergeinfo_merge(source_mergeinfo,
                                          source_history_as_mergeinfo,
                                          iterpool));
              SVN_ERR(svn_mergeinfo_intersect(&common_mergeinfo,
                                              source_mergeinfo,
                                              target_history_as_mergeinfo,
                                              iterpool));
              /* Use subpool rather than iterpool because filtered_mergeinfo
                 is  going into new_catalog below and needs to last to the
                 end of this function. */
              SVN_ERR(svn_mergeinfo_remove(&filtered_mergeinfo,
                                           common_mergeinfo,
                                           target_history_as_mergeinfo,
                                           subpool));
              if (apr_hash_count(filtered_mergeinfo))
                apr_hash_set(new_catalog,
                             apr_pstrdup(subpool, source_path),
                             APR_HASH_KEY_STRING,
                             filtered_mergeinfo);
            }
        }
    }

  /* Limit new_catalog to the youngest revisions previously merged from
     the target to the source. */
  if (SVN_IS_VALID_REVNUM(*youngest_merged_rev))
    SVN_ERR(svn_mergeinfo__filter_catalog_by_ranges(&new_catalog,
                                                    new_catalog,
                                                    *youngest_merged_rev,
                                                    0, /* No oldest bound. */
                                                    subpool));

  /* Make a shiny new copy before blowing away all the temporary pools. */
  *unmerged_to_source_catalog = svn_mergeinfo_catalog_dup(new_catalog, pool);

  /* iterpool was allocated out of subpool so this destroys both. */
  svn_pool_destroy(subpool);
  return SVN_NO_ERROR;
}

/* Helper for svn_client_merge_reintegrate() which calculates the
   'left hand side' of the underlying two-URL merge that a --reintegrate
   merge actually performs.

   TARGET_REPOS_REL_PATH is the path of the reintegrate target relative to
   the root of the repository.  SOURCE_REPOS_REL_PATH is the path of the
   reintegrate source relative to the root of the repository.

   SUBTREES_WITH_MERGEINFO is a hash of (const char *) paths in the
   reintegrate target, relative to the root of the repository, which have
   explicit mergeinfo set on them.  The reintegrate target itself should
   always be in this hash, regardless of whether it has explicit mergeinfo.

   TARGET_REV is the working revision the entire WC tree rooted at
   TARGET_REPOS_REL_PATH is at.  SOURCE_REV is the peg revision of the
   reintegrate source.

   Populate *UNMERGED_TO_SOURCE_CATALOG with the mergeinfo describing what
   parts of TARGET_REPOS_REL_PATH@TARGET_REV have not been merged to
   SOURCE_REPOS_REL_PATH@SOURCE_REV, up to the youngest revision ever merged
   from the target to the source if such exists, see doc string for
   find_unmerged_mergeinfo().

   SOURCE_RA_SESSION is a session opened to the SOURCE_REPOS_REL_PATH
   and TARGET_RA_SESSION is open to TARGET_REPOS_REL_PATH. */
static svn_error_t *
calculate_left_hand_side(const char **url_left,
                         svn_revnum_t *rev_left,
                         svn_mergeinfo_t *unmerged_to_source_catalog,
                         const char *target_repos_rel_path,
                         apr_hash_t *subtrees_with_mergeinfo,
                         svn_revnum_t target_rev,
                         const char *source_repos_rel_path,
                         const char *source_repos_root,
                         svn_revnum_t source_rev,
                         svn_ra_session_t *source_ra_session,
                         svn_ra_session_t *target_ra_session,
                         svn_client_ctx_t *ctx,
                         apr_pool_t *pool)
{
  apr_array_header_t *segments; /* array of (svn_location_segment_t *) */
  svn_mergeinfo_catalog_t mergeinfo_catalog, unmerged_catalog;
  apr_array_header_t *source_repos_rel_path_as_array
    = apr_array_make(pool, 1, sizeof(const char *));
  apr_pool_t *subpool = svn_pool_create(pool);
  apr_hash_index_t *hi;
  /* hash of paths mapped to arrays of svn_location_segment_t *. */
  apr_hash_t *segments_hash = apr_hash_make(pool);
  svn_boolean_t never_synced;
  svn_revnum_t youngest_merged_rev;
  const char *yc_ancestor_path;
  const char *source_url;
  const char *target_url;
  apr_size_t target_repos_rel_len = strlen(target_repos_rel_path);

  /* Get the history (segments) for the target and any of its subtrees
     with explicit mergeinfo. */
  for (hi = apr_hash_first(pool, subtrees_with_mergeinfo);
       hi;
       hi = apr_hash_next(hi))
    {
      const void *key;
      void *val;
      const char *path;
      const char *path_rel_to_session;

      apr_hash_this(hi, &key, NULL, &val);
      path = key;
      path_rel_to_session = path + target_repos_rel_len;
      if (path_rel_to_session[0] == '/')
        path_rel_to_session++;

      SVN_ERR(svn_client__repos_location_segments(&segments,
                                                  target_ra_session,
                                                  path_rel_to_session,
                                                  target_rev, target_rev,
                                                  SVN_INVALID_REVNUM,
                                                  ctx, subpool));
      apr_hash_set(segments_hash,
                   apr_pstrdup(subpool, path),
                   APR_HASH_KEY_STRING, segments);
    }

  /* Check that SOURCE_URL@SOURCE_REV and TARGET_URL@TARGET_REV are
     actually related, we can't reintegrate if they are not.  Also
     get an initial value for *REV_LEFT. */
  source_url = svn_path_url_add_component2(source_repos_root,
                                           source_repos_rel_path,
                                           subpool),
  target_url = svn_path_url_add_component2(source_repos_root,
                                           target_repos_rel_path,
                                           subpool);
  SVN_ERR(svn_client__get_youngest_common_ancestor(&yc_ancestor_path,
                                                   rev_left,
                                                   source_url, source_rev,
                                                   target_url, target_rev,
                                                   ctx, subpool));
  if (!(yc_ancestor_path && SVN_IS_VALID_REVNUM(*rev_left)))
    return svn_error_createf(SVN_ERR_CLIENT_NOT_READY_TO_MERGE, NULL,
                             _("'%s@%ld' must be ancestrally related to "
                               "'%s@%ld'"), source_url, source_rev,
                             target_url, target_rev);

  /* Get the mergeinfo from the source, including its descendants
     with differing explicit mergeinfo. */
  APR_ARRAY_PUSH(source_repos_rel_path_as_array, const char *) = "";
  SVN_ERR(svn_ra_get_mergeinfo(source_ra_session, &mergeinfo_catalog,
                               source_repos_rel_path_as_array, source_rev,
                               svn_mergeinfo_inherited, TRUE, subpool));

  if (mergeinfo_catalog)
    SVN_ERR(svn_mergeinfo__add_prefix_to_catalog(&mergeinfo_catalog,
                                                 mergeinfo_catalog,
                                                 source_repos_rel_path,
                                                 subpool, subpool));

  if (!mergeinfo_catalog)
    mergeinfo_catalog = apr_hash_make(subpool);

  /* Filter the source's mergeinfo catalog so that we are left with
     mergeinfo that describes what has *not* previously been merged from
     TARGET_REPOS_REL_PATH@TARGET_REV to SOURCE_REPOS_REL_PATH@SOURCE_REV. */
  SVN_ERR(find_unmerged_mergeinfo(&unmerged_catalog,
                                  &never_synced,
                                  &youngest_merged_rev,
                                  *rev_left,
                                  mergeinfo_catalog,
                                  segments_hash,
                                  source_repos_rel_path,
                                  target_repos_rel_path,
                                  target_rev,
                                  source_rev,
                                  source_ra_session,
                                  target_ra_session,
                                  ctx,
                                  subpool));

  /* Simplify unmerged_catalog through elision then make a copy in POOL. */
  SVN_ERR(svn_client__elide_mergeinfo_catalog(unmerged_catalog,
                                              subpool));
  *unmerged_to_source_catalog = svn_mergeinfo_catalog_dup(unmerged_catalog,
                                                          pool);

  if (never_synced)
    {
      /* We never merged to the source.  Just return the branch point. */
      *url_left = svn_path_url_add_component2(source_repos_root,
                                              yc_ancestor_path, pool);
    }
  else
    {
      /* We've previously merged some or all of the target, up to
         youngest_merged_rev, from the target to the source.  Set *URL_LEFT
         and *REV_LEFT to cover the youngest part of this range. */
      svn_opt_revision_t peg_revision;
      const char *youngest_url;

      peg_revision.kind = svn_opt_revision_number;
      peg_revision.value.number = youngest_merged_rev;

      *rev_left = youngest_merged_rev;
      SVN_ERR(svn_client__derive_location(
        &youngest_url, NULL,
        svn_path_url_add_component2(source_repos_root,
                                    target_repos_rel_path,
                                    subpool),
        &peg_revision, target_ra_session, NULL, ctx, subpool));
      *url_left = apr_pstrdup(pool, youngest_url);
    }

  svn_pool_destroy(subpool);
  return SVN_NO_ERROR;
}

/* A baton for get_subtree_mergeinfo_walk_cb. */
struct get_subtree_mergeinfo_walk_baton
{
  /* Merge target and target relative to repository root. */
  const char *target_path;
  const char *target_repos_root;

  /* Hash of paths (const char *) that have explicit mergeinfo. */
  apr_hash_t *subtrees_with_mergeinfo;

  /* Access for the tree being walked. */
  svn_wc_adm_access_t *base_access;
  svn_client_ctx_t *ctx;
};

/* svn_wc_entry_callbacks2_t found_entry() callback for get_mergeinfo_paths.

   Given the working copy path PATH, its corresponding ENTRY, and WALK_BATON,
   where WALK_BATON is of type get_subtree_mergeinfo_walk_baton *:

   If PATH has explicit mergeinfo or is the same as WALK_BATON->TARGET_PATH,
   then store a copy of PATH in WALK_BATON->SUBTREES_WITH_MERGEINFO.  The copy
   is allocated in WALK_BATON->SUBTREES_WITH_MERGEINFO's pool.

   POOL is used only for temporary allocations. */
static svn_error_t *
get_subtree_mergeinfo_walk_cb(const char *path,
                              const svn_wc_entry_t *entry,
                              void *walk_baton,
                              apr_pool_t *pool)
{
  struct get_subtree_mergeinfo_walk_baton *wb = walk_baton;
  const svn_string_t *propval;

  /* We're going to receive dirents twice;  we want to ignore the
     first one (where it's a child of a parent dir), and only use
     the second one (where we're looking at THIS_DIR).  The exception
     is absent dirs, these only come through once, so continue. */
  if ((entry->kind == svn_node_dir)
      && (strcmp(entry->name, SVN_WC_ENTRY_THIS_DIR) != 0)
      && !entry->absent)
    return SVN_NO_ERROR;

  SVN_ERR(svn_wc_prop_get(&propval, SVN_PROP_MERGEINFO, path,
                          wb->base_access, pool));

  /* We always want to include the reintegrate target even if it has
     no explicit mergeinfo.  It still has natural history we'll need
     to consider. */
  if (propval || strcmp(path, wb->target_path) == 0)
    {
      const char *stored_path;

      SVN_ERR(svn_client__path_relative_to_root(&stored_path, path,
                                                wb->target_repos_root, FALSE,
                                                NULL, wb->base_access, pool));
      stored_path = apr_pstrdup(apr_hash_pool_get(wb->subtrees_with_mergeinfo),
                                stored_path);
      apr_hash_set(wb->subtrees_with_mergeinfo, stored_path,
                   APR_HASH_KEY_STRING, stored_path);
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_merge_reintegrate(const char *source,
                             const svn_opt_revision_t *peg_revision,
                             const char *target_wcpath,
                             svn_boolean_t dry_run,
                             const apr_array_header_t *merge_options,
                             svn_client_ctx_t *ctx,
                             apr_pool_t *pool)
{
  svn_wc_adm_access_t *adm_access;
  const svn_wc_entry_t *entry;
  const char *wc_repos_root, *source_repos_root;
  svn_opt_revision_t working_revision;
  svn_ra_session_t *target_ra_session;
  svn_ra_session_t *source_ra_session;
  const char *source_repos_rel_path, *target_repos_rel_path;
  const char *yc_ancestor_path;
  svn_revnum_t yc_ancestor_rev;
  const char *url1, *url2;
  svn_revnum_t rev1, rev2;
  svn_mergeinfo_t unmerged_to_source_mergeinfo_catalog;
  svn_boolean_t use_sleep = FALSE;
  svn_error_t *err;
  static const svn_wc_entry_callbacks2_t walk_callbacks =
    { get_subtree_mergeinfo_walk_cb, get_mergeinfo_error_handler };
  struct get_subtree_mergeinfo_walk_baton wb;

  /* Open an admistrative session with the working copy. */
  SVN_ERR(svn_wc_adm_probe_open3(&adm_access, NULL, target_wcpath,
                                 (! dry_run), -1, ctx->cancel_func,
                                 ctx->cancel_baton, pool));

  /* Fetch the target's entry. */
  SVN_ERR(svn_wc__entry_versioned(&entry, target_wcpath, adm_access,
                                  FALSE, pool));

  /* Make sure we're dealing with a real URL. */
  SVN_ERR(svn_client_url_from_path(&url2, source, pool));
  if (! url2)
    return svn_error_createf(SVN_ERR_ENTRY_MISSING_URL, NULL,
                             _("'%s' has no URL"),
                             svn_path_local_style(source, pool));

  /* Determine the working copy target's repository root URL. */
  working_revision.kind = svn_opt_revision_working;
  SVN_ERR(svn_client__get_repos_root(&wc_repos_root, target_wcpath,
                                     &working_revision, adm_access, ctx,
                                     pool));

  /* Determine the source's repository root URL. */
  SVN_ERR(svn_client__get_repos_root(&source_repos_root, url2, peg_revision,
                                     adm_access, ctx, pool));

  /* source_repos_root and wc_repos_root are required to be the same,
     as mergeinfo doesn't come into play for cross-repository merging. */
  if (strcmp(source_repos_root, wc_repos_root) != 0)
    return svn_error_createf(SVN_ERR_CLIENT_UNRELATED_RESOURCES, NULL,
                             _("'%s' must be from the same repository as "
                               "'%s'"), svn_path_local_style(source, pool),
                             svn_path_local_style(target_wcpath, pool));

  SVN_ERR(ensure_wc_reflects_repository_subtree(target_wcpath, ctx, pool));

  /* As the WC tree is "pure", use its last-updated-to revision as
     the default revision for the left side of our merge, since that's
     what the repository sub-tree is required to be up to date with
     (with regard to the WC). */
  rev1 = entry->revision;

  source_repos_rel_path =
    url2 + 1 + strlen(svn_path_get_longest_ancestor(wc_repos_root, url2,
                                                    pool));
  source_repos_rel_path = svn_path_uri_decode(source_repos_rel_path, pool);
  SVN_ERR(svn_client__path_relative_to_root(&target_repos_rel_path,
                                            target_wcpath, wc_repos_root,
                                            FALSE, NULL, NULL, pool));

  /* Can't reintegrate to or from the root of the repository. */
  if (svn_path_is_empty(source_repos_rel_path)
      || svn_path_is_empty(target_repos_rel_path))
    return svn_error_createf(SVN_ERR_CLIENT_NOT_READY_TO_MERGE, NULL,
                             _("Neither the reintegrate source nor target "
                               "can be the root of the repository"));

  /* Find all the subtree's in TARGET_WCPATH that have explicit mergeinfo. */
  wb.target_path = target_wcpath;
  wb.target_repos_root = wc_repos_root;
  wb.base_access = adm_access;
  wb.subtrees_with_mergeinfo = apr_hash_make(pool);
  wb.ctx = ctx;
  SVN_ERR(svn_wc_walk_entries3(target_wcpath, adm_access, &walk_callbacks,
                               &wb, svn_depth_infinity, TRUE,
                               ctx->cancel_func, ctx->cancel_baton, pool));

  /* Open two RA sessions, one to our source and one to our target. */
  SVN_ERR(svn_client__open_ra_session_internal(&target_ra_session, entry->url,
                                               NULL, NULL, NULL, FALSE, FALSE,
                                               ctx, pool));
  SVN_ERR(svn_client__open_ra_session_internal(&source_ra_session, url2,
                                               NULL, NULL, NULL, FALSE, FALSE,
                                               ctx, pool));

  SVN_ERR(svn_client__get_revision_number(&rev2, NULL, source_ra_session,
                                          peg_revision, "", pool));

  SVN_ERR(calculate_left_hand_side(&url1, &rev1,
                                   &unmerged_to_source_mergeinfo_catalog,
                                   target_repos_rel_path,
                                   wb.subtrees_with_mergeinfo,
                                   rev1,
                                   source_repos_rel_path,
                                   source_repos_root,
                                   rev2,
                                   source_ra_session,
                                   target_ra_session,
                                   ctx,
                                   pool));

  /* If the target was moved after the source was branched from it,
     it is possible that the left URL differs from the target's current
     URL.  If so, then adjust TARGET_RA_SESSION to point to the old URL. */
  if (strcmp(url1, entry->url))
    SVN_ERR(svn_ra_reparent(target_ra_session, url1, pool));

  SVN_ERR(svn_client__get_youngest_common_ancestor(&yc_ancestor_path,
                                                   &yc_ancestor_rev,
                                                   url2, rev2,
                                                   url1, rev1,
                                                   ctx, pool));

  if (!(yc_ancestor_path && SVN_IS_VALID_REVNUM(yc_ancestor_rev)))
    return svn_error_createf(SVN_ERR_CLIENT_NOT_READY_TO_MERGE, NULL,
                             _("'%s@%ld' must be ancestrally related to "
                               "'%s@%ld'"), url1, rev1, url2, rev2);

  if (rev1 > yc_ancestor_rev)
    {
      /* Have we actually merged anything to the source from the
         target?  If so, make sure we've merged a contiguous
         prefix. */
      err = ensure_all_missing_ranges_are_phantoms(
        target_repos_rel_path, target_ra_session,
        unmerged_to_source_mergeinfo_catalog, pool);
      if (err)
        {
          if (err->apr_err == SVN_ERR_CLIENT_NOT_READY_TO_MERGE)
            {
              svn_string_t *source_mergeinfo_cat_string;

              svn_error_clear(err);
              err = NULL;
              SVN_ERR(svn_mergeinfo__catalog_to_formatted_string(
                &source_mergeinfo_cat_string,
                unmerged_to_source_mergeinfo_catalog,
                "  ", "    Missing ranges: ", pool));
              return svn_error_createf(SVN_ERR_CLIENT_NOT_READY_TO_MERGE,
                                       NULL,
                                       _("Reintegrate can only be used if "
                                         "revisions %ld through %ld were "
                                         "previously merged from %s to the "
                                         "reintegrate source, but this is "
                                         "not the case:\n%s"),
                                       yc_ancestor_rev + 1, rev2,
                                       entry->url,
                                       source_mergeinfo_cat_string->data);
            }
          else
            {
              return err;
            }
        }
    }

  /* Left side: trunk@youngest-trunk-rev-merged-to-branch-at-specified-peg-rev
   * Right side: branch@specified-peg-revision */

  /* Do the real merge! */
  /* ### TODO(reint): Make sure that one isn't the same line ancestor
     ### of the other (what's erroneously referred to as "ancestrally
     ### related" in this source file).  We can merge to trunk without
     ### implementing this. */
  err = merge_cousins_and_supplement_mergeinfo(target_wcpath, entry,
                                               adm_access,
                                               target_ra_session,
                                               source_ra_session,
                                               url1, rev1, url2, rev2,
                                               yc_ancestor_rev,
                                               source_repos_root,
                                               wc_repos_root,
                                               svn_depth_infinity,
                                               FALSE, FALSE, FALSE, dry_run,
                                               merge_options, &use_sleep,
                                               ctx, pool);

  if (use_sleep)
    svn_io_sleep_for_timestamps(target_wcpath, pool);

  if (err)
    return err;

  /* Shutdown the administrative session. */
  return svn_wc_adm_close2(adm_access, pool);
}



svn_error_t *
svn_client_merge_peg3(const char *source,
                      const apr_array_header_t *ranges_to_merge,
                      const svn_opt_revision_t *peg_revision,
                      const char *target_wcpath,
                      svn_depth_t depth,
                      svn_boolean_t ignore_ancestry,
                      svn_boolean_t force,
                      svn_boolean_t record_only,
                      svn_boolean_t dry_run,
                      const apr_array_header_t *merge_options,
                      svn_client_ctx_t *ctx,
                      apr_pool_t *pool)
{
  svn_wc_adm_access_t *adm_access;
  const svn_wc_entry_t *entry;
  const char *URL;
  apr_array_header_t *merge_sources;
  const char *wc_repos_root, *source_repos_root;
  svn_opt_revision_t working_rev;
  svn_ra_session_t *ra_session;
  apr_pool_t *sesspool;
  svn_boolean_t use_sleep = FALSE;
  svn_error_t *err;
  svn_boolean_t same_repos;

  /* No ranges to merge?  No problem. */
  if (ranges_to_merge->nelts == 0)
    return SVN_NO_ERROR;

  /* Open an admistrative session with the working copy. */
  SVN_ERR(svn_wc_adm_probe_open3(&adm_access, NULL, target_wcpath,
                                 (! dry_run), -1, ctx->cancel_func,
                                 ctx->cancel_baton, pool));

  /* Fetch the target's entry. */
  SVN_ERR(svn_wc__entry_versioned(&entry, target_wcpath, adm_access,
                                  FALSE, pool));

  /* Make sure we're dealing with a real URL. */
  SVN_ERR(svn_client_url_from_path(&URL, source, pool));
  if (! URL)
    return svn_error_createf(SVN_ERR_ENTRY_MISSING_URL, NULL,
                             _("'%s' has no URL"),
                             svn_path_local_style(source, pool));

  /* Determine the working copy target's repository root URL. */
  working_rev.kind = svn_opt_revision_working;
  SVN_ERR(svn_client__get_repos_root(&wc_repos_root, target_wcpath,
                                     &working_rev, adm_access, ctx, pool));

  /* Open an RA session to our source URL, and determine its root URL. */
  sesspool = svn_pool_create(pool);
  SVN_ERR(svn_client__open_ra_session_internal(&ra_session,
                                               URL, NULL, NULL, NULL,
                                               FALSE, TRUE, ctx, sesspool));
  SVN_ERR(svn_ra_get_repos_root2(ra_session, &source_repos_root, pool));

  /* Normalize our merge sources. */
  SVN_ERR(normalize_merge_sources(&merge_sources, source, URL,
                                  source_repos_root, peg_revision,
                                  ranges_to_merge, ra_session, ctx, pool));

  /* Check for same_repos. */
  if (strcmp(wc_repos_root, source_repos_root) != 0)
    {
      const char *source_repos_uuid;
      const char *wc_repos_uuid;

      SVN_ERR(svn_ra_get_uuid2(ra_session, &source_repos_uuid, pool));
      if (entry)
        wc_repos_uuid = entry->uuid;
      else
        SVN_ERR(svn_client_uuid_from_url(&wc_repos_uuid, wc_repos_root,
                                         ctx, pool));
      same_repos = (strcmp(wc_repos_uuid, source_repos_uuid) == 0);
    }
  else
    same_repos = TRUE;

  /* We're done with our little RA session. */
  svn_pool_destroy(sesspool);

  /* Do the real merge!  (We say with confidence that our merge
     sources are both ancestral and related.) */
  err = do_merge(NULL, merge_sources, target_wcpath, entry, adm_access,
                 TRUE, TRUE, same_repos, ignore_ancestry, force, dry_run,
                 record_only, FALSE, depth, merge_options,
                 &use_sleep, ctx, pool);

  if (use_sleep)
    svn_io_sleep_for_timestamps(target_wcpath, pool);

  if (err)
    return err;

  /* Shutdown the administrative session. */
  return svn_wc_adm_close2(adm_access, pool);
}
