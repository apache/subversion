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



/*** Includes. ***/

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
#include "client.h"
#include "mergeinfo.h"
#include <assert.h>

#include "private/svn_wc_private.h"
#include "private/svn_mergeinfo_private.h"
#include "private/svn_client_private.h"

#include "svn_private_config.h"

/*-----------------------------------------------------------------------*/

/*** Utilities. ***/

/* Sanity check -- ensure that we have valid revisions to look at. */
#define ENSURE_VALID_REVISION_KINDS(rev1_kind, rev2_kind) \
  if ((rev1_kind == svn_opt_revision_unspecified) \
      || (rev2_kind == svn_opt_revision_unspecified)) \
    { \
      return svn_error_create \
        (SVN_ERR_CLIENT_BAD_REVISION, NULL, \
         _("Not all required revisions are specified")); \
    }


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

/*** Callbacks for 'svn merge', invoked by the repos-diff editor. ***/


struct merge_cmd_baton {
  svn_boolean_t force;
  svn_boolean_t record_only;          /* Whether to only record mergeinfo. */
  svn_boolean_t dry_run;
  svn_boolean_t same_repos;           /* Whether the merge source repository
                                         is the same repository as the
                                         target.  Defaults to FALSE if DRY_RUN
                                         is TRUE.*/
  const char *added_path;             /* Set to the dir path whenever the
                                         dir is added as a child of a
                                         versioned dir (dry-run only) */
  const char *target;                 /* Working copy target of merge */
  const char *url;                    /* The second URL in the merge */
  const char *path;                   /* The wc path of the second target, this
                                         can be NULL if we don't have one. */
  const svn_opt_revision_t *revision; /* Revision of second URL in the merge */
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

  /* The diff3_cmd in ctx->config, if any, else null.  We could just
     extract this as needed, but since more than one caller uses it,
     we just set it up when this baton is created. */
  const char *diff3_cmd;
  const apr_array_header_t *merge_options;

  apr_pool_t *pool;
};

apr_hash_t *
svn_client__dry_run_deletions(void *merge_cmd_baton)
{
  struct merge_cmd_baton *merge_b = merge_cmd_baton;
  return merge_b->dry_run_deletions;
}

/* Used to avoid spurious notifications (e.g. conflicts) from a merge
   attempt into an existing target which would have been deleted if we
   weren't in dry_run mode (issue #2584).  Assumes that WCPATH is
   still versioned (e.g. has an associated entry). */
static APR_INLINE svn_boolean_t
dry_run_deleted_p(struct merge_cmd_baton *merge_b, const char *wcpath)
{
  return (merge_b->dry_run &&
          apr_hash_get(merge_b->dry_run_deletions, wcpath,
                       APR_HASH_KEY_STRING) != NULL);
}


