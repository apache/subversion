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
 * @file svn_branch.h
 * @brief Operating on a branched version history
 *
 * @since New in 1.10.
 */

/* Transactions
 *
 * A 'txn' contains a set of changes to the branches/elements.
 *
 * To make changes you say, for example, "for element 5: I want the parent
 * element to be 3 now, and its name to be 'bar', and its content to be
 * {props=... text=...}". That sets up a move and/or rename and/or
 * content-change (or possibly a no-op for all three aspects) for element 5.
 *
 * Before or after (or at the same time, if we make a parallelizable
 * implementation) we can make edits to the other elements, including
 * element 3.
 *
 * So at the time of the edit method 'change e5: let its parent be e3'
 * we might or might not have even created e3, if that happens to be an
 * element that we wish to create rather than one that already existed.
 *
 * We allow this non-ordering because we want the changes to different
 * elements to be totally independent.
 *
 * So at any given 'moment' in time during specifying the changes to a
 * txn, the txn state is not necessarily one that maps directly to a
 * flat tree (single-rooted, no cycles, no clashes of paths, etc.).
 *
 * Once we've finished specifying the edits, then the txn state will be
 * converted to a flat tree, and that's the final result. But we can't
 * query an arbitrary txn (potentially in the middle of making changes
 * to it) by path, because the paths are not fully defined yet.
 *
 * So there are three kinds of operations:
 *
 * - query involving paths
 *   => requires a flat tree state to query, not an in-progress txn
 *
 * - query, not involving paths
 *   => accepts a txn-in-progress or a flat tree
 *
 * - modify (not involving paths)
 *   => requires a txn
 *
 * Currently, a txn is represented by 'svn_branch_revision_root_t', with
 * 'svn_branch_state_t' for the individual branches in it. A flat tree is
 * represented by 'svn_branch_subtree_t'. But there is currently not a
 * clean separation; there is some overlap and some warts such as the
 * 'svn_editor3_sequence_point' method.
 */


#ifndef SVN_BRANCH_H
#define SVN_BRANCH_H

#include <apr_pools.h>

#include "svn_types.h"
#include "svn_error.h"
#include "svn_io.h"    /* for svn_stream_t  */
#include "svn_delta.h"

#include "private/svn_element.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/* ### */
#define SVN_ERR_BRANCHING 123456

/** Element Identifier (EID).
 *
 * An element may appear in any or all branches, and its EID is the same in
 * each branch in which the element appears.
 * 
 * By definition, an element keeps the same EID for its whole lifetime, even
 * if deleted from all branches and later 'resurrected'.
 *
 * In principle, an EID is an arbitrary token and has no intrinsic
 * relationships (except equality) to other EIDs. The current implementation
 * uses integers and allocates them sequentially from a central counter, but
 * the implementation may be changed.
 *
 * ### In most places the code currently says 'int', verbatim.
 */
typedef int svn_branch_eid_t;

typedef struct svn_branch_el_rev_id_t svn_branch_el_rev_id_t;

typedef struct svn_branch_rev_bid_eid_t svn_branch_rev_bid_eid_t;

typedef struct svn_branch_rev_bid_t svn_branch_rev_bid_t;

typedef struct svn_branch_state_t svn_branch_state_t;

/* Per-repository branching info.
 */
typedef struct svn_branch_repos_t svn_branch_repos_t;

/* A container for all the branching metadata for a specific revision (or
 * an uncommitted transaction).
 */
typedef struct svn_branch_revision_root_t
{
  /* The repository in which this revision exists. */
  svn_branch_repos_t *repos;

  /* If committed, the revision number; else SVN_INVALID_REVNUM. */
  svn_revnum_t rev;

  /* If committed, the previous revision number, else the revision number
     on which this transaction is based. */
  svn_revnum_t base_rev;

  /* The range of element ids assigned. */
  /* EIDs local to the txn are negative, assigned by decrementing FIRST_EID
   * (skipping -1). */
  int first_eid, next_eid;

  /* All branches. */
  apr_array_header_t *branches;

} svn_branch_revision_root_t;

/* Create a new branching revision-info object.
 *
 * It will have no branch-roots.
 */
svn_branch_revision_root_t *
svn_branch_revision_root_create(svn_branch_repos_t *repos,
                                svn_revnum_t rev,
                                svn_revnum_t base_rev,
                                apr_pool_t *result_pool);

