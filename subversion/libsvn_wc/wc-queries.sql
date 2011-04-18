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

-- STMT_SELECT_NODE_INFO
SELECT op_depth, repos_id, repos_path, presence, kind, revision, checksum,
  translated_size, changed_revision, changed_date, changed_author, depth,
  symlink_target, last_mod_time, properties
FROM nodes
WHERE wc_id = ?1 AND local_relpath = ?2
ORDER BY op_depth DESC

-- STMT_SELECT_NODE_INFO_WITH_LOCK
SELECT op_depth, nodes.repos_id, nodes.repos_path, presence, kind, revision,
  checksum, translated_size, changed_revision, changed_date, changed_author,
  depth, symlink_target, last_mod_time, properties, lock_token, lock_owner,
  lock_comment, lock_date
FROM nodes
LEFT OUTER JOIN lock ON nodes.repos_id = lock.repos_id
  AND nodes.repos_path = lock.repos_relpath
WHERE wc_id = ?1 AND local_relpath = ?2
ORDER BY op_depth DESC

-- STMT_SELECT_BASE_NODE
SELECT repos_id, repos_path, presence, kind, revision, checksum,
  translated_size, changed_revision, changed_date, changed_author, depth,
  symlink_target, last_mod_time, properties, file_external IS NOT NULL
FROM nodes
WHERE wc_id = ?1 AND local_relpath = ?2 AND op_depth = 0

-- STMT_SELECT_BASE_NODE_WITH_LOCK
SELECT nodes.repos_id, nodes.repos_path, presence, kind, revision,
  checksum, translated_size, changed_revision, changed_date, changed_author,
  depth, symlink_target, last_mod_time, properties, file_external IS NOT NULL,
  lock_token, lock_owner, lock_comment, lock_date
FROM nodes
LEFT OUTER JOIN lock ON nodes.repos_id = lock.repos_id
  AND nodes.repos_path = lock.repos_relpath
WHERE wc_id = ?1 AND local_relpath = ?2 AND op_depth = 0

-- STMT_SELECT_BASE_CHILDREN_INFO
SELECT local_relpath, nodes.repos_id, nodes.repos_path, presence, kind,
  revision, depth, file_external IS NOT NULL,
  lock_token, lock_owner, lock_comment, lock_date
FROM nodes
LEFT OUTER JOIN lock ON nodes.repos_id = lock.repos_id
  AND nodes.repos_path = lock.repos_relpath
WHERE wc_id = ?1 AND parent_relpath = ?2 AND op_depth = 0

-- STMT_SELECT_WORKING_NODE
SELECT op_depth, presence, kind, checksum, translated_size,
  changed_revision, changed_date, changed_author, depth, symlink_target,
  repos_id, repos_path, revision,
  moved_here, moved_to, last_mod_time, properties
FROM nodes
WHERE wc_id = ?1 AND local_relpath = ?2 AND op_depth > 0
ORDER BY op_depth DESC
LIMIT 1

-- STMT_SELECT_LOWEST_WORKING_NODE
SELECT op_depth, presence
FROM nodes
WHERE wc_id = ?1 AND local_relpath = ?2 AND op_depth > 0
ORDER BY op_depth
LIMIT 1

-- STMT_SELECT_ACTUAL_NODE
SELECT prop_reject, changelist, conflict_old, conflict_new,
conflict_working, tree_conflict_data, properties
FROM actual_node
WHERE wc_id = ?1 AND local_relpath = ?2

-- STMT_SELECT_ACTUAL_TREE_CONFLICT
SELECT tree_conflict_data
FROM actual_node
WHERE wc_id = ?1 AND local_relpath = ?2 AND tree_conflict_data IS NOT NULL

-- STMT_SELECT_NODE_CHILDREN_INFO
/* Getting rows in an advantageous order using
     ORDER BY local_relpath, op_depth DESC
   turns out to be slower than getting rows in a random order and making the
   C code handle it. */
SELECT op_depth, nodes.repos_id, nodes.repos_path, presence, kind, revision,
  checksum, translated_size, changed_revision, changed_date, changed_author,
  depth, symlink_target, last_mod_time, properties, lock_token, lock_owner,
  lock_comment, lock_date, local_relpath
FROM nodes
LEFT OUTER JOIN lock ON nodes.repos_id = lock.repos_id
  AND nodes.repos_path = lock.repos_relpath
WHERE wc_id = ?1 AND parent_relpath = ?2

-- STMT_SELECT_NODE_CHILDREN_WALKER_INFO
/* ### See comment at STMT_SELECT_NODE_CHILDREN_INFO.
   ### Should C code handle GROUP BY local_relpath ORDER BY op_depths DESC? */
SELECT local_relpath, op_depth, presence, kind
FROM nodes
WHERE wc_id = ?1 AND parent_relpath = ?2
GROUP BY local_relpath
ORDER BY op_depth DESC

-- STMT_SELECT_ACTUAL_CHILDREN_INFO
SELECT prop_reject, changelist, conflict_old, conflict_new,
conflict_working, tree_conflict_data, properties, local_relpath,
conflict_data
FROM actual_node
WHERE wc_id = ?1 AND parent_relpath = ?2

-- STMT_SELECT_REPOSITORY_BY_ID
SELECT root, uuid FROM repository WHERE id = ?1

-- STMT_SELECT_WCROOT_NULL
SELECT id FROM wcroot WHERE local_abspath IS NULL

-- STMT_SELECT_REPOSITORY
SELECT id FROM repository WHERE root = ?1

-- STMT_INSERT_REPOSITORY
INSERT INTO repository (root, uuid) VALUES (?1, ?2)

-- STMT_INSERT_NODE
INSERT OR REPLACE INTO nodes (
  wc_id, local_relpath, op_depth, parent_relpath, repos_id, repos_path,
  revision, presence, depth, kind, changed_revision, changed_date,
  changed_author, checksum, properties, translated_size, last_mod_time,
  dav_cache, symlink_target )
VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14,
        ?15, ?16, ?17, ?18, ?19)

-- STMT_SELECT_OP_DEPTH_CHILDREN
SELECT local_relpath FROM nodes
WHERE wc_id = ?1 AND parent_relpath = ?2 AND op_depth = ?3

-- STMT_SELECT_GE_OP_DEPTH_CHILDREN
SELECT 1 FROM nodes
WHERE wc_id = ?1 AND parent_relpath = ?2
  AND (op_depth > ?3 OR (op_depth = ?3 AND presence != 'base-deleted'))
UNION
SELECT 1 FROM ACTUAL_NODE
WHERE wc_id = ?1 AND parent_relpath = ?2

