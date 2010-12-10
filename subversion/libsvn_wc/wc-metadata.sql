/* wc-metadata.sql -- schema used in the wc-metadata SQLite database
 *     This is intended for use with SQLite 3
 *
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
 */

/*
 * the KIND column in these tables has one of the following values
 * (documented in the corresponding C type #svn_wc__db_kind_t):
 *   "file"
 *   "dir"
 *   "symlink"
 *   "unknown"
 *   "subdir"
 *
 * the PRESENCE column in these tables has one of the following values
 * (see also the C type #svn_wc__db_status_t):
 *   "normal"
 *   "absent" -- server has declared it "absent" (ie. authz failure)
 *   "excluded" -- administratively excluded (ie. sparse WC)
 *   "not-present" -- node not present at this REV
 *   "incomplete" -- state hasn't been filled in
 *   "base-deleted" -- node represents a delete of a BASE node
 */

/* One big list of statements to create our (current) schema.  */
-- STMT_CREATE_SCHEMA

/* ------------------------------------------------------------------------- */

CREATE TABLE REPOSITORY (
  id INTEGER PRIMARY KEY AUTOINCREMENT,

  /* The root URL of the repository. This value is URI-encoded.  */
  root  TEXT UNIQUE NOT NULL,

  /* the UUID of the repository */
  uuid  TEXT NOT NULL
  );

/* Note: a repository (identified by its UUID) may appear at multiple URLs.
   For example, http://example.com/repos/ and https://example.com/repos/.  */
CREATE INDEX I_UUID ON REPOSITORY (uuid);
CREATE INDEX I_ROOT ON REPOSITORY (root);


/* ------------------------------------------------------------------------- */

CREATE TABLE WCROOT (
  id  INTEGER PRIMARY KEY AUTOINCREMENT,

  /* absolute path in the local filesystem.  NULL if storing metadata in
     the wcroot itself. */
  local_abspath  TEXT UNIQUE
  );

CREATE UNIQUE INDEX I_LOCAL_ABSPATH ON WCROOT (local_abspath);


/* ------------------------------------------------------------------------- */

/*
The BASE_NODE table:

  BASE is what we get from the server.  It is the *absolute* pristine copy.
  You need to use checkout, update, switch, or commit to alter your view of
  the repository.

  In the BASE tree, each node corresponds to a particular node-rev in the
  repository.  It can be a mixed-revision tree.  Each node holds either a
  copy of the node-rev as it exists in the repository (if presence ==
  'normal'), or a place-holder (if presence == 'absent' or 'excluded' or
  'not-present').
                                                  [Quoted from wc_db.h]

Overview of BASE_NODE columns:

  Indexing columns: (wc_id, local_relpath, parent_relpath)

  (presence)

    - The Node-Rev, Content and Last-Change column groups take one of the
      states shown in the table below, according to the 'presence':

                                    Has           Has       Has
      'presence'      Meaning       Node-Rev?     Content?  Last-Change?
      ----------      -----------   -----------   --------  ------------
      normal      =>  Present       Yes           Yes       Yes
      incomplete  =>  Incomplete    Yes           Partial   No ### ?
      absent      =>  Unauthz       Yes           No        No
      excluded    =>  Unwanted      Yes           No        No
      not-present =>  Nonexistent   No            No        No

    - If presence==incomplete, this node refers to an existing node-rev but
      its Content is not fully and correctly stored.  In particular, if it
      is a directory, some rows that should represent its children may not
      exist or may be in the wrong state.  This is intended to be a
      temporary state, e.g. during an update.

    - If presence==absent or ==excluded, this row refers to a node that
      exists in the repo, but the node is not stored in the WC.

    - If presence==not-present, this row indicates that its parent in the WC
      is a directory that, in its pristine state, would have a child of this
      name.  However, this child was updated or switched to a node-revision
      that does not exist.  Information about which node-revision it was
      updated or switched to is lost; only the fact that it is currently not
      present is remembered.

    - The order of precedence of the negative presence values is:
        'excluded' if administratively excluded from the WC, else
        'absent' if server doesn't authorize reading the path, else
        'not-present' if it does not exist in repo.

  Node-Rev columns: (repos_id, repos_relpath, revnum)

    - The Node-Rev group points to the corresponding repository node-rev.

    - If not used (as specified by the 'presence' table above), the values
      are undefined.
      ### Perhaps we should set them to null to make it clearer.

  Content columns: (kind, properties, depth, target, checksum)
                    ----  ----------  -----  ------  --------
                    'dir'    Yes      Yes    null    null
                'symlink'    Yes      null   Yes     null
                   'file'    Yes      null   null    Yes
                'unknown'    null     null   null    null

    - The Content columns take one of the states shown in the table above.
      If Content is present, a copy of the Node-Rev's content is stored
      locally according to one of the first three rows in the table above,
      otherwise kind==unknown and the other columns are null.

    - If kind==dir, the children are represented by the existence of other
      BASE_NODE rows.  For each immediate child of 'repos_relpath'@'revnum'
      in the repo, subject to 'depth', a BASE_NODE row exists with its
      'local_relpath' being this node's 'local_relpath' plus the child's
      basename.  (Rows may also exist for additional children which are
      outside the scope of 'depth' or do not exist as children of this
      node-rev in the repository, including 'externals' and paths updated to
      a revision in which they do exist.)  There is no distinction between
      depth==immediates and depth==infinity here.

    - If kind==symlink, the target path is contained in 'target'.

    - If kind==file, the content is contained in the Pristine Store,
      referenced by its SHA-1 checksum 'checksum'.

  Last-Change columns: (changed_rev, changed_date, changed_author)

    - Specifies the revision in which the content was last changed before
      Node-Rev, following copies and not counting the copy operation itself
      as a change.

    - Does not specify the revision in which this node first appeared at
      the repository path 'repos_relpath', which could be more recent than
      the last change of this node's content.

    - Includes a copy of the corresponding date and author rev-props.

    - If not used (as specified by the 'presence' table above), all null.
      ### Not checked; in practice these columns may have undefined values.

  Working file status: (translated_size, last_mod_time)

    - Present iff kind==file and node has no WORKING_NODE row, otherwise
      null.  (If kind==file and node has a WORKING_NODE row, the info is
      recorded in that row).  ### True?

    - Records the status of the working file on disk, for the purpose of
      detecting quickly whether that file has been modified.

    - Logically belongs to the ACTUAL_NODE table but is recorded in the
      BASE_NODE and WORKING_NODE tables instead to avoid the overhead of
      storing an ACTUAL_NODE row for each unmodified file.

    - Records the actual size and mod-time of the disk file at the time when
      its content was last determined to be logically unmodified relative to
      its base, taking account of keywords and EOL style.

  (dav_cache)

    - Content is opaque to libsvn_wc.  ### ?

    - Lifetime is managed by the WC: values cleared to null at certain times.
      ### To be documented.

  (incomplete_children)

    - Obsolete, unused.

  (file_external)

    - ### To be obsoleted?
*/

