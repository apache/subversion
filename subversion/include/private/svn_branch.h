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
 * ### In most places the code currently says 'int', verbatim.
 */
typedef int svn_branch_eid_t;

typedef struct svn_branch_el_rev_id_t svn_branch_el_rev_id_t;

typedef struct svn_branch_state_t svn_branch_state_t;

/* Per-repository branching info.
 */
typedef struct svn_branch_repos_t
{
  /* Array of (svn_branch_revision_info_t *), indexed by revision number. */
  apr_array_header_t *rev_roots;

  /* The pool in which this object lives. */
  apr_pool_t *pool;
} svn_branch_repos_t;

/* Create a new branching metadata object */
svn_branch_repos_t *
svn_branch_repos_create(apr_pool_t *result_pool);

/* Set *EL_REV_P to the el-rev-id of the element at repos-relpath RRPATH
 * in revision REVNUM in REPOS.
 *
 * If there is no element there, set *EL_REV_P to point to an id in which
 * the BRANCH field is the nearest enclosing branch of RRPATH and the EID
 * field is -1.
 *
 * Allocate *EL_REV_P (but not the branch object that it refers to) in
 * RESULT_POOL.
 *
 * ### TODO: Clarify sequencing requirements.
 */
svn_error_t *
svn_branch_repos_find_el_rev_by_path_rev(svn_branch_el_rev_id_t **el_rev_p,
                                const char *rrpath,
                                svn_revnum_t revnum,
                                const svn_branch_repos_t *repos,
                                apr_pool_t *result_pool,
                                apr_pool_t *scratch_pool);

/* A container for all the branching metadata for a specific revision (or
 * an uncommitted transaction).
 */
typedef struct svn_branch_revision_root_t
{
  /* The repository in which this revision exists. */
  svn_branch_repos_t *repos;

  /* If committed, the revision number; else SVN_INVALID_REVNUM. */
  svn_revnum_t rev;

  /* The range of element ids assigned. */
  int first_eid, next_eid;

  /* The root branch. */
  struct svn_branch_state_t *root_branch;

  /* All branches, including ROOT_BRANCH. */
  apr_array_header_t *branches;

} svn_branch_revision_root_t;