-- STMT_DELETE_SHADOWED_RECURSIVE
DELETE FROM nodes
WHERE wc_id = ?1
  AND (local_relpath = ?2
       OR ((local_relpath > ?2 || '/') AND (local_relpath < ?2 || '0')))
  AND (op_depth < ?3
       OR (op_depth = ?3 AND presence = 'base-deleted'))

-- STMT_SELECT_NODE_CHILDREN
/* Return all paths that are children of the directory (?1, ?2) in any
   op-depth, including children of any underlying, replaced directories. */
SELECT local_relpath FROM nodes
WHERE wc_id = ?1 AND parent_relpath = ?2

-- STMT_SELECT_WORKING_CHILDREN
/* Return all paths that are children of the working version of the
   directory (?1, ?2).  A given path is not included just because it is a
   child of an underlying (replaced) directory, it has to be in the
   working version of the directory. */
SELECT local_relpath FROM nodes
WHERE wc_id = ?1 AND parent_relpath = ?2
  AND (op_depth > (SELECT MAX(op_depth) FROM nodes
                   WHERE wc_id = ?1 AND local_relpath = ?2)
       OR
       (op_depth = (SELECT MAX(op_depth) FROM nodes
                    WHERE wc_id = ?1 AND local_relpath = ?2)
        AND presence != 'base-deleted'))

-- STMT_SELECT_BASE_PROPS
SELECT properties FROM nodes
WHERE wc_id = ?1 AND local_relpath = ?2 AND op_depth = 0

-- STMT_SELECT_NODE_PROPS
SELECT properties, presence FROM nodes
WHERE wc_id = ?1 AND local_relpath = ?2
ORDER BY op_depth DESC

-- STMT_SELECT_NODE_PROPS_OF_CHILDREN
SELECT properties, presence, local_relpath, kind FROM nodes
WHERE wc_id = ?1 AND parent_relpath = ?2
ORDER BY local_relpath, op_depth DESC

-- STMT_SELECT_ACTUAL_PROPS
SELECT properties FROM actual_node
WHERE wc_id = ?1 AND local_relpath = ?2

-- STMT_SELECT_ACTUAL_PROPS_OF_CHILDREN
SELECT properties, local_relpath FROM actual_node
WHERE wc_id = ?1 AND parent_relpath = ?2

-- STMT_UPDATE_NODE_BASE_PROPS
UPDATE nodes SET properties = ?3
WHERE wc_id = ?1 AND local_relpath = ?2 AND op_depth = 0

-- STMT_UPDATE_NODE_WORKING_PROPS
UPDATE nodes SET properties = ?3
WHERE wc_id = ?1 AND local_relpath = ?2
  AND op_depth =
   (SELECT MAX(op_depth) FROM nodes
    WHERE wc_id = ?1 AND local_relpath = ?2 AND op_depth > 0)

-- STMT_UPDATE_ACTUAL_PROPS
UPDATE actual_node SET properties = ?3
WHERE wc_id = ?1 AND local_relpath = ?2

-- STMT_INSERT_ACTUAL_PROPS
INSERT INTO actual_node (wc_id, local_relpath, parent_relpath, properties)
VALUES (?1, ?2, ?3, ?4)

-- STMT_INSERT_LOCK
INSERT OR REPLACE INTO lock
(repos_id, repos_relpath, lock_token, lock_owner, lock_comment,
 lock_date)
VALUES (?1, ?2, ?3, ?4, ?5, ?6)

-- STMT_SELECT_BASE_NODE_LOCK_TOKENS_RECURSIVE
SELECT nodes.repos_id, nodes.repos_path, lock_token
FROM nodes
LEFT JOIN lock ON nodes.repos_id = lock.repos_id
  AND nodes.repos_path = lock.repos_relpath
WHERE wc_id = ?1 AND op_depth = 0
  AND (local_relpath = ?2 OR local_relpath LIKE ?3 ESCAPE '#')

-- STMT_INSERT_WCROOT
INSERT INTO wcroot (local_abspath)
VALUES (?1)

-- STMT_UPDATE_BASE_NODE_DAV_CACHE
UPDATE nodes SET dav_cache = ?3
WHERE wc_id = ?1 AND local_relpath = ?2 AND op_depth = 0

-- STMT_SELECT_BASE_DAV_CACHE
SELECT dav_cache FROM nodes
WHERE wc_id = ?1 AND local_relpath = ?2 AND op_depth = 0

-- STMT_SELECT_DELETION_INFO
SELECT nodes_base.presence, nodes_work.presence, nodes_work.moved_to,
       nodes_work.op_depth
FROM nodes AS nodes_work
LEFT OUTER JOIN nodes nodes_base ON nodes_base.wc_id = nodes_work.wc_id
  AND nodes_base.local_relpath = nodes_work.local_relpath
  AND nodes_base.op_depth = 0
WHERE nodes_work.wc_id = ?1 AND nodes_work.local_relpath = ?2
  AND nodes_work.op_depth = (SELECT MAX(op_depth) FROM nodes
                             WHERE wc_id = ?1 AND local_relpath = ?2
                                              AND op_depth > 0)

-- STMT_DELETE_LOCK
DELETE FROM lock
WHERE repos_id = ?1 AND repos_relpath = ?2

-- STMT_CLEAR_BASE_NODE_RECURSIVE_DAV_CACHE
UPDATE nodes SET dav_cache = NULL
WHERE dav_cache IS NOT NULL AND wc_id = ?1 AND op_depth = 0 AND
  (local_relpath = ?2 OR
   local_relpath LIKE ?3 ESCAPE '#')

-- STMT_RECURSIVE_UPDATE_NODE_REPO
UPDATE nodes SET repos_id = ?5, dav_cache = NULL
WHERE wc_id = ?1 AND repos_id = ?4 AND
  (local_relpath = ?2
   OR local_relpath LIKE ?3 ESCAPE '#')

-- STMT_UPDATE_LOCK_REPOS_ID
UPDATE lock SET repos_id = ?4
WHERE repos_id = ?1 AND
  (repos_relpath = ?2 OR
   repos_relpath LIKE ?3 ESCAPE '#')

-- STMT_UPDATE_NODE_FILEINFO
UPDATE nodes SET translated_size = ?3, last_mod_time = ?4
WHERE wc_id = ?1 AND local_relpath = ?2
  AND op_depth = (SELECT MAX(op_depth) FROM nodes
                  WHERE wc_id = ?1 AND local_relpath = ?2)

-- STMT_UPDATE_ACTUAL_TREE_CONFLICTS
UPDATE actual_node SET tree_conflict_data = ?3
WHERE wc_id = ?1 AND local_relpath = ?2

-- STMT_INSERT_ACTUAL_TREE_CONFLICTS
INSERT INTO actual_node (
  wc_id, local_relpath, tree_conflict_data, parent_relpath)
