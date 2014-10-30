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
 * @file svn_editor3.h
 * @brief Tree editing
 *
 * @since New in 1.10.
 */

#ifndef SVN_EDITOR3_H
#define SVN_EDITOR3_H

#include "svn_editor.h"

#include <apr_pools.h>

#include "svn_types.h"
#include "svn_error.h"
#include "svn_io.h"    /* for svn_stream_t  */
#include "svn_delta.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/*
 * ### Under construction. Currently, two kinds of editor interface are
 *     declared within the same "svn_editor3_t" framework. This is for
 *     experimentation, and not intended to stay that way.
 *
 * TODO:
 *
 *   - Consider edits rooted at a sub-path of the repository. At present,
 *     the editor is designed to be rooted at the repository root.
 */

/*
 * ===================================================================
 * Versioning Model Assumed
 * ===================================================================
 *
 *   - per-node, copying-is-branching
 *   - copying is independent per node: a copy-child is not detectably
 *     "the same copy" as its parent, it's just copied at the same time
 *       => (cp ^/a@5 b; del b/c; cp ^/a/c@5 b/c) == (cp ^/a@5 b)
 *   - a node-rev's versioned state consists of:
 *        its tree linkage (parent node-branch identity, name)
 *        its content (props, text, link-target)
 *   - resurrection is supported
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
 * for tree changes with style (2) for content changes.
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
 * (2) node-id
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
 * Addressing by Node-Id
 * ---------------------
 *
 * For the purposes of addressing nodes within an edit, node-ids need not
 * be repository-wide unique ids, they only need to be known within the
 * editor. However, if the sender is to use ids that are not already known
 * to the receiver, then it must provide a mapping from ids to nodes.
 *
 * The sender assigns an id to each node including new nodes. (It is not
 * appropriate for the editor or its receiver to assign an id to an added
 * node, because the sender needs to be able to refer to that node as a
 * parent node for other nodes without creating any ordering dependency.)
 *
 * If the sender does not know the repository-wide id for a node, which is
 * especially likely for a new node, it must assign a temporary id for use
 * just within the edit. In that case, each new node or new node-branch is
 * necessarily independent. On the other hand, if the sender is able to
 * use repository-wide ids, then the possibility arises of the sender
 * asking to create a new node or a new node-branch that has the same id
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
 *      make       node-id does not exist [1]
 *      new        target parent node-branch exists (may have
 *      node         been moved/altered/del-and-resurrected)
 *                 no same-named sibling exists in target parent
 *
 *      copy       (source: no restriction)
 *      (root      target node-branch-id does not exist [1]
 *      node)      target parent node-branch exists (")
 *                 no same-named sibling exists in target parent
 *
 *      resurrect  node-branch does not exist
 *      (per       target parent node-branch exists (")
 *      node)      no same-named sibling exists in target parent
 *
 *      move       node-branch exists and is identical to base
 *      &/or       (children: no restriction)
 *      alter      target parent node-branch exists (")
 *                 no same-named sibling exists in target parent
 *
 *      del        node-branch exists and is identical to base
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
 *      make       node-id does not exist, or
 *      new          node-branch exists and is identical [1]
 *      node       target parent node-branch exists (may have
 *                   been moved/altered/del-and-resurrected)
 *                 no same-named sibling exists in target parent
 *
 *      copy       (source: no restriction)
 *      (root      target node-branch-id does not exist, or
 *      node)        node-branch exists and is identical [1]
 *                 target parent node-branch exists (")
 *                 no same-named sibling exists in target parent
 *
 *      resurrect  node-branch does not exist, or
 *      (per         node-branch exists and is identical
 *      node)      target parent node-branch exists (")
 *                 no same-named sibling exists in target parent
 *
 *      move       node-branch exists, and
 *      &/or         is identical to base or identical to target
 *      alter      (children: no restriction)
 *                 target parent node-branch exists (")
 *                 no same-named sibling exists in target parent
 *
 *      del        node-branch exists and is identical to base, or
 *      (per         node-branch is deleted
 *      node)      (parent: no restriction)
 *                 no new children on the other side
 *                   (they would end up as orphans)
 *
 * Terminology:
 *      An id. "exists" even if deleted, whereas a node-branch "exists"
 *      only when it is alive, not deleted. A node-branch is "identical"
 *      if its content and name and parent-nbid are identical.
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

/*
 * ===================================================================
 * Resurrection
 * ===================================================================
 *
 * Resurrection is needed in a branching model where element ids are the
 * key to matching up corresponding nodes between "big branches".
 *
 * Resurrection is not needed in a per-node branching model. A copy is
 * sufficient to restore a previously deleted node, as there is no need
 * to keep its old node-branch-id.
 */
/*#define SVN_EDITOR3_WITH_RESURRECTION*/


/**
 * @defgroup svn_editor The editor interface
 * @{
 */

/** Tree Editor
 */
typedef struct svn_editor3_t svn_editor3_t;

/** A location in a committed revision.
 *
 * @a rev shall not be #SVN_INVALID_REVNUM unless the interface using this
 * type specifically allows it and defines its meaning. */
typedef struct svn_editor3_peg_path_t
{
  svn_revnum_t rev;
  const char *relpath;
} svn_editor3_peg_path_t;

/* Return a duplicate of OLD, allocated in RESULT_POOL. */
svn_editor3_peg_path_t
svn_editor3_peg_path_dup(svn_editor3_peg_path_t old,
                         apr_pool_t *result_pool);

/** A reference to a node in a txn.
 *
 * @a peg gives a pegged location and @a peg.rev shall not be
 * #SVN_INVALID_REVNUM. @a relpath shall not be null. If @a relpath is
 * empty then @a peg identifies the node, otherwise @a relpath specifies
 * the one or more components that are newly created (includes children
 * of a copy). */
typedef struct svn_editor3_txn_path_t
{
  svn_editor3_peg_path_t peg;
  const char *relpath;
} svn_editor3_txn_path_t;

/* Return a duplicate of OLD, allocated in RESULT_POOL. */
svn_editor3_txn_path_t
svn_editor3_txn_path_dup(svn_editor3_txn_path_t old,
                         apr_pool_t *result_pool);

