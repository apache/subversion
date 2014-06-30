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
 */

/*
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
 */

/**
 * @defgroup svn_editor The editor interface
 * @{
 */

/** Tree Editor
 */
typedef struct svn_editor3_t svn_editor3_t;

/** A location in the current transaction (when @a rev == -1) or in
 * a revision (when @a rev != -1). */
typedef struct pathrev_t
{
  svn_revnum_t rev;
  const char *relpath;
} pathrev_t;

/** Node-Branch Identifier -- functionally similar to the FSFS
 * <node-id>.<copy-id>, but the ids used within an editor drive may be
 * scoped locally to that editor drive rather than in-repository ids.
 * (Presently a null-terminated C string.) */
typedef char *svn_editor3_nbid_t;

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
 * @see svn_editor3_t.
 *
 * @defgroup svn_editor3_drive Driving the editor
 * @{
 */

/*
 * ===================================================================
 * Editor for Commit, with Incremental Path-Based Tree Changes
 * ===================================================================
 *
 * Versioning model assumed:
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
 * Edit Operations:
 *
 *   - mk  kind               {dir-path | ^/dir-path@rev}[1] new-name[2]
 *   - cp  ^/from-path@rev[3] {dir-path | ^/dir-path@rev}[1] new-name[2]
 *   - cp  from-path[4]       {dir-path | ^/dir-path@rev}[1] new-name[2]
 *   - mv  ^/from-path@rev[4] {dir-path | ^/dir-path@rev}[1] new-name[2]
 *   - res ^/from-path@rev[3] {dir-path | ^/dir-path@rev}[1] new-name[2]
 *   - rm                     {path | ^/path@rev}[5]
 *   - put new-content        {path | ^/path@rev}[5]
 *
 * Preconditions:
 *
 *   [1] target parent dir must exist in txn
 *   [2] target name (in parent dir) must not exist in txn
 *   [3] source must exist in committed revision
 *   [4] source must exist in txn
 *   [5] target must exist in txn
 *
 * Characteristics of this editor:
 *
 *   - tree changes form an ordered list
 *   - content changes are unordered and independent
 *   - all tree changes MAY be sent before all content changes
 *
 *   ### In order to expand the scope of this editor to situations like
 *       update/switch, where the receiver doesn't have the repository
 *       to refer to, Can we add a full-traversal kind of copy?
 *       Is that merely a matter of driving the same API in a different
 *       way ("let the copy operation mean non-recursive copy")? Or is
 *       it totally out of scope? (To support WC update we need other
 *       changes too, not just this.)
 *
 * Description of operations:
 *
 *   - "cp", "mv" and "rm" are recursive; "mk" and "put" are non-recursive.
 *
 *   - "mk": Create a single new node, not related to any other existing
 *     node. The default content is empty, and MAY be altered by "put".
 *
 *   - "cp": Create a copy of the subtree found at the specified "from"
 *     location in a committed revision or [if supported] in the current
 *     txn. Each node in the target subtree is marked as "copied from" the
 *     node with the corresponding path in the source subtree.
 *
 *   - "mv": Move a subtree to a new parent node-branch and/or a new name.
 *     The source must be present in the txn but is specified by reference
 *     to a location in a committed revision.
 *
 *   - "res": Resurrect a previously deleted node-branch. The specified
 *     source is any location at which this node-branch existed, not
 *     necessarily at its youngest revision nor even within its most
 *     recent period of existence. The default content is that of the
 *     source location, and MAY be altered by "put".
 *
 *     The source node-branch MUST NOT exist in the txn. If the source
 *     node-branch exists in the txn-base, resurrection would be
 *     equivalent to reverting a local delete in the txn; the sender
 *     SHOULD NOT do this. [### Why not? Just because it seems like
 *     unnecessary flexibility.]
 *
 *     ### Can we have a recursive resurrect operation? What should it do
 *         if a child node is still alive (moved or already resurrected)?
 *
 *   - "rm": Remove the specified node and, recursively, all nodes that
 *     are currently its children in the txn. It does not delete nodes
 *     that used to be its children that have since been moved away.
 *     "rm" SHOULD NOT be used on a node-branch created by "mk" nor on the
 *     root node-branch created by "cp", but MAY be used on a child of a
 *     copy.
 *
 *   - "put": Set the content of a node to the specified value. (The new
 *     content may be described in terms of a delta against another node's
 *     content.)
 *
 *     "put" MAY be sent for any node that exists in the final state, and
 *     SHOULD NOT be sent for a node that will no longer exist in the final
 *     state. "put" SHOULD NOT be sent more than once for any node-branch.
 *     "put" MUST provide the right kind of content to match the node kind;
 *     it cannot change the kind of a node nor convert the content to match
 *     the node kind.
 *
 * Commit Rebase:
 *
 *   - We assume the rebase will require there be no moves in
 *     intervening commits that overlap path-wise with the edits we are
 *     making. (If it would follow such moves while merging "on the fly",
 *     then it would be harder to design the editor such that the sender
 *     would know what paths-in-txn to refer to.)
 *
 *     This is quite a stringent restriction. See "Paths" below.
 *
 * Notes on Paths:
 *
 *   - A bare "path" refers to a "path-in-txn", that is a path in the
 *     current state of the transaction. ^/path@rev refers to a path in a
 *     committed revision which is to be traced to the current transaction.
 *     A path-in-txn can refer to a node that was created with "mk" or
 *     "cp" (including children) and MAY [### or SHOULD NOT?] refer to a
 *     node-branch that already existed before the edit began.
 *
 *   - Ev1 declares, by nesting, exactly what parent dir each operation
 *     refers to: a pre-existing one (in which case it checks it's still
 *     the same one) or one it has just created in the txn. We make this
 *     distinction with {path-in-txn | ^/path-in-rev@rev} instead.
 *
 *   - When the target path to "mk" or "cp" or "mv" is specified as
 *     ^/dir-path@rev, the new (root) path to be created in the txn is:
 *
 *         (^/dir-path@rev traced forward to the txn)/(new-name)
 *
 *     When the target path to "rm" or "put" is specified as ^/path@rev,
 *     the path to be removed or changed in the txn is:
 *
 *         (^/path@rev traced forward to the txn)
 *
 *   - Why use the semantic form "^/path@rev" rather than
 *     (path-in-txn, wc-base-rev)?
 * 
 *     Basically because, in general (if other commits on the server
 *     are allowed to move the nodes that this commit is editing),
 *     then (path-in-txn, wc-base-rev) does not unambiguously identify
 *     a node-revision or a specific path in revision wc-base-rev. The
 *     sender cannot know what path in the txn corresponds to a given path
 *     in wc-base-rev.
 *
 *     The server needs to identify the specific node-revision on which
 *     the client is basing a change, in order to check whether it is
 *     out of date. If the base of the change is out of date, a merge of
 *     this node would be required. The merge cannot be done on the server
 *     as then the committed version may differ from the version sent by
 *     the client, and there is no mechanism to inform the client of this.
 *     Therefore the commit must be rejected and the merge done on the
 *     client side via an "update".
 *
 *     (As a possible special case, if each side of the merge has identical
 *     changes, this may be considered a null merge when a "permissive"
 *     strictness policy is in effect.)
 * 
 *     Given "^/path@rev" the receiver can trace the node-branch forward
 *     from ^/path@rev to the txn, and find the path at which it is
 *     currently located in the txn (or find that it is not present), as
 *     well as discovering whether there was any change to it (including
 *     deletion) between ^/path@rev and the txn-base.
 * 
 *     When the node-branch is traced forward to the txn, due to moves in
 *     the txn the path-in-txn may be different from the initial path.
 *     The client needs to know the path-in-txn in order for future operations.
 *     (This is the case even if the out-of-date check rejects any move
 *     between WC-base and txn-base that affects the node-branch.)
 *
 *     Given (path-in-txn, wc-base-rev), if the OOD check *allows* merging
 *     with repository-side moves, then the sender cannot know what the paths
 *     in the txn-base are, and so cannot know what path-in-txn identifies
 *     any node that existed in an earlier revision.
 *
 *     Given (path-in-txn, wc-base-rev), if the OOD check *forbids* merging
 *     with repository-side moves then the receiver can trace backward
 *     from path-in-txn to path-in-txn-base and then from path-in-txn-base
 *     to path-in-rev, and find:
 *
 *       (a) this node-branch did not exist in "rev" => OOD
 *       (b) path-in-rev != path-in-txn-base => OOD
 *       (c) path-in-rev == path-in-txn-base => OOD iff changed
 *
 *     It would seem unnecessarily restrictive to expect that we would
 *     never want the OOD check to allow merging with a repository-side
 *     move of a parent of the node we are editing.
 *
 *   - When a target path is specified by ^/path@rev, note that the sender
 *     and the receiver both have to map that path forward through moves
 *     to calculate the corresponding path-in-txn.
 *
 *     ### If the server can merge the edits with repository-side moves
 *     on the fly, then the sender will not know what in-txn paths to
 *     refer to subsequently.
 *
 *     ### One way to support this: the sender could use "^/path@rev" to
 *     refer to a pre-existing node, appended with any sub-path created in
 *     the txn:
 *
 *         [^/path/in/rev] @rev [/path/components/created/within/txn]
 *
 *     The "^/path/in/rev@rev" part acts like an unambiguous node-id for
 *     each pre-existing node. The remaining part acts like an identifier
 *     for nodes created in the txn, but is unambiguous only if we take
 *     care not to allow them to be moved around freely.
 *
 * Notes on Copying:
 *
 *   - Copy from path-in-txn is required iff we want to support copying
 *     from "this revision". If we don't then the source is necessarily
 *     a pre-existing node and so can be referenced by ^/path@rev.
 *
 *   - There is no provision for making a non-tracked copy of a subtree,
 *     nor a copy in which some nodes are tracked and others untracked,
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
 * directory node-branch specified by @a parent_loc which may be either in
 * a committed revision or in the current txn. Set the new node's name to
 * @a new_name.
 *
 * The new node is not related by node identity to any other existing node
 * nor to any other node created by another "mk" operation.
 *
 * Preconditions: see above.
 *
 * @node "put" is optional for a node made by "mk".
 */
svn_error_t *
svn_editor3_mk(svn_editor3_t *editor,
               svn_node_kind_t new_kind,
               pathrev_t parent_loc,
               const char *new_name);

/** Create a copy of a subtree.
 *
 * The source subtree is found at @a from_loc. If @a from_loc is a
 * location in a committed revision, make a copy from (and referring to)
 * that location. [If supported] If @a from_loc is a location in the
 * current txn, make a copy from the current txn, which when committed
 * will refer to the committed revision.
 *
 * Create the root node of the new subtree in the parent directory
 * node-branch specified by @a parent_loc (which may be either in a
 * committed revision or in the current txn) with the name @a new_name.
 *
 * Each node in the target subtree has a "copied from" relationship with
 * the node with the corresponding path in the source subtree.
 *
 * The content of a node copied from an existing revision is, by default,
 * the content of the source node. The content of a node copied from this
 * revision is, by default, the FINAL content of the source node as
 * committed, even if the source node is changed after the copy operation.
 * In either case, the default content MAY be changed by a "put".
 */
svn_error_t *
svn_editor3_cp(svn_editor3_t *editor,
               pathrev_t from_loc,
               pathrev_t parent_loc,
               const char *new_name);

/** Move a subtree to a new parent directory and/or a new name.
 *
 * The root node of the source subtree in the current txn is the node-branch
 * specified by @a from_loc. @a from_loc must refer to a committed revision.
 *
 * Create the root node of the new subtree in the parent directory
 * node-branch specified by @a parent_loc (which may be either in a
 * committed revision or in the current txn) with the name @a new_name.
 *
 * Each node in the target subtree remains the same node-branch as
 * the node with the corresponding path in the source subtree.
 */
svn_error_t *
svn_editor3_mv(svn_editor3_t *editor,
               pathrev_t from_loc,
               pathrev_t new_parent_loc,
               const char *new_name);

/** Resurrect a node.
 *
 * Resurrect the node-branch that previously existed at @a from_loc,
 * a location in a committed revision. Put the resurrected node at
 * @a parent_loc, @a new_name.
 *
 * Set the content to @a new_content.
 */
svn_error_t *
svn_editor3_res(svn_editor3_t *editor,
                pathrev_t from_loc,
                pathrev_t parent_loc,
                const char *new_name);

/** Remove the existing node-branch identified by @a loc and, recursively,
 * all nodes that are currently its children in the txn.
 *
 * It does not delete nodes that used to be children of the specified
 * node-branch that have since been moved away.
 */
svn_error_t *
svn_editor3_rm(svn_editor3_t *editor,
               pathrev_t loc);

/** Set the content of the node-branch identified by @a loc.
 *
 * Set the content to @a new_content.
 */
svn_error_t *
svn_editor3_put(svn_editor3_t *editor,
                pathrev_t loc,
                const svn_editor3_node_content_t *new_content);


/*
 * ========================================================================
 * Editor for Commit, with Separate Unordered Per-Node Tree Changes
 * ========================================================================
 *
 * Versioning model assumed:
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
 * Edit Operations:
 *
 *   - add     kind      new-parent-nb[2] new-name new-content  ->  new-nb
 *   - copy    nb@rev[3] new-parent-nb[2] new-name new-content  ->  new-nb
 *   - delete  nb[1]   since-rev
 *   - alter   nb[1,2] since-rev new-parent-nb[2] new-name new-content
 *
 * Preconditions:
 *
 *   [1] node-branch must exist in initial state
 *   [2] node-branch must exist in final state
 *   [3] source must exist in committed revision or txn final state
 *
 * Characteristics of this editor:
 *
 *   - tree changes and content changes are specified per node
 *   - the changes for each node are unordered and mostly independent;
 *     the only dependencies are those needed to ensure the result is a
 *     directory hierarchy
 *   - copies are non-recursive
 *     ### Can we design recursive (cheap) copy?
 *
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

/** Create a new versioned object of kind @a new_kind.
 * 
 * Assign the new node a locally unique node-branch-id, @a local_nbid,
 * with which it can be referenced within this edit.
 *
 * Set the node's parent and name to @a new_parent_nbid and @a new_name.
 *
 * Set the content to @a new_content.
 *
 * For all restrictions on driving the editor, see #svn_editor3_t.
 */
svn_error_t *
svn_editor3_add(svn_editor3_t *editor,
                svn_editor3_nbid_t local_nbid,
                svn_node_kind_t new_kind,
                svn_editor3_nbid_t new_parent_nbid,
                const char *new_name,
                const svn_editor3_node_content_t *new_content);

/** Create a copy of an existing or new node, and optionally change its
 * content.
 *
 * Assign the target node a locally unique node-branch-id, @a local_nbid,
 * with which it can be referenced within this edit.
 *
 * Copy from the source node at @a src_revision, @a src_nbid. If
 * @a src_revision is #SVN_INVALID_REVNUM, it means copy from within
 * the new revision being described.
 *   ### See note on copy_tree().
 *
 * Set the target node's parent and name to @a new_parent_nbid and
 * @a new_name. Set the target node's content to @a new_content.
 *
 * @note This copy is not recursive. Children may be copied separately if
 * required.
 *
 * @see svn_editor3_copy_tree()
 *
 * For all restrictions on driving the editor, see #svn_editor3_t.
 */
svn_error_t *
svn_editor3_copy_one(svn_editor3_t *editor,
                     svn_editor3_nbid_t local_nbid,
                     svn_revnum_t src_revision,
                     svn_editor3_nbid_t src_nbid,
                     svn_editor3_nbid_t new_parent_nbid,
                     const char *new_name,
                     const svn_editor3_node_content_t *new_content);

/** Create a copy of an existing or new subtree. Each node in the source
 * subtree will be copied (branched) to the same relative path within the
 * target subtree. The nodes created by this copy cannot be modified or
 * addressed within this edit.
 *
 * Set the target root node's parent and name to @a new_parent_nbid and
 * @a new_name.
 *
 * Copy from the source node at @a src_revision, @a src_nbid. If
 * @a src_revision is #SVN_INVALID_REVNUM, it means copy from within
 * the new revision being described. In this case the subtree copied is
 * the FINAL subtree as committed, regardless of the order in which the
 * edit operations are described.
 *   ### Is it necessarily the case that the state at the end of the edit
 *       is the state to be committed (subject to rebasing), or is it
 *       possible that a later edit might be performed on the txn?
 *       And how might we apply this principle to a non-commit editor
 *       such as a WC update?
 *
 * The content of each node copied from an existing revision is the content
 * of the source node. The content of each node copied from this revision
 * is the FINAL content of the source node as committed.
 *
 * @see svn_editor3_copy_one()
 *
 * For all restrictions on driving the editor, see #svn_editor3_t.
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
 * ###  @note The delete is not recursive. Child nodes must be explicitly
 *      deleted or moved away.
 *   OR @note The delete is implicitly recursive: each child node that
 *      is not otherwise moved to a new parent will be deleted as well.
 *
 * For all restrictions on driving the editor, see #svn_editor3_t.
 */
svn_error_t *
svn_editor3_delete(svn_editor3_t *editor,
                   svn_revnum_t since_rev,
                   svn_editor3_nbid_t nbid);

/** Alter the tree position and/or contents of the node-branch identified
 * by @a nbid, or resurrect it if it previously existed.
 *
 * @a since_rev specifies the base revision on which this edit was
 * performed: the server can consider the change "out of date" if a commit
 * since then has changed or deleted this node-branch.
 *
 * Set the node's parent and name to @a new_parent_nbid and @a new_name.
 *
 * Set the content to @a new_content.
 *
 * A no-op change MUST be accepted but, in the interest of efficiency,
 * SHOULD NOT be sent.
 *
 * For all restrictions on driving the editor, see #svn_editor3_t.
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
 * For all restrictions on driving the editor, see #svn_editor3_t.
 */
svn_error_t *
svn_editor3_complete(svn_editor3_t *editor);

/** Drive @a editor's #svn_editor3_cb_abort_t callback.
 *
 * Notify that the edit transmission was not successful.
 * ### TODO @todo Shouldn't we add a reason-for-aborting argument?
 *
 * For all restrictions on driving the editor, see #svn_editor3_t.
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
 * @see svn_editor3_t, svn_editor3_cb_funcs_t.
 *
 * @defgroup svn_editor_callbacks Editor callback definitions
 * @{
 */

/** @see svn_editor3_mk(), svn_editor3_t.
 */
typedef svn_error_t *(*svn_editor3_cb_mk_t)(
  void *baton,
  svn_node_kind_t new_kind,
  pathrev_t parent_loc,
  const char *new_name,
  apr_pool_t *scratch_pool);

/** @see svn_editor3_cp(), svn_editor3_t.
 */
typedef svn_error_t *(*svn_editor3_cb_cp_t)(
  void *baton,
  pathrev_t from_loc,
  pathrev_t parent_loc,
  const char *new_name,
  apr_pool_t *scratch_pool);

/** @see svn_editor3_mv(), svn_editor3_t.
 */
typedef svn_error_t *(*svn_editor3_cb_mv_t)(
  void *baton,
  pathrev_t from_loc,
  pathrev_t new_parent_loc,
  const char *new_name,
  apr_pool_t *scratch_pool);

/** @see svn_editor3_res(), svn_editor3_t.
 */
typedef svn_error_t *(*svn_editor3_cb_res_t)(
  void *baton,
  pathrev_t from_loc,
  pathrev_t parent_loc,
  const char *new_name,
  apr_pool_t *scratch_pool);

/** @see svn_editor3_rm(), svn_editor3_t.
 */
typedef svn_error_t *(*svn_editor3_cb_rm_t)(
  void *baton,
  pathrev_t loc,
  apr_pool_t *scratch_pool);

/** @see svn_editor3_put(), svn_editor3_t.
 */
typedef svn_error_t *(*svn_editor3_cb_put_t)(
  void *baton,
  pathrev_t loc,
  const svn_editor3_node_content_t *new_content,
  apr_pool_t *scratch_pool);

/** @see svn_editor3_add(), svn_editor3_t.
 */
typedef svn_error_t *(*svn_editor3_cb_add_t)(
  void *baton,
  svn_editor3_nbid_t local_nbid,
  svn_node_kind_t new_kind,
  svn_editor3_nbid_t new_parent_nbid,
  const char *new_name,
  const svn_editor3_node_content_t *new_content,
  apr_pool_t *scratch_pool);

/** @see svn_editor3_copy(), svn_editor3_t.
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

/** @see svn_editor3_copy(), svn_editor3_t.
 */
typedef svn_error_t *(*svn_editor3_cb_copy_tree_t)(
  void *baton,
  svn_revnum_t src_revision,
  svn_editor3_nbid_t src_nbid,
  svn_editor3_nbid_t new_parent_nbid,
  const char *new_name,
  apr_pool_t *scratch_pool);

/** @see svn_editor3_delete(), svn_editor3_t.
 */
typedef svn_error_t *(*svn_editor3_cb_delete_t)(
  void *baton,
  svn_revnum_t since_rev,
  svn_editor3_nbid_t nbid,
  apr_pool_t *scratch_pool);

/** @see svn_editor3_alter(), svn_editor3_t.
 */
typedef svn_error_t *(*svn_editor3_cb_alter_t)(
  void *baton,
  svn_revnum_t since_rev,
  svn_editor3_nbid_t nbid,
  svn_editor3_nbid_t new_parent_nbid,
  const char *new_name,
  const svn_editor3_node_content_t *new_content,
  apr_pool_t *scratch_pool);

/** @see svn_editor3_complete(), svn_editor3_t.
 */
typedef svn_error_t *(*svn_editor3_cb_complete_t)(
  void *baton,
  apr_pool_t *scratch_pool);

/** @see svn_editor3_abort(), svn_editor3_t.
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
 * @see svn_editor3_create(), svn_editor3_t.
 */
typedef struct svn_editor3_cb_funcs_t
{
  svn_editor3_cb_mk_t cb_mk;
  svn_editor3_cb_cp_t cb_cp;
  svn_editor3_cb_mv_t cb_mv;
  svn_editor3_cb_res_t cb_res;
  svn_editor3_cb_rm_t cb_rm;
  svn_editor3_cb_put_t cb_put;

  svn_editor3_cb_add_t cb_add;
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
 * @a scratch_pool is used for temporary allocations (if any). Note that
 * this is NOT the same @a scratch_pool that is passed to callback functions.
 *
 * @see svn_editor3_t
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
 */
void *
svn_editor3_get_baton(const svn_editor3_t *editor);

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
 * The @a kind field specifies the node kind. The kind of content must
 * match the kind of node it is being put into, as a node's kind cannot
 * be changed.
 *
 * The @a ref field specifies a reference content: the content of an
 * existing committed node, or empty. The other fields are optional
 * overrides for parts of the content.
 *
 * ### Specify content as deltas against the (optional) reference instead
 *     of as overrides?
 */
struct svn_editor3_node_content_t
{
  /* The node kind: dir, file, or symlink */
  svn_node_kind_t kind;

  /* Reference the content in an existing, committed node-rev.
   *
   * If this is (SVN_INVALID_REVNUM, NULL) then the reference content
   * is empty.
   *
   * ### Reference a whole node-rev instead? (Don't need to reference a
   *     specific rev.)
   */
  pathrev_t ref;

  /* Properties (for all node kinds).
   * Maps (const char *) name -> (svn_string_t) value. */
  apr_hash_t *props;

  /* Text checksum (only for a file; otherwise SHOULD be NULL). */
  const svn_checksum_t *checksum;

  /* Text stream, readable (only for a file; otherwise SHOULD be NULL).
   * ### May be null if we expect the receiver to retrieve the text by its
   *     checksum? */
  svn_stream_t *stream;

  /* Symlink target (only for a symlink; otherwise SHOULD be NULL). */
  const char *target;

};

/* Duplicate a node-content into result_pool.
 * ### What about the stream though? Maybe we shouldn't have a _dup.
 */
/* svn_editor3_node_content_t *
svn_editor3_node_content_dup(const svn_editor3_node_content_t *old,
                             apr_pool_t *result_pool); */

/* Create a new node-content object for a directory node.
 *
 * Allocate it in @a result_pool. */
svn_editor3_node_content_t *
svn_editor3_node_content_create_dir(pathrev_t ref,
                                    apr_hash_t *props,
                                    apr_pool_t *result_pool);

/* Create a new node-content object for a file node.
 *
 * Allocate it in @a result_pool. */
svn_editor3_node_content_t *
svn_editor3_node_content_create_file(pathrev_t ref,
                                     apr_hash_t *props,
                                     const svn_checksum_t *checksum,
                                     svn_stream_t *stream,
                                     apr_pool_t *result_pool);

/* Create a new node-content object for a symlink node.
 *
 * Allocate it in @a result_pool. */
svn_editor3_node_content_t *
svn_editor3_node_content_create_symlink(pathrev_t ref,
                                        apr_hash_t *props,
                                        const char *target,
                                        apr_pool_t *result_pool);

/** @} */

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_EDITOR3_H */
