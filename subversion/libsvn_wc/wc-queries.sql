/* wc-queries.sql -- queries used to interact with the wc-metadata
 *                   SQLite database
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

/* ------------------------------------------------------------------------- */

/* these are used in wc_db.c  */

-- STMT_SELECT_BASE_NODE
SELECT repos_id, repos_relpath, presence, kind, revnum, checksum,
  translated_size, changed_rev, changed_date, changed_author, depth,
  symlink_target, last_mod_time, properties
FROM base_node
WHERE wc_id = ?1 AND local_relpath = ?2;

-- STMT_SELECT_BASE_NODE_1
SELECT repos_id, repos_path, presence, kind, revision, checksum,
  translated_size, changed_revision, changed_date, changed_author, depth,
  symlink_target, last_mod_time, properties
FROM nodes
WHERE wc_id = ?1 AND local_relpath = ?2 AND op_depth = 0;

-- STMT_SELECT_BASE_NODE_WITH_LOCK
SELECT base_node.repos_id, base_node.repos_relpath, presence, kind,
  revnum, checksum, translated_size, changed_rev, changed_date,
  changed_author, depth, symlink_target, last_mod_time, properties,
  lock_token, lock_owner, lock_comment, lock_date
FROM base_node
LEFT OUTER JOIN lock ON base_node.repos_id = lock.repos_id
  AND base_node.repos_relpath = lock.repos_relpath
WHERE wc_id = ?1 AND local_relpath = ?2;

-- STMT_SELECT_BASE_NODE_WITH_LOCK_1
SELECT nodes.repos_id, nodes.repos_path, presence, kind, revision,
  checksum, translated_size, changed_revision, changed_date, changed_author,
  depth, symlink_target, last_mod_time, properties, lock_token, lock_owner,
  lock_comment, lock_date
FROM nodes
LEFT OUTER JOIN lock ON nodes.repos_id = lock.repos_id
  AND nodes.repos_path = lock.repos_relpath
WHERE wc_id = ?1 AND local_relpath = ?2 AND op_depth = 0;

-- STMT_SELECT_WORKING_NODE
SELECT presence, kind, checksum, translated_size,
  changed_rev, changed_date, changed_author, depth, symlink_target,
  copyfrom_repos_id, copyfrom_repos_path, copyfrom_revnum,
  moved_here, moved_to, last_mod_time, properties
FROM working_node
WHERE wc_id = ?1 AND local_relpath = ?2;

-- STMT_SELECT_WORKING_NODE_1
SELECT presence, kind, checksum, translated_size,
  changed_revision, changed_date, changed_author, depth, symlink_target,
  repos_id, repos_path, revision,
  moved_here, moved_to, last_mod_time, properties
FROM nodes
WHERE wc_id = ?1 AND local_relpath = ?2 AND op_depth > 0
ORDER BY op_depth DESC
LIMIT 1;

-- STMT_SELECT_ACTUAL_NODE
SELECT prop_reject, changelist, conflict_old, conflict_new,
conflict_working, tree_conflict_data, properties
FROM actual_node
WHERE wc_id = ?1 AND local_relpath = ?2;

-- STMT_SELECT_REPOSITORY_BY_ID
SELECT root, uuid FROM repository WHERE id = ?1;

-- STMT_SELECT_WCROOT_NULL
SELECT id FROM wcroot WHERE local_abspath IS NULL;

-- STMT_SELECT_REPOSITORY
SELECT id FROM repository WHERE root = ?1;

-- STMT_INSERT_REPOSITORY
INSERT INTO repository (root, uuid) VALUES (?1, ?2);

-- STMT_INSERT_BASE_NODE
INSERT OR replace INTO base_node (
  wc_id, local_relpath, repos_id, repos_relpath, parent_relpath, presence,
  kind, revnum, properties, changed_rev, changed_date, changed_author,
  depth, checksum, translated_size, symlink_target, dav_cache)
VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14,
        ?15, ?16, ?17);

-- STMT_INSERT_NODE
INSERT OR REPLACE INTO nodes (
  wc_id, local_relpath, op_depth, parent_relpath, repos_id, repos_path,
  revision, presence, depth, kind, changed_revision, changed_date,
  changed_author, checksum, properties, translated_size, last_mod_time,
  dav_cache, symlink_target )
VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14,
        ?15, ?16, ?17, ?18, ?19);

-- STMT_INSERT_BASE_NODE_INCOMPLETE
INSERT OR IGNORE INTO base_node (
  wc_id, local_relpath, parent_relpath, presence, kind, revnum)
VALUES (?1, ?2, ?3, 'incomplete', 'unknown', ?4);

-- STMT_INSERT_BASE_NODE_INCOMPLETE_DIR
INSERT OR IGNORE INTO base_node (
  wc_id, local_relpath, repos_id, repos_relpath, parent_relpath, presence,
  kind, revnum, depth)
VALUES (?1, ?2, ?3, ?4, ?5, 'incomplete', 'dir', ?6, ?7);

-- STMT_INSERT_WORKING_NODE_INCOMPLETE
INSERT OR IGNORE INTO working_node (
  wc_id, local_relpath, parent_relpath, presence, kind)
VALUES (?1, ?2, ?3, 'incomplete', 'unknown');

-- STMT_SELECT_BASE_NODE_CHILDREN
SELECT local_relpath FROM base_node
WHERE wc_id = ?1 AND parent_relpath = ?2;