/** Element Identifier within a branch family.
 *
 * This does not contain an implied revision number or branch identifier.
 */
typedef int svn_editor3_nbid_t;

/** Versioned content of a node, excluding tree structure information.
 *
 * This specifies the content (properties, text of a file, symbolic link
 * target) directly, or by reference to an existing committed node, or
 * by a delta against such a reference content.
 *
 * ### An idea: If the sender and receiver agree, the content for a node
 *     may be specified as "null" to designate that the content is not
 *     available. For example, when a client performing a WC update has
 *     no read authorization for a given path, the server may send null
 *     content and the client may record an 'absent' WC node. (This
 *     would not make sense in a commit.)
 */
typedef struct svn_editor3_node_content_t svn_editor3_node_content_t;

/** The kind of the checksum to be used throughout the #svn_editor3_t APIs.
 */
#define SVN_EDITOR3_CHECKSUM_KIND svn_checksum_sha1


/** These functions are called by the tree delta driver to edit the target.
 *
 * @see #svn_editor3_t
 *
 * @defgroup svn_editor3_drive Driving the editor
 * @{
 */

/*
 * ===================================================================
 * Editor for Commit (incremental tree changes; path-based addressing)
 * ===================================================================
 *
 * Edit Operations:
 *
 *   - mk   kind                dir-location[1]  new-name[2]
 *   - cp   ^/from-path@rev[3]  dir-location[1]  new-name[2]
 * <SVN_EDITOR3_WITH_COPY_FROM_THIS_REV>
 *   - cp   from-path[4]        dir-location[1]  new-name[2]
 * </SVN_EDITOR3_WITH_COPY_FROM_THIS_REV>
 *   - mv   location[1]         dir-location[1]  new-name[2]
 *   - res  ^/from-path@rev[3]  dir-location[1]  new-name[2]
 *   - rm                       pegged-path[1]
 *   - put  new-content         pegged-path[1]
 *
 *   [*] 'location' means the tuple (^/peg-path @ peg-rev, created-relpath)
 *
 * Preconditions:
 *
 *   [1] this node-branch must exist in txn
 *   [2] a child with this name must not exist in the parent dir in txn
 *       (as far as sender knows; the rebase will check whether it
 *        exists and/or can be merged on receiver side)
 *   [3] this node-rev must exist in committed revision
 *   [4] this path must exist in txn
 *
 * Characteristics of this editor:
 *
 *   - Tree changes are ordered.
 * 
 *   - Content changes are unordered and independent.
 *
 *     Each node's content is set or altered at most once, and only for
 *     nodes present in the final state.
 *
 *   - There can be more than one move operation per node. Some changes
 *     require a node to be moved to a temporary location and then moved
 *     again to its final location. This could be restricted to at most
 *     two moves per node. Temporary move(s) could be required to use a
 *     defined temporary name space.
 * 
 *     There is not (yet) a defined canonical sequence of editor operations
 *     to represent an arbitrary change.
 *
 *   - The sender needs a name space it can use for temporary paths.
 *
 *     If the receiver will be applying changes to a state that may not
 *     exactly match the sender's base state, such as a commit editor,
 *     it is necessary that the temporary paths will not clash with other
 *     paths present on the receiving side. It may also be useful for the
 *     receiver to be aware of the temporary name space so that it can
 *     optimise temporary moves differently from other moves.
 *
 *   - All tree changes MAY be sent before all content changes.
 *
 *   - Copying or deleting a subtree is an O(1) cheap operation.
 *
 *   - The commit rebase MAY (but need not) merge a repository-side move
 *     with incoming edits inside the moved subtree, and vice-versa.
 *
 *   ### In order to expand the scope of this editor to situations like
 *       update/switch, where the receiver doesn't have the repository
 *       to refer to, Can we add a full-traversal kind of copy?
 *       Is that merely a matter of driving the same API in a different
 *       way ("let the copy operation mean non-recursive copy")? Or is
 *       it totally out of scope? (To support WC update we need other
 *       changes too, not just this.)
 *
 * Notes on Paths:
 *
 *   - Each node in the txn was either pre-existing or was created within
 *     the txn. A pre-existing node may be moved by the rebase-on-commit
 *     and/or by operations within the txn, whereas a created node is
 *     required to remain at the same path where it was created, relative
 *     to its pathwise-nearest pre-existing node.
 *
 *     We refer to a node in a txn by means of a pegged path and a created
 *     relative path:
 *
 *       (^/peg-path @ peg-rev, created-relpath).
 *
 *     The "path @ rev" part identifies the nearest pre-existing node-branch,
 *     by reference to a path in a committed revision which is to be
 *     traced forward to the current transaction. The Out-Of-Date
 *     check notes whether the specified node-branch still exists in
 *     the txn, and, if applicable, that it hasn't been modified.
 *
 *     Each component of the "created-relpath" refers to a node that was
 *     created within the txn (with "mk" or "cp", but not "res"). It MUST
 *     NOT refer to a node-branch that already existed before the edit
 *     began. The "created-relpath" may be empty.
 *
 *   - Ev1 referred to each node in a txn by a nesting of "open" (for a
 *     pre-existing node) and "add" (for a created node) operations.
 *
 * Notes on Copying:
 *
 *   - Copy from path-in-txn is required iff we want to support copying
 *     from "this revision". If we don't then the source is necessarily
 *     a pre-existing node and so can be referenced by ^/path@rev.
 *
 *   - There is no provision for making a non-tracked copy of a subtree
 *     in a single operation.
 *
 * Notes on Moving:
 *
 *   - There is no operation to move a subtree whose root node was created
 *     in this txn, merely because it is not necessary. (A node created by
 *     "mk" can always be created in the required location. A subtree of a
 *     copy can be moved by deleting it and making a new copy from the
 *     corresponding subtree of the original copy root, as there is no
 *     distinction between the first copy and the second copy.)
 *
 */

/** Make a single new node ("versioned object") with empty content.
 * 
 * Set the node kind to @a new_kind. Create the node in the parent
 * directory node-branch specified by @a parent_loc. Set the new node's
 * name to @a new_name.
 *
 * The new node is not related by node identity to any other existing node
 * nor to any other node created by another "mk" operation.
 *
 * @node "put" is optional for a node made by "mk".
 * ### For use as an 'update' editor, maybe 'mk' without 'put' should
 *     make an 'absent' node.
 *
 * @see #svn_editor3_t
 */
