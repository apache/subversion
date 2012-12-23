/*
 * externals.c:  handle the svn:externals property
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

#include <apr_uri.h>
#include "svn_wc.h"
#include "svn_pools.h"
#include "svn_client.h"
#include "svn_types.h"
#include "svn_error.h"
#include "svn_dirent_uri.h"
#include "svn_path.h"
#include "svn_props.h"
#include "svn_config.h"
#include "client.h"

#include "svn_private_config.h"
#include "private/svn_wc_private.h"
 

/* Remove the directory at LOCAL_ABSPATH from revision control, and do the
 * same to any revision controlled directories underneath LOCAL_ABSPATH
 * (including directories not referred to by parent svn administrative areas);
 * then if LOCAL_ABSPATH is empty afterwards, remove it, else rename it to a
 * unique name in the same parent directory.
 *
 * Pass CANCEL_FUNC, CANCEL_BATON to svn_wc_remove_from_revision_control.
 *
 * Use SCRATCH_POOL for all temporary allocation.
 */
static svn_error_t *
relegate_dir_external(svn_wc_context_t *wc_ctx,
                      const char *wri_abspath,
                      const char *local_abspath,
                      svn_cancel_func_t cancel_func,
                      void *cancel_baton,
                      apr_pool_t *scratch_pool)
{
  svn_error_t *err = SVN_NO_ERROR;

  err = svn_wc__external_remove(wc_ctx, wri_abspath, local_abspath,
                                cancel_func, cancel_baton, scratch_pool);
  if (err && (err->apr_err == SVN_ERR_WC_LEFT_LOCAL_MOD))
    {
      const char *parent_dir;
      const char *dirname;
      const char *new_path;

      svn_error_clear(err);
      err = SVN_NO_ERROR;

      svn_dirent_split(&parent_dir, &dirname, local_abspath, scratch_pool);

      /* Reserve the new dir name. */
      SVN_ERR(svn_io_open_uniquely_named(NULL, &new_path,
                                         parent_dir, dirname, ".OLD",
                                         svn_io_file_del_none,
                                         scratch_pool, scratch_pool));

      /* Sigh...  We must fall ever so slightly from grace.

         Ideally, there would be no window, however brief, when we
         don't have a reservation on the new name.  Unfortunately,
         at least in the Unix (Linux?) version of apr_file_rename(),
         you can't rename a directory over a file, because it's just
         calling stdio rename(), which says:

            ENOTDIR
              A  component used as a directory in oldpath or newpath
              path is not, in fact, a directory.  Or, oldpath  is
              a directory, and newpath exists but is not a directory

         So instead, we get the name, then remove the file (ugh), then
         rename the directory, hoping that nobody has gotten that name
         in the meantime -- which would never happen in real life, so
         no big deal.
      */
      /* Do our best, but no biggy if it fails. The rename will fail. */
      svn_error_clear(svn_io_remove_file2(new_path, TRUE, scratch_pool));

      /* Rename. */
      SVN_ERR(svn_io_file_rename(local_abspath, new_path, scratch_pool));
    }

  return svn_error_trace(err);
}

/* Try to update a directory external at PATH to URL at REVISION.
   Use POOL for temporary allocations, and use the client context CTX. */
