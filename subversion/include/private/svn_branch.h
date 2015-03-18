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

#include "svn_editor.h"

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

/** Element Identifier within a branch family.
 *
 * This does not contain an implied revision number or branch identifier.
 */
typedef int svn_branch_eid_t;

typedef struct svn_branch_el_rev_id_t svn_branch_el_rev_id_t;

typedef struct svn_branch_sibling_t svn_branch_sibling_t;

typedef struct svn_branch_instance_t svn_branch_instance_t;

/* Per-repository branching info.
 */
typedef struct svn_branch_repos_t
{
  /* The range of family ids assigned within this repos (starts at 0). */
  int next_fid;

  /* Array of (svn_branch_revision_info_t *), indexed by revision number. */
  apr_array_header_t *rev_roots;

  /* Hash of (int *fid -> svn_branch_family_t *), of all the branch family
     objects. */
  apr_hash_t *families;

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

/* Info about the branching in a specific revision (committed or uncommitted) */
typedef struct svn_branch_revision_root_t
{
  /* The repository in which this revision exists. */
  svn_branch_repos_t *repos;

  /* If committed, the revision number; else SVN_INVALID_REVNUM. */
  svn_revnum_t rev;

  /* The root branch instance. */
  struct svn_branch_instance_t *root_branch;

  /* All branch instances. */
  /* ### including root_branch */
  apr_array_header_t *branch_instances;

} svn_branch_revision_root_t;

/* Create a new branching revision-info object */
svn_branch_revision_root_t *
svn_branch_revision_root_create(svn_branch_repos_t *repos,
                                svn_revnum_t rev,
                                struct svn_branch_instance_t *root_branch,
                                apr_pool_t *result_pool);

/* A branch family.
 */
typedef struct svn_branch_family_t
{
  /* --- Identity of this object --- */

  /* The repository in which this family exists. */
  svn_branch_repos_t *repos;

  /* The outer family of which this is a sub-family. NULL if this is the
     root family. */
  /*struct svn_branch_family_t *outer_family;*/

  /* The FID of this family within its repository. */
  int fid;

  /* --- Contents of this object --- */

  /* The branch siblings in this family. */
  apr_array_header_t *branch_siblings;

  /* The range of branch sibling ids assigned within this family. */
  int first_bsid, next_bsid;

  /* The range of element ids assigned within this family. */
  int first_eid, next_eid;

  /* The immediate sub-family of this family, or NULL if none. */
  struct svn_branch_family_t *subfamily;

  /* The pool in which this object lives. */
  apr_pool_t *pool;
} svn_branch_family_t;

/* Create a new branch family object */
svn_branch_family_t *
svn_branch_family_create(svn_branch_repos_t *repos,
                         int fid,
                         int first_bsid,
                         int next_bsid,
                         int first_eid,
                         int next_eid,
                         apr_pool_t *result_pool);

/* Return the branch instances that are members of FAMILY in REV_ROOT.
 *
 * Return an empty array if there are none.
 */
apr_array_header_t *
svn_branch_family_get_branch_instances(
                                svn_branch_revision_root_t *rev_root,
                                svn_branch_family_t *family,
                                apr_pool_t *result_pool);

/* Assign a new element id in FAMILY.
 */
int
svn_branch_family_add_new_element(svn_branch_family_t *family);

/* Create a new, empty family in OUTER_FAMILY.
 */
svn_branch_family_t *
svn_branch_family_add_new_subfamily(svn_branch_family_t *outer_family);

/* Add a new branch sibling definition to FAMILY, with root element id
 * ROOT_EID.
 */
svn_branch_sibling_t *
svn_branch_family_add_new_branch_sibling(svn_branch_family_t *family,
                                         int root_eid);

/* A branch.
 *
 * A branch sibling object describes the characteristics of a branch
 * in a given family with a given BSID. This sibling is common to each
 * branch that has this same family and BSID: there can be one such instance
 * within each branch of its outer families.
 *
 * Often, all branches in a family have the same root element. For example,
 * branching /trunk to /branches/br1 results in:
 *
 *      family 1, branch 1, root-EID 100
 *          EID 100 => /trunk
 *          ...
 *      family 1, branch 2, root-EID 100
 *          EID 100 => /branches/br1
 *          ...
 *
 * However, the root element of one branch may correspond to a non-root
 * element of another branch; such a branch could be called a "subtree
 * branch". Using the same example, branching from the trunk subtree
 * /trunk/D (which is not itself a branch root) results in:
 *
 *      family 1, branch 3: root-EID = 104
 *          EID 100 => (nil)
 *          ...
 *          EID 104 => /branches/branch-of-trunk-subtree-D
 *          ...
 *
 * If family 1 were nested inside an outer family, then there could be
 * multiple branch-instances for each branch-sibling. In the above
 * example, all instances of (family 1, branch 1) will have root-EID 100,
 * and all instances of (family 1, branch 3) will have root-EID 104.
 */
struct svn_branch_sibling_t
{
  /* --- Identity of this object --- */

  /* The family of which this branch is a member. */
  svn_branch_family_t *family;

  /* The BSID of this branch within its family. */
  int bsid;

  /* The EID, within the outer family, of the branch root element. */
  /*int outer_family_eid_of_branch_root;*/

  /* --- Contents of this object --- */

  /* The EID within its family of its pathwise root element. */
  int root_eid;
};

/* Create a new branch sibling object */
svn_branch_sibling_t *
svn_branch_sibling_create(svn_branch_family_t *family,
                          int bsid,
                          int root_eid,
                          apr_pool_t *result_pool);

/* A branch instance.
 *
 * A branch instance object describes one branch in this family. (There is
 * one instance of this branch within each branch of its outer families.)
 */
struct svn_branch_instance_t
{
  /* --- Identity of this object --- */

  /* The branch-sibling class to which this branch belongs */
  svn_branch_sibling_t *sibling_defn;

  /* The revision to which this branch-revision-instance belongs */
  svn_branch_revision_root_t *rev_root;

  /* The branch instance within the outer family that contains the
     root element of this branch. Null if this is the root branch. */
  struct svn_branch_instance_t *outer_branch;

  /* The element in OUTER_BRANCH of the root of this branch, or -1
   * if this is the root branch. */
  int outer_eid;

  /* --- Contents of this object --- */

  /* EID -> svn_branch_el_rev_content_t mapping. */
  apr_hash_t *e_map;

};

/* Create a new branch instance object, with no elements (not even a root
 * element).
 */
svn_branch_instance_t *
svn_branch_instance_create(svn_branch_sibling_t *branch_sibling,
                           svn_branch_revision_root_t *rev_root,
                           svn_branch_instance_t *outer_branch,
                           int outer_eid,
                           apr_pool_t *result_pool);

/* Create a new branch instance at OUTER_BRANCH:OUTER_EID, of the branch class
 * BRANCH_SIBLING, with no elements (not even a root element).
 *
 * Do not require that a subbranch root element exists in OUTER_BRANCH,
 * nor create one.
 */
svn_branch_instance_t *
svn_branch_add_new_branch_instance(svn_branch_instance_t *outer_branch,
                                   int outer_eid,
                                   svn_branch_sibling_t *branch_sibling,
                                   apr_pool_t *scratch_pool);

/* Delete the branch instance BRANCH, and any subbranches recursively.
 *
 * Do not require that a subbranch root element exists in its outer branch,
 * nor delete it if it does exist.
 */
void
svn_branch_delete_branch_instance_r(svn_branch_instance_t *branch,
                                    apr_pool_t *scratch_pool);

/* Return an array of pointers to the branch instances that are immediate
 * sub-branches of BRANCH at or below EID.
 */
apr_array_header_t *
svn_branch_get_subbranches(const svn_branch_instance_t *branch,
                           int eid,
                           apr_pool_t *result_pool,
                           apr_pool_t *scratch_pool);

/* Return an array of pointers to the branch instances that are immediate
 * sub-branches of BRANCH.
 */
apr_array_header_t *
svn_branch_get_all_sub_branches(const svn_branch_instance_t *branch,
                                apr_pool_t *result_pool,
                                apr_pool_t *scratch_pool);

/* Return the subbranch instance rooted at BRANCH:EID, or NULL if that is
 * not a subbranch root.
 */
svn_branch_instance_t *
svn_branch_get_subbranch_at_eid(svn_branch_instance_t *branch,
                                int eid,
                                apr_pool_t *scratch_pool);

/* element */
/*
typedef struct svn_branch_element_t
{
  int eid;
  svn_branch_family_t *family;
  svn_node_kind_t node_kind;
} svn_branch_element_t;
*/

/* Branch-Element-Revision */
struct svn_branch_el_rev_id_t
{
  /* The branch-instance that applies to REV. */
  svn_branch_instance_t *branch;
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
svn_branch_el_rev_id_create(svn_branch_instance_t *branch,
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
  /* content (kind, props, text, ...) */
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


/* Declare that the following function requires/implies that in BRANCH's
 * mapping, for each existing element, the parent also exists.
 *
 * ### Find a better word? flattened, canonical, finalized, ...
 */
#define SVN_BRANCH_SEQUENCE_POINT(branch)

/* In BRANCH, get element EID's node (parent, name, content).
 *
 * If element EID is not present, return null. Otherwise, the returned
 * node's content may be null meaning it is unknown.
 */
svn_branch_el_rev_content_t *
svn_branch_map_get(const svn_branch_instance_t *branch,
                   int eid);

/* In BRANCH, delete element EID.
 */
void
svn_branch_map_delete(svn_branch_instance_t *branch,
                      int eid);

/* Set or change the EID:element mapping for EID in BRANCH.
 *
 * Duplicate NEW_NAME and NEW_CONTENT into the branch mapping's pool.
 */
void
svn_branch_map_update(svn_branch_instance_t *branch,
                      int eid,
                      svn_branch_eid_t new_parent_eid,
                      const char *new_name,
                      const svn_element_content_t *new_content);

/* Set or change the EID:element mapping for EID in BRANCH to reflect a
 * subbranch root node. This node has no content in this branch; the
 * corresponding element of the subbranch will define its content.
 *
 * Duplicate NEW_NAME into the branch mapping's pool.
 */
void
svn_branch_map_update_as_subbranch_root(svn_branch_instance_t *branch,
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
svn_branch_map_purge_orphans(svn_branch_instance_t *branch,
                             apr_pool_t *scratch_pool);

/* Purge orphaned elements and subbranches.
 */
void
svn_branch_purge_r(svn_branch_instance_t *branch,
                   apr_pool_t *scratch_pool);

/* Branch a subtree.
 *
 * For each element that in FROM_BRANCH is a pathwise descendant of
 * FROM_PARENT_EID, excluding FROM_PARENT_EID itself, instantiate the
 * same element in TO_BRANCH. For each element, keep the same parent
 * element (except, for first-level children, change FROM_PARENT_EID to
 * TO_PARENT_EID), name, and content that it had in FROM_BRANCH.
 *
 * ### It's not particularly useful to allow TO_PARENT_EID != FROM_PARENT_EID.
 *
 * FROM_BRANCH and TO_BRANCH must be different branch instances in the
 * same branch family.
 *
 * FROM_PARENT_EID MUST be an existing element in FROM_BRANCH. It may be the
 * root element of FROM_BRANCH.
 *
 * TO_PARENT_EID MUST be an existing element in TO_BRANCH. It may be the
 * root element of TO_BRANCH.
 */
svn_error_t *
svn_branch_map_branch_children(svn_branch_instance_t *from_branch,
                               int from_parent_eid,
                               svn_branch_instance_t *to_branch,
                               int to_parent_eid,
                               apr_pool_t *scratch_pool);

/* Branch a subtree.
 *
 * Adjust TO_OUTER_BRANCH and its subbranches (recursively),
 * to reflect branching a subtree from FROM_BRANCH:FROM_EID to
 * create a new subbranch of TO_OUTER_BRANCH at TO_OUTER_PARENT_EID:NEW_NAME.
 *
 * FROM_BRANCH must be an immediate child branch of OUTER_BRANCH.
 *
 * FROM_BRANCH:FROM_EID must be an existing element. It may be the
 * root of FROM_BRANCH. It must not be the root of a subbranch of
 * FROM_BRANCH.
 *
 * TO_OUTER_BRANCH:TO_OUTER_PARENT_EID must be an existing directory
 * and NEW_NAME must be nonexistent in that directory.
 */
svn_error_t *
svn_branch_branch_subtree_r(svn_branch_instance_t **new_branch_p,
                            svn_branch_instance_t *from_branch,
                            int from_eid,
                            svn_branch_instance_t *to_outer_branch,
                            svn_branch_eid_t to_outer_parent_eid,
                            const char *new_name,
                            apr_pool_t *scratch_pool);

/* Instantiate a new branch of FROM_BRANCH, selecting only the subtree at
 * FROM_EID, at existing branch-root element TO_OUTER_BRANCH:TO_OUTER_EID.
 */
svn_error_t *
svn_branch_branch_subtree_r2(svn_branch_instance_t **new_branch_p,
                             svn_branch_instance_t *from_branch,
                             int from_eid,
                             svn_branch_instance_t *to_outer_branch,
                             svn_branch_eid_t to_outer_eid,
                             svn_branch_sibling_t *new_branch_def,
                             apr_pool_t *scratch_pool);

/* Copy a subtree.
 *
 * For each element that in FROM_BRANCH is a pathwise descendant of
 * FROM_PARENT_EID, excluding FROM_PARENT_EID itself, instantiate a
 * new element in TO_BRANCH. For each element, keep the same parent
 * element (except, for first-level children, change FROM_PARENT_EID to
 * TO_PARENT_EID), name, and content that it had in FROM_BRANCH.
 *
 * Assign a new EID in TO_BRANCH's family for each copied element.
 *
 * FROM_BRANCH and TO_BRANCH may be the same or different branch instances
 * in the same or different branch families.
 *
 * FROM_PARENT_EID MUST be an existing element in FROM_BRANCH. It may be the
 * root element of FROM_BRANCH.
 *
 * TO_PARENT_EID MUST be an existing element in TO_BRANCH. It may be the
 * root element of TO_BRANCH.
 */
svn_error_t *
svn_branch_map_copy_children(svn_branch_instance_t *from_branch,
                             int from_parent_eid,
                             svn_branch_instance_t *to_branch,
                             int to_parent_eid,
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
 *     elements; make a subbranch by branching the source subbranch in
 *     cases where possible; make a subbranch in a new family?
 *
 * TO_PARENT_EID must be a directory element in TO_BRANCH, and TO_NAME a
 * non-existing path in it.
 */
svn_error_t *
svn_branch_copy_subtree_r(const svn_branch_el_rev_id_t *from_el_rev,
                          svn_branch_instance_t *to_branch,
                          svn_branch_eid_t to_parent_eid,
                          const char *to_name,
                          apr_pool_t *scratch_pool);

/* Return the root repos-relpath of BRANCH.
 *
 * ### TODO: Clarify sequencing requirements.
 */
const char *
svn_branch_get_root_rrpath(const svn_branch_instance_t *branch,
                           apr_pool_t *result_pool);

/* Return the branch-relative path of element EID in BRANCH.
 *
 * If the element EID does not currently exist in BRANCH, return NULL.
 *
 * ### TODO: Clarify sequencing requirements.
 */
const char *
svn_branch_get_path_by_eid(const svn_branch_instance_t *branch,
                           int eid,
                           apr_pool_t *result_pool);

/* Return the repos-relpath of element EID in BRANCH.
 *
 * If the element EID does not currently exist in BRANCH, return NULL.
 *
 * ### TODO: Clarify sequencing requirements.
 */
const char *
svn_branch_get_rrpath_by_eid(const svn_branch_instance_t *branch,
                             int eid,
                             apr_pool_t *result_pool);

/* Return the EID for the branch-relative path PATH in BRANCH.
 *
 * If no element of BRANCH is at this path, return -1.
 *
 * ### TODO: Clarify sequencing requirements.
 */
int
svn_branch_get_eid_by_path(const svn_branch_instance_t *branch,
                           const char *path,
                           apr_pool_t *scratch_pool);

/* Return the EID for the repos-relpath RRPATH in BRANCH.
 *
 * If no element of BRANCH is at this path, return -1.
 *
 * ### TODO: Clarify sequencing requirements.
 */
int
svn_branch_get_eid_by_rrpath(svn_branch_instance_t *branch,
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
                                svn_branch_instance_t **branch_p,
                                int *eid_p,
                                svn_branch_instance_t *root_branch,
                                const char *rrpath,
                                apr_pool_t *scratch_pool);

/* Create a new revision-root object *REV_ROOT_P, initialized with info
 * parsed from STREAM, allocated in RESULT_POOL.
 */
svn_error_t *
svn_branch_revision_root_parse(svn_branch_revision_root_t **rev_root_p,
                               int *next_fid_p,
                               svn_branch_repos_t *repos,
                               svn_stream_t *stream,
                               apr_pool_t *result_pool,
                               apr_pool_t *scratch_pool);

/* Write to STREAM a parseable representation of REV_ROOT.
 */
svn_error_t *
svn_branch_revision_root_serialize(svn_stream_t *stream,
                                   svn_branch_revision_root_t *rev_root,
                                   int next_fid,
                                   apr_pool_t *scratch_pool);

/* Branch the subtree of FROM_BRANCH found at FROM_EID, to create
 * a new branch at TO_OUTER_BRANCH:TO_OUTER_PARENT_EID:NEW_NAME.
 *
 * FROM_BRANCH must be an immediate sub-branch of TO_OUTER_BRANCH.
 */
svn_error_t *
svn_branch_branch(svn_branch_instance_t **new_branch_p,
                  svn_branch_instance_t *from_branch,
                  int from_eid,
                  svn_branch_instance_t *to_outer_branch,
                  svn_branch_eid_t to_outer_parent_eid,
                  const char *new_name,
                  apr_pool_t *scratch_pool);

/* Change the existing simple sub-tree at OUTER_BRANCH:OUTER_EID into a
 * sub-branch in a new branch family.
 *
 * Set *NEW_BRANCH_P to the new branch.
 *
 * ### TODO: Also we must (in order to maintain correctness) branchify
 *     the corresponding subtrees in all other branches in this family.
 *
 * TODO: Allow adding to an existing family, by specifying a mapping.
 *
 *   create a new family
 *   create a new branch-def and branch-instance
 *   for each element in subtree:
 *     ?[unassign eid in outer branch (except root element)]
 *     assign a new eid in inner branch
 */
svn_error_t *
svn_branch_branchify(svn_branch_instance_t **new_branch_p,
                     svn_branch_instance_t *outer_branch,
                     svn_branch_eid_t outer_eid,
                     apr_pool_t *scratch_pool);

/* Get the full id of branch BRANCH.
 */
const char *
svn_branch_instance_get_id(svn_branch_instance_t *branch,
                           apr_pool_t *result_pool);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_BRANCH_H */
