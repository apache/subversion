/*
 * patch.c:  wrapper around wc patch functionality.
 *
 * ====================================================================
 * Copyright (c) 2000-2006 CollabNet.  All rights reserved.
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

#include "svn_time.h"
#include "svn_wc.h"
#include "svn_client.h"
#include "svn_config.h"
#include "client.h"
#include "svn_io.h"
#include "svn_path.h"
#include "svn_pools.h"
#include "svn_base64.h"
#include "svn_string.h"
#include "svn_hash.h"
#include <assert.h>

#include "svn_private_config.h"


/*** Code. ***/

static const char equal_string[] = 
  "=========================";

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

/*-----------------------------------------------------------------------*/


struct patch_cmd_baton {
  svn_boolean_t force;
  svn_boolean_t dry_run;

  /* Set to the dir path whenever the dir is added as a child of a
   * versioned dir (dry-run only). */
  const char *added_path;

  /* Working copy target path. */
  const char *target;          

  /* Client context for callbacks, etc. */
  svn_client_ctx_t *ctx;       

  /* The list of paths for entries we've deleted, used only when in
   * dry_run mode. */
  apr_hash_t *dry_run_deletions;

  apr_pool_t *pool;
};

/* Used to avoid spurious notifications (e.g. conflicts) from a merge
   attempt into an existing target which would have been deleted if we
   weren't in dry_run mode (issue #2584).  Assumes that WCPATH is
   still versioned (e.g. has an associated entry). */
static APR_INLINE svn_boolean_t
dry_run_deleted_p(struct patch_cmd_baton *patch_b, const char *wcpath)
{
  return (patch_b->dry_run &&
          apr_hash_get(patch_b->dry_run_deletions, wcpath,
                       APR_HASH_KEY_STRING) != NULL);
}


