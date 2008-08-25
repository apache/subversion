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
#include "svn_mergeinfo.h"
#include "svn_client.h"
#include "svn_string.h"
#include "svn_error.h"
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
 *     +--------------------------------+---------------------+
 *  -c |       mergeinfo-driven         |                     |
 *  or |        cherrypicking           |    unsupported      |
 *  -r |    (svn_client_merge_peg)      |                     |
 *     +--------------------------------+---------------------+
 *  no |      mergeinfo-driven          |   mergeinfo-writing |
 *  -c |        whole-branch            |    diff-and-apply   |
 *  or |       heuristic merge          |  (svn_client_merge) |
 *  -r | (svn_client_merge_reintegrate) |                     |
 *     +--------------------------------+---------------------+
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
 * CHILDREN_WITH_MERGEINFO is intially created by get_mergeinfo_paths()
 * and outside of that function and its helpers should always meet the
 * criteria dictated in get_mergeinfo_paths()'s doc string.
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

  SVN_ERR(svn_wc_entry(&ent, path, adm_access, TRUE, pool));

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
  const char *added_path;             /* Set to the dir path whenever the
                                         dir is added as a child of a
                                         versioned dir (dry-run only) */
  const char *target;                 /* Working copy target of merge */
  const char *url;                    /* The second URL in the merge */
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

  /* Flag indicating the fact target has everything merged already,
     for the sake of children's merge to work it sets itself a dummy
     merge range of requested_end_rev:requested_end_rev. */
  svn_boolean_t target_has_dummy_merge_range;

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
dry_run_deleted_p(merge_cmd_baton_t *merge_b, const char *wcpath)
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

/* Set *HONOR_MERGEINFO and *RECORD_MERGEINFO (if non-NULL)
   appropriately for MERGE_B.
   One rule is that we shan't record mergeinfo if we're not honoring it. */
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

/* Helper for merge_props_changed().  Filter out mergeinfo property additions
   to PATH when those additions refer to the same line of history.

   *PROPS is an array of svn_prop_t structures representing regular properties
   to be added to the working copy PATH.  ADM_ACCESS and MERGE_B are cascaded
   from merge_props_changed().

   If mergeinfo is not being honored, do nothing.  Otherwise examine the added
   mergeinfo, looking at each range (or single rev) of each source path.  If a
   source_path/range refers to the same line of history as PATH (pegged at its
   base revision), then filter out that range.  If the entire rangelist for a
   given path is filtered then filter out the path as well.  Set outgoing
   *PROPS to a shallow copy (allocated in POOL) of incoming *PROPS minus the
   filtered self-referential mergeinfo. */
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

  /* If we aren't honoring mergeinfo, get outta here. */
  mergeinfo_behavior(&honor_mergeinfo, NULL, merge_b);
  if (! honor_mergeinfo)
    return SVN_NO_ERROR;

  adjusted_props = apr_array_make(pool, (*props)->nelts, sizeof(svn_prop_t));
  for (i = 0; i < (*props)->nelts; ++i)
    {
      svn_prop_t *prop = &APR_ARRAY_IDX((*props), i, svn_prop_t);

      /* If this property isn't mergeinfo or is NULL valued (i.e. prop removal)
         or empty mergeinfo it does not require any special handling.  There
         is nothing to filter out of empty mergeinfo and the concept of
         filtering doesn't apply if we are trying to remove mergeinfo
         entirely. */
      if ((strcmp(prop->name, SVN_PROP_MERGEINFO) != 0)
          || (! prop->value)       /* Removal of mergeinfo */
          || (! prop->value->len)) /* Empty mergeinfo */
        {
          APR_ARRAY_PUSH(adjusted_props, svn_prop_t) = *prop;
        }
      else /* Non-empty mergeinfo; filter self-referential mergeinfo out. */
        {
          svn_mergeinfo_t mergeinfo, filtered_mergeinfo = NULL;
          apr_hash_index_t *hi;
          const char *target_url, *merge_source_root_url;
          const svn_wc_entry_t *target_entry;
          const char *old_url = NULL;

          SVN_ERR(svn_ra_get_repos_root2(merge_b->ra_session2,
                                         &merge_source_root_url, pool));

          /* Get an entry for PATH so we can find its base revision. */
          SVN_ERR(svn_wc__entry_versioned(&target_entry, path, adm_access,
                                          FALSE, pool));

          /* Temporarily reparent our RA session to the merge
             target's URL. */
          SVN_ERR(svn_client_url_from_path(&target_url, path, pool));
          SVN_ERR(svn_client__ensure_ra_session_url(&old_url,
                                                    merge_b->ra_session2,
                                                    target_url, pool));

          /* Parse the incoming mergeinfo to allow easier meddling. */
          SVN_ERR(svn_mergeinfo_parse(&mergeinfo, prop->value->data, pool));

          for (hi = apr_hash_first(NULL, mergeinfo);
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
              merge_source_url = svn_path_join(merge_source_root_url,
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
                          || err->apr_err == SVN_ERR_FS_NOT_FOUND)
                        {
                          /* PATH@TARGET_ENTRY->REVISION didn't exist at
                             RANGE->START or is unrelated to the resource
                             PATH@RANGE->START.  Either way we don't
                             filter. */
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
                         line of history at RANGE->START.  But it might
                         have existed under a different name then, so
                         check if the URL it had then is the same as the
                         URL for the mergeinfo we are trying to add.  If
                         it is the same we can filter it out. */
                      if (strcmp(start_url, merge_source_url) != 0)
                        {
                          APR_ARRAY_PUSH(adjusted_rangelist,
                                         svn_merge_range_t *) = range;
                        }
                    }
                } /* for (j = 0; j < rangelist->nelts; j++) */

              if (adjusted_rangelist->nelts)
                {
                  if (!filtered_mergeinfo)
                    filtered_mergeinfo = apr_hash_make(pool);
                  apr_hash_set(filtered_mergeinfo, source_path,
                               APR_HASH_KEY_STRING, adjusted_rangelist);
                }
            } /* mergeinfo hash iteration */

          /* If only some of the ranges mapped from SOURCE_PATH were
             filtered then create a new svn_prop_t to represent
             this.  Otherwise everything was filtered and we can
             ignore the svn:merginfo props entirely. */
          if (filtered_mergeinfo)
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

          /* If we reparented MERGE_B->RA_SESSION2 above, put it back
             to the original URL. */
          if (old_url)
            SVN_ERR(svn_ra_reparent(merge_b->ra_session2, old_url, pool));

        } /* Property is non-empty mergeinfo. */
    } /* (i = 0; i < (*props)->nelts; ++i) */

  *props = adjusted_props;
  return SVN_NO_ERROR;
}

/* An svn_wc_diff_callbacks3_t function.  Used for both file and directory
   property merges. */
