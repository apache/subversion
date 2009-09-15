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
     been computed. The kind of checksum (e.g. SHA-1, MD5) is stored in the
     value */
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
/* ### BH: Will CHECKSUM be the same key as used for indexing a file in the
           Pristine store? If that key is SHA-1 we might need an alternative
           MD5 checksum column on this table to use with the current delta
           editors that don't understand SHA-1. */
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
  /* ### BH: Should we call this copyfrom_repos_relpath and skip the initial '/'
     ### to match the other repository paths used in sqlite and to make it easier
     ### to join these paths? */
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

  /* basenames of the conflict files. */
  /* ### do we want to record the revnums which caused this?  
     ### BH: Yes, probably urls too if it is caused by a merge. Preferably
     ###     the same info as currently passed to the interactive conflict
     ###     handler. I would like url@rev for left, right and original, but
     ###     some of those are available in other ways. Refer to repository
     ###     table instead of full urls? .*/
  /* ### also, shouldn't these be local_relpaths too?
     ### they aren't currently, but that would be more consistent with other
     ### columns. (though it would require a format bump). */
  /* ### BH: Shouldn't we move all these into the new CONFLICT_VICTIM table? */
  /* ### HKW: I think so.  These columns pre-date that table, and are just
     ###      a mapping from svn_wc_entry_t.  I haven't thought about how the
     ###      CONFLICT_VICTIM table would need to be extended for this, though.
     ###      (may want do to that before the f13 bump, if possible) */
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
     removed in format 13, in favor of the CONFLICT_VICTIM table*/
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

/* Format 13 introduces the work queue, and erases a few columns from the
   original schema.  */
-- format: 13
CREATE TABLE WORK_QUEUE (
  /* Work items are identified by this value.  */
  id  INTEGER PRIMARY KEY AUTOINCREMENT,

  /* A serialized skel specifying the work item.  */
  work  BLOB NOT NULL
  );

/* The contents of dav_cache are suspect in format 12, so it is best to just
   erase anything there.  */
UPDATE BASE_NODE SET incomplete_children=null, dav_cache=null;


/* ------------------------------------------------------------------------- */

/* Format 14 introduces new handling for conflict information.  */
-- format: 14

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

  /* what sort of conflict are we describing?
     "text", "property" or "tree" */
  conflict_kind  TEXT NOT NULL,

  /* the name of the property in conflict, or NULL */
  property_name  TEXT,

  /* conflict information, if kind is 'text' */
  conflict_action  TEXT,
  conflict_reason  TEXT,

  /* operation which exposed the conflict, if kind is 'tree' */
  operation  TEXT,
  
  /* ### BH: Add original/base version? */
  /* ### BH: Add relpath for conflict files? Or just basename */
  /* ### BH: Add checksum to allow referring to pristine? */
  /* ### BH: How to handle the .prej file? (Multiple property conflicts?) */
  /* the 'base' version of the incoming change. */
/*base_repos_id  INTEGER,
  base_repos_relpath  TEXT,
  base_peg_rev  INTEGER,
  base_kind  TEXT,
  base_local_relpath  TEXT,
  base_checksum  TEXT */

  /* the 'merge-left' source, 'older' version of the incoming change. */
  left_repos_id  INTEGER,
  left_repos_relpath  TEXT,
  left_peg_rev  INTEGER,
  left_kind  TEXT,
/*left_local_relpath  TEXT,
  left_checksum  TEXT, */

  /* the 'merge-right' source, or 'their' version of the incoming change. */
  right_repos_id  INTEGER,
  right_repos_relpath  TEXT,
  right_peg_rev  INTEGER,
  right_kind  TEXT,
/*right_local_relpath  TEXT,
  right_checksum  TEXT, */

  /* ### BH: Add conflict kind? Add property name? Primary key should be
         unique */
  PRIMARY KEY (wc_id, local_relpath)
  );

CREATE INDEX I_CVPARENT ON CONFLICT_VICTIM (wc_id, parent_relpath);


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


/* ------------------------------------------------------------------------- */

/* these are used in wc_db.c  */

-- STMT_SELECT_BASE_NODE
select wc_id, local_relpath, repos_id, repos_relpath,
  presence, kind, revnum, checksum, translated_size,
  changed_rev, changed_date, changed_author, depth, symlink_target,
  last_mod_time
from base_node
where wc_id = ?1 and local_relpath = ?2;

-- STMT_SELECT_BASE_NODE_WITH_LOCK
select wc_id, local_relpath, base_node.repos_id, base_node.repos_relpath,
  presence, kind, revnum, checksum, translated_size,
  changed_rev, changed_date, changed_author, depth, symlink_target,
  last_mod_time,
  lock_token, lock_owner, lock_comment, lock_date
from base_node
left outer join lock on base_node.repos_id = lock.repos_id
  and base_node.repos_relpath = lock.repos_relpath