VALUES (?1, ?2, ?3, ?4)

-- STMT_UPDATE_ACTUAL_TEXT_CONFLICTS
UPDATE actual_node SET conflict_old = ?3, conflict_new = ?4,
  conflict_working = ?5
WHERE wc_id = ?1 AND local_relpath = ?2

-- STMT_INSERT_ACTUAL_TEXT_CONFLICTS
INSERT INTO actual_node (
  wc_id, local_relpath, conflict_old, conflict_new, conflict_working,
  parent_relpath)
VALUES (?1, ?2, ?3, ?4, ?5, ?6)

-- STMT_UPDATE_ACTUAL_PROPERTY_CONFLICTS
UPDATE actual_node SET prop_reject = ?3
WHERE wc_id = ?1 AND local_relpath = ?2

-- STMT_INSERT_ACTUAL_PROPERTY_CONFLICTS
INSERT INTO actual_node (
  wc_id, local_relpath, prop_reject, parent_relpath)
VALUES (?1, ?2, ?3, ?4)

-- STMT_UPDATE_ACTUAL_CHANGELIST_FILTER_CHANGELIST
UPDATE actual_node SET changelist = ?3
WHERE wc_id = ?1 AND local_relpath = ?2
  AND changelist=?4

-- STMT_UPDATE_ACTUAL_CHANGELIST
UPDATE actual_node SET changelist = ?3
WHERE wc_id = ?1 AND local_relpath = ?2

-- STMT_INSERT_ACTUAL_CHANGELIST
INSERT INTO actual_node (
  wc_id, local_relpath, changelist, parent_relpath)
VALUES (?1, ?2, ?3, ?4)

-- STMT_RESET_ACTUAL_WITH_CHANGELIST
REPLACE INTO actual_node (
  wc_id, local_relpath, parent_relpath, changelist)
VALUES (?1, ?2, ?3, ?4)

-- STMT_CREATE_CHANGELIST_LIST
DROP TABLE IF EXISTS changelist_list;
CREATE TEMPORARY TABLE changelist_list (
  wc_id  INTEGER NOT NULL,
  local_relpath TEXT NOT NULL,
  notify INTEGER,
  changelist TEXT NOT NULL
  );
CREATE INDEX changelist_list_index ON changelist_list(wc_id, local_relpath);
/* We have four cases upon which we wish to notify.  The first is easy:

        Action                                  Notification
        ------                                  ------------
        INSERT ACTUAL                           cl-set

   The others are a bit more complex:
        Action          Old CL      New CL      Notification
        ------          ------      ------      ------------
        UPDATE ACTUAL   NULL        NOT NULL    cl-set
        UPDATE ACTUAL   NOT NULL    NOT NULL    cl-clear / cl-set
        UPDATE ACTUAL   NOT NULL    NULL        cl-clear

Of the following triggers, the first address the first case, and the second
two address the last three cases.
*/
DROP TRIGGER IF EXISTS   trigger_changelist_list_actual_cl_insert;
CREATE TEMPORARY TRIGGER trigger_changelist_list_actual_cl_insert
BEFORE INSERT ON actual_node
BEGIN
    /* 26 corresponds to svn_wc_notify_changelist_set */
    INSERT INTO changelist_list(wc_id, local_relpath, notify, changelist)
    VALUES (NEW.wc_id, NEW.local_relpath, 26, NEW.changelist);
END;
DROP TRIGGER IF EXISTS   trigger_changelist_list_actual_cl_clear;
CREATE TEMPORARY TRIGGER trigger_changelist_list_actual_cl_clear
BEFORE UPDATE ON actual_node
WHEN OLD.changelist IS NOT NULL AND
        (OLD.changelist != NEW.changelist OR NEW.changelist IS NULL)
BEGIN
    /* 27 corresponds to svn_wc_notify_changelist_clear */
    INSERT INTO changelist_list(wc_id, local_relpath, notify, changelist)
    VALUES (OLD.wc_id, OLD.local_relpath, 27, OLD.changelist);
END;
DROP TRIGGER IF EXISTS   trigger_changelist_list_actual_cl_set;
CREATE TEMPORARY TRIGGER trigger_changelist_list_actual_cl_set
BEFORE UPDATE ON actual_node
WHEN NEW.CHANGELIST IS NOT NULL AND
        (OLD.changelist != NEW.changelist OR OLD.changelist IS NULL)
BEGIN
    /* 26 corresponds to svn_wc_notify_changelist_set */
    INSERT INTO changelist_list(wc_id, local_relpath, notify, changelist)
    VALUES (NEW.wc_id, NEW.local_relpath, 26, NEW.changelist);
END;

-- STMT_DROP_CHANGELIST_LIST_TRIGGERS
DROP TRIGGER IF EXISTS trigger_changelist_list_actual_cl_insert;
DROP TRIGGER IF EXISTS trigger_changelist_list_actual_cl_set;
DROP TRIGGER IF EXISTS trigger_changelist_list_actual_cl_clear;

-- STMT_INSERT_CHANGELIST_LIST
INSERT INTO changelist_list(wc_id, local_relpath, notify, changelist)
VALUES (?1, ?2, ?3, ?4);

-- STMT_DELETE_CHANGELIST_LIST_RECURSIVE
DELETE FROM changelist_list
WHERE wc_id = ?1 AND local_relpath = ?2 OR local_relpath LIKE ?3 ESCAPE '#'

-- STMT_SELECT_CHANGELIST_LIST_RECURSIVE
SELECT local_relpath, notify, changelist
FROM changelist_list
WHERE wc_id = ?1 AND local_relpath = ?2 or local_relpath LIKE ?3 ESCAPE '#'
ORDER BY local_relpath

-- STMT_DELETE_ACTUAL_EMPTY
DELETE FROM actual_node
WHERE wc_id = ?1 AND local_relpath = ?2
  AND properties IS NULL
  AND conflict_old IS NULL
  AND conflict_new IS NULL
  AND prop_reject IS NULL
  AND changelist IS NULL
  AND text_mod IS NULL
  AND tree_conflict_data IS NULL
  AND older_checksum IS NULL
  AND right_checksum IS NULL
  AND left_checksum IS NULL

-- STMT_DELETE_BASE_NODE
DELETE FROM nodes
WHERE wc_id = ?1 AND local_relpath = ?2 AND op_depth = 0

-- STMT_DELETE_WORKING_NODE
DELETE FROM nodes
WHERE wc_id = ?1 AND local_relpath = ?2
  AND op_depth = (SELECT MAX(op_depth) FROM nodes
                  WHERE wc_id = ?1 AND local_relpath = ?2 AND op_depth > 0)

