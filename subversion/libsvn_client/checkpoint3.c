/*
 * checkpoint3.c:  implementation of 'checkpoint' by commits in a local repo
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

#include <string.h>
#include <apr_strings.h>
#include <apr_hash.h>
#include "svn_hash.h"
#include "svn_wc.h"
#include "svn_ra.h"
#include "svn_client.h"
#include "svn_string.h"
#include "svn_pools.h"
#include "svn_error.h"
#include "svn_error_codes.h"
#include "svn_dirent_uri.h"
#include "svn_path.h"
#include "svn_sorts.h"
#include "svn_props.h"
#include "svn_repos.h"

#include "client.h"
#include "private/svn_wc_private.h"
#include "private/svn_ra_private.h"
#include "private/svn_sorts_private.h"

#include "svn_private_config.h"


/* -------------------- checkpoint repo -------------------- */

/* Return the abspatch to the checkpoints repo.
 *
 * It need not necessarily exist.
 */
static char *
checkpoints_repo_dir(const char *wc_root_abspath,
                     apr_pool_t *result_pool)
{
  return svn_dirent_join_many(result_pool,
                              wc_root_abspath, ".svn", "checkpoints",
                              SVN_VA_NULL);
}

/* ### temp: we should not be accessing the repo via URL but directly */
static const char *
checkpoints_repo_url(const char *wc_root_abspath,
                     apr_pool_t *result_pool)
{
  char *path = svn_dirent_join_many(result_pool,
                                    wc_root_abspath,".svn", "checkpoints",
                                    SVN_VA_NULL);
  const char *url;

  svn_error_clear(svn_uri_get_file_url_from_dirent(&url, path, result_pool));
  return url;
}

/* Store the original repos URL in a file the checkpoints repo dir. */
static svn_error_t *
write_original_repos_url(const char *url,
                         const char *wc_root_abspath,
                         apr_pool_t *scratch_pool)
{
  const char *filename = svn_dirent_join_many(scratch_pool,
                                              wc_root_abspath,
                                              ".svn", "checkpoints", "orig-url",
                                              SVN_VA_NULL);

  svn_error_clear(svn_io_write_atomic2(filename, url, strlen(url),
                                       NULL /*copy_perms_path*/,
                                       FALSE /*flush*/,
                                       scratch_pool));
  return SVN_NO_ERROR;
}

/* Retrieve the original repos URL from its file in the checkpoints repo dir. */
static const char *
read_original_repos_url(const char *wc_root_abspath,
                        apr_pool_t *result_pool)
{
  const char *filename = svn_dirent_join_many(result_pool,
                                              wc_root_abspath,
                                              ".svn", "checkpoints", "orig-url",
                                              SVN_VA_NULL);
  svn_stringbuf_t *url;

  svn_error_clear(svn_stringbuf_from_file2(&url, filename, result_pool));
  return url->data;
}

/* Create the repo if it is not already present.
 *
 * Open the repo and return it in *REPOS_P.
 */
static svn_error_t *
checkpoints_repo_create(svn_repos_t **repos_p,
                        const char *wc_root_abspath,
                        const char *uuid,
                        apr_pool_t *scratch_pool)
{
  const char *repo_dir
    = checkpoints_repo_dir(wc_root_abspath, scratch_pool);

  SVN_ERR(svn_repos_create(repos_p, repo_dir,
                           NULL, NULL, NULL, NULL, scratch_pool));
  SVN_ERR(svn_fs_set_uuid(svn_repos_fs(*repos_p), uuid, scratch_pool));
  return SVN_NO_ERROR;
}

/* Open the checkpoints repo.
 */
static svn_error_t *
checkpoints_repo_open(svn_repos_t **repos_p,
                      const char *wc_root_abspath,
                      apr_pool_t *result_pool,
                      apr_pool_t *scratch_pool)
{
  const char *repo_dir
    = checkpoints_repo_dir(wc_root_abspath, scratch_pool);

  SVN_ERR(svn_repos_open3(repos_p, repo_dir,
                          NULL, result_pool, scratch_pool));
  return SVN_NO_ERROR;
}

/*  */
static svn_error_t *
checkpoints_repo_destroy(const char *wc_root_abspath,
                         apr_pool_t *scratch_pool)
{
  printf("  (destroying the repo is not implemented yet)\n");
  return SVN_NO_ERROR;
}