where wc_id = ?1 and local_relpath = ?2;

-- STMT_SELECT_WORKING_NODE
select presence, kind, checksum, translated_size,
  changed_rev, changed_date, changed_author, depth, symlink_target,
  copyfrom_repos_id, copyfrom_repos_path, copyfrom_revnum,
  moved_here, moved_to, last_mod_time
from working_node
where wc_id = ?1 and local_relpath = ?2;

-- STMT_SELECT_ACTUAL_NODE
select prop_reject, changelist, conflict_old, conflict_new,
conflict_working, tree_conflict_data
from actual_node
where wc_id = ?1 and local_relpath = ?2;

-- STMT_SELECT_REPOSITORY_BY_ID
select root, uuid from repository where id = ?1;

-- STMT_SELECT_WCROOT_NULL
select id from wcroot where local_abspath is null;

-- STMT_SELECT_REPOSITORY
select id from repository where root = ?1;

-- STMT_INSERT_REPOSITORY
insert into repository (root, uuid) values (?1, ?2);

-- STMT_INSERT_BASE_NODE
insert or replace into base_node (
  wc_id, local_relpath, repos_id, repos_relpath, parent_relpath, presence,
  kind, revnum, properties, changed_rev, changed_date, changed_author,
  depth, checksum, translated_size, symlink_target)
values (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14,
        ?15, ?16);

-- STMT_INSERT_BASE_NODE_INCOMPLETE
insert or ignore into base_node (
  wc_id, local_relpath, parent_relpath, presence, kind, revnum)
values (?1, ?2, ?3, 'incomplete', 'unknown', ?5);

-- STMT_SELECT_BASE_NODE_CHILDREN
select local_relpath from base_node
where wc_id = ?1 and parent_relpath = ?2;

-- STMT_SELECT_WORKING_CHILDREN
select local_relpath from base_node
where wc_id = ?1 and parent_relpath = ?2
union
select local_relpath from working_node
where wc_id = ?1 and parent_relpath = ?2;

-- STMT_SELECT_WORKING_IS_FILE
select kind == 'file' from working_node
where wc_id = ?1 and local_relpath = ?2;

-- STMT_SELECT_BASE_IS_FILE
select kind == 'file' from base_node
where wc_id = ?1 and local_relpath = ?2;

-- STMT_SELECT_BASE_PROPS
select properties from base_node
where wc_id = ?1 and local_relpath = ?2;

-- STMT_UPDATE_ACTUAL_PROPS
update actual_node set properties = ?3
where wc_id = ?1 and local_relpath = ?2;

-- STMT_SELECT_ALL_PROPS
select actual_node.properties, working_node.properties,
  base_node.properties
from base_node
left outer join working_node on base_node.wc_id = working_node.wc_id
  and base_node.local_relpath = working_node.local_relpath
left outer join actual_node on base_node.wc_id = actual_node.wc_id
  and base_node.local_relpath = actual_node.local_relpath
where base_node.wc_id = ?1 and base_node.local_relpath = ?2;

-- STMT_SELECT_PRISTINE_PROPS
select working_node.properties, base_node.properties
from base_node
left outer join working_node on base_node.wc_id = working_node.wc_id
  and base_node.local_relpath = working_node.local_relpath
where base_node.wc_id = ?1 and base_node.local_relpath = ?2;

-- STMT_INSERT_LOCK
insert or replace into lock
(repos_id, repos_relpath, lock_token, lock_owner, lock_comment,
 lock_date)
values (?1, ?2, ?3, ?4, ?5, ?6);

-- STMT_INSERT_WCROOT
insert into wcroot (local_abspath)
values (?1);

-- STMT_UPDATE_BASE_DAV_CACHE
update base_node set dav_cache = ?3
where wc_id = ?1 and local_relpath = ?2;

-- STMT_SELECT_BASE_DAV_CACHE
select dav_cache from base_node
where wc_id = ?1 and local_relpath = ?2;

-- STMT_SELECT_DELETION_INFO
select base_node.presence, working_node.presence, moved_to
from working_node
left outer join base_node on base_node.wc_id = working_node.wc_id
  and base_node.local_relpath = working_node.local_relpath
where working_node.wc_id = ?1 and working_node.local_relpath = ?2;

-- STMT_SELECT_PARENT_STUB_INFO
select presence = 'not-present', revnum from base_node
where wc_id = ?1 and local_relpath = ?2;

-- STMT_DELETE_LOCK
delete from lock
where repos_id = ?1 and repos_relpath = ?2;

-- STMT_UPDATE_BASE_RECURSIVE_REPO
update base_node set repos_id = ?4
where repos_id is not null and wc_id = ?1 and
  (local_relpath = ?2 or
   local_relpath like ?3 escape '#');