/* Return all the branches in REV_ROOT.
 *
 * Return an empty array if there are none.
 */
const apr_array_header_t *
svn_branch_revision_root_get_branches(svn_branch_revision_root_t *rev_root,
                                      apr_pool_t *result_pool);

/* Return the branch whose id is BRANCH_ID in REV_ROOT.
 *
 * Return NULL if not found.
 *
 * Note: a branch id is, in behavioural terms, an arbitrary token. In the
 * current implementation it is constructed from the hierarchy of subbranch
 * root EIDs leading to the branch, but that may be changed in future.
 *
 * See also: svn_branch_get_id().
 */
svn_branch_state_t *
svn_branch_revision_root_get_branch_by_id(const svn_branch_revision_root_t *rev_root,
                                          const char *branch_id,
                                          apr_pool_t *scratch_pool);

/* Assign a new txn-scope element id in REV_ROOT.
 */
int
svn_branch_txn_new_eid(svn_branch_revision_root_t *rev_root);

/* Change txn-local EIDs (negative integers) in TXN to revision EIDs, by
 * assigning a new revision-EID (positive integer) for each one.
 *
 * Rewrite TXN->first_eid and TXN->next_eid accordingly.
 */
svn_error_t *
svn_branch_txn_finalize_eids(svn_branch_revision_root_t *txn,
                             apr_pool_t *scratch_pool);

/* Often, branches have the same root element. For example,
 * branching /trunk to /branches/br1 results in:
 *
 *      branch 1: (root-EID=100)
 *          EID 100 => /trunk
 *          ...
 *      branch 2: (root-EID=100)
 *          EID 100 => /branches/br1
 *          ...
 *
 * However, the root element of one branch may correspond to a non-root
 * element of another branch.
 *
 * Continuing the same example, branching from the trunk subtree
 * /trunk/D (which is not itself a branch root) results in:
 *
 *      branch 3: (root-EID=104)
 *          EID 100 => (nil)
 *          ...
 *          EID 104 => /branches/branch-of-trunk-subtree-D
 *          ...
 */

/* A branch state.
 *
 * A branch state object describes one version of one branch.
 */
struct svn_branch_state_t
{
  /* The branch identifier (starting with 'B') */
  const char *bid;

  /* The previous location in the lifeline of this branch. */
  /* (REV = -1 means "in this txn") */
  svn_branch_rev_bid_t *predecessor;

  /* The revision to which this branch state belongs */
  svn_branch_revision_root_t *rev_root;

  /* EID -> svn_branch_el_rev_content_t mapping. */
  svn_element_tree_t *element_tree;

};

/* Create a new branch state object, with no elements (not even a root
 * element).
 */
svn_branch_state_t *
svn_branch_state_create(const char *bid,
                        svn_branch_rev_bid_t *predecessor,
                        int root_eid,
                        svn_branch_revision_root_t *rev_root,
                        apr_pool_t *result_pool);

/* Get the full id of branch BRANCH.
 *
 * Branch id format:
 *      B<top-level-branch-num>[.<1st-level-eid>[.<2nd-level-eid>[...]]]
 *
 * Note: a branch id is, in behavioural terms, an arbitrary token. In the
 * current implementation it is constructed from the hierarchy of subbranch
 * root EIDs leading to the branch, but that may be changed in future.
 *
 * See also: svn_branch_revision_root_get_branch_by_id().
 */
const char *
svn_branch_get_id(svn_branch_state_t *branch,
                  apr_pool_t *result_pool);

/* Return the element id of the root element of BRANCH.
 */
int
svn_branch_root_eid(const svn_branch_state_t *branch);

/* Return the id of the branch nested in OUTER_BID at element OUTER_EID.
 *
 * For a top-level branch, OUTER_BID is null and OUTER_EID is the
 * top-level branch number.
 *
 * (Such branches need not exist. This works purely with ids, making use
 * of the fact that nested branch ids are predictable based on the nesting
 * element id.)
 */
const char *
svn_branch_id_nest(const char *outer_bid,
                   int outer_eid,
                   apr_pool_t *result_pool);