CREATE TABLE BASE_NODE (
  /* specifies the location of this node in the local filesystem. wc_id
     implies an absolute path, and local_relpath is relative to that
     location (meaning it will be "" for the wcroot). */
  wc_id  INTEGER NOT NULL REFERENCES WCROOT (id),
  local_relpath  TEXT NOT NULL,

  /* The repository this node is part of, and the relative path (from its
     root) within revision "revnum" of that repository.  These may be NULL,
     implying they should be derived from the parent and local_relpath.
     Non-NULL typically indicates a switched node.

     Note: they must both be NULL, or both non-NULL. */
  repos_id  INTEGER REFERENCES REPOSITORY (id),
  repos_relpath  TEXT,

  /* parent's local_relpath for aggregating children of a given parent.
     this will be "" if the parent is the wcroot. NULL if this is the
     wcroot node. */
  parent_relpath  TEXT,

  /* Is this node "present" or has it been excluded for some reason?
     The "base-deleted" presence value is not allowed.  */
  presence  TEXT NOT NULL,

  /* The node kind: "file", "dir", or "symlink", or "unknown" if the node is
     not present. */
  kind  TEXT NOT NULL,

  /* The revision number in which "repos_relpath" applies in "repos_id".
     this could be NULL for non-present nodes -- no info. */
  revnum  INTEGER,

  /* If this node is a file, then the SHA-1 checksum of the pristine text. */
  checksum  TEXT,

  /* The size in bytes of the working file when it had no local text
     modifications. This means the size of the text when translated from
     repository-normal format to working copy format with EOL style
     translated and keywords expanded according to the properties in the
     "properties" column of this row.

     NULL if this node is not a file or if the size has not (yet) been
     computed. */
  translated_size  INTEGER,

  /* Information about the last change to this node. changed_rev must be
     not-null if this node has presence=="normal". changed_date and
     changed_author may be null if the corresponding revprops are missing.

     All three values are null for a not-present node.  */
  changed_rev  INTEGER,
  changed_date  INTEGER,  /* an APR date/time (usec since 1970) */
  changed_author  TEXT,

  /* NULL depth means "default" (typically svn_depth_infinity) */
  depth  TEXT,

  /* for kind==symlink, this specifies the target. */
  symlink_target  TEXT,

  /* The mod-time of the working file when it was last determined to be
     logically unmodified relative to its base, taking account of keywords
     and EOL style.

     NULL if this node is not a file or if this info has not yet been
     determined.
   */
  /* ### Do we need this?  We've currently got various mod time APIs
     ### internal to libsvn_wc, but those might be used in answering some
     ### question which is better answered some other way. */
  last_mod_time  INTEGER,  /* an APR date/time (usec since 1970) */

  /* serialized skel of this node's properties. NULL if we
     have no information about the properties (a non-present node). */
  properties  BLOB,

  /* serialized skel of this node's dav-cache.  could be NULL if the
     node does not have any dav-cache. */
  dav_cache  BLOB,

  /* ### this column is removed in format 13. it will always be NULL.  */
  incomplete_children  INTEGER,

  /* The serialized file external information. */
  /* ### hack.  hack.  hack.
     ### This information is already stored in properties, but because the
     ### current working copy implementation is such a pain, we can't
     ### readily retrieve it, hence this temporary cache column.
     ### When it is removed, be sure to remove the extra column from
     ### the db-tests.

     ### Note: This is only here as a hack, and should *NOT* be added
     ### to any wc_db APIs.  */
  file_external  TEXT,

  PRIMARY KEY (wc_id, local_relpath)
  );

