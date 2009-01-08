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

/* ### the following tables define the BASE tree */

CREATE TABLE WCROOT (
  id  INTEGER PRIMARY KEY AUTOINCREMENT,

  /* absolute path in the local filesystem */
  local_abspath  TEXT NOT NULL
  );

CREATE UNIQUE INDEX I_LOCAL_ABSPATH ON WCROOT (local_abspath);


CREATE TABLE BASE_NODE (
  id  INTEGER PRIMARY KEY AUTOINCREMENT,

  /* the WCROOT that we are part of. NULL if the metadata is stored in
     {wcroot}/.svn/ */
  wc_id  INTEGER,

  /* relative path from wcroot */
  local_relpath  TEXT NOT NULL,

  /* URL of this node in the repository. NULL if implied by parent. for
     switched nodes, this will be non-NULL. */
  url  TEXT,

  /* UUID of the repository. NULL if implied by parent. */
  uuid  TEXT,

  /* parent node. used to aggregate all child nodes of a given parent.
     NULL for the wcroot node. */
  parent_id  INTEGER,

  revnum  INTEGER NOT NULL,

  /* file/dir/special. none is not allowed. */
  kind  INTEGER NOT NULL,

  /* NULL for a directory */
  checksum  TEXT,

  /* Information about the last change to this node */
  changed_rev  INTEGER NOT NULL,
  changed_date  INTEGER NOT NULL,  /* an APR date/time (usec since 1970) */
  changed_author  TEXT NOT NULL,

  /* NULL depth means "default" (typically svn_depth_infinity) */
  depth  INTEGER,

  /* ### note: these values are caches from the server! */
  lock_token  TEXT,
  lock_owner  TEXT,
  lock_comment  TEXT,
  lock_date  INTEGER,  /* an APR date/time (usec since 1970) */

  /* ### Do we need this?  We've currently got various mod time APIs
     ### internal to libsvn_wc, but those might be used in answering some
     ### question which is better answered some other way. */
  last_mod_time  INTEGER,  /* an APR date/time (usec since 1970) */

  /* serialized skel of this node's properties. */
  properties  BLOB
  );

CREATE UNIQUE INDEX I_PATH ON BASE_NODE (wc_id, local_relpath);
CREATE INDEX I_PARENT ON BASE_NODE (parent_id);
CREATE INDEX I_LOCKS ON BASE_NODE (lock_token);


CREATE TABLE PRISTINE (
  /* ### the hash algorithm (MD5 or SHA-1) is encoded in this value */
  checksum  TEXT NOT NULL PRIMARY KEY,

  /* ### enumerated values specifying type of compression. NULL implies
     ### that no compression has been applied. */
  compression  INTEGER,

  /* ### used to verify the pristine file is "proper". NULL if unknown,
     ### and (thus) the pristine copy is incomplete/unusable. */
  size  INTEGER,

  refcount  INTEGER NOT NULL
  );


/* ------------------------------------------------------------------------- */

/* ### the following tables define the WORKING tree */

/* ### add/delete nodes */
CREATE TABLE WORKING_NODE (
  id  INTEGER PRIMARY KEY AUTOINCREMENT, 

  /* specifies the location of this node in the local filesystem */
  wc_id  INTEGER,
  local_relpath  TEXT NOT NULL,

  /* kind==none implies this node was deleted or moved (see moved_to).
     other kinds:
       if a BASE_NODE exists at the same local_relpath, then this is a
       replaced item (possibly copied or moved here), which implies the
       base node should be deleted first. */
  kind  INTEGER NOT NULL,

  /* Where this node was copied from. */
  copyfrom_repos_path  TEXT,
  copyfrom_revnum  INTEGER,

  /* If this node was moved (rather than just copied), this specifies
     the local_relpath of the source of the move. */
  moved_from  TEXT,

  /* If this node was moved (rather than just deleted), this specifies
     where the node was moved to. */
  moved_to  TEXT,
                 
  checksum  TEXT,
  changed_rev  INTEGER,
  changed_date  INTEGER,  /* an APR date/time (usec since 1970) */
  changed_author  TEXT,

  changelist_id  INTEGER,

  /* serialized skel of this node's properties. */
  properties BLOB
  );

CREATE UNIQUE INDEX I_PATH_WORKING ON NODE_CHANGES (wc_id, local_relpath);


CREATE TABLE ACTUAL_NODE (
  id  INTEGER PRIMARY KEY AUTOINCREMENT,

  /* specifies the location of this node in the local filesystem */
  wc_id  INTEGER,
  local_relpath  TEXT NOT NULL,

  size  INTEGER,

  /* serialized skel of this node's properties. */
  properties  BLOB,

  /* ### do we want to record the revnums which caused this? */
  conflict_old  TEXT,
  conflict_new  TEXT,
  conflict_working  TEXT,
  prop_reject  TEXT,  /* ### is this right? */

  changelist_id  INTEGER
  );


CREATE TABLE CHANGELIST (
  id  INTEGER PRIMARY KEY AUTOINCREMENT,

  wc_id  INTEGER NOT NULL,

  name  TEXT NOT NULL
  );

CREATE UNIQUE INDEX I_CHANGELIST ON CHANGELIST (wc_id, name);
CREATE UNIQUE INDEX I_CL_LIST ON CHANGELIST (wc_id);