static svn_error_t *
merge_props_changed(svn_wc_adm_access_t *adm_access,
                    svn_wc_notify_state_t *state,
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

  SVN_ERR(svn_categorize_props(propchanges, NULL, NULL, &props, subpool));

  /* We only want to merge "regular" version properties:  by
     definition, 'svn merge' shouldn't touch any data within .svn/  */
  if (props->nelts)
    {
      /* svn_wc_merge_props() requires ADM_ACCESS to be the access for
         the parent of PATH. Since the advent of merge tracking,
         do_directory_merge() may call this (indirectly) with
         the access for the merge_b->target instead (issue #2781).
         So, if we have the wrong access, get the right one. */
      if (svn_path_compare_paths(svn_wc_adm_access_path(adm_access),
                                 path) != 0)
        SVN_ERR(svn_wc_adm_probe_try3(&adm_access, adm_access, path,
                                      TRUE, -1, ctx->cancel_func,
                                      ctx->cancel_baton, subpool));

      /* Don't add mergeinfo from PATH's own history. */
      SVN_ERR(filter_self_referential_mergeinfo(&props, path, merge_b,
                                                adm_access, subpool));

      err = svn_wc_merge_props2(state, path, adm_access, original_props, props,
                                FALSE, merge_b->dry_run, ctx->conflict_func,
                                ctx->conflict_baton, subpool);
      if (err && (err->apr_err == SVN_ERR_ENTRY_NOT_FOUND
                  || err->apr_err == SVN_ERR_UNVERSIONED_RESOURCE))
        {
          /* if the entry doesn't exist in the wc, just 'skip' over
             this part of the tree-delta. */
          if (state)
            *state = svn_wc_notify_state_missing;
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

  /* Easy out:  no access baton means there ain't no merge target */
  if (adm_access == NULL)
    {
      if (content_state)
        *content_state = svn_wc_notify_state_missing;
      if (prop_state)
        *prop_state = svn_wc_notify_state_missing;
      svn_pool_destroy(subpool);
      return SVN_NO_ERROR;
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
    SVN_ERR(merge_props_changed(adm_access, prop_state, mine, prop_changes,
                                original_props, baton));
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
          conflict_resolver_baton_t conflict_baton =
            { merge_b->ctx->conflict_func, merge_b->ctx->conflict_baton,
              &merge_b->conflicted_paths, merge_b->pool };
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
  apr_hash_t *new_props;

  /* In most cases, we just leave prop_state as unknown, and let the
     content_state what happened, so we set prop_state here to avoid that
     below. */
  if (prop_state)
    *prop_state = svn_wc_notify_state_unknown;

  /* Apply the prop changes to a new hash table. */
  new_props = apr_hash_copy(subpool, original_props);
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

      apr_hash_set(new_props, prop->name, APR_HASH_KEY_STRING, prop->value);
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
          if (prop_state && apr_hash_count(new_props))
            *prop_state = svn_wc_notify_state_changed;
        }
      else
        *content_state = svn_wc_notify_state_missing;
      svn_pool_destroy(subpool);
      return SVN_NO_ERROR;
    }

  SVN_ERR(svn_io_check_path(mine, &kind, subpool));
  switch (kind)
    {
    case svn_node_none:
      {
        const svn_wc_entry_t *entry;
        SVN_ERR(svn_wc_entry(&entry, mine, adm_access, FALSE, subpool));
        if (entry && entry->schedule != svn_wc_schedule_delete)
          {
            /* It's versioned but missing. */
            if (content_state)
              *content_state = svn_wc_notify_state_obstructed;
            svn_pool_destroy(subpool);
            return SVN_NO_ERROR;
          }
        if (! merge_b->dry_run)
          {
            const char *copyfrom_url = NULL;
            svn_revnum_t copyfrom_rev = SVN_INVALID_REVNUM;

            /* If this is a merge from the same repository as our working copy,
               we handle adds as add-with-history.  Otherwise, we'll use a pure
               add. */
            if (merge_b->same_repos)
              {
                const char *child = svn_path_is_child(merge_b->target,
                                                      mine, subpool);
                if (child != NULL)
                  copyfrom_url = svn_path_url_add_component(merge_b->url,
                                                            child, subpool);
                else
                  copyfrom_url = merge_b->url;
                copyfrom_rev = rev2;
                SVN_ERR(check_scheme_match(adm_access, copyfrom_url));
              }

            /* Since 'mine' doesn't exist, and this is
               'merge_file_added', I hope it's safe to assume that
               'older' is empty, and 'yours' is the full file.  Merely
               copying 'yours' to 'mine', isn't enough; we need to get
               the whole text-base and props installed too, just as if
               we had called 'svn cp wc wc'. */
            SVN_ERR(svn_wc_add_repos_file2(mine, adm_access, yours, NULL,
                                           new_props, NULL, copyfrom_url,
                                           copyfrom_rev, subpool));
          }
        if (content_state)
          *content_state = svn_wc_notify_state_changed;
        if (prop_state && apr_hash_count(new_props))
          *prop_state = svn_wc_notify_state_changed;
      }
      break;
    case svn_node_dir:
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
        /* file already exists, is it under version control? */
        const svn_wc_entry_t *entry;
        SVN_ERR(svn_wc_entry(&entry, mine, adm_access, FALSE, subpool));

        /* If it's an unversioned file, don't touch it.  If it's scheduled
           for deletion, then rm removed it from the working copy and the
           user must have recreated it, don't touch it */
        if (!entry || entry->schedule == svn_wc_schedule_delete)
          {
            /* this will make the repos_editor send a 'skipped' message */
            if (content_state)
              *content_state = svn_wc_notify_state_obstructed;
          }
        else
          {
            if (dry_run_deleted_p(merge_b, mine))
              {
                if (content_state)
                  *content_state = svn_wc_notify_state_changed;
              }
            else
              {
                /* Indicate that we merge because of an add to handle a
                   special case for binary files with no local mods. */
                  merge_b->add_necessitated_merge = TRUE;

                  SVN_ERR(merge_file_changed(adm_access, content_state,
                                             prop_state, mine, older, yours,
                                             rev1, rev2,
                                             mimetype1, mimetype2,
                                             prop_changes, original_props,
                                             baton));

                /* Reset the state so that the baton can safely be reused
                   in subsequent ops occurring during this merge. */
                  merge_b->add_necessitated_merge = FALSE;
              }
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

  SVN_ERR(svn_io_check_path(mine, &kind, subpool));
  switch (kind)
    {
    case svn_node_file:
      {
        svn_boolean_t same;

        /* If the files are identical, attempt deletion */
        SVN_ERR(files_same_p(&same, older, original_props, mine, adm_access,
                             subpool));
        if (same || merge_b->force)
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
            /* The files differ, so skip instead of deleting */
            if (state)
              *state = svn_wc_notify_state_obstructed;
          }
      }
      break;
    case svn_node_dir:
      if (state)
        *state = svn_wc_notify_state_obstructed;
      break;
    case svn_node_none:
      /* file is already non-existent, this is a no-op. */
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
      copyfrom_url = svn_path_url_add_component(merge_b->url, child, subpool);
      copyfrom_rev = rev;
      SVN_ERR(check_scheme_match(adm_access, copyfrom_url));
    }

  SVN_ERR(svn_io_check_path(path, &kind, subpool));
  switch (kind)
    {
    case svn_node_none:
      SVN_ERR(svn_wc_entry(&entry, path, adm_access, FALSE, subpool));
      if (entry && entry->schedule != svn_wc_schedule_delete)
        {
          /* Versioned but missing */
          if (state)
            *state = svn_wc_notify_state_obstructed;
          svn_pool_destroy(subpool);
          return SVN_NO_ERROR;
        }
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
      SVN_ERR(svn_wc_entry(&entry, path, adm_access, TRUE, subpool));
      if (! entry || entry->schedule == svn_wc_schedule_delete)
        {
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
      else if (state)
        {
          if (dry_run_deleted_p(merge_b, path))
            *state = svn_wc_notify_state_changed;
          else
            *state = svn_wc_notify_state_obstructed;
        }
      break;
    case svn_node_file:
      if (merge_b->dry_run)
        merge_b->added_path = NULL;

      if (state)
        {
          SVN_ERR(svn_wc_entry(&entry, path, adm_access, FALSE, subpool));

          if (entry && dry_run_deleted_p(merge_b, path))
            /* ### TODO: Retain record of this dir being added to
               ### avoid problems from subsequent edits which try to
               ### add children. */
            *state = svn_wc_notify_state_changed;
          else
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
                  const char *path,
                  void *baton)
{
  merge_cmd_baton_t *merge_b = baton;
  apr_pool_t *subpool = svn_pool_create(merge_b->pool);
  svn_node_kind_t kind;
  svn_wc_adm_access_t *parent_access;
  const char *parent_path;
  svn_error_t *err;

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

  SVN_ERR(svn_io_check_path(path, &kind, subpool));
  switch (kind)
    {
    case svn_node_dir:
      {
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
            if (state)
              *state = svn_wc_notify_state_obstructed;
            svn_error_clear(err);
          }
        else if (state)
          {
            *state = svn_wc_notify_state_changed;
          }
      }
      break;
    case svn_node_file:
      if (state)
        *state = svn_wc_notify_state_obstructed;
      break;
    case svn_node_none:
      /* dir is already non-existent, this is a no-op. */
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
                 const char *path,
                 svn_revnum_t rev,
                 void *baton)
{
  return SVN_NO_ERROR;
}

/* An svn_wc_diff_callbacks3_t function. */
static svn_error_t *
merge_dir_closed(svn_wc_adm_access_t *adm_access,
                 svn_wc_notify_state_t *state,
                 const char *path,
                 void *baton)
{
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

  /* The list of merged paths. */
  apr_hash_t *merged_paths;

  /* The list of any skipped paths, which should be examined and
     cleared after each invocation of the callback. */
  apr_hash_t *skipped_paths;

  /* A list of the root paths of any added subtrees which might require
     their own explicit mergeinfo. */
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
                     || notify->action == svn_wc_notify_update_add)

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
              notify->action == svn_wc_notify_update_delete ? FALSE : TRUE,
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
                       && notify_b->merge_b->target_has_dummy_merge_range))
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

/* Helper for the numerous times we need to allocate a svn_merge_range_t
   and push it onto a rangelist.

   Push onto RANGELIST an svn_merge_range_t * element allocated in
   POOL and defined by START, END, and INHERITABLE. */
static void
push_range(apr_array_header_t *rangelist,
           svn_revnum_t start,
           svn_revnum_t end,
           svn_boolean_t inheritable,
           apr_pool_t *pool)
{
  svn_merge_range_t *range = apr_pcalloc(pool, sizeof(*range));

  range->start = start;
  range->end = end;
  range->inheritable = inheritable;
  APR_ARRAY_PUSH(rangelist, svn_merge_range_t *) = range;
}

/* Helper for filter_merged_revisions() when that function is operating on
   a *subtree* of the merge target.  Like filter_merged_revisions(), this
   should only be called when honoring mergeinfo.

   MERGEINFO_PATH, PARENT, REVISION1, REVISION2, PRIMARY_URL, RA_SESSION,
   and CTX are all cascaded from filter_merged_revisions() - see that function
   for more information on each.

   Since this function is only invoked for subtrees of the merge target, the
   guarantees afforded by normalize_merge_sources() don't apply.  Therefore it
   is possible that PRIMARY_URL@REVISION1 and PRIMARY_URL@REVISION2 don't
   describe the endpoints of an unbroken line of history.  The purpose of
   this helper is to identify these cases of broken history and where possible
   to adjust the requested range REVISION1:REVISION2 being merged to the subtree
   so that we don't try to describe invalid path/revisions to the merge report
   editor -- see drive_merge_report_editor().

   Set *CHILD_DELETED_OR_NONEXISTANT and *REQUESTED_RANGELIST as described
   in the following eight cases.  *REQUESTED_RANGELIST is an array of
   svn_merge_range_t *elements allocated from POOL.  Unless noted otherwise,
   *REQUESTED_RANGELIST is set to a rangelist containing one svn_merge_range_t
   *element with a 'start' field equal to REVISION1, an 'end' field equal to
   REVISION2.  The inheritable fields of all svn_merge_range_t in
   *REQUESTED_RANGELIST, in all cases, are always set to true.

   Forward Merges, i.e. REVISION1 < REVISION2

     A) Requested range deletes subtree.

        PRIMARY_URL@REVISION1 exists, but PRIMARY_URL@REVISION2 doesn't
        exist because PRIMARY_URL was deleted prior to REVISION2.

        Set *CHILD_DELETED_OR_NONEXISTANT to TRUE.

     B) Part of requested range predates subtree's existance.

        PRIMARY_URL@REVISION2 exists, but PRIMARY_URL@REVISION1 doesn't
        exist because PRIMARY_URL didn't come into existence until some
        revision 'N' where N > REVISION1.

        Set *CHILD_DELETED_OR_NONEXISTANT to FALSE.  Populate
        *REQUESTED_RANGELIST with the ranges between N and REVISION2
        (inclusive) at which PRIMARY_URL exists.  Then take the intersection
        of REVISION1:N (i.e. the range which predates the existance of
        PRIMARY_URL) and PARENT->REMAINING_RANGELIST and add it to
        *REQUESTED_RANGELIST.  This prevents us from later trying to describe
        any non-existant path/revs for this subtree in
        drive_merge_report_editor().  A good thing as that would break the
        editor.

     C) Subtree doesn't exist in requested range or exists inside the
        requested range but is ultimately deleted.

        Neither PRIMARY_URL@REVISION1 or PRIMARY_URL@REVISION2 exist.

        Set *CHILD_DELETED_OR_NONEXISTANT to TRUE

     D) Subtree exists at start and end of requested range and was
        not replaced within that range.

        PRIMARY_URL@REVISION1 and PRIMARY_URL@REVISION2 both exist.

        Set *CHILD_DELETED_OR_NONEXISTANT to FALSE.

  Reverse Merges, i.e. REVISION1 > REVISION2

     E) Part of requested range postdates subtree's existance.

        PRIMARY_URL@REVISION2 exists, but PRIMARY_URL@REVISION1 doesn't
        exist because PRIMARY_URL was deleted prior to REVISION1.

        Set *CHILD_DELETED_OR_NONEXISTANT to FALSE.

        ### This is tricky, sort of the inverse of B; we want to reverse
        ### merge some range M:N, let's say 14:4, into the subtree, but the
        ### subtree was deleted at r10.  *BUT* we only allow reverse merges
        ### of ranges that exist in implicit or explicit mergeinfo.  Can't we
        ### simply set *REQUESTED_RANGELIST to REVISION1:REVISION2 and let the
        ### existing code in filter_merged_revisions() do its thing?  Because
        ### if the subtree has any explicit mergeinfo (via inheritance)
        ### describing ranges that postdate the subtree's existance, the
        ### subtree's nearest parent must also have that mergeinfo right?
        ### Put another way, how can all of the  following ever be true?
        ###
        ###   i)   The subtree merge source doesn't exist anymore at
        ###        revsion X.
		###
        ###   ii)  Mergeinfo for X is explicitly set on the subtree.
        ###
		###   iii) The subtree's parent has no explicit mergeinfo for X.

     F) Requested range deletes (or replaces) a subtree.

        PRIMARY_URL@REVISION1 exists, but PRIMARY_URL@REVISION2 doesn't
        exist because PRIMARY_URL didn't come into existence until
        *after* REVISION2.  Or PRIMARY_URL@REVISION1 and PRIMARY_URL@REVISION2
        both exist, but they don't describe an unbroken line of history.

        Set *CHILD_DELETED_OR_NONEXISTANT to TRUE.

     G) Subtree doesn't exist in requested range or exists inside the
        requested range but is ultimately deleted.

        Neither PRIMARY_URL@REVISION1 or PRIMARY_URL@REVISION2 exist.

        Set *CHILD_DELETED_OR_NONEXISTANT to TRUE.

     H) Subtree exists at start and end of requested range and was
        not replaced within that range.

        PRIMARY_URL@REVISION1 and PRIMARY_URL@REVISION2 both exist and
        describe the start and end of an unbroken line of history.

        Set *CHILD_DELETED_OR_NONEXISTANT to FALSE.

   All the allocations are made from POOL. */
static svn_error_t *
prepare_subtree_ranges(apr_array_header_t **requested_rangelist,
                       svn_boolean_t *child_deleted_or_nonexistant,
                       const char *mergeinfo_path,
                       svn_client__merge_path_t *parent,
                       svn_revnum_t revision1,
                       svn_revnum_t revision2,
                       const char *primary_url,
                       svn_ra_session_t *ra_session,
                       svn_client_ctx_t *ctx,
                       apr_pool_t *pool)
{
  svn_boolean_t is_rollback = revision2 < revision1;
  svn_revnum_t peg_rev = is_rollback ? revision1 : revision2;
  svn_revnum_t start_rev = is_rollback ? revision1 : revision2;
  svn_revnum_t end_rev = is_rollback ? revision2 : revision1;
  apr_array_header_t *segments;
  const char *rel_source_path;
  const char *session_url;
  svn_error_t *err;

  SVN_ERR(svn_ra_get_session_url(ra_session, &session_url, pool));
  SVN_ERR(svn_client__path_relative_to_root(&rel_source_path,
                                            primary_url,
                                            session_url,
                                            FALSE,
                                            ra_session,
                                            NULL,
                                            pool));
  err = svn_client__repos_location_segments(&segments, ra_session,
                                            rel_source_path, peg_rev,
                                            start_rev, end_rev, ctx, pool);

  /* If REL_SOURCE_PATH@PEG_REV doesn't exist then
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
          svn_error_clear(err);
          if (is_rollback)
            {
              svn_dirent_t *dirent;
              SVN_ERR(svn_ra_stat(ra_session, rel_source_path,
                                  revision2,  &dirent, pool));
              if (dirent)
                *child_deleted_or_nonexistant = FALSE; /* Case E. */
              else
                *child_deleted_or_nonexistant = TRUE;  /* Case G. */
            }
          else
            {
              *child_deleted_or_nonexistant = TRUE; /* Case A & C. */
            }

          *requested_rangelist = init_rangelist(revision1, revision2,
                                                TRUE, pool);
        }
      else
        return err;
    }
  else
    {
      if (segments->nelts)
        {
          /* This algorithm needs the youngest location segment inside the
             requested merge range.
             svn_client__repos_location_segments gives the segments ordered
             from oldest to youngest.
             So consider the last segment as it is the youngest.
           */
          svn_location_segment_t *segment =
            APR_ARRAY_IDX(segments, (segments->nelts - 1),
                          svn_location_segment_t *);
          if (is_rollback)
            {
              if (segment->range_start == revision2
                  && segment->range_end == revision1)
                {
                  /* Case H. */
                  *requested_rangelist = init_rangelist(revision1, revision2,
                                                        TRUE, pool);
                  *child_deleted_or_nonexistant = FALSE;
                }
              else /* Multiple location segements found. */
                {
                  /* Case F. */
                  *requested_rangelist = init_rangelist(revision1, revision2,
                                                        TRUE, pool);
                  *child_deleted_or_nonexistant = TRUE;
                }
            }
          else /* Forward merge */
            {
              /* Again, because REVISION2 is the peg revision for the call
                 to svn_client__repos_location_segments, we know that the
                 range_end of the last segment in segments is equal to
                 REVISION2. */
              if (segment->range_start == revision1
                  && segment->range_end == revision2)
                {
                  /* Case D. */
                  *requested_rangelist = init_rangelist(revision1, revision2,
                                                        TRUE, pool);
                  *child_deleted_or_nonexistant = FALSE;
                }
              else /* segment->range_start != revision1, since
                      segment->range_start can't be less than REVISION1,
                      this implies revision1 < segment->range_start. */
                {
                  /* Case B. */
                  int i;
                  apr_array_header_t *predate_intersection_rangelist;
                  apr_array_header_t *different_name_rangelist =
                    apr_array_make(pool, 1, sizeof(svn_merge_range_t *));

                  /* Make a rangelist that describes the range which predates
                     PRIMARY_URL's existance... */
                  apr_array_header_t *predate_rangelist =
                    init_rangelist(revision1,
                                   segment->range_start,
                                   TRUE, pool);
                  /* ...Find the intersection of that rangelist and the
                     subtree's parent's remaining ranges. */
                  SVN_ERR(svn_rangelist_intersect(
                    &predate_intersection_rangelist,
                    predate_rangelist,
                    parent->remaining_ranges,
                    FALSE, pool));
                  *requested_rangelist =
                    init_rangelist(segment->range_start,
                                   revision2,
                                   TRUE, pool);
                  /* Merge *REQUESTED_RANGELIST with its parent's remaining
                     ranges the intersect with the subtree's prehistory. */
                  SVN_ERR(svn_rangelist_merge(
                    requested_rangelist, predate_intersection_rangelist,
                    pool));

                  /* Remove ranges that predate PRIMARY_URL's existance
                     because the source exists under a different URL due to a
                     rename between REVISION1:REVISION2 - see 'MERGE FAILS' in
                     http://subversion.tigris.org/issues/show_bug.cgi?id=3067#desc34.
                     */
                  for (i = 0; i < segments->nelts; i++)
                    {
                      segment =
                        APR_ARRAY_IDX(segments, i, svn_location_segment_t *);
                      if (segment->path
                          && strcmp(segment->path, mergeinfo_path + 1) != 0)
                        push_range(different_name_rangelist,
                                   segment->range_start,
                                   segment->range_end, TRUE, pool);
                    }
                  if (different_name_rangelist->nelts)
                    SVN_ERR(svn_rangelist_remove(requested_rangelist,
                                                 different_name_rangelist,
                                                 *requested_rangelist, FALSE,
                                                 pool));
                  *child_deleted_or_nonexistant = FALSE;
                }
            }
        }
    } /* ! err */

  return SVN_NO_ERROR;
}

/*-----------------------------------------------------------------------*/

/*** Determining What Remains To Be Merged ***/

