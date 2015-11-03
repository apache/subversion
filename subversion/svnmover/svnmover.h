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
#include "svn_ra.h"

#include "private/svn_branch.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/* Print a verbose notification: in 'quiet' mode, don't print it. */
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
  const char *branch_id;
  svn_branch_state_t *branch;
} svnmover_wc_version_t;

typedef struct svnmover_wc_t
{
  apr_pool_t *pool;
  const char *repos_root_url;
  /*const char *anchor_repos_relpath;*/
  svn_revnum_t head_revision;

  svn_ra_session_t *ra_session;
  svn_branch_txn_t *edit_txn;

  /* Base and working versions. */
  svnmover_wc_version_t *base, *working;

  /* Textual list of commands the commands that were executed, suitable
     for putting in a log message */
  const char *list_of_commands;

  svn_client_ctx_t *ctx;

} svnmover_wc_t;

/*  */
typedef struct conflict_storage_t
{
  /* Single-element conflicts */
  /* (eid -> element_merge3_conflict_t) */
  apr_hash_t *single_element_conflicts;

  /* Name-clash conflicts */
  /* ("%{parent_eid}d/%{name}s" -> name_clash_conflict_t) */
  apr_hash_t *name_clash_conflicts;

  /* Cycle conflicts */
  /* (eid -> cycle_conflict_t) */
  apr_hash_t *cycle_conflicts;

  /* Orphan conflicts */
  /* (eid -> orphan_conflict_t) */
  apr_hash_t *orphan_conflicts;
} conflict_storage_t;

/* Merge SRC into TGT, using the common ancestor YCA.
 *
 * Merge the two sets of changes: YCA -> SRC and YCA -> TGT, applying
 * the result to the transaction at TGT.
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
svnmover_branch_merge(svn_branch_txn_t *edit_txn,
                      conflict_storage_t **conflict_storage_p,
                      svn_branch_el_rev_id_t *src,
                      svn_branch_el_rev_id_t *tgt,
                      svn_branch_el_rev_id_t *yca,
                      apr_pool_t *result_pool,
                      apr_pool_t *scratch_pool);

/*  */
svn_error_t *
svnmover_display_conflicts(conflict_storage_t *conflict_storage,
                           apr_pool_t *scratch_pool);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVNMOVER_H */