/* Given a nested branch id BID, set *OUTER_BID to the outer branch's id
 * and *OUTER_EID to the nesting element in the outer branch.
 *
 * For a top-level branch, set *OUTER_BID to NULL and *OUTER_EID to the
 * top-level branch number.
 *
 * (Such branches need not exist. This works purely with ids, making use
 * of the fact that nested branch ids are predictable based on the nesting
 * element id.)
 */
void
svn_branch_id_unnest(const char **outer_bid,
                     int *outer_eid,
                     const char *bid,
                     apr_pool_t *result_pool);

/* Create a new branch with branch id BID, with no elements
 * (not even a root element).
 *
 * Create and return a new branch object. Register its existence in REV_ROOT.
 *
 * Set the root element to ROOT_EID.
 */
svn_branch_state_t *
svn_branch_add_new_branch(const char *bid,
                          svn_branch_revision_root_t *rev_root,
                          svn_branch_rev_bid_t *predecessor,
                          int root_eid,
                          apr_pool_t *scratch_pool);

/* Remove branch BRANCH from the list of branches in REV_ROOT.
 */
void
svn_branch_revision_root_delete_branch(
                                svn_branch_revision_root_t *rev_root,
                                svn_branch_state_t *branch,
                                apr_pool_t *scratch_pool);

/* element */
/*
typedef struct svn_branch_element_t
{
  int eid;
  svn_node_kind_t node_kind;
} svn_branch_element_t;
*/

/* Branch-Element-Revision */
struct svn_branch_el_rev_id_t
{
  /* The branch state that applies to REV. */
  svn_branch_state_t *branch;
  /* Element. */
  int eid;
  /* Revision. SVN_INVALID_REVNUM means 'in this transaction', not 'head'.
     ### Do we need this if BRANCH refers to a particular branch-revision? */
  svn_revnum_t rev;

};

/* Revision-branch-element id. */
struct svn_branch_rev_bid_eid_t
{
  /* Revision. SVN_INVALID_REVNUM means 'in this transaction', not 'head'. */
  svn_revnum_t rev;
  /* The branch id in revision REV. */
  const char *bid;
  /* Element id. */
  int eid;

};

/* Revision-branch id. */
struct svn_branch_rev_bid_t
{
  /* Revision. SVN_INVALID_REVNUM means 'in this transaction', not 'head'. */
  svn_revnum_t rev;
  /* The branch id in revision REV. */
  const char *bid;

};

/* Return a new el_rev_id object constructed with *shallow* copies of BRANCH,
 * EID and REV, allocated in RESULT_POOL.
 */
svn_branch_el_rev_id_t *
svn_branch_el_rev_id_create(svn_branch_state_t *branch,
                            int eid,
                            svn_revnum_t rev,
                            apr_pool_t *result_pool);

/* Return a new id object constructed with deep copies of REV, BRANCH_ID
 * and EID, allocated in RESULT_POOL.
 */
svn_branch_rev_bid_eid_t *
svn_branch_rev_bid_eid_create(svn_revnum_t rev,
                              const char *branch_id,
                              int eid,
                              apr_pool_t *result_pool);
svn_branch_rev_bid_t *
svn_branch_rev_bid_create(svn_revnum_t rev,
                          const char *branch_id,
                          apr_pool_t *result_pool);

/* Return a new id object constructed with a deep copy of OLD_ID,
 * allocated in RESULT_POOL. */
svn_branch_rev_bid_eid_t *
svn_branch_rev_bid_eid_dup(const svn_branch_rev_bid_eid_t *old_id,
                           apr_pool_t *result_pool);
svn_branch_rev_bid_t *
svn_branch_rev_bid_dup(const svn_branch_rev_bid_t *old_id,
                       apr_pool_t *result_pool);


/* Return the element-tree of BRANCH.
 */
const svn_element_tree_t *
svn_branch_get_element_tree(svn_branch_state_t *branch);

/* Return the element-tree within BRANCH rooted at EID.
 *
 * The result is limited by the lifetime of BRANCH. It includes a shallow
 * copy of the element maps in BRANCH: the hash table is
 * duplicated but the keys and values (element content data) are not.
 * It assumes that modifications on a svn_branch_state_t treat element
 * map keys and values as immutable -- which they do.
 */
svn_element_tree_t *
svn_branch_get_element_tree_at_eid(svn_branch_state_t *branch,
                                   int eid,
                                   apr_pool_t *result_pool);