/* Helper for calculate_remaining_ranges().

   Calculate the ranges that remain to be merged from the merge
   source MERGEINFO_PATH (relative to the repository root) to the working
   copy path represented by CHILD -- for use by drive_merge_report_editor()'s
   application of the editor to the WC.  Set CHILD->remaining_ranges to the
   set of revisions to merge.

   PARENT, PRIMARY_URL, IS_SUBTREE, RA_SESSION, CTX are cascaded from
   calculate_remaining_ranges().  If IS_SUBTREE is FALSE then PARENT is
   ignored.

   REVISION1 and REVISION2 describe the merge range requested from
   MERGEINFO_PATH.

   TARGET_MERGEINFO is the path's explicit or inherited mergeinfo.
   May be NULL if there is not mergeinfo or an empty hash for
   empty mergeinfo.

   IMPLICIT_MERGEINFO is the path's natural history described as
   mergeinfo - see svn_client__get_history_as_mergeinfo().

   NOTE: This should only be called when honoring mergeinfo.
*/
static svn_error_t *
filter_merged_revisions(svn_client__merge_path_t *parent,
                        svn_client__merge_path_t *child,
                        const char *mergeinfo_path,
                        svn_mergeinfo_t target_mergeinfo,
                        svn_mergeinfo_t implicit_mergeinfo,
                        svn_revnum_t revision1,
                        svn_revnum_t revision2,
                        const char *primary_url,
                        svn_ra_session_t *ra_session,
                        svn_boolean_t is_subtree,
                        svn_client_ctx_t *ctx,
                        apr_pool_t *pool)
{
  apr_array_header_t *target_rangelist = NULL;
  svn_mergeinfo_t mergeinfo = implicit_mergeinfo;
  apr_array_header_t *requested_merge;

  if (is_subtree)
    {
      /* If CHILD is the merge target we then know that PRIMARY_URL,
         REVISION1, and REVISION2 are provided by normalize_merge_sources()
         -- see 'MERGEINFO MERGE SOURCE NORMALIZATION'.  Due to this
         normalization we know that PRIMARY_URL@REVISION1 and
         PRIMARY_URL@REVISION2 describe an unbroken line of history such
         that the entire range described by REVISION1:REVISION2 can
         potentially be merged to CHILD.  So we simply convert REVISION1 and
         REVISION2 to a rangelist and proceed to the filtering of merged
         revisions.

         But if CHILD is a subtree we don't have the same guarantees about
         PRIMARY_URL, REVISION1, and REVISION2 as we do for the merge target.
         PRIMARY_URL@REVSION1 and/or PRIMARY_URL@REVSION2 might not exist.

         If one or both doesn't exist, we need to know so we don't later try
         to describe these invalid subtrees in drive_merge_report_editor(),
         as that will break the merge. */
      svn_boolean_t child_deleted_or_nonexistant;
      SVN_ERR(prepare_subtree_ranges(&requested_merge,
                                     &child_deleted_or_nonexistant,
                                     mergeinfo_path, parent,
                                     revision1, revision2,
                                     primary_url, ra_session, ctx, pool));
      if (child_deleted_or_nonexistant && parent)
        {
          /* A little trick: If CHILD is a subtree which will be deleted by
             the requested merge or simply doesn't exist along the line of
             history described by PRIMARY_URL@REVSION1 -> PRIMARY_URL@REVSION2,
             then don't bother dealing with CHILD in a separate editor drive.
             Just make child's remaining ranges exactly the same as its
             nearest parent.

             For deletions this will cause the editor drive to be rooted at
             the subtree CHILD's nearest parent in CHILDREN_WITH_MERGEINFO
             This will simply delete the subtree.  For the case where neither
             PRIMARY_URL@REVSION1 or PRIMARY_URL@REVSION2 exist, there is
             nothing to merge to the subtree, so ignoring it completely is
             safe. See
             http://subversion.tigris.org/issues/show_bug.cgi?id=3067#desc5.
             */
          child->remaining_ranges =
            svn_rangelist_dup(parent->remaining_ranges, pool);
          return SVN_NO_ERROR;
        }
    }
  else
    {
      /* Convert REVISION1 and REVISION2 to a rangelist.

         Note: Talking about a requested merge range's inheritability doesn't
         make much sense, but as we are using svn_merge_range_t to describe
         it we need to pick *something*.  Since all the rangelist
         manipulations in this function either don't consider inheritance
         by default or we are requesting that they don't (i.e.
         svn_rangelist_remove and svn_rangelist_intersect) then we could
         set the inheritability as FALSE, it won't matter either way. */
      requested_merge = init_rangelist(revision1, revision2, TRUE, pool);
    }

  /* Now filter out revisions that have already been merged to CHILD. */
  if (revision1 > revision2) /* This is a reverse merge. */
    {
      if (target_mergeinfo)
        {
          mergeinfo = svn_mergeinfo_dup(implicit_mergeinfo, pool);
          SVN_ERR(svn_mergeinfo_merge(mergeinfo, target_mergeinfo, pool));
        }

      target_rangelist = apr_hash_get(mergeinfo,
                                      mergeinfo_path, APR_HASH_KEY_STRING);
      if (target_rangelist)
        {
          /* Return the intersection of the revs which are both
             already represented by the WC and are requested for
             revert.  The revert range and will need to be reversed
             for our APIs to work properly, as will the output for the
             revert to work properly. */
          SVN_ERR(svn_rangelist_reverse(requested_merge, pool));

          /* We don't consider inheritance we determining intersecting
             ranges.  If we *did* consider inheritance, then our calculation
             would be wrong.  For example, if the REQUESTED_MERGE is 5:3 and
             TARGET_RANGELIST is r5* (non-inheritable) then the intersection
             would be r4.  And that would be wrong as we clearly want to
             reverse merge both r4 and r5 in this case.  Ignoring the ranges'
             inheritance results in an intersection of r4-5.

             You might be wondering about ENTRY's children, doesn't the above
             imply that we will reverse merge r4-5 from them?  Nope, this is
             safe to do because any path whose parent has non-inheritable
             ranges is always considered a subtree with differing mergeinfo
             even if that path has no explicit mergeinfo prior to the
             merge -- See condition 3 in the doc string for
             merge.c:get_mergeinfo_paths(). */
          SVN_ERR(svn_rangelist_intersect(&(child->remaining_ranges),
                                          target_rangelist,
                                          requested_merge, FALSE, pool));

          SVN_ERR(svn_rangelist_reverse(child->remaining_ranges, pool));
        }
      else
        {
          child->remaining_ranges =
            apr_array_make(pool, 1, sizeof(svn_merge_range_t *));
        }
    }
  else /* This is a forward merge */
    {
      child->remaining_ranges = requested_merge;

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
      if (target_mergeinfo)
        target_rangelist = apr_hash_get(target_mergeinfo,
                                        mergeinfo_path, APR_HASH_KEY_STRING);
#else
      if (target_mergeinfo)
        {
          mergeinfo = svn_mergeinfo_dup(implicit_mergeinfo, pool);
          SVN_ERR(svn_mergeinfo_merge(mergeinfo, target_mergeinfo, pool));
        }

      target_rangelist = apr_hash_get(mergeinfo,
                                      mergeinfo_path, APR_HASH_KEY_STRING);
#endif
      /* See earlier comment preceeding svn_rangelist_intersect() for
         why we don't consider inheritance here. */
      if (target_rangelist)
        SVN_ERR(svn_rangelist_remove(&(child->remaining_ranges),
                                     target_rangelist,
                                     requested_merge, FALSE, pool));
    }
  return SVN_NO_ERROR;
}

/* Helper for do_file_merge and do_directory_merge (by way of
   populate_remaining_ranges() for the latter).

   Determine what portions of URL1@REVISION1 -> URL2@REVISION2 have already
   been merged to CHILD->PATH and populate CHILD->REMAINING_RANGES with the
   ranges that still need merging.

   SOURCE_ROOT_URL, URL1, REVISION1, URL2, REVISION2, TARGET_MERGEINFO,
   IMPLICIT_MERGEINFO, RA_SESSION, and CTX are all cascaded from the
   caller's arguments of the same names.

   If IS_SUBTREE is FALSE then CHILD describes the merge target and the
   requirements around the values of URL1, REVISION1, URL2, and REVISION2
   described in 'MERGEINFO MERGE SOURCE NORMALIZATION' hold.  If IS_SUBTREE
   is TRUE then CHILD describes some subtree of a merge target and these
   normalization conditions do not necessarily hold.  IS_SUBTREE should
   always be FALSE when calling from do_file_merge().

   If IS_SUBTREE is FALSE then PARENT is ignored, otherwise PARENT must
   represent the nearest working copy ancestor of CHILD.

   NOTE: This should only be called when honoring mergeinfo.

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
                           svn_mergeinfo_t implicit_mergeinfo,
                           svn_boolean_t is_subtree,
                           svn_ra_session_t *ra_session,
                           const svn_wc_entry_t *entry,
                           svn_client_ctx_t *ctx,
                           apr_pool_t *pool)
{
  const char *mergeinfo_path;
  const char *primary_url = (revision1 < revision2) ? url2 : url1;

  /* Determine which of the requested ranges to consider merging... */
  SVN_ERR(svn_client__path_relative_to_root(&mergeinfo_path, primary_url,
                                            source_root_url, TRUE,
                                            ra_session, NULL, pool));
  SVN_ERR(filter_merged_revisions(parent, child, mergeinfo_path,
                                  target_mergeinfo, implicit_mergeinfo,
                                  revision1, revision2, primary_url,
                                  ra_session, is_subtree, ctx, pool));

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
  const char *session_url = NULL, *url;
  svn_revnum_t target_rev;
  svn_opt_revision_t peg_revision;
  apr_pool_t *sesspool = NULL;

  /* Assert that we have sane input. */
  SVN_ERR_ASSERT(SVN_IS_VALID_REVNUM(start)
                 && SVN_IS_VALID_REVNUM(end)
                 && (start > end));

  /* First, we get the real mergeinfo. */
  SVN_ERR(svn_client__get_wc_or_repos_mergeinfo(recorded_mergeinfo, entry,
                                                indirect, FALSE, inherit,
                                                ra_session, target_wcpath,
                                                adm_access, ctx, pool));

  peg_revision.kind = svn_opt_revision_working;
  SVN_ERR(svn_client__derive_location(&url, &target_rev, target_wcpath,
                                      &peg_revision, ra_session, adm_access,
                                      ctx, pool));
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
                                                   NULL, NULL, NULL, FALSE,
                                                   TRUE, ctx, sesspool));
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
                                               ra_session, NULL, ctx, pool));

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

  return SVN_NO_ERROR;
}


/* Helper for do_directory_merge().

   For each child in CHILDREN_WITH_MERGEINFO, populates that
   child's remaining_ranges list.  CHILDREN_WITH_MERGEINFO is expected
   to be sorted in depth first order.  All persistent allocations are
   from CHILDREN_WITH_MERGEINFO->pool.

   If HONOR_MERGEINFO is set, this function will actually try to be
   intelligent about populating remaining_ranges list.  Otherwise, it
   will claim that each child has a single remaining range, from
   revision1, to revision2.

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
                          svn_boolean_t inheritable,
                          svn_boolean_t honor_mergeinfo,
                          svn_ra_session_t *ra_session,
                          const char *parent_merge_src_canon_path,
                          svn_wc_adm_access_t *adm_access,
                          merge_cmd_baton_t *merge_b)
{
  apr_pool_t *iterpool, *pool;
  int merge_target_len = strlen(merge_b->target);
  int i;

  pool = children_with_mergeinfo->pool;
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
          range->inheritable = inheritable;

          child->remaining_ranges =
            apr_array_make(pool, 1, sizeof(svn_merge_range_t *));
          APR_ARRAY_PUSH(child->remaining_ranges, svn_merge_range_t *) = range;
        }
      return SVN_NO_ERROR;
    }

  for (i = 0; i < children_with_mergeinfo->nelts; i++)
    {
      const char *child_repos_path;
      const svn_wc_entry_t *child_entry;
      const char *child_url1, *child_url2;
      svn_client__merge_path_t *child =
        APR_ARRAY_IDX(children_with_mergeinfo, i, svn_client__merge_path_t *);
      svn_client__merge_path_t *parent = NULL;

      /* If the path is absent don't do subtree merge either. */
      if (!child || child->absent)
        continue;

      svn_pool_clear(iterpool);

      if (strlen(child->path) == merge_target_len)
        child_repos_path = "";
      else
        child_repos_path = child->path +
          (merge_target_len ? merge_target_len + 1 : 0);
      child_url1 = svn_path_join(url1, child_repos_path, iterpool);
      child_url2 = svn_path_join(url2, child_repos_path, iterpool);

      SVN_ERR(svn_wc__entry_versioned(&child_entry, child->path, adm_access,
                                      FALSE, iterpool));

      SVN_ERR(get_full_mergeinfo(&(child->pre_merge_mergeinfo),
                                 &(child->implicit_mergeinfo), child_entry,
                                 &(child->indirect_mergeinfo),
                                 svn_mergeinfo_inherited, NULL, child->path,
                                 MAX(revision1, revision2),
                                 MIN(revision1, revision2),
                                 adm_access, merge_b->ctx, pool));

      /* If CHILD isn't the merge target find its parent. */
      if (i > 0)
        {
          int parent_index = find_nearest_ancestor(children_with_mergeinfo,
                                                   FALSE, child->path);
          parent = APR_ARRAY_IDX(children_with_mergeinfo, parent_index,
                                 svn_client__merge_path_t *);
          /* If CHILD is a subtree then its parent must be in
             CHILDREN_WITH_MERGEINFO, see the global comment
             'THE CHILDREN_WITH_MERGEINFO ARRAY'. */
          SVN_ERR_ASSERT(parent);
        }

      SVN_ERR(calculate_remaining_ranges(parent, child,
                                         source_root_url,
                                         child_url1, revision1,
                                         child_url2, revision2,
                                         child->pre_merge_mergeinfo,
                                         child->implicit_mergeinfo,
                                         i > 0 ? TRUE : FALSE, /* is subtree */
                                         ra_session, child_entry, merge_b->ctx,
                                         pool));
    }

  /* Take advantage of the depth first ordering,
     i.e first(0th) item is target.*/
  if (children_with_mergeinfo->nelts > 1)
    {
      svn_client__merge_path_t *child =
        APR_ARRAY_IDX(children_with_mergeinfo, 0, svn_client__merge_path_t *);

      if (child->remaining_ranges->nelts == 0)
        {
          svn_merge_range_t *dummy_range =
            apr_pcalloc(pool, sizeof(*dummy_range));
          dummy_range->start = revision2;
          dummy_range->end = revision2;
          dummy_range->inheritable = inheritable;
          child->remaining_ranges = apr_array_make(pool, 1,
                                                   sizeof(dummy_range));
          APR_ARRAY_PUSH(child->remaining_ranges, svn_merge_range_t *) =
            dummy_range;
          merge_b->target_has_dummy_merge_range = TRUE;
        }
    }

  svn_pool_destroy(iterpool);
  return SVN_NO_ERROR;
}


/*-----------------------------------------------------------------------*/

/*** Other Helper Functions ***/


/* Create mergeinfo describing the merge of RANGELIST into TARGET_WCPATH,
   accounting for paths unaffected by the merge due to skips or conflicts from
   NOTIFY_B. For 'immediates' merge it sets an inheritable mergeinfo
   corresponding to current merge on merge target. For 'files' merge it sets
   an inheritable mergeinfo corrsponding to current merge on merged files.
   If TARGET_WCPATH is a directory and it is missing an immediate child then
   TARGET_MISSING_CHILD should be true, otherwise it is false.*/