static svn_error_t *
switch_dir_external(const char *local_abspath,
                    const char *url,
                    const svn_opt_revision_t *peg_revision,
                    const svn_opt_revision_t *revision,
                    const char *defining_abspath,
                    svn_boolean_t *timestamp_sleep,
                    svn_client_ctx_t *ctx,
                    apr_pool_t *pool)
{
  svn_node_kind_t kind;
  svn_error_t *err;
  svn_revnum_t external_peg_rev = SVN_INVALID_REVNUM;
  svn_revnum_t external_rev = SVN_INVALID_REVNUM;
  apr_pool_t *subpool = svn_pool_create(pool);
  const char *repos_root_url;
  const char *repos_uuid;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  if (peg_revision->kind == svn_opt_revision_number)
    external_peg_rev = peg_revision->value.number;

  if (revision->kind == svn_opt_revision_number)
    external_rev = revision->value.number;

  /* If path is a directory, try to update/switch to the correct URL
     and revision. */
  SVN_ERR(svn_io_check_path(local_abspath, &kind, pool));
  if (kind == svn_node_dir)
    {
      const char *node_url;

      /* Doubles as an "is versioned" check. */
      err = svn_wc__node_get_url(&node_url, ctx->wc_ctx, local_abspath,
                                 pool, subpool);
      if (err && err->apr_err == SVN_ERR_WC_PATH_NOT_FOUND)
        {
          svn_error_clear(err);
          err = SVN_NO_ERROR;
          goto relegate;
        }
      else if (err)
        return svn_error_trace(err);

      if (node_url)
        {
          /* If we have what appears to be a version controlled
             subdir, and its top-level URL matches that of our
             externals definition, perform an update. */
          if (strcmp(node_url, url) == 0)
            {
              SVN_ERR(svn_client__update_internal(NULL, local_abspath,
                                                  revision, svn_depth_unknown,
                                                  FALSE, FALSE, FALSE, TRUE,
                                                  FALSE, TRUE,
                                                  timestamp_sleep,
                                                  ctx,
                                                  ctx->conflict_func2,
                                                  ctx->conflict_baton2,
                                                  subpool));
              svn_pool_destroy(subpool);
              goto cleanup;
            }

          SVN_ERR(svn_wc__node_get_repos_info(&repos_root_url, &repos_uuid,
                                              ctx->wc_ctx, local_abspath,
                                              pool, subpool));
          if (repos_root_url)
            {
              /* URLs don't match.  Try to relocate (if necessary) and then
                 switch. */
              if (! svn_uri__is_ancestor(repos_root_url, url))
                {
                  const char *repos_root;
                  svn_ra_session_t *ra_session;

                  /* Get the repos root of the new URL. */
                  SVN_ERR(svn_client__open_ra_session_internal
                          (&ra_session, NULL, url, NULL, NULL,
                           FALSE, TRUE, ctx, subpool));
                  SVN_ERR(svn_ra_get_repos_root2(ra_session, &repos_root,
                                                 subpool));

                  err = svn_client_relocate2(local_abspath, repos_root_url,
                                             repos_root,
                                             FALSE, ctx, subpool);
                  /* If the relocation failed because the new URL points
                     to another repository, then we need to relegate and
                     check out a new WC. */
                  if (err
                      && (err->apr_err == SVN_ERR_WC_INVALID_RELOCATION
                          || (err->apr_err
                              == SVN_ERR_CLIENT_INVALID_RELOCATION)))
                    {
                      svn_error_clear(err);
                      goto relegate;
                    }
                  else if (err)
                    return svn_error_trace(err);
                }

              SVN_ERR(svn_client__switch_internal(NULL, local_abspath, url,
                                                  peg_revision, revision,
                                                  svn_depth_infinity,
                                                  TRUE, FALSE, FALSE,
                                                  TRUE /* ignore_ancestry */,
                                                  timestamp_sleep,
                                                  ctx, subpool));

              SVN_ERR(svn_wc__external_register(ctx->wc_ctx,
                                                defining_abspath,
                                                local_abspath, svn_node_dir,
                                                repos_root_url, repos_uuid,
                                                svn_uri_skip_ancestor(
                                                            repos_root_url,
                                                            url, subpool),
                                                external_peg_rev,
                                                external_rev,
                                                subpool));

              svn_pool_destroy(subpool);
              goto cleanup;
            }
        }
    }

 relegate:

  /* Fall back on removing the WC and checking out a new one. */

  /* Ensure that we don't have any RA sessions or WC locks from failed
     operations above. */
  svn_pool_destroy(subpool);

  if (kind == svn_node_dir)
    {
      /* Buh-bye, old and busted ... */
      SVN_ERR(svn_wc__acquire_write_lock(NULL, ctx->wc_ctx, local_abspath,
                                         FALSE, pool, pool));

      SVN_ERR(relegate_dir_external(ctx->wc_ctx, defining_abspath,
                                    local_abspath,
                                    ctx->cancel_func, ctx->cancel_baton,
                                    pool));
    }
  else
    {
      /* The target dir might have multiple components.  Guarantee
         the path leading down to the last component. */
      const char *parent = svn_dirent_dirname(local_abspath, pool);
      SVN_ERR(svn_io_make_dir_recursively(parent, pool));
    }

  /* ... Hello, new hotness. */
  SVN_ERR(svn_client__checkout_internal(NULL, url, local_abspath, peg_revision,
                                        revision, svn_depth_infinity,
                                        FALSE, FALSE, timestamp_sleep,
                                        ctx, pool));

  SVN_ERR(svn_wc__node_get_repos_info(&repos_root_url,
                                      &repos_uuid,
                                      ctx->wc_ctx, local_abspath,
                                      pool, pool));

  SVN_ERR(svn_wc__external_register(ctx->wc_ctx,
                                    defining_abspath,
                                    local_abspath, svn_node_dir,
                                    repos_root_url, repos_uuid,
                                    svn_uri_skip_ancestor(repos_root_url,
                                                          url, pool),
                                    external_peg_rev,
                                    external_rev,
                                    pool));

 cleanup:
  /* Issues #4123 and #4130: We don't need to keep the newly checked
     out external's DB open. */
  SVN_ERR(svn_wc__close_db(local_abspath, ctx->wc_ctx, pool));

  return SVN_NO_ERROR;
}