-- STMT_DELETE_LOWEST_WORKING_NODE
DELETE FROM nodes
WHERE wc_id = ?1 AND local_relpath = ?2
  AND op_depth = (SELECT MIN(op_depth) FROM nodes
                  WHERE wc_id = ?1 AND local_relpath = ?2 AND op_depth > 0)
  AND presence = 'base-deleted'

-- STMT_DELETE_ALL_WORKING_NODES
DELETE FROM nodes
WHERE wc_id = ?1 AND local_relpath = ?2 AND op_depth > 0

-- STMT_DELETE_NODES
DELETE FROM nodes
WHERE wc_id = ?1 AND local_relpath = ?2

-- STMT_DELETE_NODES_RECURSIVE
DELETE FROM nodes
WHERE wc_id = ?1 AND (local_relpath = ?2 OR local_relpath LIKE ?3 ESCAPE '#')
  AND op_depth >= ?4

-- STMT_DELETE_ACTUAL_NODE
DELETE FROM actual_node
WHERE wc_id = ?1 AND local_relpath = ?2

-- STMT_DELETE_ACTUAL_NODE_RECURSIVE
DELETE FROM actual_node
WHERE wc_id = ?1 AND (local_relpath = ?2 OR local_relpath LIKE ?3 ESCAPE '#')

-- STMT_DELETE_ACTUAL_NODE_WITHOUT_CONFLICT
DELETE FROM actual_node
WHERE wc_id = ?1 AND local_relpath = ?2
      AND tree_conflict_data IS NULL

-- STMT_DELETE_ACTUAL_NODE_LEAVING_CHANGELIST
DELETE FROM actual_node
WHERE wc_id = ?1 AND local_relpath = ?2
      AND (changelist IS NULL
           OR local_relpath NOT IN (SELECT local_relpath FROM nodes_current
                                     WHERE wc_id  = ?1 AND local_relpath = ?2
                                       AND kind = 'file'))

-- STMT_DELETE_ACTUAL_NODE_LEAVING_CHANGELIST_RECURSIVE
DELETE FROM actual_node
WHERE wc_id = ?1 AND (local_relpath = ?2 OR local_relpath LIKE ?3 ESCAPE '#')
      AND (changelist IS NULL
           OR local_relpath NOT IN (SELECT local_relpath FROM nodes_current
                                    WHERE wc_id = ?1
                                      AND (local_relpath = ?2
                                           OR local_relpath LIKE ?3 ESCAPE '#')
                                      AND kind = 'file'))

-- STMT_DELETE_CHILD_NODES_RECURSIVE
DELETE FROM nodes
WHERE wc_id = ?1 AND local_relpath LIKE ?2 ESCAPE '#' AND op_depth = ?3
  AND presence = ?4

-- STMT_CLEAR_ACTUAL_NODE_LEAVING_CHANGELIST
UPDATE actual_node
SET properties = NULL,
    text_mod = NULL,
    tree_conflict_data = NULL,
    conflict_old = NULL,
    conflict_new = NULL,
    conflict_working = NULL,
    prop_reject = NULL,
    older_checksum = NULL,
    left_checksum = NULL,
    right_checksum = NULL
WHERE wc_id = ?1 AND local_relpath = ?2

-- STMT_CLEAR_ACTUAL_NODE_LEAVING_CHANGELIST_RECURSIVE
UPDATE actual_node
SET properties = NULL,
    text_mod = NULL,
    tree_conflict_data = NULL,
    conflict_old = NULL,
    conflict_new = NULL,
    conflict_working = NULL,
    prop_reject = NULL,
    older_checksum = NULL,
    left_checksum = NULL,
    right_checksum = NULL
WHERE wc_id = ?1 AND (local_relpath = ?2 OR local_relpath LIKE ?3 ESCAPE '#')

-- STMT_CLEAR_ACTUAL_NODE_LEAVING_CONFLICT
UPDATE actual_node
SET properties = NULL,
    text_mod = NULL,
    changelist = NULL,
    conflict_old = NULL,
    conflict_new = NULL,
    conflict_working = NULL,
    prop_reject = NULL,
    older_checksum = NULL,
    left_checksum = NULL,
    right_checksum = NULL
WHERE wc_id = ?1 AND local_relpath = ?2

-- STMT_UPDATE_NODE_BASE_DEPTH
UPDATE nodes SET depth = ?3
WHERE wc_id = ?1 AND local_relpath = ?2 AND op_depth = 0

-- STMT_UPDATE_NODE_WORKING_DEPTH
UPDATE nodes SET depth = ?3
WHERE wc_id = ?1 AND local_relpath = ?2 AND
      op_depth = (SELECT MAX(op_depth) FROM nodes
                  WHERE wc_id = ?1 AND local_relpath = ?2 AND op_depth > 0)

-- STMT_UPDATE_NODE_BASE_EXCLUDED
UPDATE nodes SET presence = 'excluded', depth = NULL
WHERE wc_id = ?1 AND local_relpath = ?2 AND op_depth = 0

-- STMT_UPDATE_NODE_WORKING_EXCLUDED
UPDATE nodes SET presence = 'excluded', depth = NULL
WHERE wc_id = ?1 AND local_relpath = ?2 AND
      op_depth = (SELECT MAX(op_depth) FROM nodes
                  WHERE wc_id = ?1 AND local_relpath = ?2)

-- STMT_UPDATE_NODE_BASE_PRESENCE
UPDATE nodes SET presence = ?3
WHERE wc_id = ?1 AND local_relpath = ?2 AND op_depth = 0

-- STMT_UPDATE_NODE_WORKING_PRESENCE
UPDATE nodes SET presence = ?3
WHERE wc_id = ?1 AND local_relpath = ?2
  AND op_depth = (SELECT MAX(op_depth) FROM nodes
                  WHERE wc_id = ?1 AND local_relpath = ?2 AND op_depth > 0)

-- STMT_UPDATE_BASE_NODE_PRESENCE_REVNUM_AND_REPOS_PATH
UPDATE nodes SET presence = ?3, revision = ?4, repos_path = ?5
WHERE wc_id = ?1 AND local_relpath = ?2 AND op_depth = 0

-- STMT_LOOK_FOR_WORK
SELECT id FROM work_queue LIMIT 1

-- STMT_INSERT_WORK_ITEM
INSERT INTO work_queue (work) VALUES (?1)

-- STMT_SELECT_WORK_ITEM
SELECT id, work FROM work_queue ORDER BY id LIMIT 1

-- STMT_DELETE_WORK_ITEM
DELETE FROM work_queue WHERE id = ?1

