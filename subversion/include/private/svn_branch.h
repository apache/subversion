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

  /* The root branches, indexed by top-level branch id (0...N). */
  apr_array_header_t *root_branches;

  /* All branches, including root branches. */
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

/* Return the top-level branch numbered TOP_BRANCH_NUM in REV_ROOT.
 *
 * Return null if there is no such branch.
 */
svn_branch_state_t *
svn_branch_revision_root_get_root_branch(svn_branch_revision_root_t *rev_root,
                                         int top_branch_num);

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
  /* --- Identity of this object --- */

  /* The EID of its pathwise root element. */
  int root_eid;

  /* The revision to which this branch state belongs */
  svn_branch_revision_root_t *rev_root;

  /* The outer branch state that contains the subbranch
     root element of this branch. Null if this is a root branch. */
  struct svn_branch_state_t *outer_branch;

  /* The subbranch-root element in OUTER_BRANCH of the root of this branch.
   * The top branch id if this is a root branch. */
  int outer_eid;

  /* --- Contents of this object --- */

  /* EID -> svn_branch_el_rev_content_t mapping. */
  /* ### TODO: This should use an svn_branch_subtree_t instead of E_MAP and
   *     ROOT_EID. And the immediate subbranches would be directly in there,
   *     instead of (or as well as) being in a single big list in REV_ROOT.
   *     And a whole bunch of methods would be common to both. */
  apr_hash_t *e_map;

};

/* Create a new branch state object, with no elements (not even a root
 * element).
 */