/* Try to update a file external at LOCAL_ABSPATH to URL at REVISION using a
   access baton that has a write lock.  Use SCRATCH_POOL for temporary
   allocations, and use the client context CTX. */
static svn_error_t *
switch_file_external(const char *local_abspath,
                     const char *url,
                     const svn_opt_revision_t *peg_revision,
                     const svn_opt_revision_t *revision,
                     const char *def_dir_abspath,
                     svn_ra_session_t *ra_session,
                     svn_client_ctx_t *ctx,
                     apr_pool_t *scratch_pool)
{
  apr_pool_t *subpool = svn_pool_create(scratch_pool);
  const char *dir_abspath;
  const char *target;
  svn_config_t *cfg = ctx->config ? apr_hash_get(ctx->config,
                                                 SVN_CONFIG_CATEGORY_CONFIG,
                                                 APR_HASH_KEY_STRING) : NULL;
  svn_boolean_t use_commit_times;
  const char *diff3_cmd;
  const char *preserved_exts_str;
  const apr_array_header_t *preserved_exts;
  svn_boolean_t locked_here;
  svn_error_t *err = NULL;
  svn_node_kind_t kind, external_kind;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  /* See if the user wants last-commit timestamps instead of current ones. */
  SVN_ERR(svn_config_get_bool(cfg, &use_commit_times,
                              SVN_CONFIG_SECTION_MISCELLANY,
                              SVN_CONFIG_OPTION_USE_COMMIT_TIMES, FALSE));

  /* Get the external diff3, if any. */
  svn_config_get(cfg, &diff3_cmd, SVN_CONFIG_SECTION_HELPERS,
                 SVN_CONFIG_OPTION_DIFF3_CMD, NULL);

  if (diff3_cmd != NULL)
    SVN_ERR(svn_path_cstring_to_utf8(&diff3_cmd, diff3_cmd, subpool));

  /* See which files the user wants to preserve the extension of when
     conflict files are made. */
  svn_config_get(cfg, &preserved_exts_str, SVN_CONFIG_SECTION_MISCELLANY,
                 SVN_CONFIG_OPTION_PRESERVED_CF_EXTS, "");
  preserved_exts = *preserved_exts_str
    ? svn_cstring_split(preserved_exts_str, "\n\r\t\v ", FALSE, subpool)
    : NULL;

  svn_dirent_split(&dir_abspath, &target, local_abspath, subpool);

  /* Try to get a access baton for the anchor using the input access
     baton.  If this fails and returns SVN_ERR_WC_NOT_LOCKED, then try
     to get a new access baton to support inserting a file external
     into a directory external. */
  SVN_ERR(svn_wc_locked2(&locked_here, NULL, ctx->wc_ctx, dir_abspath,
                         subpool));
  if (!locked_here)
    {
      const char *wcroot_abspath;

      SVN_ERR(svn_wc__get_wc_root(&wcroot_abspath, ctx->wc_ctx, dir_abspath,
                                  subpool, subpool));

      if (!svn_dirent_is_ancestor(wcroot_abspath, def_dir_abspath))
        return svn_error_createf(
                        SVN_ERR_WC_BAD_PATH, NULL,
                        _("Cannot insert a file external defined on '%s' "
                          "into the working copy '%s'."),
                        svn_dirent_local_style(def_dir_abspath,
                                               scratch_pool),
                        svn_dirent_local_style(wcroot_abspath,
                                               scratch_pool));
    }

  err = svn_wc_read_kind(&kind, ctx->wc_ctx, local_abspath, FALSE, subpool);
  if (err)
    goto cleanup;

  err = svn_wc__read_external_info(&external_kind, NULL, NULL, NULL, NULL,
                                   ctx->wc_ctx, local_abspath, local_abspath,
                                   TRUE, scratch_pool, scratch_pool);

  if (err)
    goto cleanup;

  /* If there is a versioned item with this name, ensure it's a file
     external before working with it.  If there is no entry in the
     working copy, then create an empty file and add it to the working
     copy. */
  if (kind != svn_node_none && kind != svn_node_unknown)
    {
      if (external_kind != svn_node_file)
        {
          if (!locked_here)
            SVN_ERR(svn_wc__release_write_lock(ctx->wc_ctx, dir_abspath,
                                               subpool));

          return svn_error_createf(
              SVN_ERR_CLIENT_FILE_EXTERNAL_OVERWRITE_VERSIONED, 0,
             _("The file external from '%s' cannot overwrite the existing "
               "versioned item at '%s'"),
             url, svn_dirent_local_style(local_abspath, subpool));
        }
    }
  else
    {
      svn_node_kind_t disk_kind;

      err = svn_io_check_path(local_abspath, &disk_kind, subpool);

      if (err)
        goto cleanup;

      if (kind == svn_node_file || kind == svn_node_dir)
        {
          err = svn_error_createf(SVN_ERR_WC_PATH_FOUND, NULL,
                                  _("The file external '%s' can not be "
                                    "created because the node exists."),
                                  svn_dirent_local_style(local_abspath,
                                                         subpool));
          goto cleanup;
        }
    }

  {
    const svn_ra_reporter3_t *reporter;
    void *report_baton;
    const svn_delta_editor_t *switch_editor;
    void *switch_baton;
    svn_client__pathrev_t *switch_loc;
    svn_revnum_t revnum;
    /* ### TODO: Provide the real definition path (now available in
       ### def_dir_abspath) after switching to the new externals store.
       ### We can't enable this now, because that would move the external
       ### information into the wrong working copy */
    const char *definition_abspath = svn_dirent_dirname(local_abspath,subpool);

    /* Open an RA session to 'source' URL */
    SVN_ERR(svn_client__ra_session_from_path2(&ra_session, &switch_loc,
                                              url, dir_abspath,
                                              peg_revision, revision,
                                              ctx, subpool));

    SVN_ERR(svn_ra_reparent(ra_session, url, subpool));

    SVN_ERR(svn_wc__get_file_external_editor(&switch_editor, &switch_baton,
                                             &revnum, ctx->wc_ctx,
                                             local_abspath,
                                             definition_abspath /* wri */,
                                             switch_loc->url,
                                             switch_loc->repos_root_url,
                                             switch_loc->repos_uuid,
                                             use_commit_times,
                                             diff3_cmd, preserved_exts,
                                             definition_abspath /* def */,
                                             url, peg_revision, revision,
                                             ctx->conflict_func2,
                                             ctx->conflict_baton2,
                                             ctx->cancel_func,
                                             ctx->cancel_baton,
                                             ctx->notify_func2,
                                             ctx->notify_baton2,
                                             subpool, subpool));

    /* Tell RA to do an update of URL+TARGET to REVISION; if we pass an
     invalid revnum, that means RA will use the latest revision. */
    SVN_ERR(svn_ra_do_switch2(ra_session, &reporter, &report_baton,
                              switch_loc->rev,
                              target, svn_depth_unknown, url,
                              switch_editor, switch_baton, subpool));

    SVN_ERR(svn_wc__crawl_file_external(ctx->wc_ctx, local_abspath,
                                        reporter, report_baton,
                                        TRUE,  use_commit_times,
                                        ctx->cancel_func, ctx->cancel_baton,
                                        ctx->notify_func2, ctx->notify_baton2,
                                        subpool));

    if (ctx->notify_func2)
      {
        svn_wc_notify_t *notify
          = svn_wc_create_notify(local_abspath, svn_wc_notify_update_completed,
                                 subpool);
        notify->kind = svn_node_none;
        notify->content_state = notify->prop_state
          = svn_wc_notify_state_inapplicable;
        notify->lock_state = svn_wc_notify_lock_state_inapplicable;
        notify->revision = revnum;
        (*ctx->notify_func2)(ctx->notify_baton2, notify, subpool);
      }
  }

cleanup:
  if (!locked_here)
    err = svn_error_compose_create(err,
             svn_wc__release_write_lock(ctx->wc_ctx, dir_abspath, subpool));

  svn_pool_destroy(subpool);
  return svn_error_trace(err);
}