CREATE INDEX I_PARENT ON BASE_NODE (wc_id, parent_relpath);


/* ------------------------------------------------------------------------- */

/* The PRISTINE table keeps track of pristine texts. Each pristine text is
   stored in a file which may be compressed. Each pristine text is
   referenced by any number of rows in the BASE_NODE and WORKING_NODE
   and ACTUAL_NODE tables.
 */
CREATE TABLE PRISTINE (
  /* The SHA-1 checksum of the pristine text. This is a unique key. The
     SHA-1 checksum of a pristine text is assumed to be unique among all
     pristine texts referenced from this database. */
  checksum  TEXT NOT NULL PRIMARY KEY,

  /* ### enumerated values specifying type of compression. NULL implies
     ### that no compression has been applied. */
  compression  INTEGER,

  /* The size in bytes of the file in which the pristine text is stored. */
  /* ### used to verify the pristine file is "proper". NULL if unknown,
     ### and (thus) the pristine copy is incomplete/unusable. */
  size  INTEGER,

  /* ### this will probably go away, in favor of counting references
     ### that exist in BASE_NODE and WORKING_NODE. */
  refcount  INTEGER NOT NULL,

  /* Alternative MD5 checksum used for communicating with older
     repositories. Not guaranteed to be unique among table rows.
     NULL if not (yet) calculated. */
  md5_checksum  TEXT
  );


/* ------------------------------------------------------------------------- */

/* The WORKING_NODE table describes tree changes in the WC relative to the
   BASE_NODE table.

   The WORKING_NODE row for a given path exists iff a node at this path
   is itself one of:

     - deleted
     - moved away [1]

   and/or one of:

     - added
     - copied here [1]
     - moved here [1]

   or if this path is a child (or grandchild, etc.) under any such node.
   (### Exact meaning of "child" when mixed-revision, switched, etc.?)

   [1] The WC-NG "move" operation requires that both the source and
   destination paths are represented in the BASE_NODE and WORKING_NODE
   tables. The "copy" operation takes as its source a repository node,
   regardless whether that node is also represented in the WC.
 */