/* Prune revisions later than NEW_HEAD from the repo at REPO_DIR.
 * ### Ultra hacky!
 */
static svn_error_t *
checkpoints_repo_prune(const char *repo_dir,
                       svn_revnum_t new_head,
                       apr_pool_t *scratch_pool)
{
  char *current_file_path = svn_path_join(repo_dir, "db/current", scratch_pool);
  char *rep_cache_db_path = svn_path_join(repo_dir, "db/rep-cache.db", scratch_pool);
  int old_head, r;

  SVN_ERR(svn_io_read_version_file(&old_head, current_file_path, scratch_pool));
  SVN_ERR(svn_io_write_version_file(current_file_path, new_head, scratch_pool));

  /* Remove later rev files (at least the revprop files need to be gone) */
  for (r = old_head; r > new_head; r--)
    {
      char *rev_file_path
        = apr_psprintf(scratch_pool, "%s/db/revs/0/%d", repo_dir, r);
      char *revprop_file_path
        = apr_psprintf(scratch_pool, "%s/db/revprops/0/%d", repo_dir, r);

      printf("  (pruning checkpoint %d (r%d))\n", r - 1, r);
      SVN_ERR(svn_io_remove_file2(rev_file_path, FALSE /*ignore_enoent*/,
                                  scratch_pool));
      SVN_ERR(svn_io_remove_file2(revprop_file_path, FALSE /*ignore_enoent*/,
                                  scratch_pool));
    }
  /* Remove later revs from the rep cache */
  /* ### For now, remove the whole rep cache */
  SVN_ERR(svn_io_remove_file2(rep_cache_db_path, TRUE /*ignore_enoent*/,
                              scratch_pool));
  return SVN_NO_ERROR;
}

/* -------------------- transfers -------------------- */

/* Update the WC to r(checkpoint_number + 1).
 */
static svn_error_t *
checkpoint_update(int checkpoint_number,
                  const char *wc_root_abspath,
                  svn_client_ctx_t *ctx,
                  apr_pool_t *scratch_pool)
{
  apr_array_header_t *paths = apr_array_make(scratch_pool, 1, sizeof(char *));
  svn_opt_revision_t revision = {svn_opt_revision_number};

  APR_ARRAY_PUSH(paths, const char *) = wc_root_abspath;
  revision.value.number = checkpoint_number + 1;

  SVN_ERR(svn_client_update4(NULL /*result_revs*/,
                             paths, &revision,
                             svn_depth_infinity,
                             FALSE /*depth_is_sticky*/,
                             TRUE /*ignore_externals*/,
                             TRUE /*allow_unver_obstructions*/,
                             TRUE /*adds_as_modification*/,
                             FALSE /*make_parents*/,
                             ctx, scratch_pool));

  return SVN_NO_ERROR;
}

/* Copy the WC base into the repo.
 *
 * ### Uses 'import' from disk into a commit editor.
 * ### Lots of problems with that: loses props, adds unversioned files, etc.
 * ### Instead, must export content and props directly from WC base.
 */
static svn_error_t *
copy_base(const char *wc_root_abspath,
          svn_repos_t *repos,
          svn_client_ctx_t *ctx,
          apr_pool_t *scratch_pool)
{
  const svn_delta_editor_t *editor;
  void *edit_baton;
  const char *repos_url = checkpoints_repo_url(wc_root_abspath, scratch_pool);
  /* List of in-repo path components from repo root to WC root. */
  apr_array_header_t *new_entries = apr_array_make(scratch_pool, 4,
                                                   sizeof(const char *));
  svn_boolean_t updated_repository;

  SVN_ERR(svn_repos_get_commit_editor5(&editor, &edit_baton,
                                       repos,
                                       NULL /*txn*/,
                                       "" /*repos_url_decoded*/,
                                       "" /*base_path*/,
                                       apr_hash_make(scratch_pool) /*revprop_table*/,
                                       NULL, NULL, /*commit cb*/
                                       NULL, NULL, /*authz cb*/
                                       scratch_pool));

  SVN_ERR(svn_client__import_internal(
            &updated_repository,
            wc_root_abspath,
            repos_url, /* used just for a notification */
            new_entries, editor, edit_baton,
            svn_depth_infinity, 0 /*base_rev*/,
            apr_hash_make(scratch_pool) /*excludes*/,
            NULL /*autoprops*/,
            NULL /*local_ignores_arr*/, NULL /*global_ignores*/,
            TRUE /*no_ignore*/,
            TRUE /*no_autoprops*/,
            TRUE /*ignore_unknown_node_types*/,
            NULL, NULL, /*filter cb*/
            ctx, scratch_pool));
  return SVN_NO_ERROR;
}

