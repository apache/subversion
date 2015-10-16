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
 * @file branch_private.h
 * @brief Nested branches and subbranch-root elements
 *
 * @since New in 1.10.
 */

#ifndef BRANCH_PRIVATE_H
#define	BRANCH_PRIVATE_H

#ifdef	__cplusplus
extern "C" {
#endif


/* Common aspects od a txn/branch 'editor' class (derived from Ev2) */
typedef struct svn_vtable_priv_t
{
  /* Standard cancellation function. Called before each callback.  */
  svn_cancel_func_t cancel_func;
  void *cancel_baton;

  /* This pool is used as the scratch_pool for all callbacks.  */
  apr_pool_t *scratch_pool;

#ifdef ENABLE_ORDERING_CHECK
  svn_boolean_t within_callback;
  svn_boolean_t finished;
  apr_pool_t *state_pool;
#endif

} svn_vtable_priv_t;

/* The methods of svn_branch_txn_t.
 * See the corresponding public API functions for details.
 */

typedef svn_error_t *(*branch_txn_v_new_eid_t)(
  svn_branch_txn_t *txn,
  svn_branch_eid_t *eid_p,
  apr_pool_t *scratch_pool);

typedef svn_error_t *(*branch_txn_v_open_branch_t)(
  svn_branch_txn_t *txn,
  const char **new_branch_id_p,
  svn_branch_rev_bid_t *predecessor,
  const char *outer_branch_id,
  int outer_eid,
  int root_eid,
  apr_pool_t *result_pool,
  apr_pool_t *scratch_pool);

typedef svn_error_t *(*branch_txn_v_branch_t)(
  svn_branch_txn_t *txn,
  const char **new_branch_id_p,
  svn_branch_rev_bid_eid_t *from,
  const char *outer_branch_id,
  int outer_eid,
  apr_pool_t *result_pool,
  apr_pool_t *scratch_pool);

typedef svn_error_t *(*branch_txn_v_sequence_point_t)(
  svn_branch_txn_t *txn,
  apr_pool_t *scratch_pool);

struct svn_branch_txn_priv_t
{
  svn_vtable_priv_t vpriv;

  /* Methods. */
  branch_txn_v_new_eid_t new_eid;
  branch_txn_v_open_branch_t open_branch;
  branch_txn_v_branch_t branch;
  branch_txn_v_sequence_point_t sequence_point;

  /* All branches. */
  apr_array_header_t *branches;

};

/* The methods of svn_branch_state_t.
 * See the corresponding public API functions for details.
 */

typedef svn_error_t *(*branch_state_v_alter_one_t)(
  svn_branch_state_t *branch,
  svn_branch_eid_t eid,
  svn_branch_eid_t new_parent_eid,
  const char *new_name,
  const svn_element_payload_t *new_payload,
  apr_pool_t *scratch_pool);

typedef svn_error_t *(*branch_state_v_copy_one_t)(
  svn_branch_state_t *branch,
  const svn_branch_rev_bid_eid_t *src_el_rev,
  svn_branch_eid_t local_eid,
  svn_branch_eid_t new_parent_eid,
  const char *new_name,
  const svn_element_payload_t *new_payload,
  apr_pool_t *scratch_pool);

typedef svn_error_t *(*branch_state_v_copy_tree_t)(
  svn_branch_state_t *branch,
  const svn_branch_rev_bid_eid_t *src_el_rev,
  svn_branch_eid_t new_parent_eid,
  const char *new_name,
  apr_pool_t *scratch_pool);

typedef svn_error_t *(*branch_state_v_delete_one_t)(
  svn_branch_state_t *branch,
  svn_branch_eid_t eid,
  apr_pool_t *scratch_pool);

typedef svn_error_t *(*branch_state_v_payload_resolve_t)(
  svn_branch_state_t *branch,
  svn_element_content_t *element,
  apr_pool_t *scratch_pool);

typedef svn_error_t *(*branch_state_v_purge_t)(
  svn_branch_state_t *branch,
  apr_pool_t *scratch_pool);

struct svn_branch_state_priv_t
{
  svn_vtable_priv_t vpriv;

  branch_state_v_alter_one_t alter_one;
  branch_state_v_copy_one_t copy_one;
  branch_state_v_copy_tree_t copy_tree;
  branch_state_v_delete_one_t delete_one;
  branch_state_v_payload_resolve_t payload_resolve;
  branch_state_v_purge_t purge;

  /* EID -> svn_branch_el_rev_content_t mapping. */
  svn_element_tree_t *element_tree;

};


#ifdef	__cplusplus
}
#endif

#endif	/* BRANCH_PRIVATE_H */

