/**
 * @copyright
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
 * @endcopyright
 *
 * @file svnmover.h
 * @brief Concept Demo for Move Tracking and Branching
 */

#ifndef SVNMOVER_H
#define SVNMOVER_H

#include "svn_types.h"
#include "svn_client.h"
#include "svn_ra.h"

#include "private/svn_branch.h"
#include "private/svn_branch_compat.h"

/* Decide whether to use the 'linenoise' library for command-line input
   editing and completion. */
#ifndef WIN32
#define HAVE_LINENOISE
#endif


#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/* Like apr_hash_overlay() and apr_hash_merge() except allocating the
 * result in the pool of the first input hash (OVERLAY and H1 respectively).
 *
 * When APR pool debugging is enabled, these functions require that the
 * result pool does not have greater lifetime than the inputs, so passing
 * an arbitrary result pool doesn't work well.
 *
 * If the second hash's pool has a shorter lifetime than that of the first,
 * you're out of luck.
 */
#define hash_overlay(overlay, base) \
  apr_hash_overlay(apr_hash_pool_get(overlay), overlay, base)
#define hash_merge(overlay, h1) \
  apr_hash_merge(apr_hash_pool_get(overlay), h1, h2, merger, data)


enum { UI_MODE_EIDS, UI_MODE_PATHS, UI_MODE_SERIAL };
extern int the_ui_mode;


/* Display PROMPT_STR, read a line of text, and set *RESULT to that line.
 *
 * The interface here is similar to svn_cmdline_prompt_user2().
 */
svn_error_t *
svnmover_prompt_user(const char **result,
                     const char *prompt_str,
                     apr_pool_t *pool);

/* Print a notification. */
__attribute__((format(printf, 1, 2)))
void
svnmover_notify(const char *fmt,
                ...);

/* Print a verbose notification: in 'quiet' mode, don't print it. */
__attribute__((format(printf, 1, 2)))
void
svnmover_notify_v(const char *fmt,
                  ...);

typedef struct svnmover_wc_version_t
{
  svn_revnum_t revision;  /* always SVN_INVALID_REVNUM in working version */
  svn_branch__state_t *branch;
} svnmover_wc_version_t;

/* Return (left, right) pairs of element content that differ between
 * LEFT and RIGHT.
 *
 * Examine only the elements listed in ELEMENTS, a hash of (eid ->
 * [anything]). If ELEMENTS is NULL, use the union of LEFT and RIGHT.
 *
 * LEFT and/or RIGHT may be null, meaning an empty set of elements.
 *
 * Set *DIFF_P to a hash of (eid -> (svn_element__content_t *)[2]).
 */
svn_error_t *
svnmover_element_differences(apr_hash_t **diff_p,
                             const svn_element__tree_t *left,
                             const svn_element__tree_t *right,
                             apr_hash_t *elements,
                             apr_pool_t *result_pool,
                             apr_pool_t *scratch_pool);

/*  */
typedef struct conflict_storage_t conflict_storage_t;

typedef struct svnmover_wc_t
{
  apr_pool_t *pool;
  const char *repos_root_url;
  /*const char *anchor_repos_relpath;*/
  svn_revnum_t head_revision;

  svn_ra_session_t *ra_session;
  svn_branch__txn_t *edit_txn;
  conflict_storage_t *conflicts;

  /* The base revisions, for (at least) all EIDs in BASE:
     branch_id -> hash { eid -> revnum } */
  apr_hash_t *base_revs;

  /* Base and working versions. */
  svnmover_wc_version_t *base, *working;

  /* Textual list of commands the commands that were executed, suitable
     for putting in a log message */
  const char *list_of_commands;

  svn_client_ctx_t *ctx;

} svnmover_wc_t;

struct conflict_storage_t
{
  svn_branch__state_t *yca_branch, *src_branch, *tgt_branch, *merged_branch;

  /* Single-element conflicts */
  /* (eid -> element_merge3_conflict_t) */
  apr_hash_t *element_merge_conflicts;

  /* Name-clash conflicts */
  /* ("%{parent_eid}d/%{name}s" -> name_clash_conflict_t) */
  apr_hash_t *name_clash_conflicts;

  /* Cycle conflicts */
  /* (eid -> cycle_conflict_t) */
  apr_hash_t *cycle_conflicts;

  /* Orphan conflicts */
  /* (eid -> orphan_conflict_t) */
  apr_hash_t *orphan_conflicts;
};