-- STMT_SELECT_BASE_NODE_CHILDREN_1
SELECT local_relpath FROM nodes
WHERE wc_id = ?1 AND parent_relpath = ?2 AND op_depth = 0;

-- STMT_SELECT_WORKING_NODE_CHILDREN
SELECT local_relpath FROM working_node
WHERE wc_id = ?1 AND parent_relpath = ?2;

-- STMT_SELECT_WORKING_NODE_CHILDREN_1
SELECT DISTINCT local_relpath FROM nodes
WHERE wc_id = ?1 AND parent_relpath = ?2 AND op_depth > 0;

-- STMT_SELECT_WORKING_IS_FILE
SELECT kind == 'file' FROM working_node
WHERE wc_id = ?1 AND local_relpath = ?2;

-- STMT_SELECT_BASE_IS_FILE
SELECT kind == 'file' FROM base_node
WHERE wc_id = ?1 AND local_relpath = ?2;

-- STMT_SELECT_BASE_PROPS
SELECT properties FROM base_node
WHERE wc_id = ?1 AND local_relpath = ?2;

-- STMT_SELECT_BASE_PROPS_1
SELECT properties FROM nodes
WHERE wc_id = ?1 AND local_relpath = ?2 AND op_depth = 0;

-- STMT_SELECT_WORKING_PROPS
SELECT properties, presence FROM working_node
WHERE wc_id = ?1 AND local_relpath = ?2;

-- STMT_SELECT_WORKING_PROPS_1
SELECT properties, presence FROM nodes
WHERE wc_id = ?1 AND local_relpath = ?2 AND op_depth > 0
ORDER BY op_depth DESC
LIMIT 1;

-- STMT_SELECT_ACTUAL_PROPS
SELECT properties FROM actual_node
WHERE wc_id = ?1 AND local_relpath = ?2;

-- STMT_UPDATE_BASE_PROPS
UPDATE base_node SET properties = ?3
WHERE wc_id = ?1 AND local_relpath = ?2;

-- STMT_UPDATE_NODE_BASE_PROPS
UPDATE nodes SET properties = ?3
WHERE wc_id = ?1 AND local_relpath = ?2 AND op_depth = 0;

-- STMT_UPDATE_WORKING_PROPS
UPDATE working_node SET properties = ?3
WHERE wc_id = ?1 AND local_relpath = ?2;

-- STMT_UPDATE_NODE_WORKING_PROPS
UPDATE nodes SET properties = ?3
WHERE wc_id = ?1 AND local_relpath = ?2
  AND op_depth =
   (SELECT MAX(op_depth) FROM nodes
    WHERE wc_id = ?1 AND local_relpath = ?2 AND op_depth > 0);

-- STMT_UPDATE_ACTUAL_PROPS
UPDATE actual_node SET properties = ?3
WHERE wc_id = ?1 AND local_relpath = ?2;

-- STMT_INSERT_ACTUAL_PROPS
INSERT INTO actual_node (wc_id, local_relpath, parent_relpath, properties)
VALUES (?1, ?2, ?3, ?4);

-- STMT_INSERT_LOCK
INSERT OR REPLACE INTO lock
(repos_id, repos_relpath, lock_token, lock_owner, lock_comment,
 lock_date)
VALUES (?1, ?2, ?3, ?4, ?5, ?6);

-- STMT_INSERT_WCROOT
INSERT INTO wcroot (local_abspath)
VALUES (?1);

-- STMT_UPDATE_BASE_DAV_CACHE
UPDATE base_node SET dav_cache = ?3
WHERE wc_id = ?1 AND local_relpath = ?2;

-- STMT_UPDATE_BASE_NODE_DAV_CACHE
UPDATE nodes SET dav_cache = ?3
WHERE wc_id = ?1 AND local_relpath = ?2 AND op_depth = 0;

-- STMT_SELECT_BASE_DAV_CACHE
SELECT dav_cache FROM nodes
WHERE wc_id = ?1 AND local_relpath = ?2 AND op_depth = 0;

-- STMT_SELECT_DELETION_INFO
SELECT base_node.presence, working_node.presence, moved_to
FROM working_node
LEFT OUTER JOIN base_node ON base_node.wc_id = working_node.wc_id
  AND base_node.local_relpath = working_node.local_relpath
WHERE working_node.wc_id = ?1 AND working_node.local_relpath = ?2;

-- STMT_SELECT_DELETION_INFO_1
SELECT nodes_base.presence, nodes_work.presence, nodes_work.moved_to
FROM nodes nodes_work
LEFT OUTER JOIN nodes nodes_base ON nodes_base.wc_id = nodes_work.wc_id
  AND nodes_base.local_relpath = nodes_work.local_relpath
  AND nodes_base.op_depth = 0
WHERE nodes_work.wc_id = ?1 AND nodes_work.local_relpath = ?2
  AND nodes_work.op_depth = (SELECT MAX(op_depth) FROM nodes
                             WHERE wc_id = ?1 AND local_relpath = ?2
                                              AND op_depth > 0);

-- STMT_DELETE_LOCK
DELETE FROM lock
WHERE repos_id = ?1 AND repos_relpath = ?2;