-- STMT_INSERT_OR_IGNORE_PRISTINE
INSERT OR IGNORE INTO pristine (checksum, md5_checksum, size, refcount)
VALUES (?1, ?2, ?3, 0)

-- STMT_INSERT_PRISTINE
INSERT INTO pristine (checksum, md5_checksum, size, refcount)
VALUES (?1, ?2, ?3, 0)

-- STMT_SELECT_PRISTINE
SELECT md5_checksum
FROM pristine
WHERE checksum = ?1

-- STMT_SELECT_PRISTINE_BY_MD5
SELECT checksum
FROM pristine
WHERE md5_checksum = ?1

-- STMT_SELECT_UNREFERENCED_PRISTINES
SELECT checksum
FROM pristine
WHERE refcount = 0

-- STMT_DELETE_PRISTINE_IF_UNREFERENCED
DELETE FROM pristine
WHERE checksum = ?1 AND refcount = 0

-- STMT_SELECT_ACTUAL_CONFLICT_VICTIMS
SELECT local_relpath
FROM actual_node
WHERE wc_id = ?1 AND parent_relpath = ?2 AND
  NOT ((prop_reject IS NULL) AND (conflict_old IS NULL)
       AND (conflict_new IS NULL) AND (conflict_working IS NULL)
       AND (tree_conflict_data IS NULL))

-- STMT_SELECT_ACTUAL_CHILDREN_TREE_CONFLICT
SELECT local_relpath, tree_conflict_data
FROM actual_node
WHERE wc_id = ?1 AND parent_relpath = ?2 AND tree_conflict_data IS NOT NULL

-- STMT_SELECT_CONFLICT_DETAILS
SELECT prop_reject, conflict_old, conflict_new, conflict_working,
    tree_conflict_data
FROM actual_node
WHERE wc_id = ?1 AND local_relpath = ?2

-- STMT_CLEAR_TEXT_CONFLICT
UPDATE actual_node SET
  conflict_old = NULL,
  conflict_new = NULL,
  conflict_working = NULL
WHERE wc_id = ?1 AND local_relpath = ?2

-- STMT_CLEAR_PROPS_CONFLICT
UPDATE actual_node SET
  prop_reject = NULL
WHERE wc_id = ?1 AND local_relpath = ?2

-- STMT_INSERT_WC_LOCK
INSERT INTO wc_lock (wc_id, local_dir_relpath, locked_levels)
VALUES (?1, ?2, ?3)

-- STMT_SELECT_WC_LOCK
SELECT locked_levels FROM wc_lock
WHERE wc_id = ?1 AND local_dir_relpath = ?2

-- STMT_DELETE_WC_LOCK
DELETE FROM wc_lock
WHERE wc_id = ?1 AND local_dir_relpath = ?2

-- STMT_FIND_WC_LOCK
SELECT local_dir_relpath FROM wc_lock
WHERE wc_id = ?1 AND local_dir_relpath LIKE ?2 ESCAPE '#'

-- STMT_DELETE_WC_LOCK_ORPHAN
DELETE FROM wc_lock
WHERE wc_id = ?1 AND local_dir_relpath = ?2
AND NOT EXISTS (SELECT 1 FROM nodes
                 WHERE nodes.wc_id = ?1
                   AND nodes.local_relpath = wc_lock.local_dir_relpath)

-- STMT_DELETE_WC_LOCK_ORPHAN_RECURSIVE
DELETE FROM wc_lock
WHERE wc_id = ?1
AND (local_dir_relpath = ?2 OR local_dir_relpath LIKE ?3 ESCAPE '#')
AND NOT EXISTS (SELECT 1 FROM nodes
                 WHERE nodes.wc_id = ?1
                   AND nodes.local_relpath = wc_lock.local_dir_relpath)

-- STMT_APPLY_CHANGES_TO_BASE_NODE
/* translated_size and last_mod_time are not mentioned here because they will
   be tweaked after the working-file is installed.
   ### what to do about file_external?  */
INSERT OR REPLACE INTO nodes (
  wc_id, local_relpath, op_depth, parent_relpath, repos_id, repos_path,
  revision, presence, depth, kind, changed_revision, changed_date,
  changed_author, checksum, properties, dav_cache, symlink_target )
VALUES (?1, ?2, 0,
        ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14, ?15, ?16)

-- STMT_INSERT_WORKING_NODE_FROM_BASE
INSERT OR REPLACE INTO nodes (
    wc_id, local_relpath, op_depth, parent_relpath, presence, kind, checksum,
    changed_revision, changed_date, changed_author, depth, symlink_target,
    translated_size, last_mod_time, properties)
SELECT wc_id, local_relpath, ?3 /*op_depth*/, parent_relpath, ?4 /*presence*/,
       kind, checksum, changed_revision, changed_date, changed_author, depth,
       symlink_target, translated_size, last_mod_time, properties
FROM nodes
WHERE wc_id = ?1 AND local_relpath = ?2 AND op_depth = 0

-- STMT_INSERT_WORKING_NODE_FROM_NODE
INSERT OR REPLACE INTO nodes (
    wc_id, local_relpath, op_depth, parent_relpath, presence, kind, checksum,
    changed_revision, changed_date, changed_author, depth, symlink_target,
    translated_size, last_mod_time, properties)
SELECT wc_id, local_relpath, ?3 /*op_depth*/, parent_relpath, ?4 /*presence*/,
       kind, checksum, changed_revision, changed_date, changed_author, depth,
       symlink_target, translated_size, last_mod_time, properties
FROM nodes
WHERE wc_id = ?1 AND local_relpath = ?2
ORDER BY op_depth DESC
LIMIT 1

-- STMT_INSERT_WORKING_NODE_FROM_BASE_COPY
INSERT INTO nodes (
    wc_id, local_relpath, op_depth, parent_relpath, repos_id, repos_path,
    revision, presence, depth, kind, changed_revision, changed_date,
    changed_author, checksum, properties, translated_size, last_mod_time,
    symlink_target )
SELECT wc_id, local_relpath, ?3 /*op_depth*/, parent_relpath, repos_id,
    repos_path, revision, presence, depth, kind, changed_revision,
    changed_date, changed_author, checksum, properties, translated_size,
    last_mod_time, symlink_target
FROM nodes
WHERE wc_id = ?1 AND local_relpath = ?2 AND op_depth = 0

-- STMT_INSERT_WORKING_NODE_FROM_BASE_COPY_PRESENCE
INSERT INTO nodes (
    wc_id, local_relpath, op_depth, parent_relpath, repos_id, repos_path,
    revision, presence, depth, kind, changed_revision, changed_date,
    changed_author, checksum, properties, translated_size, last_mod_time,
    symlink_target )