CREATE TABLE WORKING_NODE (
  /* specifies the location of this node in the local filesystem */
  wc_id  INTEGER NOT NULL REFERENCES WCROOT (id),
  local_relpath  TEXT NOT NULL,

  /* parent's local_relpath for aggregating children of a given parent.
     this will be "" if the parent is the wcroot.  Since a wcroot will
     never have a WORKING node the parent_relpath will never be null. */
  /* ### would be nice to make this column NOT NULL.  */
  parent_relpath  TEXT,

  /* Is this node "present" or has it been excluded for some reason?
     Only allowed values: normal, not-present, incomplete, base-deleted,
     excluded.  (the others do not make sense for the WORKING tree)

     normal: this node has been added/copied/moved-here. There may be an
       underlying BASE node at this location, implying this is a replace.
       Scan upwards from here looking for copyfrom or moved_here values
       to detect the type of operation constructing this node.

     not-present: the node (or parent) was originally copied or moved-here.
       A subtree of that source has since been deleted. There may be
       underlying BASE node to replace. For a move-here or copy-here, the
       records are simply removed rather than switched to not-present.
       Note this reflects a deletion only. It is not possible move-away
       nodes from the WORKING tree. The purported destination would receive
       a copy from the original source of a copy-here/move-here, or if the
       nodes were plain adds, those nodes would be shifted to that target
       for addition.

     incomplete: nodes are being added into the WORKING tree, and the full
       information about this node is not (yet) present.

     base-deleted: the underlying BASE node has been marked for deletion due
       to a delete or a move-away (see the moved_to column to determine
       which), and has not been replaced.

     excluded: this node is administratively excluded (sparse WC). This must
       be a child (or grandchild etc.) of a copied directory.
  */
  presence  TEXT NOT NULL,

  /* the kind of the new node. may be "unknown" if the node is not present. */
  kind  TEXT NOT NULL,

  /* The SHA-1 checksum of the pristine text, if this node is a file and was
     moved here or copied here, else NULL. */
  checksum  TEXT,

  /* The size in bytes of the working file when it had no local text
     modifications. This means the size of the text when translated from
     repository-normal format to working copy format with EOL style
     translated and keywords expanded according to the properties in the
     "properties" column of this row.

     NULL if this node is not a file, or is not moved here or copied here,
     or if the size has not (yet) been computed. */
  translated_size  INTEGER,

  /* If this node was moved here or copied here, then the following fields may
     have information about their source node. See BASE_NODE.changed_* for
     more information.

     For an added or not-present node, these are null.  */
  changed_rev  INTEGER,
  changed_date  INTEGER,  /* an APR date/time (usec since 1970) */
  changed_author  TEXT,

  /* NULL depth means "default" (typically svn_depth_infinity) */
  /* ### depth on WORKING? seems this is a BASE-only concept. how do
     ### you do "files" on an added-directory? can't really ignore
     ### the subdirs! */
  /* ### maybe a WC-to-WC copy can retain a depth?  */
  depth  TEXT,

  /* for kind==symlink, this specifies the target. */
  symlink_target  TEXT,

  /* Where this node was copied/moved from. All copyfrom_* fields are set
     only on the root of the operation, and are NULL for all children. */
  copyfrom_repos_id  INTEGER REFERENCES REPOSITORY (id),
  copyfrom_repos_path  TEXT,
  copyfrom_revnum  INTEGER,

  /* ### JF: For an old-style move, "copyfrom" info stores its source, but a
     new WC-NG "move" is intended to be a "true rename" so its copyfrom
     revision is implicit, being in effect (new head - 1) at commit time.
     For a (new) move, we need to store or deduce the copyfrom local-relpath;
     perhaps add a column called "moved_from". */

  /* Boolean value, specifying if this node was moved here (rather than just
     copied). The source of the move is specified in copyfrom_*.  */
  moved_here  INTEGER,

  /* If the underlying node was moved away (rather than just deleted), this
     specifies the local_relpath of where the BASE node was moved to.
     This is set only on the root of a move, and is NULL for all children.

     Note that moved_to never refers to *this* node. It always refers
     to the "underlying" node, whether that is BASE or a child node
     implied from a parent's move/copy.  */
  moved_to  TEXT,

  /* ### Do we need this?  We've currently got various mod time APIs
     ### internal to libsvn_wc, but those might be used in answering some
     ### question which is better answered some other way. */
  last_mod_time  INTEGER,  /* an APR date/time (usec since 1970) */

  /* serialized skel of this node's properties. NULL if we
     have no information about the properties (a non-present node). */
  properties  BLOB,

  /* should the node on disk be kept after a schedule delete?

     ### Bert points out that this can disappear once we get to single-db. 
     ### The entire reason for this flag to exist is
     ### so that the admin area can exist for the commit of a delete,
     ### and so the post-commit cleanup knows not to actually delete the dir
     ### from disk (which is why the flag is only ever set on the this_dir
     ### entry in WC-OLD.)  With single-db, we don't need to keep the old
     ### admin area around, so this flag can disappear.
     ### neels: In contrast, the --keep-local commandline option will not
     ### disappear. The user will still be able to do
     ### 'svn delete --keep-local' and keep the to-be-unversioned paths
     ### in the file system. It just won't be necessary to remember the
     ### keep-local-ness here, because we either delete the file system paths
     ### right away during 'svn delete' or we don't at all. There won't be a
     ### "second pass" for file system deletion at commit time anymore. */
  keep_local  INTEGER,

  PRIMARY KEY (wc_id, local_relpath)
  );

CREATE INDEX I_WORKING_PARENT ON WORKING_NODE (wc_id, parent_relpath);


/* ------------------------------------------------------------------------- */

/* The ACTUAL_NODE table describes text changes and property changes on each
   node in the WC, relative to the WORKING_NODE table row for the same path
   (if present) or else to the BASE_NODE row for the same path.  (Either a
   WORKING_NODE row or a BASE_NODE row must exist if this node exists, but
   an ACTUAL_NODE row can exist on its own if it is just recording info on
   a non-present node - a tree conflict or a changelist, for example.)

   The ACTUAL_NODE table row for a given path exists if the node at that
   path is known to have text or property changes relative to its
   WORKING_NODE row. ("Is known" because a text change on disk may not yet
   have been discovered and recorded here.)

   The ACTUAL_NODE table row for a given path may also exist in other cases,
   including if the "changelist" or any of the conflict columns have a
   non-null value.
 */
CREATE TABLE ACTUAL_NODE (
  /* specifies the location of this node in the local filesystem */
  wc_id  INTEGER NOT NULL REFERENCES WCROOT (id),
  local_relpath  TEXT NOT NULL,

  /* parent's local_relpath for aggregating children of a given parent.
     this will be "" if the parent is the wcroot. NULL if this is the
     wcroot node. */
  parent_relpath  TEXT,

  /* serialized skel of this node's properties. NULL implies no change to
     the properties, relative to WORKING/BASE as appropriate. */
  properties  BLOB,

  /* basenames of the conflict files. */
  /* ### These columns will eventually be merged into conflict_data below. */
  conflict_old  TEXT,
  conflict_new  TEXT,
  conflict_working  TEXT,
  prop_reject  TEXT,  /* ### is this right? */

  /* if not NULL, this node is part of a changelist. */
  changelist  TEXT,
  
  /* ### need to determine values. "unknown" (no info), "admin" (they
     ### used something like 'svn edit'), "noticed" (saw a mod while
     ### scanning the filesystem). */
  text_mod  TEXT,

  /* if a directory, serialized data for all of tree conflicts therein.
     ### This column will eventually be merged into the conflict_data column,
     ### but within the ACTUAL node of the tree conflict victim itself, rather
     ### than the node of the tree conflict victim's parent directory. */
  tree_conflict_data  TEXT,

  /* A skel containing the conflict details.  */
  conflict_data  BLOB,

  /* Three columns containing the checksums of older, left and right conflict
     texts.  Stored in a column to allow storing them in the pristine store  */
  /* stsp: This is meant for text conflicts, right? What about property
           conflicts? Why do we need these in a column to refer to the
           pristine store? Can't we just parse the checksums from
           conflict_data as well? */
  older_checksum  TEXT,
  left_checksum  TEXT,
  right_checksum  TEXT,

  PRIMARY KEY (wc_id, local_relpath)
  );

