/* wc-metadata.sql -- schema used in the wc-metadata SQLite database
 *     This is intended for use with SQLite 3
 *
 * ====================================================================
 *    Licensed to the Subversion Corporation (SVN Corp.) under one
 *    or more contributor license agreements.  See the NOTICE file
 *    distributed with this work for additional information
 *    regarding copyright ownership.  The SVN Corp. licenses this file
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
 * the KIND column in these tables has one of the following values:
 *   "file"
 *   "dir"
 *   "symlink"
 *   "unknown"
 *   "subdir"
 *
 * the PRESENCE column in these tables has one of the following values:
 *   "normal"
 *   "absent" -- server has declared it "absent" (ie. authz failure)
 *   "excluded" -- administratively excluded
 *   "not-present" -- node not present at this REV
 *   "incomplete" -- state hasn't been filled in
 *   "base-deleted" -- node represents a delete of a BASE node
 */

/* All the SQL below is for format 12: SVN_WC__WC_NG_VERSION  */
-- format: 12

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

CREATE TABLE BASE_NODE (
  /* specifies the location of this node in the local filesystem. wc_id
     implies an absolute path, and local_relpath is relative to that
     location (meaning it will be "" for the wcroot). */
  wc_id  INTEGER NOT NULL,
  local_relpath  TEXT NOT NULL,

  /* the repository this node is part of, and the relative path [to its
     root] within that repository.  these may be NULL, implying it should
     be derived from the parent and local_relpath.  non-NULL typically
     indicates a switched node.

     Note: they must both be NULL, or both non-NULL. */
  repos_id  INTEGER,
  repos_relpath  TEXT,

  /* parent's local_relpath for aggregating children of a given parent.
     this will be "" if the parent is the wcroot. NULL if this is the
     wcroot node. */
  parent_relpath  TEXT,

  /* Is this node "present" or has it been excluded for some reason?
     The "base-deleted" presence value is not allowed.  */
  presence  TEXT NOT NULL,

  /* what kind of node is this? may be "unknown" if the node is not present */
  kind  TEXT NOT NULL,

  /* this could be NULL for non-present nodes -- no info. */
  revnum  INTEGER,

  /* if this node is a file, then the checksum and its translated size
     (given the properties on this file) are specified by the following
     two fields. translated_size may be NULL if the size has not (yet)
     been computed. */
  checksum  TEXT,
  translated_size  INTEGER,

  /* Information about the last change to this node. changed_rev must be
     not-null if this node has presence=="normal". changed_date and
     changed_author may be null if the corresponding revprops are missing.

     All three values may be null for non-present nodes.  */
  changed_rev  INTEGER,
  changed_date  INTEGER,  /* an APR date/time (usec since 1970) */
  changed_author  TEXT,

  /* NULL depth means "default" (typically svn_depth_infinity) */
  depth  TEXT,

  /* for kind==symlink, this specifies the target. */
  symlink_target  TEXT,

  /* ### Do we need this?  We've currently got various mod time APIs
     ### internal to libsvn_wc, but those might be used in answering some
     ### question which is better answered some other way. */
  last_mod_time  INTEGER,  /* an APR date/time (usec since 1970) */

  /* serialized skel of this node's properties. could be NULL if we
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

CREATE TABLE PRISTINE (
  /* ### the hash algorithm (MD5 or SHA-1) is encoded in this value */
  checksum  TEXT NOT NULL PRIMARY KEY,

  /* ### enumerated values specifying type of compression. NULL implies
     ### that no compression has been applied. */
  compression  INTEGER,

  /* ### used to verify the pristine file is "proper". NULL if unknown,
     ### and (thus) the pristine copy is incomplete/unusable. */
  size  INTEGER,

  /* ### this will probably go away, in favor of counting references
     ### that exist in BASE_NODE and WORKING_NODE. */
  refcount  INTEGER NOT NULL
  );


/* ------------------------------------------------------------------------- */