-- STMT_CLEAR_BASE_RECURSIVE_DAV_CACHE
UPDATE base_node SET dav_cache = NULL
WHERE dav_cache IS NOT NULL AND wc_id = ?1 AND
  (local_relpath = ?2 OR
   local_relpath LIKE ?3 ESCAPE '#');

-- STMT_CLEAR_BASE_NODE_RECURSIVE_DAV_CACHE
UPDATE nodes SET dav_cache = NULL
WHERE dav_cache IS NOT NULL AND wc_id = ?1 AND op_depth = 0 AND
  (local_relpath = ?2 OR
   local_relpath LIKE ?3 ESCAPE '#');

-- STMT_UPDATE_BASE_RECURSIVE_REPO
UPDATE base_node SET repos_id = ?5
WHERE repos_id = ?4 AND wc_id = ?1 AND
  (local_relpath = ?2 OR
   local_relpath LIKE ?3 ESCAPE '#');

-- STMT_UPDATE_WORKING_RECURSIVE_COPYFROM_REPO
UPDATE working_node SET copyfrom_repos_id = ?5
WHERE copyfrom_repos_id = ?4 AND wc_id = ?1 AND
  (local_relpath = ?2 OR
   local_relpath LIKE ?3 ESCAPE '#');

-- STMT_RECURSIVE_UPDATE_NODE_REPO
UPDATE nodes SET repos_id = ?5, dav_cache = NULL
WHERE wc_id = ?1 AND repos_id = ?4 AND
  (local_relpath = ?2
   OR local_relpath LIKE ?3 ESCAPE '#');

-- STMT_UPDATE_LOCK_REPOS_ID
UPDATE lock SET repos_id = ?4
WHERE repos_id = ?1 AND
  (repos_relpath = ?2 OR
   repos_relpath LIKE ?3 ESCAPE '#');

-- STMT_UPDATE_BASE_FILEINFO
UPDATE base_node SET translated_size = ?3, last_mod_time = ?4
WHERE wc_id = ?1 AND local_relpath = ?2;

-- STMT_UPDATE_BASE_NODE_FILEINFO
UPDATE nodes SET translated_size = ?3, last_mod_time = ?4
WHERE wc_id = ?1 AND local_relpath = ?2 AND op_depth = 0;

-- STMT_UPDATE_WORKING_FILEINFO
UPDATE working_node SET translated_size = ?3, last_mod_time = ?4
WHERE wc_id = ?1 AND local_relpath = ?2;

-- STMT_UPDATE_WORKING_NODE_FILEINFO
UPDATE nodes SET translated_size = ?3, last_mod_time = ?4
WHERE wc_id = ?1 AND local_relpath = ?2
  AND op_depth = (SELECT MAX(op_depth) FROM nodes
                  WHERE wc_id = ?1 AND local_relpath = ?2 AND op_depth > 0);

-- STMT_UPDATE_ACTUAL_TREE_CONFLICTS
UPDATE actual_node SET tree_conflict_data = ?3
WHERE wc_id = ?1 AND local_relpath = ?2;

-- STMT_INSERT_ACTUAL_TREE_CONFLICTS
/* tree conflicts are always recorded on the wcroot node, so the
   parent_relpath will be null.  */
INSERT INTO actual_node (
  wc_id, local_relpath, tree_conflict_data)
VALUES (?1, ?2, ?3);

-- STMT_UPDATE_ACTUAL_TEXT_CONFLICTS
UPDATE actual_node SET conflict_old = ?3, conflict_new = ?4,
  conflict_working = ?5
WHERE wc_id = ?1 AND local_relpath = ?2;

-- STMT_INSERT_ACTUAL_TEXT_CONFLICTS
INSERT INTO actual_node (
  wc_id, local_relpath, conflict_old, conflict_new, conflict_working,
  parent_relpath)
VALUES (?1, ?2, ?3, ?4, ?5, ?6);

-- STMT_UPDATE_ACTUAL_PROPERTY_CONFLICTS
UPDATE actual_node SET prop_reject = ?3
WHERE wc_id = ?1 AND local_relpath = ?2;

-- STMT_INSERT_ACTUAL_PROPERTY_CONFLICTS
INSERT INTO actual_node (
  wc_id, local_relpath, prop_reject, parent_relpath)
VALUES (?1, ?2, ?3, ?4);

-- STMT_UPDATE_ACTUAL_CHANGELIST
UPDATE actual_node SET changelist = ?3
WHERE wc_id = ?1 AND local_relpath = ?2;

-- STMT_INSERT_ACTUAL_CHANGELIST
INSERT INTO actual_node (
  wc_id, local_relpath, changelist, parent_relpath)
VALUES (?1, ?2, ?3, ?4);

-- STMT_RESET_ACTUAL_WITH_CHANGELIST
REPLACE INTO actual_node (
  wc_id, local_relpath, parent_relpath, changelist)
VALUES (?1, ?2, ?3, ?4);

-- STMT_DELETE_BASE_NODE
DELETE FROM base_node
WHERE wc_id = ?1 AND local_relpath = ?2;

-- STMT_DELETE_BASE_NODE_1
DELETE FROM nodes
WHERE wc_id = ?1 AND local_relpath = ?2 AND op_depth = 0;

-- STMT_DELETE_WORKING_NODE
DELETE FROM working_node
WHERE wc_id = ?1 AND local_relpath = ?2;

