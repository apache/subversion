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
 * @file svn_editor3e.h
 * @brief Tree editing
 *
 * @since New in 1.10.
 */

#ifndef SVN_EDITOR3E_H
#define SVN_EDITOR3E_H

#include <apr_pools.h>

#include "svn_types.h"
#include "svn_error.h"
#include "svn_delta.h"
#include "svn_ra.h"
#include "private/svn_branch.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/*
 * ===================================================================
 * Some Characteristics of this Versioning Model
 * ===================================================================
 *
 *   - the versioned state of an element consists of:
 *        its tree linkage (parent element identity, name)
 *        its payload (props, text, link-target)
 *
 *   - an element can be resurrected with the same element id that it
 *        had before it was deleted, even if it had been deleted from
 *        all branches
 *
 *   - copying is independent per node: a copy-child is not detectably
 *     "the same copy" as its parent, it's just copied at the same time
 *       => (cp ^/a@5 b; del b/c; cp ^/a/c@5 b/c) == (cp ^/a@5 b)
 *
 * ===================================================================
 * Possible contexts (uses) for an editor
 * ===================================================================
 *
 * (1) Commit
 *
 *   - From single-rev or mixed-rev;
 *       need to tell the receiver the "from" revision(s)
 *   - To single-rev (implied new head revision)
 *   - Diff: with simple context (for simple merge with recent commits)
 *   - Copies: can send O(1) "copy"
 *       with O(E) edits inside; E ~ size of edits
 *   - Copies: can copy from within the new rev (?)
 *
 * Commit is logically the same whether from a WC or "direct". In either
 * case the client has to have an idea of what it is basing its changes
 * on, and tell the server so that the server can perform its Out-Of-Date
 * checks. This base could potentially be mixed-revision. A non-WC commit
 * is typically unlikely to work from a mixed-rev base, but logically it
 * is possible. An O(1) copy is more obviously needed for a non-WC commit
 * such as creating a branch directly in the repository. One could argue
 * that committing a copy from a WC already involves O(N) space and time
 * for the copy within the WC, and so requiring an O(1) commit is not
 * necessarily justifiable; but as commit may be vastly more expensive
 * than local operations, making it important even in this case. There is
 * also the WC-to-repo copy operation which involves elements of committing
 * from a WC and "directly".
 *
 * (2) Update/Switch
 *
 *   - One change per *WC* path rather than per *repo* path
 *   - From mixed-rev to single-rev
 *   - Rx initially has a complete copy of the "from" state
 *   - Diff: with context (for merging)
 *   - Copies: can expand "copy" (non-recursive)
 *
 * (3) Diff (wc-base/repo:repo) (for merging/patching/displaying)
 *
 *   - From mixed-rev (for wc-base) to single-rev
 *       (enhancement: mixed-rev "to" state?)
 *   - Rx needs to be told the "from" revisions
 *   - Diff: with context (for merging)
 *   - Diff: can be reversible
 *   - Copies: can send O(1) "copy" (recursive + edits)
 *   - Copies: can expand "copy" (non-recursive)
 *
 * ===================================================================
 * Two different styles of "editing"
 * ===================================================================
 *
 * (1) Ordered, cumulative changes to a txn
 *
 * (2) Transmission of a set of independent changes
 *
 * These can be mixed: e.g. one interface declared here uses style (1)
 * for tree changes with style (2) for payload changes.
 *
 * ===================================================================
 * Two different ways of "addressing" a node
 * ===================================================================
 *
 * Two classes of "node" need to be addressed within an edit:
 *
 *   - a node that already existed in the sender's base state
 *   - a node that the sender is creating
 *
 * Two basic forms of address are being considered:
 *
 * (1) path [@ old-rev] + created-relpath
 *
 * (2) element-id
 *
 * (We are talking just about what the editor API needs to know, not
 * about how the sender or receiver implementation connects the editor
 * API to a real WC or repository.)
 *
 * Form (1), called "txn path" in the first design, and form (2), the
 * "local node-branch id" used in the second design, both provide a
 * locally unique id for each node-branch referenced in the edit.
 *
 * Where they differ is that form (1) *also* happens to provide a specific
 * revision number. This can be used, in the case of a pre-existing node,
 * as the base revision for OOD checking when modifying or deleting a
 * node. The "node-branch-id" form used in the second design doesn't
 * implicitly include a base revision. The base revision is communicated
 * separately when required.
 *
 * To make this clearer, we can define the "local-node-branch-id" to be
 * exactly a "txn path". We do this in the second design. We do not use
 * the revision number component as an implicit "base revision"; instead
 * we pass the base revision separately when required.
 *
 * ### Are the two designs explicit and consistent in where a peg rev is
 *     provided for the OOD check? (When creating a new node, the OOD
 *     check may or may not be interested in a base revision at which
 *     the node did not exist.)
 *
 *
 * Addressing by Path
 * ------------------
 *
 * A node-branch that exists at the start of the edit can be addressed
 * by giving a location (peg-path @ peg-rev) where it was known to exist.
 *
 * The server commit logic can look up (peg-path @ peg-rev) and trace
 * that node-branch forward to the txn, and
 * find the path at which that node-branch is currently located in the
 * txn (or find that it is not present), as well as discovering whether
 * there was any change to it (including deletion) between peg-rev and
 * the txn-base, or after txn-base up to the current state of the txn.
 *
 * A node-branch created within the txn can be addressed by path only if
 * the sender knows that path. In order to create the node the sender
 * would have specified a parent node-branch and a new name. The node can
 * now be addressed as
 *
 *   (parent peg path @ rev) / new-name
 *
 * which translates in the txn to
 *
 *   parent-path-in-txn / new-name
 *
 * When the sender creates another node as a child of this one, this second
 * new node can be addressed as either
 *
 *   (parent-peg-path @ peg-rev) / new-name / new-name-2
 *
 * or, if the sender knows the path-in-txn that resulted from the first one
 *
 *   parent-path-in-txn / new-name / new-name-2
 *
 * The difficulty is that, in a commit, the txn is based on a repository
 * state that the sender does not know. The paths may be different in that
 * state, due to recently committed moves, if the Out-Of-Date logic permits
 * that. The "parent-path-in-txn" is not, in general, known to the sender.
 *
 * Therefore the sender needs to address nested additions as
 *
 *   (peg-path @ peg-rev) / (path-created-in-txn)
 *
 * Why can't we use the old Ev1 form (path-in-txn, wc-base-rev)?
 *
 *     Basically because, in general (if other commits on the server
 *     are allowed to move the nodes that this commit is editing),
 *     then (path-in-txn, wc-base-rev) does not unambiguously identify
 *     a node-revision or a specific path in revision wc-base-rev. The
 *     sender cannot know what path in the txn corresponds to a given path
 *     in wc-base-rev.
 *
 * Why not restrict OOD checking to never merge with out-of-date moves?
 *
 *     It would seem unnecessarily restrictive to expect that we would
 *     never want the OOD check to allow merging with a repository-side
 *     move of a parent of the node we are editing. That would not be in
 *     the spirit of move tracking, nor would it be symmetrical with the
 *     client-side expected behaviour of silently merging child edits
 *     with a parent move.
 *
 * Why not provide a way for the client to learn the path-in-txn resulting
 * from each operation in the edit, to be used in further operations that
 * refer to the same node-branch?
 * 
 *     That's basically equivalent to specifying the address in a
 *     satisfactory manner in the first place. And it's only possible
 *     with a sequential editing model.
 *
 * Addressing by Element-Id
 * ------------------------
 *
 * For the purposes of addressing elements within an edit, element-ids need not
 * be repository-wide unique ids, they only need to be known within the
 * editor. However, if the sender is to use ids that are not already known
 * to the receiver, then it must provide a mapping from ids to elements.
 *
 * The sender assigns an id to each element including new elements. (It is not
 * appropriate for the editor or its receiver to assign an id to an added
 * element, because the sender needs to be able to refer to that element as a
 * parent element for other elements without creating any ordering dependency.)
 *
 * If the sender does not know the repository-wide id for an element, which is
 * especially likely for a new element, it must assign a temporary id for use
 * just within the edit. In that case, each new element or element-branch is
 * necessarily independent. On the other hand, if the sender is able to
 * use repository-wide ids, then the possibility arises of the sender
 * asking to create a new element or element-branch that has the same id
 * as an existing one. The receiver would consider that to be a conflict.
 *
 *
 * ===================================================================
 * WC update/switch
 * ===================================================================
 *
 * How Subversion does an update (or switch), roughly:
 *
 *   - Client sends a "report" of WC base node locations to server.
 *   - Server calculates a diff from reported mixed-rev WC-base to
 *     requested single-rev target.
 *   - Server maps repo paths to WC paths (using the report) before
 *     transmitting edits.
 *
 * ===================================================================
 * Commit from WC
 * ===================================================================
 * 
 * How Subversion does a commit, roughly:
 *
 *   - Server starts a txn based on current head rev
 *
 *                   r1 2 3 4 5 6 7 8 head  txn
 *     WC-base  @4 -> A . . M . . . . .     |...
 *      |_B     @3 -> A . M . . . . . .  == |...D
 *      |_C     @3 -> A . M . . . . . .     |...
 *        |_foo @6 -> . A . . . M . D .     |...
 *       \_____________________________________/
 *            del /B r3
 *
 *   - Client sends changes based on its WC-base rev for each node,
 *     sending "this is the base rev I'm using" for each node.
 *
 *   - Server "merges" the client's changes into the txn on the fly,
 *     rejecting as "out of date" any change that requires a non-trivial
 *     merge.
 *
 *                   r1 2 3 4 5 6 7 8 head
 *     WC-base  @4 -> A . . M . . . . .
 *      |_B     @3 -> A . M . . . . . .    txn
 *      |_C     @3 -> A . M . . . . . . \  |...
 *        |_foo @6 -> . A . . . M . D .  \ |...x
 *       \                                 |...
 *        \                                |...OOD! (deleted since r6)
 *         \___________________________________/
 *            edit /C/foo r6
 *
 *   - Server "merges" the txn in the same way with any further commits,
 *     until there are no further commits, and then commits the txn.
 *
 * The old design assumes that the client can refer to a node by its path.
 * Either this path in the txn refers to the same node as in the WC base,
 * or the WC base node has since been deleted and perhaps replaced. This is
 * detected by the OOD check. The node's path-in-txn can never be different
 * from its path-in-WC-base.
 *
 * When we introduce moves, it is possible that nodes referenced by the WC
 * will have been moved in the repository after the WC-base and before the
 * txn-base. Unless the client queries for such moves, it will not know
 * what path-in-txn corresponds to each path-in-WC-base.
 * 
 * It seems wrong to design an editor interface that requires there have
 * been no moves in the repository between the WC base and the txn-base
 * affecting the paths being referenced in the commit. Not totally
 * unreasonable for the typical work flows of today, but unreasonably
 * restricting the work flows that should be possible in the future with
 * move tracking in place.
 *
 * ===================================================================
 * Commit Rebase and OOD Checks
 * ===================================================================
 *
 * When the client commits changes, it describes the change for each node
 * against a base version of that node. (For new nodes being created, the
 * base is "none".)
 *
 * The server must inform the client of the result of the commit, and
 * there are only two possible outcomes. Either the state of each node
 * being changed by the commit now matches the committed revision and
 * the client's base version of each other node remains unchanged, or
 * the commit fails.
 *
 * The rebase on commit is a simple kind of merge. For each node being
 * changed in the commit, the server must either accept the incoming
 * version or reject the whole commit. It can only "merge" the incoming
 * change with recent changes in the repository if the changes are
 * trivially compatible, such that the committed version can be used as
 * the result. It cannot perform a merge that creates a result that
 * differs from the version sent by the client, as there is no mechanism
 * to inform the client of this.
 *
 * If the rebase rejects the commit, the client's base version of a node
 * is said to be "out of date": there are two competing changes to the
 * node. After a commit is rejected, the changes can be merged on the
 * client side via an "update".
 *
 * The key to the rebase logic is defining what constitutes a "trivial"
 * merge. That is a subjective design choice, as it controls how "close"
 * two independently committed changes may be before the system forces
 * the user to merge them on the client side. In that way it is the same
 * as a three-way text merge tool having options to control how close
 * a change on one side may be to a change on the other side before it
 * considers them to conflict -- whether one line of unchanged context is
 * needed between them, or changes to adjacent lines are accepted, or in
 * some tools changes affecting separate words or characters on the same
 * line can be merged without considering them to conflict.
 *
 * Different rebase-on-commit policies are appropriate for different use
 * cases, and so it is reasonable to design the system such that the user
 * can configure what policy to use.
 *
 * Here are two specifications of requirements for a rebase-on-commit
 * merge. Both of them consider each node independently, except for the
 * need to end up with a valid tree hierarchy. Both of them consider
 * something to be "changed" only if it is different from what it was
 * originally, and not merely if it was changed and then changed back
 * again or if a no-op "change" was committed. This follows the principle
 * that collapsing intermediate history should make no difference.
 * Similarly, they MUST interpret a no-op incoming "change" as no
 * incoming change.
 *
 * Rebase Policy: "Changes"
 * ------------------------
 *
 * This policy considers the intent of a change to be a change rather
 * than to be the creation of the new state. It merges a change with
 * a no-change, per node. It is more strict than the "State Setting"
 * policy.
 *
 *      Changes on one side vs. requirements on other side of the merge
 *      -----------------------------------------------------------------
 *      change     requirements on other side
 *      ------     ------------------------------------------------------
 *
 *      make       element-id not already assigned [1]
 *      new        target parent element exists (may have
 *      node         been moved/altered/del-and-resurrected)
 *                 no same-named sibling exists in target parent
 *
 *      copy       (source: no restriction)
 *      (root      target element-id does not exist [1]
 *      node)      target parent element exists (")
 *                 no same-named sibling exists in target parent
 *
 *      resurrect  element does not exist
 *      (per       target parent element exists (")
 *      node)      no same-named sibling exists in target parent
 *
 *      move       element exists and is identical to base
 *      &/or       (children: no restriction)
 *      alter      target parent element exists (")
 *                 no same-named sibling exists in target parent
 *
 *      del        element exists and is identical to base
 *      (per       (parent: no restriction)
 *      node)      no new children on the other side
 *                   (they would end up as orphans)
 *
 * Rebase Policy: "State Setting"
 * ------------------------------
 *
 * This policy considers the intent of a change to be the creation of
 * the new state. It allows silent de-duplication of identical changes
 * on both sides, per node. It is less strict than the "Changes" policy.
 *
 *      Changes on one side vs. requirements on other side of the merge
 *      -----------------------------------------------------------------
 *      change     requirements on other side
 *      ------     ------------------------------------------------------
 *
 *      make       element-id not already assigned, or
 *      new          element exists and is identical [1]
 *      node       target parent element exists (may have
 *                   been moved/altered/del-and-resurrected)
 *                 no same-named sibling exists in target parent
 *
 *      copy       (source: no restriction)
 *      (root      target element-id does not exist, or
 *      node)        element exists and is identical [1]
 *                 target parent element exists (")
 *                 no same-named sibling exists in target parent
 *
 *      resurrect  element does not exist, or
 *      (per         element exists and is identical
 *      node)      target parent element exists (")
 *                 no same-named sibling exists in target parent
 *
 *      move       element exists, and
 *      &/or         is identical to base or identical to target
 *      alter      (children: no restriction)
 *                 target parent element exists (")
 *                 no same-named sibling exists in target parent
 *
 *      del        element exists and is identical to base, or
 *      (per         element is deleted
 *      node)      (parent: no restriction)
 *                 no new children on the other side
 *                   (they would end up as orphans)
 *
 * Terminology:
 *      An id. "exists" even if deleted, whereas an element "exists"
 *      only when it is alive, not deleted. An element is "identical"
 *      if its payload and name and parent-eid are identical.
 *
 * Notes:
 *      [1] A target node or id that is to be created can be found to
 *          "exist" on the other side only if the request is of the form
 *          "create a node with id <X>" rather than "create a node with
 *          a new id".
 *
 * Other Rebase Policies
 * ---------------------
 *
 * The two rebase policies above are general-purpose, each conforming to
 * a simple model of versioned data in which changes to separate nodes
 * are always considered independent and any changes to the same node are
 * considered inter-dependent. For special purposes, a finer-grained or a
 * larger-grained notion of dependence may be useful.
 *
 * A policy could allow finer-grained merging. For example, an incoming
 * commit making both a property change and a text change, where the
 * repository side has only the same prop-change or the same text-change
 * but not both.
 *
 * A policy could consider changes at a larger granularity. For example,
 * it could consider that any change to the set of immediate children of
 * a directory conflicts with any other change to its set of immediate
 * children. It could consider that a moved parent directory conflicts
 * with any changes inside that subtree. (This latter might be appropriate
 * for Java programming where a rename of a parent directory typically
 * needs to be reflected inside files in the subtree.)
 *
 * TODO
 * ====
 *
 *   * Catalogue exactly what rebase policy Subversion 1.9 implements.
 */

/*
 * ===================================================================
 * Copy From This Revision
 * ===================================================================
 *
 * ### Is copy-from-this-revision needed?
 */
/*#define SVN_EDITOR3_WITH_COPY_FROM_THIS_REV*/


/**
 * @defgroup svn_editor The editor interface
 * @{
 */

/** Tree Editor
 */
typedef struct svn_editor3_t svn_editor3_t;


/** These functions are called by the tree delta driver to edit the target.
 *
 * @see #svn_editor3_t
 *
 * @defgroup svn_editor3_drive Driving the editor
 * @{
 */

/*
 * ========================================================================
 * Editor for Commit (independent per-element changes; element-id addressing)
 * ========================================================================
 *
 * Scope of Edit:
 *
 * The edit may include changes to one or more branches.
 *
 * Edit Operations:
 *
 *   operations on elements of a branch
 *   - alter     br:eid[2]     new-(parent-eid[2],name,payload)
 *   - copy-one  br:eid@rev[3] new-(parent-eid[2],name,payload)  ->  new-eid
 *   - copy-tree br:eid@rev[3] new-(parent-eid[2],name)          ->  new-eid
 *   - delete    br:eid[1]
 *
 *   operations on branches
 *   - ### TODO: branch, mkbranch, rmbranch, ...?
 *
 * Preconditions:
 *
 *   [1] element must exist in initial state
 *   [2] element must exist in final state
 *   [3] source must exist in committed revision or txn final state
 *
 * Characteristics of this editor:
 *
 *   - Tree structure is partitioned among the elements, in such a way that
 *     each of the most important concepts such as "move", "copy",
 *     "create" and "delete" is modeled as a single change to a single
 *     element. The name and the identity of its parent directory element are
 *     considered to be attributes of that element, alongside its payload.
 *
 *   - Changes are independent and unordered. The change to one element is
 *     independent of the change to any other element, except for the
 *     requirement that the final state forms a valid (path-wise) tree
 *     hierarchy. A valid tree hierarchy is NOT required in any
 *     intermediate state after each change or after a subset of changes.
 *
 *   - Copies can be made in two ways: a copy of a single element which can
 *     be edited, or a "cheap" O(1) copy of a subtree which cannot be edited.
 *
 *   - Deleting a subtree is O(1) cheap: when the root element of a subtree
 *     is deleted, the rest of the subtree disappears implicitly.
 *
 *   - The commit rebase MAY (but need not) merge a repository-side move
 *     with incoming edits inside the moved subtree, and vice-versa.
 *
 * Notes on Copying:
 *
 *   - copy_one and copy_tree are separate. In this model it doesn't
 *     make sense to describe a copy-and-modify by means of generating
 *     a full copy (with ids, at least implicitly, for each element) and
 *     then potentially "deleting" some of the generated child elements.
 *     Instead, each element has to be specified in its final state or not
 *     at all. Tree-copy therefore generates an immutable copy, while
 *     single-element copy supports arbitrary copy-and-modify operations,
 *     and tree-copy can be used for any unmodified subtrees therein.
 *     There is no need to reference the root element of a tree-copy again
 *     within the same edit, and so no id is provided. [### Or maybe there
 *     is such a need, when performing the same copy in multiple branches;
 *     but in that case the caller would need to specify the new eids.]
 */

/** Allocate a new EID.
 */
svn_error_t *
svn_editor3_new_eid(svn_editor3_t *editor,
                    svn_branch_eid_t *eid_p);

/** Create a new branch or access an existing branch.
 *
 * When creating a branch, declare its root element id to be ROOT_EID. Do
 * not instantiate the root element, nor any other elements.
 *
 * We use a common 'open subbranch' method for both 'find' and 'add'
 * cases, according to the principle that the editor dictates the new
 * state without reference to the old state.
 *
 * This must be used before editing the resulting branch. In that sense
 * this method conceptually returns a "branch editor" for the designated
 * branch.
 *
 * When adding a new branch, PREDECESSOR and ROOT_EID are used; when
 * finding an existing branch they must match it (else throw an error).
 *
 * ### Should we take a single branch-id parameter instead of taking
 *     (outer-bid, outer-eid) and returning the new branch-id?
 *
 *     If we want to think of this as a "txn editor" method and we want
 *     random access to any branch, that would be a good option.
 *
 *     If we want to think of this as a "branch editor" method then
 *     outer-branch-id conceptually identifies "this branch" that we're
 *     editing and could be represented instead by a different value of
 *     the "editor" parameter; and the subbranch must be an immediate child.
 */
svn_error_t *
svn_editor3_open_branch(svn_editor3_t *editor,
                        const char **new_branch_id_p,
                        svn_branch_rev_bid_t *predecessor,
                        const char *outer_branch_id,
                        int outer_eid,
                        int root_eid,
                        apr_pool_t *result_pool);
svn_error_t *
svn_editor3_branch(svn_editor3_t *editor,
                   const char **new_branch_id_p,
                   svn_branch_rev_bid_eid_t *from,
                   const char *outer_branch_id,
                   int outer_eid,
                   apr_pool_t *result_pool);

/** Specify the tree position and payload of the element of @a branch_id
 * identified by @a eid.
 *
 * This may create a new element or alter an existing element.
 *
 * Set the element's parent and name to @a new_parent_eid and @a new_name.
 *
 * Set the payload to @a new_payload. If @a new_payload is null, create a
 * subbranch-root element instead of a normal element.
 *
 * A no-op change MUST be accepted but, in the interest of efficiency,
 * SHOULD NOT be sent.
 *
 * @see #svn_editor3_t
 *
 * If the element ...                   we can describe the effect as ...
 *
 *   exists in the branch               =>  altering it;
 *   previously existed in the branch   =>  resurrecting it;
 *   only existed in other branches     =>  branching it;
 *   never existed anywhere             =>  creating or adding it.
 *
 * However, these are imprecise descriptions and not mutually exclusive.
 * For example, if it existed previously in this branch and another, then
 * we may describe the result as 'resurrecting' and/or as 'branching'.
 *
 * ### When converting this edit to an Ev1 edit, do we need a way to specify
 *     where the Ev1 node is to be "copied" from, when this is branching the
 *     element?
 */
svn_error_t *
svn_editor3_alter(svn_editor3_t *editor,
                  const char *branch_id,
                  svn_branch_eid_t eid,
                  svn_branch_eid_t new_parent_eid,
                  const char *new_name,
                  const svn_element_payload_t *new_payload);

/** Create a new element that is copied from a pre-existing
 * <SVN_EDITOR3_WITH_COPY_FROM_THIS_REV> or newly created </>
 * element, with the same or different content (parent, name, payload).
 *
 * Assign the target element a locally unique element-id, @a local_eid,
 * with which it can be referenced within this edit.
 *
 * Copy from the source element at @a src_el_rev.
 * <SVN_EDITOR3_WITH_COPY_FROM_THIS_REV>
 * If @a src_el_rev->rev is #SVN_INVALID_REVNUM, it means copy from within
 * the new revision being described.
 *   ### See note on copy_tree().
 * </SVN_EDITOR3_WITH_COPY_FROM_THIS_REV>
 *
 * Set the target element's parent and name to @a new_parent_eid and
 * @a new_name. Set the target element's payload to @a new_payload, or make
 * it the same as the source if @a new_payload is null.
 *
 * @note This copy is not recursive. Children may be copied separately if
 * required.
 *
 * @note The @a local_eid has meaning only within this edit. The server
 * must create a new element, and MUST NOT match local_eid with any other
 * element that may already exist or that may be created by another edit.
 *
 * @see svn_editor3_copy_tree(), #svn_editor3_t
 */
svn_error_t *
svn_editor3_copy_one(svn_editor3_t *editor,
                     const svn_branch_rev_bid_eid_t *src_el_rev,
                     const char *branch_id,
                     svn_branch_eid_t local_eid,
                     svn_branch_eid_t new_parent_eid,
                     const char *new_name,
                     const svn_element_payload_t *new_payload);

/** Create a copy of a pre-existing
 * <SVN_EDITOR3_WITH_COPY_FROM_THIS_REV> or newly created </>
 * subtree, with the same content (tree structure and payload).
 *
 * Each element in the source subtree will be copied (branched) to the same
 * relative path within the target subtree. The elements created by
 * this copy cannot be modified or addressed within this edit.
 *
 * Set the target root element's parent and name to @a new_parent_eid and
 * @a new_name.
 *
 * Copy from the source subtree at @a src_el_rev.
 * <SVN_EDITOR3_WITH_COPY_FROM_THIS_REV>
 * If @a src_el_rev->rev is #SVN_INVALID_REVNUM, it means copy from within
 * the new revision being described. In this case the subtree copied is
 * the FINAL subtree as committed, regardless of the order in which the
 * edit operations are described.
 *   ### Is it necessarily the case that the state at the end of the edit
 *       is the state to be committed (subject to rebasing), or is it
 *       possible that a later edit might be performed on the txn?
 *       And how might we apply this principle to a non-commit editor
 *       such as a WC update?
 * </SVN_EDITOR3_WITH_COPY_FROM_THIS_REV>
 *
 * The content of each element copied from an existing revision is the content
 * of the source element. The content of each element copied from this revision
 * is the FINAL content of the source element as committed.
 *
 * @see svn_editor3_copy_one(), #svn_editor3_t
 */
svn_error_t *
svn_editor3_copy_tree(svn_editor3_t *editor,
                      const svn_branch_rev_bid_eid_t *src_el_rev,
                      const char *branch_id,
                      svn_branch_eid_t new_parent_eid,
                      const char *new_name);

/** Delete the existing element of @a branch_id identified by @a eid.
 *
 * The delete is not explicitly recursive. However, unless otherwise
 * specified, the caller may assume that each element that has element
 * @a eid as its parent in the final state will also be deleted,
 * recursively.
 *
 * If the element @a eid is a subbranch root, then delete that subbranch
 * (recursively). The element @a eid is not the root element of @a branch_id.
 *
 * ### Options for Out-Of-Date Checking on Rebase
 *
 *   We may want to specify what kind of OOD check takes place. The
 *   following two options differ in what happens to an element that is
 *   added, on the other side, as a child of this deleted element.
 *
 *   Rebase option 1: The rebase checks for changes in the whole subtree,
 *   excluding any portions of the subtree for which an explicit delete or
 *   move-away has been issued. The check includes checking that the other
 *   side has not added any child. In other words, the deletion is
 *   interpreted as an action affecting a subtree (dynamically rooted at
 *   this element), rather than as an action affecting a single element or
 *   a fixed set of elements that was explicitly or implicitly specified
 *   by the sender.
 *
 *   To delete a mixed-rev subtree, the client sends an explicit delete for
 *   each subtree that has a different base revision from its parent.
 *
 *   Rebase option 2: The rebase checks for changes to this element only.
 *   The sender can send an explicit delete for each existing child element
 *   that it requires to be checked as well. However, there is no way for
 *   the sender to specify whether a child element added by the other side
 *   should be considered an out-of-date error or silently deleted.
 *
 *   It would also be possible to let the caller specify, per delete call,
 *   which option to use.
 *
 * @see #svn_editor3_t
 */
svn_error_t *
svn_editor3_delete(svn_editor3_t *editor,
                   const char *branch_id,
                   svn_branch_eid_t eid);

/** Register a sequence point.
 *
 * At a sequence point, elements are arranged in a tree hierarchy: each
 * element has exactly one parent element, except the root, and so on.
 * Translation between paths and element addressing is defined only at
 * a sequence point.
 *
 * The other edit operations -- add, alter, delete, etc. -- result in a
 * state that is not a sequence point.
 *
 * The beginning of an edit is a sequence point. Completion of an edit
 * (svn_editor3_complete()) creates a sequence point.
 */
svn_error_t *
svn_editor3_sequence_point(svn_editor3_t *editor);

/** Drive @a editor's #svn_editor3_cb_complete_t callback.
 *
 * Send word that the edit has been completed successfully.
 *
 * @see #svn_editor3_t
 */
svn_error_t *
svn_editor3_complete(svn_editor3_t *editor);

/** Drive @a editor's #svn_editor3_cb_abort_t callback.
 *
 * Notify that the edit transmission was not successful.
 * ### TODO @todo Shouldn't we add a reason-for-aborting argument?
 *
 * @see #svn_editor3_t
 */
svn_error_t *
svn_editor3_abort(svn_editor3_t *editor);

/** @} */


/** These function types define the callback functions a tree delta consumer
 * implements.
 *
 * Each of these "receiving" function types matches a "driving" function,
 * which has the same arguments with these differences:
 *
 * - These "receiving" functions have a @a baton argument, which is the
 *   @a editor_baton originally passed to svn_editor3_create(), as well as
 *   a @a scratch_pool argument.
 *
 * - The "driving" functions have an #svn_editor3_t* argument, in order to
 *   call the implementations of the function types defined here that are
 *   registered with the given #svn_editor3_t instance.
 *
 * Note that any remaining arguments for these function types are explained
 * in the comment for the "driving" functions. Each function type links to
 * its corresponding "driver".
 *
 * @see #svn_editor3_cb_funcs_t, #svn_editor3_t
 *
 * @defgroup svn_editor_callbacks Editor callback definitions
 * @{
 */

/** @see svn_editor3_new_eid(), #svn_editor3_t
 */
typedef svn_error_t *(*svn_editor3_cb_new_eid_t)(
  void *baton,
  svn_branch_eid_t *eid_p,
  apr_pool_t *scratch_pool);

/** @see svn_editor3_open_branch(), #svn_editor3_t
 */
typedef svn_error_t *(*svn_editor3_cb_open_branch_t)(
  void *baton,
  const char **new_branch_id_p,
  svn_branch_rev_bid_t *predecessor,
  const char *outer_branch_id,
  int outer_eid,
  int root_eid,
  apr_pool_t *result_pool,
  apr_pool_t *scratch_pool);

/** @see svn_editor3_branch(), #svn_editor3_t
 */
typedef svn_error_t *(*svn_editor3_cb_branch_t)(
  void *baton,
  const char **new_branch_id_p,
  svn_branch_rev_bid_eid_t *from,
  const char *outer_branch_id,
  int outer_eid,
  apr_pool_t *result_pool,
  apr_pool_t *scratch_pool);

/** @see svn_editor3_alter(), #svn_editor3_t
 */
typedef svn_error_t *(*svn_editor3_cb_alter_t)(
  void *baton,
  const char *branch_id,
  svn_branch_eid_t eid,
  svn_branch_eid_t new_parent_eid,
  const char *new_name,
  const svn_element_payload_t *new_payload,
  apr_pool_t *scratch_pool);

/** @see svn_editor3_copy_one(), #svn_editor3_t
 */
typedef svn_error_t *(*svn_editor3_cb_copy_one_t)(
  void *baton,
  const svn_branch_rev_bid_eid_t *src_el_rev,
  const char *branch_id,
  svn_branch_eid_t local_eid,
  svn_branch_eid_t new_parent_eid,
  const char *new_name,
  const svn_element_payload_t *new_payload,
  apr_pool_t *scratch_pool);

/** @see svn_editor3_copy_tree(), #svn_editor3_t
 */
typedef svn_error_t *(*svn_editor3_cb_copy_tree_t)(
  void *baton,
  const svn_branch_rev_bid_eid_t *src_el_rev,
  const char *branch_id,
  svn_branch_eid_t new_parent_eid,
  const char *new_name,
  apr_pool_t *scratch_pool);

/** @see svn_editor3_delete(), #svn_editor3_t
 */
typedef svn_error_t *(*svn_editor3_cb_delete_t)(
  void *baton,
  const char *branch_id,
  svn_branch_eid_t eid,
  apr_pool_t *scratch_pool);

/** @see svn_editor3_sequence_point(), #svn_editor3_t
 */
typedef svn_error_t *(*svn_editor3_cb_sequence_point_t)(
  void *baton,
  apr_pool_t *scratch_pool);

/** @see svn_editor3_complete(), #svn_editor3_t
 */
typedef svn_error_t *(*svn_editor3_cb_complete_t)(
  void *baton,
  apr_pool_t *scratch_pool);

/** @see svn_editor3_abort(), #svn_editor3_t
 */
typedef svn_error_t *(*svn_editor3_cb_abort_t)(
  void *baton,
  apr_pool_t *scratch_pool);

/** @} */


/** These functions create an editor instance so that it can be driven.
 *
 * @defgroup svn_editor3_create Editor creation
 * @{
 */

/** A set of editor callback functions.
 *
 * If a function pointer is NULL, it will not be called.
 *
 * @see svn_editor3_create(), #svn_editor3_t
 */
typedef struct svn_editor3_cb_funcs_t
{
  svn_editor3_cb_new_eid_t cb_new_eid;
  svn_editor3_cb_open_branch_t cb_open_branch;
  svn_editor3_cb_branch_t cb_branch;
  svn_editor3_cb_alter_t cb_alter;
  svn_editor3_cb_copy_one_t cb_copy_one;
  svn_editor3_cb_copy_tree_t cb_copy_tree;
  svn_editor3_cb_delete_t cb_delete;

  svn_editor3_cb_sequence_point_t cb_sequence_point;
  svn_editor3_cb_complete_t cb_complete;
  svn_editor3_cb_abort_t cb_abort;

} svn_editor3_cb_funcs_t;

/** Allocate an #svn_editor3_t instance from @a result_pool, store
 * @a *editor_funcs, @a editor_baton, @a cancel_func and @a cancel_baton
 * in the new instance and return it.
 *
 * @a cancel_func / @a cancel_baton may be NULL / NULL if not wanted.
 *
 * @see #svn_editor3_t
 */
svn_editor3_t *
svn_editor3_create(const svn_editor3_cb_funcs_t *editor_funcs,
                   void *editor_baton,
                   svn_cancel_func_t cancel_func,
                   void *cancel_baton,
                   apr_pool_t *result_pool);

/** Return an editor's private baton.
 *
 * In some cases, the baton is required outside of the callbacks. This
 * function returns the private baton for use.
 *
 * @note Not a good public API, as outside the callbacks one generally
 * doesn't know whether the editor given is the interesting editor or a
 * wrapper around it.
 *
 * @see svn_editor3_create(), #svn_editor3_t
 */
void *
svn_editor3__get_baton(const svn_editor3_t *editor);

/** @} */

/** @} */


/* ====================================================================== */

#ifdef SVN_DEBUG
/** Return an editor in @a *editor_p which will forward all calls to the
 * @a wrapped_editor while printing a diagnostic trace of the calls to
 * standard output, prefixed with 'DBG:'.
 *
 * The wrapper editor will not perform cancellation checking.
 *
 * Allocate *editor_p in RESULT_POOL.
 */
svn_error_t *
svn_editor3__get_debug_editor(svn_editor3_t **editor_p,
                              svn_editor3_t *wrapped_editor,
                              apr_pool_t *result_pool);
#endif

/** Callback to retrieve a node's kind and content.  This is
 * needed by the various editor shims in order to effect backwards
 * compatibility.
 *
 * Implementations should set @a *kind to the node kind of @a repos_relpath
 * in @a revision.
 *
 * Implementations should set @a *props to the hash of properties
 * associated with @a repos_relpath in @a revision, allocating that hash
 * and its contents in @a result_pool. Only the 'regular' props should be
 * included, not special props such as 'entry props'.
 *
 * Implementations should set @a *filename to the name of a file
 * suitable for use as a delta base for @a repos_relpath in @a revision
 * (allocating @a *filename from @a result_pool), or to @c NULL if the
 * base stream is empty.
 *
 * Any output argument may be NULL if the output is not wanted.
 *
 * @a baton is an implementation-specific closure.
 * @a repos_relpath is relative to the repository root.
 * The implementation should ensure that @a new_content, including any
 * file therein, lives at least for the life time of @a result_pool.
 * @a scratch_pool is provided for temporary allocations.
 */
typedef svn_error_t *(*svn_editor3__shim_fetch_func_t)(
  svn_node_kind_t *kind,
  apr_hash_t **props,
  svn_stringbuf_t **file_text,
  apr_hash_t **children_names,
  void *baton,
  const char *repos_relpath,
  svn_revnum_t revision,
  apr_pool_t *result_pool,
  apr_pool_t *scratch_pool
  );

/*
 */
svn_error_t *
svn_payload_fetch(svn_element_payload_t **payload_p,
                  svn_branch_txn_t *txn,
                  svn_element_branch_ref_t branch_ref,
                  svn_editor3__shim_fetch_func_t fetch_func,
                  void *fetch_baton,
                  apr_pool_t *result_pool,
                  apr_pool_t *scratch_pool);

/* An object for communicating out-of-band details between an Ev1-to-Ev3
 * shim and an Ev3-to-Ev1 shim. */
typedef struct svn_editor3__shim_connector_t svn_editor3__shim_connector_t;

/* Return an Ev3 editor in *EDITOR_P which will drive the Ev1 delta
 * editor DEDITOR/DEDIT_BATON.
 *
 * This editor buffers all the changes and then drives the Ev1 when the
 * returned editor's "close" method is called.
 *
 * This editor converts moves into copy-and-delete. It presently makes a
 * one-way (lossy) conversion.
 *
 *   TODO: Option to pass the 'move' information through as some sort of
 *   metadata so that it can be preserved in an Ev3-Ev1-Ev3 round-trip
 *   conversion.
 *     - Use 'entry-props'?
 *     - Send copy-and-delete with copy-from-rev = -1?
 *
 * This editor implements the "independent per-element changes" variant
 * of the Ev3 commit editor interface.
 *
 * Use *BRANCHING_TXN as the branching state info ...
 *
 * SHIM_CONNECTOR can be used to enable a more exact round-trip conversion
 * from an Ev1 drive to Ev3 and back to Ev1. The caller should pass the
 * returned *SHIM_CONNECTOR value to svn_delta__delta_from_ev3_for_commit().
 * SHIM_CONNECTOR may be null if not wanted.
 *
 * REPOS_ROOT_URL is the repository root URL.
 *
 * FETCH_FUNC/FETCH_BATON is a callback by which the shim may retrieve the
 * original or copy-from kind/properties/text for a path being committed.
 *
 * CANCEL_FUNC / CANCEL_BATON: The usual cancellation callback; folded
 * into the produced editor. May be NULL/NULL if not wanted.
 *
 * Allocate the new editor in RESULT_POOL, which may become large and must
 * live for the lifetime of the edit. Use SCRATCH_POOL for temporary
 * allocations.
 */
svn_error_t *
svn_editor3__ev3_from_delta_for_commit(
                        svn_editor3_t **editor_p,
                        svn_editor3__shim_connector_t **shim_connector,
                        const svn_delta_editor_t *deditor,
                        void *dedit_baton,
                        svn_branch_txn_t *branching_txn,
                        const char *repos_root_url,
                        svn_editor3__shim_fetch_func_t fetch_func,
                        void *fetch_baton,
                        svn_cancel_func_t cancel_func,
                        void *cancel_baton,
                        apr_pool_t *result_pool,
                        apr_pool_t *scratch_pool);

/* Return a delta editor in DEDITOR/DEDITOR_BATON which will drive EDITOR.
 *
 * REPOS_ROOT_URL is the repository root URL, and BASE_RELPATH is the
 * relative path within the repository of the root directory of the edit.
 * (An Ev1 edit must be rooted at a directory, not at a file.)
 *
 * FETCH_FUNC/FETCH_BATON is a callback by which the shim may retrieve the
 * original or copy-from kind/properties/text for a path being committed.
 *
 * SHIM_CONNECTOR can be used to enable a more exact round-trip conversion
 * from an Ev1 drive to Ev3 and back to Ev1. It must live for the lifetime
 * of the edit. It may be null if not wanted.
 *
 * Allocate the new editor in RESULT_POOL, which may become large and must
 * live for the lifetime of the edit. Use SCRATCH_POOL for temporary
 * allocations.
 */
svn_error_t *
svn_editor3__delta_from_ev3_for_commit(
                        const svn_delta_editor_t **deditor,
                        void **dedit_baton,
                        svn_editor3_t *editor,
                        const char *repos_root_url,
                        const char *base_relpath,
                        svn_editor3__shim_fetch_func_t fetch_func,
                        void *fetch_baton,
                        const svn_editor3__shim_connector_t *shim_connector,
                        apr_pool_t *result_pool,
                        apr_pool_t *scratch_pool);

/* Return in NEW_DEDITOR/NEW_DETIT_BATON a delta editor that wraps
 * OLD_DEDITOR/OLD_DEDIT_BATON, inserting a pair of shims that convert
 * Ev1 to Ev3 and back to Ev1.
 *
 * REPOS_ROOT_URL is the repository root URL, and BASE_RELPATH is the
 * relative path within the repository of the root directory of the edit.
 *
 * FETCH_FUNC/FETCH_BATON is a callback by which the shim may retrieve the
 * original or copy-from kind/properties/text for a path being committed.
 */
svn_error_t *
svn_editor3__insert_shims(
                        const svn_delta_editor_t **new_deditor,
                        void **new_dedit_baton,
                        const svn_delta_editor_t *old_deditor,
                        void *old_dedit_baton,
                        const char *repos_root,
                        const char *base_relpath,
                        svn_editor3__shim_fetch_func_t fetch_func,
                        void *fetch_baton,
                        apr_pool_t *result_pool,
                        apr_pool_t *scratch_pool);

/* A callback for declaring the target revision of an update or switch.
 */
typedef svn_error_t *(*svn_editor3__set_target_revision_func_t)(
  void *baton,
  svn_revnum_t target_revision,
  apr_pool_t *scratch_pool);

/* An update (or switch) editor.
 *
 * This consists of a plain Ev3 editor and the additional methods or
 * resources needed for use as an update or switch editor.
 */
typedef struct svn_update_editor3_t {
  /* The basic editor. */
  svn_editor3_t *editor;

  /* A method to communicate the target revision of the update (or switch),
   * to be called before driving the editor. It has its own baton, rather
   * than using the editor's baton, so that the editor can be replaced (by
   * a wrapper editor, typically) without having to wrap this callback. */
  svn_editor3__set_target_revision_func_t set_target_revision_func;
  void *set_target_revision_baton;
} svn_update_editor3_t;

/* Like svn_delta__ev3_from_delta_for_commit() but for an update editor.
 */
svn_error_t *
svn_editor3__ev3_from_delta_for_update(
                        svn_update_editor3_t **editor_p,
                        const svn_delta_editor_t *deditor,
                        void *dedit_baton,
                        svn_branch_txn_t *branching_txn,
                        const char *repos_root_url,
                        const char *base_repos_relpath,
                        svn_editor3__shim_fetch_func_t fetch_func,
                        void *fetch_baton,
                        svn_cancel_func_t cancel_func,
                        void *cancel_baton,
                        apr_pool_t *result_pool,
                        apr_pool_t *scratch_pool);

/* Like svn_delta__delta_from_ev3_for_commit() but for an update editor.
 */
svn_error_t *
svn_editor3__delta_from_ev3_for_update(
                        const svn_delta_editor_t **deditor,
                        void **dedit_baton,
                        svn_update_editor3_t *update_editor,
                        const char *repos_root_url,
                        const char *base_repos_relpath,
                        svn_editor3__shim_fetch_func_t fetch_func,
                        void *fetch_baton,
                        apr_pool_t *result_pool,
                        apr_pool_t *scratch_pool);

/* An Ev1 editor that drives (heuristically) a move-tracking editor.
 */
svn_error_t *
svn_branch_get_migration_editor(const svn_delta_editor_t **old_editor,
                                void **old_edit_baton,
                                svn_branch_txn_t *edit_txn,
                                svn_ra_session_t *from_session,
                                svn_revnum_t revision,
                                apr_pool_t *result_pool);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_EDITOR3E_H */