/* Three-way-merge the changes from YCA to SRC and YCA to TGT. YCA is
 * notionally the youngest common ancestor of SRC and TGT.
 *
 * The elements to merge are the union of the elements in the three input
 * subtrees (SRC, TGT, YCA). For each such element, merge the two changes:
 * YCA -> SRC and YCA -> TGT, applying the result to EDIT_TXN:EDIT_BRANCH.
 *
 * If conflicts arise, return them in *CONFLICT_STORAGE_P; otherwise set
 * that to null.
 *
 * SRC, TGT and YCA must be existing and corresponding (same EID) elements.
 *
 * None of SRC, TGT and YCA is a subbranch root element.
 *
 * Nested subbranches will also be merged.
 */
svn_error_t *
svnmover_branch_merge(svn_branch__txn_t *edit_txn,
                      svn_branch__state_t *edit_branch,
                      conflict_storage_t **conflict_storage_p,
                      svn_branch__el_rev_id_t *src,
                      svn_branch__el_rev_id_t *tgt,
                      svn_branch__el_rev_id_t *yca,
                      apr_pool_t *result_pool,
                      apr_pool_t *scratch_pool);

/*  */
svn_error_t *
svnmover_display_conflicts(conflict_storage_t *conflict_storage,
                           apr_pool_t *scratch_pool);

svn_error_t *
svnmover_conflict_resolved(conflict_storage_t *conflicts,
                           const char *id_string,
                           apr_pool_t *scratch_pool);

/*  */
svn_boolean_t
svnmover_any_conflicts(const conflict_storage_t *conflicts);

/* Load branching info.
 */
svn_error_t *
svn_ra_load_branching_state(svn_branch__txn_t **branching_txn_p,
                            svn_branch__compat_fetch_func_t *fetch_func,
                            void **fetch_baton,
                            svn_ra_session_t *session,
                            const char *branch_info_dir,
                            svn_revnum_t base_revision,
                            apr_pool_t *result_pool,
                            apr_pool_t *scratch_pool);

/* Ev3 version of svn_ra_get_commit_editor().
 *
 * If BRANCH_INFO_DIR is non-null, store branching info in that local
 * directory, otherwise store branching info in revprops.
 */
svn_error_t *
svn_ra_get_commit_txn(svn_ra_session_t *session,
                      svn_branch__txn_t **edit_txn_p,
                      apr_hash_t *revprop_table,
                      svn_commit_callback2_t commit_callback,
                      void *commit_baton,
                      apr_hash_t *lock_tokens,
                      svn_boolean_t keep_locks,
                      const char *branch_info_dir,
                      apr_pool_t *pool);

/** Describes a server-side move (really a copy+delete within the same
 * revision) which has been identified by scanning the revision log.
 */
typedef struct svn_repos_move_info_t {
  /* The repository relpath the node was moved from. */
  const char *moved_from_repos_relpath;

  /* The repository relpath the node was moved to. */
  const char *moved_to_repos_relpath;

  /* The revision in which the move happened. */
  svn_revnum_t revision;

  /* The copyfrom revision of the moved-to path. */
  svn_revnum_t copyfrom_rev;

  /* Pointers to previous or subsequent moves of the same node
   * within interesting history. */
  struct svn_repos_move_info_t *prev;
  struct svn_repos_move_info_t *next;

  /* @note Fields may be added to the end of this structure in future
   * versions.  Therefore, to preserve binary compatibility, users
   * should not directly allocate structures of this type but should use
   * svn_wc_create_repos_move_info(). */
} svn_repos_move_info_t;

/** Create a svn_wc_repos_move_info_t structure.
 * @see svn_wc_repos_move_info_t
 */
svn_repos_move_info_t *
svn_repos_move_info_create(const char *moved_from_repos_relpath,
                              const char *moved_to_repos_relpath,
                              svn_revnum_t revision,
                              svn_revnum_t copyfrom_rev,
                              svn_repos_move_info_t *prev,
                              svn_repos_move_info_t *next,
                              apr_pool_t *result_pool);

/* ...
 */
const char *
svn_client__format_move_chain_for_display(svn_repos_move_info_t *first_move,
                                          const char *indent,
                                          apr_pool_t *result_pool);
/* ...
 */
svn_error_t *
svn_client__get_repos_moves(apr_hash_t **moves,
                            const char *anchor_abspath,
                            svn_ra_session_t *ra_session,
                            svn_revnum_t start,
                            svn_revnum_t end,
                            svn_client_ctx_t *ctx,
                            apr_pool_t *result_pool,
                            apr_pool_t *scratch_pool);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVNMOVER_H */