static svn_error_t *
handle_external_item_removal(const svn_client_ctx_t *ctx,
                             const char *defining_abspath,
                             const char *local_abspath,
                             apr_pool_t *scratch_pool)
{
  /* This branch is only used when an external is deleted from the
     repository and the working copy is updated or committed. */

  svn_error_t *err;
  svn_boolean_t lock_existed;
  svn_node_kind_t kind;
  const char *lock_root_abspath = NULL;

  /* local_abspath should be a wcroot or a file external */
  SVN_ERR(svn_wc_read_kind(&kind, ctx->wc_ctx, local_abspath, FALSE,
                           scratch_pool));

  if (kind == svn_node_none)
    return SVN_NO_ERROR; /* It's neither... Nothing to remove */

  SVN_ERR(svn_wc_locked2(&lock_existed, NULL, ctx->wc_ctx,
                         local_abspath, scratch_pool));

  if (! lock_existed)
    {
      SVN_ERR(svn_wc__acquire_write_lock(&lock_root_abspath,
                                         ctx->wc_ctx, local_abspath,
                                         FALSE,
                                         scratch_pool,
                                         scratch_pool));
    }

  /* We don't use relegate_dir_external() here, because we know that
     nothing else in this externals description (at least) is
     going to need this directory, and therefore it's better to
     leave stuff where the user expects it. */
  err = svn_wc__external_remove(ctx->wc_ctx, defining_abspath,
                                local_abspath,
                                ctx->cancel_func, ctx->cancel_baton,
                                scratch_pool);

  if (ctx->notify_func2)
    {
      svn_wc_notify_t *notify =
          svn_wc_create_notify(local_abspath,
                               svn_wc_notify_update_external_removed,
                               scratch_pool);

      notify->kind = kind;
      notify->err = err;

      (ctx->notify_func2)(ctx->notify_baton2, notify, scratch_pool);
    }

  if (err && err->apr_err == SVN_ERR_WC_LEFT_LOCAL_MOD)
    {
      svn_error_clear(err);
      err = NULL;
    }


  /* Unlock if we acquired the lock */
  if (lock_root_abspath != NULL)
    {
      svn_error_t *err2 = svn_wc__release_write_lock(ctx->wc_ctx,
                                                     lock_root_abspath,
                                                     scratch_pool);

      if (err2 && err2->apr_err == SVN_ERR_WC_NOT_LOCKED)
        {
          /* We removed the lock by removing the node, how nice! */
          svn_error_clear(err2);
        }
      else
        err = svn_error_compose_create(err, err2);
    }

  return svn_error_trace(err);
}

