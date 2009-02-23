/* wc-metadata.sql -- schema used in the wc-metadata SQLite database
 *     This is intended for use with SQLite 3
 *
 * ====================================================================
 * Copyright (c) 2008 CollabNet.  All rights reserved.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution.  The terms
 * are also available at http://subversion.tigris.org/license-1.html.
 * If newer versions of this license are posted there, you may use a
 * newer version instead, at your option.
 *
 * This software consists of voluntary contributions made by many
 * individuals.  For exact contribution history, see the revision
 * history and logs, available at http://subversion.tigris.org/.
 * ====================================================================
 */

/*
 * the KIND column in these tables has one of five values:
 *   "file"
 *   "dir"
 *   "symlink"
 *   "unknown"
 *
 * the PRESENCE column in these tables has one of five values:
 *   "normal"
 *   "absent" -- server has declared it "absent" (ie. authz failure)
 *   "excluded" -- administratively excluded
 *   "not-present" -- node not present at this REV
 *   "incomplete" -- state hasn't been filled in
 */


/* ------------------------------------------------------------------------- */

CREATE TABLE REPOSITORY (
  id INTEGER PRIMARY KEY AUTOINCREMENT,

  /* the root URL of the repository */
  root  TEXT UNIQUE NOT NULL,

  /* the UUID of the repository */
  uuid  TEXT UNIQUE NOT NULL
  );

CREATE UNIQUE INDEX I_UUID ON REPOSITORY (uuid);


/* ------------------------------------------------------------------------- */

CREATE TABLE WCROOT (
  id  INTEGER PRIMARY KEY AUTOINCREMENT,

  /* absolute path in the local filesystem.  NULL if storing metadata in
     the wcroot itself. */
  local_abspath  TEXT
  );

CREATE UNIQUE INDEX I_LOCAL_ABSPATH ON WCROOT (local_abspath);


/* ------------------------------------------------------------------------- */

CREATE TABLE BASE_NODE (
  id  INTEGER PRIMARY KEY AUTOINCREMENT,

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

  /* is this node "present" or has it been excluded for some reason? */
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

  /* Information about the last change to this node. these could be
     NULL for non-present nodes, when we have no info. */
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

  /* this node is a directory, and all of its child nodes have not (yet)
     been created [for this revision number]. Note: boolean value. */
  /* ### this will probably disappear in favor of incomplete child
     ### nodes */
  incomplete_children  INTEGER
  );

CREATE UNIQUE INDEX I_PATH ON BASE_NODE (wc_id, local_relpath);
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
  id  INTEGER PRIMARY KEY AUTOINCREMENT, 

  /* specifies the location of this node in the local filesystem */
  wc_id  INTEGER NOT NULL,
  local_relpath  TEXT NOT NULL,

  /* parent's local_relpath for aggregating children of a given parent.
     this will be "" if the parent is the wcroot. */
  parent_relpath  TEXT NOT NULL,

  /* is this node "present" or has it been excluded for some reason?
     only allowed values: normal, not-present, incomplete. (the others
     do not make sense for the WORKING tree)

     presence=not-present means this node has been deleted or moved
     (see moved_to). for presence=normal: if a BASE_NODE exists at
     the same local_relpath, then this is a replaced item (possibly
     copied or moved here), which implies the base node should be
     deleted or moved (see moved_to).

     beware: a "not-present" value could refer to the deletion of the
     BASE node, or it could refer to the deletion of a child of a
     copied/moved tree (scan upwards for copyfrom data). */
  presence  TEXT NOT NULL,

  /* the kind of the new node. may be "unknown" if the node is not present. */
  kind  TEXT NOT NULL,

  /* if this node was added-with-history AND is a file, then the checksum
     and its translated size (given the properties on this file) are
     specified by the following two fields. translated_size may be NULL
     if the size has not (yet) been computed. */
  checksum  TEXT,
  translated_size  INTEGER,

  /* if this node was added-with-history, then the following fields will
     be NOT NULL */
  changed_rev  INTEGER,
  changed_date  INTEGER,  /* an APR date/time (usec since 1970) */
  changed_author  TEXT,

  /* NULL depth means "default" (typically svn_depth_infinity) */
  /* ### depth on WORKING? seems this is a BASE-only concept. how do
     ### you do "files" on an added-directory? can't really ignore
     ### the subdirs! */
  depth  TEXT,

  /* for kind==symlink, this specifies the target. */
  symlink_target  TEXT,

  /* Where this node was copied from. Set only on the root of the copy,
     and implied for all children. */
  copyfrom_repos_id  INTEGER,
  copyfrom_repos_path  TEXT,
  copyfrom_revnum  INTEGER,

  /* If this node was moved (rather than just copied), this specifies
     the local_relpath of the source of the move. */
  moved_from  TEXT,

  /* If this node was moved (rather than just deleted), this specifies
     where the BASE node was moved to.

     ### uh oh. what if the BASE is moved, then a directory is copied
     ### here, then a child is moved? does "moved_to" apply to the BASE,
     ### to to the replacing nodes?
     ### answer: only use moved_to for the *root* of a moved tree. thus,
     ### any moves below that point *must* apply to the replacing nodes. */
  moved_to  TEXT,

  /* ### Do we need this?  We've currently got various mod time APIs
     ### internal to libsvn_wc, but those might be used in answering some
     ### question which is better answered some other way. */
  last_mod_time  INTEGER,  /* an APR date/time (usec since 1970) */

  /* serialized skel of this node's properties. could be NULL if we
     have no information about the properties (a non-present node). */
  properties  BLOB
  );

CREATE UNIQUE INDEX I_WORKING_PATH ON WORKING_NODE (wc_id, local_relpath);
CREATE INDEX I_WORKING_PARENT ON WORKING_NODE (wc_id, parent_relpath);


/* ------------------------------------------------------------------------- */

CREATE TABLE ACTUAL_NODE (
  id  INTEGER PRIMARY KEY AUTOINCREMENT,

  /* specifies the location of this node in the local filesystem */
  wc_id  INTEGER NOT NULL,
  local_relpath  TEXT NOT NULL,

  /* parent's local_relpath for aggregating children of a given parent.
     this will be "" if the parent is the wcroot. */
  parent_relpath  TEXT NOT NULL,

  /* serialized skel of this node's properties. NULL implies no change to
     the properties, relative to WORKING/BASE as appropriate. */
  properties  BLOB,

  /* ### do we want to record the revnums which caused this? */
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

  /* if a directory, serialized data for all of tree conflicts therein. */
  tree_conflict_data  TEXT
  );

CREATE UNIQUE INDEX I_ACTUAL_PATH ON ACTUAL_NODE (wc_id, local_relpath);
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