/* Switch the WC base to point to the new repo.
 */
static svn_error_t *
switch_base(const char *wc_root_abspath,
            svn_repos_t *repos,
            svn_client_ctx_t *ctx,
            apr_pool_t *scratch_pool)
{
  const char *orig_repos_root_url;
  const char *repos_url = checkpoints_repo_url(wc_root_abspath, scratch_pool);

  SVN_ERR(svn_client_get_repos_root(&orig_repos_root_url, NULL /* uuid */,
                                    wc_root_abspath,
                                    ctx, scratch_pool, scratch_pool));
  SVN_ERR(svn_client_relocate2(wc_root_abspath,
                               orig_repos_root_url, repos_url,
                               TRUE /*ignore_externals*/,
                               ctx, scratch_pool));
  SVN_ERR(write_original_repos_url(orig_repos_root_url,
                                   wc_root_abspath, scratch_pool));
  return SVN_NO_ERROR;
}

/* Switch the WC base to point to the original repo.
 */
static svn_error_t *
switch_to_original_base(const char *wc_root_abspath,
                        svn_repos_t *repos,
                        svn_client_ctx_t *ctx,
                        apr_pool_t *scratch_pool)
{
  const char *orig_repos_url = read_original_repos_url(wc_root_abspath, scratch_pool);
  const char *repos_url = checkpoints_repo_url(wc_root_abspath, scratch_pool);

  SVN_ERR(svn_client_relocate2(wc_root_abspath,
                               repos_url, orig_repos_url,
                               TRUE /*ignore_externals*/,
                               ctx, scratch_pool));
  return SVN_NO_ERROR;
}

/*
 * Assumes no uncommitted mods.
 */
static svn_error_t *
squash_to_working_state(const char *wc_root_abspath,
                        svn_client_ctx_t *ctx,
                        apr_pool_t *scratch_pool)
{
  apr_pool_t *repos_pool = svn_pool_create(scratch_pool);
  svn_repos_t *repos;
  svn_opt_revision_t peg_revision1 = {svn_opt_revision_head};
  apr_array_header_t *ranges_to_merge
    = apr_array_make(scratch_pool, 1, sizeof(void *));
  svn_opt_revision_range_t range = { {svn_opt_revision_number, {1}},
                                     {svn_opt_revision_head} };

  SVN_ERR(checkpoints_repo_open(&repos, wc_root_abspath,
                                repos_pool, scratch_pool));

  /* update to r1 */
  printf("  (squash: updating to r1)\n");
  SVN_ERR(checkpoint_update(0 /*checkpoint_number*/, wc_root_abspath,
                            ctx, scratch_pool));

  /* merge all committed changes into WC, with no mergeinfo */
  printf("  (squash: merging checkpointed changes into WC)\n");
  APR_ARRAY_PUSH(ranges_to_merge, void *) = &range;
  SVN_ERR(svn_client_merge_peg5("" /*sourcepath1*/,
                                ranges_to_merge,
                                &peg_revision1,
                                "" /*targetpath*/,
                                svn_depth_infinity,
                                TRUE /*ignore_mergeinfo*/,
                                FALSE /*diff_ignore_ancestry*/,
                                FALSE /*force_delete*/,
                                FALSE /*record_only*/,
                                FALSE /*dry_run*/,
                                FALSE /*allow_mixed_rev*/,
                                NULL /*options*/,
                                ctx, scratch_pool));

  /* Close the repo */
  svn_pool_destroy(repos_pool);

  return SVN_NO_ERROR;
}

/* Start a series of checkpoints.
 *
 * Create a local repo.
 * Copy the WC base into it.
 * Switch the WC to point at it.
 *
 * Assumptions (for initial prototype):
 *   WC is clean
 *   WC is single-rev
 *   WC base is at r1!
 *   WC root URL is repos root URL
 */