-- STMT_DELETE_WORKING_NODES
DELETE FROM nodes
WHERE wc_id = ?1 AND local_relpath = ?2 AND op_depth > 0;

-- STMT_DELETE_NODES
DELETE FROM nodes
WHERE wc_id = ?1 AND local_relpath = ?2;

-- STMT_DELETE_ACTUAL_NODE
DELETE FROM actual_node
WHERE wc_id = ?1 AND local_relpath = ?2;

-- STMT_UPDATE_BASE_DEPTH
UPDATE base_node SET depth = ?3
WHERE wc_id = ?1 AND local_relpath = ?2;

-- STMT_UPDATE_NODE_BASE_DEPTH
UPDATE nodes SET depth = ?3
WHERE wc_id = ?1 AND local_relpath = ?2 AND op_depth = 0;

-- STMT_UPDATE_WORKING_DEPTH
UPDATE working_node SET depth = ?3
WHERE wc_id = ?1 AND local_relpath = ?2;

-- STMT_UPDATE_NODE_WORKING_DEPTH
UPDATE nodes SET depth = ?3
WHERE wc_id = ?1 AND local_relpath = ?2 AND
      op_depth = (SELECT MAX(op_depth) FROM nodes
                  WHERE wc_id = ?1 AND local_relpath = ?2 AND op_depth > 0);

-- STMT_UPDATE_BASE_EXCLUDED
UPDATE base_node SET presence = 'excluded', depth = NULL
WHERE wc_id = ?1 AND local_relpath = ?2;

-- STMT_UPDATE_NODE_BASE_EXCLUDED
UPDATE nodes SET presence = 'excluded', depth = NULL
WHERE wc_id = ?1 AND local_relpath = ?2 AND op_depth = 0;

-- STMT_UPDATE_WORKING_EXCLUDED
UPDATE working_node SET presence = 'excluded', depth = NULL
WHERE wc_id = ?1 AND local_relpath = ?2;

-- STMT_UPDATE_NODE_WORKING_EXCLUDED
UPDATE nodes SET presence = 'excluded', depth = NULL
WHERE wc_id = ?1 AND local_relpath = ?2 AND
      op_depth = (SELECT MAX(op_depth) FROM nodes
                  WHERE wc_id = ?1 AND local_relpath = ?2);

-- STMT_UPDATE_BASE_PRESENCE
UPDATE base_node SET presence= ?3
WHERE wc_id = ?1 AND local_relpath = ?2;

-- STMT_UPDATE_NODE_BASE_PRESENCE
UPDATE nodes SET presence = ?3
WHERE wc_id = ?1 AND local_relpath = ?2 AND op_depth = 0;

-- STMT_UPDATE_BASE_PRESENCE_KIND
UPDATE base_node SET presence = ?3, kind = ?4
WHERE wc_id = ?1 AND local_relpath = ?2;

-- STMT_UPDATE_NODE_BASE_PRESENCE_KIND
UPDATE nodes SET presence = ?3, kind = ?4
WHERE wc_id = ?1 AND local_relpath = ?2 AND op_depth = 0;

-- STMT_UPDATE_WORKING_PRESENCE
UPDATE working_node SET presence = ?3
WHERE wc_id = ?1 AND local_relpath =?2;

-- STMT_UPDATE_NODE_WORKING_PRESENCE
UPDATE nodes SET presence = ?3
WHERE wc_id = ?1 AND local_relpath = ?2
  AND op_depth = (SELECT MAX(op_depth) FROM nodes
                  WHERE wc_id = ?1 AND local_relpath = ?2 AND op_depth > 0);

-- STMT_UPDATE_BASE_PRESENCE_AND_REVNUM
UPDATE base_node SET presence = ?3, revnum = ?4
WHERE wc_id = ?1 AND local_relpath = ?2;

-- STMT_UPDATE_BASE_NODE_PRESENCE_AND_REVNUM
UPDATE nodes SET presence = ?3, revision = ?4
WHERE wc_id = ?1 AND local_relpath = ?2 AND op_depth = 0;

-- STMT_UPDATE_BASE_PRESENCE_REVNUM_AND_REPOS_RELPATH
UPDATE base_node SET presence = ?3, revnum = ?4, repos_relpath = ?5
WHERE wc_id = ?1 AND local_relpath = ?2;

-- STMT_UPDATE_BASE_NODE_PRESENCE_REVNUM_AND_REPOS_PATH
UPDATE nodes SET presence = ?3, revision = ?4, repos_path = ?5
WHERE wc_id = ?1 AND local_relpath = ?2 AND op_depth = 0;

-- STMT_LOOK_FOR_WORK
SELECT id FROM work_queue LIMIT 1;

-- STMT_INSERT_WORK_ITEM
INSERT INTO work_queue (work) VALUES (?1);

-- STMT_SELECT_WORK_ITEM
SELECT id, work FROM work_queue ORDER BY id LIMIT 1;

-- STMT_DELETE_WORK_ITEM
DELETE FROM work_queue WHERE id = ?1;

-- STMT_INSERT_PRISTINE
INSERT OR IGNORE INTO pristine (checksum, md5_checksum, size, refcount)
VALUES (?1, ?2, ?3, 1);

-- STMT_SELECT_PRISTINE_MD5_CHECKSUM
SELECT md5_checksum
FROM pristine
WHERE checksum = ?1