static svn_error_t *
handle_external_item_change(svn_client_ctx_t *ctx,
                            const char *repos_root_url,
                            const char *parent_dir_abspath,
                            const char *parent_dir_url,
                            const char *local_abspath,
                            const char *old_defining_abspath,
                            const svn_wc_external_item2_t *new_item,
                            svn_boolean_t *timestamp_sleep,
                            apr_pool_t *scratch_pool)
{
  svn_ra_session_t *ra_session;
  svn_client__pathrev_t *new_loc;
  const char *new_url;
  svn_node_kind_t ext_kind;

  SVN_ERR_ASSERT(repos_root_url && parent_dir_url);
  SVN_ERR_ASSERT(new_item != NULL);

  /* Don't bother to check status, since we'll get that for free by
     attempting to retrieve the hash values anyway.  */

  /* When creating the absolute URL, use the pool and not the
     iterpool, since the hash table values outlive the iterpool and
     any pointers they have should also outlive the iterpool.  */

  SVN_ERR(svn_wc__resolve_relative_external_url(&new_url,
                                                new_item, repos_root_url,
                                                parent_dir_url,
                                                scratch_pool, scratch_pool));

  /* Determine if the external is a file or directory. */
  /* Get the RA connection. */
  SVN_ERR(svn_client__ra_session_from_path2(&ra_session, &new_loc,
                                            new_url, NULL,
                                            &(new_item->peg_revision),
                                            &(new_item->revision), ctx,
                                            scratch_pool));

  SVN_ERR(svn_ra_check_path(ra_session, "", new_loc->rev, &ext_kind,
                            scratch_pool));

  if (svn_node_none == ext_kind)
    return svn_error_createf(SVN_ERR_RA_ILLEGAL_URL, NULL,
                             _("URL '%s' at revision %ld doesn't exist"),
                             new_loc->url, new_loc->rev);

  if (svn_node_dir != ext_kind && svn_node_file != ext_kind)
    return svn_error_createf(SVN_ERR_RA_ILLEGAL_URL, NULL,
                             _("URL '%s' at revision %ld is not a file "
                               "or a directory"),
                             new_loc->url, new_loc->rev);


  /* Not protecting against recursive externals.  Detecting them in
     the global case is hard, and it should be pretty obvious to a
     user when it happens.  Worst case: your disk fills up :-). */

  /* First notify that we're about to handle an external. */
  if (ctx->notify_func2)
    {
      (*ctx->notify_func2)(
         ctx->notify_baton2,
         svn_wc_create_notify(local_abspath,
                              svn_wc_notify_update_external,
                              scratch_pool),
         scratch_pool);
    }

  if (! old_defining_abspath)
    {
      /* The target dir might have multiple components.  Guarantee the path
         leading down to the last component. */
      SVN_ERR(svn_io_make_dir_recursively(svn_dirent_dirname(local_abspath,
                                                             scratch_pool),
                                          scratch_pool));
    }

  switch (ext_kind)
    {
      case svn_node_dir:
        SVN_ERR(switch_dir_external(local_abspath, new_url,
                                    &(new_item->peg_revision),
                                    &(new_item->revision),
                                    parent_dir_abspath,
                                    timestamp_sleep, ctx,
                                    scratch_pool));
        break;
      case svn_node_file:
        if (strcmp(repos_root_url, new_loc->repos_root_url))
          {
            const char *local_repos_root_url;
            const char *local_repos_uuid;
            const char *ext_repos_relpath;

            /*
             * The working copy library currently requires that all files
             * in the working copy have the same repository root URL.
             * The URL from the file external's definition differs from the
             * one used by the working copy. As a workaround, replace the
             * root URL portion of the file external's URL, after making
             * sure both URLs point to the same repository. See issue #4087.
             */

            SVN_ERR(svn_wc__node_get_repos_info(&local_repos_root_url,
                                                &local_repos_uuid,
                                                ctx->wc_ctx,
                                                parent_dir_abspath,
                                                scratch_pool, scratch_pool));
            ext_repos_relpath = svn_uri_skip_ancestor(new_loc->repos_root_url,
                                                      new_url, scratch_pool);
            if (local_repos_uuid == NULL || local_repos_root_url == NULL ||
                ext_repos_relpath == NULL ||
                strcmp(local_repos_uuid, new_loc->repos_uuid) != 0)
              return svn_error_createf(SVN_ERR_UNSUPPORTED_FEATURE, NULL,
                        _("Unsupported external: URL of file external '%s' "
                          "is not in repository '%s'"),
                        new_url, repos_root_url);

            new_url = svn_path_url_add_component2(local_repos_root_url,
                                                  ext_repos_relpath,
                                                  scratch_pool);
            SVN_ERR(svn_client__ra_session_from_path2(&ra_session, &new_loc,
                                                      new_url,
                                                      NULL,
                                                      &(new_item->peg_revision),
                                                      &(new_item->revision),
                                                      ctx, scratch_pool));
          }

        SVN_ERR(switch_file_external(local_abspath,
                                     new_url,
                                     &new_item->peg_revision,
                                     &new_item->revision,
                                     parent_dir_abspath,
                                     ra_session,
                                     ctx,
                                     scratch_pool));
        break;

      default:
        SVN_ERR_MALFUNCTION();
        break;
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
wrap_external_error(const svn_client_ctx_t *ctx,
                    const char *target_abspath,
                    svn_error_t *err,
                    apr_pool_t *scratch_pool)
{
  if (err && err->apr_err != SVN_ERR_CANCELLED)
    {
      if (ctx->notify_func2)
        {
          svn_wc_notify_t *notifier = svn_wc_create_notify(
                                            target_abspath,
                                            svn_wc_notify_failed_external,
                                            scratch_pool);
          notifier->err = err;
          ctx->notify_func2(ctx->notify_baton2, notifier, scratch_pool);
        }
      svn_error_clear(err);
      return SVN_NO_ERROR;
    }

  return err;
}

static svn_error_t *
handle_externals_change(svn_client_ctx_t *ctx,
                        const char *repos_root_url,
                        svn_boolean_t *timestamp_sleep,
                        const char *local_abspath,
                        const char *new_desc_text,
                        apr_hash_t *old_externals,
                        svn_depth_t ambient_depth,
                        svn_depth_t requested_depth,
                        apr_pool_t *scratch_pool)
{
  apr_array_header_t *new_desc;
  int i;
  apr_pool_t *iterpool;
  const char *url;

  iterpool = svn_pool_create(scratch_pool);

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  /* Bag out if the depth here is too shallow for externals action. */
  if ((requested_depth < svn_depth_infinity
       && requested_depth != svn_depth_unknown)
      || (ambient_depth < svn_depth_infinity
          && requested_depth < svn_depth_infinity))
    return SVN_NO_ERROR;

  if (new_desc_text)
    SVN_ERR(svn_wc_parse_externals_description3(&new_desc, local_abspath,
                                                new_desc_text,
                                                FALSE, scratch_pool));
  else
    new_desc = NULL;

  SVN_ERR(svn_wc__node_get_url(&url, ctx->wc_ctx, local_abspath,
                               scratch_pool, iterpool));

  SVN_ERR_ASSERT(url);

  for (i = 0; new_desc && (i < new_desc->nelts); i++)
    {
      const char *old_defining_abspath;
      svn_wc_external_item2_t *new_item;
      const char *target_abspath;

      new_item = APR_ARRAY_IDX(new_desc, i, svn_wc_external_item2_t *);

      svn_pool_clear(iterpool);

      if (ctx->cancel_func)
        SVN_ERR(ctx->cancel_func(ctx->cancel_baton));

      target_abspath = svn_dirent_join(local_abspath, new_item->target_dir,
                                       iterpool);

      old_defining_abspath = apr_hash_get(old_externals, target_abspath,
                                          APR_HASH_KEY_STRING);

      SVN_ERR(wrap_external_error(
                      ctx, target_abspath,
                      handle_external_item_change(ctx,
                                                  repos_root_url,
                                                  local_abspath, url,
                                                  target_abspath,
                                                  old_defining_abspath,
                                                  new_item,
                                                  timestamp_sleep,
                                                  iterpool),
                      iterpool));

      /* And remove already processed items from the to-remove hash */
      if (old_defining_abspath)
        apr_hash_set(old_externals, target_abspath, APR_HASH_KEY_STRING, NULL);
    }

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}


svn_error_t *
svn_client__handle_externals(apr_hash_t *externals_new,
                             apr_hash_t *ambient_depths,
                             const char *repos_root_url,
                             const char *target_abspath,
                             svn_depth_t requested_depth,
                             svn_boolean_t *timestamp_sleep,
                             svn_client_ctx_t *ctx,
                             apr_pool_t *scratch_pool)
{
  apr_hash_t *old_external_defs;
  apr_hash_index_t *hi;
  apr_pool_t *iterpool;

  SVN_ERR_ASSERT(repos_root_url);

  iterpool = svn_pool_create(scratch_pool);

  /* Parse the old externals. This part will be replaced by reading EXTERNALS
     from the DB. */
  SVN_ERR(svn_wc__externals_defined_below(&old_external_defs,
                                          ctx->wc_ctx, target_abspath,
                                          scratch_pool, iterpool));

  for (hi = apr_hash_first(scratch_pool, externals_new);
       hi;
       hi = apr_hash_next(hi))
    {
      const char *local_abspath = svn__apr_hash_index_key(hi);
      const char *desc_text = svn__apr_hash_index_val(hi);
      svn_depth_t ambient_depth = svn_depth_infinity;

      svn_pool_clear(iterpool);

      if (ambient_depths)
        {
          const char *ambient_depth_w;

          ambient_depth_w = apr_hash_get(ambient_depths, local_abspath,
                                         svn__apr_hash_index_klen(hi));

          if (ambient_depth_w == NULL)
            {
              return svn_error_createf(
                        SVN_ERR_WC_CORRUPT, NULL,
                        _("Traversal of '%s' found no ambient depth"),
                        svn_dirent_local_style(local_abspath, scratch_pool));
            }
          else
            {
              ambient_depth = svn_depth_from_word(ambient_depth_w);
            }
        }

      SVN_ERR(handle_externals_change(ctx, repos_root_url, timestamp_sleep,
                                      local_abspath,
                                      desc_text, old_external_defs,
                                      ambient_depth, requested_depth,
                                      iterpool));
    }

  /* Remove the remaining externals */
  for (hi = apr_hash_first(scratch_pool, old_external_defs);
       hi;
       hi = apr_hash_next(hi))
    {
      const char *item_abspath = svn__apr_hash_index_key(hi);
      const char *defining_abspath = svn__apr_hash_index_val(hi);
      const char *parent_abspath;

      svn_pool_clear(iterpool);

      SVN_ERR(wrap_external_error(
                          ctx, item_abspath,
                          handle_external_item_removal(ctx, defining_abspath,
                                                       item_abspath, iterpool),
                          iterpool));

      /* Are there any unversioned directories between the removed
       * external and the DEFINING_ABSPATH which we can remove? */
      parent_abspath = item_abspath;
      do {
        svn_wc_status3_t *parent_status;

        parent_abspath = svn_dirent_dirname(parent_abspath, iterpool);
        SVN_ERR(svn_wc_status3(&parent_status, ctx->wc_ctx, parent_abspath,
                               iterpool, iterpool));
        if (parent_status->node_status == svn_wc_status_unversioned)
          {
            svn_error_t *err;

            err = svn_io_dir_remove_nonrecursive(parent_abspath, iterpool);
            if (err && APR_STATUS_IS_ENOTEMPTY(err->apr_err))
              {
                svn_error_clear(err);
                break;
              }
            else
              SVN_ERR(err);
          }
      } while (strcmp(parent_abspath, defining_abspath) != 0);
    }


  svn_pool_destroy(iterpool);
  return SVN_NO_ERROR;
}


svn_error_t *
svn_client__export_externals(apr_hash_t *externals,
                             const char *from_url,
                             const char *to_abspath,
                             const char *repos_root_url,
                             svn_depth_t requested_depth,
                             const char *native_eol,
                             svn_boolean_t ignore_keywords,
                             svn_boolean_t *timestamp_sleep,
                             svn_client_ctx_t *ctx,
                             apr_pool_t *scratch_pool)
{
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);
  apr_pool_t *sub_iterpool = svn_pool_create(scratch_pool);
  apr_hash_index_t *hi;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(to_abspath));

  for (hi = apr_hash_first(scratch_pool, externals);
       hi;
       hi = apr_hash_next(hi))
    {
      const char *local_abspath = svn__apr_hash_index_key(hi);
      const char *desc_text = svn__apr_hash_index_val(hi);
      const char *local_relpath;
      const char *dir_url;
      apr_array_header_t *items;
      int i;

      svn_pool_clear(iterpool);

      SVN_ERR(svn_wc_parse_externals_description3(&items, local_abspath,
                                                  desc_text, FALSE,
                                                  scratch_pool));

      if (! items->nelts)
        continue;

      local_relpath = svn_dirent_skip_ancestor(to_abspath, local_abspath);

      dir_url = svn_path_url_add_component2(from_url, local_relpath,
                                            scratch_pool);

      for (i = 0; i < items->nelts; i++)
        {
          const char *item_abspath;
          const char *new_url;
          svn_wc_external_item2_t *item = APR_ARRAY_IDX(items, i,
                                                svn_wc_external_item2_t *);

          svn_pool_clear(sub_iterpool);

          item_abspath = svn_dirent_join(local_abspath, item->target_dir,
                                         sub_iterpool);

          SVN_ERR(svn_wc__resolve_relative_external_url(&new_url, item,
                                                        repos_root_url,
                                                        dir_url, sub_iterpool,
                                                        sub_iterpool));

          /* The target dir might have multiple components.  Guarantee
             the path leading down to the last component. */
          SVN_ERR(svn_io_make_dir_recursively(svn_dirent_dirname(item_abspath,
                                                                 sub_iterpool),
                                              sub_iterpool));

          SVN_ERR(wrap_external_error(
                          ctx, item_abspath,
                          svn_client_export5(NULL, new_url, item_abspath,
                                             &item->peg_revision,
                                             &item->revision,
                                             TRUE, FALSE, ignore_keywords,
                                             svn_depth_infinity,
                                             native_eol,
                                             ctx, sub_iterpool),
                          sub_iterpool));
        }
    }

  svn_pool_destroy(sub_iterpool);
  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}