CREATE INDEX I_ACTUAL_PARENT ON ACTUAL_NODE (wc_id, parent_relpath);
CREATE INDEX I_ACTUAL_CHANGELIST ON ACTUAL_NODE (changelist);


/* ------------------------------------------------------------------------- */

CREATE TABLE LOCK (
  /* what repository location is locked */
  repos_id  INTEGER NOT NULL REFERENCES REPOSITORY (id),
  repos_relpath  TEXT NOT NULL,
  /* ### BH: Shouldn't this refer to an working copy location? You can have a
         single relpath checked out multiple times in one (switch) or more
         working copies. */
  /* ### HKW: No, afaik.  This table is just a cache of what's in the
         repository, so these should be repos_relpaths. */

  /* Information about the lock. Note: these values are just caches from
     the server, and are not authoritative. */
  lock_token  TEXT NOT NULL,
  /* ### make the following fields NOT NULL ? */
  lock_owner  TEXT,
  lock_comment  TEXT,
  lock_date  INTEGER,   /* an APR date/time (usec since 1970) */
  
  PRIMARY KEY (repos_id, repos_relpath)
  );


/* ------------------------------------------------------------------------- */

CREATE TABLE WORK_QUEUE (
  /* Work items are identified by this value.  */
  id  INTEGER PRIMARY KEY AUTOINCREMENT,

  /* A serialized skel specifying the work item.  */
  work  BLOB NOT NULL
  );


/* ------------------------------------------------------------------------- */

CREATE TABLE WC_LOCK (
  /* specifies the location of this node in the local filesystem */
  wc_id  INTEGER NOT NULL  REFERENCES WCROOT (id),
  local_dir_relpath  TEXT NOT NULL,

  locked_levels  INTEGER NOT NULL DEFAULT -1,

  PRIMARY KEY (wc_id, local_dir_relpath)
 );


PRAGMA user_version =
-- define: SVN_WC__VERSION
;


/* ------------------------------------------------------------------------- */

/* The NODES table describes the way WORKING nodes are layered on top of
   BASE nodes and on top of other WORKING nodes, due to nested tree structure
   changes. The layers are modelled using the "op_depth" column.

   An 'operation depth' refers to the number of directory levels down from
   the WC root at which a tree-change operation (delete, add?, copy, move)
   was performed.  A row's 'op_depth' does NOT refer to the depth of its own
   'local_relpath', but rather to the depth of the nearest tree change that
   affects that node.

   The row with op_depth=0 for any given local relpath represents the "base"
   node that is created and updated by checkout, update, switch and commit
   post-processing.  The row with the highest op_depth for a particular
   local_relpath represents the working version.  Any rows with intermediate
   op_depth values are not normally visible to the user but may become
   visible after reverting local changes.

   ### The following text needs revision

   Each row in BASE_NODE has an associated row NODE_DATA. Additionally, each
   row in WORKING_NODE has one or more associated rows in NODE_DATA.

   This table contains full node descriptions for nodes in either the BASE
   or WORKING trees as described in notes/wc-ng/design. Fields relate
   both to BASE and WORKING trees, unless documented otherwise.

   ### This table is to be integrated into the SCHEMA statement as soon
       the experimental status of NODES is lifted.
   ### This table superseeds NODE_DATA

   For illustration, with a scenario like this:

     # (0)
     svn rm foo
     svn cp ^/moo foo   # (1)
     svn rm foo/bar
     touch foo/bar
     svn add foo/bar    # (2)

   , these are the NODES for the path foo/bar (before single-db, the
   numbering of op_depth is still a bit different):

   (0)  BASE_NODE ----->  NODES (op_depth == 0)
   (1)                    NODES (op_depth == 1) ( <----_ )
   (2)                    NODES (op_depth == 2)   <----- WORKING_NODE

   0 is the original data for foo/bar before 'svn rm foo' (if it existed).
   1 is the data for foo/bar copied in from ^/moo/bar.
   2 is the to-be-committed data for foo/bar, created by 'svn add foo/bar'.

   An 'svn revert foo/bar' would remove the NODES of (2).

 */
