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


/** Tree Editor
 */
typedef struct svn_editor3_t svn_editor3_t;


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

/** On serializing an edit drive over a network:
 *
 * A no-op change MUST be accepted but, in the interest of efficiency,
 * SHOULD NOT be sent.
 */

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

/** On 'flattening' with nested branching:
 *
 * Deleting a subbranch root element implies also deleting the subbranch
 * it points to, recursively.
 */


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_EDITOR3E_H */