svn_error_t *
svn_editor3_mk(svn_editor3_t *editor,
               svn_node_kind_t new_kind,
               svn_editor3_txn_path_t parent_loc,
               const char *new_name);

/** Create a copy of a subtree.
 * 
 * The source subtree is found at @a from_loc. Create the root node of
 * the new subtree in the parent directory node-branch specified by
 * @a parent_loc with the name @a new_name.
 *
 * Each node in the target subtree has a "copied from" relationship with
 * the node with the corresponding path in the source subtree.
 *
 * <SVN_EDITOR3_WITH_COPY_FROM_THIS_REV>
 * If @a from_loc has a non-empty "created relpath", then it refers to the
 * current state in the txn.
 *   ### Or use some other indication, such as (from_loc.rev == -1)?
 * Make a copy of the current state of that subtree in the txn. When
 * committed, the copy will have a "copied from" reference to the
 * committed revision.
 *
 * Modifying the source subtree later within this edit will not affect
 * the target's tree structure and content, but will modify the copy
 * relationships of the target subtree accordingly. Moving a source
 * node (directly or as a child) will update the corresponding target's
 * "copied from" reference to follow it.
 *   ### Except if we move a source node into the target subtree, ...?
 * Deleting a source node will
 * remove the corresponding target node's "copied from" reference.
 * </SVN_EDITOR3_WITH_COPY_FROM_THIS_REV>
 *
 * The content of each node in the target subtree is by default the
 * content of the node at the corresponding path within the source
 * subtree, and MAY be changed by a "put" operation.
 *
 * @see #svn_editor3_t
 */
svn_error_t *
svn_editor3_cp(svn_editor3_t *editor,
#ifdef SVN_EDITOR3_WITH_COPY_FROM_THIS_REV
               svn_editor3_txn_path_t from_loc,
#else
               svn_editor3_peg_path_t from_loc,
#endif
               svn_editor3_txn_path_t parent_loc,
               const char *new_name);

/** Move a subtree to a new parent directory and/or a new name.
 *
 * The root node of the source subtree is specified by @a from_loc
 * which refers to a committed revision. This node must exist in the
 * current txn, but may have been moved and/or modified. (This method
 * cannot be used to move a node that has been created within the edit.)
 *
 * Move the root node of the subtree to the parent directory node-branch
 * specified by @a new_parent_loc and change its name to @a new_name.
 *
 * Each node in the target subtree remains the same node-branch as
 * the node with the corresponding path in the source subtree.
 *
 * Any modifications that have already been made within the subtree are
 * preserved.
 *
 * @see #svn_editor3_t
 */
svn_error_t *
svn_editor3_mv(svn_editor3_t *editor,
               svn_editor3_peg_path_t from_loc,
               svn_editor3_txn_path_t new_parent_loc,
               const char *new_name);

#ifdef SVN_EDITOR3_WITH_RESURRECTION
/** Resurrect a previously deleted node-branch.
 *
 * Resurrect the node-branch that previously existed at @a from_loc,
 * a location in a committed revision. Put the resurrected node at
 * @a parent_loc, @a new_name.
 *
 * The content of the resurrected node is, by default, the content of the
 * source node at @a from_loc. The content MAY be changed by a "put".
 *
 * The specified source is any location at which this node-branch existed,
 * not necessarily at its youngest revision nor even within its most
 * recent period of existence.
 *
 * ### The source node-branch MUST NOT exist in the txn. If the source
 *     node-branch exists in the txn-base, resurrection would be
 *     equivalent to reverting a local delete in the txn; the sender
 *     SHOULD NOT do this. [### Why not? Just because it seems like
 *     unnecessary flexibility.]
 *
 * ### Can we have a recursive resurrect operation? What should it do
 *     if a child node is still alive (moved or already resurrected)?
 *
 * @see #svn_editor3_t
 */
svn_error_t *
svn_editor3_res(svn_editor3_t *editor,
                svn_editor3_peg_path_t from_loc,
                svn_editor3_txn_path_t parent_loc,
                const char *new_name);
#endif

/** Remove the existing node-branch identified by @a loc and, recursively,
 * all nodes that are currently its children in the txn.
 *
 * @note This does not delete nodes that used to be children of the specified
 * node-branch that have since been moved away.
 *
 * @note Each node-branch to be removed, that is each node-branch currently
 * at or below @a loc, MAY be a child of a copy but otherwise SHOULD NOT
 * have been created or modified in this edit. Other node-branches MAY have
 * previously existed under @a loc and been deleted or moved away.
 *
 * @see #svn_editor3_t
 */
svn_error_t *
svn_editor3_rm(svn_editor3_t *editor,
               svn_editor3_txn_path_t loc);

/** Set the content of the node-branch identified by @a loc.
 *
 * Set the content to @a new_content. (The new content may be described
 * in terms of a delta against another node's content.)
 *
 * The caller owns @a new_content, including any file therein, and may
 * destroy it after this call returns.
 *
 * @note "put" MAY be sent for any node that exists in the final state.
 * "put" SHOULD NOT be sent for a node that will not exist in the final
 * state. "put" SHOULD NOT be sent more than once for any node-branch.
 * "put" MUST provide the right kind of content to match the node kind; it
 * cannot change the kind of a node nor convert the content to match the
 * node kind.
 *
 * @see #svn_editor3_t
 */
svn_error_t *
svn_editor3_put(svn_editor3_t *editor,
                svn_editor3_txn_path_t loc,
                const svn_editor3_node_content_t *new_content);