static svn_error_t *
checkpoints_init(const char *wc_root_abspath,
                 svn_client_ctx_t *ctx,
                 apr_pool_t *scratch_pool)
{
  const char *uuid;
  apr_pool_t *repos_pool = svn_pool_create(scratch_pool);
  svn_repos_t *repos;

  SVN_ERR(svn_client_uuid_from_path2(&uuid, wc_root_abspath,
                                     ctx, scratch_pool, scratch_pool));

  printf("-- creating local repo at '.svn/checkpoints'\n");
  SVN_ERR(checkpoints_repo_create(&repos, wc_root_abspath, uuid, repos_pool));

  printf("-- copying WC base into local repo r1\n");
  printf("  (cheating: just importing from disk: disregards properties etc.)\n");
  SVN_ERR(copy_base(wc_root_abspath, repos, ctx, scratch_pool));

  printf("-- switching/relocating WC base to point at local repo r1\n");
  SVN_ERR(switch_base(wc_root_abspath, repos, ctx, scratch_pool));

  /* Close the repo */
  svn_pool_destroy(repos_pool);
  return SVN_NO_ERROR;
}

/* End a series of checkpoints.
 *
 * Switch the WC base to point at the original repo and base revision(s).
 * Make the WC working state be the checkpoint final state.
 * Destroy the local repo.
 *
 * Assumptions (for initial prototype):
 */
static svn_error_t *
checkpoints_uninit(const char *wc_root_abspath,
                   svn_client_ctx_t *ctx,
                   apr_pool_t *scratch_pool)
{
  apr_pool_t *repos_pool = svn_pool_create(scratch_pool);
  svn_repos_t *repos;

  SVN_ERR(checkpoints_repo_open(&repos, wc_root_abspath,
                                repos_pool, scratch_pool));

  printf("-- switching/relocating WC back to original base URL & rev\n");
  SVN_ERR(switch_to_original_base(wc_root_abspath, repos, ctx, scratch_pool));

  /* Close the repo */
  svn_pool_destroy(repos_pool);

  printf("-- destroying the checkpoints repo\n");
  SVN_ERR(checkpoints_repo_destroy(wc_root_abspath, scratch_pool));

  return SVN_NO_ERROR;
}

/* Set *CURRENT to the current checkpoint number.
 */
static svn_error_t *
read_current(int *current,
             const char *wc_root_abspath,
             svn_client_ctx_t *ctx,
             apr_pool_t *scratch_pool)
{
  apr_pool_t *repos_pool = svn_pool_create(scratch_pool);
  svn_repos_t *repos;
  svn_revnum_t youngest;

  SVN_ERR(checkpoints_repo_open(&repos, wc_root_abspath,
                                repos_pool, scratch_pool));
  SVN_ERR(svn_fs_youngest_rev(&youngest, svn_repos_fs(repos), scratch_pool));
  *current = (youngest - 1);

  /* Close the repo */
  svn_pool_destroy(repos_pool);
  return SVN_NO_ERROR;
}

/* -------------------- checkpoint save -------------------- */

typedef struct commit_baton_t {
  svn_revnum_t revision;
} commit_baton_t;

static svn_error_t *
commit_callback(const svn_commit_info_t *commit_info,
                void *baton,
                apr_pool_t *scratch_pool)
{
  commit_baton_t *cb = baton;

  cb->revision = commit_info->revision;
  return SVN_NO_ERROR;
}

/* Write a checkpoint of the whole WC.
 */
static svn_error_t *
checkpoint_save(int *checkpoint_number,
                /*const apr_array_header_t *paths,
                  svn_depth_t depth,
                  const apr_array_header_t *changelists,*/
                const char *wc_root_abspath,
                svn_client_ctx_t *ctx,
                apr_pool_t *scratch_pool)
{
  apr_array_header_t *paths = apr_array_make(scratch_pool, 1, sizeof(char *));
  commit_baton_t cb = { 0 };

  APR_ARRAY_PUSH(paths, const char *) = wc_root_abspath;

  /* ### TODO: if not at latest checkpoint, first prune later checkpoints */

  ctx->log_msg_func3 = NULL;
  ctx->log_msg_func2 = NULL;
  ctx->log_msg_func = NULL;
  SVN_ERR(svn_client_commit6(
            paths,
            svn_depth_infinity /*depth*/,
            TRUE /*keep_locks*/,
            TRUE /*keep_changelists*/,
            TRUE /*commit_as_operations*/,
            FALSE, /* include_file_externals */
            FALSE, /* include_dir_externals */
            NULL /*changelists*/,
            NULL /*revprop_table*/,
            commit_callback, &cb,
            ctx, scratch_pool));

  if (checkpoint_number)
    *checkpoint_number = (cb.revision - 1);
  return SVN_NO_ERROR;
}