/* A svn_wc_diff_callbacks2_t function.  Used for both file and directory
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
  struct merge_cmd_baton *merge_b = baton;
  apr_pool_t *subpool = svn_pool_create(merge_b->pool);
  svn_error_t *err;

  SVN_ERR(svn_categorize_props(propchanges, NULL, NULL, &props, subpool));

  /* We only want to merge "regular" version properties:  by
     definition, 'svn merge' shouldn't touch any data within .svn/  */
  if (props->nelts)
    {
      /* svn_wc_merge_props() requires ADM_ACCESS to be the access for
         the parent of PATH. Since the advent of merge tracking,
         discover_and_merge_children() may call this (indirectly) with
         the access for the merge_b->target instead (issue #2781).
         So, if we have the wrong access, get the right one. */
      if (svn_path_compare_paths(svn_wc_adm_access_path(adm_access),
                                 path) != 0)
        SVN_ERR(svn_wc_adm_probe_try3(&adm_access, adm_access, path,
                                      TRUE, -1, merge_b->ctx->cancel_func,
                                      merge_b->ctx->cancel_baton, subpool));

      err = svn_wc_merge_props(state, path, adm_access, original_props, props,
                               FALSE, merge_b->dry_run, subpool);
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

/* A svn_wc_diff_callbacks2_t function. */
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
  struct merge_cmd_baton *merge_b = baton;
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

      /* Special case:  if a binary file isn't locally modified, and is
         exactly identical to the 'left' side of the merge, then don't
         allow svn_wc_merge to produce a conflict.  Instead, just
         overwrite the working file with the 'right' side of the merge.

         Alternately, if the 'left' side of the merge doesn't exist in
         the repository, and the 'right' side of the merge is
         identical to the WC, pretend we did the merge (a no-op). */
      if ((! has_local_mods)
          && ((mimetype1 && svn_mime_type_is_binary(mimetype1))
              || (mimetype2 && svn_mime_type_is_binary(mimetype2))))
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
          SVN_ERR(svn_wc_merge3(&merge_outcome,
                                older, yours, mine, adm_access,
                                left_label, right_label, target_label,
                                merge_b->dry_run, merge_b->diff3_cmd,
                                merge_b->merge_options, prop_changes,
                                merge_b->ctx->conflict_func,
                                merge_b->ctx->conflict_baton,
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

/* A svn_wc_diff_callbacks2_t function. */
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
  struct merge_cmd_baton *merge_b = baton;
  apr_pool_t *subpool = svn_pool_create(merge_b->pool);
  svn_node_kind_t kind;
  const char *copyfrom_url;
  const char *child;
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
            child = svn_path_is_child(merge_b->target, mine, subpool);
            if (child != NULL)
              copyfrom_url = svn_path_url_add_component(merge_b->url, child,
                                                        subpool);
            else
              copyfrom_url = merge_b->url;
            SVN_ERR(check_scheme_match(adm_access, copyfrom_url));

            /* Since 'mine' doesn't exist, and this is
               'merge_file_added', I hope it's safe to assume that
               'older' is empty, and 'yours' is the full file.  Merely
               copying 'yours' to 'mine', isn't enough; we need to get
               the whole text-base and props installed too, just as if
               we had called 'svn cp wc wc'. */

            SVN_ERR(svn_wc_add_repos_file2(mine, adm_access,
                                           yours, NULL,
                                           new_props, NULL,
                                           copyfrom_url,
                                           rev2,
                                           subpool));
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

/* A svn_wc_diff_callbacks2_t function. */
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
  struct merge_cmd_baton *merge_b = baton;
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

  SVN_ERR(svn_io_check_path(mine, &kind, subpool));
  switch (kind)
    {
    case svn_node_file:
      svn_path_split(mine, &parent_path, NULL, subpool);
      SVN_ERR(svn_wc_adm_retrieve(&parent_access, adm_access, parent_path,
                                  subpool));
      /* Passing NULL for the notify_func and notify_baton because
         repos_diff.c:delete_entry() will do it for us. */
      err = svn_client__wc_delete(mine, parent_access, merge_b->force,
                                  merge_b->dry_run, FALSE, NULL, NULL,
                                  merge_b->ctx, subpool);
      if (err && state)
        {
          *state = svn_wc_notify_state_obstructed;
          svn_error_clear(err);
        }
      else if (state)
        {
          *state = svn_wc_notify_state_changed;
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

/* A svn_wc_diff_callbacks2_t function. */
static svn_error_t *
merge_dir_added(svn_wc_adm_access_t *adm_access,
                svn_wc_notify_state_t *state,
                const char *path,
                svn_revnum_t rev,
                void *baton)
{
  struct merge_cmd_baton *merge_b = baton;
  apr_pool_t *subpool = svn_pool_create(merge_b->pool);
  svn_node_kind_t kind;
  const svn_wc_entry_t *entry;
  const char *copyfrom_url, *child;

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
  assert(child != NULL);
  copyfrom_url = svn_path_url_add_component(merge_b->url, child, subpool);
  SVN_ERR(check_scheme_match(adm_access, copyfrom_url));

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
      if (! merge_b->dry_run)
        {
          SVN_ERR(svn_io_make_dir_recursively(path, subpool));
          SVN_ERR(svn_wc_add2(path, adm_access,
                              copyfrom_url, rev,
                              merge_b->ctx->cancel_func,
                              merge_b->ctx->cancel_baton,
                              NULL, NULL, /* don't pass notification func! */
                              subpool));

        }
      if (merge_b->dry_run)
        merge_b->added_path = apr_pstrdup(merge_b->pool, path);
      if (state)
        *state = svn_wc_notify_state_changed;
      break;
    case svn_node_dir:
      /* Adding an unversioned directory doesn't destroy data */
      SVN_ERR(svn_wc_entry(&entry, path, adm_access, TRUE, subpool));
      if (! entry || entry->schedule == svn_wc_schedule_delete)
        {
          if (!merge_b->dry_run)
            SVN_ERR(svn_wc_add2(path, adm_access,
                                copyfrom_url, rev,
                                merge_b->ctx->cancel_func,
                                merge_b->ctx->cancel_baton,
                                NULL, NULL, /* no notification func! */
                                subpool));
          if (merge_b->dry_run)
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

/* Struct used for as the baton for calling merge_delete_notify_func(). */
typedef struct merge_delete_notify_baton_t
{
  svn_client_ctx_t *ctx;

  /* path to skip */
  const char *path_skip;
} merge_delete_notify_baton_t;

/* Notify callback function that wraps the normal callback
 * function to remove a notification that will be sent twice
 * and set the proper action. */
static void
merge_delete_notify_func(void *baton,
                         const svn_wc_notify_t *notify,
                         apr_pool_t *pool)
{
  merge_delete_notify_baton_t *mdb = baton;
  svn_wc_notify_t *new_notify;

  /* Skip the notification for the path we called svn_client__wc_delete() with,
   * because it will be outputed by repos_diff.c:delete_item */  
  if (strcmp(notify->path, mdb->path_skip) == 0)
    return;
  
  /* svn_client__wc_delete() is written primarily for scheduling operations not
   * update operations.  Since merges are update operations we need to alter
   * the delete notification to show as an update not a schedule so alter 
   * the action. */
  if (notify->action == svn_wc_notify_delete)
    {
      /* We need to copy it since notify is const. */
      new_notify = svn_wc_dup_notify(notify, pool);
      new_notify->action = svn_wc_notify_update_delete;
      notify = new_notify;
    }

  if (mdb->ctx->notify_func2)
    (*mdb->ctx->notify_func2)(mdb->ctx->notify_baton2, notify, pool);
}

/* A svn_wc_diff_callbacks2_t function. */
static svn_error_t *
merge_dir_deleted(svn_wc_adm_access_t *adm_access,
                  svn_wc_notify_state_t *state,
                  const char *path,
                  void *baton)
{
  struct merge_cmd_baton *merge_b = baton;
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
        merge_delete_notify_baton_t mdb;

        mdb.ctx = merge_b->ctx;
        mdb.path_skip = path;

        svn_path_split(path, &parent_path, NULL, subpool);
        SVN_ERR(svn_wc_adm_retrieve(&parent_access, adm_access, parent_path,
                                    subpool));
        err = svn_client__wc_delete(path, parent_access, merge_b->force,
                                    merge_b->dry_run, FALSE,
                                    merge_delete_notify_func, &mdb,
                                    merge_b->ctx, subpool);
        if (err && state)
          {
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
  
/* The main callback table for 'svn merge'.  */
static const svn_wc_diff_callbacks2_t
merge_callbacks =
  {
    merge_file_changed,
    merge_file_added,
    merge_file_deleted,
    merge_dir_added,
    merge_dir_deleted,
    merge_props_changed
  };


/*-----------------------------------------------------------------------*/

/*** Retrieving mergeinfo. ***/

/* Adjust merge sources in MERGEINFO (which is assumed to be non-NULL). */
static APR_INLINE void
adjust_mergeinfo_source_paths(apr_hash_t *mergeinfo, const char *walk_path,
                              apr_hash_t *wc_mergeinfo, apr_pool_t *pool)
{
  apr_hash_index_t *hi;
  const void *merge_source;
  void *rangelist;
  const char *path;

  for (hi = apr_hash_first(NULL, wc_mergeinfo); hi; hi = apr_hash_next(hi))
    {
      /* Copy inherited mergeinfo into our output hash, adjusting the
         merge source as appropriate. */
      apr_hash_this(hi, &merge_source, NULL, &rangelist);
      path = svn_path_join((const char *) merge_source, walk_path,
                           apr_hash_pool_get(mergeinfo));
      /* ### If pool has a different lifetime than mergeinfo->pool,
         ### this use of "rangelist" will be a problem... */
      apr_hash_set(mergeinfo, path, APR_HASH_KEY_STRING, rangelist);
    }
}

/* Find explicit or inherited WC mergeinfo for WCPATH, and return it
   in *MERGEINFO (NULL if no mergeinfo is set).  Set *INHERITED to
   whether the mergeinfo was inherited (TRUE or FALSE).

   INHERIT indicates whether explicit, explicit or inherited, or only
   inherited mergeinfo for WCPATH is retrieved.

   Don't look for inherited mergeinfo any higher than LIMIT_PATH
   (ignored if NULL).

   Set *WALKED_PATH to the path climbed from WCPATH to find inherited
   mergeinfo, or "" if none was found. (ignored if NULL). */
static svn_error_t *
get_wc_mergeinfo(apr_hash_t **mergeinfo,
                 svn_boolean_t *inherited,
                 svn_boolean_t pristine,
                 svn_mergeinfo_inheritance_t inherit,
                 const svn_wc_entry_t *entry,
                 const char *wcpath,
                 const char *limit_path,
                 const char **walked_path,
                 svn_wc_adm_access_t *adm_access,
                 svn_client_ctx_t *ctx,
                 apr_pool_t *pool)
{
  const char *walk_path = "";
  apr_hash_t *wc_mergeinfo;
  svn_boolean_t switched;

  if (limit_path)
    SVN_ERR(svn_path_get_absolute(&limit_path, limit_path, pool));

  while (TRUE)
    {
      /* Don't look for explicit mergeinfo on WCPATH if we are only
         interested in inherited mergeinfo. */
      if (inherit == svn_mergeinfo_nearest_ancestor)
        {
          wc_mergeinfo = NULL;
          inherit = svn_mergeinfo_inherited;
        }
      else
        {
          /* Look for mergeinfo on WCPATH.  If there isn't any and we want
             inherited mergeinfo, walk towards the root of the WC until we
             encounter either (a) an unversioned directory, or (b) mergeinfo.
             If we encounter (b), use that inherited mergeinfo as our
             baseline. */
          SVN_ERR(svn_client__parse_mergeinfo(&wc_mergeinfo, entry, wcpath,
                                              pristine, adm_access, ctx,
                                              pool));

          /* If WCPATH is switched, don't look any higher for inherited
             mergeinfo. */
          SVN_ERR(svn_wc__path_switched(wcpath, &switched, entry, pool));
          if (switched)
            break;
        }

      /* Subsequent svn_wc_adm_access_t need to be opened with
         an absolute path so we can walk up and out of the WC
         if necessary.  If we are using LIMIT_PATH it needs to
         be absolute too. */
#if defined(WIN32) || defined(__CYGWIN__)
      /* On Windows a path is also absolute when it starts with
         'H:/' where 'H' is any upper or lower case letter. */
      if (strlen(wcpath) == 0 
          || ((strlen(wcpath) > 0 && wcpath[0] != '/')
               && !(strlen(wcpath) > 2
                    && wcpath[1] == ':'
                    && wcpath[2] == '/'
                    && ((wcpath[0] >= 'A' && wcpath[0] <= 'Z')
                        || (wcpath[0] >= 'a' && wcpath[0] <= 'z')))))
#else
      if (!(strlen(wcpath) > 0 && wcpath[0] == '/'))
#endif /* WIN32 or Cygwin */
        {
          SVN_ERR(svn_path_get_absolute(&wcpath, wcpath, pool));
        }

      if (wc_mergeinfo == NULL &&
          inherit != svn_mergeinfo_explicit &&
          !svn_dirent_is_root(wcpath, strlen(wcpath)))
        {
          svn_error_t *err;

          /* Don't look any higher than the limit path. */
          if (limit_path && strcmp(limit_path, wcpath) == 0)
            break;

          /* No explicit mergeinfo on this path.  Look higher up the
             directory tree while keeping track of what we've walked. */
          walk_path = svn_path_join(svn_path_basename(wcpath, pool),
                                    walk_path, pool);
          wcpath = svn_path_dirname(wcpath, pool);

          err = svn_wc_adm_open3(&adm_access, NULL, wcpath,
                                 FALSE, 0, NULL, NULL, pool);
          if (err)
            {
              if (err->apr_err == SVN_ERR_WC_NOT_DIRECTORY)
                {
                  svn_error_clear(err);
                  err = SVN_NO_ERROR;
                  *inherited = FALSE;
                  *mergeinfo = wc_mergeinfo;
                }
              return err;
            }

          SVN_ERR(svn_wc_entry(&entry, wcpath, adm_access, FALSE, pool));

          if (entry)
            /* We haven't yet risen above the root of the WC. */
            continue;
      }
      break;
    }

  if (svn_path_is_empty(walk_path))
    {
      /* Merge info is explicit. */
      *inherited = FALSE;
      *mergeinfo = wc_mergeinfo;
    }
  else
    {
      /* Merge info may be inherited. */
      if (wc_mergeinfo)
        {
          *inherited = (wc_mergeinfo && apr_hash_count(wc_mergeinfo) > 0);
          *mergeinfo = apr_hash_make(pool);
          adjust_mergeinfo_source_paths(*mergeinfo, walk_path, wc_mergeinfo,
                                        pool);
        }
      else
        {
          *inherited = FALSE;
          *mergeinfo = NULL;
        }
    }

  if (walked_path)
    *walked_path = walk_path;

  return SVN_NO_ERROR;
}

/* Retrieve the direct mergeinfo for the TARGET_WCPATH from the WC's
   mergeinfo prop, or that inherited from its nearest ancestor if the
   target has no info of its own.

   If no mergeinfo can be obtained from the WC or REPOS_ONLY is TRUE,
   get it from the repository (opening a new RA session if RA_SESSION
   is NULL).  Store any mergeinfo obtained for TARGET_WCPATH -- which
   is reflected by ENTRY -- in *TARGET_MERGEINFO, if no mergeinfo is
   found *TARGET_MERGEINFO is NULL.

   INHERIT indicates whether explicit, explicit or inherited, or only
   inherited mergeinfo for TARGET_WCPATH is retrieved.

   If TARGET_WCPATH inherited its mergeinfo from a working copy ancestor
   or if it was obtained from the repository, set *INDIRECT to TRUE, set it
   to FALSE *otherwise. */
static svn_error_t *
get_wc_or_repos_mergeinfo(apr_hash_t **target_mergeinfo,
                          const svn_wc_entry_t *entry,
                          svn_boolean_t *indirect,
                          svn_boolean_t repos_only,
                          svn_mergeinfo_inheritance_t inherit,
                          svn_ra_session_t *ra_session,
                          const char *target_wcpath,
                          svn_wc_adm_access_t *adm_access,
                          svn_client_ctx_t *ctx,
                          apr_pool_t *pool)
{
  const char *url;
  const char *repos_rel_path;
  svn_revnum_t target_rev;

  /* We may get an entry with abrieviated information from TARGET_WCPATH's
     parent if TARGET_WCPATH is missing.  These limited entries do not have
     a URL and without that we cannot get accurate mergeinfo for
     TARGET_WCPATH. */
  if (entry->url == NULL)
    return svn_error_createf(SVN_ERR_ENTRY_MISSING_URL, NULL, 
                             _("Entry '%s' has no URL"),
                             svn_path_local_style(target_wcpath, pool));

  /* ### FIXME: dionisos sez: "We can have schedule 'normal' files
     ### with a copied parameter of TRUE and a revision number of
     ### INVALID_REVNUM.  Copied directories cause this behaviour on
     ### their children.  It's an implementation shortcut to model
     ### wc-side copies." */
  switch (entry->schedule)
    {
    case svn_wc_schedule_add:
    case svn_wc_schedule_replace:
      /* If we have any history, consider its mergeinfo. */
      if (entry->copyfrom_url)
        {
          url = entry->copyfrom_url;
          target_rev = entry->copyfrom_rev;
          break;
        }

    default:
      /* Consider the mergeinfo for the WC target. */
      url = entry->url;
      target_rev = entry->revision;
      break;
    }

  repos_rel_path = url + strlen(entry->repos);

  /* ### TODO: To handle sub-tree mergeinfo, the list will need to
     ### include the those child paths which have mergeinfo which
     ### differs from that of TARGET_WCPATH, and if those paths are
     ### directories, their children as well. */

  if (repos_only)
    *target_mergeinfo = NULL;
  else
    SVN_ERR(get_wc_mergeinfo(target_mergeinfo, indirect, FALSE, inherit,
                             entry, target_wcpath, NULL, NULL, adm_access,
                             ctx, pool));

  /* If there in no WC mergeinfo check the repository. */
  if (*target_mergeinfo == NULL)
    {
      apr_hash_t *repos_mergeinfo;

      /* No need to check the repos is this is a local addition. */
      if (entry->schedule != svn_wc_schedule_add)
        {
          apr_hash_t *props = apr_hash_make(pool);

          /* Get the pristine SVN_PROP_MERGE_INFO. 
             If it exists, then it should have been deleted by the local 
             merges. So don't get the mergeinfo from the repository. Just 
             assume the mergeinfo to be NULL.
          */
          SVN_ERR(svn_client__get_prop_from_wc(props, SVN_PROP_MERGE_INFO,
                                               target_wcpath, TRUE, entry, 
                                               adm_access, FALSE, ctx, pool));
          if (apr_hash_get(props, target_wcpath, APR_HASH_KEY_STRING) == NULL)
            {
              if (ra_session == NULL)
                SVN_ERR(svn_client__open_ra_session_internal(&ra_session, url,
                                                             NULL, NULL, NULL,
                                                             FALSE, TRUE, ctx,
                                                             pool));

              SVN_ERR(svn_client__get_repos_mergeinfo(ra_session,
                                                      &repos_mergeinfo,
                                                      repos_rel_path, 
                                                      target_rev,
                                                      inherit,
                                                      pool));
              if (repos_mergeinfo)
                {
                  *target_mergeinfo = repos_mergeinfo;
                  *indirect = TRUE;
                }
            }
        }
    }
  return SVN_NO_ERROR;
}

/*-----------------------------------------------------------------------*/

/*** Eliding mergeinfo. ***/

/* Helper for elide_mergeinfo().

   Find all paths in CHILD_MERGEINFO which map to empty revision ranges
   and copy these from CHILD_MERGEINFO to *EMPTY_RANGE_MERGEINFO iff
   PARENT_MERGEINFO is NULL or does not have mergeinfo for the path in
   question.

   All mergeinfo in CHILD_MERGEINFO not copied to *EMPTY_RANGE_MERGEINFO
   is copied to *NONEMPTY_RANGE_MERGEINFO.

   *EMPTY_RANGE_MERGEINFO and *NONEMPTY_RANGE_MERGEINFO are set to empty
   hashes if nothing is copied into them.

   All copied hashes are deep copies allocated in POOL. */
static svn_error_t *
get_empty_rangelists_unique_to_child(apr_hash_t **empty_range_mergeinfo,
                                     apr_hash_t **nonempty_range_mergeinfo,
                                     apr_hash_t *child_mergeinfo,
                                     apr_hash_t *parent_mergeinfo,
                                     apr_pool_t *pool)
{
  *empty_range_mergeinfo = apr_hash_make(pool);
  *nonempty_range_mergeinfo = apr_hash_make(pool);

  if (child_mergeinfo)
    {
      apr_hash_index_t *hi;
      void *child_val;
      const void *child_key;
      apr_array_header_t *child_range;
      const char *child_path;

      /* Iterate through CHILD_MERGEINFO looking for mergeinfo with empty
         revision ranges. */
      for (hi = apr_hash_first(pool, child_mergeinfo); hi;
           hi = apr_hash_next(hi))
        {
          apr_hash_this(hi, &child_key, NULL, &child_val);
          child_path = child_key;
          child_range = child_val;

          /* Copy paths with empty revision ranges which don't exist in
             PARENT_MERGEINFO from CHILD_MERGEINFO to *EMPTY_RANGE_MERGEINFO.
             Copy everything else to *NONEMPTY_RANGE_MERGEINFO. */
          if (child_range->nelts == 0
              && (parent_mergeinfo == NULL
                  || apr_hash_get(parent_mergeinfo, child_path,
                                  APR_HASH_KEY_STRING) == NULL))
            {
              apr_hash_set(*empty_range_mergeinfo,
                           apr_pstrdup(pool, child_path),
                           APR_HASH_KEY_STRING,
                           svn_rangelist_dup(child_range, pool));
            }
          else
            {
              apr_hash_set(*nonempty_range_mergeinfo,
                           apr_pstrdup(pool, child_path),
                           APR_HASH_KEY_STRING,
                           svn_rangelist_dup(child_range, pool));
            }
        }
    }
  return SVN_NO_ERROR;
}

/* Helper for svn_client__elide_mergeinfo() and elide_children().

   Given a working copy PATH, its mergeinfo hash CHILD_MERGEINFO, and the
   mergeinfo of PATH's nearest ancestor PARENT_MERGEINFO, compare
   CHILD_MERGEINFO to PARENT_MERGEINFO to see if the former elides to
   the latter, following the elision rules described in
   svn_client__elide_mergeinfo()'s docstring.  If elision (full or partial)
   does occur, then update PATH's mergeinfo appropriately.  If CHILD_MERGEINFO
   is NULL, do nothing.
   
   If PATH_SUFFIX and PARENT_MERGEINFO are not NULL append PATH_SUFFIX to each
   path in PARENT_MERGEINFO before performing the comparison. */
static svn_error_t *
elide_mergeinfo(apr_hash_t *parent_mergeinfo,
                apr_hash_t *child_mergeinfo,
                const char *path,
                const char *path_suffix,
                svn_wc_adm_access_t *adm_access,
                apr_pool_t *pool)
{
  apr_pool_t *subpool;
  apr_hash_t *mergeinfo, *child_empty_mergeinfo, *child_nonempty_mergeinfo;
  svn_boolean_t equal_mergeinfo;

  /* A tri-state value describing the various types of elision possible for
     svn:mergeinfo set on a WC path. */
  enum wc_elision_type
  {
    elision_type_none,    /* No elision occurs. */
    elision_type_partial, /* Paths that exist only in CHILD_MERGEINFO and
                             map to empty revision ranges elide. */
    elision_type_full     /* All mergeinfo in CHILD_MERGEINFO elides. */
  } elision_type = elision_type_none;
  
  /* Easy out: No child mergeinfo to elide. */
  if (child_mergeinfo == NULL)
    return SVN_NO_ERROR;

  subpool = svn_pool_create(pool);
  if (path_suffix && parent_mergeinfo)
    {
      apr_hash_index_t *hi;
      void *val;
      const void *key;
      const char *new_path;
      apr_array_header_t *rangelist;

      mergeinfo = apr_hash_make(subpool);

      for (hi = apr_hash_first(subpool, parent_mergeinfo); hi;
           hi = apr_hash_next(hi))
        {
          apr_hash_this(hi, &key, NULL, &val);
          new_path = svn_path_join((const char *) key, path_suffix, subpool);
          rangelist = val;
          apr_hash_set(mergeinfo, new_path, APR_HASH_KEY_STRING, rangelist);
        }
    }
  else
    {
      mergeinfo = parent_mergeinfo;
    }

 /* Separate any mergeinfo with empty rev ranges for paths that exist only
    in CHILD_MERGEINFO and store these in CHILD_EMPTY_MERGEINFO. */
  SVN_ERR(get_empty_rangelists_unique_to_child(&child_empty_mergeinfo,
                                               &child_nonempty_mergeinfo,
                                               child_mergeinfo, mergeinfo,
                                               subpool));
  
  /* If *all* paths in CHILD_MERGEINFO map to empty revision ranges and none
     of these paths exist in PARENT_MERGEINFO full elision occurs; if only
     *some* of the paths in CHILD_MERGEINFO meet this criteria we know, at a
     minimum, partial elision will occur. */
  if (apr_hash_count(child_empty_mergeinfo) > 0)
    elision_type = apr_hash_count(child_nonempty_mergeinfo) == 0
                   ? elision_type_full : elision_type_partial;

  if (elision_type == elision_type_none && mergeinfo)
    {
      apr_hash_t *parent_empty_mergeinfo, *parent_nonempty_mergeinfo;
      
      /* Full elision also occurs if MERGEINFO and TARGET_MERGEINFO are
         equal except for paths unique to MERGEINFO that map to empty
         revision ranges.
        
         Separate any mergeinfo with empty rev ranges for paths that exist
         only in MERGEINFO and store these in PARENT_EMPTY_MERGEINFO and
         compare that with CHILD_MERGEINFO. */
      SVN_ERR(get_empty_rangelists_unique_to_child(&parent_empty_mergeinfo,
                                                   &parent_nonempty_mergeinfo,
                                                   mergeinfo, child_mergeinfo,
                                                   subpool));
      SVN_ERR(svn_mergeinfo__equals(&equal_mergeinfo,
                                    parent_nonempty_mergeinfo,
                                    child_mergeinfo, subpool));
      if (equal_mergeinfo)
        elision_type = elision_type_full;
    }

  if (elision_type != elision_type_full && mergeinfo)
    {
      /* If no determination of elision status has been made yet or we know
        only that partial elision occurs, compare CHILD_NONEMPTY_MERGEINFO
        with the PATH_SUFFIX tweaked version of PARENT_MERGEINFO for equality.
        
        If we determined that at least partial elision occurs, full elision
        may still yet occur if CHILD_NONEMPTY_MERGEINFO, which no longer
        contains any paths unique to it that map to empty revision ranges,
        is equivalent to PARENT_MERGEINFO. */
      SVN_ERR(svn_mergeinfo__equals(&equal_mergeinfo,
                                    child_nonempty_mergeinfo,
                                    mergeinfo, subpool));
      if (equal_mergeinfo)
        elision_type = elision_type_full;
    }

    switch (elision_type)
      {
      case elision_type_full:
        SVN_ERR(svn_wc_prop_set2(SVN_PROP_MERGE_INFO, NULL, path, adm_access,
                                 TRUE, subpool));
        break;
      case elision_type_partial:
        SVN_ERR(svn_client__record_wc_mergeinfo(path,
                                                child_nonempty_mergeinfo,
                                                adm_access, subpool));
        break;
      default:
        break; /* Leave mergeinfo on PATH as-is. */
      }

  svn_pool_destroy(subpool);
  return SVN_NO_ERROR;
}

/* Helper for svn_client_merge3 and svn_client_merge_peg3

   CHILDREN_WITH_MERGEINFO is filled with child paths (const char *) of
   TARGET_WCPATH which have svn:mergeinfo set on them, arranged in depth
   first order (see discover_and_merge_children).

   For each path in CHILDREN_WITH_MERGEINFO which is an immediate child of
   TARGET_WCPATH, check if that path's mergeinfo elides to TARGET_WCPATH.
   If it does elide, clear all mergeinfo from the path. */
static svn_error_t *
elide_children(apr_array_header_t *children_with_mergeinfo,
               const char *target_wcpath,
               const svn_wc_entry_t *entry,
               svn_wc_adm_access_t *adm_access,
               svn_client_ctx_t *ctx,
               apr_pool_t *pool)
{
  if (children_with_mergeinfo && children_with_mergeinfo->nelts)
    {
      int i;
      const char *last_immediate_child;
      apr_hash_t *target_mergeinfo;
      apr_pool_t *iterpool = svn_pool_create(pool);

      /* Get mergeinfo for the target of the merge. */
      SVN_ERR(svn_client__parse_mergeinfo(&target_mergeinfo, entry,
                                          target_wcpath, FALSE,
                                          adm_access, ctx, pool));

      /* For each immediate child of the merge target check if
         its merginfo elides to the target. */
      for (i = 0; i < children_with_mergeinfo->nelts; i++)
        {
          apr_hash_t *child_mergeinfo;
          const char *child_wcpath;
          svn_boolean_t switched;
          const svn_wc_entry_t *child_entry;
          svn_pool_clear(iterpool);
          child_wcpath = APR_ARRAY_IDX(children_with_mergeinfo, i,
                                       const char *);
          if (i == 0)
            {
              /* children_with_mergeinfo is sorted depth
                 first so first path might be the target of
                 the merge if the target had mergeinfo prior
                 to the start of the merge. */
              if (strcmp(target_wcpath, child_wcpath) == 0)
                {
                  last_immediate_child = NULL;
                  continue;
                }
              last_immediate_child = child_wcpath;
            }
          else if (last_immediate_child
                   && svn_path_is_ancestor(last_immediate_child, child_wcpath))
            {
              /* Not an immediate child. */
              continue;
            }
          else
            {
              /* Found the first (last_immediate_child == NULL)
                 or another immediate child. */
              last_immediate_child = child_wcpath;
            }

          /* Don't try to elide switched children. */
          SVN_ERR(svn_wc__entry_versioned(&child_entry, child_wcpath,
                                          adm_access, FALSE, iterpool));
          SVN_ERR(svn_wc__path_switched(child_wcpath, &switched, child_entry,
                                        iterpool));
          if (!switched)
            {
              const char *path_prefix = svn_path_dirname(child_wcpath,
                                                         iterpool);
              const char *path_suffix = svn_path_basename(child_wcpath,
                                                          iterpool);
              
              SVN_ERR(svn_client__parse_mergeinfo(&child_mergeinfo, entry,
                                                  child_wcpath, FALSE,
                                                  adm_access, ctx, iterpool));

              while (strcmp(path_prefix, target_wcpath) != 0)
                {
                  path_suffix = svn_path_join(svn_path_basename(path_prefix,
                                                                iterpool),
                                              path_suffix, iterpool);
                  path_prefix = svn_path_dirname(path_prefix, iterpool);
                }

              SVN_ERR(elide_mergeinfo(target_mergeinfo, child_mergeinfo,
                                      child_wcpath, path_suffix, adm_access,
                                      iterpool));
            }
        }
    svn_pool_destroy(iterpool);
  }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_client__elide_mergeinfo(const char *target_wcpath,
                            const char *wc_elision_limit_path,
                            const svn_wc_entry_t *entry,
                            svn_wc_adm_access_t *adm_access,
                            svn_client_ctx_t *ctx,
                            apr_pool_t *pool)
{
  /* Check for first easy out: We are already at the limit path. */
  if (!wc_elision_limit_path
      || strcmp(target_wcpath, wc_elision_limit_path) != 0)
    {
      apr_hash_t *target_mergeinfo;
      apr_hash_t *mergeinfo = NULL;
      svn_boolean_t inherited, switched;
      const char *walk_path;

      /* Check for second easy out: TARGET_WCPATH is switched. */
      SVN_ERR(svn_wc__path_switched(target_wcpath, &switched, entry, pool));
      if (!switched)
        {
          /* Get the TARGET_WCPATH's explicit mergeinfo. */
          SVN_ERR(get_wc_mergeinfo(&target_mergeinfo, &inherited,
                                   FALSE, svn_mergeinfo_inherited,
                                   entry, target_wcpath,
                                   wc_elision_limit_path
                                   ? wc_elision_limit_path : NULL,
                                   &walk_path, adm_access, ctx, pool));

         /* If TARGET_WCPATH has no explicit mergeinfo, there's nothing to
             elide, we're done. */
          if (inherited || target_mergeinfo == NULL)
            return SVN_NO_ERROR;

          /* Get TARGET_WCPATH's inherited mergeinfo from the WC. */
          SVN_ERR(get_wc_mergeinfo(&mergeinfo, &inherited, FALSE,
                                   svn_mergeinfo_nearest_ancestor, entry,
                                   target_wcpath,
                                   wc_elision_limit_path
                                   ? wc_elision_limit_path : NULL,
                                   &walk_path, adm_access, ctx, pool));

          /* If TARGET_WCPATH inherited no mergeinfo from the WC and we are
             not limiting our search to the working copy then check if it
             inherits any from the repos. */
          if (!mergeinfo && !wc_elision_limit_path)
            {
              SVN_ERR(get_wc_or_repos_mergeinfo(&mergeinfo, entry,
                                                &inherited, TRUE,
                                                svn_mergeinfo_nearest_ancestor,
                                                NULL, target_wcpath,
                                                adm_access, ctx, pool));
            }
          
          /* If there is nowhere to elide TARGET_WCPATH's mergeinfo to and
             the elision is limited, then we are done.*/
          if (!mergeinfo && wc_elision_limit_path)
            return SVN_NO_ERROR;

          SVN_ERR(elide_mergeinfo(mergeinfo, target_mergeinfo, target_wcpath,
                                  NULL, adm_access, pool));
        }
    }
  return SVN_NO_ERROR;
}

/*-----------------------------------------------------------------------*/

/*** Doing the actual merging. ***/

/* Find any merged revision ranges that the merge history for the
   merge source SRC_URL (between RANGE->start and RANGE->end) has
   recorded for the merge target ENTRY.  Get the mergeinfo for the
   source, then get the rangelist for the target (ENTRY) from that
   mergeinfo, subtract it from *UNREFINED_RANGE, and record the
   result in *REQUESTED_RANGELIST. */
static svn_error_t *
calculate_requested_ranges(apr_array_header_t **requested_rangelist,
                           svn_merge_range_t *unrefined_range,
                           const char *src_url, const svn_wc_entry_t *entry,
                           svn_wc_adm_access_t *adm_access,
                           svn_client_ctx_t *ctx, apr_pool_t *pool)
{
  apr_array_header_t *src_rangelist_for_tgt = NULL;
  apr_hash_t *added_mergeinfo, *deleted_mergeinfo,
    *start_mergeinfo, *end_mergeinfo;
  svn_opt_revision_t revision;
  revision.kind = svn_opt_revision_number;

  /* Find any mergeinfo added in RANGE. */
  /* ### svn_ra_get_mergeinfo() might improve efficiency. */
  revision.value.number = MIN(unrefined_range->start, unrefined_range->end);
  SVN_ERR(svn_client_get_mergeinfo(&start_mergeinfo, src_url, &revision,
                                   ctx, pool));
  revision.value.number = MAX(unrefined_range->start, unrefined_range->end);
  SVN_ERR(svn_client_get_mergeinfo(&end_mergeinfo, src_url, &revision,
                                   ctx, pool));
  SVN_ERR(svn_mergeinfo_diff(&deleted_mergeinfo, &added_mergeinfo,
                             start_mergeinfo, end_mergeinfo, pool));

  if (added_mergeinfo)
    {
      const char *src_rel_path;
      SVN_ERR(svn_client__path_relative_to_root(&src_rel_path, entry->url,
                                                entry->repos, NULL, adm_access,
                                                pool));
      src_rangelist_for_tgt = apr_hash_get(added_mergeinfo, src_rel_path,
                                           APR_HASH_KEY_STRING);
    }

  *requested_rangelist = apr_array_make(pool, 1, sizeof(unrefined_range));
  APR_ARRAY_PUSH(*requested_rangelist, svn_merge_range_t *) = unrefined_range;
  if (src_rangelist_for_tgt)
    /* Remove overlapping revision ranges from the requested range. */
    SVN_ERR(svn_rangelist_remove(requested_rangelist, src_rangelist_for_tgt,
                                 *requested_rangelist, pool));
  return SVN_NO_ERROR;
}

/* Calculate a rangelist of svn_merge_range_t *'s -- for use by
   do_merge()'s application of the editor to the WC -- by subtracting
   revisions which have already been merged into the WC from the
   requested range(s) REQUESTED_MERGE, and storing what's left in
   REMAINING_RANGES.  TARGET_MERGEINFO is expected to be non-NULL. */
static svn_error_t *
calculate_merge_ranges(apr_array_header_t **remaining_ranges,
                       const char *rel_path,
                       apr_hash_t *target_mergeinfo,
                       apr_array_header_t *requested_merge,
                       svn_boolean_t is_rollback,
                       apr_pool_t *pool)
{
  apr_array_header_t *target_rangelist;

  if (is_rollback)
    /* As we monkey with this data, make a copy of it. */
    requested_merge = svn_rangelist_dup(requested_merge, pool);

  /* If we don't end up removing any revisions from the requested
     range, it'll end up as our sole remaining range. */
  *remaining_ranges = requested_merge;

  /* Subtract the revision ranges which have already been merged into
     the WC (if any) from the range requested for merging (to avoid
     repeated merging). */
  if (target_mergeinfo)
    target_rangelist = apr_hash_get(target_mergeinfo, rel_path,
                                    APR_HASH_KEY_STRING);
  else
    target_rangelist = NULL;

  if (target_rangelist)
    {
      if (is_rollback)
        {
          /* Return the intersection of the revs which are both
             already represented by the WC and are requested for
             revert.  The revert range and will need to be reversed
             for our APIs to work properly, as will the output for the
             revert to work properly. */
          SVN_ERR(svn_rangelist_reverse(requested_merge, pool));
          SVN_ERR(svn_rangelist_intersect(remaining_ranges, target_rangelist,
                                          requested_merge, pool));
          SVN_ERR(svn_rangelist_reverse(*remaining_ranges, pool));
        }
      else
        /* Return only those revs not already represented by this WC. */
        SVN_ERR(svn_rangelist_remove(remaining_ranges, target_rangelist,
                                     requested_merge, pool));
    }

  return SVN_NO_ERROR;
}

/* Contains any state collected while receiving path notifications. */
typedef struct
{
  /* The wrapped callback and baton. */
  svn_wc_notify_func2_t wrapped_func;
  void *wrapped_baton;

  /* Whether the operation's URL1 and URL2 are the same. */
  svn_boolean_t same_urls;

  /* The number of notifications received. */
  apr_uint32_t nbr_notifications;

  /* The number of operative notifications received. */
  apr_uint32_t nbr_operative_notifications;

  /* The list of any skipped paths, which should be examined and
     cleared after each invocation of the callback. */
  apr_hash_t *skipped_paths;

  /* Pool used in notification_receiver() to avoid the iteration
     sub-pool which is passed in, then subsequently destroyed. */
  apr_pool_t *pool;
} notification_receiver_baton_t;

/* Our svn_wc_notify_func2_t wrapper.*/
static void
notification_receiver(void *baton, const svn_wc_notify_t *notify,
                      apr_pool_t *pool)
{
  notification_receiver_baton_t *notify_b = baton;

  if (notify_b->same_urls)
    {
      notify_b->nbr_notifications++;

      if (notify->content_state == svn_wc_notify_state_conflicted
          || notify->content_state == svn_wc_notify_state_merged
          || notify->content_state == svn_wc_notify_state_changed
          || notify->prop_state == svn_wc_notify_state_conflicted
          || notify->prop_state == svn_wc_notify_state_merged
          || notify->prop_state == svn_wc_notify_state_changed)
        notify_b->nbr_operative_notifications++;

      if (notify->action == svn_wc_notify_skip)
        {
          const char *skipped_path = apr_pstrdup(notify_b->pool, notify->path);

          if (notify_b->skipped_paths == NULL)
            notify_b->skipped_paths = apr_hash_make(notify_b->pool);

          apr_hash_set(notify_b->skipped_paths, skipped_path,
                       APR_HASH_KEY_STRING, skipped_path);
        }
    }

  if (notify_b->wrapped_func)
    (*notify_b->wrapped_func)(notify_b->wrapped_baton, notify, pool);
}

#if 0
/* An implementation of the svn_wc_conflict_resolver_func_t
   interface.  Our default conflict resolution approach is to
   complain, and error out. */
static svn_error_t *
default_conflict_resolver(const char *path, void *baton, apr_pool_t *pool)
{
  return svn_error_createf(SVN_ERR_WC_FOUND_CONFLICT, NULL,
                           _("Path '%s' is in conflict, and must be resolved "
                             "before the remainder of the requested merge "
                             "can be applied"), path);
}
#endif

/* Create mergeinfo describing the merge of RANGE into our target,
   without including mergeinfo for skips or conflicts from
   NOTIFY_B. */
static svn_error_t *
determine_merges_performed(apr_hash_t **merges, const char *target_wcpath,
                           svn_merge_range_t *range,
                           notification_receiver_baton_t *notify_b,
                           apr_pool_t *pool)
{
  apr_array_header_t *rangelist = apr_array_make(pool, 1, sizeof(*range));
  apr_size_t nbr_skips = (notify_b->skipped_paths != NULL ?
                          apr_hash_count(notify_b->skipped_paths) : 0);
  *merges = apr_hash_make(pool);
  APR_ARRAY_PUSH(rangelist, svn_merge_range_t *) = range;

  /* Set the mergeinfo for the root of the target tree unless we skipped
     everything. */
  if (nbr_skips == 0 || notify_b->nbr_operative_notifications > 0)
    {
      apr_hash_set(*merges, target_wcpath, APR_HASH_KEY_STRING, rangelist);
      if (nbr_skips > 0)
        {
          apr_hash_index_t *hi;
          const void *skipped_path;

          /* Override the mergeinfo for child paths which weren't
             actually merged. */
          for (hi = apr_hash_first(NULL, notify_b->skipped_paths); hi;
               hi = apr_hash_next(hi))
            {
              apr_hash_this(hi, &skipped_path, NULL, NULL);

              /* Add an empty range list for this path. */
              apr_hash_set(*merges, (const char *) skipped_path,
                           APR_HASH_KEY_STRING,
                           apr_array_make(pool, 0, sizeof(*range)));

              if (nbr_skips < notify_b->nbr_notifications)
                /* ### Use RANGELIST as the mergeinfo for all children of
                   ### this path which were not also explicitly
                   ### skipped? */
                ;
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
  apr_hash_t *mergeinfo;
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
      SVN_ERR(svn_client__parse_mergeinfo(&mergeinfo, entry, path, FALSE,
                                          adm_access, ctx, subpool));

      /* If we are attempting to set empty revision range override mergeinfo
         on a path with no explicit mergeinfo, we first need the pristine 
         mergeinfo that path inherits. */
      if (mergeinfo == NULL && ranges->nelts == 0)
        {
          svn_boolean_t inherited;
          SVN_ERR(get_wc_mergeinfo(&mergeinfo, &inherited, TRUE,
                                   svn_mergeinfo_nearest_ancestor, entry,
                                   path, NULL, NULL, adm_access, ctx,
                                   subpool));
        }

      if (mergeinfo == NULL)
        mergeinfo = apr_hash_make(subpool);

      /* ASSUMPTION: "target_wcpath" is always both a parent and
         prefix of "path". */
      len = strlen(target_wcpath);
      if (len < strlen(path))
        rel_path = apr_pstrcat(subpool, repos_rel_path, "/", path + len + 1,
                               NULL);
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
                                       subpool));
        }
      else
        {
          SVN_ERR(svn_rangelist_merge(&rangelist, ranges, subpool));
        }
      /* Update the mergeinfo by adjusting the path's rangelist. */
      apr_hash_set(mergeinfo, rel_path, APR_HASH_KEY_STRING, rangelist);

      if (is_rollback && apr_hash_count(mergeinfo) == 0)
        mergeinfo = NULL;

      err = svn_client__record_wc_mergeinfo(path, mergeinfo,
                                            adm_access, subpool);

      if (err && err->apr_err == SVN_ERR_ENTRY_NOT_FOUND)
        {
          /* PATH isn't just missing, it's not even versioned as far as this
             working copy knows.  But it was included in MERGES, which means
             that the server knows about it.  Likely we don't have access to
             the source due to authz restrictions.  For now just clear the
             error and continue...

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

/* A tri-state value returned by grok_range_info_from_opt_revisions(). */
enum merge_type
{
  merge_type_merge,     /* additive */
  merge_type_rollback,  /* subtractive */
  merge_type_no_op      /* no change */
};

/* Resolve requested revisions for PATH1@REVISION1 and PATH2@REVISION2
   (using RA_SESSION1 and RA_SESSION2), convert them into a merge
   range, determine whether that range represents a merge/revert/no-op
   if SAME_URLS (assume merge otherwise), and store that knowledge in
   *RANGE and *MERGE_TYPE (respectively).  If the resulting revisions
   would result in the merge being a no-op, RANGE->START and
   RANGE->END are set to SVN_INVALID_REVNUM. */
static svn_error_t *
grok_range_info_from_opt_revisions(svn_merge_range_t *range,
                                   enum merge_type *merge_type,
                                   svn_boolean_t same_urls,
                                   svn_ra_session_t *ra_session1,
                                   const char *path1,
                                   svn_opt_revision_t *revision1,
                                   svn_ra_session_t *ra_session2,
                                   const char *path2,
                                   svn_opt_revision_t *revision2,
                                   apr_pool_t *pool)
{
  /* Resolve the revision numbers. */
  SVN_ERR(svn_client__get_revision_number
          (&range->start, ra_session1, revision1, path1, pool));
  SVN_ERR(svn_client__get_revision_number
          (&range->end, ra_session2, revision2, path2, pool));

  /* If comparing revisions from different URLs when doing a 3-way
     merge, there's no way to determine the merge type on the
     client-side from the peg revs of the URLs alone (history tracing
     would be required). */
  if (same_urls)
    {
      if (range->start < range->end)
        {
          *merge_type = merge_type_merge;
        }
      else if (range->start > range->end)
        {
          *merge_type = merge_type_rollback;
        }
      else  /* No revisions to merge. */
        {
          *merge_type = merge_type_no_op;
          range->start = range->end = SVN_INVALID_REVNUM;
        }
    }
  else 
    {
      *merge_type = merge_type_merge;
    }

  return SVN_NO_ERROR;
}

/* Default the values of REVISION1 and REVISION2 to be oldest rev at
   which ra_session's root got created and HEAD (respectively), if
   REVISION1 and REVISION2 are unspecified.  This assumed value is set
   at *ASSUMED_REVISION1 and *ASSUMED_REVISION2.  RA_SESSION is used
   to retrieve the revision of the current HEAD revision.  Use POOL
   for temporary allocations. */
static svn_error_t *
assume_default_rev_range(const svn_opt_revision_t *revision1,
                         svn_opt_revision_t *assumed_revision1,
                         const svn_opt_revision_t *revision2,
                         svn_opt_revision_t *assumed_revision2,
                         svn_ra_session_t *ra_session,
                         apr_pool_t *pool)
{
  svn_opt_revision_t head_rev_opt;
  svn_revnum_t head_revnum = SVN_INVALID_REVNUM;
  head_rev_opt.kind = svn_opt_revision_head;
  /* Provide reasonable defaults for unspecified revisions. */
  if (revision1->kind == svn_opt_revision_unspecified)
    {
      SVN_ERR(svn_client__get_revision_number(&head_revnum, ra_session,
                                              &head_rev_opt, "", pool));
      SVN_ERR(svn_client__oldest_rev_at_path(&assumed_revision1->value.number,
                                             ra_session, "",
                                             head_revnum,
                                             pool));
      if (SVN_IS_VALID_REVNUM(assumed_revision1->value.number))
        {
          assumed_revision1->kind = svn_opt_revision_number;
        }
    }
  else
    {
      *assumed_revision1 = *revision1;
    }
  if (revision2->kind == svn_opt_revision_unspecified)
    {
      if (SVN_IS_VALID_REVNUM(head_revnum))
        {
          assumed_revision2->value.number = head_revnum;
          assumed_revision2->kind = svn_opt_revision_number;
        }
      else
        {
          assumed_revision2->kind = svn_opt_revision_head;
        }
    }
  else
    {
      *assumed_revision2 = *revision2;
    }
  return SVN_NO_ERROR;
}

/* URL1/PATH1, URL2/PATH2, and TARGET_WCPATH all better be
   directories.  For the single file case, the caller does the merging
   manually.  PATH1 and PATH2 can be NULL.

   If PEG_REVISION is specified, then INITIAL_PATH2 is the path to peg
   off of, unless it is NULL, in which case INITIAL_URL2 is the peg
   path.  The actual two paths to compare are then found by tracing
   copy history from this peg path to INITIAL_REVISION2 and
   INITIAL_REVISION1.

   Handle DEPTH as documented for svn_client_merge3().

   CHILDREN_SW_OR_WITH_MERGEINFO may contain child paths (const char *) which
   are switched or which have mergeinfo which differs from that of the merge
   target root (ignored if empty or NULL). CHILDREN_SW_OR_WITH_MERGEINFO list
   should have entries sorted in depth first order as mandated by the reporter
   API. Because of this, we drive the diff editor in such a way that it avoids
   merging child paths when a merge is driven for their parent path.
*/
static svn_error_t *
do_merge(const char *initial_URL1,
         const char *initial_path1,
         const svn_opt_revision_t *initial_revision1,
         const char *initial_URL2,
         const char *initial_path2,
         const svn_opt_revision_t *initial_revision2,
         const svn_opt_revision_t *peg_revision,
         const char *target_wcpath,
         svn_wc_adm_access_t *adm_access,
         svn_depth_t depth,
         svn_boolean_t ignore_ancestry,
         const svn_wc_diff_callbacks2_t *callbacks,
         struct merge_cmd_baton *merge_b,
         apr_array_header_t *children_sw_or_with_mergeinfo,
         apr_pool_t *pool)
{
  apr_hash_t *target_mergeinfo;
  apr_array_header_t *remaining_ranges;
  svn_merge_range_t range;
  enum merge_type merge_type;
  svn_boolean_t is_rollback;
  svn_ra_session_t *ra_session, *ra_session2;
  const svn_ra_reporter3_t *reporter;
  void *report_baton;
  const svn_delta_editor_t *diff_editor;
  void *diff_edit_baton;
  svn_client_ctx_t *ctx = merge_b->ctx;
  notification_receiver_baton_t notify_b =
    { ctx->notify_func2, ctx->notify_baton2, TRUE, 0, 0, NULL, pool };
  const char *URL1, *URL2, *path1, *path2, *rel_path;
  svn_opt_revision_t *revision1, *revision2;
  const svn_wc_entry_t *entry;
  int i;
  svn_boolean_t indirect;
  svn_opt_revision_t assumed_initial_revision1, assumed_initial_revision2;
  apr_size_t target_count, merge_target_count;
  apr_pool_t *subpool;

  SVN_ERR(svn_wc__entry_versioned(&entry, target_wcpath, adm_access, FALSE,
                                  pool));

  /* Establish first RA session to initial_URL1. */
  SVN_ERR(svn_client__open_ra_session_internal(&ra_session, initial_URL1, NULL,
                                               NULL, NULL, FALSE, TRUE,
                                               ctx, pool));
  SVN_ERR(assume_default_rev_range(initial_revision1,
                                   &assumed_initial_revision1,
                                   initial_revision2,
                                   &assumed_initial_revision2,
                                   ra_session,
                                   pool));

  ENSURE_VALID_REVISION_KINDS(assumed_initial_revision1.kind,
                              assumed_initial_revision2.kind);


  /* If we are performing a pegged merge, we need to find out what our
     actual URLs will be. */
  if (peg_revision->kind != svn_opt_revision_unspecified)
    {
      SVN_ERR(svn_client__repos_locations(&URL1, &revision1,
                                          &URL2, &revision2,
                                          NULL,
                                          initial_path2 ? initial_path2
                                          : initial_URL2,
                                          peg_revision,
                                          &assumed_initial_revision1,
                                          &assumed_initial_revision2,
                                          ctx, pool));

      /* Reparent session if actual URL changed. */
      if (strcmp(initial_URL1, URL1))
        SVN_ERR(svn_ra_reparent(ra_session, URL1, pool));

      merge_b->url = URL2;
      path1 = NULL;
      path2 = NULL;
      merge_b->path = NULL;
    }
  else
    {
      URL1 = initial_URL1;
      URL2 = initial_URL2;
      path1 = initial_path1;
      path2 = initial_path2;
      revision1 = &assumed_initial_revision1;
      revision2 = &assumed_initial_revision2;
    }

  notify_b.same_urls = (strcmp(URL1, URL2) == 0);
  if (!notify_b.same_urls && merge_b->record_only)
    return svn_error_create(SVN_ERR_INCORRECT_PARAMS, NULL,
                            _("Use of two URLs is not compatible with "
                              "mergeinfo modification"));
  SVN_ERR(grok_range_info_from_opt_revisions(&range, &merge_type,
                                             notify_b.same_urls,
                                             ra_session, path1, revision1,
                                             ra_session, path2, revision2,
                                             pool));
  if (merge_type == merge_type_no_op)
    return SVN_NO_ERROR;

  /* Open a second session used to request individual file
     contents. Although a session can be used for multiple requests, it
     appears that they must be sequential. Since the first request, for
     the diff, is still being processed the first session cannot be
     reused. This applies to ra_neon, ra_local does not appears to have
     this limitation. */
  SVN_ERR(svn_client__open_ra_session_internal(&ra_session2, URL1, NULL,
                                               NULL, NULL, FALSE, TRUE,
                                               ctx, pool));

  if (notify_b.same_urls)
    {
      apr_array_header_t *requested_rangelist;

      SVN_ERR(get_wc_or_repos_mergeinfo(&target_mergeinfo, entry,
                                        &indirect, FALSE,
                                        svn_mergeinfo_inherited, ra_session,
                                        target_wcpath, adm_access,
                                        ctx, pool));

      is_rollback = (merge_type == merge_type_rollback);
      SVN_ERR(svn_client__path_relative_to_root(&rel_path, URL1, NULL,
                                                ra_session, adm_access, pool));

      /* When only recording mergeinfo, we don't perform an actual
         merge for the specified range. */
      if (merge_b->record_only)
        {
          if (merge_b->dry_run || !merge_b->same_repos)
            {
              return SVN_NO_ERROR;
            }
          else
            {
              apr_hash_t *merges;
              /* Blindly record the range specified by the user (rather
                 than refining it as we do for actual merges). */
              SVN_ERR(determine_merges_performed(&merges, target_wcpath,
                                                 &range, &notify_b, pool));

              /* If merge target has indirect mergeinfo set it. */
              if (indirect)
                SVN_ERR(svn_client__record_wc_mergeinfo(target_wcpath,
                                                        target_mergeinfo,
                                                        adm_access, pool));

              return update_wc_mergeinfo(target_wcpath, entry, rel_path,
                                         merges, is_rollback, adm_access,
                                         ctx, pool);
            }
        }

      /* Determine which of the requested ranges to consider merging... */
      SVN_ERR(calculate_requested_ranges(&requested_rangelist, &range, URL1,
                                         entry, adm_access, ctx, pool));
      /* ...and of those ranges, determine which ones actually still
         need merging. */
      SVN_ERR(calculate_merge_ranges(&remaining_ranges, rel_path,
                                     target_mergeinfo, requested_rangelist,
                                     is_rollback, pool));
    }
  else
    {
      /* HACK: Work around the fact that we don't yet take mergeinfo
         into account when performing 3-way merging with differing
         URLs by handling the merge in the style from pre-Merge
         Tracking. */
      /* ### TODO: Grab WC mergeinfo, push it to the server, and
         ### account for mergeinfo there before pulling down a patch
         ### to apply to the WC. */
        is_rollback = FALSE;
        remaining_ranges = apr_array_make(pool, 1, sizeof(&range));
        APR_ARRAY_PUSH(remaining_ranges, svn_merge_range_t *) = &range;
    }

  subpool = svn_pool_create(pool);

  /* Revisions from the requested range which have already been merged
     may create holes in range to merge.  Loop over the revision
     ranges we have left to merge, getting an editor for each range,
     and applying its delta. */
  for (i = 0; i < remaining_ranges->nelts; i++)
    {
      svn_wc_notify_t *notify;

      /* When using this merge range, account for the exclusivity of
         its low value (which is indicated by this operation being a
         merge vs. revert). */
      svn_merge_range_t *r = APR_ARRAY_IDX(remaining_ranges, i,
                                           svn_merge_range_t *);

      svn_pool_clear(subpool);

      notify = svn_wc_create_notify(target_wcpath, svn_wc_notify_merge_begin,
                                    subpool);
      notify->merge_range = r;
      notification_receiver(&notify_b, notify, subpool);

      /* We must avoid subsequent merges to files which are already in
         conflict, as subsequent merges might overlap with the
         conflict markers in the file (or worse, be completely inside
         them). */
      /* ### TODO: Drill code to avoid merges for files which are
         ### already in conflict down into the API which requests or
         ### applies the diff. */

      SVN_ERR(svn_client__get_diff_editor(target_wcpath,
                                          adm_access,
                                          callbacks,
                                          merge_b,
                                          depth,
                                          merge_b->dry_run,
                                          ra_session2,
                                          r->start,
                                          notification_receiver,
                                          &notify_b,
                                          ctx->cancel_func,
                                          ctx->cancel_baton,
                                          &diff_editor,
                                          &diff_edit_baton,
                                          subpool));

      SVN_ERR(svn_ra_do_diff3(ra_session,
                              &reporter, &report_baton,
                              r->end,
                              "",
                              depth,
                              ignore_ancestry,
                              TRUE,  /* text_deltas */
                              URL2,
                              diff_editor, diff_edit_baton, subpool));

      SVN_ERR(reporter->set_path(report_baton, "",
                                 r->start,
                                 depth, FALSE, NULL, subpool));
      if (notify_b.same_urls &&
          children_sw_or_with_mergeinfo &&
          children_sw_or_with_mergeinfo->nelts > 0)
        {
          /* Describe children with mergeinfo overlapping this merge
             operation such that no diff is retrieved for them from
             the repository. */
          apr_size_t target_wcpath_len = strlen(target_wcpath);
          int j;
          for (j = 0; j < children_sw_or_with_mergeinfo->nelts; j++)
            {
              const char *child_repos_path;
              const char *child_wcpath;

              child_wcpath = APR_ARRAY_IDX(children_sw_or_with_mergeinfo, j,
                                           const char *);
              /* svn_path_is_ancestor returns true if paths are same,
                 so make sure paths are not same. */
              if (svn_path_is_ancestor(target_wcpath, child_wcpath) &&
                  strcmp(child_wcpath, target_wcpath) != 0)
                {
                  child_repos_path = child_wcpath +
                    (target_wcpath_len ? target_wcpath_len + 1 : 0);
                  SVN_ERR(reporter->set_path(report_baton, child_repos_path,
                                             r->end,
                                             depth, FALSE, NULL, subpool));
                }
            }
        }

      /* ### TODO: Print revision range separator line. */

      SVN_ERR(reporter->finish_report(report_baton, subpool));

      /* ### LATER: Give the caller a shot at resolving any conflicts
         ### we've detected.  If the conflicts are not resolved, abort
         ### application of any remaining revision ranges for this WC
         ### item. */

      if (notify_b.same_urls)
        {
          if (!merge_b->dry_run && merge_b->same_repos)
            {
              /* Update the WC mergeinfo here to account for our new
                 merges, minus any unresolved conflicts and skips. */
              apr_hash_t *merges;
              SVN_ERR(determine_merges_performed(&merges, target_wcpath, r,
                                                 &notify_b, subpool));

              /* If merge target has indirect mergeinfo set it before
                 recording the first merge range. */
              if (i == 0 && indirect)
                SVN_ERR(svn_client__record_wc_mergeinfo(target_wcpath,
                                                        target_mergeinfo,
                                                        adm_access, subpool));

              SVN_ERR(update_wc_mergeinfo(target_wcpath, entry, rel_path,
                                          merges, is_rollback, adm_access,
                                          ctx, subpool));
            }

          /* Clear the notification counter and list of skipped paths
             in preparation for the next revision range merge. */
          notify_b.nbr_notifications = 0;
          if (notify_b.skipped_paths != NULL)
            svn_hash__clear(notify_b.skipped_paths);
        }
    }

  apr_pool_destroy(subpool);

  /* MERGE_B->TARGET hasn't been merged yet so only elide as
     far MERGE_B->TARGET's immediate children.  If TARGET_WCPATH
     is an immdediate child of MERGE_B->TARGET don't even attempt to
     elide since TARGET_WCPATH can't elide to itself. */
  if (!merge_b->dry_run)
    {
      target_count = svn_path_component_count(target_wcpath);
      merge_target_count = svn_path_component_count(merge_b->target);

      if (target_count - merge_target_count > 1)
        {
          svn_stringbuf_t *elision_limit_path =
            svn_stringbuf_create(target_wcpath, pool);
          svn_path_remove_components(elision_limit_path,
                                     target_count - merge_target_count - 1);
          SVN_ERR(svn_client__elide_mergeinfo(target_wcpath,
                                              elision_limit_path->data, entry,
                                              adm_access, ctx, pool));
        }
    }

  /* Sleep to ensure timestamp integrity. */
  svn_sleep_for_timestamps();

  return SVN_NO_ERROR;
}


/* Get REVISION of the file at URL.  SOURCE is a path that refers to that 
   file's entry in the working copy, or NULL if we don't have one.  Return in 
   *FILENAME the name of a file containing the file contents, in *PROPS a hash 
   containing the properties and in *REV the revision.  All allocation occurs 
   in POOL. */
static svn_error_t *
single_file_merge_get_file(const char **filename,
                           svn_ra_session_t *ra_session,
                           apr_hash_t **props,
                           svn_revnum_t rev,
                           const char *url,
                           const char *wc_target,
                           apr_pool_t *pool)
{
  apr_file_t *fp;
  svn_stream_t *stream;

  /* ### Create this temporary file under .svn/tmp/ instead of next to
     ### the working file.*/
  SVN_ERR(svn_io_open_unique_file2(&fp, filename,
                                   wc_target, ".tmp",
                                   svn_io_file_del_none, pool));
  stream = svn_stream_from_aprfile2(fp, FALSE, pool);
  SVN_ERR(svn_ra_get_file(ra_session, "", rev,
                          stream, NULL, props, pool));
  SVN_ERR(svn_stream_close(stream));

  return SVN_NO_ERROR;
}


/* Send a notification specific to a single-file merge. */
static APR_INLINE void
single_file_merge_notify(void *notify_baton, const char *target_wcpath,
                         svn_wc_notify_action_t action,
                         svn_wc_notify_state_t text_state,
                         svn_wc_notify_state_t prop_state, apr_pool_t *pool)
{
  svn_wc_notify_t *notify = svn_wc_create_notify(target_wcpath, action, pool);
  notify->kind = svn_node_file;
  notify->content_state = text_state;
  notify->prop_state = prop_state;
  if (notify->content_state == svn_wc_notify_state_missing)
    notify->action = svn_wc_notify_skip;
  notification_receiver(notify_baton, notify, pool);
}


/* The single-file, simplified version of do_merge. */
static svn_error_t *
do_single_file_merge(const char *initial_URL1,
                     const char *initial_path1,
                     const svn_opt_revision_t *initial_revision1,
                     const char *initial_URL2,
                     const char *initial_path2,
                     const svn_opt_revision_t *initial_revision2,
                     const svn_opt_revision_t *peg_revision,
                     const char *target_wcpath,
                     svn_wc_adm_access_t *adm_access,
                     struct merge_cmd_baton *merge_b,
                     svn_boolean_t ignore_ancestry,
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
  notification_receiver_baton_t notify_b =
    { ctx->notify_func2, ctx->notify_baton2, TRUE, 0, 0, NULL, pool };
  const char *URL1, *path1, *URL2, *path2, *rel_path;
  svn_opt_revision_t *revision1, *revision2;
  svn_error_t *err;
  svn_merge_range_t range;
  svn_ra_session_t *ra_session1, *ra_session2;
  enum merge_type merge_type;
  svn_boolean_t is_rollback;
  apr_hash_t *target_mergeinfo;
  const svn_wc_entry_t *entry;
  int i;
  svn_boolean_t indirect = FALSE, is_replace = FALSE;
  apr_size_t target_count, merge_target_count;
  svn_opt_revision_t assumed_initial_revision1, assumed_initial_revision2;
  apr_pool_t *subpool;

  SVN_ERR(svn_wc__entry_versioned(&entry, target_wcpath, adm_access, FALSE,
                                  pool));

  /* Establish first RA session to URL1. */
  SVN_ERR(svn_client__open_ra_session_internal(&ra_session1, initial_URL1, 
                                               NULL, NULL, NULL, FALSE, TRUE,
                                               ctx, pool));
  SVN_ERR(assume_default_rev_range(initial_revision1,
                                   &assumed_initial_revision1,
                                   initial_revision2,
                                   &assumed_initial_revision2,
                                   ra_session1,
                                   pool));

  ENSURE_VALID_REVISION_KINDS(assumed_initial_revision1.kind,
                              assumed_initial_revision2.kind);

  /* If we are performing a pegged merge, we need to find out what our
     actual URLs will be. */
  if (peg_revision->kind != svn_opt_revision_unspecified)
    {
      /* ### FIXME: This dies with SVN_ERR_CLIENT_UNRELATED_RESOURCES
         ### for the record_only case when one of the URLs doesn't
         ### exist at the specified revision.  This can occur
         ### (validly) during an attempt to wipe the memory of the
         ### implied mergeinfo for a copy. */
      err = svn_client__repos_locations(&URL1, &revision1,
                                        &URL2, &revision2,
                                        NULL,
                                        initial_path2 ? initial_path2
                                                      : initial_URL2,
                                        peg_revision,
                                        &assumed_initial_revision1,
                                        &assumed_initial_revision2,
                                        ctx, pool);
      if (err)
        {
          /* ### Do something like this, perhaps? */
          if (merge_b->record_only &&
              err->apr_err == SVN_ERR_CLIENT_UNRELATED_RESOURCES)
            svn_error_clear(err);
          else
            return err;
        }

      merge_b->url = URL2;
      merge_b->path = NULL;
      path1 = NULL;
      path2 = NULL;
    }
  /* If we are not performing a pegged merge, but we also are not
     ignoring ancestry, then we need to check the relationship between
     the two sides of our merge. */
  else if (! ignore_ancestry)
    {
      const char *location_url;
      svn_opt_revision_t unspecified_revision, *location_rev;
      unspecified_revision.kind = svn_opt_revision_unspecified;

      /* Try to locate the left side of the merge location by tracing the
         history of right side.  We do this only do verify that one of
         these locations is an ancestor of the other. */
      err = svn_client__repos_locations(&location_url, &location_rev,
                                        NULL, NULL,
                                        NULL,
                                        initial_path2 ? initial_path2
                                                      : initial_URL2,
                                        &assumed_initial_revision2,
                                        &assumed_initial_revision1,
                                        &unspecified_revision,
                                        ctx, pool);

      /* If the two sides don't have an ancestral relationship, that's
         okay.  But because we are preserving ancestry, we have to
         treat a merge across those locations as a deletion of the one
         and addition of the other. */
      if (err && err->apr_err == SVN_ERR_CLIENT_UNRELATED_RESOURCES)
        {
          is_replace = TRUE;
          svn_error_clear(err);
          err = SVN_NO_ERROR;
        }
      SVN_ERR(err);
      
      /* ### FIXME: We need to actual do some relationship checks. */
      URL1 = initial_URL1;
      URL2 = initial_URL2;
      path1 = initial_path1;
      path2 = initial_path2;
      revision1 = &assumed_initial_revision1;
      revision2 = &assumed_initial_revision2;
    }
  /* And if we aren't performing a pegged merge but we are ignoring
     ancestry, then we don't care about the relationships between the
     sides of our merge source, and happily accept them as they are. */
  else
    {
      URL1 = initial_URL1;
      URL2 = initial_URL2;
      path1 = initial_path1;
      path2 = initial_path2;
      revision1 = &assumed_initial_revision1;
      revision2 = &assumed_initial_revision2;
    }
 
  /* reparent RA session to URL1. */
  SVN_ERR(svn_ra_reparent(ra_session1, URL1, pool));
  /* Establish RA session to URL2. */
  SVN_ERR(svn_client__open_ra_session_internal(&ra_session2, URL2, NULL,
                                               NULL, NULL, FALSE, TRUE,
                                               ctx, pool));

  notify_b.same_urls = (strcmp(URL1, URL2) == 0);
  if (!notify_b.same_urls && merge_b->record_only)
    return svn_error_create(SVN_ERR_INCORRECT_PARAMS, NULL,
                            _("Use of two URLs is not compatible with "
                              "mergeinfo modification"));
  SVN_ERR(grok_range_info_from_opt_revisions(&range, &merge_type,
                                             notify_b.same_urls,
                                             ra_session1, path1, revision1,
                                             ra_session2, path2, revision2,
                                             pool));
  if (notify_b.same_urls)
    {
      apr_array_header_t *requested_rangelist;

      if (merge_type == merge_type_no_op)
        return SVN_NO_ERROR;

      SVN_ERR(get_wc_or_repos_mergeinfo(&target_mergeinfo, entry,
                                        &indirect, FALSE,
                                        svn_mergeinfo_inherited, ra_session1,
                                        target_wcpath, adm_access,
                                        ctx, pool));

      is_rollback = (merge_type == merge_type_rollback);
      SVN_ERR(svn_client__path_relative_to_root(&rel_path, URL1, NULL,
                                                ra_session1, adm_access,
                                                pool));
      /* When only recording mergeinfo, we don't perform an actual
         merge for the specified range. */
      if (merge_b->record_only)
        {
          if (merge_b->dry_run || !merge_b->same_repos)
            {
              return SVN_NO_ERROR;
            }
          else
            {
              /* Blindly record the range specified by the user (rather
                 than refining it as we do for actual merges). */
              apr_hash_t *merges;
              SVN_ERR(determine_merges_performed(&merges, target_wcpath,
                                                 &range, &notify_b, pool));

              /* If merge target has indirect mergeinfo set it. */
              if (indirect)
                SVN_ERR(svn_client__record_wc_mergeinfo(target_wcpath,
                                                        target_mergeinfo,
                                                        adm_access, pool));

              return update_wc_mergeinfo(target_wcpath, entry, rel_path,
                                         merges, is_rollback, adm_access,
                                         ctx, pool);
            }
        }

      /* Determine which of the requested ranges to consider merging... */
      SVN_ERR(calculate_requested_ranges(&requested_rangelist, &range, URL1,
                                         entry, adm_access, ctx, pool));
      /* ...and of those ranges, determine which ones actually still
         need merging. */
      SVN_ERR(calculate_merge_ranges(&remaining_ranges, rel_path,
                                     target_mergeinfo, requested_rangelist,
                                     is_rollback, pool));
    }
  else
    {
        is_rollback = FALSE;
        remaining_ranges = apr_array_make(pool, 1, sizeof(&range));
        APR_ARRAY_PUSH(remaining_ranges, svn_merge_range_t *) = &range;
    }

  subpool = svn_pool_create(pool);

  for (i = 0; i < remaining_ranges->nelts; i++)
    {
      svn_wc_notify_t *n;

      /* When using this merge range, account for the exclusivity of
         its low value (which is indicated by this operation being a
         merge vs. revert). */
      svn_merge_range_t *r = APR_ARRAY_IDX(remaining_ranges, i,
                                           svn_merge_range_t *);

      svn_pool_clear(subpool);

      n = svn_wc_create_notify(target_wcpath,
                               svn_wc_notify_merge_begin,
                               subpool);
      n->merge_range = r;
      notification_receiver(&notify_b, n, subpool);

      /* While we currently don't allow it, in theory we could be
         fetching two fulltexts from two different repositories here. */
      SVN_ERR(single_file_merge_get_file(&tmpfile1, ra_session1, &props1, 
                                         r->start,
                                         URL1, target_wcpath, subpool));

      /* ### CMP ### */
      if (is_replace)
        SVN_ERR(svn_ra_reparent(ra_session2, URL1, pool));
      SVN_ERR(single_file_merge_get_file(&tmpfile2, ra_session2, &props2, 
                                         r->end,
                                         URL2, target_wcpath, subpool));

      /* Discover any svn:mime-type values in the proplists */
      pval = apr_hash_get(props1, SVN_PROP_MIME_TYPE,
                          strlen(SVN_PROP_MIME_TYPE));
      mimetype1 = pval ? pval->data : NULL;

      pval = apr_hash_get(props2, SVN_PROP_MIME_TYPE,
                          strlen(SVN_PROP_MIME_TYPE));
      mimetype2 = pval ? pval->data : NULL;

      /* Deduce property diffs. */
      SVN_ERR(svn_prop_diffs(&propchanges, props2, props1, subpool));

      if (is_replace) 
        {
          SVN_ERR(merge_file_deleted(adm_access,
                                     &text_state,
                                     target_wcpath,
                                     NULL,
                                     NULL,
                                     mimetype1, mimetype2,
                                     props1,
                                     merge_b));
          single_file_merge_notify(&notify_b, target_wcpath,
                                   svn_wc_notify_update_delete, text_state,
                                   svn_wc_notify_state_unknown, subpool);

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
          single_file_merge_notify(&notify_b, target_wcpath,
                                   svn_wc_notify_update_add, text_state,
                                   prop_state, subpool);
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
          single_file_merge_notify(&notify_b, target_wcpath,
                                   svn_wc_notify_update_update, text_state,
                                   prop_state, subpool);
        }

      /* Ignore if temporary file not found. It may have been renamed. */
      err = svn_io_remove_file(tmpfile1, subpool);
      if (err && ! APR_STATUS_IS_ENOENT(err->apr_err))
        return err;
      svn_error_clear(err);
      err = svn_io_remove_file(tmpfile2, subpool);
      if (err && ! APR_STATUS_IS_ENOENT(err->apr_err))
        return err;
      svn_error_clear(err);
  
      /* ### LATER: Give the caller a shot at resolving any conflicts
         ### we've detected.  If the conflicts are not resolved, abort
         ### application of any remaining revision ranges for this WC
         ### item. */

      if (notify_b.same_urls)
        {
          if (!merge_b->dry_run && merge_b->same_repos)
            {
              /* Update the WC mergeinfo here to account for our new
                 merges, minus any unresolved conflicts and skips. */
              apr_hash_t *merges;
              SVN_ERR(determine_merges_performed(&merges, target_wcpath, r,
                                                 &notify_b, subpool));

              /* If merge target has indirect mergeinfo set it before
                 recording the first merge range. */
              if (i == 0 && indirect)
                SVN_ERR(svn_client__record_wc_mergeinfo(target_wcpath,
                                                        target_mergeinfo,
                                                        adm_access, subpool));

              SVN_ERR(update_wc_mergeinfo(target_wcpath, entry, rel_path,
                                          merges, is_rollback, adm_access,
                                          ctx, subpool));
            }

          /* Clear the notification counter and list of skipped paths
             in preparation for the next revision range merge. */
          notify_b.nbr_notifications = 0;
          if (notify_b.skipped_paths != NULL)
            svn_hash__clear(notify_b.skipped_paths);
        }
    }

  apr_pool_destroy(subpool);

  /* MERGE_B->TARGET hasn't been merged yet so only elide as
     far MERGE_B->TARGET's immediate children.  If TARGET_WCPATH
     is an immdediate child of MERGE_B->TARGET don't even attempt to
     elide since TARGET_WCPATH can't elide to itself. */
  if (!merge_b->dry_run)
    {
      target_count = svn_path_component_count(target_wcpath);
      merge_target_count = svn_path_component_count(merge_b->target);

      if (target_count - merge_target_count > 1)
        {
          svn_stringbuf_t *elision_limit_path =
            svn_stringbuf_create(target_wcpath, pool);
          svn_path_remove_components(elision_limit_path,
                                     target_count - merge_target_count - 1);
          SVN_ERR(svn_client__elide_mergeinfo(target_wcpath,
                                              elision_limit_path->data, entry,
                                              adm_access, ctx, pool));
        }
    }
  /* Sleep to ensure timestamp integrity. */
  svn_sleep_for_timestamps();

  return SVN_NO_ERROR;
}

/* A baton for get_sw_mergeinfo_walk_cb. */
struct get_sw_mergeinfo_walk_baton
{
  /* Access for the tree being walked. */
  svn_wc_adm_access_t *base_access;
  /* Array of paths that have explicit mergeinfo and/or are switched. */
  apr_array_header_t *children_sw_or_with_mergeinfo;
};


/* svn_wc_entry_callbacks2_t found_entry() callback for
   get_sw_mergeinfo_paths.

   Given PATH, its corresponding ENTRY, and WB, where WB is the WALK_BATON
   of type "struct get_sw_mergeinfo_walk_baton *":  If PATH is switched or
   has explicit working svn:mergeinfo set on it, then make a copy of *PATH,
   allocated in WB->CHILDREN_SW_OR_WITH_MERGEINFO->POOL, and push the copy
   into to the WB->CHILDREN_SW_OR_WITH_MERGEINFO array. */
static svn_error_t *
get_sw_mergeinfo_walk_cb(const char *path,
                         const svn_wc_entry_t *entry,
                         void *walk_baton,
                         apr_pool_t *pool)
{
  struct get_sw_mergeinfo_walk_baton *wb = walk_baton;
  const svn_string_t *propval;
  svn_boolean_t switched = FALSE;

  /* We're going to receive dirents twice;  we want to ignore the
     first one (where it's a child of a parent dir), and only use
     the second one (where we're looking at THIS_DIR).  */
  if ((entry->kind == svn_node_dir)
      && (strcmp(entry->name, SVN_WC_ENTRY_THIS_DIR) != 0))
    return SVN_NO_ERROR;

  /* Ignore the entry if it does not exist at the time of interest. */
  if (entry->schedule == svn_wc_schedule_delete)
    return SVN_NO_ERROR;

  SVN_ERR(svn_wc_prop_get(&propval, SVN_PROP_MERGE_INFO, path,
                          wb->base_access, pool));

  /* If PATH doesn't have mergeinfo see if it is switched. */
  if (!propval)
    SVN_ERR(svn_wc__path_switched(path, &switched, entry, pool));

  /* Store PATHs with mergeinfo and/or which are switched. */
  if (propval || switched)
    {
      path = apr_pstrdup(wb->children_sw_or_with_mergeinfo->pool, path);
      APR_ARRAY_PUSH(wb->children_sw_or_with_mergeinfo, const char *) = path;
    }  

  return SVN_NO_ERROR;
}

/* svn_wc_entry_callbacks2_t handle_error() callback for
   get_sw_mergeinfo_paths.

   Squelch ERR by returning SVN_NO_ERROR if ERR is caused by a missing
   path (i.e. SVN_ERR_WC_PATH_NOT_FOUND). */
static svn_error_t *
get_sw_mergeinfo_error_handler(const char *path,
                               svn_error_t *err,
                               void *walk_baton,
                               apr_pool_t *pool)
{
  if (svn_error_root_cause_is(err, SVN_ERR_WC_PATH_NOT_FOUND))
    {
      svn_error_clear(err);
      return SVN_NO_ERROR;
    }
  else
    {
      return err;
    }
}

/* Helper for discover_and_merge_children()

   Perform a depth first walk of the working copy tree rooted at TARGET (with
   the corresponding ENTRY).  Place any path which has working svn:mergeinfo,
   or is switched, in CHILDREN_SW_OR_WITH_MERGEINFO. */
static svn_error_t *
get_sw_mergeinfo_paths(apr_array_header_t *children_sw_or_with_mergeinfo,
                       const char *target,
                       const svn_wc_entry_t *entry,
                       svn_wc_adm_access_t *adm_access,
                       svn_client_ctx_t *ctx,
                       apr_pool_t *pool)
{
  static const svn_wc_entry_callbacks2_t walk_callbacks =
    { get_sw_mergeinfo_walk_cb, get_sw_mergeinfo_error_handler };
  struct get_sw_mergeinfo_walk_baton wb =
    { adm_access, children_sw_or_with_mergeinfo };

  if (entry->kind == svn_node_file)
    SVN_ERR(walk_callbacks.found_entry(target, entry, &wb, pool));
  else
    SVN_ERR(svn_wc_walk_entries3(target, adm_access, &walk_callbacks, &wb,
                                 FALSE, ctx->cancel_func, ctx->cancel_baton,
                                 pool));
  return SVN_NO_ERROR;
}

/* Fill *CHILDREN_SW_OR_WITH_MERGEINFO with child paths (const char *) which
   might have intersecting merges because they have explicit working
   svn:mergeinfo and/or are switched. Here the paths are arranged in a depth
   first order. For each such child, call do_merge() or do_single_file_merge()
   with the appropriate arguments (based on the type of child).  Use
   PARENT_ENTRY and ADM_ACCESS to fill CHILDREN_SW_OR_WITH_MERGEINFO.
   Cascade PARENT_WC_URL, INITIAL_PATH1, REVISION1, INITIAL_PATH2,
   REVISION2, PEG_REVISION, DEPTH, IGNORE_ANCESTRY, ADM_ACCESS, and
   MERGE_CMD_BATON to do_merge() and do_single_file_merge().  All
   allocation occurs in POOL.
   
   Note that any paths in CHILDREN_SW_OR_WITH_MERGEINFO which were switched
   but had no explicit working mergeinfo at the start of the call, will have
   some at the end as a result of do_merge() and/or do_single_file_merge. */
static svn_error_t *
discover_and_merge_children(apr_array_header_t **children_sw_or_with_mergeinfo,
                            const svn_wc_entry_t *parent_entry,
                            const char *parent_wc_url,
                            const char *initial_path1,
                            const svn_opt_revision_t *revision1,
                            const char *initial_path2,
                            const svn_opt_revision_t *revision2,
                            const svn_opt_revision_t *peg_revision,
                            svn_depth_t depth,
                            svn_boolean_t ignore_ancestry,
                            svn_wc_adm_access_t *adm_access,
                            struct merge_cmd_baton *merge_cmd_baton,
                            apr_pool_t *pool)
{
  const svn_wc_entry_t *child_entry;
  int merge_target_len = strlen(merge_cmd_baton->target);
  int i;

  *children_sw_or_with_mergeinfo = apr_array_make(pool, 0,
                                                  sizeof(const char *));
  SVN_ERR(get_sw_mergeinfo_paths(*children_sw_or_with_mergeinfo,
                                 merge_cmd_baton->target, parent_entry,
                                 adm_access, merge_cmd_baton->ctx, pool));

  for (i = 0; i < (*children_sw_or_with_mergeinfo)->nelts; i++)
    {
      const char *child_repos_path;
      const char *child_wcpath;
      const char *child_url;

      child_wcpath = APR_ARRAY_IDX(*children_sw_or_with_mergeinfo, i,
                                   const char *);
      if (strcmp(child_wcpath, merge_cmd_baton->target) == 0)
        continue;
      SVN_ERR(svn_wc__entry_versioned(&child_entry, child_wcpath, adm_access,
                                      FALSE, pool));
      child_repos_path = child_wcpath +
        (merge_target_len ? merge_target_len + 1 : 0);
      child_url = svn_path_join(parent_wc_url, child_repos_path, pool);
      if (child_entry->kind == svn_node_file)
        {
          SVN_ERR(do_single_file_merge(child_url, initial_path1, revision1,
                                       child_url, initial_path2, revision2,
                                       peg_revision,
                                       child_wcpath,
                                       adm_access,
                                       merge_cmd_baton,
                                       ignore_ancestry,
                                       pool));
        }
      else if (child_entry->kind == svn_node_dir)
        {
          SVN_ERR(do_merge(child_url,
                           initial_path1,
                           revision1,
                           child_url,
                           initial_path2,
                           revision2,
                           peg_revision,
                           child_wcpath,
                           adm_access,
                           depth,
                           ignore_ancestry,
                           &merge_callbacks,
                           merge_cmd_baton,
                           *children_sw_or_with_mergeinfo,
                           pool));
        }
    }
  return SVN_NO_ERROR;
}

/* Return whether the merge source (SRC_URL) is from a different
   repository from the merge target (ENTRY), to avoid later
   erroneously setting mergeinfo on the target. */
static APR_INLINE svn_error_t *
from_same_repos(struct merge_cmd_baton *merge_cmd_baton, const char *src_url,
                const svn_wc_entry_t *entry, svn_client_ctx_t *ctx,
                apr_pool_t *pool)
{
  if (merge_cmd_baton->dry_run)
    {
      merge_cmd_baton->same_repos = FALSE;  
    }
  else
    {
      const char *src_root;
      svn_ra_session_t *ra_session;
      SVN_ERR(svn_client__open_ra_session_internal(&ra_session, src_url, NULL,
                                                   NULL, NULL, FALSE, TRUE,
                                                   ctx, pool));
      SVN_ERR(svn_ra_get_repos_root(ra_session, &src_root, pool));
      merge_cmd_baton->same_repos = svn_path_is_ancestor(src_root,
                                                         entry->repos);
    }
  return SVN_NO_ERROR;
}

/*-----------------------------------------------------------------------*/

/*** Public APIs. ***/

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
  struct merge_cmd_baton merge_cmd_baton;
  const char *URL1, *URL2;
  const char *path1, *path2;
  svn_opt_revision_t peg_revision;
  apr_array_header_t *children_with_mergeinfo;

  /* This is not a pegged merge. */
  peg_revision.kind = svn_opt_revision_unspecified;

  /* If source1 or source2 are paths, we need to get the underlying URL
   * from the wc and save the initial path we were passed so we can use it as 
   * a path parameter (either in the baton or not).  otherwise, the path 
   * will just be NULL, which means we won't be able to figure out some kind 
   * of revision specifications, but in that case it won't matter, because 
   * those ways of specifying a revision are meaningless for a url.
   */
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

  if (URL1 == source1)
    path1 = NULL;
  else
    path1 = source1;

  if (URL2 == source2)
    path2 = NULL;
  else
    path2 = source2;

  SVN_ERR(svn_wc_adm_probe_open3(&adm_access, NULL, target_wcpath,
                                 ! dry_run,
                                 SVN_DEPTH_TO_RECURSE(depth) ? -1 : 0,
                                 ctx->cancel_func, ctx->cancel_baton,
                                 pool));

  SVN_ERR(svn_wc__entry_versioned(&entry, target_wcpath, adm_access, FALSE,
                                 pool));

  if (depth == svn_depth_unknown)
    depth = entry->depth;

  merge_cmd_baton.force = force;
  merge_cmd_baton.record_only = record_only;
  merge_cmd_baton.dry_run = dry_run;
  merge_cmd_baton.merge_options = merge_options;
  merge_cmd_baton.target = target_wcpath;
  merge_cmd_baton.url = URL2;
  merge_cmd_baton.revision = revision2;
  merge_cmd_baton.path = path2;
  merge_cmd_baton.added_path = NULL;
  merge_cmd_baton.add_necessitated_merge = FALSE;
  merge_cmd_baton.dry_run_deletions = (dry_run ? apr_hash_make(pool) : NULL);
  merge_cmd_baton.ctx = ctx;
  /* No need to check URL2, since if it's from a different repository
     than URL1, then the whole merge will fail anyway. */
  SVN_ERR(from_same_repos(&merge_cmd_baton, URL1, entry, ctx, pool));
  merge_cmd_baton.pool = pool;

  /* Set up the diff3 command, so various callers don't have to. */
  {
    svn_config_t *cfg =
      ctx->config ? apr_hash_get(ctx->config, SVN_CONFIG_CATEGORY_CONFIG,
                                 APR_HASH_KEY_STRING) : NULL;

    svn_config_get(cfg, &(merge_cmd_baton.diff3_cmd),
                   SVN_CONFIG_SECTION_HELPERS,
                   SVN_CONFIG_OPTION_DIFF3_CMD, NULL);
  }

  /* If our target_wcpath is a single file, assume that PATH1 and
     PATH2 are files as well, and do a single-file merge. */
  if (entry->kind == svn_node_file)
    {
      SVN_ERR(do_single_file_merge(URL1, path1, revision1,
                                   URL2, path2, revision2,
                                   &peg_revision,
                                   target_wcpath,
                                   adm_access,
                                   &merge_cmd_baton,
                                   ignore_ancestry,
                                   pool));
    }

  /* Otherwise, this must be a directory merge.  Do the fancy
     recursive diff-editor thing. */
  else if (entry->kind == svn_node_dir)
    {
      if (strcmp(URL1, URL2) == 0)
        {
          /* Merge children with differing mergeinfo. */
          SVN_ERR(discover_and_merge_children(&children_with_mergeinfo,
                                              entry,
                                              URL1,
                                              path1,
                                              revision1,
                                              merge_cmd_baton.path,
                                              revision2,
                                              &peg_revision,
                                              depth,
                                              ignore_ancestry,
                                              adm_access,
                                              &merge_cmd_baton,
                                              pool));
        }
      else
        {
          children_with_mergeinfo = NULL;
        }

      /* Merge of the actual target.*/
      SVN_ERR(do_merge(URL1,
                       path1,
                       revision1,
                       URL2,
                       merge_cmd_baton.path,
                       revision2,
                       &peg_revision,
                       target_wcpath,
                       adm_access,
                       depth,
                       ignore_ancestry,
                       &merge_callbacks,
                       &merge_cmd_baton,
                       children_with_mergeinfo,
                       pool));

      /* The merge of the actual target is complete.  See if the target's
         immediate children's mergeinfo elides to the target. */
      if (!dry_run)
        SVN_ERR(elide_children(children_with_mergeinfo, target_wcpath,
                               entry, adm_access, ctx, pool));
    }

  /* The final mergeinfo on TARGET_WCPATH may itself elide. */
  if (!dry_run)
    SVN_ERR(svn_client__elide_mergeinfo(target_wcpath, NULL, entry,
                                        adm_access, ctx, pool));

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
                           target_wcpath, SVN_DEPTH_FROM_RECURSE(recurse),
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

svn_error_t *
svn_client_merge_peg3(const char *source,
                      const svn_opt_revision_t *revision1,
                      const svn_opt_revision_t *revision2,
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
  struct merge_cmd_baton merge_cmd_baton;
  const char *URL;
  const char *path = NULL;
  apr_array_header_t *children_with_mergeinfo;

  if (source)
    {
      /* If source is a path, we need to get the underlying URL from
         the wc and save the initial path we were passed so we can use
         it as a path parameter (either in the baton or not).
         otherwise, the path will just be NULL, which means we won't
         be able to figure out some kind of revision specifications,
         but in that case it won't matter, because those ways of
         specifying a revision are meaningless for a URL. */
      SVN_ERR(svn_client_url_from_path(&URL, source, pool));
      if (! URL)
        return svn_error_createf(SVN_ERR_ENTRY_MISSING_URL, NULL,
                                 _("'%s' has no URL"),
                                 svn_path_local_style(source, pool));
      if (URL != source)
        path = source;
    }
  else
    {
      /* If a merge source was not specified, try to derive it. */
      apr_array_header_t *suggested_sources;
      svn_revnum_t rev;
      svn_opt_revision_t target_revision;
      const char *repos_root;
      svn_ra_session_t *ra_session;
      const char *target_url;
      target_revision.kind = svn_opt_revision_working;
      SVN_ERR(svn_client__suggest_merge_sources(target_wcpath,
                                                &target_revision,
                                                &suggested_sources,
                                                ctx, pool));
      if (! suggested_sources->nelts)
        return svn_error_createf(SVN_ERR_INCORRECT_PARAMS, NULL,
                                 _("Unable to determine merge source for "
                                   "'%s', please provide an explicit source"),
                                 svn_path_local_style(target_wcpath, pool));

      /* Prepend the repository root path to the copy source path. */

      /* ### TODO: Try something cheaper than creating a RA session. */
      SVN_ERR(svn_client__ra_session_from_path(&ra_session,
                                               &rev,
                                               &target_url,
                                               target_wcpath,
                                               &target_revision,
                                               &target_revision,
                                               ctx,
                                               pool));
      SVN_ERR(svn_ra_get_repos_root(ra_session, &repos_root, pool));
      URL = apr_pstrcat(pool, repos_root,
                        APR_ARRAY_IDX(suggested_sources, 0, char *),
                        NULL);
    }

  SVN_ERR(svn_wc_adm_probe_open3(&adm_access, NULL, target_wcpath,
                                 ! dry_run,
                                 SVN_DEPTH_TO_RECURSE(depth) ? -1 : 0,
                                 ctx->cancel_func, ctx->cancel_baton,
                                 pool));

  SVN_ERR(svn_wc__entry_versioned(&entry, target_wcpath, adm_access, FALSE,
                                 pool));

  if (depth == svn_depth_unknown)
    depth = entry->depth;

  merge_cmd_baton.force = force;
  merge_cmd_baton.record_only = record_only;
  merge_cmd_baton.dry_run = dry_run;
  merge_cmd_baton.merge_options = merge_options;
  merge_cmd_baton.target = target_wcpath;
  merge_cmd_baton.url = URL;
  merge_cmd_baton.revision = revision2;
  merge_cmd_baton.path = path;
  merge_cmd_baton.added_path = NULL;
  merge_cmd_baton.add_necessitated_merge = FALSE;
  merge_cmd_baton.dry_run_deletions = (dry_run ? apr_hash_make(pool) : NULL);
  merge_cmd_baton.ctx = ctx;
  SVN_ERR(from_same_repos(&merge_cmd_baton, URL, entry, ctx, pool));
  merge_cmd_baton.pool = pool;

  /* Set up the diff3 command, so various callers don't have to. */
  {
    svn_config_t *cfg =
      ctx->config ? apr_hash_get(ctx->config, SVN_CONFIG_CATEGORY_CONFIG,
                                 APR_HASH_KEY_STRING) : NULL;

    svn_config_get(cfg, &(merge_cmd_baton.diff3_cmd),
                   SVN_CONFIG_SECTION_HELPERS,
                   SVN_CONFIG_OPTION_DIFF3_CMD, NULL);
  }

  /* If our target_wcpath is a single file, assume that PATH1 and
     PATH2 are files as well, and do a single-file merge. */
  if (entry->kind == svn_node_file)
    {
      SVN_ERR(do_single_file_merge(URL, path, revision1,
                                   URL, path, revision2,
                                   peg_revision,
                                   target_wcpath,
                                   adm_access,
                                   &merge_cmd_baton,
                                   ignore_ancestry,
                                   pool));
    }

  /* Otherwise, this must be a directory merge.  Do the fancy
     recursive diff-editor thing. */
  else if (entry->kind == svn_node_dir)
    {
      /* Merge children with differing mergeinfo. */
      SVN_ERR(discover_and_merge_children(&children_with_mergeinfo,
                                          entry,
                                          URL,
                                          path,
                                          revision1,
                                          path,
                                          revision2,
                                          peg_revision,
                                          depth,
                                          ignore_ancestry,
                                          adm_access,
                                          &merge_cmd_baton,
                                          pool));

      /* Merge of the actual target.*/
      SVN_ERR(do_merge(URL,
                       path,
                       revision1,
                       URL,
                       path,
                       revision2,
                       peg_revision,
                       target_wcpath,
                       adm_access,
                       depth,
                       ignore_ancestry,
                       &merge_callbacks,
                       &merge_cmd_baton,
                       children_with_mergeinfo,
                       pool));

      /* The merge of the actual target is complete.  See if the target's
         immediate children's mergeinfo elides to the target. */
      if (!dry_run)
        SVN_ERR(elide_children(children_with_mergeinfo, target_wcpath,
                               entry, adm_access, ctx, pool));
    }

  /* The final mergeinfo on TARGET_WCPATH may itself elide. */
  if (!dry_run)
    SVN_ERR(svn_client__elide_mergeinfo(target_wcpath, NULL, entry,
                                        adm_access, ctx, pool));

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
  return svn_client_merge_peg3(source, revision1, revision2, peg_revision,
                               target_wcpath, SVN_DEPTH_FROM_RECURSE(recurse),
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

svn_error_t *
svn_client_get_mergeinfo(apr_hash_t **mergeinfo,
                         const char *path_or_url,
                         const svn_opt_revision_t *revision,
                         svn_client_ctx_t *ctx,
                         apr_pool_t *pool)
{
  if (svn_path_is_url(path_or_url))
    {
      svn_ra_session_t *ra_session;
      const char *repos_rel_path;
      svn_revnum_t rev;

      SVN_ERR(svn_client__open_ra_session_internal(&ra_session, path_or_url,
                                                   NULL, NULL, NULL, FALSE,
                                                   TRUE, ctx, pool));
      SVN_ERR(svn_client__get_revision_number(&rev, ra_session, revision, "",
                                              pool));
      SVN_ERR(svn_client__path_relative_to_root(&repos_rel_path, path_or_url,
                                                NULL, ra_session, NULL, pool));
      SVN_ERR(svn_client__get_repos_mergeinfo(ra_session, mergeinfo,
                                              repos_rel_path, rev,
                                              svn_mergeinfo_inherited, pool));
    }
  else
    {
      /* PATH_OR_URL is a WC path. */
      svn_wc_adm_access_t *adm_access;
      const svn_wc_entry_t *entry;
      svn_boolean_t indirect;

      SVN_ERR(svn_wc_adm_probe_open3(&adm_access, NULL, path_or_url, FALSE,
                                     0, ctx->cancel_func, ctx->cancel_baton,
                                     pool));
      SVN_ERR(svn_wc__entry_versioned(&entry, path_or_url, adm_access, FALSE,
                                      pool));
      SVN_ERR(get_wc_or_repos_mergeinfo(mergeinfo, entry, &indirect, FALSE,
                                        svn_mergeinfo_inherited, NULL,
                                        path_or_url, adm_access, ctx, pool));
      SVN_ERR(svn_wc_adm_close(adm_access));
    }

  return SVN_NO_ERROR;
}