-- STMT_SELECT_PRISTINE_SHA1_CHECKSUM
SELECT checksum
FROM pristine
WHERE md5_checksum = ?1

-- STMT_SELECT_PRISTINE_ROWS
SELECT checksum
FROM pristine

-- STMT_SELECT_ANY_PRISTINE_REFERENCE
SELECT 1 FROM base_node
  WHERE checksum = ?1 OR checksum = ?2
UNION ALL
SELECT 1 FROM working_node
  WHERE checksum = ?1 OR checksum = ?2
UNION ALL
SELECT 1 FROM actual_node
  WHERE older_checksum = ?1 OR older_checksum = ?2
    OR  left_checksum  = ?1 OR left_checksum  = ?2
    OR  right_checksum = ?1 OR right_checksum = ?2
LIMIT 1

-- STMT_SELECT_ANY_PRISTINE_REFERENCE_1
SELECT 1 FROM nodes
  WHERE checksum = ?1 OR checksum = ?2
UNION ALL
SELECT 1 FROM actual_node
  WHERE older_checksum = ?1 OR older_checksum = ?2
    OR  left_checksum  = ?1 OR left_checksum  = ?2
    OR  right_checksum = ?1 OR right_checksum = ?2
LIMIT 1

-- STMT_DELETE_PRISTINE
DELETE FROM pristine
WHERE checksum = ?1

-- STMT_SELECT_ACTUAL_CONFLICT_VICTIMS
SELECT local_relpath
FROM actual_node
WHERE wc_id = ?1 AND parent_relpath = ?2 AND
  NOT ((prop_reject IS NULL) AND (conflict_old IS NULL)
       AND (conflict_new IS NULL) AND (conflict_working IS NULL))

-- STMT_SELECT_ACTUAL_TREE_CONFLICT
SELECT tree_conflict_data
FROM actual_node
WHERE wc_id = ?1 AND local_relpath = ?2;

-- STMT_SELECT_CONFLICT_DETAILS
SELECT prop_reject, conflict_old, conflict_new, conflict_working
FROM actual_node
WHERE wc_id = ?1 AND local_relpath = ?2;

-- STMT_CLEAR_TEXT_CONFLICT
UPDATE actual_node SET
  conflict_old = NULL,
  conflict_new = NULL,
  conflict_working = NULL
WHERE wc_id = ?1 AND local_relpath = ?2;

-- STMT_CLEAR_PROPS_CONFLICT
UPDATE actual_node SET
  prop_reject = NULL
WHERE wc_id = ?1 AND local_relpath = ?2;

-- STMT_INSERT_WC_LOCK
INSERT INTO wc_lock (wc_id, local_dir_relpath, locked_levels)
VALUES (?1, ?2, ?3);

-- STMT_SELECT_WC_LOCK
SELECT locked_levels FROM wc_lock
WHERE wc_id = ?1 AND local_dir_relpath = ?2;

-- STMT_DELETE_WC_LOCK
DELETE FROM wc_lock
WHERE wc_id = ?1 AND local_dir_relpath = ?2;

-- STMT_FIND_WC_LOCK
SELECT local_dir_relpath FROM wc_lock
WHERE wc_id = ?1 AND local_dir_relpath LIKE ?2 ESCAPE '#';

-- STMT_APPLY_CHANGES_TO_BASE
/* translated_size and last_mod_time are not mentioned here because they will
   be tweaked after the working-file is installed.
   ### what to do about file_external?  */
INSERT OR REPLACE INTO base_node (
  wc_id, local_relpath, parent_relpath, presence, kind, revnum, changed_rev,
  changed_author, properties, repos_id, repos_relpath, checksum, changed_date,
  depth, symlink_target, dav_cache)
VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14, ?15, ?16);

-- STMT_APPLY_CHANGES_TO_BASE_NODE
INSERT OR REPLACE INTO nodes (
  wc_id, local_relpath, op_depth, parent_relpath, repos_id, repos_path,
  revision, presence, depth, kind, changed_revision, changed_date,
  changed_author, checksum, properties, dav_cache, symlink_target )
VALUES (?1, ?2, 0,
        ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14, ?15, ?16);

-- STMT_INSERT_WORKING_NODE_FROM_BASE_NODE
INSERT INTO working_node (
    wc_id, local_relpath, parent_relpath, presence, kind, checksum,
    translated_size, changed_rev, changed_date, changed_author, depth,
    symlink_target, last_mod_time, properties )
SELECT wc_id, local_relpath, parent_relpath, ?3 AS presence, kind, checksum,
    translated_size, changed_rev, changed_date, changed_author, depth,
    symlink_target, last_mod_time, properties
FROM base_node
WHERE wc_id = ?1 AND local_relpath = ?2;

-- STMT_INSERT_WORKING_NODE_FROM_BASE
INSERT INTO nodes (
    wc_id, local_relpath, op_depth, parent_relpath, presence, kind, checksum,
    changed_revision, changed_date, changed_author, depth, symlink_target,
    translated_size, last_mod_time, properties)
SELECT wc_id, local_relpath, ?3 AS op_depth, parent_relpath, ?4 AS presence,
       kind, checksum, changed_revision, changed_date, changed_author, depth,
       symlink_target, translated_size, last_mod_time, properties
FROM nodes
WHERE wc_id = ?1 AND local_relpath = ?2 AND op_depth = 0;