svn_branch_state_t *
svn_branch_state_create(int root_eid,
                        svn_branch_revision_root_t *rev_root,
                        svn_branch_state_t *outer_branch,
                        int outer_eid,
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

/*
 */
void
svn_branch_id_split(const char **outer_bid,
                    int *outer_eid,
                    const char *bid,
                    apr_pool_t *result_pool);

/* Create a new branch at OUTER_BRANCH:OUTER_EID, with no elements
 * (not even a root element).
 *
 * Create and return a new branch object. Register its existence in REV_ROOT.
 *
 * If OUTER_BRANCH is NULL, create a top-level branch with a new top-level
 * branch number, ignoring OUTER_EID. Otherise, create a branch that claims
 * to be nested under OUTER_BRANCH:OUTER_EID, but do not require that
 * a subbranch root element exists there, nor create one.
 *
 * Set the root element to ROOT_EID.
 */
svn_branch_state_t *
svn_branch_add_new_branch(svn_branch_revision_root_t *rev_root,
                          svn_branch_state_t *outer_branch,
                          int outer_eid,
                          int root_eid,
                          apr_pool_t *scratch_pool);

/* Delete the branch BRANCH, and any subbranches recursively.
 *
 * Do not require that a subbranch root element exists in its outer branch,
 * nor delete it if it does exist.
 */
void
svn_branch_delete_branch_r(svn_branch_state_t *branch,
                           apr_pool_t *scratch_pool);

/* Return an array of pointers to the branches that are immediate
 * sub-branches of BRANCH.
 */
apr_array_header_t *
svn_branch_get_immediate_subbranches(const svn_branch_state_t *branch,
                                     apr_pool_t *result_pool,
                                     apr_pool_t *scratch_pool);

/* Return the subbranch rooted at BRANCH:EID, or NULL if that is
 * not a subbranch root.
 */
svn_branch_state_t *
svn_branch_get_subbranch_at_eid(svn_branch_state_t *branch,
                                int eid,
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

/* Return a new id object constructed with a deep copy of OLD_ID,
 * allocated in RESULT_POOL. */
svn_branch_rev_bid_eid_t *
svn_branch_rev_bid_eid_dup(const svn_branch_rev_bid_eid_t *old_id,
                           apr_pool_t *result_pool);

/* The content (parent, name and payload) of an element-revision.
 * In other words, an el-rev node in a (mixed-rev) directory-tree.
 */
typedef struct svn_branch_el_rev_content_t
{
  /* eid of the parent element, or -1 if this is the root element */
  int parent_eid;
  /* struct svn_branch_element_t *parent_element; */
  /* element name, or "" for root element; never null */
  const char *name;
  /* payload (kind, props, text, ...);
   * null if this is a subbranch root element */
  svn_element_payload_t *payload;

} svn_branch_el_rev_content_t;

/* Return a new content object constructed with deep copies of PARENT_EID,
 * NAME and PAYLOAD, allocated in RESULT_POOL.
 */
svn_branch_el_rev_content_t *
svn_branch_el_rev_content_create(svn_branch_eid_t parent_eid,
                                 const char *name,
                                 const svn_element_payload_t *payload,
                                 apr_pool_t *result_pool);

/* Return a deep copy of OLD, allocated in RESULT_POOL.
 */
svn_branch_el_rev_content_t *
svn_branch_el_rev_content_dup(const svn_branch_el_rev_content_t *old,
                              apr_pool_t *result_pool);

/* Return TRUE iff CONTENT_LEFT is the same as CONTENT_RIGHT. */
svn_boolean_t
svn_branch_el_rev_content_equal(const svn_branch_el_rev_content_t *content_left,
                                const svn_branch_el_rev_content_t *content_right,
                                apr_pool_t *scratch_pool);


/* Describe a subtree of elements.
 *
 * A subtree is described by the content of element ROOT_EID in E_MAP,
 * and its children (as determined by their parent links) and their names
 * and their content recursively. For the element ROOT_EID itself, only
 * its content is relevant; its parent and name are to be ignored.
 *
 * E_MAP may also contain entries that are not part of the subtree. Thus,
 * to select a sub-subtree, it is only necessary to change ROOT_EID.
 *
 * The EIDs used in here may be considered either as global EIDs (known to
 * the repo), or as local stand-alone EIDs (in their own local name-space),
 * according to the context.
 *
 * ### TODO: This should be used in the implementation of svn_branch_state_t.
 *     A whole bunch of methods would be common to both.
 */
typedef struct svn_branch_subtree_t
{
  /* EID -> svn_branch_el_rev_content_t mapping. */
  apr_hash_t *e_map;

  /* Subtree root EID. (ROOT_EID must be an existing key in E_MAP.) */
  int root_eid;

  /* Subbranches to be included: each subbranch-root element in E_MAP
     should be mapped here.

     A mapping of (int)EID -> (svn_branch_subtree_t *). */
  apr_hash_t *subbranches;
} svn_branch_subtree_t;

/* Create an empty subtree (no elements populated, not even ROOT_EID).
 *
 * The result contains a *shallow* copy of E_MAP, or a new empty mapping
 * if E_MAP is null.
 */
svn_branch_subtree_t *
svn_branch_subtree_create(apr_hash_t *e_map,
                          int root_eid,
                          apr_pool_t *result_pool);

/* Return the subbranch rooted at SUBTREE:EID, or NULL if that is
 * not a subbranch root. */
svn_branch_subtree_t *
svn_branch_subtree_get_subbranch_at_eid(svn_branch_subtree_t *subtree,
                                        int eid,
                                        apr_pool_t *result_pool);

/* Return the subtree of BRANCH rooted at EID.
 * Recursive: includes subbranches.
 *
 * The result is limited by the lifetime of BRANCH. It includes a shallow
 * copy of the element maps in BRANCH and its subbranches: the hash tables
 * are duplicated but the keys and values (element content data) are not.
 * It assumes that modifications on a svn_branch_state_t treat element
 * map keys and values as immutable -- which they do.
 */
svn_branch_subtree_t *
svn_branch_get_subtree(const svn_branch_state_t *branch,
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
 * If element EID is not present, return null. Otherwise, the returned
 * element's payload may be null meaning it is a subbranch-root.
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

/* Set or change the EID:element mapping for EID in BRANCH to reflect a
 * subbranch root element. This element has no payload in this branch; the
 * corresponding element of the subbranch will define its payload.
 *
 * Duplicate NEW_NAME into the branch mapping's pool.
 */
void
svn_branch_update_subbranch_root_element(svn_branch_state_t *branch,
                                         int eid,
                                         svn_branch_eid_t new_parent_eid,
                                         const char *new_name);

/* Purge orphaned elements and subbranches.
 */
void
svn_branch_purge_r(svn_branch_state_t *branch,
                   apr_pool_t *scratch_pool);

/* Instantiate a subtree.
 *
 * In TO_BRANCH, instantiate (or alter, if existing) each element of
 * FROM_SUBTREE, with the given tree structure and payload. Set the subtree
 * root element's parent to NEW_PARENT_EID and name to NEW_NAME.
 *
 * Also branch the subbranches in FROM_SUBTREE, creating corresponding new
 * subbranches in TO_BRANCH, recursively.
 *
 * If FROM_SUBTREE.root_eid is the same as TO_BRANCH.root_eid, then
 * (NEW_PARENT_EID, NEW_NAME) must be (-1, ""); otherwise, NEW_PARENT_EID
 * must be an existing element (it may be the root element) of TO_BRANCH and
 * NEW_NAME must not be not "".
 */
svn_error_t *
svn_branch_instantiate_subtree(svn_branch_state_t *to_branch,
                               svn_branch_eid_t new_parent_eid,
                               const char *new_name,
                               svn_branch_subtree_t from_subtree,
                               apr_pool_t *scratch_pool);

/* Create a new branch of a given subtree.
 *
 * Create a new branch object. Register its existence in REV_ROOT.
 * Instantiate the subtree FROM_SUBTREE in this new branch. In the new
 * branch, create new subbranches corresponding to any subbranches
 * specified in FROM_SUBTREE, recursively.
 *
 * If TO_OUTER_BRANCH is NULL, create a top-level branch with a new top-level
 * branch number, ignoring TO_OUTER_EID. Otherwise, create a branch that claims
 * to be nested under TO_OUTER_BRANCH:TO_OUTER_EID, but do not require that
 * a subbranch root element exists there, nor create one.
 *
 * Set *NEW_BRANCH_P to the new branch (the one at TO_OUTER_BRANCH:TO_OUTER_EID).
 */
svn_error_t *
svn_branch_branch_subtree(svn_branch_state_t **new_branch_p,
                          svn_branch_subtree_t from_subtree,
                          svn_branch_revision_root_t *rev_root,
                          svn_branch_state_t *to_outer_branch,
                          svn_branch_eid_t to_outer_eid,
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
                           svn_branch_subtree_t new_subtree,
                           apr_pool_t *scratch_pool);

/* Return the root repos-relpath of BRANCH.
 *
 * ### TODO: Clarify sequencing requirements.
 */
const char *
svn_branch_get_root_rrpath(const svn_branch_state_t *branch,
                           apr_pool_t *result_pool);

/* Return the subtree-relative path of element EID in SUBTREE.
 *
 * If the element EID does not currently exist in SUBTREE, return NULL.
 *
 * ### TODO: Clarify sequencing requirements.
 */
const char *
svn_branch_subtree_get_path_by_eid(const svn_branch_subtree_t *subtree,
                                   int eid,
                                   apr_pool_t *result_pool);
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

/* Return the repos-relpath of element EID in BRANCH.
 *
 * If the element EID does not currently exist in BRANCH, return NULL.
 *
 * ### TODO: Clarify sequencing requirements.
 */
const char *
svn_branch_get_rrpath_by_eid(const svn_branch_state_t *branch,
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

/* Return the EID for the repos-relpath RRPATH in BRANCH.
 *
 * If no element of BRANCH is at this path, return -1.
 *
 * ### TODO: Clarify sequencing requirements.
 */
int
svn_branch_get_eid_by_rrpath(svn_branch_state_t *branch,
                             const char *rrpath,
                             apr_pool_t *scratch_pool);

/* Find the (deepest) branch of which the path RELPATH is either the root
 * path or a normal, non-sub-branch path. An element need not exist at
 * RELPATH.
 *
 * Set *BRANCH_P to the deepest branch within ROOT_BRANCH (recursively,
 * including itself) that contains the path RELPATH.
 *
 * If EID_P is not null then set *EID_P to the element id of RELPATH in
 * *BRANCH_P, or to -1 if no element exists at RELPATH in that branch.
 *
 * If RELPATH is not within any branch in ROOT_BRANCH, set *BRANCH_P to
 * NULL and (if EID_P is not null) *EID_P to -1.
 *
 * ### TODO: Clarify sequencing requirements.
 */
void
svn_branch_find_nested_branch_element_by_relpath(
                                svn_branch_state_t **branch_p,
                                int *eid_p,
                                svn_branch_state_t *root_branch,
                                const char *relpath,
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