static svn_error_t *
determine_merges_performed(apr_hash_t **merges, const char *target_wcpath,
                           apr_array_header_t *rangelist,
                           svn_depth_t depth,
                           svn_wc_adm_access_t *adm_access,
                           notification_receiver_baton_t *notify_b,
                           merge_cmd_baton_t *merge_b,
                           apr_pool_t *pool)
{
  apr_size_t nbr_skips = (notify_b->skipped_paths != NULL ?
                          apr_hash_count(notify_b->skipped_paths) : 0);

  *merges = apr_hash_make(pool);
  apr_hash_set(*merges, target_wcpath, APR_HASH_KEY_STRING, rangelist);
  if (nbr_skips > 0)
    {
      apr_hash_index_t *hi;

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
          SVN_ERR(svn_wc_status2(&status, (const char *) skipped_path,
                                 adm_access, pool));
          if (status->text_status == svn_wc_status_none
              || status->text_status == svn_wc_status_unversioned)
            continue;

          /* Add an empty range list for this path.

             ### TODO: This works fine for a file path skipped because it is
             ### missing as long as the file's parent directory is present.
             ### But missing directory paths skipped are not handled yet,
             ### see issue #2915. */
          apr_hash_set(*merges, (const char *) skipped_path,
                       APR_HASH_KEY_STRING,
                       apr_array_make(pool, 0, sizeof(svn_merge_range_t)));

          if (nbr_skips < notify_b->nbr_notifications)
            /* ### Use RANGELIST as the mergeinfo for all children of
               ### this path which were not also explicitly
               ### skipped? */
            ;
        }
    }
  if ((depth != svn_depth_infinity) && notify_b->merged_paths)
    {
      apr_hash_index_t *hi;
      const void *merged_path;

      for (hi = apr_hash_first(NULL, notify_b->merged_paths); hi;
           hi = apr_hash_next(hi))
        {
          const svn_wc_entry_t *child_entry;
          apr_array_header_t *rangelist_of_child = NULL;
          apr_hash_this(hi, &merged_path, NULL, NULL);
          SVN_ERR(svn_wc__entry_versioned(&child_entry,
                                          merged_path,
                                          adm_access, FALSE,
                                          pool));
          if (((child_entry->kind == svn_node_dir)
               && (strcmp(merge_b->target, merged_path) == 0)
               && (depth == svn_depth_immediates))
              || ((child_entry->kind == svn_node_file)
                  && (depth == svn_depth_files)))
            {
              /* Set the explicit inheritable mergeinfo for,
                 1. Merge target directory if depth is immediates.
                 2. If merge is on a file and requested depth is 'files'.
               */
              int i;
              rangelist_of_child = svn_rangelist_dup(rangelist, pool);
              for (i = 0; i < rangelist_of_child->nelts; i++)
                {
                  svn_merge_range_t *rng =
                    APR_ARRAY_IDX(rangelist_of_child, i, svn_merge_range_t *);
                  rng->inheritable = TRUE;
                }
            }
          if (rangelist_of_child)
            {
              apr_hash_set(*merges, (const char *)merged_path,
                           APR_HASH_KEY_STRING, rangelist_of_child);
            }
        }
    }

  return SVN_NO_ERROR;
}

/* Calculate the new mergeinfo for the target tree based on the merge
   info for TARGET_WCPATH and MERGES (a mapping of WC paths to range
   lists), and record it in the WC (at, and possibly below,
   TARGET_WCPATH). */
static svn_error_t *
update_wc_mergeinfo(const char *target_wcpath, const svn_wc_entry_t *entry,
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
      int len;
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

  svn_pool_destroy(subpool);
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
      if (child
          && (child->absent || child->scheduled_for_deletion)
          && svn_path_is_ancestor(target_wcpath, child->path))
        {
          if (notify_b->skipped_paths)
            apr_hash_set(notify_b->skipped_paths, child->path,
                         APR_HASH_KEY_STRING, NULL);
          APR_ARRAY_IDX(children_with_mergeinfo, i,
                        svn_client__merge_path_t *) = NULL;
        }
    }
}