/* -------------------- checkpoint ... -------------------- */

/*  */
static svn_error_t *
checkpoint_revert(int checkpoint_number,
                  const char *wc_root_abspath,
                  svn_boolean_t dry_run,
                  svn_client_ctx_t *ctx,
                  apr_pool_t *scratch_pool)
{
  apr_array_header_t *paths = apr_array_make(scratch_pool, 1, sizeof(char *));

  APR_ARRAY_PUSH(paths, const char *) = wc_root_abspath;

  /* Revert the WC local mods */
  printf("-- reverting any uncheckpointed modifications\n");
  SVN_ERR(svn_client_revert3(paths,
                             svn_depth_infinity,
                             NULL /*changelists*/,
                             FALSE /*clear_changelists*/,
                             FALSE /*metadata_only*/,
                             ctx, scratch_pool));

  printf("-- performing an 'update' to revert to checkpoint %d (r%d)\n",
         checkpoint_number, checkpoint_number+1);
  SVN_ERR(checkpoint_update(checkpoint_number, wc_root_abspath,
                            ctx, scratch_pool));

  /* Prune later revisions from the repo */
  printf("-- pruning any later checkpoint revisions from the repository\n");
  SVN_ERR(checkpoints_repo_prune(
            checkpoints_repo_dir(wc_root_abspath, scratch_pool),
            checkpoint_number + 1, scratch_pool));

  return SVN_NO_ERROR;
}

/* -------------------- checkpoint ... -------------------- */