-- STMT_UPDATE_WORKING_RECURSIVE_COPYFROM_REPO
update working_node set copyfrom_repos_id = ?4
where copyfrom_repos_id is not null and wc_id = ?1 and
  (local_relpath = ?2 or
   local_relpath like ?3 escape '#');

-- STMT_UPDATE_LOCK_REPOS_ID
update lock set repos_id = ?4
where repos_id = ?1 and
  (repos_relpath = ?2 or
   repos_relpath like ?3 escape '#');

-- STMT_UPDATE_BASE_LAST_MOD_TIME
update base_node set last_mod_time = ?3
where wc_id = ?1 and local_relpath = ?2;

-- STMT_UPDATE_ACTUAL_TREE_CONFLICTS
update actual_node set tree_conflict_data = ?3
where wc_id = ?1 and local_relpath = ?2;

-- STMT_INSERT_ACTUAL_TREE_CONFLICTS
insert into actual_node (
  wc_id, local_relpath, tree_conflict_data)
values (?1, ?2, ?3);

-- STMT_UPDATE_ACTUAL_CHANGELIST
update actual_node set changelist = ?3
where wc_id = ?1 and local_relpath = ?2;

-- STMT_INSERT_ACTUAL_CHANGELIST
insert into actual_node (
  wc_id, local_relpath, changelist)
values (?1, ?2, ?3);

-- STMT_DELETE_BASE_NODE
delete from base_node
where wc_id = ?1 and local_relpath = ?2;

-- STMT_DELETE_WORKING_NODE
delete from working_node
where wc_id = ?1 and local_relpath = ?2;

-- STMT_DELETE_ACTUAL_NODE
delete from actual_node
where wc_id = ?1 and local_relpath = ?2;

-- STMT_UPDATE_BASE_DEPTH
update base_node set depth = ?3
where wc_id = ?1 and local_relpath = ?2;

-- STMT_UPDATE_WORKING_DEPTH
update working_node set depth = ?3
where wc_id = ?1 and local_relpath = ?2;

/* ------------------------------------------------------------------------- */

/* these are used in entries.c  */

-- STMT_INSERT_BASE_NODE_FOR_ENTRY
insert or replace into base_node (
  wc_id, local_relpath, repos_id, repos_relpath, parent_relpath,
  presence,
  revnum, kind, checksum, translated_size, changed_rev, changed_date,
  changed_author, depth, last_mod_time, properties)
values (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14,
  ?15, ?16);

-- STMT_INSERT_WORKING_NODE
insert or replace into working_node (
  wc_id, local_relpath, parent_relpath, presence, kind,
  copyfrom_repos_id,
  copyfrom_repos_path, copyfrom_revnum, moved_here, moved_to, checksum,
  translated_size, changed_rev, changed_date, changed_author, depth,
  last_mod_time, properties, keep_local)
values (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14,
  ?15, ?16, ?17, ?18, ?19);

-- STMT_INSERT_ACTUAL_NODE
insert or replace into actual_node (
  wc_id, local_relpath, parent_relpath, properties, conflict_old,
  conflict_new,
  conflict_working, prop_reject, changelist, text_mod,
  tree_conflict_data)
values (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11);

-- STMT_DELETE_ALL_WORKING
delete from working_node;

-- STMT_DELETE_ALL_BASE
delete from base_node;

-- STMT_DELETE_ALL_ACTUAL
delete from actual_node;

-- STMT_SELECT_KEEP_LOCAL_FLAG
select keep_local from working_node
where wc_id = ?1 and local_relpath = ?2;

-- STMT_SELECT_NOT_PRESENT
select 1 from base_node
where wc_id = ?1 and local_relpath = ?2 and presence = 'not-present';

-- STMT_SELECT_FILE_EXTERNAL
select file_external from base_node
where wc_id = ?1 and local_relpath = ?2;

-- STMT_UPDATE_FILE_EXTERNAL
update base_node set file_external = ?3
where wc_id = ?1 and local_relpath = ?2;


/* ------------------------------------------------------------------------- */

/* these are used in upgrade.c  */

-- STMT_SELECT_OLD_TREE_CONFLICT
select wc_id, local_relpath, tree_conflict_data
from actual_node
where tree_conflict_data is not null;

-- STMT_INSERT_NEW_CONFLICT
insert into conflict_victim (
  wc_id, local_relpath, parent_relpath, node_kind, conflict_kind,
  property_name, conflict_action, conflict_reason, operation,
  left_repos_id, left_repos_relpath, left_peg_rev, left_kind,
  right_repos_id, right_repos_relpath, right_peg_rev, right_kind)
values (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14, ?15,
  ?16, ?17);

-- STMT_ERASE_OLD_CONFLICTS
update actual_node set tree_conflict_data = null;


/* ------------------------------------------------------------------------- */