CREATE TABLE WORKING_NODE (
  /* specifies the location of this node in the local filesystem */
  wc_id  INTEGER NOT NULL,
  local_relpath  TEXT NOT NULL,

  /* parent's local_relpath for aggregating children of a given parent.
     this will be "" if the parent is the wcroot. NULL if this is the
     wcroot node. */
  parent_relpath  TEXT,

  /* Is this node "present" or has it been excluded for some reason?
     Only allowed values: normal, not-present, incomplete, base-deleted.
     (the others do not make sense for the WORKING tree)

     normal: this node has been added/copied/moved-here. There may be an
       underlying BASE node at this location, implying this is a replace.
       Scan upwards from here looking for copyfrom or moved_here values
       to detect the type of operation constructing this node.

     not-present: the node (or parent) was originally copied or moved-here.
       A subtree of that source has since been deleted. There may be
       underlying BASE node to replace. For an add-without-history, the
       records are simply removed rather than switched to not-present.
       Note this reflects a deletion only. It is not possible move-away
       nodes from the WORKING tree. The purported destination would receive
       a copy from the original source of a copy-here/move-here, or if the
       nodes were plain adds, those nodes would be shifted to that target
       for addition.

     incomplete: nodes are being added into the WORKING tree, and the full
       information about this node is not (yet) present.

     base-delete: the underlying BASE node has been marked for deletion due
       to a delete or a move-away (see the moved_to column to determine).  */
  presence  TEXT NOT NULL,

  /* the kind of the new node. may be "unknown" if the node is not present. */
  kind  TEXT NOT NULL,

  /* if this node was added-with-history AND is a file, then the checksum
     and its translated size (given the properties on this file) are
     specified by the following two fields. translated_size may be NULL
     if the size has not (yet) been computed. */
  checksum  TEXT,
  translated_size  INTEGER,

  /* If this node was added-with-history, then the following fields may
     have information about their source node. See BASE_NODE.changed_* for
     more information.

     For added or not-present nodes, these may be null.  */
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

  /* Where this node was copied/moved from. Set only on the root of the
     operation, and implied for all children. */
  copyfrom_repos_id  INTEGER,
  copyfrom_repos_path  TEXT,
  copyfrom_revnum  INTEGER,

  /* Boolean value, specifying if this node was moved here (rather than just
     copied). The source of the move is specified in copyfrom_*.  */
  moved_here  INTEGER,

  /* If the underlying node was moved (rather than just deleted), this
     specifies the local_relpath of where the BASE node was moved to.
     This is set only on the root of a move, and implied for all children.
     The whole moved subtree is marked with presence=base-deleted

     Note that moved_to never refers to *this* node. It always refers
     to the "underlying" node, whether that is BASE or a child node
     implied from a parent's move/copy.  */
  moved_to  TEXT,

  /* ### Do we need this?  We've currently got various mod time APIs
     ### internal to libsvn_wc, but those might be used in answering some
     ### question which is better answered some other way. */
  last_mod_time  INTEGER,  /* an APR date/time (usec since 1970) */

  /* serialized skel of this node's properties. could be NULL if we
     have no information about the properties (a non-present node). */
  properties  BLOB,

  /* should the node on disk be kept after a schedule delete?

     ### Bert points out that this can disappear once we get centralized 
     ### with our metadata.  The entire reason for this flag to exist is
     ### so that the admin area can exist for the commit of a the delete,
     ### and so the post-commit cleanup knows not to actually delete the dir
     ### from disk (which is why the flag is only ever set on the this_dir
     ### entry in WC-OLD.)  In the New World, we don't need to keep the old
     ### admin area around, so this flag can disappear. */
  keep_local  INTEGER,

  PRIMARY KEY (wc_id, local_relpath)
  );

CREATE INDEX I_WORKING_PARENT ON WORKING_NODE (wc_id, parent_relpath);


/* ------------------------------------------------------------------------- */

CREATE TABLE ACTUAL_NODE (
  /* specifies the location of this node in the local filesystem */
  wc_id  INTEGER NOT NULL,
  local_relpath  TEXT NOT NULL,

  /* parent's local_relpath for aggregating children of a given parent.
     this will be "" if the parent is the wcroot. NULL if this is the
     wcroot node. */
  parent_relpath  TEXT,

  /* serialized skel of this node's properties. NULL implies no change to
     the properties, relative to WORKING/BASE as appropriate. */
  properties  BLOB,

  /* ### do we want to record the revnums which caused this? */
  /* ### also, shouldn't these be absolute paths?
     ### they aren't currently, but that would be more consistent with other
     ### columns. (though it would require a format bump) */
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
     removed in format 13, in favor of the TREE_CONFLICT_VICTIM table*/
  tree_conflict_data  TEXT,

  PRIMARY KEY (wc_id, local_relpath)
  );

CREATE INDEX I_ACTUAL_PARENT ON ACTUAL_NODE (wc_id, parent_relpath);
CREATE INDEX I_ACTUAL_CHANGELIST ON ACTUAL_NODE (changelist);


/* ------------------------------------------------------------------------- */

CREATE TABLE LOCK (
  /* what repository location is locked */
  repos_id  INTEGER NOT NULL,
  repos_relpath  TEXT NOT NULL,

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

/* Format 13 introduces the work queue, and erases a few columns from the
   original schema.  */
-- format: 13
CREATE TABLE WORK_QUEUE (
  /* Work items are identified by this value.  */
  id  INTEGER PRIMARY KEY AUTOINCREMENT,

  /* A serialized skel specifying the work item.  */
  work  BLOB NOT NULL
  );

CREATE TABLE CONFLICT_VICTIM (
  /* specifies the location of this node in the local filesystem */
  wc_id  INTEGER NOT NULL,
  local_relpath  TEXT NOT NULL,

  /* parent's local_relpath for aggregating children of a given parent.
     this will be "" if the parent is the wcroot. NULL if this is the
     wcroot node. */
  parent_relpath  TEXT,
  
  /* what kind of node is this? may be "unknown" if the node is not present */
  node_kind  TEXT NOT NULL,

  /* what sort of conflict are we describing? */
  conflict_kind  TEXT NOT NULL,

  /* the name of the property in conflict, or NULL */
  property_name  TEXT,

  /* conflict information, if kind is 'text' */
  conflict_action  TEXT,
  conflict_reason  TEXT,

  /* operation which exposed the conflict, if kind is 'tree' */
  operation  TEXT,

  /* the 'merge-left' source, or 'older' version of the incoming change. */
  left_repos_id  INTEGER,
  left_repos_relpath  TEXT,
  left_peg_rev  INTEGER,
  left_kind  TEXT,

  /* the 'merge-right' source, or 'their' version of the incoming change. */
  right_repos_id  INTEGER,
  right_repos_relpath  TEXT,
  right_peg_rev  INTEGER,
  right_kind  TEXT,

  PRIMARY KEY (wc_id, local_relpath)
  );

CREATE INDEX I_TCPARENT ON CONFLICT_VICTIM (wc_id, parent_relpath);


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

/* The contents of dav_cache are suspect in format 12, so it is best to just
   erase anything there.  */
UPDATE BASE_NODE SET incomplete_children=null, dav_cache=null;


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
  wc_id  INTEGER NOT NULL,
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