-- STMT_CREATE_NODES
CREATE TABLE NODES (
  /* Working copy location related fields */

  wc_id  INTEGER NOT NULL REFERENCES WCROOT (id),
  local_relpath  TEXT NOT NULL,

  /* Contains the depth (= number of path segments) of the operation
     modifying the working copy tree structure. All nodes below the root
     of the operation (aka operation root, aka oproot) affected by the
     operation will be assigned the same op_depth.

     op_depth == 0 designates the initial checkout; the BASE tree.

   */
  op_depth INTEGER NOT NULL,

  /* parent's local_relpath for aggregating children of a given parent.
     this will be "" if the parent is the wcroot.  Since a wcroot will
     never have a WORKING node the parent_relpath will never be null,
     except when op_depth == 0 and the node is a wcroot. */
  parent_relpath  TEXT,


  /* Repository location fields */

  /* When op_depth == 0, these fields refer to the repository location of the
     BASE node, the location of the initial checkout.

     When op_depth != 0, they indicate where this node was copied/moved from.
     In this case, the fields are set only on the root of the operation,
     and are NULL for all children. */
  repos_id  INTEGER REFERENCES REPOSITORY (id),
  repos_path  TEXT,
  revision  INTEGER,


  /* WC state fields */

  /* Is this node "present" or has it been excluded for some reason?

     In case 'op_depth' is equal to 0, this is part of the BASE tree; in
     that case, all presence values except 'base-deleted' are allowed.

     In case 'op_depth' is greater than 0, this is part of a layer of
     working nodes; in that case, the following presence values apply:

     Only allowed values: normal, not-present, incomplete, base-deleted,
     excluded.  (the others do not make sense for the WORKING tree)

     normal: this node has been added/copied/moved-here. There may be an
       underlying BASE node at this location, implying this is a replace.
       Scan upwards from here looking for copyfrom or moved_here values
       to detect the type of operation constructing this node.

     not-present: the node (or parent) was originally copied or moved-here.
       A subtree of that source has since been deleted. There may be
       underlying BASE node to replace. For a move-here or copy-here, the
       records are simply removed rather than switched to not-present.
       Note this reflects a deletion only. It is not possible move-away
       nodes from the WORKING tree. The purported destination would receive
       a copy from the original source of a copy-here/move-here, or if the
       nodes were plain adds, those nodes would be shifted to that target
       for addition.

     incomplete: nodes are being added into the WORKING tree, and the full
       information about this node is not (yet) present.

     base-deleted: the underlying BASE node has been marked for deletion due
       to a delete or a move-away (see the moved_to column to determine
       which), and has not been replaced.

     excluded: this node is administratively excluded (sparse WC). This must
       be a child (or grandchild etc.) of a copied directory.
  */
  presence  TEXT NOT NULL,

  /* NULL depth means "default" (typically svn_depth_infinity) */
  /* ### depth on WORKING? seems this is a BASE-only concept. how do
     ### you do "files" on an added-directory? can't really ignore
     ### the subdirs! */
  /* ### maybe a WC-to-WC copy can retain a depth?  */
  depth  TEXT,

  /* ### JF: For an old-style move, "copyfrom" info stores its source, but a
     new WC-NG "move" is intended to be a "true rename" so its copyfrom
     revision is implicit, being in effect (new head - 1) at commit time.
     For a (new) move, we need to store or deduce the copyfrom local-relpath;
     perhaps add a column called "moved_from". */

  /* Boolean value, specifying if this node was moved here (rather than just
     copied). The source of the move is specified in copyfrom_*.  */
  moved_here  INTEGER,

  /* If the underlying node was moved away (rather than just deleted), this
     specifies the local_relpath of where the BASE node was moved to.
     This is set only on the root of a move, and is NULL for all children.

     Note that moved_to never refers to *this* node. It always refers
     to the "underlying" node, whether that is BASE or a child node
     implied from a parent's move/copy.  */
  moved_to  TEXT,

  /* Repository state fields */

  /* the kind of the new node. may be "unknown" if the node is not present. */
  kind  TEXT NOT NULL,

  /* If this node was moved here or copied here, then the following fields may
     have information about their source node. See BASE_NODE.changed_* for
     more information.

     For an added or not-present node, these are null.  */
  changed_revision  INTEGER,
  changed_date      INTEGER,  /* an APR date/time (usec since 1970) */
  changed_author    TEXT,

  /* The SHA-1 checksum of the pristine text, if this node is a file and was
     moved here or copied here, else NULL. */
  checksum  TEXT,

  /* serialized skel of this node's properties. NULL if we
     have no information about the properties (a non-present node). */
  properties  BLOB,


  /* Various cache fields */

  /* The size in bytes of the working file when it had no local text
     modifications. This means the size of the text when translated from
     repository-normal format to working copy format with EOL style
     translated and keywords expanded according to the properties in the
     "properties" column of this row.

     NULL if this node is not a file or if the size has not (yet) been
     computed. */
  translated_size  INTEGER,

  /* The mod-time of the working file when it was last determined to be
     logically unmodified relative to its base, taking account of keywords
     and EOL style. This value is used in the change detection heuristic
     used by the status command.

     NULL if this node is not a file or if this info has not yet been
     determined.
   */
  last_mod_time  INTEGER,  /* an APR date/time (usec since 1970) */

  /* serialized skel of this node's dav-cache.  could be NULL if the
     node does not have any dav-cache. */
  dav_cache  BLOB,

  /* for kind==symlink, this specifies the target. */
  symlink_target  TEXT,

  /* The serialized file external information. */
  /* ### hack.  hack.  hack.
     ### This information is already stored in properties, but because the
     ### current working copy implementation is such a pain, we can't
     ### readily retrieve it, hence this temporary cache column.
     ### When it is removed, be sure to remove the extra column from
     ### the db-tests.

     ### Note: This is only here as a hack, and should *NOT* be added
     ### to any wc_db APIs.  */
  file_external  TEXT,


  PRIMARY KEY (wc_id, local_relpath, op_depth)

  );