/*
 * ========================================================================
 * Editor for Commit (independent per-node changes; node-id addressing)
 * ========================================================================
 *
 * Scope of Edit:
 *
 * The edit may include changes to one or more branches.
 *
 * Edit Operations:
 *
 *   operations on elements of "this" branch
 *   - add       kind      new-parent-nb[2] new-name new-content  ->  new-nb
 *   - copy-one  nb@rev[3] new-parent-nb[2] new-name new-content  ->  new-nb
 *   - copy-tree nb@rev[3] new-parent-nb[2] new-name              ->  new-nb
 *   - delete    nb[1]   since-rev
 *   - alter     nb[1,2] since-rev new-parent-nb[2] new-name new-content
 *
 *   operations on sub-branches
 *   - branch
 *   - branchify
 *   - unbranchify ("dissolve"?)
 *
 * Preconditions:
 *
 *   [1] node-branch must exist in initial state
 *   [2] node-branch must exist in final state
 *   [3] source must exist in committed revision or txn final state
 *
 * Characteristics of this editor:
 *
 *   - Tree structure is partitioned among the nodes, in such a way that
 *     each of the most important concepts such as "move", "copy",
 *     "create" and "delete" is modeled as a single change to a single
 *     node. The name and the identity of its parent directory node are
 *     considered to be attributes of that node, alongside its content.
 *
 *   - Changes are independent and unordered. The change to one node is
 *     independent of the change to any other node, except for the
 *     requirement that the final state forms a valid (path-wise) tree
 *     hierarchy. A valid tree hierarchy is NOT required in any
 *     intermediate state after each change or after a subset of changes.
 *
 *   - Copies can be made in two ways: a copy of a single node can have
 *     its content changed and its children may be arbitrarily arranged,
 *     or a "cheap" O(1) copy of a subtree which cannot be edited.
 *
 *   - Deleting a subtree is O(1) cheap // or not. ### To be decided.
 *
 *   - The commit rebase MAY (but need not) merge a repository-side move
 *     with incoming edits inside the moved subtree, and vice-versa.
 *
 * Notes on Copying:
 *
 *   - copy_one and copy_tree are separate. In this model it doesn't
 *     make sense to describe a copy-and-modify by means of generating
 *     a full copy (with ids, at least implicitly, for each node) and
 *     then potentially "deleting" some of the generated child nodes.
 *     Instead, each node has to be specified in its final state or not
 *     at all. Tree-copy therefore generates an immutable copy, while
 *     single-node copy supports arbitrary copy-and-modify operations,
 *     and tree-copy can be used for any unmodified subtrees therein.
 *     There is no need to reference the root node of a tree-copy again
 *     within the same edit, and so no id is provided.
 */

/** Create a new element (versioned object) of kind @a new_kind.
 * 
 * Assign the new node a new element id; store this in @a *eid_p if
 * @a eid_p is not null.
 *
 * Set the node's parent and name to @a new_parent_nbid and @a new_name.
 *
 * Set the content to @a new_content.
 *
 * @see #svn_editor3_t
 */
svn_error_t *
svn_editor3_add(svn_editor3_t *editor,
                svn_editor3_nbid_t *eid,
                svn_node_kind_t new_kind,
                svn_editor3_nbid_t new_parent_eid,
                const char *new_name,
                const svn_editor3_node_content_t *new_content);

/* Make the existing element @a eid exist in this branch, assuming it was
 * previously not existing in this branch.
 *
 * This can be used to "branch" the element from another branch during a
 * merge, or to resurrect it.
 *
 * Set the node's parent and name to @a new_parent_nbid and @a new_name.
 *
 * Set the content to @a new_content.
 *
 * @see #svn_editor3_t
 *
 * ### Need to specify where the underlying FS node is to be "copied" from?
 */
svn_error_t *
svn_editor3_instantiate(svn_editor3_t *editor,
                        svn_editor3_nbid_t eid,
                        svn_editor3_nbid_t new_parent_eid,
                        const char *new_name,
                        const svn_editor3_node_content_t *new_content);

/** Create a new node-branch that is copied (branched) from a pre-existing
 * <SVN_EDITOR3_WITH_COPY_FROM_THIS_REV> or newly created </>
 * node-branch, with the same or different content.
 *
 * Assign the target node a locally unique node-branch-id, @a local_nbid,
 * with which it can be referenced within this edit.
 *
 * Copy from the source node at @a src_revision, @a src_nbid.
 * <SVN_EDITOR3_WITH_COPY_FROM_THIS_REV>
 * If @a src_revision is #SVN_INVALID_REVNUM, it means copy from within
 * the new revision being described.
 *   ### See note on copy_tree().
 * </SVN_EDITOR3_WITH_COPY_FROM_THIS_REV>
 *
 * Set the target node's parent and name to @a new_parent_nbid and
 * @a new_name. Set the target node's content to @a new_content, or make
 * it the same as the source if @a new_content is null.
 *
 * @note This copy is not recursive. Children may be copied separately if
 * required.
 *
 * @note The @a local_nbid has meaning only within this edit. The server
 * must create a new node, and MUST NOT match local_nbid with any other
 * node that may already exist or that may be created by another edit.
 *
 * @see svn_editor3_copy_tree(), #svn_editor3_t
 */
svn_error_t *
svn_editor3_copy_one(svn_editor3_t *editor,
                     svn_editor3_nbid_t local_nbid,
                     svn_revnum_t src_revision,
                     svn_editor3_nbid_t src_nbid,
                     svn_editor3_nbid_t new_parent_nbid,
                     const char *new_name,
                     const svn_editor3_node_content_t *new_content);

/** Create a copy of a pre-existing
 * <SVN_EDITOR3_WITH_COPY_FROM_THIS_REV> or newly created </>
 * subtree, with the same content and tree structure.
 *
 * Each node in the source subtree will be copied (branched) to the same
 * relative path within the target subtree. The node-branches created by
 * this copy cannot be modified or addressed within this edit.
 *
 * Set the target root node's parent and name to @a new_parent_nbid and
 * @a new_name.
 *
 * Copy from the source node at @a src_revision, @a src_nbid.
 * <SVN_EDITOR3_WITH_COPY_FROM_THIS_REV>
 * If @a src_revision is #SVN_INVALID_REVNUM, it means copy from within
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
 * The content of each node copied from an existing revision is the content
 * of the source node. The content of each node copied from this revision
 * is the FINAL content of the source node as committed.
 *
 * @see svn_editor3_copy_one(), #svn_editor3_t
 */