SELECT wc_id, local_relpath, ?3 /*op_depth*/, parent_relpath, repos_id,
    repos_path, revision, ?4 /*presence*/, depth, kind, changed_revision,
    changed_date, changed_author, checksum, properties, translated_size,
    last_mod_time, symlink_target
FROM nodes
WHERE wc_id = ?1 AND local_relpath = ?2 AND op_depth = 0


/* ### these statements below should be setting copyfrom_revision! */
-- STMT_UPDATE_COPYFROM
UPDATE nodes SET repos_id = ?3, repos_path = ?4
WHERE wc_id = ?1 AND local_relpath = ?2
  AND op_depth = (SELECT MAX(op_depth) FROM nodes
                  WHERE wc_id = ?1 AND local_relpath = ?2 AND op_depth > 0)

-- STMT_UPDATE_OP_DEPTH_REDUCE_RECURSIVE
UPDATE nodes SET op_depth = ?3 - 1
WHERE wc_id = ?1 AND local_relpath LIKE ?2 ESCAPE '#' AND op_depth = ?3

-- STMT_UPDATE_OP_DEPTH_INCREASE_RECURSIVE
UPDATE nodes SET op_depth = ?3 + 1
WHERE wc_id = ?1 AND local_relpath LIKE ?2 ESCAPE '#' AND op_depth = ?3

-- STMT_UPDATE_OP_DEPTH
UPDATE nodes SET op_depth = ?4
WHERE wc_id = ?1 AND local_relpath = ?2 AND op_depth = ?3


-- STMT_UPDATE_COPYFROM_TO_INHERIT
UPDATE nodes SET
  repos_id = NULL,
  repos_path = NULL,
  revision = NULL
WHERE wc_id = ?1 AND local_relpath = ?2
  AND op_depth = (SELECT MAX(op_depth) FROM nodes
                  WHERE wc_id = ?1 AND local_relpath = ?2 AND op_depth > 0)

-- STMT_DETERMINE_WHICH_TREES_EXIST
SELECT op_depth FROM nodes WHERE wc_id = ?1 AND local_relpath = ?2
ORDER BY op_depth

/* ### This doesn't select all absent nodes, it only selects one of them. */
-- STMT_SELECT_ABSENT_NODES
SELECT local_relpath FROM nodes
WHERE wc_id = ?1 AND (local_relpath = ?2 OR local_relpath LIKE ?3 ESCAPE '#')
  AND op_depth = 0 AND presence = 'absent' LIMIT 1

/* ### Select all absent nodes. */
-- STMT_SELECT_ALL_ABSENT_NODES
SELECT local_relpath FROM nodes
WHERE wc_id = ?1 AND (local_relpath = ?2 OR local_relpath LIKE ?3 ESCAPE '#')
  AND op_depth = 0 AND presence = 'absent'

/* ### Why can't this query not just use the BASE repository
   location values, instead of taking 3 additional parameters?! */
-- STMT_INSERT_WORKING_NODE_COPY_FROM_BASE
INSERT OR REPLACE INTO nodes (
    wc_id, local_relpath, op_depth, parent_relpath, repos_id,
    repos_path, revision, presence, depth, kind, changed_revision,
    changed_date, changed_author, checksum, properties, translated_size,
    last_mod_time, symlink_target )
SELECT wc_id, ?3 /*local_relpath*/, ?4 /*op_depth*/, ?5 /*parent_relpath*/,
    ?6 /*repos_id*/, ?7 /*repos_path*/, ?8 /*revision*/, ?9 /*presence*/, depth,
    kind, changed_revision, changed_date, changed_author, checksum, properties,
    translated_size, last_mod_time, symlink_target
FROM nodes
WHERE wc_id = ?1 AND local_relpath = ?2 AND op_depth = 0

-- STMT_INSERT_WORKING_NODE_COPY_FROM_WORKING
INSERT OR REPLACE INTO nodes (
    wc_id, local_relpath, op_depth, parent_relpath, repos_id, repos_path,
    revision, presence, depth, kind, changed_revision, changed_date,
    changed_author, checksum, properties, translated_size, last_mod_time,
    symlink_target )
SELECT wc_id, ?3 /*local_relpath*/, ?4 /*op_depth*/, ?5 /*parent_relpath*/,
    ?6 /*repos_id*/, ?7 /*repos_path*/, ?8 /*revision*/, ?9 /*presence*/, depth,
    kind, changed_revision, changed_date, changed_author, checksum, properties,
    translated_size, last_mod_time, symlink_target
FROM nodes
WHERE wc_id = ?1 AND local_relpath = ?2 AND op_depth > 0
ORDER BY op_depth DESC
LIMIT 1

-- STMT_INSERT_ACTUAL_NODE_FROM_ACTUAL_NODE
INSERT OR REPLACE INTO actual_node (
     wc_id, local_relpath, parent_relpath, properties,
     conflict_old, conflict_new, conflict_working,
     prop_reject, changelist, text_mod, tree_conflict_data )
SELECT wc_id, ?3 /*local_relpath*/, ?4 /*parent_relpath*/, properties,
     conflict_old, conflict_new, conflict_working,
     prop_reject, changelist, text_mod, tree_conflict_data
FROM actual_node
WHERE wc_id = ?1 AND local_relpath = ?2

-- STMT_UPDATE_BASE_REVISION
UPDATE nodes SET revision = ?3
WHERE wc_id = ?1 AND local_relpath = ?2 AND op_depth = 0

-- STMT_UPDATE_BASE_REPOS
UPDATE nodes SET repos_id = ?3, repos_path = ?4
WHERE wc_id = ?1 AND local_relpath = ?2 AND op_depth = 0

-- STMT_SELECT_NODES_GE_OP_DEPTH_RECURSIVE
SELECT 1
FROM nodes
WHERE wc_id = ?1 AND (local_relpath = ?2 OR local_relpath LIKE ?3 ESCAPE '#')
  AND op_depth >= ?4

-- STMT_SELECT_ACTUAL_NODE_RECURSIVE
SELECT 1
FROM actual_node
WHERE wc_id = ?1 AND (local_relpath = ?2 OR local_relpath LIKE ?3 ESCAPE '#')
  AND tree_conflict_data IS NULL

-- STMT_SELECT_ACTUAL_CHILDREN
SELECT 1 FROM actual_node WHERE wc_id = ?1 AND parent_relpath = ?2

/* ------------------------------------------------------------------------- */

/* these are used in entries.c  */

-- STMT_INSERT_BASE_NODE_FOR_ENTRY
/* The BASE tree has a fixed op_depth '0' */
INSERT OR REPLACE INTO nodes (
  wc_id, local_relpath, op_depth, parent_relpath, repos_id, repos_path,
  revision, presence, kind, checksum,
  changed_revision, changed_date, changed_author, depth, properties,
  translated_size, last_mod_time )