CREATE INDEX I_NODES_PARENT ON NODES (wc_id, parent_relpath);
CREATE INDEX I_NODES_PATH ON NODES (wc_id, local_relpath);



/* ------------------------------------------------------------------------- */

/* Format 13 introduces the work queue, and erases a few columns from the
   original schema.  */
-- STMT_UPGRADE_TO_13

CREATE TABLE WORK_QUEUE (
  /* Work items are identified by this value.  */
  id  INTEGER PRIMARY KEY AUTOINCREMENT,

  /* A serialized skel specifying the work item.  */
  work  BLOB NOT NULL
  );

/* The contents of dav_cache are suspect in format 12, so it is best to just
   erase anything there.  */
UPDATE BASE_NODE SET incomplete_children=null, dav_cache=null;

PRAGMA user_version = 13;


/* ------------------------------------------------------------------------- */

/* Format 14 introduces a table for storing wc locks, and additional columns
   for storing conflict data in ACTUAL. */
-- STMT_UPGRADE_TO_14

/* The existence of a row in this table implies a write lock. */
CREATE TABLE WC_LOCK (
  /* specifies the location of this node in the local filesystem */
  wc_id  INTEGER NOT NULL  REFERENCES WCROOT (id),
  local_dir_relpath  TEXT NOT NULL,
 
  PRIMARY KEY (wc_id, local_dir_relpath)
 );

/* A skel containing the conflict details. */
ALTER TABLE ACTUAL_NODE
ADD COLUMN conflict_data  BLOB;

/* Three columns containing the checksums of older, left and right conflict
   texts.  Stored in a column to allow storing them in the pristine store */
ALTER TABLE ACTUAL_NODE
ADD COLUMN older_checksum  TEXT;

ALTER TABLE ACTUAL_NODE
ADD COLUMN left_checksum  TEXT;

ALTER TABLE ACTUAL_NODE
ADD COLUMN right_checksum  TEXT;

PRAGMA user_version = 14;


/* ------------------------------------------------------------------------- */

/* Format 15 introduces new handling for excluded nodes.  */
-- STMT_UPGRADE_TO_15

UPDATE base_node
SET
  presence = 'excluded',
  checksum = NULL, translated_size = NULL, changed_rev = NULL,
  changed_date = NULL, changed_author = NULL, depth = NULL,
  symlink_target = NULL, last_mod_time = NULL, properties = NULL,
  incomplete_children = NULL, file_external = NULL
WHERE depth = 'exclude';

/* We don't support cropping working nodes, but we might see them
   via a copy from a sparse tree. Convert them anyway to make sure
   we never see depth exclude in our database */
UPDATE working_node
SET
  presence = 'excluded',
  checksum = NULL, translated_size = NULL, changed_rev = NULL,
  changed_date = NULL, changed_author = NULL, depth = NULL,
  symlink_target = NULL, copyfrom_repos_id = NULL, copyfrom_repos_path = NULL,
  copyfrom_revnum = NULL, moved_here = NULL, moved_to = NULL,
  last_mod_time = NULL, properties = NULL, keep_local = NULL
WHERE depth = 'exclude';

PRAGMA user_version = 15;


/* ------------------------------------------------------------------------- */

/* Format 16 introduces some new columns for pristines and locks.  */
-- STMT_UPGRADE_TO_16

/* An md5 column for the pristine table. */
ALTER TABLE PRISTINE
ADD COLUMN md5_checksum  TEXT;

/* Add the locked_levels column to record the depth of a lock. */
ALTER TABLE WC_LOCK
ADD COLUMN locked_levels  INTEGER NOT NULL DEFAULT -1;

/* Default the depth of existing locks to 0. */
UPDATE wc_lock
SET locked_levels = 0;

PRAGMA user_version = 16;

/* ------------------------------------------------------------------------- */

/* Format 17 involves no schema changes, it moves the pristine files
   from .svn/text-base to .svn/pristine */

-- STMT_UPGRADE_TO_17
PRAGMA user_version = 17;

/* ------------------------------------------------------------------------- */

/* Format 18 involves no schema changes, it moves the property data from files
   into the database. */

-- STMT_UPGRADE_TO_18
PRAGMA user_version = 18;