-- STMT_INSERT_WORKING_NODE_NORMAL_FROM_BASE_NODE
INSERT INTO working_node (
    wc_id, local_relpath, parent_relpath, presence, kind, checksum,
    translated_size, changed_rev, changed_date, changed_author, depth,
    symlink_target, last_mod_time, properties, copyfrom_repos_id,
    copyfrom_repos_path, copyfrom_revnum )
SELECT wc_id, local_relpath, parent_relpath, 'normal', kind, checksum,
    translated_size, changed_rev, changed_date, changed_author, depth,
    symlink_target, last_mod_time, properties, repos_id,
    repos_relpath, revnum
FROM base_node
WHERE wc_id = ?1 AND local_relpath = ?2;

-- STMT_INSERT_WORKING_NODE_NORMAL_FROM_BASE
INSERT INTO nodes (
    wc_id, local_relpath, op_depth, parent_relpath, repos_id, repos_path,
    revision, presence, depth, kind, changed_revision, changed_date,
    changed_author, checksum, properties, translated_size, last_mod_time,
    symlink_target )
SELECT wc_id, local_relpath, ?3 AS op_depth, parent_relpath, repos_id,
    repos_path, revision, 'normal', depth, kind, changed_revision,
    changed_date, changed_author, checksum, properties, translated_size,
    last_mod_time, symlink_target
FROM nodes
WHERE wc_id = ?1 AND local_relpath = ?2 AND op_depth = 0;


-- STMT_INSERT_WORKING_NODE_NOT_PRESENT_FROM_BASE_NODE
INSERT INTO working_node (
    wc_id, local_relpath, parent_relpath, presence, kind, changed_rev,
    changed_date, changed_author, copyfrom_repos_id,
    copyfrom_repos_path, copyfrom_revnum )
SELECT wc_id, local_relpath, parent_relpath, 'not-present', kind, changed_rev,
    changed_date, changed_author, repos_id,
    repos_relpath, revnum
FROM base_node
WHERE wc_id = ?1 AND local_relpath = ?2;

-- STMT_INSERT_WORKING_NODE_NOT_PRESENT_FROM_BASE
INSERT INTO nodes (
    wc_id, local_relpath, op_depth, parent_relpath, repos_id, repos_path,
    revision, presence, kind, changed_revision, changed_date, changed_author )
SELECT wc_id, local_relpath, ?3 as op_depth, parent_relpath, repos_id,
       repos_path, revision, 'not-present', kind, changed_revision,
       changed_date, changed_author
FROM nodes
WHERE wc_id = ?1 AND local_relpath = ?2 AND op_depth = 0;


-- ### these statements below should be setting copyfrom_revision!
-- STMT_UPDATE_COPYFROM
UPDATE working_node SET copyfrom_repos_id = ?3, copyfrom_repos_path = ?4
WHERE wc_id = ?1 AND local_relpath = ?2;

-- STMT_UPDATE_COPYFROM_1
UPDATE nodes SET repos_id = ?3, repos_path = ?4
WHERE wc_id = ?1 AND local_relpath = ?2;
  AND op_depth = (SELECT MAX(op_depth) FROM nodes
                  WHERE wc_id = ?1 AND local_relpath = ?2 AND op_depth > 0);

-- STMT_SELECT_CHILDREN_OP_DEPTH_RECURSIVE
SELECT local_relpath, op_depth FROM nodes as node
WHERE wc_id = ?1 AND local_relpath LIKE ?2 ESCAPE '#'
  AND op_depth = (SELECT MAX(op_depth) FROM nodes
                  WHERE wc_id = node.wc_id
                    AND local_relpath = node.local_relpath);

-- STMT_UPDATE_OP_DEPTH
UPDATE nodes SET op_depth = ?4
WHERE wc_id = ?1 AND local_relpath = ?2 AND op_depth = ?3;


-- STMT_UPDATE_COPYFROM_TO_INHERIT
UPDATE working_node SET
  copyfrom_repos_id = NULL,
  copyfrom_repos_path = NULL,
  copyfrom_revnum = NULL
WHERE wc_id = ?1 AND local_relpath = ?2;

-- STMT_UPDATE_COPYFROM_TO_INHERIT_1
UPDATE nodes SET
  repos_id = NULL,
  repos_path = NULL,
  revision = NULL
WHERE wc_id = ?1 AND local_relpath = ?2
  AND op_depth = (SELECT MAX(op_depth) FROM nodes
                  WHERE wc_id = ?1 AND local_relpath = ?2 AND op_depth > 0);

-- STMT_DETERMINE_TREE_FOR_RECORDING
SELECT 0 FROM base_node WHERE wc_id = ?1 AND local_relpath = ?2
UNION
SELECT 1 FROM working_node WHERE wc_id = ?1 AND local_relpath = ?2;

-- STMT_DETERMINE_TREE_FOR_RECORDING_1
SELECT 0 FROM nodes WHERE wc_id = ?1 AND local_relpath = ?2 AND op_depth = 0
UNION
SELECT 1 FROM nodes WHERE wc_id = ?1 AND local_relpath = ?2
  AND op_depth = (SELECT MAX(op_depth) FROM nodes
                  WHERE wc_id = ?1 AND local_relpath = ?2 AND op_depth > 0);


/* ### Why can't this query not just use the BASE repository
   location values, instead of taking 3 additional parameters?! */