VALUES (?1, ?2, 0, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13,
       ?14, ?15, ?16 )

-- STMT_INSERT_ACTUAL_NODE
INSERT OR REPLACE INTO actual_node (
  wc_id, local_relpath, parent_relpath, properties, conflict_old,
  conflict_new,
  conflict_working, prop_reject, changelist, text_mod,
  tree_conflict_data)
VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, NULL, ?10)

-- STMT_SELECT_NOT_PRESENT
SELECT 1 FROM nodes
WHERE wc_id = ?1 AND local_relpath = ?2 AND presence = 'not-present'
  AND op_depth = 0

-- STMT_SELECT_FILE_EXTERNAL
SELECT file_external FROM nodes
WHERE wc_id = ?1 AND local_relpath = ?2 AND op_depth = 0

-- STMT_UPDATE_FILE_EXTERNAL
UPDATE nodes SET file_external = ?3
WHERE wc_id = ?1 AND local_relpath = ?2 AND op_depth = 0

/* ------------------------------------------------------------------------- */

/* these are used in upgrade.c  */

-- STMT_UPDATE_ACTUAL_CONFLICT_DATA
UPDATE actual_node SET conflict_data = ?3
WHERE wc_id = ?1 AND local_relpath = ?2

-- STMT_INSERT_ACTUAL_CONFLICT_DATA
INSERT INTO actual_node (
  wc_id, local_relpath, conflict_data, parent_relpath)
VALUES (?1, ?2, ?3, ?4)

-- STMT_SELECT_OLD_TREE_CONFLICT
SELECT wc_id, local_relpath, tree_conflict_data
FROM actual_node
WHERE tree_conflict_data IS NOT NULL

-- STMT_ERASE_OLD_CONFLICTS
UPDATE actual_node SET tree_conflict_data = NULL

-- STMT_SELECT_ALL_FILES
/* Should this select on wc_id as well? */
SELECT DISTINCT local_relpath FROM nodes
WHERE kind = 'file' AND parent_relpath = ?1

-- STMT_SELECT_NODE_UPGRADE
SELECT op_depth, presence, wc_id
FROM nodes
WHERE local_relpath = ?1
ORDER BY op_depth DESC

-- STMT_UPDATE_NODE_PROPS
UPDATE nodes SET properties = ?4
WHERE wc_id = ?1 AND local_relpath = ?2 AND op_depth = ?3

-- STMT_HAS_WORKING_NODES
SELECT 1 FROM nodes WHERE op_depth > 0

-- STMT_HAS_ACTUAL_NODES_CONFLICTS
SELECT 1 FROM actual_node
WHERE NOT ((prop_reject IS NULL) AND (conflict_old IS NULL)
           AND (conflict_new IS NULL) AND (conflict_working IS NULL)
           AND (tree_conflict_data IS NULL))

-- STMT_UPDATE_CHECKSUM
UPDATE nodes SET checksum = ?4
WHERE wc_id = ?1 AND local_relpath = ?2 AND op_depth = ?3

/* ------------------------------------------------------------------------- */
/* PROOF OF CONCEPT: Complex queries for callback walks, caching results
                     in a temporary table. */

-- STMT_CLEAR_NODE_PROPS_CACHE
DROP TABLE IF EXISTS temp__node_props_cache

-- STMT_CACHE_NODE_PROPS_RECURSIVE
CREATE TEMPORARY TABLE temp__node_props_cache AS
  SELECT local_relpath, kind, properties FROM nodes_current
  WHERE wc_id = ?1
    AND (?2 = '' OR local_relpath = ?2 OR local_relpath LIKE ?3 ESCAPE '#')
    AND local_relpath NOT IN (
      SELECT local_relpath FROM actual_node WHERE wc_id = ?1)
    AND (presence = 'normal' OR presence = 'incomplete')
/* ###  Need index?
CREATE UNIQUE INDEX temp__node_props_cache_unique
  ON temp__node_props_cache (local_relpath) */

-- STMT_CACHE_ACTUAL_PROPS_RECURSIVE
INSERT INTO temp__node_props_cache (local_relpath, kind, properties)
  SELECT A.local_relpath, N.kind, A.properties
  FROM actual_node AS A JOIN nodes_current AS N
    ON A.wc_id = N.wc_id AND A.local_relpath = N.local_relpath
       AND (N.presence = 'normal' OR N.presence = 'incomplete')
  WHERE A.wc_id = ?1
    AND (?2 = '' OR A.local_relpath = ?2 OR A.local_relpath LIKE ?3 ESCAPE '#')
    AND A.local_relpath NOT IN
      (SELECT local_relpath FROM temp__node_props_cache)

-- STMT_CACHE_NODE_BASE_PROPS_RECURSIVE
CREATE TEMPORARY TABLE temp__node_props_cache AS
  SELECT local_relpath, kind, properties FROM nodes_base
  WHERE wc_id = ?1
    AND (?2 = '' OR local_relpath = ?2 OR local_relpath LIKE ?3 ESCAPE '#')
    AND local_relpath NOT IN (
      SELECT local_relpath FROM actual_node WHERE wc_id = ?1)
    AND (presence = 'normal' OR presence = 'incomplete')
/* ###  Need index?
CREATE UNIQUE INDEX temp__node_props_cache_unique
  ON temp__node_props_cache (local_relpath) */

-- STMT_CACHE_NODE_PROPS_OF_CHILDREN
CREATE TEMPORARY TABLE temp__node_props_cache AS
  SELECT local_relpath, kind, properties FROM nodes_current
  WHERE wc_id = ?1
    AND (local_relpath = ?2 OR parent_relpath = ?2)
    AND local_relpath NOT IN (
      SELECT local_relpath FROM actual_node WHERE wc_id = ?1)
    AND (presence = 'normal' OR presence = 'incomplete')
/* ###  Need index?
CREATE UNIQUE INDEX temp__node_props_cache_unique
  ON temp__node_props_cache (local_relpath) */

-- STMT_CACHE_ACTUAL_PROPS_OF_CHILDREN
INSERT INTO temp__node_props_cache (local_relpath, kind, properties)
  SELECT A.local_relpath, N.kind, A.properties
  FROM actual_node AS A JOIN nodes_current AS N
    ON A.wc_id = N.wc_id AND A.local_relpath = N.local_relpath
       AND (N.presence = 'normal' OR N.presence = 'incomplete')
  WHERE A.wc_id = ?1
    AND (A.local_relpath = ?2 OR A.parent_relpath = ?2)
    AND A.local_relpath NOT IN
      (SELECT local_relpath FROM temp__node_props_cache)