/* ------------------------------------------------------------------------- */

/* Format YYY introduces new handling for conflict information.  */
-- format: YYY


/* ------------------------------------------------------------------------- */

/* Format 99 drops all columns not needed due to previous format upgrades.
   Before we release 1.7, these statements will be pulled into a format bump
   and all the tables will be cleaned up. We don't know what that format
   number will be, however, so we're just marking it as 99 for now.  */
-- format: 99

/* We cannot directly remove columns, so we use a temporary table instead. */
/* First create the temporary table without the undesired column(s). */
CREATE TEMPORARY TABLE BASE_NODE_BACKUP(
  wc_id  INTEGER NOT NULL,
  local_relpath  TEXT NOT NULL,
  repos_id  INTEGER,
  repos_relpath  TEXT,
  parent_relpath  TEXT,
  presence  TEXT NOT NULL,
  kind  TEXT NOT NULL,
  revnum  INTEGER,
  checksum  TEXT,
  translated_size  INTEGER,
  changed_rev  INTEGER,
  changed_date  INTEGER,
  changed_author  TEXT,
  depth  TEXT,
  symlink_target  TEXT,
  last_mod_time  INTEGER,
  properties  BLOB,
  dav_cache  BLOB,
  file_external  TEXT
);

/* Copy everything into the temporary table. */
INSERT INTO BASE_NODE_BACKUP SELECT
  wc_id, local_relpath, repos_id, repos_relpath, parent_relpath, presence,
  kind, revnum, checksum, translated_size, changed_rev, changed_date,
  changed_author, depth, symlink_target, last_mod_time, properties, dav_cache,
  file_external
FROM BASE_NODE;

/* Drop the original table. */
DROP TABLE BASE_NODE;

/* Recreate the original table, this time less the temporary columns.
   Column descriptions are same as BASE_NODE in format 12 */
CREATE TABLE BASE_NODE(
  wc_id  INTEGER NOT NULL REFERENCES WCROOT (id),
  local_relpath  TEXT NOT NULL,
  repos_id  INTEGER REFERENCES REPOSITORY (id),
  repos_relpath  TEXT,
  parent_relpath  TEXT,
  presence  TEXT NOT NULL,
  kind  TEXT NOT NULL,
  revnum  INTEGER,
  checksum  TEXT,
  translated_size  INTEGER,
  changed_rev  INTEGER,
  changed_date  INTEGER,
  changed_author  TEXT,
  depth  TEXT,
  symlink_target  TEXT,
  last_mod_time  INTEGER,
  properties  BLOB,
  dav_cache  BLOB,
  file_external  TEXT,

  PRIMARY KEY (wc_id, local_relpath)
  );

/* Recreate the index. */
CREATE INDEX I_PARENT ON BASE_NODE (wc_id, parent_relpath);

/* Copy everything back into the original table. */
INSERT INTO BASE_NODE SELECT
  wc_id, local_relpath, repos_id, repos_relpath, parent_relpath, presence,
  kind, revnum, checksum, translated_size, changed_rev, changed_date,
  changed_author, depth, symlink_target, last_mod_time, properties, dav_cache,
  file_external
FROM BASE_NODE_BACKUP;

/* Drop the temporary table. */
DROP TABLE BASE_NODE_BACKUP;

/* Now "drop" the tree_conflict_data column from actual_node. */
CREATE TABLE ACTUAL_NODE_BACKUP (
  wc_id  INTEGER NOT NULL,
  local_relpath  TEXT NOT NULL,
  parent_relpath  TEXT,
  properties  BLOB,
  conflict_old  TEXT,
  conflict_new  TEXT,
  conflict_working  TEXT,
  prop_reject  TEXT,
  changelist  TEXT,
  text_mod  TEXT
  );

INSERT INTO ACTUAL_NODE_BACKUP SELECT
  wc_id, local_relpath, parent_relpath, properties, conflict_old,
  conflict_new, conflict_working, prop_reject, changelist, text_mod
FROM ACTUAL_NODE;

DROP TABLE ACTUAL_NODE;

CREATE TABLE ACTUAL_NODE (
  wc_id  INTEGER NOT NULL REFERENCES WCROOT (id),
  local_relpath  TEXT NOT NULL,
  parent_relpath  TEXT,
  properties  BLOB,
  conflict_old  TEXT,
  conflict_new  TEXT,
  conflict_working  TEXT,
  prop_reject  TEXT,
  changelist  TEXT,
  text_mod  TEXT,

  PRIMARY KEY (wc_id, local_relpath)
  );

CREATE INDEX I_ACTUAL_PARENT ON ACTUAL_NODE (wc_id, parent_relpath);
CREATE INDEX I_ACTUAL_CHANGELIST ON ACTUAL_NODE (changelist);

INSERT INTO ACTUAL_NODE SELECT
  wc_id, local_relpath, parent_relpath, properties, conflict_old,
  conflict_new, conflict_working, prop_reject, changelist, text_mod
FROM ACTUAL_NODE_BACKUP;

DROP TABLE ACTUAL_NODE_BACKUP;