svn_error_t *
svn_editor3_copy_tree(svn_editor3_t *editor,
                      svn_revnum_t src_revision,
                      svn_editor3_nbid_t src_nbid,
                      svn_editor3_nbid_t new_parent_nbid,
                      const char *new_name);

/** Delete the existing node-branch identified by @a nbid.
 *
 * @a since_rev specifies the base revision on which this deletion was
 * performed: the server can consider the change "out of date" if a commit
 * since then has changed or deleted this node-branch.
 *
 * ###  @note The delete is not recursive. Each child node must be
 *      explicitly deleted or moved away. (In this case, the rebase does
 *      not have to check explicitly whether the other side modified a
 *      child. That will be checked either when we try to delete or move
 *      the child, or, for a child added on the other side, when we check
 *      for orphaned nodes in the final state.)
 *   OR @note The delete is implicitly recursive: each child node that
 *      is not otherwise moved to a new parent will be deleted as well.
 *      (The rebase should check for changes in the whole subtree,
 *      including checking that the other side has not added any child.)
 *      ### Does this make sense when deleting a mixed-rev tree? Sender
 *          asks to delete a "complete" tree, as if single-rev; this
 *          implies to the receiver what set of nodes is involved. How
 *          would the WC know whether its mixed-rev tree is "complete"?
 *          Would we need a non-recursive delete as well?
 *      ### Deletes nested branches.
 *
 * @see #svn_editor3_t
 */
svn_error_t *
svn_editor3_delete(svn_editor3_t *editor,
                   svn_revnum_t since_rev,
                   svn_editor3_nbid_t nbid);

/** Alter the tree position and/or contents of the node-branch identified
 * by @a nbid.
 * <SVN_EDITOR3_WITH_RESURRECTION> ### or resurrect it? </>
 *
 * @a since_rev specifies the base revision on which this edit was
 * performed: the server can consider the change "out of date" if a commit
 * since then has changed or deleted this node-branch.
 *
 * Set the node's parent and name to @a new_parent_nbid and @a new_name.
 *
 * Set the content to @a new_content, or if null then leave the content
 * unchanged.
 *
 * A no-op change MUST be accepted but, in the interest of efficiency,
 * SHOULD NOT be sent.
 *
 * @see #svn_editor3_t
 */
svn_error_t *
svn_editor3_alter(svn_editor3_t *editor,
                  svn_revnum_t since_rev,
                  svn_editor3_nbid_t nbid,
                  svn_editor3_nbid_t new_parent_nbid,
                  const char *new_name,
                  const svn_editor3_node_content_t *new_content);

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

/** @see svn_editor3_mk(), #svn_editor3_t
 */
typedef svn_error_t *(*svn_editor3_cb_mk_t)(
  void *baton,
  svn_node_kind_t new_kind,
  svn_editor3_txn_path_t parent_loc,
  const char *new_name,
  apr_pool_t *scratch_pool);

/** @see svn_editor3_cp(), #svn_editor3_t
 */
typedef svn_error_t *(*svn_editor3_cb_cp_t)(
  void *baton,
#ifdef SVN_EDITOR3_WITH_COPY_FROM_THIS_REV
  svn_editor3_txn_path_t from_loc,
#else
  svn_editor3_peg_path_t from_loc,
#endif
  svn_editor3_txn_path_t parent_loc,
  const char *new_name,
  apr_pool_t *scratch_pool);

/** @see svn_editor3_mv(), #svn_editor3_t
 */
typedef svn_error_t *(*svn_editor3_cb_mv_t)(
  void *baton,
  svn_editor3_peg_path_t from_loc,
  svn_editor3_txn_path_t new_parent_loc,
  const char *new_name,
  apr_pool_t *scratch_pool);

#ifdef SVN_EDITOR3_WITH_RESURRECTION
/** @see svn_editor3_res(), #svn_editor3_t
 */
typedef svn_error_t *(*svn_editor3_cb_res_t)(
  void *baton,
  svn_editor3_peg_path_t from_loc,
  svn_editor3_txn_path_t parent_loc,
  const char *new_name,
  apr_pool_t *scratch_pool);
#endif

/** @see svn_editor3_rm(), #svn_editor3_t
 */
typedef svn_error_t *(*svn_editor3_cb_rm_t)(
  void *baton,
  svn_editor3_txn_path_t loc,
  apr_pool_t *scratch_pool);

/** @see svn_editor3_put(), #svn_editor3_t
 */
typedef svn_error_t *(*svn_editor3_cb_put_t)(
  void *baton,
  svn_editor3_txn_path_t loc,
  const svn_editor3_node_content_t *new_content,
  apr_pool_t *scratch_pool);

/** @see svn_editor3_add(), #svn_editor3_t
 */
typedef svn_error_t *(*svn_editor3_cb_add_t)(
  void *baton,
  svn_editor3_nbid_t *eid,
  svn_node_kind_t new_kind,
  svn_editor3_nbid_t new_parent_eid,
  const char *new_name,
  const svn_editor3_node_content_t *new_content,
  apr_pool_t *scratch_pool);

/** @see svn_editor3_instantiate(), #svn_editor3_t
 */
typedef svn_error_t *(*svn_editor3_cb_instantiate_t)(
  void *baton,
  svn_editor3_nbid_t eid,
  svn_editor3_nbid_t new_parent_eid,
  const char *new_name,
  const svn_editor3_node_content_t *new_content,
  apr_pool_t *scratch_pool);

/** @see svn_editor3_copy_one(), #svn_editor3_t
 */
typedef svn_error_t *(*svn_editor3_cb_copy_one_t)(
  void *baton,
  svn_editor3_nbid_t local_nbid,
  svn_revnum_t src_revision,
  svn_editor3_nbid_t src_nbid,
  svn_editor3_nbid_t new_parent_nbid,
  const char *new_name,
  const svn_editor3_node_content_t *new_content,
  apr_pool_t *scratch_pool);

/** @see svn_editor3_copy_tree(), #svn_editor3_t
 */
typedef svn_error_t *(*svn_editor3_cb_copy_tree_t)(
  void *baton,
  svn_revnum_t src_revision,
  svn_editor3_nbid_t src_nbid,
  svn_editor3_nbid_t new_parent_nbid,
  const char *new_name,
  apr_pool_t *scratch_pool);