-- STMT_CACHE_NODE_BASE_PROPS_OF_CHILDREN
CREATE TEMPORARY TABLE temp__node_props_cache AS
  SELECT local_relpath, kind, properties FROM nodes_base
  WHERE wc_id = ?1
    AND (local_relpath = ?2 OR parent_relpath = ?2)
    AND local_relpath NOT IN (
      SELECT local_relpath FROM actual_node WHERE wc_id = ?1)
    AND (presence = 'normal' OR presence = 'incomplete')
/* ###  Need index?
CREATE UNIQUE INDEX temp__node_props_cache_unique
  ON temp__node_props_cache (local_relpath) */

-- STMT_SELECT_RELEVANT_PROPS_FROM_CACHE
SELECT local_relpath, kind, properties FROM temp__node_props_cache
ORDER BY local_relpath

-- STMT_CREATE_REVERT_LIST
DROP TABLE IF EXISTS revert_list;
CREATE TEMPORARY TABLE revert_list (
   local_relpath TEXT PRIMARY KEY,
   conflict_old TEXT,
   conflict_new TEXT,
   conflict_working TEXT,
   prop_reject TEXT,
   notify INTEGER
   );
CREATE UNIQUE INDEX revert_list_index ON revert_list(local_relpath);
DROP TRIGGER IF EXISTS   trigger_revert_list_nodes;
CREATE TEMPORARY TRIGGER trigger_revert_list_nodes
BEFORE DELETE ON nodes
BEGIN
   INSERT OR REPLACE INTO revert_list(local_relpath, notify)
   SELECT OLD.local_relpath, 1;
END;
DROP TRIGGER IF EXISTS   trigger_revert_list_actual_delete;
CREATE TEMPORARY TRIGGER trigger_revert_list_actual_delete
BEFORE DELETE ON actual_node
BEGIN
   INSERT OR REPLACE INTO revert_list(local_relpath, conflict_old,
                                       conflict_new, conflict_working,
                                       prop_reject, notify)
   SELECT OLD.local_relpath,
          OLD.conflict_old, OLD.conflict_new, OLD.conflict_working,
          OLD.prop_reject,
          CASE
          WHEN OLD.properties IS NOT NULL OR OLD.tree_conflict_data IS NOT NULL
          THEN 1 ELSE NULL END;
END;
DROP TRIGGER IF EXISTS   trigger_revert_list_actual_update;
CREATE TEMPORARY TRIGGER trigger_revert_list_actual_update
BEFORE UPDATE ON actual_node
BEGIN
   INSERT OR REPLACE INTO revert_list(local_relpath, conflict_old,
                                       conflict_new, conflict_working,
                                       prop_reject, notify)
   SELECT OLD.local_relpath,
          OLD.conflict_old, OLD.conflict_new, OLD.conflict_working,
          OLD.prop_reject,
          CASE
          WHEN OLD.properties IS NOT NULL OR OLD.tree_conflict_data IS NOT NULL
          THEN 1 ELSE NULL END;
END

-- STMT_DROP_REVERT_LIST_TRIGGERS
DROP TRIGGER IF EXISTS trigger_revert_list_nodes;
DROP TRIGGER IF EXISTS trigger_revert_list_actual_delete;
DROP TRIGGER IF EXISTS trigger_revert_list_actual_update

-- STMT_SELECT_REVERT_LIST
SELECT conflict_old, conflict_new, conflict_working, prop_reject, notify
FROM revert_list
WHERE local_relpath = ?1

-- STMT_DELETE_REVERT_LIST
DELETE FROM revert_list WHERE local_relpath = ?1

-- STMT_SELECT_REVERT_LIST_RECURSIVE
SELECT local_relpath, notify
FROM revert_list
WHERE local_relpath = ?1 or local_relpath LIKE ?2 ESCAPE '#'
ORDER BY local_relpath

-- STMT_DELETE_REVERT_LIST_RECURSIVE
DELETE FROM revert_list
WHERE local_relpath = ?1 OR local_relpath LIKE ?2 ESCAPE '#'


/* ------------------------------------------------------------------------- */

/* Queries for revision status. */

-- STMT_SELECT_MIN_MAX_REVISIONS
SELECT MIN(revision), MAX(revision),
       MIN(changed_revision), MAX(changed_revision) FROM nodes
  WHERE wc_id = ?1
  AND (local_relpath = ?2 OR local_relpath LIKE ?3 ESCAPE '#')
  AND (presence = 'normal' OR presence = 'incomplete')
  AND file_external IS NULL
  AND op_depth = 0

-- STMT_SELECT_SPARSE_NODES
SELECT local_relpath, presence, depth FROM nodes
WHERE wc_id = ?1 AND (local_relpath = ?2 OR local_relpath LIKE ?3 ESCAPE '#')
  AND op_depth = 0
  AND (presence = 'absent' OR presence = 'excluded' OR
       (depth != 'infinity' AND depth != 'unknown'))
  AND file_external IS NULL

-- STMT_SUBTREE_HAS_TREE_MODIFICATIONS
SELECT 1 FROM nodes
WHERE wc_id = ?1 AND (local_relpath = ?2 OR local_relpath LIKE ?3 ESCAPE '#')
  AND op_depth > 0
  AND (presence = 'normal' OR presence = 'incomplete' OR
       presence = 'base-deleted')
  AND file_external IS NULL
LIMIT 1

-- STMT_SUBTREE_HAS_PROP_MODIFICATIONS
SELECT 1 FROM actual_node
WHERE wc_id = ?1 AND (local_relpath = ?2 OR local_relpath LIKE ?3 ESCAPE '#')
  AND properties IS NOT NULL
LIMIT 1

/* This query expects repos_path(wcroot)/% as arg 4,
   and repos_path(wcroot), with a slash appended unless the path is empty,
   as arg 5. */
-- STMT_SELECT_SWITCHED_NODES
SELECT local_relpath FROM nodes_base
WHERE wc_id = ?1 AND local_relpath != ""
  AND (local_relpath = ?2 OR local_relpath LIKE ?3 ESCAPE '#')
  AND ((repos_path NOT LIKE ?4 ESCAPE '#' AND repos_path != local_relpath)
       OR (repos_path != ?5 || local_relpath))
  AND file_external IS NULL

-- STMT_SELECT_CURRENT_NODES_RECURSIVE
SELECT local_relpath, kind FROM nodes_current
WHERE wc_id = ?1
  AND (?2 = '' OR local_relpath = ?2 OR local_relpath LIKE ?3 ESCAPE '#')
  AND (presence = 'normal' OR presence = 'incomplete')
  AND file_external IS NULL

/* Grab all the statements related to the schema.  */

-- include: wc-metadata