/* A svn_wc_diff_callbacks3_t function.  Used for both file and directory
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
  struct patch_cmd_baton *patch_b = baton;
  apr_pool_t *subpool = svn_pool_create(patch_b->pool);
  svn_error_t *err;

  SVN_ERR(svn_categorize_props(propchanges, NULL, NULL, &props, subpool));

  /* We only want to merge "regular" version properties:  by
     definition, 'svn merge' shouldn't touch any data within .svn/,
     so does 'svn patch'. */
  if (props->nelts)
    {
      /* svn_wc_merge_props() requires ADM_ACCESS to be the access for
         the parent of PATH. Since the advent of merge tracking,
         discover_and_merge_children() may call this (indirectly) with
         the access for the patch_b->target instead (issue #2781).
         So, if we have the wrong access, get the right one. */
      if (svn_path_compare_paths(svn_wc_adm_access_path(adm_access),
                                 path) != 0)
        SVN_ERR(svn_wc_adm_probe_try3(&adm_access, adm_access, path,
                                      TRUE, -1, patch_b->ctx->cancel_func,
                                      patch_b->ctx->cancel_baton, subpool));

      err = svn_wc_merge_props(state, path, adm_access, original_props, props,
                               FALSE, patch_b->dry_run, subpool);
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

/* A svn_wc_diff_callbacks3_t function. */
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
  struct patch_cmd_baton *patch_b = baton;
  apr_pool_t *subpool = svn_pool_create(patch_b->pool);
  svn_boolean_t merge_required = (mimetype2
                                  && svn_mime_type_is_binary(mimetype2));
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

  /* Do property merge before content merge so that keyword expansion takes
     into account the new property values. */
  if (prop_changes->nelts > 0)
    SVN_ERR(merge_props_changed(adm_access, prop_state, mine, prop_changes,
                                original_props, baton));
  else
    if (prop_state)
      *prop_state = svn_wc_notify_state_unchanged;

  /* Now with content modifications */
  {
    svn_boolean_t has_local_mods;
    SVN_ERR(svn_wc_text_modified_p(&has_local_mods, mine, FALSE,
                                   adm_access, subpool));

    /* Special case:  if a binary file isn't locally modified, and is
       exactly identical to the file content from the patch, then don't
       allow svn_wc_merge to produce a conflict.  Instead, just
       overwrite the working file with the one from the patch. */
  if (!has_local_mods
      && (mimetype2 && svn_mime_type_is_binary(mimetype2)))
    {
      if (!patch_b->dry_run)
        SVN_ERR(svn_io_file_rename(yours, mine, subpool));
      merge_outcome = svn_wc_merge_merged;
      merge_required = FALSE;
    }

  /* The binary file has local modifications, we'll use svn_wc_merge
   * conflict facility to prompt the user and spawn backup files.
   * Workaround: since svn_wc_merge needs 3 input files, we create an
   * empty file which we remove when returning from svn_wc_merge. */
  if (merge_required)
    {
      const char *target_label = _(".working");
      const char *right_label = _(".patch");
      const char *left_label = _(".empty");
      const char *left;
      SVN_ERR(svn_wc_create_tmp_file2
              (NULL, &left,
               svn_wc_adm_access_path(adm_access),
               svn_io_file_del_on_pool_cleanup, subpool));
      SVN_ERR(svn_wc_merge3(&merge_outcome,
                            left, yours, mine, adm_access,
                            left_label, right_label, target_label,
                            patch_b->dry_run,
                            NULL, /* no diff3 */
                            NULL, /* no merge_options */
                            prop_changes,
                            patch_b->ctx->conflict_func,
                            patch_b->ctx->conflict_baton,
                            subpool));
      SVN_ERR(svn_io_remove_file
              (apr_pstrcat(subpool, mine, left_label, NULL),
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

/* A svn_wc_diff_callbacks3_t function. */
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
                 const char *copyfrom_path,
                 svn_revnum_t copyfrom_rev,
                 const apr_array_header_t *prop_changes,
                 apr_hash_t *original_props,
                 void *baton)
{
  struct patch_cmd_baton *patch_b = baton;
  apr_pool_t *subpool = svn_pool_create(patch_b->pool);
  svn_node_kind_t kind;
  int i;
  apr_hash_t *new_props;
  const char *path_basename = svn_path_basename(mine, subpool);

  /* This new file can't have any original prop in this offline context. */
  original_props = apr_hash_make(subpool);

  /* In most cases, we just leave prop_state as unknown, and let the
     content_state what happened, so we set prop_state here to avoid that
     below. */
  if (prop_state)
    *prop_state = svn_wc_notify_state_unknown;

  /* Apply the prop changes to a new hash table. */
  new_props = apr_hash_make(subpool);
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
      if (patch_b->dry_run && patch_b->added_path
          && svn_path_is_child(patch_b->added_path, mine, subpool))
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

        if (! patch_b->dry_run)
          {
            if (copyfrom_path) /* schedule-add-with-history */
              {
                svn_error_t *err;
                err = svn_wc_copy2(copyfrom_path, adm_access,
                                   path_basename,
                                   patch_b->ctx->cancel_func,
                                   patch_b->ctx->cancel_baton,
                                   NULL, NULL, /* no notification */
                                   subpool);
                if (err)
                  {
                    switch (err->apr_err)
                      {
                      case SVN_ERR_CANCELLED:
                        return err; /* may be allocated in subpool */

                      /* XXX: assume the following ENTRY is the source
                       * path.  How reliable is that? */
                      case SVN_ERR_ENTRY_NOT_FOUND:
                      case SVN_ERR_WC_COPYFROM_PATH_NOT_FOUND:
                        if (content_state)
                          *content_state = svn_wc_notify_state_source_missing;
                        break;

                      /* TODO: any other errors?  There are plenty, possibly
                       * all svn_wc_copy2 callees.. */

                      default:
                        if (content_state)
                          *content_state = svn_wc_notify_state_obstructed;
                      }
                    svn_error_clear(err);
                    svn_pool_destroy(subpool);
                    return SVN_NO_ERROR;
                  }
              }
            else /* schedule-add */
              {
                /* Copy the cached empty file and schedule-add it.  The
                 * contents will come in either via apply-textdelta
                 * following calls if this is a binary file or with
                 * unidiff for text files. */
                SVN_ERR(svn_io_copy_file(yours, mine, TRUE, subpool));
                SVN_ERR(svn_wc_add2(mine, adm_access, NULL, SVN_IGNORED_REVNUM,
                                    patch_b->ctx->cancel_func,
                                    patch_b->ctx->cancel_baton,
                                    NULL, NULL, /* no notification */
                                    subpool));
              }

          }

        /* Now regardless of the schedule-add nature, merge properties. */
        if (prop_changes->nelts > 0)
          SVN_ERR(merge_props_changed(adm_access, prop_state,
                                      mine, prop_changes,
                                      original_props, baton));
        else
          if (prop_state)
            *prop_state = svn_wc_notify_state_unchanged;

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

          if (entry && dry_run_deleted_p(patch_b, mine))
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
            if (dry_run_deleted_p(patch_b, mine))
              {
                if (content_state)
                  *content_state = svn_wc_notify_state_changed;
              }
            else
              {
                  SVN_ERR(merge_file_changed
                          (adm_access, content_state,
                           prop_state, mine, NULL, yours,
                           SVN_IGNORED_REVNUM, SVN_IGNORED_REVNUM,
                           mimetype1, mimetype2,
                           prop_changes, original_props,
                           baton));
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

/* A svn_wc_diff_callbacks3_t function. */
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
  struct patch_cmd_baton *patch_b = baton;
  apr_pool_t *subpool = svn_pool_create(patch_b->pool);
  svn_node_kind_t kind;
  svn_wc_adm_access_t *parent_access;
  const char *parent_path;
  svn_error_t *err;
  svn_boolean_t has_local_mods;

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

      SVN_ERR(svn_wc_text_modified_p(&has_local_mods, mine, TRUE,
                                     adm_access, subpool));
      /* Passing NULL for the notify_func and notify_baton because
         delete_entry() will do it for us. */
      err = svn_client__wc_delete(mine, parent_access, patch_b->force,
                                  patch_b->dry_run,
                                  has_local_mods ? TRUE : FALSE,
                                  NULL, NULL,
                                  patch_b->ctx, subpool);
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

/* A svn_wc_diff_callbacks3_t function. */
static svn_error_t *
merge_dir_added(svn_wc_adm_access_t *adm_access,
                svn_wc_notify_state_t *state,
                const char *path,
                svn_revnum_t rev,
                const char *copyfrom_path,
                svn_revnum_t copyfrom_rev,
                void *baton)
{
  struct patch_cmd_baton *patch_b = baton;
  apr_pool_t *subpool = svn_pool_create(patch_b->pool);
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
          if (patch_b->dry_run && patch_b->added_path
              && svn_path_is_child(patch_b->added_path, path, subpool))
            *state = svn_wc_notify_state_changed;
          else
            *state = svn_wc_notify_state_missing;
        }
      svn_pool_destroy(subpool);
      return SVN_NO_ERROR;
    }

  child = svn_path_is_child(patch_b->target, path, subpool);
  assert(child != NULL);

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
      if (! patch_b->dry_run)
        {
          SVN_ERR(svn_io_make_dir_recursively(path, subpool));
          SVN_ERR(svn_wc_add2(path, adm_access,
                              NULL, SVN_IGNORED_REVNUM,
                              patch_b->ctx->cancel_func,
                              patch_b->ctx->cancel_baton,
                              NULL, NULL, /* don't pass notification func! */
                              subpool));

        }
      if (patch_b->dry_run)
        patch_b->added_path = apr_pstrdup(patch_b->pool, path);
      if (state)
        *state = svn_wc_notify_state_changed;
      break;
    case svn_node_dir:
      /* Adding an unversioned directory doesn't destroy data */
      SVN_ERR(svn_wc_entry(&entry, path, adm_access, TRUE, subpool));
      if (! entry || entry->schedule == svn_wc_schedule_delete)
        {
          if (!patch_b->dry_run)
            SVN_ERR(svn_wc_add2(path, adm_access,
                                copyfrom_url, rev,
                                patch_b->ctx->cancel_func,
                                patch_b->ctx->cancel_baton,
                                NULL, NULL, /* no notification func! */
                                subpool));
          if (patch_b->dry_run)
            patch_b->added_path = apr_pstrdup(patch_b->pool, path);
          if (state)
            *state = svn_wc_notify_state_changed;
        }
      else if (state)
        {
          if (dry_run_deleted_p(patch_b, path))
            *state = svn_wc_notify_state_changed;
          else
            *state = svn_wc_notify_state_obstructed;
        }
      break;
    case svn_node_file:
      if (patch_b->dry_run)
        patch_b->added_path = NULL;

      if (state)
        {
          SVN_ERR(svn_wc_entry(&entry, path, adm_access, FALSE, subpool));

          if (entry && dry_run_deleted_p(patch_b, path))
            /* ### TODO: Retain record of this dir being added to
               ### avoid problems from subsequent edits which try to
               ### add children. */
            *state = svn_wc_notify_state_changed;
          else
            *state = svn_wc_notify_state_obstructed;
        }
      break;
    default:
      if (patch_b->dry_run)
        patch_b->added_path = NULL;
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

/* A svn_wc_diff_callbacks3_t function. */
static svn_error_t *
merge_dir_deleted(svn_wc_adm_access_t *adm_access,
                  svn_wc_notify_state_t *state,
                  const char *path,
                  void *baton)
{
  struct patch_cmd_baton *patch_b = baton;
  apr_pool_t *subpool = svn_pool_create(patch_b->pool);
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

        mdb.ctx = patch_b->ctx;
        mdb.path_skip = path;

        svn_path_split(path, &parent_path, NULL, subpool);
        SVN_ERR(svn_wc_adm_retrieve(&parent_access, adm_access, parent_path,
                                    subpool));
        err = svn_client__wc_delete(path, parent_access, patch_b->force,
                                    patch_b->dry_run, FALSE,
                                    merge_delete_notify_func, &mdb,
                                    patch_b->ctx, subpool);
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
  
/* The main callback table for 'svn patch'.  We leave merge callback
 * names as (a) they are pretty much merge operations (b) even if
 * tweaked them to meet 'svn patch' needs, they do pretty much what
 * their real sibblings do. */
static const svn_wc_diff_callbacks3_t
patch_callbacks =
  {
    merge_file_changed,
    merge_file_added,
    merge_file_deleted,
    merge_dir_added,
    merge_dir_deleted,
    merge_props_changed
  };

struct edit_baton {
  /* Directory against which 'svn patch' is run. */
  const char *target;

  /* ADM_ACCESS is an access baton that includes the TARGET directory. */
  svn_wc_adm_access_t *adm_access;

  /* Is it a dry-run patch application? */
  svn_boolean_t dry_run; 

  /* Empty hash used for adds. */
  apr_hash_t *empty_hash;

  /* The path to a temporary empty file used for adds.  The path is
   * cached here so that it can be reused, since all empty files are the
   * same. */
  const char *empty_file;

  /* The merge callbacks array and its baton. */
  const svn_wc_diff_callbacks3_t *diff_callbacks;
  void *diff_cmd_baton;

  /* If the func is non-null, send notifications of actions. */
  svn_wc_notify_func2_t notify_func;
  void *notify_baton;

  apr_pool_t *pool;
};

/* Directory level baton. */
struct dir_baton {
  /* Gets set if the directory is added rather than replaced/unchanged. */
  svn_boolean_t added;

  /* The path of the directory within the repository */
  const char *path;

  /* The path of the directory in the wc, relative to cwd */
  const char *wcpath;

  /* The baton for the parent directory, or null if this is the root of the
     hierarchy to be compared. */
  struct dir_baton *dir_baton;

  /* The overall crawler editor baton. */
  struct edit_baton *edit_baton;

  /* A cache of any property changes (svn_prop_t) received for this dir. */
  apr_array_header_t *propchanges;

  /* The pool passed in by add_dir, open_dir, or open_root.
     Also, the pool this dir baton is allocated in. */
  apr_pool_t *pool;
};

/* File level baton. */
struct file_baton {
  /* Gets set if the file is added rather than replaced. */
  svn_boolean_t added;

  /* The path of the file within the repository */
  const char *path;

  /* The path of the file in the wc, relative to cwd */
  const char *wcpath;

  /* The path and APR file handle to the temporary file that contains
   * an incoming binary file from the patch's guts. */
  const char *path_incoming;
  apr_file_t *file_incoming;

  /* Whether this file is considered as binary.  This flag is set upon
   * apply-textdelta calls. */
  svn_boolean_t is_binary;

  /* APPLY_HANDLER/APPLY_BATON represent the delta application baton. */
  svn_txdelta_window_handler_t apply_handler;
  void *apply_baton;

  /* The overall crawler editor baton. */
  struct edit_baton *edit_baton;

  /* The directory that contains the file. */
  struct dir_baton *dir_baton;

  /* A cache of any property changes (svn_prop_t) received for this file. */
  apr_array_header_t *propchanges;

  /* The source file's path the file was copied from. */
  const char *copyfrom_path;

  /* The source file's revision the file was copied from. */
  svn_revnum_t copyfrom_rev;

  /* The pool passed in by add_file or open_file.
     Also, the pool this file_baton is allocated in. */
  apr_pool_t *pool;
};

/* Create a new directory baton for PATH in POOL.  ADDED is set if
 * this directory is being added rather than replaced. PARENT_BATON is
 * the baton of the parent directory (or NULL if this is the root of
 * the comparison hierarchy). The directory and its parent may or may
 * not exist in the working copy.  EDIT_BATON is the overall crawler
 * editor baton.
 */
static struct dir_baton *
make_dir_baton(const char *path,
               struct dir_baton *parent_baton,
               struct edit_baton *edit_baton,
               svn_boolean_t added,
               apr_pool_t *pool)
{
  struct dir_baton *dir_baton = apr_pcalloc(pool, sizeof(*dir_baton));

  dir_baton->dir_baton = parent_baton;
  dir_baton->edit_baton = edit_baton;
  dir_baton->added = added;
  dir_baton->pool = pool;
  dir_baton->path = apr_pstrdup(pool, path);
  dir_baton->wcpath = svn_path_join(edit_baton->target, path, pool);
  dir_baton->propchanges  = apr_array_make(pool, 1, sizeof(svn_prop_t));

  return dir_baton;
}

/* Create a new file baton for PATH in POOL, which is a child of
 * directory PARENT_PATH. ADDED is set if this file is being added
 * rather than replaced.  EDIT_BATON is a pointer to the global edit
 * baton.
 */
static struct file_baton *
make_file_baton(const char *path,
                svn_boolean_t added,
                void *edit_baton,
                struct dir_baton *parent_baton,
                const char *copyfrom_path,
                svn_revnum_t copyfrom_rev,
                apr_pool_t *pool)
{
  struct file_baton *file_baton = apr_pcalloc(pool, sizeof(*file_baton));
  struct edit_baton *eb = edit_baton;

  file_baton->edit_baton = edit_baton;
  file_baton->added = added;
  file_baton->pool = pool;
  file_baton->path = apr_pstrdup(pool, path);
  file_baton->wcpath = svn_path_join(eb->target, path, pool);
  file_baton->propchanges  = apr_array_make(pool, 1, sizeof(svn_prop_t));
  file_baton->dir_baton = parent_baton;
  file_baton->is_binary = FALSE;
  file_baton->copyfrom_path = copyfrom_path;
  file_baton->copyfrom_rev = copyfrom_rev;

  return file_baton;
}

/* Some utility functions. */

/* Create an empty file, the path to the file is returned in
   EMPTY_FILE_PATH.  If ADM_ACCESS is not NULL and a lock is held,
   create the file in the adm tmp/ area, otherwise use a system temp
   directory.
 
   If FILE is non-NULL, an open file is returned in *FILE. */
static svn_error_t *
create_empty_file(apr_file_t **file,
                  const char **empty_file_path,
                  svn_wc_adm_access_t *adm_access,
                  svn_io_file_del_t delete_when,
                  apr_pool_t *pool)
{
  if (adm_access && svn_wc_adm_locked(adm_access))
    SVN_ERR(svn_wc_create_tmp_file2(file, empty_file_path,
                                    svn_wc_adm_access_path(adm_access),
                                    delete_when, pool));
  else
    {
      const char *temp_dir;

      SVN_ERR(svn_io_temp_dir(&temp_dir, pool));
      SVN_ERR(svn_io_open_unique_file2(file, empty_file_path,
                                       svn_path_join(temp_dir, "tmp", pool),
                                       "", delete_when, pool));
    }

  return SVN_NO_ERROR;
}

/* Return in *PATH_ACCESS the access baton for the directory PATH by
   searching the access baton set of ADM_ACCESS.  If ADM_ACCESS is NULL
   then *PATH_ACCESS will be NULL.  If LENIENT is TRUE then failure to find
   an access baton will not return an error but will set *PATH_ACCESS to
   NULL instead. */
static svn_error_t *
get_path_access(svn_wc_adm_access_t **path_access,
                svn_wc_adm_access_t *adm_access,
                const char *path,
                svn_boolean_t lenient,
                apr_pool_t *pool)
{
  if (! adm_access)
    *path_access = NULL;
  else
    {
      svn_error_t *err = svn_wc_adm_retrieve(path_access, adm_access, path,
                                             pool);
      if (err)
        {
          if (! lenient)
            return err;
          svn_error_clear(err);
          *path_access = NULL;
        }
    }

  return SVN_NO_ERROR;
}
                  
/* Like get_path_access except the returned access baton, in
   *PARENT_ACCESS, is for the parent of PATH rather than for PATH
   itself. */
static svn_error_t *
get_parent_access(svn_wc_adm_access_t **parent_access,
                  svn_wc_adm_access_t *adm_access,
                  const char *path,
                  svn_boolean_t lenient,
                  apr_pool_t *pool)
{
  if (! adm_access)
    *parent_access = NULL;  /* Avoid messing around with paths */
  else
    {
      const char *parent_path = svn_path_dirname(path, pool);
      SVN_ERR(get_path_access(parent_access, adm_access, parent_path,
                              lenient, pool));
    }
  return SVN_NO_ERROR;
}

/* Get the empty file associated with the edit baton. This is cached so
 * that it can be reused, all empty files are the same.
 */
static svn_error_t *
get_empty_file(struct edit_baton *eb,
               const char **empty_file_path)
{
  /* Create the file if it does not exist or is empty path. */
  /* Note that we tried to use /dev/null in r17220, but
     that won't work on Windows: it's impossible to stat NUL */
  if (!eb->empty_file || !*(eb->empty_file))
    SVN_ERR(create_empty_file(NULL, &(eb->empty_file), eb->adm_access,
                              svn_io_file_del_on_pool_cleanup, eb->pool));

  *empty_file_path = eb->empty_file;

  return SVN_NO_ERROR;
}

/* Convenience function */
static apr_hash_t *
dry_run_deletions_hash(struct edit_baton *eb)
{
  return ((struct patch_cmd_baton*)eb)->dry_run_deletions;
}

/* Implementation of svn_delta_editor_t vtable. */

/* An editor function. The root of the comparison hierarchy */
static svn_error_t *
open_root(void *edit_baton,
          svn_revnum_t base_revision,
          apr_pool_t *pool,
          void **root_baton)
{
  struct edit_baton *eb = edit_baton;
  struct dir_baton *b = make_dir_baton("", NULL, eb, FALSE, pool);

  /* Override the wcpath in our baton. */
  b->wcpath = apr_pstrdup(pool, eb->target);

  *root_baton = b;
  return SVN_NO_ERROR;
}

/* An editor function.  */
static svn_error_t *
delete_entry(const char *path,
             svn_revnum_t base_revision,
             void *parent_baton,
             apr_pool_t *pool)
{
  struct dir_baton *pb = parent_baton;
  struct edit_baton *eb = pb->edit_baton;
  svn_node_kind_t kind;
  svn_wc_adm_access_t *adm_access;
  svn_wc_notify_state_t state = svn_wc_notify_state_inapplicable;
  svn_wc_notify_action_t action = svn_wc_notify_skip;

  /* We need to know if this is a directory or a file.  Unfortunately,
   * if @a path is missing (e.g. user removes manually), this check
   * below returns svn_node_none and a discrepancy shows up when
   * notifying the world: we get a 'D' instead of a 'Skipped missing
   * target'.  One day we want to provide svnpatch's delete-entry
   * command with a hint on what this thing -- path -- really is, since
   * svnpatch application takes place offline as opposed to merge.  This
   * would help the following switch fall in the right case, and thus
   * clean our discrepancy. */
  SVN_ERR(svn_io_check_path(path, &kind, pool));
  SVN_ERR(get_path_access(&adm_access, eb->adm_access, pb->wcpath,
                          TRUE, pool));
  if ((! eb->adm_access) || adm_access)
    {
      switch (kind)
        {
        case svn_node_file:
          {
            struct file_baton *b;
            
            /* Compare a file being deleted against an empty file */
            b = make_file_baton(path, FALSE, eb, pb, NULL,
                                SVN_IGNORED_REVNUM, pool);
            
            SVN_ERR(eb->diff_callbacks->file_deleted 
                    (adm_access, &state, b->wcpath,
                     NULL, NULL, NULL, NULL, NULL, /* useless for del */
                     b->edit_baton->diff_cmd_baton));
            
            break;
          }
        case svn_node_dir:
          {
            SVN_ERR(eb->diff_callbacks->dir_deleted 
                    (adm_access, &state, 
                     svn_path_join(eb->target, path, pool),
                     eb->diff_cmd_baton));
            break;
          }
        default:
          break;
        }

      if ((state != svn_wc_notify_state_missing)
          && (state != svn_wc_notify_state_obstructed))
        {
          action = svn_wc_notify_update_delete;
          if (eb->dry_run)
            {
              /* Remember what we _would've_ deleted (issue #2584). */
              const char *wcpath = svn_path_join(eb->target, path, pb->pool);
              apr_hash_set(dry_run_deletions_hash(eb->diff_cmd_baton),
                           wcpath, APR_HASH_KEY_STRING, wcpath);

              /* ### TODO: if (kind == svn_node_dir), record all
                 ### children as deleted to avoid collisions from
                 ### subsequent edits. */
            }
        }
    }

  if (eb->notify_func)
    {
      svn_wc_notify_t *notify
        = svn_wc_create_notify(svn_path_join(eb->target, path, pool),
                               action, pool);
      notify->kind = kind;
      notify->content_state = notify->prop_state = state;
      notify->lock_state = svn_wc_notify_lock_state_inapplicable;
      (*eb->notify_func)(eb->notify_baton, notify, pool);
    }

  return SVN_NO_ERROR;
}

/* An editor function.  */
static svn_error_t *
add_directory(const char *path,
              void *parent_baton,
              const char *copyfrom_path,
              svn_revnum_t copyfrom_revision,
              apr_pool_t *pool,
              void **child_baton)
{
  struct dir_baton *pb = parent_baton;
  struct edit_baton *eb = pb->edit_baton;
  struct dir_baton *b;
  svn_wc_adm_access_t *adm_access;
  svn_wc_notify_state_t state;
  svn_wc_notify_action_t action;

  b = make_dir_baton(path, pb, eb, TRUE, pool);
  *child_baton = b;

  SVN_ERR(get_path_access(&adm_access, eb->adm_access, pb->wcpath, TRUE,
                          pool));

  SVN_ERR(eb->diff_callbacks->dir_added 
          (adm_access, &state, b->wcpath, SVN_IGNORED_REVNUM,
           copyfrom_path, copyfrom_revision,
           eb->diff_cmd_baton));

  if ((state == svn_wc_notify_state_missing)
      || (state == svn_wc_notify_state_obstructed))
    action = svn_wc_notify_skip;
  else
    action = svn_wc_notify_update_add;

  if (eb->notify_func)
    {
      svn_wc_notify_t *notify = svn_wc_create_notify(b->wcpath, action, pool);
      notify->kind = svn_node_dir;
      (*eb->notify_func)(eb->notify_baton, notify, pool);
    }

  return SVN_NO_ERROR;
}

/* An editor function.  */
static svn_error_t *
open_directory(const char *path,
               void *parent_baton,
               svn_revnum_t base_revision,
               apr_pool_t *pool,
               void **child_baton)
{
  struct dir_baton *pb = parent_baton;
  struct dir_baton *b;

  b = make_dir_baton(path, pb, pb->edit_baton, FALSE, pool);
  *child_baton = b;

  return SVN_NO_ERROR;
}

/* An editor function.  */
static svn_error_t *
add_file(const char *path,
         void *parent_baton,
         const char *copyfrom_path,
         svn_revnum_t copyfrom_revision,
         apr_pool_t *pool,
         void **file_baton)
{
  struct dir_baton *pb = parent_baton;
  struct file_baton *b;

  b = make_file_baton(path, TRUE, pb->edit_baton, pb,
                      copyfrom_path, copyfrom_revision, pool);
  *file_baton = b;

  /* We want to schedule this file for addition. */
  SVN_ERR(get_empty_file(b->edit_baton, &(b->path_incoming)));

  return SVN_NO_ERROR;
}

/* An editor function.  */
static svn_error_t *
open_file(const char *path,
          void *parent_baton,
          svn_revnum_t base_revision,
          apr_pool_t *pool,
          void **file_baton)
{
  struct dir_baton *pb = parent_baton;
  struct file_baton *b;

  b = make_file_baton(path, FALSE, pb->edit_baton, pb,
                      NULL, SVN_IGNORED_REVNUM, pool);
  *file_baton = b;

  return SVN_NO_ERROR;
}

/* Do the work of applying the text delta.  */
static svn_error_t *
window_handler(svn_txdelta_window_t *window,
               void *window_baton)
{
  struct file_baton *b = window_baton;

  SVN_ERR(b->apply_handler(window, b->apply_baton));

  if (!window)
    SVN_ERR(svn_io_file_close(b->file_incoming, b->pool));

  return SVN_NO_ERROR;
}

/* An editor function.  */
static svn_error_t *
apply_textdelta(void *file_baton,
                const char *base_checksum,
                apr_pool_t *pool,
                svn_txdelta_window_handler_t *handler,
                void **handler_baton)
{
  struct file_baton *b = file_baton;
  svn_wc_adm_access_t *adm_access;

  /* This must be a binary file since, in a svnpatch context, we're only
   * carrying txdeltas from binary files. */
  b->is_binary = TRUE;

  if (b->edit_baton->adm_access)
    {
      svn_error_t *err;

      err = svn_wc_adm_probe_retrieve(&adm_access, b->edit_baton->adm_access,
                                      b->wcpath, pool);
      if (err)
        {
          svn_error_clear(err);
          adm_access = NULL;
        }
    }
  else
    adm_access = NULL;

  SVN_ERR(create_empty_file(&(b->file_incoming),
                            &(b->path_incoming), adm_access,
                            svn_io_file_del_none, b->pool));

  /* svnpatch's txdeltas are svn_txdelta_source-action-less, i.e. we
   * don't need any source stream here as bytes are written directly to
   * the target stream. */
  svn_txdelta_apply(NULL,
                    svn_stream_from_aprfile(b->file_incoming, b->pool),
                    NULL, b->path, b->pool,
                    &(b->apply_handler), &(b->apply_baton));

  *handler = window_handler;
  *handler_baton = file_baton;

  return SVN_NO_ERROR;
}

/* An editor function.  When the file is closed we have a temporary file
 * containing a pristine version of the file from the patch. This can be
 * compared against the working copy.
 *
 * ### Ignore TEXT_CHECKSUM for now.  Someday we can use it to verify
 * ### the integrity of the file being diffed.  Done efficiently, this
 * ### would probably involve calculating the checksum as the data is
 * ### received, storing the final checksum in the file_baton, and
 * ### comparing against it here.
 */
static svn_error_t *
close_file(void *file_baton,
           const char *text_checksum,
           apr_pool_t *pool)
{
  struct file_baton *b = file_baton;
  struct edit_baton *eb = b->edit_baton;
  svn_wc_adm_access_t *adm_access;
  svn_error_t *err;
  svn_wc_notify_action_t action;
  svn_wc_notify_state_t
    content_state = svn_wc_notify_state_unknown,
    prop_state = svn_wc_notify_state_unknown;

  err = get_parent_access(&adm_access, eb->adm_access, 
                          b->wcpath, eb->dry_run, b->pool);

  if (err && err->apr_err == SVN_ERR_WC_NOT_LOCKED)
    {
      /* ### maybe try to stat the local b->wcpath? */      
      /* If the file path doesn't exist, then send a 'skipped' notification. */
      if (eb->notify_func)
        {
          svn_wc_notify_t *notify = svn_wc_create_notify(b->wcpath,
                                                         svn_wc_notify_skip,
                                                         pool);
          notify->kind = svn_node_file;
          notify->content_state = svn_wc_notify_state_missing;
          notify->prop_state = prop_state;
          (*eb->notify_func)(eb->notify_baton, notify, pool);
        }
      
      svn_error_clear(err);
      return SVN_NO_ERROR;
    }
  else if (err)
    return err;

  if (b->path_incoming || b->propchanges->nelts > 0)
    {
      const char *mimetype
        = b->is_binary ? "application/octet-stream" : NULL;

      if (b->added)
        SVN_ERR(eb->diff_callbacks->file_added
                (adm_access, &content_state, &prop_state,
                 b->wcpath,
                 NULL,
                 b->path_incoming,
                 SVN_IGNORED_REVNUM,
                 SVN_IGNORED_REVNUM,
                 NULL, mimetype,
                 b->copyfrom_path, b->copyfrom_rev,
                 b->propchanges, NULL,
                 b->edit_baton->diff_cmd_baton));
      else
        SVN_ERR(eb->diff_callbacks->file_changed
                (adm_access, &content_state, &prop_state,
                 b->wcpath,
                 NULL,
                 b->path_incoming,
                 SVN_IGNORED_REVNUM,
                 SVN_IGNORED_REVNUM,
                 NULL, mimetype,
                 b->propchanges, NULL, /* use base props */
                 b->edit_baton->diff_cmd_baton));
    }


  if ((content_state == svn_wc_notify_state_missing)
      || (content_state == svn_wc_notify_state_obstructed)
      || (content_state == svn_wc_notify_state_source_missing))
    action = svn_wc_notify_skip;
  else if (b->added)
    action = svn_wc_notify_update_add;
  else
    action = svn_wc_notify_update_update;

  if (eb->notify_func)
    {
      svn_wc_notify_t *notify = svn_wc_create_notify(b->wcpath, action,
                                                     pool);
      notify->kind = svn_node_file;
      notify->content_state = content_state;
      notify->prop_state = prop_state;
      (*eb->notify_func)(eb->notify_baton, notify, pool);
    }

  return SVN_NO_ERROR;
}

/* An editor function.  */
static svn_error_t *
close_directory(void *dir_baton,
                apr_pool_t *pool)
{
  struct dir_baton *b = dir_baton;
  struct edit_baton *eb = b->edit_baton;
  svn_wc_notify_state_t prop_state = svn_wc_notify_state_unknown;
  svn_error_t *err;

  if (eb->dry_run)
    svn_hash__clear(dry_run_deletions_hash(eb->diff_cmd_baton));

  if (b->propchanges->nelts > 0)
    {
      svn_wc_adm_access_t *adm_access;
      err = get_path_access(&adm_access, eb->adm_access, b->wcpath,
                            eb->dry_run, b->pool);

      if (err && err->apr_err == SVN_ERR_WC_NOT_LOCKED)
        {
          /* ### maybe try to stat the local b->wcpath? */          
          /* If the path doesn't exist, then send a 'skipped' notification. */
          if (eb->notify_func)
            {
              svn_wc_notify_t *notify
                = svn_wc_create_notify(b->wcpath, svn_wc_notify_skip, pool);
              notify->kind = svn_node_dir;
              notify->content_state = notify->prop_state
                = svn_wc_notify_state_missing;
              (*eb->notify_func)(eb->notify_baton, notify, pool);
            }
          svn_error_clear(err);      
          return SVN_NO_ERROR;
        }
      else if (err)
        return err;

      /* Don't do the props_changed stuff if this is a dry_run and we don't
         have an access baton, since in that case the directory will already
         have been recognised as added, in which case they cannot conflict. */
      if (! eb->dry_run || adm_access)
        SVN_ERR(eb->diff_callbacks->dir_props_changed
                (adm_access, &prop_state,
                 b->wcpath,
                 b->propchanges, NULL,
                 b->edit_baton->diff_cmd_baton));
    }

  /* ### Don't notify added directories as they triggered notification
     in add_directory.  Does this mean that directory notification
     isn't getting all the information? */
  if (!b->added && eb->notify_func)
    {
      svn_wc_notify_t *notify
        = svn_wc_create_notify(b->wcpath, svn_wc_notify_update_update, pool);
      notify->kind = svn_node_dir;
      notify->content_state = svn_wc_notify_state_inapplicable;
      notify->prop_state = prop_state;
      notify->lock_state = svn_wc_notify_lock_state_inapplicable;
      (*eb->notify_func)(eb->notify_baton, notify, pool);
    }

  return SVN_NO_ERROR;
}

/* An editor function.  */
static svn_error_t *
change_file_prop(void *file_baton,
                 const char *name,
                 const svn_string_t *value,
                 apr_pool_t *pool)
{
  struct file_baton *b = file_baton;
  svn_prop_t *propchange;

  propchange = apr_array_push(b->propchanges);
  propchange->name = apr_pstrdup(b->pool, name);
  propchange->value = value ? svn_string_dup(value, b->pool) : NULL;

  return SVN_NO_ERROR;
}

/* An editor function.  */
static svn_error_t *
change_dir_prop(void *dir_baton,
                const char *name,
                const svn_string_t *value,
                apr_pool_t *pool)
{
  struct dir_baton *db = dir_baton;
  svn_prop_t *propchange;

  propchange = apr_array_push(db->propchanges);
  propchange->name = apr_pstrdup(db->pool, name);
  propchange->value = value ? svn_string_dup(value, db->pool) : NULL;

  return SVN_NO_ERROR;
}

/* An editor function.  */
static svn_error_t *
close_edit(void *edit_baton,
           apr_pool_t *pool)
{
  struct edit_baton *eb = edit_baton;

  svn_pool_destroy(eb->pool);

  return SVN_NO_ERROR;
}

static struct edit_baton *
make_editor_baton(const char *target,
                  svn_wc_adm_access_t *adm_access,
                  svn_boolean_t dry_run,
                  const svn_wc_diff_callbacks3_t *callbacks,
                  void *patch_cmd_baton,
                  svn_wc_notify_func2_t notify_func,
                  void *notify_baton,
                  const svn_delta_editor_t **editor,
                  apr_pool_t *pool)
{
  apr_pool_t *subpool = svn_pool_create(pool);
  struct edit_baton *eb = apr_pcalloc(subpool, sizeof(*eb));
  svn_delta_editor_t *tree_editor = svn_delta_default_editor(subpool);

  eb->target = target;
  eb->adm_access = adm_access;
  eb->dry_run = dry_run;
  eb->empty_hash = apr_hash_make(subpool);
  eb->diff_callbacks = callbacks;
  eb->diff_cmd_baton = patch_cmd_baton;
  eb->notify_func = notify_func;
  eb->notify_baton = notify_baton;
  eb->pool = subpool;

  tree_editor->open_root = open_root;
  tree_editor->delete_entry = delete_entry;
  tree_editor->add_directory = add_directory;
  tree_editor->open_directory = open_directory;
  tree_editor->change_dir_prop = change_dir_prop;
  tree_editor->close_directory = close_directory;
  tree_editor->add_file = add_file;
  tree_editor->open_file = open_file;
  tree_editor->apply_textdelta = apply_textdelta;
  tree_editor->change_file_prop = change_file_prop;
  tree_editor->close_file = close_file;
  tree_editor->close_edit = close_edit;

  *editor = tree_editor;
  
  /* subpool is destroyed upon close_edit() */
  return eb;
}


/* Extract and uncompress-decode the svnpatch block that's in @a
 * original_patch_path, and fill @a *patch_file with its clear-text
 * format. */
static svn_error_t *
extract_svnpatch(const char *original_patch_path,
                 apr_file_t **patch_file,
                 apr_pool_t *pool)
{
  apr_file_t *original_patch_file; /* gzip-base64'ed */
  svn_stream_t *original_patch_stream;
  apr_file_t *compressed_file; /* base64-decoded, gzip-compressed */
  svn_stream_t *compressed_stream;
  svn_stream_t *svnpatch_stream; /* clear-text, attached to @a patch_file */
  const char *tempdir;
  svn_string_t *svnpatch_header;
  svn_stringbuf_t *patch_line;
  apr_pool_t *subpool = svn_pool_create(pool);

  /* We assume both clients have the same version for now. */
  svnpatch_header = svn_string_createf(pool,
                                       "%s SVNPATCH%d BLOCK %s",
                                       equal_string,
                                       SVN_CLIENT_SVNPATCH_VERSION,
                                       equal_string);

  SVN_ERR(svn_io_file_open(&original_patch_file, original_patch_path,
                           APR_READ, APR_OS_DEFAULT, pool));
  original_patch_stream = svn_stream_from_aprfile2(original_patch_file,
                                          FALSE, pool);

  while (1)
    {
      svn_boolean_t eof;
      svn_pool_clear(subpool);
      SVN_ERR(svn_stream_readline(original_patch_stream, &patch_line, "\n",
                                  &eof, subpool));
      /* No need to go deeper down the stack when the first char isn't
       * even '='. */
      if (*patch_line->data == '='
          && svn_string_compare_stringbuf(svnpatch_header, patch_line))
        break;

      /* @a original_patch_path doesn't contain the svnpatch block
       * we're looking for. */
      if (eof)
        {
          *patch_file = NULL;
          return SVN_NO_ERROR;
        }
    }
  svn_pool_destroy(subpool);

  /* At this point, original_patch_stream's cursor points right after the
   * svnpatch header, that is, the bytes we're interested in,
   * gzip-base64'ed.  So create the temp file that will carry clear-text
   * Editor commands for later work, decode the svnpatch chunk we have
   * in hand, and write to it. */
  SVN_ERR(svn_io_temp_dir(&tempdir, pool));
  SVN_ERR(svn_io_open_unique_file2
          (patch_file, NULL,
           svn_path_join(tempdir, "patch", pool),
           "", svn_io_file_del_none, pool));
  svnpatch_stream = svn_stream_from_aprfile(*patch_file, pool);

  /* Oh, and we can't gzip-base64 decode in one step since
   * svn_base64_decode wraps a write-decode handler and
   * svn_stream_compressed wraps a write-compress handler.  We split the
   * pipe out in two here, with an intermediate temp-file.  If someone
   * feels like hacking libsvn_subr, we could either add a read-decode
   * handler to svn_base64_decode or have a new svn_stream_decompressed
   * (as opposed to svn_stream_compressed) to skip this
   * {time,IO}-consuming workaround. */
  SVN_ERR(svn_io_open_unique_file2
          (&compressed_file, NULL,
           svn_path_join(tempdir, "compressedpatch", pool),
           "", svn_io_file_del_on_close, pool));
  compressed_stream = svn_base64_decode
                      (svn_stream_from_aprfile2
                       (compressed_file, FALSE, pool), pool);
  SVN_ERR(svn_stream_copy(original_patch_stream, compressed_stream, pool));

  {
    /* Rewind. */
    apr_off_t offset = 0;
    SVN_ERR(svn_io_file_seek(compressed_file,
                             APR_SET, &offset, pool));
  }

  compressed_stream = svn_stream_compressed
                      (svn_stream_from_aprfile2
                       (compressed_file, FALSE, pool), pool);
  SVN_ERR(svn_stream_copy(compressed_stream, svnpatch_stream, pool));
  SVN_ERR(svn_stream_close(svnpatch_stream));

  /* TODO: wrap errors? */

  {
    /* Rewind so that next reads don't get it wrong. */
    apr_off_t offset = 0;
    SVN_ERR(svn_io_file_seek(*patch_file,
                             APR_SET, &offset, pool));
  }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_patch(const char *patch_path,
                 const char *wc_path,
                 svn_boolean_t force,
                 apr_file_t *outfile,
                 apr_file_t *errfile,
                 svn_client_ctx_t *ctx,
                 apr_pool_t *pool)
{
  apr_file_t *decoded_patch_file;
  struct patch_cmd_baton patch_cmd_baton;
  const svn_delta_editor_t *diff_editor;
  svn_wc_adm_access_t *adm_access;
  struct edit_baton *eb;
  svn_boolean_t dry_run = FALSE; /* disable dry_run for now */

  /* Pull out the svnpatch block. */
  SVN_ERR(extract_svnpatch(patch_path, &decoded_patch_file, pool));

  if (decoded_patch_file)
    {
      /* Get ready with the editor baton. */
      patch_cmd_baton.force = force;
      patch_cmd_baton.dry_run = dry_run;
      patch_cmd_baton.added_path = NULL;
      patch_cmd_baton.target = wc_path;
      patch_cmd_baton.ctx = ctx;
      patch_cmd_baton.dry_run_deletions = (dry_run ? apr_hash_make(pool)
                                            : NULL);
      patch_cmd_baton.pool = pool;

      SVN_ERR(svn_wc_adm_open3(&adm_access, NULL, wc_path,
                               TRUE, -1, NULL, NULL, pool));
      eb = make_editor_baton(wc_path, adm_access, dry_run,
                             &patch_callbacks, &patch_cmd_baton,
                             ctx->notify_func2, ctx->notify_baton2,
                             &diff_editor, pool);

      /* Apply the svnpatch part of the patch file against the WC. */
      SVN_ERR(svn_wc_apply_svnpatch(decoded_patch_file, diff_editor,
                                    eb, pool));

    }

  /* Now proceed with the unidiff bytes. */
  SVN_ERR(svn_wc_apply_unidiff(patch_path, force, outfile, errfile,
                               ctx->config, pool));

  return SVN_NO_ERROR;
}