/** @see svn_editor3_delete(), #svn_editor3_t
 */
typedef svn_error_t *(*svn_editor3_cb_delete_t)(
  void *baton,
  svn_revnum_t since_rev,
  svn_editor3_nbid_t nbid,
  apr_pool_t *scratch_pool);

/** @see svn_editor3_alter(), #svn_editor3_t
 */
typedef svn_error_t *(*svn_editor3_cb_alter_t)(
  void *baton,
  svn_revnum_t since_rev,
  svn_editor3_nbid_t nbid,
  svn_editor3_nbid_t new_parent_nbid,
  const char *new_name,
  const svn_editor3_node_content_t *new_content,
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
  svn_editor3_cb_mk_t cb_mk;
  svn_editor3_cb_cp_t cb_cp;
  svn_editor3_cb_mv_t cb_mv;
#ifdef SVN_EDITOR3_WITH_RESURRECTION
  svn_editor3_cb_res_t cb_res;
#endif
  svn_editor3_cb_rm_t cb_rm;
  svn_editor3_cb_put_t cb_put;

  svn_editor3_cb_add_t cb_add;
  svn_editor3_cb_instantiate_t cb_instantiate;
  svn_editor3_cb_copy_one_t cb_copy_one;
  svn_editor3_cb_copy_tree_t cb_copy_tree;
  svn_editor3_cb_delete_t cb_delete;
  svn_editor3_cb_alter_t cb_alter;

  svn_editor3_cb_complete_t cb_complete;
  svn_editor3_cb_abort_t cb_abort;

} svn_editor3_cb_funcs_t;

/** Allocate an #svn_editor3_t instance from @a result_pool, store
 * @a *editor_funcs, @a editor_baton, @a cancel_func and @a cancel_baton
 * in the new instance and return it in @a *editor.
 *
 * @a cancel_func / @a cancel_baton may be NULL / NULL if not wanted.
 *
 * @a scratch_pool is used for temporary allocations (if any). Note that
 * this is NOT the same @a scratch_pool that is passed to callback functions.
 *
 * @see #svn_editor3_t
 */
svn_error_t *
svn_editor3_create(svn_editor3_t **editor,
                   const svn_editor3_cb_funcs_t *editor_funcs,
                   void *editor_baton,
                   svn_cancel_func_t cancel_func,
                   void *cancel_baton,
                   apr_pool_t *result_pool,
                   apr_pool_t *scratch_pool);

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


/*
 * ========================================================================
 * Node Content Interface
 * ========================================================================
 *
 * @defgroup svn_editor3_node_content Node content interface
 * @{
 */

/** Versioned content of a node, excluding tree structure information.
 *
 * Content is described by setting fields in one of the following ways.
 * Other fields SHOULD be null (or equivalent).
 *
 *   by reference:  (kind=unknown, ref)
 *   dir:           (kind=dir, props)
 *   file:          (kind=file, props, text)
 *   symlink:       (kind=symlink, props, target)
 *
 * ### Idea for the future: Specify content as an (optional) reference
 *     plus (optional) overrides or deltas against the reference?
 */
struct svn_editor3_node_content_t
{
  /* The node kind for this content: dir, file, symlink, or unknown. */
  svn_node_kind_t kind;

  /* Reference existing, committed content at REF (for kind=unknown).
   * The 'null' value is (SVN_INVALID_REVNUM, NULL). */
  svn_editor3_peg_path_t ref;

  /* Properties (for kind != unknown).
   * Maps (const char *) name -> (svn_string_t) value.
   * An empty hash means no properties. (SHOULD NOT be NULL.)
   * ### Presently NULL means 'no change' in some contexts. */
  apr_hash_t *props;

  /* File text (for kind=file; otherwise SHOULD be NULL). */
  svn_stringbuf_t *text;

  /* Symlink target (for kind=symlink; otherwise SHOULD be NULL). */
  const char *target;

};

/** Duplicate a node-content @a old into @a result_pool.
 */
svn_editor3_node_content_t *
svn_editor3_node_content_dup(const svn_editor3_node_content_t *old,
                             apr_pool_t *result_pool);

/* Return true iff the content of LEFT is identical to that of RIGHT.
 * References are not supported. Node kind 'unknown' is not supported.
 */
svn_boolean_t
svn_editor3_node_content_equal(const svn_editor3_node_content_t *left,
                               const svn_editor3_node_content_t *right,
                               apr_pool_t *scratch_pool);

/** Create a new node-content object by reference to an existing node.
 *
 * Set the node kind to 'unknown'.
 *
 * Allocate the result in @a result_pool, but only shallow-copy the
 * given arguments.
 */
svn_editor3_node_content_t *
svn_editor3_node_content_create_ref(svn_editor3_peg_path_t ref,
                                    apr_pool_t *result_pool);

/** Create a new node-content object for a directory node.
 *
 * Allocate the result in @a result_pool, but only shallow-copy the
 * given arguments.
 */
svn_editor3_node_content_t *
svn_editor3_node_content_create_dir(apr_hash_t *props,
                                    apr_pool_t *result_pool);

/** Create a new node-content object for a file node.
 *
 * Allocate the result in @a result_pool, but only shallow-copy the
 * given arguments.
 */
svn_editor3_node_content_t *
svn_editor3_node_content_create_file(apr_hash_t *props,
                                     svn_stringbuf_t *text,
                                     apr_pool_t *result_pool);

/** Create a new node-content object for a symlink node.
 *
 * Allocate the result in @a result_pool, but only shallow-copy the
 * given arguments.
 */
svn_editor3_node_content_t *
svn_editor3_node_content_create_symlink(apr_hash_t *props,
                                        const char *target,
                                        apr_pool_t *result_pool);

/** @} */

/** @} */


/* ====================================================================== */

/* ### */
#define SVN_ERR_BRANCHING 123456

struct svn_branch_instance_t;

/* Per-repository branching info.
 */
typedef struct svn_branch_repos_t
{
  /* The range of family ids assigned within this repos (starts at 0). */
  int next_fid;

  /* Array of (svn_branch_revision_info_t *), indexed by revision number. */
  apr_array_header_t *rev_roots;

  /* The pool in which this object lives. */
  apr_pool_t *pool;
} svn_branch_repos_t;