/* Create a new branching revision-info object */
svn_branch_revision_root_t *
svn_branch_revision_root_create(svn_branch_repos_t *repos,
                                svn_revnum_t rev,
                                struct svn_branch_state_t *root_branch,
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
 */
svn_branch_state_t *
svn_branch_revision_root_get_branch_by_id(const svn_branch_revision_root_t *rev_root,
                                          const char *branch_id,
                                          apr_pool_t *scratch_pool);

/* Assign a new element id in REV_ROOT.
 */
int
svn_branch_allocate_new_eid(svn_branch_revision_root_t *rev_root);

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
     root element of this branch. Null if this is the root branch. */
  struct svn_branch_state_t *outer_branch;

  /* The subbranch-root element in OUTER_BRANCH of the root of this branch.
   * -1 if this is the root branch. */
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

/* Create a new branch at OUTER_BRANCH:OUTER_EID, with no elements
 * (not even a root element).
 *
 * Do not require that a subbranch root element exists in OUTER_BRANCH,
 * nor create one.
 */
svn_branch_state_t *
svn_branch_add_new_branch(svn_branch_state_t *outer_branch,
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
 * sub-branches of BRANCH at or below EID.
 */
apr_array_header_t *
svn_branch_get_subbranches(const svn_branch_state_t *branch,
                           int eid,
                           apr_pool_t *result_pool,
                           apr_pool_t *scratch_pool);

/* Return an array of pointers to the branches that are immediate
 * sub-branches of BRANCH.
 */
apr_array_header_t *
svn_branch_get_all_sub_branches(const svn_branch_state_t *branch,
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

/* Return a new el_rev_id object constructed with *shallow* copies of BRANCH,
 * EID and REV, allocated in RESULT_POOL.
 */
svn_branch_el_rev_id_t *
svn_branch_el_rev_id_create(svn_branch_state_t *branch,
                            int eid,
                            svn_revnum_t rev,
                            apr_pool_t *result_pool);

/* The content (parent, name and node-content) of an element-revision.
 * In other words, an el-rev node in a (mixed-rev) directory-tree.
 */
typedef struct svn_branch_el_rev_content_t
{
  /* eid of the parent element, or -1 if this is the root element */
  int parent_eid;
  /* struct svn_branch_element_t *parent_element; */
  /* element name, or "" for root element; never null */
  const char *name;
  /* content (kind, props, text, ...);
   * null if this is a subbranch root element */
  svn_element_content_t *content;

} svn_branch_el_rev_content_t;

/* Return a new content object constructed with deep copies of PARENT_EID,
 * NAME and NODE_CONTENT, allocated in RESULT_POOL.
 */
svn_branch_el_rev_content_t *
svn_branch_el_rev_content_create(svn_branch_eid_t parent_eid,
                                 const char *name,
                                 const svn_element_content_t *node_content,
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

  /* Subtree root EID. (EID must be an existing key in E_MAP.) */
  int root_eid;

  /* Subbranches to be included: each subbranch-root element in that is
     hierarchically below ROOT_EID in E_MAP should have its subbranch
     mapped here.

     A mapping of (int)EID -> (svn_branch_subtree_t *). */
  apr_hash_t *subbranches;
} svn_branch_subtree_t;

/* Create an empty subtree (no elements populated, not even ROOT_EID).
 */
svn_branch_subtree_t *
svn_branch_subtree_create(apr_hash_t *e_map,
                          int root_eid,
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

/* In BRANCH, get element EID (parent, name, content).
 *
 * If element EID is not present, return null. Otherwise, the returned
 * element's node-content may be null meaning it is a subbranch-root.
 */
svn_branch_el_rev_content_t *
svn_branch_map_get(const svn_branch_state_t *branch,
                   int eid);

/* In BRANCH, delete element EID.
 */
void
svn_branch_map_delete(svn_branch_state_t *branch,
                      int eid);

/* Set or change the EID:element mapping for EID in BRANCH.
 *
 * Duplicate NEW_NAME and NEW_CONTENT into the branch mapping's pool.
 */
void
svn_branch_map_update(svn_branch_state_t *branch,
                      int eid,
                      svn_branch_eid_t new_parent_eid,
                      const char *new_name,
                      const svn_element_content_t *new_content);

/* Set or change the EID:element mapping for EID in BRANCH to reflect a
 * subbranch root element. This element has no content in this branch; the
 * corresponding element of the subbranch will define its content.
 *
 * Duplicate NEW_NAME into the branch mapping's pool.
 */
void
svn_branch_map_update_as_subbranch_root(svn_branch_state_t *branch,
                                        int eid,
                                        svn_branch_eid_t new_parent_eid,
                                        const char *new_name);

/* Remove from BRANCH's mapping any elements that do not have a complete
 * line of parents to the branch root. In other words, remove elements
 * that have been implicitly deleted.
 *
 * This does not remove subbranches.
 *
 * ### TODO: Clarify sequencing requirements. (This leaves BRANCH's mapping
 * in a [sequence-point/flattened/...?] state.)
 */
void
svn_branch_map_purge_orphans(svn_branch_state_t *branch,
                             apr_pool_t *scratch_pool);

/* Purge orphaned elements and subbranches.
 */
void
svn_branch_purge_r(svn_branch_state_t *branch,
                   apr_pool_t *scratch_pool);

/* Instantiate a subtree.
 *
 * In TO_BRANCH, instantiate (or alter, if existing) each element of
 * FROM_SUBTREE, keeping their tree structure and content. Set the subtree
 * root element's parent to NEW_PARENT_EID and name to NEW_NAME.
 *
 * NEW_PARENT_EID MUST be an existing element in TO_BRANCH. It may be the
 * root element of TO_BRANCH.
 */
svn_error_t *
svn_branch_instantiate_subtree(svn_branch_state_t *to_branch,
                               svn_branch_eid_t new_parent_eid,
                               const char *new_name,
                               svn_branch_subtree_t from_subtree,
                               apr_pool_t *scratch_pool);

/* Instantiate a new branch of the subtree FROM_SUBTREE, at the
 * existing branch-root element TO_OUTER_BRANCH:TO_OUTER_EID.
 * Also branch, recursively, the subbranches in FROM_SUBTREE.
 *
 * TO_OUTER_BRANCH may be the same as or different from FROM_BRANCH.
 */
svn_error_t *
svn_branch_branch_subtree_r2(svn_branch_state_t **new_branch_p,
                             svn_branch_subtree_t from_subtree,
                             svn_branch_state_t *to_outer_branch,
                             svn_branch_eid_t to_outer_eid,
                             apr_pool_t *scratch_pool);

/* Create a copy of NEW_SUBTREE in TO_BRANCH, generating a new element
 * for each non-root element in NEW_SUBTREE.
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

/* Copy a subtree.
 *
 * Adjust TO_BRANCH and its subbranches (recursively), to reflect a copy
 * of a subtree from FROM_EL_REV to TO_PARENT_EID:TO_NAME.
 *
 * FROM_EL_REV must be an existing element. (It may be a branch root.)
 *
 * ### TODO:
 * If FROM_EL_REV is the root of a subbranch and/or contains nested
 * subbranches, also copy them ...
 * ### What shall we do with a subbranch? Make plain copies of its raw
 *     elements; make a subbranch by branching the source subbranch?
 *
 * TO_PARENT_EID must be a directory element in TO_BRANCH, and TO_NAME a
 * non-existing path in it.
 */
svn_error_t *
svn_branch_copy_subtree_r(const svn_branch_el_rev_id_t *from_el_rev,
                          svn_branch_state_t *to_branch,
                          svn_branch_eid_t to_parent_eid,
                          const char *to_name,
                          apr_pool_t *scratch_pool);

/* Return the root repos-relpath of BRANCH.
 *
 * ### TODO: Clarify sequencing requirements.
 */
const char *
svn_branch_get_root_rrpath(const svn_branch_state_t *branch,
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

/* Find the (deepest) branch of which the path RRPATH is either the root
 * path or a normal, non-sub-branch path. An element need not exist at
 * RRPATH.
 *
 * Set *BRANCH_P to the deepest branch within ROOT_BRANCH (recursively,
 * including itself) that contains the path RRPATH.
 *
 * If EID_P is not null then set *EID_P to the element id of RRPATH in
 * *BRANCH_P, or to -1 if no element exists at RRPATH in that branch.
 *
 * If RRPATH is not within any branch in ROOT_BRANCH, set *BRANCH_P to
 * NULL and (if EID_P is not null) *EID_P to -1.
 *
 * ### TODO: Clarify sequencing requirements.
 */
void
svn_branch_find_nested_branch_element_by_rrpath(
                                svn_branch_state_t **branch_p,
                                int *eid_p,
                                svn_branch_state_t *root_branch,
                                const char *rrpath,
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

/* Branch all or part of an existing branch, making a new branch.
 *
 * Branch the subtree of FROM_BRANCH found at FROM_EID, to create
 * a new branch at TO_OUTER_BRANCH:TO_OUTER_PARENT_EID:NEW_NAME.
 *
 * FROM_BRANCH must be an immediate sub-branch of TO_OUTER_BRANCH.
 *
 * FROM_BRANCH:FROM_EID must be an existing element. It may be the
 * root of FROM_BRANCH. It must not be the root of a subbranch of
 * FROM_BRANCH.
 *
 * TO_OUTER_BRANCH:TO_OUTER_PARENT_EID must be an existing directory
 * and NEW_NAME must be nonexistent in that directory.
 */
svn_error_t *
svn_branch_branch(svn_branch_state_t **new_branch_p,
                  svn_branch_state_t *from_branch,
                  int from_eid,
                  svn_branch_state_t *to_outer_branch,
                  svn_branch_eid_t to_outer_parent_eid,
                  const char *new_name,
                  apr_pool_t *scratch_pool);

/* Branch the subtree of FROM_BRANCH found at FROM_EID, to appear
 * in the existing branch TO_BRANCH at TO_PARENT_EID:NEW_NAME.
 *
 * No element of FROM_BRANCH:FROM_EID may already exist in TO_BRANCH.
 * (### Or, perhaps, elements that already exist should be altered?)
 */
svn_error_t *
svn_branch_branch_into(svn_branch_state_t *from_branch,
                       int from_eid,
                       svn_branch_state_t *to_branch,
                       svn_branch_eid_t to_parent_eid,
                       const char *new_name,
                       apr_pool_t *scratch_pool);

/* Get the full id of branch BRANCH.
 */
const char *
svn_branch_get_id(svn_branch_state_t *branch,
                  apr_pool_t *result_pool);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_BRANCH_H */