/* Declare that the following function requires/implies that in BRANCH's
 * mapping, for each existing element, the parent also exists.
 *
 * ### Find a better word? flattened, canonical, finalized, ...
 */
#define SVN_BRANCH_SEQUENCE_POINT(branch)

/* Return the mapping of elements in branch BRANCH.
 *
 * The mapping is from pointer-to-eid to
 * pointer-to-svn_branch_el_rev_content_t.
 */
apr_hash_t *
svn_branch_get_elements(svn_branch_state_t *branch);

/* In BRANCH, get element EID (parent, name, payload).
 *
 * If element EID is not present, return null.
 */
svn_branch_el_rev_content_t *
svn_branch_get_element(const svn_branch_state_t *branch,
                       int eid);

/* In BRANCH, delete element EID.
 */
void
svn_branch_delete_element(svn_branch_state_t *branch,
                          int eid);

/* Set or change the EID:element mapping for EID in BRANCH.
 *
 * Duplicate NEW_NAME and NEW_PAYLOAD into the branch mapping's pool.
 */
void
svn_branch_update_element(svn_branch_state_t *branch,
                          int eid,
                          svn_branch_eid_t new_parent_eid,
                          const char *new_name,
                          const svn_element_payload_t *new_payload);

/* Purge orphaned elements in BRANCH.
 */
void
svn_branch_purge(svn_branch_state_t *branch,
                 apr_pool_t *scratch_pool);

/* Instantiate elements in a branch.
 *
 * In TO_BRANCH, instantiate (or alter, if existing) each element of
 * ELEMENTS, each with its given tree structure (parent, name) and payload.
 */
svn_error_t *
svn_branch_instantiate_elements(svn_branch_state_t *to_branch,
                                const svn_element_tree_t *elements,
                                apr_pool_t *scratch_pool);

/* Create a copy of NEW_SUBTREE in TO_BRANCH.
 *
 * For each non-root element in NEW_SUBTREE, create a new element with
 * a new EID, no matter what EID is used to represent it in NEW_SUBTREE.
 *
 * For the new subtree root element, if TO_EID is -1, generate a new EID,
 * otherwise alter (if it exists) or instantiate the element TO_EID.
 *
 * Set the new subtree root element's parent to NEW_PARENT_EID and name to
 * NEW_NAME.
 */
svn_error_t *
svn_branch_map_add_subtree(svn_branch_state_t *to_branch,
                           int to_eid,
                           svn_branch_eid_t new_parent_eid,
                           const char *new_name,
                           svn_element_tree_t *new_subtree,
                           apr_pool_t *scratch_pool);

/* Return the branch-relative path of element EID in BRANCH.
 *
 * If the element EID does not currently exist in BRANCH, return NULL.
 *
 * ### TODO: Clarify sequencing requirements.
 */
const char *
svn_branch_get_path_by_eid(const svn_branch_state_t *branch,
                           int eid,
                           apr_pool_t *result_pool);

/* Return the EID for the branch-relative path PATH in BRANCH.
 *
 * If no element of BRANCH is at this path, return -1.
 *
 * ### TODO: Clarify sequencing requirements.
 */
int
svn_branch_get_eid_by_path(const svn_branch_state_t *branch,
                           const char *path,
                           apr_pool_t *scratch_pool);

/* Get the default branching metadata for r0 of a new repository.
 */
svn_string_t *
svn_branch_get_default_r0_metadata(apr_pool_t *result_pool);

/* Create a new revision-root object *REV_ROOT_P, initialized with info
 * parsed from STREAM, allocated in RESULT_POOL.
 */
svn_error_t *
svn_branch_revision_root_parse(svn_branch_revision_root_t **rev_root_p,
                               svn_branch_repos_t *repos,
                               svn_stream_t *stream,
                               apr_pool_t *result_pool,
                               apr_pool_t *scratch_pool);

/* Write to STREAM a parseable representation of REV_ROOT.
 */
svn_error_t *
svn_branch_revision_root_serialize(svn_stream_t *stream,
                                   svn_branch_revision_root_t *rev_root,
                                   apr_pool_t *scratch_pool);

/* Write to STREAM a parseable representation of BRANCH.
 */
svn_error_t *
svn_branch_state_serialize(svn_stream_t *stream,
                           svn_branch_state_t *branch,
                           apr_pool_t *scratch_pool);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_BRANCH_H */