/* Create a new branching metadata object */
svn_branch_repos_t *
svn_branch_repos_create(apr_pool_t *result_pool);

/* Info about the branching in a specific revision (committed or uncommitted) */
typedef struct svn_branch_revision_root_t
{
  /* The repository in which this revision exists. */
  svn_branch_repos_t *repos;

  /* If committed, the revision number; else SVN_INVALID_REVNUM. */
  svn_revnum_t rev;

  /* The root branch instance. */
  struct svn_branch_instance_t *root_branch;

} svn_branch_revision_root_t;

/* Create a new branching revision-info object */
svn_branch_revision_root_t *
svn_branch_revision_root_create(svn_branch_repos_t *repos,
                                svn_revnum_t rev,
                                struct svn_branch_instance_t *root_branch,
                                apr_pool_t *result_pool);

/* A branch family.
 * ### Most of this is not per-revision data. Move it out of revision-root?
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

  /* The branch instances in this family. */
  /* ### This is per-revision data. Move to svn_branch_revision_root_t? */
  apr_array_header_t *branch_instances;

  /* The range of branch ids assigned within this family. */
  int first_bid, next_bid;

  /* The range of element ids assigned within this family. */
  int first_eid, next_eid;

  /* The immediate sub-families of this family. */
  apr_array_header_t *sub_families;

  /* The pool in which this object lives. */
  apr_pool_t *pool;
} svn_branch_family_t;

/* Create a new branch family object */
svn_branch_family_t *
svn_branch_family_create(svn_branch_repos_t *repos,
                         int fid,
                         int first_bid,
                         int next_bid,
                         int first_eid,
                         int next_eid,
                         apr_pool_t *result_pool);

/* A branch.
 *
 * A branch sibling object describes the characteristics of a branch
 * in a given family with a given BID. This sibling is common to each
 * branch that has this same family and BID: there can be one such instance
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
typedef struct svn_branch_sibling_t
{
  /* --- Identity of this object --- */

  /* The family of which this branch is a member. */
  svn_branch_family_t *family;

  /* The BID of this branch within its family. */
  int bid;

  /* The EID, within the outer family, of the branch root element. */
  /*int outer_family_eid_of_branch_root;*/

  /* --- Contents of this object --- */

  /* The EID within its family of its pathwise root element. */
  int root_eid;
} svn_branch_sibling_t;

/* Create a new branch sibling object */
svn_branch_sibling_t *
svn_branch_sibling_create(svn_branch_family_t *family,
                          int bid,
                          int root_eid,
                          apr_pool_t *result_pool);

/* A branch instance.
 *
 * A branch instance object describes one branch in this family. (There is
 * one instance of this branch within each branch of its outer families.)
 */
typedef struct svn_branch_instance_t
{
  /* --- Identity of this object --- */

  /* The branch-sibling class to which this branch belongs */
  svn_branch_sibling_t *sibling_defn;

  /* The revision to which this branch-revision-instance belongs */
  svn_branch_revision_root_t *rev_root;

  /* The branch (instance?), within the outer family, that contains the
     root element of this branch. */
  /*svn_branch_instance_t *outer_family_branch_instance;*/

  /* --- Contents of this object --- */

  /* EID -> svn_branch_el_rev_content_t mapping. */
  apr_hash_t *e_map;

  /* ### This need not be constant if a parent branch is updated, so should
   * be calculated on demand not stored here. */
  const char *branch_root_rrpath;

} svn_branch_instance_t;

/* Create a new branch instance object */
svn_branch_instance_t *
svn_branch_instance_create(svn_branch_sibling_t *branch_sibling,
                           svn_branch_revision_root_t *rev_root,
                           const char *branch_root_rrpath,
                           apr_pool_t *result_pool);

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
typedef struct svn_branch_el_rev_id_t
{
  /* The branch-instance that applies to REV. */
  svn_branch_instance_t *branch;
  /* Element. */
  int eid;
  /* Revision. SVN_INVALID_REVNUM means 'in this transaction', not 'head'.
     ### Do we need this if BRANCH refers to a particular branch-revision? */
  svn_revnum_t rev;

} svn_branch_el_rev_id_t;

/* The content (parent, name and node-content) of an element-revision.
 * In other words, an el-rev node in a (mixed-rev) directory-tree.
 */
typedef struct svn_branch_el_rev_content_t
{
  /* eid of the parent element, or -1 if this is the root element */
  int parent_eid;
  /* struct svn_branch_element_t *parent_element; */
  /* node name, or "" for root node; never null */
  const char *name;
  /* content (kind, props, text, ...) */
  svn_editor3_node_content_t *content;

} svn_branch_el_rev_content_t;

/* Return a new content object constructed with deep copies of PARENT_EID,
 * NAME and NODE_CONTENT, allocated in RESULT_POOL.
 */
svn_branch_el_rev_content_t *
svn_branch_el_rev_content_create(svn_editor3_nbid_t parent_eid,
                                 const char *name,
                                 const svn_editor3_node_content_t *node_content,
                                 apr_pool_t *result_pool);

/* Return a new content object constructed with a deep copy of OLD,
 * allocated in RESULT_POOL.
 */
svn_branch_el_rev_content_t *
svn_branch_el_rev_content_dup(const svn_branch_el_rev_content_t *old,
                              apr_pool_t *result_pool);

/* Return TRUE iff CONTENT_LEFT is the same as CONTENT_RIGHT. */
svn_boolean_t
svn_branch_el_rev_content_equal(const svn_branch_el_rev_content_t *content_left,
                                const svn_branch_el_rev_content_t *content_right,
                                apr_pool_t *scratch_pool);


/* Return the root repos-relpath of BRANCH.
 *
 * ### A branch root's rrpath can change during the edit.
 */
const char *
svn_branch_get_root_rrpath(const svn_branch_instance_t *branch);

/* Return the repos-relpath of element EID in BRANCH.
 *
 * ### A branch element's rrpath can change during the edit.
 */
const char *
svn_branch_get_rrpath_by_eid(const svn_branch_instance_t *branch,
                             int eid,
                             apr_pool_t *result_pool);