svn_error_t *
svn_client_checkpoint_init(const char *local_abspath,
                           svn_client_ctx_t *ctx,
                           apr_pool_t *scratch_pool)
{
  const char *wc_root_abspath;

  SVN_ERR(svn_client_get_wc_root(&wc_root_abspath, local_abspath,
                                 ctx, scratch_pool, scratch_pool));
  SVN_ERR(checkpoints_init(wc_root_abspath, ctx, scratch_pool));
  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_checkpoint_squash(const char *local_abspath,
                             svn_client_ctx_t *ctx,
                             apr_pool_t *scratch_pool)
{
  const char *wc_root_abspath;

  SVN_ERR(svn_client_get_wc_root(&wc_root_abspath, local_abspath,
                                 ctx, scratch_pool, scratch_pool));

  printf("-- checkpointing any uncheckpointed modifications\n");
  SVN_ERR(checkpoint_save(NULL /*checkpoint_number*/,
                          wc_root_abspath, ctx, scratch_pool));

  printf("-- squashing checkpoints to a working state\n");
  SVN_ERR(squash_to_working_state(wc_root_abspath,
                                  ctx, scratch_pool));

  /* Prune later revisions from the repo */
  SVN_ERR(checkpoints_repo_prune(
            checkpoints_repo_dir(wc_root_abspath, scratch_pool),
            1 /*new_head revision*/, scratch_pool));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_checkpoint_uninit(const char *local_abspath,
                             svn_client_ctx_t *ctx,
                             apr_pool_t *scratch_pool)
{
  const char *wc_root_abspath;

  SVN_ERR(svn_client_checkpoint_squash(local_abspath, ctx, scratch_pool));

  SVN_ERR(svn_client_get_wc_root(&wc_root_abspath, local_abspath,
                                 ctx, scratch_pool, scratch_pool));

  SVN_ERR(checkpoints_uninit(wc_root_abspath, ctx, scratch_pool));
  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_checkpoint_get_current(int *checkpoint_number_p,
                                  const char *local_abspath,
                                  svn_client_ctx_t *ctx,
                                  apr_pool_t *scratch_pool)
{
  const char *wc_root_abspath;

  SVN_ERR(svn_client_get_wc_root(&wc_root_abspath, local_abspath,
                                 ctx, scratch_pool, scratch_pool));

  SVN_ERR(read_current(checkpoint_number_p, wc_root_abspath, ctx, scratch_pool));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_checkpoint_save(int *checkpoint_number,
                           const char *local_abspath,
                           svn_client_ctx_t *ctx,
                           apr_pool_t *scratch_pool)
{
  const char *wc_root_abspath;

  SVN_ERR(svn_client_get_wc_root(&wc_root_abspath, local_abspath,
                                 ctx, scratch_pool, scratch_pool));

  SVN_ERR(checkpoint_save(checkpoint_number,
                          wc_root_abspath, ctx, scratch_pool));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_checkpoint_revert(int checkpoint_number,
                             const char *local_abspath,
                             svn_boolean_t dry_run,
                             svn_client_ctx_t *ctx,
                             apr_pool_t *scratch_pool)
{
  const char *wc_root_abspath;

  SVN_ERR(svn_client_get_wc_root(&wc_root_abspath, local_abspath,
                                 ctx, scratch_pool, scratch_pool));

  /* ### TODO: Save the current state (of whole WC) */

  /* Restore the requested checkpoint */
  SVN_ERR(checkpoint_revert(checkpoint_number, wc_root_abspath,
                            dry_run,
                            ctx, scratch_pool));

  return SVN_NO_ERROR;
}

/* -------------------- checkpoint list -------------------- */

#if 0
typedef struct path_change_baton_t
{
  apr_array_header_t *changes;
  apr_pool_t *result_pool;
} path_change_baton_t;

/*  */
static svn_error_t *
path_change_func(void *baton,
                 svn_repos_path_change_t *change,
                 apr_pool_t *scratch_pool)
{
  path_change_baton_t *b = baton;

  APR_ARRAY_PUSH(b->changes, void *) = svn_repos_path_change_dup(change, b->result_pool);
  return SVN_NO_ERROR;
}
#endif

typedef struct log_entry_baton_t
{
  /*path_change_baton_t *pb;*/
  apr_array_header_t *entries;
  apr_pool_t *result_pool;
} log_entry_baton_t;

/*  */
static svn_error_t *
log_entry_func(void *baton,
               svn_repos_log_entry_t *repos_log_entry,
               apr_pool_t *scratch_pool)
{
  log_entry_baton_t *b = baton;
  svn_log_entry_t *std_log_entry = svn_log_entry_create(b->result_pool);

  std_log_entry->revision = repos_log_entry->revision;
  std_log_entry->revprops = svn_prop_hash_dup(repos_log_entry->revprops,
                                              b->result_pool);
  /*std_log_entry->changed_paths2 = b->pb->changes;*/
  /*b->pb->changes = apr_hash_make(b->pb->result_pool);*/
  APR_ARRAY_PUSH(b->entries, void *) = std_log_entry;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_checkpoint_list(apr_array_header_t **checkpoints,
                           const char *wc_root_abspath,
                           svn_client_ctx_t *ctx,
                           apr_pool_t *result_pool,
                           apr_pool_t *scratch_pool)
{
  /*path_change_baton_t pb;*/
  log_entry_baton_t lb;
  apr_pool_t *repos_pool = svn_pool_create(scratch_pool);
  svn_repos_t *repos;

  SVN_ERR(checkpoints_repo_open(&repos, wc_root_abspath,
                                repos_pool, scratch_pool));

  /*pb.changes = apr_array_make(scratch_pool, 0, sizeof(void *));*/
  /*pb.result_pool = result_pool;*/
  /*lb.pb = &pb;*/
  lb.entries = apr_array_make(scratch_pool, 0, sizeof(void *));
  lb.result_pool = result_pool;

  SVN_ERR(svn_repos_get_logs5(repos, NULL /*paths*/,
                              SVN_INVALID_REVNUM /*start*/, 1 /*end*/,
                              0 /*limit*/,
                              TRUE /*strict_node_history*/,
                              FALSE /*include_merged_revisions*/,
                              NULL /*revprops*/,
                              NULL, NULL, /*authz_read*/
                              NULL, NULL, /*path_change_func, &pb,*/
                              log_entry_func, &lb,
                              scratch_pool));

  *checkpoints = lb.entries;

  /* Close the repo */
  svn_pool_destroy(repos_pool);
  return SVN_NO_ERROR;
}