-- STMT_INSERT_WORKING_NODE_COPY_FROM_BASE
INSERT OR REPLACE INTO working_node (
    wc_id, local_relpath, parent_relpath, presence, kind, checksum,
    translated_size, changed_rev, changed_date, changed_author, depth,
    symlink_target, last_mod_time, properties, copyfrom_repos_id,
    copyfrom_repos_path, copyfrom_revnum )
SELECT wc_id, ?3 AS local_relpath, ?4 AS parent_relpath, ?5 AS presence, kind,
    checksum, translated_size, changed_rev, changed_date, changed_author, depth,
    symlink_target, last_mod_time, properties, ?6 AS copyfrom_repos_id,
    ?7 AS copyfrom_repos_path, ?8 AS copyfrom_revnum
FROM base_node
WHERE wc_id = ?1 AND local_relpath = ?2;

-- STMT_INSERT_WORKING_NODE_COPY_FROM_BASE_1
INSERT OR REPLACE INTO nodes (
    wc_id, local_relpath, op_depth, parent_relpath, repos_id,
    repos_path, revision, presence, depth, kind, changed_revision,
    changed_date, changed_author, checksum, properties, translated_size,
    last_mod_time, symlink_target )
SELECT wc_id, ?3 AS local_relpath, ?4 AS op_depth, ?5 AS parent_relpath,
    ?6 AS repos_id, ?7 AS repos_path, ?8 AS revision, ?9 AS presence, depth,
    kind, changed_revision, changed_date, changed_author, checksum, properties,
    translated_size, last_mod_time, symlink_target
FROM nodes
WHERE wc_id = ?1 AND local_relpath = ?2 AND op_depth = 0;

-- STMT_INSERT_WORKING_NODE_COPY_FROM_WORKING
INSERT OR REPLACE INTO working_node (
    wc_id, local_relpath, parent_relpath, presence, kind, checksum,
    translated_size, changed_rev, changed_date, changed_author, depth,
    symlink_target, last_mod_time, properties, copyfrom_repos_id,
    copyfrom_repos_path, copyfrom_revnum )
SELECT wc_id, ?3 AS local_relpath, ?4 AS parent_relpath, ?5 AS presence, kind,
    checksum, translated_size, changed_rev, changed_date, changed_author, depth,
    symlink_target, last_mod_time, properties, ?6 AS copyfrom_repos_id,
    ?7 AS copyfrom_repos_path, ?8 AS copyfrom_revnum
FROM working_node
WHERE wc_id = ?1 AND local_relpath = ?2;

-- STMT_INSERT_WORKING_NODE_COPY_FROM_WORKING_1
INSERT OR REPLACE INTO nodes (
    wc_id, local_relpath, op_depth, parent_relpath, repos_id, repos_path,
    revision, presence, depth, kind, changed_revision, changed_date,
    changed_author, checksum, properties, translated_size, last_mod_time,
    symlink_target )
SELECT wc_id, ?3 AS local_relpath, ?4 AS op_depth, ?5 AS parent_relpath,
    ?6 AS repos_id, ?7 AS repos_path, ?8 AS revision, ?9 AS presence, depth,
    kind, changed_revision, changed_date, changed_author, checksum, properties,
    translated_size, last_mod_time, symlink_target
FROM nodes
WHERE wc_id = ?1 AND local_relpath = ?2 AND op_depth > 0
ORDER BY op_depth DESC
LIMIT 1;

-- STMT_INSERT_ACTUAL_NODE_FROM_ACTUAL_NODE
INSERT OR REPLACE INTO actual_node (
     wc_id, local_relpath, parent_relpath, properties,
     conflict_old, conflict_new, conflict_working,
     prop_reject, changelist, text_mod, tree_conflict_data )
SELECT wc_id, ?3 AS local_relpath, ?4 AS parent_relpath, properties,
     conflict_old, conflict_new, conflict_working,
     prop_reject, changelist, text_mod, tree_conflict_data
FROM actual_node
WHERE wc_id = ?1 AND local_relpath = ?2;

-- STMT_UPDATE_BASE_REVISION
UPDATE base_node SET revnum = ?3
WHERE wc_id = ?1 AND local_relpath = ?2;

-- STMT_UPDATE_BASE_REVISION_1
UPDATE nodes SET revision = ?3
WHERE wc_id = ?1 AND local_relpath = ?2 AND op_depth = 0;

-- STMT_UPDATE_BASE_REPOS
UPDATE base_node SET repos_id = ?3, repos_relpath = ?4
WHERE wc_id = ?1 AND local_relpath = ?2;

-- STMT_UPDATE_BASE_REPOS_1
UPDATE nodes SET repos_id = ?3, repos_path = ?4
WHERE wc_id = ?1 AND local_relpath = ?2 AND op_depth = 0;

/* ------------------------------------------------------------------------- */

/* these are used in entries.c  */

-- STMT_INSERT_BASE_NODE_FOR_ENTRY
INSERT OR REPLACE INTO base_node (
  wc_id, local_relpath, repos_id, repos_relpath, parent_relpath,
  presence,
  revnum, kind, checksum, translated_size, changed_rev, changed_date,
  changed_author, depth, last_mod_time, properties)
VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14,
  ?15, ?16);