/* Find the (deepest) branch in the state being edited by EDITOR, of which
 * the path RRPATH is either the root path or a normal, non-sub-branch
 * path. An element need not exist at RRPATH.
 *
 * Set *BRANCH_P to the deepest branch that contains the path RRPATH.
 *
 * If EID_P is not null then set *EID_P to the element id of RRPATH in
 * *BRANCH_P, or to -1 if no element exists at RRPATH in that branch.
 */
void
svn_editor3_find_branch_element_by_rrpath(svn_branch_instance_t **branch_p,
                                          int *eid_p,
                                          svn_editor3_t *editor,
                                          const char *rrpath,
                                          apr_pool_t *scratch_pool);

/* Find the deepest branch in the repository of which RRPATH @ REVNUM is
 * either the root element or a normal, non-sub-branch element.
 *
 * Return the location of the element at RRPATH in that branch, or with
 * EID=-1 if no element exists there.
 *
 * REVNUM must be the revision number of a committed revision.
 *
 * The result will never be NULL, as every path is within at least the root
 * branch.
 */
svn_error_t *
svn_editor3_find_el_rev_by_path_rev(svn_branch_el_rev_id_t **el_rev_p,
                                   svn_editor3_t *editor,
                                   const char *rrpath,
                                   svn_revnum_t revnum,
                                   apr_pool_t *result_pool,
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

/* Return the branch family of the main branch of @a editor. */
svn_branch_family_t *
svn_branch_get_family(svn_editor3_t *editor);

/* Return (left, right) pairs of element content that differ between
 * subtrees LEFT and RIGHT.

 * Set *DIFF_P to a hash of (eid -> (svn_branch_el_rev_content_t *)[2]).
 */
svn_error_t *
svn_branch_subtree_differences(apr_hash_t **diff_p,
                               svn_editor3_t *editor,
                               const svn_branch_el_rev_id_t *left,
                               const svn_branch_el_rev_id_t *right,
                               apr_pool_t *result_pool,
                               apr_pool_t *scratch_pool);

/* Branch the subtree of FROM_BRANCH found at FROM_EID, to create
 * a new branch at TO_OUTER_BRANCH:TO_OUTER_PARENT_EID:NEW_NAME.
 *
 * FROM_BRANCH must be an immediate sub-branch of TO_OUTER_BRANCH.
 */
svn_error_t *
svn_branch_branch(svn_editor3_t *editor,
                  svn_branch_instance_t *from_branch,
                  int from_eid,
                  svn_branch_instance_t *to_outer_branch,
                  svn_editor3_nbid_t to_outer_parent_eid,
                  const char *new_name,
                  apr_pool_t *scratch_pool);

/* Change the existing simple sub-tree at OUTER_EID into a sub-branch in a
 * new branch family.
 *
 * ### TODO: Also we must (in order to maintain correctness) branchify
 *     the corresponding subtrees in all other branches in this family.
 *
 * TODO: Allow adding to an existing family, by specifying a mapping.
 *
 *   create a new family
 *   create a new branch-def and branch-instance
 *   for each node in subtree:
 *     ?[unassign eid in outer branch (except root node)]
 *     assign a new eid in inner branch
 */
/* The element-based version */
/* ### Does this need to return the new branch? Certainly the caller needs
 *     some way to find out what branch was created there. Probably better
 *     to return it directly than have the caller use APIs that query the
 *     overall branching "state".
 */
svn_error_t *
svn_branch_branchify(svn_editor3_t *editor,
                     svn_editor3_nbid_t outer_eid,
                     apr_pool_t *scratch_pool);


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
 * This editor implements the "independent per-node changes" variant of the Ev3
 * commit editor interface.
 *
 * Use *BRANCHING_TXN as the branching state info ...
 *
 * SHIM_CONNECTOR can be used to enable a more exact round-trip conversion
 * from an Ev1 drive to Ev3 and back to Ev1. The caller should pass the
 * returned *SHIM_CONNECTOR value to svn_delta__delta_from_ev3_for_commit().
 * SHIM_CONNECTOR may be null if not wanted.
 *
 * REPOS_ROOT_URL is the repository root URL, and BASE_RELPATH is the
 * relative path within the repository of the root directory of the edit.
 * (An Ev1 edit must be rooted at a directory, not at a file.)
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
svn_delta__ev3_from_delta_for_commit2(
                        svn_editor3_t **editor_p,
                        svn_editor3__shim_connector_t **shim_connector,
                        const svn_delta_editor_t *deditor,
                        void *dedit_baton,
                        svn_branch_revision_root_t *branching_txn,
                        const char *repos_root_url,
                        const char *base_relpath,
                        svn_editor3__shim_fetch_func_t fetch_func,
                        void *fetch_baton,
                        svn_cancel_func_t cancel_func,
                        void *cancel_baton,
                        apr_pool_t *result_pool,
                        apr_pool_t *scratch_pool);

/* Like svn_delta__ev3_from_delta_for_commit2(), except:
 *   - doesn't take the 'branching_txn' parameter;
 *   - implements the "incremental changes" variant of the Ev3
 *     commit editor interface.
 */
svn_error_t *
svn_delta__ev3_from_delta_for_commit(
                        svn_editor3_t **editor_p,
                        svn_editor3__shim_connector_t **shim_connector,
                        const svn_delta_editor_t *deditor,
                        void *dedit_baton,
                        const char *repos_root_url,
                        const char *base_relpath,
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
svn_delta__delta_from_ev3_for_commit(
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
svn_delta__ev3_from_delta_for_update(
                        svn_update_editor3_t **editor_p,
                        const svn_delta_editor_t *deditor,
                        void *dedit_baton,
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
svn_delta__delta_from_ev3_for_update(
                        const svn_delta_editor_t **deditor,
                        void **dedit_baton,
                        svn_update_editor3_t *update_editor,
                        const char *repos_root_url,
                        const char *base_repos_relpath,
                        svn_editor3__shim_fetch_func_t fetch_func,
                        void *fetch_baton,
                        apr_pool_t *result_pool,
                        apr_pool_t *scratch_pool);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_EDITOR3_H */