svn_error_t *
svn_client__do_external_status(svn_client_ctx_t *ctx,
                               apr_hash_t *external_map,
                               svn_depth_t depth,
                               svn_boolean_t get_all,
                               svn_boolean_t update,
                               svn_boolean_t no_ignore,
                               svn_client_status_func_t status_func,
                               void *status_baton,
                               apr_pool_t *pool)
{
  apr_hash_index_t *hi;
  apr_pool_t *iterpool = svn_pool_create(pool);

  /* Loop over the hash of new values (we don't care about the old
     ones).  This is a mapping of versioned directories to property
     values. */
  for (hi = apr_hash_first(pool, external_map);
       hi;
       hi = apr_hash_next(hi))
    {
      svn_node_kind_t external_kind;
      const char *local_abspath = svn__apr_hash_index_key(hi);
      const char *defining_abspath = svn__apr_hash_index_val(hi);
      svn_node_kind_t kind;
      svn_opt_revision_t opt_rev;

      svn_pool_clear(iterpool);

      /* Obtain information on the expected external. */
      SVN_ERR(svn_wc__read_external_info(&external_kind, NULL, NULL, NULL,
                                         &opt_rev.value.number,
                                         ctx->wc_ctx, defining_abspath,
                                         local_abspath, FALSE,
                                         iterpool, iterpool));

      if (external_kind != svn_node_dir)
        continue;

      SVN_ERR(svn_io_check_path(local_abspath, &kind, iterpool));
      if (kind != svn_node_dir)
        continue;

      if (SVN_IS_VALID_REVNUM(opt_rev.value.number))
        opt_rev.kind = svn_opt_revision_number;
      else
        opt_rev.kind = svn_opt_revision_unspecified;

      /* Tell the client we're starting an external status set. */
      if (ctx->notify_func2)
        ctx->notify_func2(
               ctx->notify_baton2,
               svn_wc_create_notify(local_abspath,
                                    svn_wc_notify_status_external,
                                    iterpool), iterpool);

      /* And then do the status. */
      SVN_ERR(svn_client_status5(NULL, ctx, local_abspath, &opt_rev, depth,
                                 get_all, update, no_ignore, FALSE, FALSE,
                                 NULL, status_func, status_baton,
                                 iterpool));
    }

  /* Destroy SUBPOOL and (implicitly) ITERPOOL. */
  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}


/* Implements the `svn_wc_externals_update_t' interface. */
svn_error_t *
svn_client__external_info_gatherer(void *baton,
                                   const char *local_abspath,
                                   const svn_string_t *old_value,
                                   const svn_string_t *new_value,
                                   svn_depth_t depth,
                                   apr_pool_t *scratch_pool)
{
  svn_client__external_func_baton_t *efb = baton;

  local_abspath = apr_pstrdup(efb->result_pool, local_abspath);

  if (efb->externals_old != NULL && old_value != NULL)
    apr_hash_set(efb->externals_old, local_abspath, APR_HASH_KEY_STRING,
                 apr_pstrndup(efb->result_pool,
                              old_value->data, old_value->len));

  if (efb->externals_new != NULL && new_value != NULL)
    apr_hash_set(efb->externals_new, local_abspath, APR_HASH_KEY_STRING,
                 apr_pstrndup(efb->result_pool,
                              new_value->data, new_value->len));

  if (efb->ambient_depths != NULL)
    apr_hash_set(efb->ambient_depths, local_abspath, APR_HASH_KEY_STRING,
                 svn_depth_to_word(depth));

  return SVN_NO_ERROR;
}