/* Helper for do_directory_merge().

   Set up the diff editor report to merge URL1@REVISION1 to URL2@REVISION2
   into TARGET_WCPATH and drive it.  Properly describe any subtrees of
   TARGET_WCPATH that require only a subset of REVISION1:REVISION2 to be
   merged -- These subtrees are described in CHILDREN_WITH_MERGEINFO, an
   array of svn_client__merge_path_t *, see
   'THE CHILDREN_WITH_MERGEINFO ARRAY' comment at the top of this file
   for more info.  Note that it is possible that TARGET_WCPATH needs only
   a subset of REVISION1:REVISION2 while its subtrees need the entire range.

   REVISION1 and REVISION2 must be bound by the set of remaining_ranges
   fields in CHILDREN_WITH_MERGEINFO's elements, specifically:

     1) For forward merges the oldest revision in all the remaining_ranges
        must be equal to REVISION1 and the youngest revision in the *first*
        range of all the remaining ranges must be equal to REVISION2.

     2) For reverse merges the youngest revision in all the remaining_ranges
        must be equal to REVISION1 and the oldest revision in the *first*
        range of all the remaining ranges must be equal to REVISION2.

   If IS_ROLLBACK is true this is a reverse merge, otherwise it is a
   forward merge.  DEPTH, NOTIFY_B, ADM_ACCESS, and MERGE_B are cascasded from
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
                          svn_boolean_t is_rollback,
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
      if (merge_b->target_has_dummy_merge_range)
        {
          /* The merge target doesn't need anything merged. */
          target_start = revision2;
        }
      else if (children_with_mergeinfo && children_with_mergeinfo->nelts)
        {
          /* Get the merge target's svn_client__merge_path_t, which is always
             the first in the array due to depth first sorting requirement,
             see 'THE CHILDREN_WITH_MERGEINFO ARRAY'. */
          svn_client__merge_path_t *child =
            APR_ARRAY_IDX(children_with_mergeinfo, 0,
                          svn_client__merge_path_t *);
          if (child->remaining_ranges->nelts)
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

          if (!child || child->absent)
            continue;

          /* Find this child's nearest wc ancestor with mergeinfo. */
          parent_index = find_nearest_ancestor(children_with_mergeinfo,
                                               FALSE, child->path);
          parent = APR_ARRAY_IDX(children_with_mergeinfo, parent_index,
                                 svn_client__merge_path_t *);

          /* Note if the child's parent is the merge target. */
          nearest_parent_is_target =
            (strcmp(parent->path, target_wcpath) == 0) ? TRUE : FALSE;

          /* If a subtree needs the same range applied as it's nearest parent
             with mergeinfo, then we don't need to describe the subtree
             separately. */
          if (child->remaining_ranges->nelts)
            {
              range = APR_ARRAY_IDX(child->remaining_ranges, 0,
                                    svn_merge_range_t *);
              if (parent->remaining_ranges->nelts)
                {
                   svn_merge_range_t *parent_range =
                    APR_ARRAY_IDX(parent->remaining_ranges, 0,
                                  svn_merge_range_t *);
                   svn_merge_range_t *child_range =
                    APR_ARRAY_IDX(child->remaining_ranges, 0,
                                  svn_merge_range_t *);
                  if (parent_range->start == child_range->start)
                    continue; /* Same as parent. */
                }
            }
          else /* child->remaining_ranges->nelts == 0*/
            {
              /* If both the subtree and its parent need no ranges applied
                 consider that as the "same ranges" and don't describe
                 the subtree.  If the subtree's parent is the merge target,
                 then the parent can have a dummy range; this is still
                 the same as no remaining ranges. */
              if (parent->remaining_ranges->nelts == 0
                  || (nearest_parent_is_target
                      && merge_b->target_has_dummy_merge_range))
                continue; /* Same as parent. */
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

  /* Sleep to ensure timestamp integrity. */
  svn_sleep_for_timestamps();

  return SVN_NO_ERROR;
}

/* Return the most inclusive range start revision across all the
   remaining ranges in CHILDREN_WITH_MERGEINFO.  If there are no
   remaining ranges, return SVN_INVALID_REVNUM.  Skip no-op ranges
   on the target (they are probably dummies). */
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

/* Return the youngest qualifying end revision across the first of
   each child in CHILDREN_WITH_MERGEINFO's remaining ranges.  If
   nothing qualifies, return SVN_INVALID_REVNUM. */
static svn_revnum_t
get_youngest_end_rev(apr_array_header_t *children_with_mergeinfo,
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

   Remove the first remaining revision range for each child in
   CHILDREN_WITH_MERGEINFO *iff* that child was already merged.  END_REV is the
   ending revision of the most recently merged range, i.e. the same end_rev
   passed to drive_merge_report_editor() by do_directory_merge().  If a
   range is removed from a child's remaining_ranges array, allocate the new
   remaining_ranges array in POOL.

   ### TODO: We should have remaining_ranges in reverse order to avoid
   ### recreating and reallocationg the remaining_ranges every time we want
   ### to remove the first range.  If the ranges were reversed we could simply
   ### pop the last element in the array. */
static void
remove_first_range_from_remaining_ranges(svn_revnum_t end_rev,
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
          if (first_range->end == end_rev)
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

/* Marks 'inheritable' RANGE to TARGET_WCPATH by wiping off the
   corresponding 'non-inheritable' RANGE from TARGET_MERGEINFO for the
   merge source REL_PATH.  It does such marking only for same URLs
   from same Repository, not a dry run, target having existing
   mergeinfo(TARGET_MERGEINFO) and target being part of
   CHILDREN_WITH_MERGEINFO. */
static svn_error_t *
mark_mergeinfo_as_inheritable_for_a_range(
                                   svn_mergeinfo_t target_mergeinfo,
                                   svn_boolean_t same_urls,
                                   svn_merge_range_t *range,
                                   const char *rel_path,
                                   const char *target_wcpath,
                                   svn_wc_adm_access_t *adm_access,
                                   merge_cmd_baton_t *merge_b,
                                   apr_array_header_t *children_with_mergeinfo,
                                   int target_index, apr_pool_t *pool)
{
  /* Check if we need to make non-inheritable ranges inheritable. */
  if (target_mergeinfo && same_urls
      && !merge_b->dry_run
      && merge_b->same_repos
      && target_index >= 0)
    {
      svn_client__merge_path_t *merge_path =
        APR_ARRAY_IDX(children_with_mergeinfo,
                      target_index, svn_client__merge_path_t *);

      /* If a path has no missing children, has non-inheritable ranges,
         *and* those non-inheritable ranges intersect with the merge being
         performed (i.e. this is a repeat merge where a previously missing
         child is now present) then those non-inheritable ranges are made
         inheritable. */
      if (merge_path
          && merge_path->has_noninheritable && !merge_path->missing_child)
        {
          svn_boolean_t is_equal;
          apr_hash_t *merges;
          apr_hash_t *inheritable_merges = apr_hash_make(pool);
          apr_array_header_t *inheritable_ranges =
            apr_array_make(pool, 1, sizeof(svn_merge_range_t *));

          APR_ARRAY_PUSH(inheritable_ranges, svn_merge_range_t *) = range;
          apr_hash_set(inheritable_merges, rel_path, APR_HASH_KEY_STRING,
                       inheritable_ranges);

          /* Try to remove any non-inheritable ranges bound by the merge
             being performed. */
          SVN_ERR(svn_mergeinfo_inheritable(&merges, target_mergeinfo,
                                            rel_path, range->start,
                                            range->end, pool));
          /* If any non-inheritable ranges were removed put them back as
             inheritable ranges. */
          SVN_ERR(svn_mergeinfo__equals(&is_equal, merges, target_mergeinfo,
                                        FALSE, pool));
          if (!is_equal)
            {
              SVN_ERR(svn_mergeinfo_merge(merges, inheritable_merges, pool));
              SVN_ERR(svn_client__record_wc_mergeinfo(target_wcpath, merges,
                                                      adm_access, pool));
            }
        }
    }
  return SVN_NO_ERROR;
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
  apr_file_t *fp;
  svn_stream_t *stream;

  SVN_ERR(svn_io_open_unique_file2(&fp, filename,
                                   wc_target, ".tmp",
                                   svn_io_file_del_none, pool));
  stream = svn_stream_from_aprfile2(fp, FALSE, pool);
  SVN_ERR(svn_ra_get_file(ra_session, "", rev,
                          stream, NULL, props, pool));
  SVN_ERR(svn_stream_close(stream));

  return SVN_NO_ERROR;
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
};


/* svn_wc_entry_callbacks2_t found_entry() callback for get_mergeinfo_paths.

   Given PATH, its corresponding ENTRY, and WB, where WB is the WALK_BATON
   of type "struct get_mergeinfo_walk_baton *":  If PATH is switched,
   has explicit working svn:mergeinfo from a corresponding merge source, is
   missing a child due to a sparse checkout, is absent from disk, or is
   scheduled for deletion, then create a svn_client__merge_path_t *
   representing *PATH, allocated in WB->CHILDREN_WITH_MERGEINFO->POOL, and
   push it onto the WB->CHILDREN_WITH_MERGEINFO array. */
static svn_error_t *
get_mergeinfo_walk_cb(const char *path,
                      const svn_wc_entry_t *entry,
                      void *walk_baton,
                      apr_pool_t *pool)
{
  struct get_mergeinfo_walk_baton *wb = walk_baton;
  const svn_string_t *propval;
  svn_mergeinfo_t mergehash;
  svn_boolean_t switched = FALSE;
  svn_boolean_t has_mergeinfo_from_merge_src = FALSE;
  svn_boolean_t path_is_merge_target =
    !svn_path_compare_paths(path, wb->merge_target_path);
  const char *parent_path = svn_path_dirname(path, pool);

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
      /* We always include the merge target regardless of its mergeinfo.
         So we don't need to check that PATH's mergeinfo corresponds to
         the merge source. */
      if (propval && !path_is_merge_target)
        {
          svn_stringbuf_t *merge_src_child_path =
            svn_stringbuf_create(wb->merge_src_canon_path, pool);

          /* When the merge target is '' or '.' WB->MERGE_TARGET_PATH is
             an empty string and PATH will always be relative.  In this case
             we can safely combine WB->MERGE_SRC_CANON_PATH and PATH with
             svn_path_add_compent() which will supply the missing '/' separator.

             Otherwise WB->MERGE_TARGET_PATH is relative or absolute and
             we remove the common root component between WB->MERGE_TARGET_PATH
             and PATH from PATH before combining it with
             WB->MERGE_SRC_CANON_PATH.  The +1 is required because if we are
             here that means WB->MERGE_TARGET_PATH is a proper ancestor of
             PATH and we must skip the path separator -- svn_path_add_compent()
             will add missing separators, but won't remove existing ones -- to
             avoid a merge_src_child_path with "//" in it. */
          if (strlen(wb->merge_target_path))
            svn_path_add_component(merge_src_child_path,
                                   path + strlen(wb->merge_target_path) + 1);
          else
            svn_path_add_component(merge_src_child_path,
                                   path);
          SVN_ERR(svn_mergeinfo_parse(&mergehash, propval->data, pool));
          if (propval->len == 0 /* empty mergeinfo */
              || apr_hash_get(mergehash, merge_src_child_path->data,
                              APR_HASH_KEY_STRING))
            {
              /* The easy way: PATH already has mergeinfo
                 from this source or has empty mergeinfo... */
              has_mergeinfo_from_merge_src = TRUE;
            }
          else
            {
              /* ...the slightly harder way: See if PATH exists in the
                 merge source at the revisions being merged. If it doesn't
                 exist there is no way this subtree can be affected by the
                 merge so we can safely leave it, and its mergeinfo, alone. */
              svn_error_t *err;
              const char *original_ra_url = NULL;
              const char *mergeinfo_url =
                svn_path_join(wb->source_root_url,
                              /* Skip leading '/' or join won't work. */
                              ++(merge_src_child_path->data),
                              pool);
              svn_opt_revision_t *start_revision, *end_revision;
              const char *start_url, *end_url;
              svn_opt_revision_t peg_rev, rev1_opt, rev2_opt;

              peg_rev.value.number = wb->revision1 < wb->revision2
                ? wb->revision2 : wb->revision1;
              peg_rev.kind = svn_opt_revision_number;

              rev1_opt.kind = svn_opt_revision_number;
              rev1_opt.value.number = wb->revision1;

              rev2_opt.kind = svn_opt_revision_number;
              rev2_opt.value.number = wb->revision2;

              /* Instead of passing NULL to svn_client__repos_locations() and
                 causing another session to open, reparent WB->RA_SESSION
                 and use that. */
              SVN_ERR(svn_client__ensure_ra_session_url(&original_ra_url,
                                                        wb->ra_session,
                                                        mergeinfo_url, pool));

              /* Does PATH exist in the merge source? */
              err = svn_client__repos_locations(&start_url, &start_revision,
                                                &end_url, &end_revision,
                                                wb->ra_session, mergeinfo_url,
                                                &peg_rev, &rev1_opt, &rev2_opt,
                                                wb->ctx, pool);
              if (err)
                {
                  /* We might see any of these errors depending on the RA
                     access method, but they all mean that PATH doesn't exist
                     in the merge source.

                     ### TODO: Make svn_client__repos_locations() more
                     ###       consistent in the error it returns(?)
                     */
                  if (err->apr_err == SVN_ERR_FS_NOT_FOUND
                      || err->apr_err == SVN_ERR_CLIENT_UNRELATED_RESOURCES)
                    svn_error_clear(err);
                  else
                    return err;
                 }
              else /* PATH does exist in the merge source*/
                {
                  has_mergeinfo_from_merge_src = TRUE;
                }

              /* Reparent the session to its original URL if necessary. */
              if (original_ra_url)
                {
                  SVN_ERR(svn_ra_reparent(wb->ra_session,
                                          original_ra_url, pool));
                }
            } /* the slightly harder way */
        }
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
      || has_mergeinfo_from_merge_src
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
        apr_pcalloc(wb->children_with_mergeinfo->pool, sizeof(*child));
      child->path = apr_pstrdup(wb->children_with_mergeinfo->pool, path);
      child->missing_child = (entry->depth == svn_depth_empty
                              || entry->depth == svn_depth_files
                              || ((wb->depth == svn_depth_immediates) &&
                                  (entry->kind == svn_node_dir) &&
                                  (strcmp(parent_path,
                                          wb->merge_target_path) == 0)))
                              ? TRUE : FALSE;
      child->switched = switched;
      child->absent = entry->absent;
      child->scheduled_for_deletion =
        entry->schedule == svn_wc_schedule_delete ? TRUE : FALSE;
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

/* Helper for get_mergeinfo_paths()

   CHILDREN_WITH_MERGEINFO is a depth first sorted array filled with
   svn_client__merge_path_t *.  Starting at the element in
   CHILDREN_WITH_MERGEINFO located at START_INDEX look for that
   element's child/parent (as indicated by LOOKING_FOR_CHILD) named
   PATH. If the child/parent is found, set *CHILD_OR_PARENT to that
   element and return the index at which if was found.  If the
   child/parent is not found set *CHILD_OR_PARENT to NULL and return
   the index at which it should be inserted. */
static int
find_child_or_parent(apr_array_header_t *children_with_mergeinfo,
                     svn_client__merge_path_t **child_or_parent,
                     const char *path,
                     svn_boolean_t looking_for_child,
                     int start_index,
                     apr_pool_t *pool)
{
  int j = 0;
  *child_or_parent = NULL;

  /* If possible, search forwards in the depth first sorted array
     to find a child PATH or backwards to find a parent PATH. */
  if (start_index >= 0 && start_index < children_with_mergeinfo->nelts)
    {
      for (j = looking_for_child ? start_index + 1 : start_index;
           looking_for_child ? j < children_with_mergeinfo->nelts : j >= 0;
           j = looking_for_child ? j + 1 : j - 1)
        {
          /* If this potential child is neither the child we are looking for
             or another one of PARENT's children then CHILD_PATH doesn't
             exist in CHILDREN_WITH_MERGEINFO. */
          svn_client__merge_path_t *potential_child_or_parent =
            APR_ARRAY_IDX(children_with_mergeinfo, j,
                          svn_client__merge_path_t *);
          int cmp = svn_path_compare_paths(path,
                                           potential_child_or_parent->path);
          if (cmp == 0)
            {
              /* Found child or parent. */
              *child_or_parent = potential_child_or_parent;
              break;
            }
          else if ((looking_for_child && cmp < 0)
                   || (!looking_for_child && cmp > 0))
            {
              /* PATH doesn't exist, but found where it should be inserted. */
              if (!looking_for_child)
                j++;
              break;
            }
          else if (!looking_for_child && j == 0)
            {
              /* Looking for a parent but are at start of the array so we know
                 where to insert the parent. */
              break;
            }
          /* else we are looking for a child but found one of its
             siblings...keep looking. */
        }
    }
  return j;
}

/* Helper for get_mergeinfo_paths()

   CHILDREN_WITH_MERGEINFO is a depth first sorted array filled with
   svn_client__merge_path_t *.  Insert INSERT_ELEMENT into the
   CHILDREN_WITH_MERGEINFO array at index INSERT_INDEX. */
static void
insert_child_to_merge(apr_array_header_t *children_with_mergeinfo,
                      svn_client__merge_path_t *insert_element,
                      int insert_index)
{
  if (insert_index == children_with_mergeinfo->nelts)
    {
      APR_ARRAY_PUSH(children_with_mergeinfo,
                     svn_client__merge_path_t *) = insert_element;
    }
  else
    {
      /* Copy the last element of CHILDREN_WITH_MERGEINFO and add it to the
         end of the array. */
      int j;
      svn_client__merge_path_t *curr =
        APR_ARRAY_IDX(children_with_mergeinfo,
                      children_with_mergeinfo->nelts - 1,
                      svn_client__merge_path_t *);
      svn_client__merge_path_t *curr_copy =
        apr_palloc(children_with_mergeinfo->pool, sizeof(*curr_copy));

      *curr_copy = *curr;
      APR_ARRAY_PUSH(children_with_mergeinfo,
                     svn_client__merge_path_t *) = curr_copy;

      /* Move all elements from INSERT_INDEX to the end of the array forward
         one spot then insert the new element. */
      for (j = children_with_mergeinfo->nelts - 2; j >= insert_index; j--)
        {
          svn_client__merge_path_t *prev;
          curr = APR_ARRAY_IDX(children_with_mergeinfo, j,
                               svn_client__merge_path_t *);
          if (j == insert_index)
            *curr = *insert_element;
          else
            {
              prev = APR_ARRAY_IDX(children_with_mergeinfo, j - 1,
                                   svn_client__merge_path_t *);
              *curr = *prev;
            }
        }
    }
}

/* Helper for get_mergeinfo_paths()'s qsort() call. */
static int
compare_merge_path_t_as_paths(const void *a,
                              const void *b)
{
  svn_client__merge_path_t *child1 = *((svn_client__merge_path_t * const *) a);
  svn_client__merge_path_t *child2 = *((svn_client__merge_path_t * const *) b);

  return svn_path_compare_paths(child1->path, child2->path);
}

/* Helper for get_mergeinfo_paths().  If CHILD->PATH is switched or
   absent then make sure its parent is marked as missing a child.
   Start looking up for parent from *CURR_INDEX in
   CHILDREN_WITH_MERGEINFO.  Create the parent and insert it into
   CHILDREN_WITH_MERGEINFO if necessary (and increment *CURR_INDEX
   so that caller don't process the inserted element).  Also ensure
   that CHILD->PATH's siblings which are not already present in
   CHILDREN_WITH_MERGEINFO are also added to the array. Use POOL for
   all temporary allocations. */
static svn_error_t *
insert_parent_and_sibs_of_sw_absent_del_entry(
                                   apr_array_header_t *children_with_mergeinfo,
                                   merge_cmd_baton_t *merge_cmd_baton,
                                   int *curr_index,
                                   svn_client__merge_path_t *child,
                                   svn_wc_adm_access_t *adm_access,
                                   apr_pool_t *pool)
{
  svn_client__merge_path_t *parent;
  const char *parent_path = svn_path_dirname(child->path, pool);
  apr_hash_t *entries;
  apr_hash_index_t *hi;
  svn_wc_adm_access_t *parent_access;
  int insert_index, parent_index;

  if (!(child->absent
          || (child->switched
              && strcmp(merge_cmd_baton->target, child->path) != 0)))
    return SVN_NO_ERROR;

  parent_index = find_child_or_parent(children_with_mergeinfo, &parent,
                                      parent_path, FALSE, *curr_index, pool);
  if (parent)
    {
      parent->missing_child = TRUE;
    }
  else
    {
      /* Create a new element to insert into CHILDREN_WITH_MERGEINFO. */
      parent = apr_pcalloc(children_with_mergeinfo->pool, sizeof(*parent));
      parent->path = apr_pstrdup(children_with_mergeinfo->pool, parent_path);
      parent->missing_child = TRUE;
      /* Insert PARENT into CHILDREN_WITH_MERGEINFO. */
      insert_child_to_merge(children_with_mergeinfo, parent, parent_index);
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
      insert_index = find_child_or_parent(children_with_mergeinfo,
                                          &sibling_of_missing, child_path,
                                          TRUE, parent_index, pool);
      /* Create the missing child and insert it into CHILDREN_WITH_MERGEINFO.*/
      if (!sibling_of_missing)
        {
          sibling_of_missing = apr_pcalloc(children_with_mergeinfo->pool,
                                           sizeof(*sibling_of_missing));
          sibling_of_missing->path = apr_pstrdup(children_with_mergeinfo->pool,
                                                 child_path);
          insert_child_to_merge(children_with_mergeinfo, sibling_of_missing,
                                insert_index);
        }
    }
  return SVN_NO_ERROR;
}

/* Helper for do_directory_merge()

   Perform a depth first walk of the working copy tree rooted at
   MERGE_CMD_BATON->TARGET (with the corresponding ENTRY).  Create an
   svn_client__merge_path_t * for any path which meets one or more of the
   following criteria:

     1) Path has working svn:mergeinfo from corresponding merge source or
        has empty mergeinfo.
     2) Path is switched.
     3) Path has no mergeinfo of its own but its parent has mergeinfo with
        non-inheritable ranges (in this case the function will actually set
        override mergeinfo on the path if this isn't a dry-run and the merge
        is between differences in the same repository).
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

   Store the svn_client__merge_path_t *'s in *CHILDREN_WITH_MERGEINFO in
   depth-first order based on the svn_client__merge_path_t *s path member as
   sorted by svn_path_compare_paths().

   Note: Since the walk is rooted at MERGE_CMD_BATON->TARGET, the latter is
   guaranteed to be in *CHILDREN_WITH_MERGEINFO and due to the depth-first
   ordering it is guaranteed to be the first element in
   *CHILDREN_WITH_MERGEINFO.

   Cascade MERGE_SRC_CANON_PATH. */
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
                    svn_ra_session_t *ra_session,
                    svn_wc_adm_access_t *adm_access,
                    svn_client_ctx_t *ctx,
                    svn_depth_t depth,
                    apr_pool_t *pool)
{
  int i;
  apr_pool_t *iterpool;
  static const svn_wc_entry_callbacks2_t walk_callbacks =
    { get_mergeinfo_walk_cb, get_mergeinfo_error_handler };
  struct get_mergeinfo_walk_baton wb =
    { adm_access, children_with_mergeinfo,
      merge_src_canon_path, merge_cmd_baton->target, source_root_url,
      url1, url2, revision1, revision2,
      depth, ra_session, ctx };

  /* Cover cases 1), 2), 6), and 7) by walking the WC to get all paths which
     have mergeinfo and/or are switched or are absent from disk or is the
     target of the merge. */
  SVN_ERR(svn_wc_walk_entries3(merge_cmd_baton->target, adm_access,
                               &walk_callbacks, &wb, depth, TRUE,
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

  iterpool = svn_pool_create(pool);
  for (i = 0; i < children_with_mergeinfo->nelts; i++)
    {
      int insert_index;
      svn_client__merge_path_t *child =
        APR_ARRAY_IDX(children_with_mergeinfo, i, svn_client__merge_path_t *);
      svn_pool_clear(iterpool);

      /* Case 3) Where merging to a path with a switched child the path gets
         non-inheritable mergeinfo for the merge range performed and the child
         gets its own set of mergeinfo.  If the switched child later
         "returns", e.g. a switched path is unswitched, the child may not have
         any explicit mergeinfo.  If the initial merge is repeated we don't
         want to repeat the merge for the path, but we do want to repeat it
         for the previously switched child.  To ensure this we check if all
         of CHILD's non-missing children have explicit mergeinfo (they should
         already be present in CHILDREN_WITH_MERGEINFO if they do).  If not,
         add the children without mergeinfo to CHILDREN_WITH_MERGEINFO so
         do_directory_merge() will merge them independently.

         But that's not enough!  Since do_directory_merge() performs
         the merges on the paths in CHILDREN_WITH_MERGEINFO in a depth first
         manner it will merge the previously switched path's parent first.  As
         part of this merge it will update the parent's previously
         non-inheritable mergeinfo and make it inheritable (since it notices
         the path has no missing children), then when
         do_directory_merge() finally merges the previously missing
         child it needs to get mergeinfo from the child's nearest ancestor,
         but since do_directory_merge() already tweaked that
         mergeinfo, removing the non-inheritable flag, it appears that the
         child already has been merged to.  To prevent this we set override
         mergeinfo on the child now, before any merging is done, so it has
         explicit mergeinfo that reflects only CHILD's inheritable
         mergeinfo. */

      if (child->has_noninheritable)
        {
          apr_hash_t *entries;
          apr_hash_index_t *hi;
          svn_wc_adm_access_t *child_access;
          SVN_ERR(svn_wc_adm_probe_try3(&child_access, adm_access,
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

              /* Does this child already exist in CHILDREN_WITH_MERGEINFO?  If
                 not, create it and insert it into CHILDREN_WITH_MERGEINFO and
                 set override mergeinfo on it. */
              child_path = svn_path_join(child->path, key, iterpool);
              insert_index = find_child_or_parent(children_with_mergeinfo,
                                                  &child_of_noninheritable,
                                                  child_path, TRUE, i,
                                                  iterpool);
              if (!child_of_noninheritable)
                {
                  child_of_noninheritable =
                    apr_pcalloc(children_with_mergeinfo->pool,
                                sizeof(*child_of_noninheritable));
                  child_of_noninheritable->path =
                    apr_pstrdup(children_with_mergeinfo->pool, child_path);
                  insert_child_to_merge(children_with_mergeinfo,
                                        child_of_noninheritable,
                                        insert_index);
                  if (!merge_cmd_baton->dry_run
                      && merge_cmd_baton->same_repos)
                    {
                      svn_boolean_t inherited;
                      svn_mergeinfo_t mergeinfo;
                      SVN_ERR(svn_client__get_wc_mergeinfo
                              (&mergeinfo, &inherited, FALSE,
                               svn_mergeinfo_nearest_ancestor,
                               entry, child_of_noninheritable->path,
                               merge_cmd_baton->target, NULL, adm_access,
                               merge_cmd_baton->ctx, iterpool));

                      SVN_ERR(svn_client__record_wc_mergeinfo(
                        child_of_noninheritable->path, mergeinfo, adm_access,
                        iterpool));
                    }
                }
            }
        }
      /* Case 4 and 5 are handled by the following function. */
      SVN_ERR(insert_parent_and_sibs_of_sw_absent_del_entry(
        children_with_mergeinfo, merge_cmd_baton, &i, child,
        adm_access, iterpool));
    } /* i < children_with_mergeinfo->nelts */

  svn_pool_destroy(iterpool);
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

  /* Our list of changed revisions should be in youngest-to-oldest order. */
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
      svn_merge_range_t *range = APR_ARRAY_IDX(ranges, i, svn_merge_range_t *);
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
              APR_ARRAY_PUSH(operative_ranges, svn_merge_range_t *) = range;
              break;
            }
        }
    }
  *operative_ranges_p = operative_ranges;
  return SVN_NO_ERROR;
}


/*-----------------------------------------------------------------------*/

/*** Merge Source Normalization ***/

typedef struct merge_source_t
{
  /* "left" side URL and revision (inclusive iff youngest) */
  const char *url1;
  svn_revnum_t rev1;

  /* "right" side URL and revision (inclusive iff youngest) */
  const char *url2;
  svn_revnum_t rev2;

} merge_source_t;

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
      merge_source->url1 = svn_path_join(source_root_url,
                                         svn_path_uri_encode(path1,
                                                             pool), pool);
      merge_source->url2 = svn_path_join(source_root_url,
                                         svn_path_uri_encode(segment->path,
                                                             pool), pool);
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

              segment_url = svn_path_url_add_component(source_root_url,
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

   Note: MERGE_B->RA_SESSION1 must be associated with URL1 and
   MERGE_B->RA_SESSION2 with URL2.
*/
static svn_error_t *
do_file_merge(const char *url1,
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
  svn_mergeinfo_t implicit_mergeinfo;

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
      svn_client__merge_path_t *merge_target =
        apr_pcalloc(pool, sizeof(*merge_target));

      SVN_ERR(svn_ra_get_repos_root2(merge_b->ra_session1,
                                     &source_root_url, pool));
      SVN_ERR(svn_client__path_relative_to_root(&mergeinfo_path, primary_url,
                                                source_root_url, TRUE, NULL,
                                                NULL, pool));

      /* Fetch mergeinfo (temporarily reparenting ra_session1 to
         working copy target URL). */
      SVN_ERR(svn_ra_reparent(merge_b->ra_session1, entry->url, pool));
      SVN_ERR(get_full_mergeinfo(&target_mergeinfo, &implicit_mergeinfo,
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
                                             implicit_mergeinfo, FALSE,
                                             merge_b->ra_session1,
                                             entry, ctx, pool));
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
                                         target_wcpath,
                                         tmpfile1,
                                         tmpfile2,
                                         mimetype1, mimetype2,
                                         props1,
                                         merge_b));
              single_file_merge_notify(notify_b, target_wcpath,
                                       svn_wc_notify_update_delete, text_state,
                                       svn_wc_notify_state_unknown, n,
                                       &header_sent, subpool);

              /* ...plus add... */
              SVN_ERR(merge_file_added(adm_access,
                                       &text_state, &prop_state,
                                       target_wcpath,
                                       tmpfile1,
                                       tmpfile2,
                                       r->start,
                                       r->end,
                                       mimetype1, mimetype2,
                                       propchanges, props1,
                                       merge_b));
              single_file_merge_notify(notify_b, target_wcpath,
                                       svn_wc_notify_update_add, text_state,
                                       prop_state, n, &header_sent, subpool);
              /* ... equals replace. */
            }
          else
            {
              SVN_ERR(merge_file_changed(adm_access,
                                         &text_state, &prop_state,
                                         target_wcpath,
                                         tmpfile1,
                                         tmpfile2,
                                         r->start,
                                         r->end,
                                         mimetype1, mimetype2,
                                         propchanges, props1,
                                         merge_b));
              single_file_merge_notify(notify_b, target_wcpath,
                                       svn_wc_notify_update_update, text_state,
                                       prop_state, n, &header_sent, subpool);
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
      apr_hash_t *merges;
      apr_array_header_t *filtered_rangelist;

      /* Filter any ranges from TARGET_WCPATH's own history, there is no
         need to record this explicitly in mergeinfo, it is already part
         of TARGET_WCPATH's natural history (implicit mergeinfo). */
      SVN_ERR(filter_natural_history_from_mergeinfo(&filtered_rangelist,
                                                    mergeinfo_path,
                                                    implicit_mergeinfo,
                                                    &range, subpool));

      if (filtered_rangelist->nelts)
        {
          SVN_ERR(determine_merges_performed(&merges, target_wcpath,
                                             filtered_rangelist,
                                             svn_depth_infinity,
                                             adm_access, notify_b,
                                             merge_b, subpool));
          /* If merge target has indirect mergeinfo set it before
             recording the first merge range. */
          if (indirect)
            SVN_ERR(svn_client__record_wc_mergeinfo(target_wcpath,
                                                    target_mergeinfo,
                                                    adm_access, subpool));

          SVN_ERR(update_wc_mergeinfo(target_wcpath, entry, mergeinfo_path,
                                      merges, is_rollback, adm_access,
                                      ctx, subpool));
        }
    }

  svn_pool_destroy(subpool);

  /* Sleep to ensure timestamp integrity. */
  svn_sleep_for_timestamps();

  /* If our multi-pass merge terminated early due to conflicts, return
     that fact as an error. */
  if (conflicted_range)
    return make_merge_conflict_error(target_wcpath, conflicted_range, pool);

  return SVN_NO_ERROR;
}


/* Perform a merge of changes between URL1@REVISION1 and
   URL2@REVISION2, applied to the children of PARENT_ENTRY.  URL1,
   URL2, and PARENT_ENTRY all represent directories -- for the single
   file case, the caller should use do_file_merge().

   If MERGE_B->sources_ancestral is set, then URL1@REVISION1 must be a
   historical ancestor of URL2@REVISION2, or vice-versa (see
   `MERGEINFO MERGE SOURCE NORMALIZATION' for more requirements around
   the values of URL1, REVISION1, URL2, and REVISION2 in this case).

   Handle DEPTH as documented for svn_client_merge3().

   CHILDREN_WITH_MERGEINFO may contain child paths (svn_client__merge_path_t *)
   which are switched or which have mergeinfo which differs from that of the
   merge target root (ignored if empty or NULL).  CHILDREN_WITH_MERGEINFO
   list should have entries sorted in depth first order as mandated by the
   reporter API. Because of this, we drive the diff editor in such a way that
   it avoids merging child paths when a merge is driven for their parent path.

   CHILDREN_WITH_MERGEINFO may contain TARGET_WCPATH (which may be
   MERGE_B->TARGET), in that case TARGET_INDEX is the array index for
   TARGET_WCPATH, otherwise it should be set to a negative value.

   NOTE: This is a wrapper around drive_merge_report_editor() which
   handles the complexities inherent to situations where a given
   directory's children may have intersecting merges (because they
   meet one or more of the criteria described in get_mergeinfo_paths()).
*/
static svn_error_t *
do_directory_merge(const char *url1,
                   svn_revnum_t revision1,
                   const char *url2,
                   svn_revnum_t revision2,
                   const svn_wc_entry_t *parent_entry,
                   svn_wc_adm_access_t *adm_access,
                   svn_depth_t depth,
                   notification_receiver_baton_t *notify_b,
                   merge_cmd_baton_t *merge_b,
                   apr_pool_t *pool)
{
  svn_error_t *err = SVN_NO_ERROR;
  apr_array_header_t *children_with_mergeinfo;
  int merge_target_len = strlen(merge_b->target);
  int i;
  svn_merge_range_t range;
  svn_ra_session_t *ra_session;
  svn_boolean_t inheritable;
  apr_pool_t *iterpool;
  const char *target_wcpath = svn_wc_adm_access_path(adm_access);
  svn_client__merge_path_t *target_merge_path;
  svn_boolean_t is_rollback = (revision1 > revision2);
  const char *primary_url = is_rollback ? url1 : url2;
  const char *source_root_url, *mergeinfo_path;
  svn_boolean_t honor_mergeinfo, record_mergeinfo;
  svn_boolean_t same_urls = (strcmp(url1, url2) == 0);

  mergeinfo_behavior(&honor_mergeinfo, &record_mergeinfo, merge_b);

  /* Initialize CHILDREN_WITH_MERGEINFO. */
  children_with_mergeinfo =
    apr_array_make(pool, 0, sizeof(svn_client__merge_path_t *));
  notify_b->children_with_mergeinfo = children_with_mergeinfo;

  /* If our merge sources aren't related to each other, or don't come
     from the same repository as our target, mergeinfo is meaningless
     and we can skip right to the business of merging changes!  We'll
     just drop a dummy item into CHILDREN_WITH_MERGEINFO if the merge
     sources are related.  */
  if (! (merge_b->sources_ancestral && merge_b->same_repos))
    {
      if (merge_b->sources_ancestral)
        {
          svn_client__merge_path_t *item = apr_pcalloc(pool, sizeof(*item));
          svn_merge_range_t *itemrange = apr_pcalloc(pool, sizeof(*itemrange));
          apr_array_header_t *remaining_ranges =
            apr_array_make(pool, 1, sizeof(svn_merge_range_t *));

          itemrange->start = revision1;
          itemrange->end = revision2;
          itemrange->inheritable = TRUE;
          APR_ARRAY_PUSH(remaining_ranges, svn_merge_range_t *) = itemrange;

          item->path = apr_pstrdup(pool, target_wcpath);
          item->remaining_ranges = remaining_ranges;
          APR_ARRAY_PUSH(children_with_mergeinfo,
                         svn_client__merge_path_t *) = item;
        }
      return drive_merge_report_editor(target_wcpath,
                                       url1, revision1, url2, revision2,
                                       NULL, is_rollback, depth, notify_b,
                                       adm_access, &merge_callbacks,
                                       merge_b, pool);
    }

  /*** If we get here, we're dealing with related sources from the
       same repository as the target -- merge tracking might be
       happenin'! ***/

  /* Point our RA_SESSION to the URL of our youngest merge source side. */
  ra_session = is_rollback ? merge_b->ra_session1 : merge_b->ra_session2;

  /* Fill CHILDREN_WITH_MERGEINFO with child paths (const
     svn_client__merge_path_t *) which might have intersecting merges
     because they meet one or more of the criteria described in
     get_mergeinfo_paths(). Here the paths are arranged in a depth
     first order. */
  SVN_ERR(svn_ra_get_repos_root2(ra_session, &source_root_url, pool));
  SVN_ERR(svn_client__path_relative_to_root(&mergeinfo_path, primary_url,
                                            source_root_url, TRUE, NULL,
                                            NULL, pool));
  SVN_ERR(get_mergeinfo_paths(children_with_mergeinfo, merge_b,
                              mergeinfo_path, parent_entry, source_root_url,
                              url1, url2, revision1, revision2,
                              ra_session, adm_access,
                              merge_b->ctx, depth, pool));

  /* The first item from the CHILDREN_WITH_MERGEINFO is the target
     thanks to depth-first ordering. */
  target_merge_path = APR_ARRAY_IDX(children_with_mergeinfo, 0,
                                    svn_client__merge_path_t *);
  merge_b->target_missing_child = target_merge_path->missing_child;
  inheritable = ((! merge_b->target_missing_child)
                 && ((depth == svn_depth_infinity)
                     || (depth == svn_depth_immediates)));

  /* If we are honoring mergeinfo, then for each item in
     CHILDREN_WITH_MERGEINFO, we need to calculate what needs to be
     merged, and then merge it.  Otherwise, we just merge what we were
     asked to merge across the whole tree.  */
  SVN_ERR(populate_remaining_ranges(children_with_mergeinfo,
                                    source_root_url,
                                    url1, revision1, url2, revision2,
                                    inheritable, honor_mergeinfo,
                                    ra_session, mergeinfo_path,
                                    adm_access, merge_b));

  /* Always start with a range which describes our most inclusive merge. */
  range.start = revision1;
  range.end = revision2;
  range.inheritable = inheritable;

  if (honor_mergeinfo && !merge_b->record_only)
    {
      svn_revnum_t start_rev, end_rev;

      /* From the remaining ranges of each item in
         CHILDREN_WITH_MERGEINFO, pick the most inclusive start and
         end revisions. */
      start_rev = get_most_inclusive_start_rev(children_with_mergeinfo,
                                               is_rollback);

      /* Is there anything to merge? */
      if (SVN_IS_VALID_REVNUM(start_rev))
        {
          range.start = start_rev;
          end_rev = get_youngest_end_rev(children_with_mergeinfo, is_rollback);

          /* Build a range which describes our most inclusive merge. */
          range.start = start_rev;

          /* While END_REV is valid, do the following:

             1. slice each remaining ranges around this 'end_rev'.
             2. starting with START_REV, call
                drive_merge_report_editor() on MERGE_B->target for
                start_rev:end_rev.
             3. remove the first item from each remaining range.
             4. set START_REV=END_REV and pick the next END_REV.
             5. lather, rinse, repeat.
          */
          iterpool = svn_pool_create(pool);
          while (end_rev != SVN_INVALID_REVNUM)
            {
              svn_revnum_t next_end_rev;
              const char *real_url1 = url1, *real_url2 = url2;
              const char *old_sess1_url = NULL, *old_sess2_url = NULL;

              svn_pool_clear(iterpool);

              /* Use persistent pool while playing with remaining_ranges. */
              slice_remaining_ranges(children_with_mergeinfo, is_rollback,
                                     end_rev, pool);
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
              SVN_ERR(drive_merge_report_editor(merge_b->target,
                                                real_url1, start_rev,
                                                real_url2, end_rev,
                                                children_with_mergeinfo,
                                                is_rollback,
                                                depth, notify_b, adm_access,
                                                &merge_callbacks, merge_b,
                                                iterpool));
              if (old_sess1_url)
                SVN_ERR(svn_ra_reparent(merge_b->ra_session1,
                                        old_sess1_url, iterpool));
              if (old_sess2_url)
                SVN_ERR(svn_ra_reparent(merge_b->ra_session2,
                                        old_sess2_url, iterpool));

              /* Prepare for the next iteration (if any). */
              remove_first_range_from_remaining_ranges(
                end_rev, children_with_mergeinfo, pool);
              next_end_rev = get_youngest_end_rev(children_with_mergeinfo,
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
              start_rev = get_most_inclusive_start_rev(children_with_mergeinfo,
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
                                            NULL, is_rollback,
                                            depth, notify_b, adm_access,
                                            &merge_callbacks, merge_b,
                                            pool));
        }
    }

  /* Record mergeinfo where appropriate.*/
  iterpool = svn_pool_create(pool);
  if (record_mergeinfo)
    {
      apr_array_header_t *filtered_rangelist;
      svn_client__merge_path_t *merge_target =
        APR_ARRAY_IDX(children_with_mergeinfo, 0, svn_client__merge_path_t *);

      /* Update the WC mergeinfo here to account for our new
         merges, minus any unresolved conflicts and skips. */
      apr_hash_t *merges;

      /* Remove absent children at or under TARGET_WCPATH from
         NOTIFY_B->SKIPPED_PATHS and CHILDREN_WITH_MERGEINFO before we
         calculate the merges performed. */
      remove_absent_children(merge_b->target,
                             children_with_mergeinfo, notify_b);

      /* Filter any ranges from MERGE_B->TARGET's own history, there is no
         need to record this explicitly in mergeinfo, it is already part of
         MERGE_B->TARGET's natural history (implicit mergeinfo). */
      SVN_ERR(filter_natural_history_from_mergeinfo(
        &filtered_rangelist, mergeinfo_path, merge_target->implicit_mergeinfo,
        &range, iterpool));

      if (filtered_rangelist->nelts)
        {
          SVN_ERR(determine_merges_performed(&merges, merge_b->target,
                                             filtered_rangelist, depth,
                                             adm_access, notify_b,
                                             merge_b, iterpool));
          SVN_ERR(update_wc_mergeinfo(merge_b->target, parent_entry,
                                      mergeinfo_path, merges,
                                      is_rollback, adm_access, merge_b->ctx,
                                      iterpool));
        }

      for (i = 0; i < children_with_mergeinfo->nelts; i++)
        {
          const char *child_repos_path;
          const char *child_merge_src_canon_path;
          const svn_wc_entry_t *child_entry;
          apr_array_header_t *child_merge_rangelist;
          apr_hash_t *child_merges;
          svn_client__merge_path_t *child =
                         APR_ARRAY_IDX(children_with_mergeinfo, i,
                                       svn_client__merge_path_t *);
          if (!child || child->absent)
            continue;

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

          child_merges = apr_hash_make(iterpool);

          /* As we did above for the merge target, filter any ranges from
             each child's natural history before setting mergeinfo. */
          SVN_ERR(filter_natural_history_from_mergeinfo(
            &child_merge_rangelist, child_merge_src_canon_path,
            child->implicit_mergeinfo, &range, iterpool));

          if (child_merge_rangelist->nelts == 0)
            continue;
          else
            {
              int j;
              for (j = 0; j < child_merge_rangelist->nelts; j++)
                {
                  svn_merge_range_t *rng =
                    APR_ARRAY_IDX(child_merge_rangelist, j,
                                  svn_merge_range_t *);
                  if (child_entry->kind == svn_node_file)
                    rng->inheritable = TRUE;
                  else
                    rng->inheritable = (!(child->missing_child)
                                        && (depth == svn_depth_infinity
                                     || depth == svn_depth_immediates));
                }
            }
          apr_hash_set(child_merges, child->path, APR_HASH_KEY_STRING,
                       child_merge_rangelist);
          /* If merge target has indirect mergeinfo set it before
             recording the first merge range. */
          if (child->indirect_mergeinfo)
            {
              SVN_ERR(svn_client__record_wc_mergeinfo(
                                               child->path,
                                               child->pre_merge_mergeinfo,
                                               adm_access,
                                               iterpool));
            }
          SVN_ERR(update_wc_mergeinfo(child->path, child_entry,
                                      child_merge_src_canon_path,
                                      child_merges, is_rollback,
                                      adm_access, merge_b->ctx, iterpool));

          SVN_ERR(mark_mergeinfo_as_inheritable_for_a_range(
                                                   child->pre_merge_mergeinfo,
                                                   TRUE,
                                                   &range,
                                                   child_merge_src_canon_path,
                                                   child->path,
                                                   adm_access,
                                                   merge_b,
                                                   children_with_mergeinfo,
                                                   i, iterpool));

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
                      parent = APR_ARRAY_IDX(children_with_mergeinfo, j,
                                             svn_client__merge_path_t *);
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
        } /* (i = 0; i < children_with_mergeinfo->nelts; i++) */

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
      if (notify_b->added_paths)
        {
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
                                      svn_path_dirname(added_path, iterpool),
                                      adm_access, iterpool));
              if (added_path_parent_propval
                  && strstr(added_path_parent_propval->data,
                            SVN_MERGEINFO_NONINHERITABLE_STR))
                {
                  /* ADDED_PATH's immediate parent has non-inheritable
                     mergeinfo. */
                  svn_boolean_t inherited;
                  svn_merge_range_t *rng;
                  svn_mergeinfo_t merge_mergeinfo, added_path_mergeinfo;
                  apr_array_header_t *rangelist;
                  const svn_wc_entry_t *entry;
                  const char *common_ancestor_path =
                    svn_path_get_longest_ancestor(added_path,
                                                  target_merge_path->path,
                                                  iterpool);
                  const char *relative_added_path =
                    added_path + strlen(common_ancestor_path) + 1;
                  SVN_ERR(svn_wc__entry_versioned(&entry, added_path,
                                                  adm_access, FALSE,
                                                  iterpool));

                  /* Calculate the mergeinfo resulting from this merge. */
                  merge_mergeinfo = apr_hash_make(iterpool);
                  rangelist = apr_array_make(iterpool, 1,
                                             sizeof(svn_merge_range_t *));
                  rng = svn_merge_range_dup(&range, iterpool);
                  if (entry->kind == svn_node_file)
                    rng->inheritable = TRUE;
                  else
                    rng->inheritable =
                      (!(depth == svn_depth_infinity
                         || depth == svn_depth_immediates));
                  APR_ARRAY_PUSH(rangelist, svn_merge_range_t *) = rng;
                  apr_hash_set(merge_mergeinfo,
                               svn_path_join(mergeinfo_path,
                                             relative_added_path,
                                             iterpool),
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
    } /* (!merge_b->dry_run && merge_b->same_repos) */

  svn_pool_destroy(iterpool);
  return err;
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

   FORCE, DRY_RUN, RECORD_ONLY, IGNORE_ANCESTRY, DEPTH, MERGE_OPTIONS,
   and CTX are as described in the docstring for svn_client_merge_peg3().
*/
static svn_error_t *
do_merge(apr_array_header_t *merge_sources,
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
         svn_depth_t depth,
         const apr_array_header_t *merge_options,
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
    depth = target_entry->depth;

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
  merge_cmd_baton.target = target;
  merge_cmd_baton.pool = subpool;
  merge_cmd_baton.merge_options = merge_options;
  merge_cmd_baton.diff3_cmd = diff3_cmd;

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
      svn_ra_session_t *ra_session1, *ra_session2;

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

      /* Establish RA sessions to our URLs. */
      SVN_ERR(svn_client__open_ra_session_internal(&ra_session1, url1,
                                                   NULL, NULL, NULL,
                                                   FALSE, TRUE, ctx, subpool));
      SVN_ERR(svn_client__open_ra_session_internal(&ra_session2, url2,
                                                   NULL, NULL, NULL,
                                                   FALSE, TRUE, ctx, subpool));

      /* Populate the portions of the merge context baton that need to
         be reset for each merge source iteration. */
      merge_cmd_baton.url = url2;
      merge_cmd_baton.added_path = NULL;
      merge_cmd_baton.add_necessitated_merge = FALSE;
      merge_cmd_baton.dry_run_deletions =
        dry_run ? apr_hash_make(subpool) : NULL;
      merge_cmd_baton.conflicted_paths = NULL;
      merge_cmd_baton.target_has_dummy_merge_range = FALSE;
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
          SVN_ERR(do_file_merge(url1, rev1, url2, rev2, target,
                                sources_related, adm_access,
                                &notify_baton, &merge_cmd_baton, subpool));
        }
      else if (target_entry->kind == svn_node_dir)
        {
          SVN_ERR(do_directory_merge(url1, rev1, url2, rev2, target_entry,
                                     adm_access, depth, &notify_baton,
                                     &merge_cmd_baton, subpool));
        }

      /* The final mergeinfo on TARGET_WCPATH may itself elide. */
      if (! dry_run)
        SVN_ERR(svn_client__elide_mergeinfo(target, NULL, target_entry,
                                            adm_access, ctx, subpool));
    }

  svn_pool_destroy(subpool);
  return SVN_NO_ERROR;
}

/* Perform a two-URL merge between URLs which are related, but neither
   is a direct ancestor of the other.  This first does a real two-URL
   merge (unless this is record-only), followed by record-only merges
   to represent the changed mergeinfo.

   The merge is between URL1@REV1 (in RA_SESSION1) and URL2@REV2 (in
   RA_SESSION2); YC_REV is their youngest common ancestor.
   SOURCE_REPOS_ROOT and WC_REPOS_ROOT are the repository roots of the
   source URL and the target working copy.  ENTRY is the wc entry for
   TARGET_WCPATH.  Other arguments are as in all of the public merge
   APIs.
 */
static svn_error_t *
merge_cousins_and_supplement_mergeinfo(const char *target_wcpath,
                                       const svn_wc_entry_t *entry,
                                       svn_wc_adm_access_t *adm_access,
                                       svn_ra_session_t *ra_session,
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
                                       svn_client_ctx_t *ctx,
                                       apr_pool_t *pool)
{
  svn_opt_revision_range_t *range;
  apr_array_header_t *remove_sources, *add_sources, *ranges;
  svn_opt_revision_t peg_revision;
  const char *old_url;
  svn_boolean_t same_repos =
    (strcmp(wc_repos_root, source_repos_root) == 0) ? TRUE : FALSE;

  peg_revision.kind = svn_opt_revision_number;
  SVN_ERR(svn_ra_get_session_url(ra_session, &old_url, pool));

  range = apr_pcalloc(pool, sizeof(*range));
  range->start.kind = svn_opt_revision_number;
  range->start.value.number = rev1;
  range->end.kind = svn_opt_revision_number;
  range->end.value.number = yc_rev;
  ranges = apr_array_make(pool, 2, sizeof(svn_opt_revision_range_t *));
  APR_ARRAY_PUSH(ranges, svn_opt_revision_range_t *) = range;
  peg_revision.value.number = rev1;
  SVN_ERR(svn_ra_reparent(ra_session, URL1, pool));
  SVN_ERR(normalize_merge_sources(&remove_sources, URL1, URL1,
                                  source_repos_root, &peg_revision,
                                  ranges, ra_session, ctx, pool));

  range = apr_pcalloc(pool, sizeof(*range));
  range->start.kind = svn_opt_revision_number;
  range->start.value.number = yc_rev;
  range->end.kind = svn_opt_revision_number;
  range->end.value.number = rev2;
  ranges = apr_array_make(pool, 2, sizeof(svn_opt_revision_range_t *));
  APR_ARRAY_PUSH(ranges, svn_opt_revision_range_t *) = range;
  peg_revision.value.number = rev2;
  SVN_ERR(svn_ra_reparent(ra_session, URL2, pool));
  SVN_ERR(normalize_merge_sources(&add_sources, URL2, URL2,
                                  source_repos_root, &peg_revision,
                                  ranges, ra_session, ctx, pool));

  SVN_ERR(svn_ra_reparent(ra_session, old_url, pool));

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
      SVN_ERR(do_merge(faux_sources, target_wcpath, entry, adm_access,
                       FALSE, TRUE, same_repos,
                       ignore_ancestry, force, dry_run,
                       FALSE, depth, merge_options, ctx, pool));
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
     ancestral, so we force ancestral=TRUE here.) */
  if (same_repos)
    {
      SVN_ERR(do_merge(add_sources, target_wcpath, entry,
                       adm_access, TRUE, TRUE, same_repos,
                       ignore_ancestry, force, dry_run,
                       TRUE, depth, merge_options, ctx, pool));
      SVN_ERR(do_merge(remove_sources, target_wcpath, entry,
                       adm_access, TRUE, TRUE, same_repos,
                       ignore_ancestry, force, dry_run,
                       TRUE, depth, merge_options, ctx, pool));
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
  const char *yc_path = NULL;
  svn_revnum_t yc_rev = SVN_INVALID_REVNUM;
  apr_pool_t *sesspool;
  svn_boolean_t same_repos;

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

  /* Get the repository root URL from one of our sessions (the other
     doesn't matter -- if it ain't the same, other stuff would fall
     over later).  */
  SVN_ERR(svn_ra_get_repos_root2(ra_session1, &source_repos_root, sesspool));

  /* Do our working copy and sources come from the same repository? */
  same_repos = (strcmp(source_repos_root, wc_repos_root) == 0) ? TRUE : FALSE;

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
      yc_path = svn_path_join(source_repos_root,
                              svn_path_uri_encode(yc_path, pool), pool);

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
          SVN_ERR(merge_cousins_and_supplement_mergeinfo(target_wcpath, entry,
                                                         adm_access,
                                                         ra_session1,
                                                         URL1, rev1,
                                                         URL2, rev2,
                                                         yc_rev,
                                                         source_repos_root,
                                                         wc_repos_root,
                                                         depth,
                                                         ignore_ancestry, force,
                                                         record_only, dry_run,
                                                         merge_options, ctx,
                                                         pool));

          /* Close our temporary RA sessions (this could've happened
             after the second call to normalize_merge_sources() inside
             the merge_cousins_and_supplement_mergeinfo() routine). */
          svn_pool_destroy(sesspool);

          SVN_ERR(svn_wc_adm_close(adm_access));
          return SVN_NO_ERROR;
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

  SVN_ERR(do_merge(merge_sources, target_wcpath, entry, adm_access,
                   ancestral, related, same_repos,
                   ignore_ancestry, force, dry_run,
                   record_only, depth, merge_options, ctx, pool));

  SVN_ERR(svn_wc_adm_close(adm_access));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_merge2(const char *source1,
                  const svn_opt_revision_t *revision1,
                  const char *source2,
                  const svn_opt_revision_t *revision2,
                  const char *target_wcpath,
                  svn_boolean_t recurse,
                  svn_boolean_t ignore_ancestry,
                  svn_boolean_t force,
                  svn_boolean_t dry_run,
                  const apr_array_header_t *merge_options,
                  svn_client_ctx_t *ctx,
                  apr_pool_t *pool)
{
  return svn_client_merge3(source1, revision1, source2, revision2,
                           target_wcpath,
                           SVN_DEPTH_INFINITY_OR_FILES(recurse),
                           ignore_ancestry, force, FALSE, dry_run,
                           merge_options, ctx, pool);
}

svn_error_t *
svn_client_merge(const char *source1,
                 const svn_opt_revision_t *revision1,
                 const char *source2,
                 const svn_opt_revision_t *revision2,
                 const char *target_wcpath,
                 svn_boolean_t recurse,
                 svn_boolean_t ignore_ancestry,
                 svn_boolean_t force,
                 svn_boolean_t dry_run,
                 svn_client_ctx_t *ctx,
                 apr_pool_t *pool)
{
  return svn_client_merge2(source1, revision1, source2, revision2,
                           target_wcpath, recurse, ignore_ancestry, force,
                           dry_run, NULL, ctx, pool);
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

/* Given a "mergeinfo" hash HISTORY_AS_MERGEINFO representing revision
   ranges from a merge target that are not represented in the merge
   source, check (using RA_SESSION, which is pointed at the repository
   root) that all of the ranges in the hash are "phantoms": that is,
   their corresponding path did not change in any of their revisions.
   Raises SVN_ERR_CLIENT_NOT_READY_TO_MERGE if any are not phantoms.
   Temporary allocations in POOL.
 */
static svn_error_t *
ensure_all_missing_ranges_are_phantoms(svn_ra_session_t *ra_session,
                                       svn_mergeinfo_t history_as_mergeinfo,
                                       apr_pool_t *pool)
{
  apr_hash_index_t *hi;
  apr_pool_t *iterpool = svn_pool_create(pool);

  for (hi = apr_hash_first(pool, history_as_mergeinfo); hi;
       hi = apr_hash_next(hi))
    {
      const void *key;
      void *value;
      const char *path;
      apr_array_header_t *rangelist;
      int i;

      apr_hash_this(hi, &key, NULL, &value);
      path = key;
      rangelist = value;

      /* mergeinfo hashes contain paths that start with slashes;
         ra APIs take paths without slashes. */
      SVN_ERR_ASSERT(*path);
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

          SVN_ERR(svn_ra_stat(ra_session,
                              path,
                              range->end,
                              &dirent,
                              iterpool));

          if (svn_merge_range_contains_rev(range, dirent->created_rev))
            {
              const char *full_url;

              svn_pool_destroy(iterpool);

              SVN_ERR(svn_ra_get_session_url(ra_session, &full_url, pool));
              full_url = svn_path_url_add_component(full_url, path, pool);
              return svn_error_createf(SVN_ERR_CLIENT_NOT_READY_TO_MERGE, NULL,
                                       _("At least one revision (r%ld) "
                                         "not yet merged from '%s'"),
                                       dirent->created_rev, full_url);
            }
        }
    }

  svn_pool_destroy(iterpool);
  return SVN_NO_ERROR;
}


/* Return a new catalog in *CATALOG_P equal to CATALOG, but containing
   only source ranges from the segments in SEGMENTS (whose elements
   are of type svn_location_segment_t *).  The returned values are
   (deeply) allocated in POOL. */
static svn_error_t *
remove_irrelevant_ranges(svn_mergeinfo_catalog_t *catalog_p,
                         svn_mergeinfo_catalog_t catalog,
                         apr_array_header_t *segments,
                         const char *source_repos_rel_path,
                         apr_pool_t *pool)
{
  apr_hash_index_t *hi;
  svn_mergeinfo_catalog_t new_catalog = apr_hash_make(pool);
  svn_mergeinfo_t history_as_mergeinfo;

  SVN_ERR(svn_client__mergeinfo_from_segments(&history_as_mergeinfo,
                                              segments,
                                              pool));

  for (hi = apr_hash_first(pool, catalog);
       hi;
       hi = apr_hash_next(hi))
    {
      const void *key;
      void *val;
      const char *path;
      svn_mergeinfo_t mergeinfo, filtered_mergeinfo;

      apr_hash_this(hi, &key, NULL, &val);
      path = key;
      mergeinfo = val;

      SVN_ERR(svn_mergeinfo_intersect(&filtered_mergeinfo,
                                      mergeinfo,
                                      history_as_mergeinfo,
                                      pool));
      /* Don't put an empty hash in the catalog for the root of the source.
         An empty hash represents empty mergeinfo as opposed to *no*
         mergeinfo.  We need to do this for the root of the source because
         if calculate_left_hand_side() sees *any* mergeinfo left on the source
         it will interpret this as coming from the target, when in fact
         nothing has been merged from the target to the source.  This
         ultimately causes calculate_left_hand_side() to follow the wrong code
         path and abort.  We only do this for the root of the source however,
         subtrees with mergeinfo cannot lose all mergeinfo as that would mean
         they are inheriting from the root, which isn't the case.  See here
         for more details:
         http://subversion.tigris.org/servlets/ReadMsg?list=dev&msgNo=136908
         */
      if (apr_hash_count(filtered_mergeinfo)
          || strcmp(source_repos_rel_path, path) != 0)
        apr_hash_set(new_catalog,
                     apr_pstrdup(pool, path),
                     APR_HASH_KEY_STRING,
                     filtered_mergeinfo);
    }

  *catalog_p = new_catalog;
  return SVN_NO_ERROR;
}

/* TODO(reint): Document.

   RA_SESSION must be opened at the repository root. */
static svn_error_t *
calculate_left_hand_side(const char **url_left,
                         svn_revnum_t *rev_left,
                         svn_mergeinfo_t *source_mergeinfo_p,
                         const char *target_repos_rel_path,
                         svn_revnum_t target_rev,
                         const char *source_repos_rel_path,
                         const char *source_repos_root,
                         svn_revnum_t source_rev,
                         svn_ra_session_t *ra_session,
                         svn_client_ctx_t *ctx,
                         apr_pool_t *pool)
{
  apr_array_header_t *segments; /* array of (svn_location_segment_t *) */
  svn_boolean_t have_mergeinfo_for_source = FALSE,
    have_mergeinfo_for_descendants = FALSE;
  svn_mergeinfo_catalog_t mergeinfo_catalog;
  apr_array_header_t *source_repos_rel_path_as_array
    = apr_array_make(pool, 1, sizeof(const char *));
  apr_pool_t *subpool = svn_pool_create(pool);

  /* Get the history (segments) for the target */
  SVN_ERR(svn_client__repos_location_segments(&segments,
                                              ra_session,
                                              target_repos_rel_path,
                                              target_rev, target_rev,
                                              SVN_INVALID_REVNUM,
                                              ctx, subpool));

  /* Get the mergeinfo from the source, including its descendants. */
  APR_ARRAY_PUSH(source_repos_rel_path_as_array, const char *)
    = source_repos_rel_path;
  SVN_ERR(svn_ra_get_mergeinfo(ra_session, &mergeinfo_catalog,
                               source_repos_rel_path_as_array, source_rev,
                               svn_mergeinfo_inherited, TRUE, subpool));
  if (!mergeinfo_catalog)
    mergeinfo_catalog = apr_hash_make(subpool);

  /* Filter mergeinfo_catalog so that all of the ranges come from
     the target's history */
  SVN_ERR(remove_irrelevant_ranges(&mergeinfo_catalog,
                                   mergeinfo_catalog,
                                   segments,
                                   source_repos_rel_path,
                                   subpool));

  /* Elide! */
  SVN_ERR(svn_client__elide_mergeinfo_catalog(mergeinfo_catalog, subpool));

  /* See which case we fall into: */
  /* TODO(reint): make sure we look things up with keys that start
     with slash.  This may not be as simple as it sounds, since
     source_repos_rel_path is also used as the component argument to
     (e.g.) svn_path_join(), which expects the component to *not*
     start with a slash (or at least, it will behave in a way we
     probably don't want if the component *does* start with slash).
  */
  if (apr_hash_get(mergeinfo_catalog, source_repos_rel_path,
                   APR_HASH_KEY_STRING))
    have_mergeinfo_for_source = TRUE;
  if (apr_hash_count(mergeinfo_catalog) > 1 ||
      (! have_mergeinfo_for_source && apr_hash_count(mergeinfo_catalog) == 1))
    have_mergeinfo_for_descendants = TRUE;

  if (! have_mergeinfo_for_source && ! have_mergeinfo_for_descendants)
    {
      /* TODO(reint): Make sure we're not fetching location segments
         over and over. */
      /* We never merged to the source.  Just return the branch point. */
      const char *yc_ancestor_path,
        *source_url = svn_path_join(source_repos_root, source_repos_rel_path,
                                    subpool),
        *target_url = svn_path_join(source_repos_root, target_repos_rel_path,
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
      *url_left = svn_path_join(source_repos_root, yc_ancestor_path, pool);
      *source_mergeinfo_p = apr_hash_make(pool);

      svn_pool_destroy(subpool);
      return SVN_NO_ERROR;
    }
  else if (! have_mergeinfo_for_descendants)
    {
      /* Easy case: return the last path/rev in the mergeinfo. */
      svn_mergeinfo_t source_mergeinfo = apr_hash_get(mergeinfo_catalog,
                                                      source_repos_rel_path,
                                                      APR_HASH_KEY_STRING);
      apr_pool_t *iterpool = svn_pool_create(subpool);
      int i;
      for (i = segments->nelts - 1; i >= 0; i--)
        {
          svn_location_segment_t *segment
            = APR_ARRAY_IDX(segments, i, svn_location_segment_t *);
          apr_array_header_t *rangelist;

          svn_pool_clear(iterpool);

          /* Ignore gaps in history. */
          if (!segment->path)
            continue;

          rangelist = apr_hash_get(source_mergeinfo,
                                   apr_pstrcat(iterpool, "/", segment->path,
                                               NULL),
                                   APR_HASH_KEY_STRING);
          if (rangelist != NULL && rangelist->nelts > 0)
            {
              svn_merge_range_t *last_range
                = APR_ARRAY_IDX(rangelist, rangelist->nelts - 1,
                                svn_merge_range_t *);
              *rev_left = last_range->end;
              *url_left = svn_path_join(source_repos_root, segment->path,
                                        pool);
              *source_mergeinfo_p = svn_mergeinfo_dup(source_mergeinfo, pool);
              svn_pool_destroy(iterpool);
              svn_pool_destroy(subpool);
              return SVN_NO_ERROR;
            }
        }
      /* We only got here because we had mergeinfo for the source; if
         there were no segments, then our logic was wrong. */
      SVN_ERR_MALFUNCTION();
    }
  else
    {
      const char *full_url;
      SVN_ERR(svn_ra_get_session_url(ra_session, &full_url, pool));
      full_url = svn_path_url_add_component(full_url, source_repos_rel_path,
                                            pool);
      return svn_error_createf(SVN_ERR_CLIENT_NOT_READY_TO_MERGE, NULL,
                               "Cannot reintegrate from '%s' yet:\n"
                               "Some revisions have been merged under it "
                               "that have not been merged\n"
                               "into the reintegration target; "
                               "merge them first, then retry.", full_url);
      /* TODO(reint): It would be even better to print out mergeinfo_catalog
         here.  Is there a helper function for that? */
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
  svn_ra_session_t *ra_session;
  const char *source_repos_rel_path, *target_repos_rel_path;
  const char *yc_ancestor_path;
  svn_revnum_t yc_ancestor_rev;
  const char *url1, *url2;
  svn_revnum_t rev1, rev2;
  svn_mergeinfo_t source_mergeinfo;


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
                                     &working_revision, adm_access, ctx, pool));

  /* Open an RA session to our source URL, and determine its root URL. */
  SVN_ERR(svn_client__open_ra_session_internal(&ra_session, wc_repos_root,
                                               NULL, NULL, NULL,
                                               FALSE, FALSE, ctx, pool));
  SVN_ERR(svn_ra_get_repos_root2(ra_session, &source_repos_root, pool));

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

  SVN_ERR(svn_client__path_relative_to_root(&source_repos_rel_path,
                                            url2, NULL, FALSE,
                                            ra_session, NULL, pool));
  SVN_ERR(svn_client__path_relative_to_root(&target_repos_rel_path,
                                            target_wcpath, wc_repos_root,
                                            FALSE, ra_session, NULL, pool));

  SVN_ERR(svn_client__get_revision_number(&rev2, NULL,
                                          ra_session, peg_revision,
                                          source_repos_rel_path, pool));

  SVN_ERR(calculate_left_hand_side(&url1, &rev1, &source_mergeinfo,
                                   target_repos_rel_path,
                                   rev1,
                                   source_repos_rel_path,
                                   source_repos_root,
                                   rev2,
                                   ra_session,
                                   ctx,
                                   pool));

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
      svn_opt_revision_t opt_rev1;
      svn_mergeinfo_t target_mergeinfo, deleted_mergeinfo, added_mergeinfo;

      opt_rev1.kind = svn_opt_revision_number;
      opt_rev1.value.number = rev1;
      SVN_ERR(svn_client__get_history_as_mergeinfo(&target_mergeinfo,
                                                   entry->url,
                                                   &opt_rev1,
                                                   rev1,
                                                   yc_ancestor_rev + 1,
                                                   NULL, adm_access, ctx,
                                                   pool));

      /* ### TODO(reint): Consider CONSIDER_INHERITANCE parameter... */
      SVN_ERR(svn_mergeinfo_diff(&deleted_mergeinfo, &added_mergeinfo,
                                 target_mergeinfo, source_mergeinfo, FALSE,
                                 pool));

      SVN_ERR(ensure_all_missing_ranges_are_phantoms(ra_session,
                                                     deleted_mergeinfo, pool));
    }

  /* Left side: trunk@youngest-trunk-rev-merged-to-branch-at-specified-peg-rev
   * Right side: branch@specified-peg-revision */

  /* Do the real merge! */
  /* ### TODO(reint): Make sure that one isn't the same line ancestor
     ### of the other (what's erroneously referred to as "ancestrally
     ### related" in this source file).  We can merge to trunk without
     ### implementing this. */
  SVN_ERR(merge_cousins_and_supplement_mergeinfo(target_wcpath, entry,
                                                 adm_access, ra_session,
                                                 url1, rev1, url2, rev2,
                                                 yc_ancestor_rev,
                                                 source_repos_root,
                                                 wc_repos_root,
                                                 svn_depth_infinity,
                                                 FALSE,
                                                 FALSE, FALSE, dry_run,
                                                 merge_options, ctx, pool));

  /* Shutdown the administrative session. */
  SVN_ERR(svn_wc_adm_close(adm_access));

  return SVN_NO_ERROR;
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

  /* We're done with our little RA session. */
  svn_pool_destroy(sesspool);

  /* Do the real merge!  (We say with confidence that our merge
     sources are both ancestral and related.) */
  SVN_ERR(do_merge(merge_sources, target_wcpath, entry, adm_access,
                   TRUE, TRUE,
                   (strcmp(wc_repos_root, source_repos_root) == 0),
                   ignore_ancestry, force, dry_run, record_only, depth,
                   merge_options, ctx, pool));

  /* Shutdown the administrative session. */
  SVN_ERR(svn_wc_adm_close(adm_access));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_client_merge_peg2(const char *source,
                      const svn_opt_revision_t *revision1,
                      const svn_opt_revision_t *revision2,
                      const svn_opt_revision_t *peg_revision,
                      const char *target_wcpath,
                      svn_boolean_t recurse,
                      svn_boolean_t ignore_ancestry,
                      svn_boolean_t force,
                      svn_boolean_t dry_run,
                      const apr_array_header_t *merge_options,
                      svn_client_ctx_t *ctx,
                      apr_pool_t *pool)
{
  svn_opt_revision_range_t range;
  apr_array_header_t *ranges_to_merge =
    apr_array_make(pool, 1, sizeof(svn_opt_revision_range_t *));

  range.start = *revision1;
  range.end = *revision2;
  APR_ARRAY_PUSH(ranges_to_merge, svn_opt_revision_range_t *) = &range;
  return svn_client_merge_peg3(source, ranges_to_merge,
                               peg_revision,
                               target_wcpath,
                               SVN_DEPTH_INFINITY_OR_FILES(recurse),
                               ignore_ancestry, force, FALSE, dry_run,
                               merge_options, ctx, pool);
}

svn_error_t *
svn_client_merge_peg(const char *source,
                     const svn_opt_revision_t *revision1,
                     const svn_opt_revision_t *revision2,
                     const svn_opt_revision_t *peg_revision,
                     const char *target_wcpath,
                     svn_boolean_t recurse,
                     svn_boolean_t ignore_ancestry,
                     svn_boolean_t force,
                     svn_boolean_t dry_run,
                     svn_client_ctx_t *ctx,
                     apr_pool_t *pool)
{
  return svn_client_merge_peg2(source, revision1, revision2, peg_revision,
                               target_wcpath, recurse, ignore_ancestry, force,
                               dry_run, NULL, ctx, pool);
}