-- STMT_INSERT_BASE_NODE_FOR_ENTRY_1
/* The BASE tree has a fixed op_depth '0' */
INSERT OR REPLACE INTO nodes (
  wc_id, local_relpath, op_depth, parent_relpath, repos_id, repos_path,
  revision, presence, kind, checksum,
  changed_revision, changed_date, changed_author, depth, properties,
  translated_size, last_mod_time )
VALUES (?1, ?2, 0, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13,
       ?14, ?15, ?16 );

-- STMT_INSERT_WORKING_NODE
INSERT OR REPLACE INTO working_node (
  wc_id, local_relpath, parent_relpath, presence, kind,
  copyfrom_repos_id,
  copyfrom_repos_path, copyfrom_revnum, moved_here, moved_to, checksum,
  translated_size, changed_rev, changed_date, changed_author, depth,
  last_mod_time, properties, keep_local, symlink_target)
VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14,
  ?15, ?16, ?17, ?18, ?19, ?20);

-- STMT_INSERT_WORKING_NODE_DATA_1
INSERT OR REPLACE INTO node_data (
  wc_id, local_relpath, op_depth, parent_relpath, presence, kind,
  original_repos_id, original_repos_path, original_revision, checksum,
  changed_revision, changed_date, changed_author, depth, properties,
  symlink_target )
VALUES (?1,  ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9,
        ?10, ?11, ?12, ?13, ?14, ?15, ?16 );

-- STMT_INSERT_WORKING_NODE_DATA_2
INSERT OR REPLACE INTO working_node (
  wc_id, local_relpath, parent_relpath, moved_here, moved_to, translated_size,
  last_mod_time, keep_local )
VALUES (?1,  ?2, ?3, ?4, ?5, ?6, ?7, ?8 );


-- STMT_INSERT_ACTUAL_NODE
INSERT OR REPLACE INTO actual_node (
  wc_id, local_relpath, parent_relpath, properties, conflict_old,
  conflict_new,
  conflict_working, prop_reject, changelist, text_mod,
  tree_conflict_data)
VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11);

-- STMT_SELECT_NOT_PRESENT
SELECT 1 FROM base_node
WHERE wc_id = ?1 AND local_relpath = ?2 AND presence = 'not-present';

-- STMT_SELECT_NOT_PRESENT_1
SELECT 1 FROM nodes
WHERE wc_id = ?1 AND local_relpath = ?2 AND presence = 'not-present'
  AND op_depth = 0;

-- STMT_SELECT_FILE_EXTERNAL
SELECT file_external FROM base_node
WHERE wc_id = ?1 AND local_relpath = ?2;

-- STMT_SELECT_FILE_EXTERNAL_1
SELECT file_external FROM nodes
WHERE wc_id = ?1 AND local_relpath = ?2 AND op_depth = 0;

-- STMT_UPDATE_FILE_EXTERNAL
UPDATE base_node SET file_external = ?3
WHERE wc_id = ?1 AND local_relpath = ?2;

-- STMT_UPDATE_FILE_EXTERNAL_1
UPDATE nodes SET file_external = ?3
WHERE wc_id = ?1 AND local_relpath = ?2 AND op_depth = 0;

/* ------------------------------------------------------------------------- */

/* these are used in upgrade.c  */

-- STMT_SELECT_OLD_TREE_CONFLICT
SELECT wc_id, local_relpath, tree_conflict_data
FROM actual_node
WHERE tree_conflict_data IS NOT NULL;

-- STMT_INSERT_NEW_CONFLICT
INSERT INTO conflict_victim (
  wc_id, local_relpath, parent_relpath, node_kind, conflict_kind,
  property_name, conflict_action, conflict_reason, operation,
  left_repos_id, left_repos_relpath, left_peg_rev, left_kind,
  right_repos_id, right_repos_relpath, right_peg_rev, right_kind)
VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14, ?15,
  ?16, ?17);

-- STMT_ERASE_OLD_CONFLICTS
UPDATE actual_node SET tree_conflict_data = NULL;

-- STMT_SELECT_ALL_FILES
/* Should this select on wc_id as well? */
SELECT local_relpath FROM base_node
WHERE kind = 'file' AND parent_relpath = ?1
UNION
SELECT local_relpath FROM working_node
WHERE kind = 'file' AND parent_relpath = ?1;

-- STMT_SELECT_ALL_FILES_1
/* Should this select on wc_id as well? */
SELECT DISTINCT local_relpath FROM nodes
WHERE kind = 'file' AND parent_relpath = ?1;

-- STMT_PLAN_PROP_UPGRADE
SELECT 0, presence, wc_id FROM base_node WHERE local_relpath = ?1
UNION ALL
SELECT 1, presence, wc_id FROM working_node WHERE local_relpath = ?1;

-- STMT_PLAN_PROP_UPGRADE_1
SELECT 0, nodes_base.presence, nodes_base.wc_id FROM nodes nodes_base
WHERE nodes_base.local_relpath = ?1 AND nodes_base.op_depth = 0
UNION ALL
SELECT 1, nodes_work.presence, nodes_work.wc_id FROM nodes nodes_work
WHERE nodes_work.local_relpath = ?1
  AND nodes_work.op_depth = (SELECT MAX(op_depth) FROM nodes
                             WHERE local_relpath = ?1 AND op_depth > 0);


/* ------------------------------------------------------------------------- */

/* Grab all the statements related to the schema.  */

-- include: wc-metadata
