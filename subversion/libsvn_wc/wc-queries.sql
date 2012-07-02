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
  symlink_target, last_mod_time, properties, moved_here
FROM nodes
WHERE wc_id = ?1 AND local_relpath = ?2
ORDER BY op_depth DESC

-- STMT_SELECT_NODE_INFO_WITH_LOCK
SELECT op_depth, nodes.repos_id, nodes.repos_path, presence, kind, revision,
  checksum, translated_size, changed_revision, changed_date, changed_author,
  depth, symlink_target, last_mod_time, properties, moved_here,
  /* All the columns until now must match those returned by
     STMT_SELECT_NODE_INFO. The implementation of svn_wc__db_read_info()
     assumes that these columns are followed by the lock information) */
  lock_token, lock_owner, lock_comment, lock_date
FROM nodes
LEFT OUTER JOIN lock ON nodes.repos_id = lock.repos_id
  AND nodes.repos_path = lock.repos_relpath
WHERE wc_id = ?1 AND local_relpath = ?2
ORDER BY op_depth DESC

-- STMT_SELECT_BASE_NODE
SELECT repos_id, repos_path, presence, kind, revision, checksum,
  translated_size, changed_revision, changed_date, changed_author, depth,
  symlink_target, last_mod_time, properties, file_external
FROM nodes
WHERE wc_id = ?1 AND local_relpath = ?2 AND op_depth = 0

-- STMT_SELECT_BASE_NODE_WITH_LOCK
SELECT nodes.repos_id, nodes.repos_path, presence, kind, revision,
  checksum, translated_size, changed_revision, changed_date, changed_author,
  depth, symlink_target, last_mod_time, properties, file_external,
  /* All the columns until now must match those returned by
     STMT_SELECT_BASE_NODE. The implementation of svn_wc__db_base_get_info()
     assumes that these columns are followed by the lock information) */
  lock_token, lock_owner, lock_comment, lock_date
FROM nodes
LEFT OUTER JOIN lock ON nodes.repos_id = lock.repos_id
  AND nodes.repos_path = lock.repos_relpath
WHERE wc_id = ?1 AND local_relpath = ?2 AND op_depth = 0

-- STMT_SELECT_BASE_CHILDREN_INFO
SELECT local_relpath, nodes.repos_id, nodes.repos_path, presence, kind,
  revision, depth, file_external,
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

-- STMT_SELECT_DEPTH_NODE
SELECT repos_id, repos_path, presence, kind, revision, checksum,
  translated_size, changed_revision, changed_date, changed_author, depth,
  symlink_target, last_mod_time, properties
FROM nodes
WHERE wc_id = ?1 AND local_relpath = ?2 AND op_depth = ?3

-- STMT_SELECT_LOWEST_WORKING_NODE
SELECT op_depth, presence
FROM nodes
WHERE wc_id = ?1 AND local_relpath = ?2 AND op_depth > 0
ORDER BY op_depth
LIMIT 1

-- STMT_SELECT_ACTUAL_NODE
SELECT changelist, properties, conflict_data,
conflict_old, conflict_new, conflict_working, prop_reject, tree_conflict_data
FROM actual_node
WHERE wc_id = ?1 AND local_relpath = ?2

-- STMT_SELECT_ACTUAL_TREE_CONFLICT
SELECT tree_conflict_data
FROM actual_node
WHERE wc_id = ?1 AND local_relpath = ?2 AND tree_conflict_data IS NOT NULL

-- STMT_SELECT_ACTUAL_CHANGELIST
SELECT changelist
FROM actual_node
WHERE wc_id = ?1 AND local_relpath = ?2 AND changelist IS NOT NULL

-- STMT_SELECT_NODE_CHILDREN_INFO
/* Getting rows in an advantageous order using
     ORDER BY local_relpath, op_depth DESC
   turns out to be slower than getting rows in a random order and making the
   C code handle it. */
SELECT op_depth, nodes.repos_id, nodes.repos_path, presence, kind, revision,
  checksum, translated_size, changed_revision, changed_date, changed_author,
  depth, symlink_target, last_mod_time, properties, lock_token, lock_owner,
  lock_comment, lock_date, local_relpath, moved_here, moved_to, file_external
FROM nodes
LEFT OUTER JOIN lock ON nodes.repos_id = lock.repos_id
  AND nodes.repos_path = lock.repos_relpath AND op_depth = 0
WHERE wc_id = ?1 AND parent_relpath = ?2

-- STMT_SELECT_NODE_CHILDREN_WALKER_INFO
SELECT local_relpath, op_depth, presence, kind
FROM nodes_current
WHERE wc_id = ?1 AND parent_relpath = ?2

-- STMT_SELECT_ACTUAL_CHILDREN_INFO
SELECT local_relpath, changelist, properties, conflict_data,
conflict_old, conflict_new, conflict_working, prop_reject, tree_conflict_data
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
  dav_cache, symlink_target, file_external, moved_to, moved_here)
VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14,
        ?15, ?16, ?17, ?18, ?19, ?20, ?21, ?22)

-- STMT_SELECT_OP_DEPTH_CHILDREN
SELECT local_relpath FROM nodes
WHERE wc_id = ?1 AND parent_relpath = ?2 AND op_depth = ?3
  AND (?3 != 0 OR file_external is NULL)

-- STMT_SELECT_GE_OP_DEPTH_CHILDREN
SELECT 1 FROM nodes
WHERE wc_id = ?1 AND parent_relpath = ?2
  AND (op_depth > ?3 OR (op_depth = ?3 AND presence != 'base-deleted'))
UNION ALL
SELECT 1 FROM ACTUAL_NODE
WHERE wc_id = ?1 AND parent_relpath = ?2

/* Delete the nodes shadowed by local_relpath. Not valid for the wc-root */
-- STMT_DELETE_SHADOWED_RECURSIVE
DELETE FROM nodes
WHERE wc_id = ?1
  AND IS_STRICT_DESCENDANT_OF(local_relpath, ?2)
  AND (op_depth < ?3
       OR (op_depth = ?3 AND presence = 'base-deleted'))

/* Get not-present descendants of a copied node. Not valid for the wc-root */
-- STMT_SELECT_NOT_PRESENT_DESCENDANTS
SELECT local_relpath FROM nodes
WHERE wc_id = ?1 AND op_depth = ?3
  AND IS_STRICT_DESCENDANT_OF(local_relpath, ?2)
  AND presence == 'not-present'

-- STMT_COMMIT_DESCENDANT_TO_BASE
UPDATE NODES SET op_depth = 0, repos_id = ?4, repos_path = ?5, revision = ?6,
  moved_here = NULL, moved_to = NULL, dav_cache = NULL,
  presence = CASE presence WHEN 'normal' THEN 'normal'
                           WHEN 'excluded' THEN 'excluded'
                           ELSE 'not-present' END
WHERE wc_id = ?1 AND local_relpath = ?2 and op_depth = ?3

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

-- STMT_SELECT_ACTUAL_PROPS
SELECT properties FROM actual_node
WHERE wc_id = ?1 AND local_relpath = ?2

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

/* Not valid for the working copy root */
-- STMT_SELECT_BASE_NODE_LOCK_TOKENS_RECURSIVE
SELECT nodes.repos_id, nodes.repos_path, lock_token
FROM nodes
LEFT JOIN lock ON nodes.repos_id = lock.repos_id
  AND nodes.repos_path = lock.repos_relpath
WHERE wc_id = ?1 AND op_depth = 0
  AND IS_STRICT_DESCENDANT_OF(local_relpath, ?2)

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
SELECT (SELECT b.presence FROM nodes AS b
         WHERE b.wc_id = ?1 AND b.local_relpath = ?2 AND b.op_depth = 0),
       work.presence, work.op_depth
FROM nodes_current AS work
WHERE work.wc_id = ?1 AND work.local_relpath = ?2 AND work.op_depth > 0
LIMIT 1

-- STMT_SELECT_DELETION_INFO_SCAN
/* ### FIXME.  modes_move.moved_to IS NOT NULL works when there is
 only one move but we need something else when there are several. */
SELECT (SELECT b.presence FROM nodes AS b
         WHERE b.wc_id = ?1 AND b.local_relpath = ?2 AND b.op_depth = 0),
       work.presence, work.op_depth, moved.moved_to
FROM nodes_current AS work
LEFT OUTER JOIN nodes AS moved 
  ON moved.wc_id = work.wc_id
 AND moved.local_relpath = work.local_relpath
 AND moved.moved_to IS NOT NULL
WHERE work.wc_id = ?1 AND work.local_relpath = ?2 AND work.op_depth > 0
LIMIT 1

-- STMT_SELECT_OP_DEPTH_MOVED_TO
SELECT op_depth, moved_to, repos_path, revision
FROM nodes
WHERE wc_id = ?1 AND local_relpath = ?2
 AND op_depth <= (SELECT MIN(op_depth) FROM nodes
                  WHERE wc_id = ?1 AND local_relpath = ?2 AND op_depth > ?3)
ORDER BY op_depth DESC

-- STMT_SELECT_MOVED_TO
SELECT moved_to
FROM nodes
WHERE wc_id = ?1 AND local_relpath = ?2 AND op_depth = ?3

-- STMT_SELECT_MOVED_HERE
SELECT moved_here, presence, repos_path, revision
FROM nodes
WHERE wc_id = ?1 AND local_relpath = ?2 AND op_depth >= ?3
ORDER BY op_depth
                  
-- STMT_DELETE_LOCK
DELETE FROM lock
WHERE repos_id = ?1 AND repos_relpath = ?2

-- STMT_CLEAR_BASE_NODE_RECURSIVE_DAV_CACHE
UPDATE nodes SET dav_cache = NULL
WHERE dav_cache IS NOT NULL AND wc_id = ?1 AND op_depth = 0
  AND (local_relpath = ?2
       OR IS_STRICT_DESCENDANT_OF(local_relpath, ?2))

-- STMT_RECURSIVE_UPDATE_NODE_REPO
UPDATE nodes SET repos_id = ?4, dav_cache = NULL
/* ### The Sqlite optimizer needs help here ###
 * WHERE wc_id = ?1
 *   AND repos_id = ?3
 *   AND (local_relpath = ?2
 *        OR IS_STRICT_DESCENDANT_OF(local_relpath, ?2))*/
WHERE (wc_id = ?1 AND local_relpath = ?2 AND repos_id = ?3)
   OR (wc_id = ?1 AND IS_STRICT_DESCENDANT_OF(local_relpath, ?2)
       AND repos_id = ?3)
 

-- STMT_UPDATE_LOCK_REPOS_ID
UPDATE lock SET repos_id = ?2
WHERE repos_id = ?1

-- STMT_UPDATE_NODE_FILEINFO
UPDATE nodes SET translated_size = ?3, last_mod_time = ?4
WHERE wc_id = ?1 AND local_relpath = ?2
  AND op_depth = (SELECT MAX(op_depth) FROM nodes
                  WHERE wc_id = ?1 AND local_relpath = ?2)

-- STMT_UPDATE_NODE_FILEINFO_OPDEPTH
UPDATE nodes SET translated_size = ?3, last_mod_time = ?4
WHERE wc_id = ?1 AND local_relpath = ?2 AND op_depth = ?5

-- STMT_INSERT_ACTUAL_CONFLICT
INSERT INTO actual_node (
  wc_id, local_relpath, conflict_data,
  conflict_old, conflict_new, conflict_working, prop_reject,
  tree_conflict_data, parent_relpath)
VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9)

-- STMT_UPDATE_ACTUAL_CONFLICT
UPDATE actual_node SET conflict_data = ?3,
  conflict_old = ?4, conflict_new = ?5, conflict_working = ?6,
  prop_reject = ?7, tree_conflict_data = ?8
WHERE wc_id = ?1 AND local_relpath = ?2

-- STMT_UPDATE_ACTUAL_CHANGELISTS
UPDATE actual_node SET changelist = ?3
WHERE wc_id = ?1
  AND (local_relpath = ?2 OR IS_STRICT_DESCENDANT_OF(local_relpath, ?2))
  AND local_relpath = (SELECT local_relpath FROM targets_list AS t
                       WHERE wc_id = ?1
                         AND t.local_relpath = actual_node.local_relpath
                         AND kind = 'file')

-- STMT_UPDATE_ACTUAL_CLEAR_CHANGELIST
UPDATE actual_node SET changelist = NULL
 WHERE wc_id = ?1 AND local_relpath = ?2

-- STMT_MARK_SKIPPED_CHANGELIST_DIRS
/* 7 corresponds to svn_wc_notify_skip */
INSERT INTO changelist_list (wc_id, local_relpath, notify, changelist)
SELECT wc_id, local_relpath, 7, ?3
FROM targets_list
WHERE wc_id = ?1
  AND (local_relpath = ?2 OR IS_STRICT_DESCENDANT_OF(local_relpath, ?2))
  AND kind = 'dir'

-- STMT_RESET_ACTUAL_WITH_CHANGELIST
REPLACE INTO actual_node (
  wc_id, local_relpath, parent_relpath, changelist)
VALUES (?1, ?2, ?3, ?4)

-- STMT_CREATE_CHANGELIST_LIST
DROP TABLE IF EXISTS changelist_list;
CREATE TEMPORARY TABLE changelist_list (
  wc_id  INTEGER NOT NULL,
  local_relpath TEXT NOT NULL,
  notify INTEGER NOT NULL,
  changelist TEXT NOT NULL,
  /* Order NOTIFY descending to make us show clears (27) before adds (26) */
  PRIMARY KEY (wc_id, local_relpath, notify DESC)
)

/* Create notify items for when a node is removed from a changelist and
   when a node is added to a changelist. Make sure nothing is notified
   if there were no changes.
*/
-- STMT_CREATE_CHANGELIST_TRIGGER
DROP TRIGGER IF EXISTS   trigger_changelist_list_change;
CREATE TEMPORARY TRIGGER trigger_changelist_list_change
BEFORE UPDATE ON actual_node
WHEN old.changelist IS NOT new.changelist
BEGIN
  /* 27 corresponds to svn_wc_notify_changelist_clear */
  INSERT INTO changelist_list(wc_id, local_relpath, notify, changelist)
  SELECT old.wc_id, old.local_relpath, 27, old.changelist
   WHERE old.changelist is NOT NULL;

  /* 26 corresponds to svn_wc_notify_changelist_set */
  INSERT INTO changelist_list(wc_id, local_relpath, notify, changelist)
  SELECT new.wc_id, new.local_relpath, 26, new.changelist
   WHERE new.changelist IS NOT NULL;
END

-- STMT_INSERT_CHANGELIST_LIST
INSERT INTO changelist_list(wc_id, local_relpath, notify, changelist)
VALUES (?1, ?2, ?3, ?4)

-- STMT_FINALIZE_CHANGELIST
DROP TRIGGER IF EXISTS trigger_changelist_list_actual_cl_insert;
DROP TRIGGER IF EXISTS trigger_changelist_list_actual_cl_set;
DROP TRIGGER IF EXISTS trigger_changelist_list_actual_cl_clear;
DROP TABLE IF EXISTS changelist_list;
DROP TABLE IF EXISTS targets_list

-- STMT_SELECT_CHANGELIST_LIST
SELECT wc_id, local_relpath, notify, changelist
FROM changelist_list
ORDER BY wc_id, local_relpath ASC, notify DESC

-- STMT_CREATE_TARGETS_LIST
DROP TABLE IF EXISTS targets_list;
CREATE TEMPORARY TABLE targets_list (
  wc_id  INTEGER NOT NULL,
  local_relpath TEXT NOT NULL,
  parent_relpath TEXT,
  kind TEXT NOT NULL,
  PRIMARY KEY (wc_id, local_relpath)
  );
/* need more indicies? */

-- STMT_DROP_TARGETS_LIST
DROP TABLE IF EXISTS targets_list

-- STMT_INSERT_TARGET
INSERT INTO targets_list(wc_id, local_relpath, parent_relpath, kind)
SELECT wc_id, local_relpath, parent_relpath, kind
FROM nodes_current
WHERE wc_id = ?1
  AND local_relpath = ?2

-- STMT_INSERT_TARGET_DEPTH_FILES
INSERT INTO targets_list(wc_id, local_relpath, parent_relpath, kind)
SELECT wc_id, local_relpath, parent_relpath, kind
FROM nodes_current
WHERE wc_id = ?1
  AND parent_relpath = ?2
  AND kind = 'file'

-- STMT_INSERT_TARGET_DEPTH_IMMEDIATES
INSERT INTO targets_list(wc_id, local_relpath, parent_relpath, kind)
SELECT wc_id, local_relpath, parent_relpath, kind
FROM nodes_current
WHERE wc_id = ?1
  AND parent_relpath = ?2

-- STMT_INSERT_TARGET_DEPTH_INFINITY
INSERT INTO targets_list(wc_id, local_relpath, parent_relpath, kind)
SELECT wc_id, local_relpath, parent_relpath, kind
FROM nodes_current
WHERE wc_id = ?1
  AND IS_STRICT_DESCENDANT_OF(local_relpath, ?2)

-- STMT_INSERT_TARGET_WITH_CHANGELIST
INSERT INTO targets_list(wc_id, local_relpath, parent_relpath, kind)
SELECT N.wc_id, N.local_relpath, N.parent_relpath, N.kind
  FROM actual_node AS A JOIN nodes_current AS N
    ON A.wc_id = N.wc_id AND A.local_relpath = N.local_relpath
 WHERE N.wc_id = ?1
   AND N.local_relpath = ?2
   AND A.changelist = ?3

-- STMT_INSERT_TARGET_WITH_CHANGELIST_DEPTH_FILES
INSERT INTO targets_list(wc_id, local_relpath, parent_relpath, kind)
SELECT N.wc_id, N.local_relpath, N.parent_relpath, N.kind
  FROM actual_node AS A JOIN nodes_current AS N
    ON A.wc_id = N.wc_id AND A.local_relpath = N.local_relpath
 WHERE N.wc_id = ?1
   AND N.parent_relpath = ?2
   AND kind = 'file'
   AND A.changelist = ?3

-- STMT_INSERT_TARGET_WITH_CHANGELIST_DEPTH_IMMEDIATES
INSERT INTO targets_list(wc_id, local_relpath, parent_relpath, kind)
SELECT N.wc_id, N.local_relpath, N.parent_relpath, N.kind
  FROM actual_node AS A JOIN nodes_current AS N
    ON A.wc_id = N.wc_id AND A.local_relpath = N.local_relpath
 WHERE N.wc_id = ?1
   AND N.parent_relpath = ?2
  AND A.changelist = ?3

-- STMT_INSERT_TARGET_WITH_CHANGELIST_DEPTH_INFINITY
INSERT INTO targets_list(wc_id, local_relpath, parent_relpath, kind)
SELECT N.wc_id, N.local_relpath, N.parent_relpath, N.kind
  FROM actual_node AS A JOIN nodes_current AS N
    ON A.wc_id = N.wc_id AND A.local_relpath = N.local_relpath
 WHERE N.wc_id = ?1
   AND IS_STRICT_DESCENDANT_OF(N.local_relpath, ?2)
   AND A.changelist = ?3

/* Only used by commented dump_targets() in wc_db.c */
/*-- STMT_SELECT_TARGETS
SELECT local_relpath, parent_relpath from targets_list*/

-- STMT_INSERT_ACTUAL_EMPTIES
INSERT OR IGNORE INTO actual_node (
     wc_id, local_relpath, parent_relpath)
SELECT wc_id, local_relpath, parent_relpath
FROM targets_list

-- STMT_DELETE_ACTUAL_EMPTY
DELETE FROM actual_node
WHERE wc_id = ?1 AND local_relpath = ?2
  AND properties IS NULL
  AND conflict_data IS NULL
  AND conflict_old IS NULL
  AND conflict_new IS NULL
  AND prop_reject IS NULL
  AND tree_conflict_data IS NULL
  AND changelist IS NULL
  AND text_mod IS NULL
  AND older_checksum IS NULL
  AND right_checksum IS NULL
  AND left_checksum IS NULL

-- STMT_DELETE_ACTUAL_EMPTIES
DELETE FROM actual_node
WHERE wc_id = ?1
  AND IS_STRICT_DESCENDANT_OF(local_relpath, ?2)
  AND properties IS NULL
  AND conflict_data IS NULL
  AND conflict_old IS NULL
  AND conflict_new IS NULL
  AND prop_reject IS NULL
  AND tree_conflict_data IS NULL
  AND changelist IS NULL
  AND text_mod IS NULL
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

-- STMT_DELETE_ALL_LAYERS
DELETE FROM nodes
WHERE wc_id = ?1 AND local_relpath = ?2

-- STMT_DELETE_NODES_ABOVE_DEPTH_RECURSIVE
DELETE FROM nodes
WHERE wc_id = ?1
  AND (local_relpath = ?2
       OR IS_STRICT_DESCENDANT_OF(local_relpath, ?2))
  AND op_depth >= ?3

-- STMT_DELETE_ACTUAL_NODE
DELETE FROM actual_node
WHERE wc_id = ?1 AND local_relpath = ?2

/* Will not delete recursive when run on the wcroot */
-- STMT_DELETE_ACTUAL_NODE_RECURSIVE
DELETE FROM actual_node
WHERE wc_id = ?1
  AND (local_relpath = ?2
       OR IS_STRICT_DESCENDANT_OF(local_relpath, ?2))

-- STMT_DELETE_ACTUAL_NODE_WITHOUT_CONFLICT
DELETE FROM actual_node
WHERE wc_id = ?1 AND local_relpath = ?2
      AND tree_conflict_data IS NULL

-- STMT_DELETE_ACTUAL_NODE_LEAVING_CHANGELIST
DELETE FROM actual_node
WHERE wc_id = ?1
  AND local_relpath = ?2
  AND (changelist IS NULL
       OR NOT EXISTS (SELECT 1 FROM nodes_current c
                      WHERE c.wc_id = ?1 AND c.local_relpath = ?2
                        AND c.kind = 'file'))

-- STMT_DELETE_ACTUAL_NODE_LEAVING_CHANGELIST_RECURSIVE
DELETE FROM actual_node
WHERE wc_id = ?1
  AND (local_relpath = ?2
       OR IS_STRICT_DESCENDANT_OF(local_relpath, ?2))
  AND (changelist IS NULL
       OR NOT EXISTS (SELECT 1 FROM nodes_current c
                      WHERE c.wc_id = ?1 
                        AND c.local_relpath = actual_node.local_relpath
                        AND c.kind = 'file'))

-- STMT_CLEAR_ACTUAL_NODE_LEAVING_CHANGELIST
UPDATE actual_node
SET properties = NULL,
    text_mod = NULL,
    conflict_data = NULL,
    conflict_old = NULL,
    conflict_new = NULL,
    conflict_working = NULL,
    prop_reject = NULL,
    tree_conflict_data = NULL,
    older_checksum = NULL,
    left_checksum = NULL,
    right_checksum = NULL
WHERE wc_id = ?1 AND local_relpath = ?2

-- STMT_CLEAR_ACTUAL_NODE_LEAVING_CHANGELIST_RECURSIVE
UPDATE actual_node
SET properties = NULL,
    text_mod = NULL,
    conflict_data = NULL,
    conflict_old = NULL,
    conflict_new = NULL,
    conflict_working = NULL,
    prop_reject = NULL,
    tree_conflict_data = NULL,
    older_checksum = NULL,
    left_checksum = NULL,
    right_checksum = NULL
WHERE wc_id = ?1
  AND (local_relpath = ?2
       OR IS_STRICT_DESCENDANT_OF(local_relpath, ?2))

-- STMT_UPDATE_NODE_BASE_DEPTH
UPDATE nodes SET depth = ?3
WHERE wc_id = ?1 AND local_relpath = ?2 AND op_depth = 0
  AND kind='dir'

-- STMT_UPDATE_NODE_BASE_PRESENCE
UPDATE nodes SET presence = ?3
WHERE wc_id = ?1 AND local_relpath = ?2 AND op_depth = 0

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

-- STMT_SELECT_PRISTINE_SIZE
SELECT size
FROM pristine
WHERE checksum = ?1 LIMIT 1

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

-- STMT_SELECT_CONFLICT_MARKER_FILES1
SELECT prop_reject
FROM actual_node
WHERE wc_id = ?1 AND local_relpath = ?2
  AND (prop_reject IS NOT NULL)

-- STMT_SELECT_CONFLICT_MARKER_FILES2
SELECT prop_reject, conflict_old, conflict_new, conflict_working
FROM actual_node
WHERE wc_id = ?1 AND parent_relpath = ?2
  AND ((prop_reject IS NOT NULL) OR (conflict_old IS NOT NULL)
       OR (conflict_new IS NOT NULL) OR (conflict_working IS NOT NULL))

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

-- STMT_UPDATE_CLEAR_TREE_CONFLICT
UPDATE actual_node SET tree_conflict_data = NULL
WHERE wc_id = ?1 AND local_relpath = ?2

-- STMT_INSERT_WC_LOCK
INSERT INTO wc_lock (wc_id, local_dir_relpath, locked_levels)
VALUES (?1, ?2, ?3)

-- STMT_SELECT_WC_LOCK
SELECT locked_levels FROM wc_lock
WHERE wc_id = ?1 AND local_dir_relpath = ?2

-- STMT_SELECT_ANCESTOR_WCLOCKS
SELECT local_dir_relpath, locked_levels FROM wc_lock
WHERE wc_id = ?1
  AND ((local_dir_relpath >= ?3 AND local_dir_relpath <= ?2)
       OR local_dir_relpath = '')

-- STMT_DELETE_WC_LOCK
DELETE FROM wc_lock
WHERE wc_id = ?1 AND local_dir_relpath = ?2

-- STMT_FIND_WC_LOCK
SELECT local_dir_relpath FROM wc_lock
WHERE wc_id = ?1
  AND IS_STRICT_DESCENDANT_OF(local_dir_relpath, ?2)

-- STMT_DELETE_WC_LOCK_ORPHAN
DELETE FROM wc_lock
WHERE wc_id = ?1 AND local_dir_relpath = ?2
AND NOT EXISTS (SELECT 1 FROM nodes
                 WHERE nodes.wc_id = ?1
                   AND nodes.local_relpath = wc_lock.local_dir_relpath)

-- STMT_DELETE_WC_LOCK_ORPHAN_RECURSIVE
DELETE FROM wc_lock
WHERE wc_id = ?1
  AND (local_dir_relpath = ?2
       OR IS_STRICT_DESCENDANT_OF(local_dir_relpath, ?2))
  AND NOT EXISTS (SELECT 1 FROM nodes
                   WHERE nodes.wc_id = ?1
                     AND nodes.local_relpath = wc_lock.local_dir_relpath)

-- STMT_APPLY_CHANGES_TO_BASE_NODE
/* translated_size and last_mod_time are not mentioned here because they will
   be tweaked after the working-file is installed. When we replace an existing
   BASE node (read: bump), preserve its file_external status. */
INSERT OR REPLACE INTO nodes (
  wc_id, local_relpath, op_depth, parent_relpath, repos_id, repos_path,
  revision, presence, depth, kind, changed_revision, changed_date,
  changed_author, checksum, properties, dav_cache, symlink_target,
  file_external )
VALUES (?1, ?2, 0,
        ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14, ?15, ?16,
        (SELECT file_external FROM nodes
          WHERE wc_id = ?1
            AND local_relpath = ?2
            AND op_depth = 0))

-- STMT_INSTALL_WORKING_NODE_FOR_DELETE
INSERT OR REPLACE INTO nodes (
    wc_id, local_relpath, op_depth,
    parent_relpath, presence, kind)
SELECT wc_id, local_relpath, ?3 /*op_depth*/,
       parent_relpath, ?4 /*presence*/, kind
FROM nodes
WHERE wc_id = ?1 AND local_relpath = ?2 AND op_depth = 0

/* If this query is updated, STMT_INSERT_DELETE_LIST should too. */
-- STMT_INSERT_DELETE_FROM_NODE_RECURSIVE
INSERT INTO nodes (
    wc_id, local_relpath, op_depth, parent_relpath, presence, kind)
SELECT wc_id, local_relpath, ?4 /*op_depth*/, parent_relpath, 'base-deleted',
       kind
FROM nodes
WHERE wc_id = ?1
  AND (local_relpath = ?2
       OR IS_STRICT_DESCENDANT_OF(local_relpath, ?2))
  AND op_depth = ?3
  AND presence NOT IN ('base-deleted', 'not-present', 'excluded', 'absent')

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

-- STMT_INSERT_DELETE_FROM_BASE
INSERT INTO nodes (
    wc_id, local_relpath, op_depth, parent_relpath, presence, kind)
SELECT wc_id, local_relpath, ?3 /*op_depth*/, parent_relpath,
    'base-deleted', kind
FROM nodes
WHERE wc_id = ?1 AND local_relpath = ?2 AND op_depth = 0

/* Not valid on the wc-root */
-- STMT_UPDATE_OP_DEPTH_INCREASE_RECURSIVE
UPDATE nodes SET op_depth = ?3 + 1
WHERE wc_id = ?1
 AND IS_STRICT_DESCENDANT_OF(local_relpath, ?2)
 AND op_depth = ?3

-- STMT_DOES_NODE_EXIST
SELECT 1 FROM nodes WHERE wc_id = ?1 AND local_relpath = ?2
LIMIT 1

-- STMT_HAS_SERVER_EXCLUDED_DESCENDANTS
SELECT local_relpath FROM nodes
WHERE wc_id = ?1
  AND IS_STRICT_DESCENDANT_OF(local_relpath, ?2)
  AND op_depth = 0 AND presence = 'absent'
LIMIT 1

/* Select all excluded nodes. Not valid on the WC-root */
-- STMT_SELECT_ALL_EXCLUDED_DESCENDANTS
SELECT local_relpath FROM nodes
WHERE wc_id = ?1
  AND IS_STRICT_DESCENDANT_OF(local_relpath, ?2)
  AND op_depth = 0
  AND (presence = 'absent' OR presence = 'excluded')

/* Creates a copy from one top level NODE to a different location */
-- STMT_INSERT_WORKING_NODE_COPY_FROM
INSERT OR REPLACE INTO nodes (
    wc_id, local_relpath, op_depth, parent_relpath, repos_id,
    repos_path, revision, presence, depth, moved_here, kind, changed_revision,
    changed_date, changed_author, checksum, properties, translated_size,
    last_mod_time, symlink_target, moved_to )
SELECT wc_id, ?3 /*local_relpath*/, ?4 /*op_depth*/, ?5 /*parent_relpath*/,
    repos_id, repos_path, revision, ?6 /*presence*/, depth,
    ?7/*moved_here*/, kind, changed_revision, changed_date,
    changed_author, checksum, properties, translated_size,
    last_mod_time, symlink_target,
    (SELECT dst.moved_to FROM nodes AS dst
                         WHERE dst.wc_id = ?1
                         AND dst.local_relpath = ?3
                         AND dst.op_depth = ?4)
FROM nodes_current
WHERE wc_id = ?1 AND local_relpath = ?2

-- STMT_INSERT_WORKING_NODE_COPY_FROM_DEPTH
INSERT OR REPLACE INTO nodes (
    wc_id, local_relpath, op_depth, parent_relpath, repos_id,
    repos_path, revision, presence, depth, moved_here, kind, changed_revision,
    changed_date, changed_author, checksum, properties, translated_size,
    last_mod_time, symlink_target, moved_to )
SELECT wc_id, ?3 /*local_relpath*/, ?4 /*op_depth*/, ?5 /*parent_relpath*/,
    repos_id, repos_path, revision, ?6 /*presence*/, depth,
    ?7 /*moved_here*/, kind, changed_revision, changed_date,
    changed_author, checksum, properties, translated_size,
    last_mod_time, symlink_target,
    (SELECT dst.moved_to FROM nodes AS dst
                         WHERE dst.wc_id = ?1
                         AND dst.local_relpath = ?3
                         AND dst.op_depth = ?4)
FROM nodes
WHERE wc_id = ?1 AND local_relpath = ?2 AND op_depth = ?8

-- STMT_UPDATE_BASE_REVISION
UPDATE nodes SET revision = ?3
WHERE wc_id = ?1 AND local_relpath = ?2 AND op_depth = 0

-- STMT_UPDATE_BASE_REPOS
UPDATE nodes SET repos_id = ?3, repos_path = ?4
WHERE wc_id = ?1 AND local_relpath = ?2 AND op_depth = 0

-- STMT_ACTUAL_HAS_CHILDREN
SELECT 1 FROM actual_node
WHERE wc_id = ?1 AND parent_relpath = ?2
LIMIT 1

-- STMT_INSERT_EXTERNAL
INSERT OR REPLACE INTO externals (
    wc_id, local_relpath, parent_relpath, presence, kind, def_local_relpath,
    repos_id, def_repos_relpath, def_operational_revision, def_revision)
VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10)

-- STMT_SELECT_EXTERNAL_INFO
SELECT presence, kind, def_local_relpath, repos_id,
    def_repos_relpath, def_operational_revision, def_revision
FROM externals WHERE wc_id = ?1 AND local_relpath = ?2
LIMIT 1

/* Select all committable externals, i.e. only unpegged ones on the same
 * repository as the target path ?2, that are defined by WC ?1 to
 * live below the target path. It does not matter which ancestor has the
 * svn:externals definition, only the local path at which the external is
 * supposed to be checked out is queried.
 * Arguments:
 *  ?1: wc_id.
 *  ?2: the target path, local relpath inside ?1.
 *  ?3: boolean, if 1 return immediate children of ?2 only.
 *
 * ### NOTE: This statement deliberately removes file externals that live
 * inside an unversioned dir, because commit still breaks on those.
 * Once that's been fixed, the conditions below "--->8---" become obsolete. */
-- STMT_SELECT_COMMITTABLE_EXTERNALS_BELOW
SELECT local_relpath, kind, def_repos_relpath,
       (SELECT root FROM repository AS r
         WHERE r.id = e.repos_id)
FROM externals AS e
WHERE e.wc_id = ?1
  AND IS_STRICT_DESCENDANT_OF(e.local_relpath, ?2)
  AND e.def_revision IS NULL
  AND e.repos_id = (SELECT repos_id
                    FROM nodes AS n
                    WHERE n.wc_id = ?1
                      AND n.local_relpath = ''
                      AND n.op_depth = 0)
  AND ( (NOT ?3) OR (parent_relpath = ?2) )
  /* ------>8----- */
  AND (EXISTS (SELECT 1 FROM nodes
               WHERE nodes.wc_id = e.wc_id
               AND nodes.local_relpath = e.parent_relpath))

-- STMT_SELECT_EXTERNALS_DEFINED
SELECT local_relpath, def_local_relpath
FROM externals
/* ### The Sqlite optimizer needs help here ###
 * WHERE wc_id = ?1
 *   AND (def_local_relpath = ?2
 *        OR IS_STRICT_DESCENDANT_OF(def_local_relpath, ?2)) */
WHERE (wc_id = ?1 AND def_local_relpath = ?2)
   OR (wc_id = ?1 AND IS_STRICT_DESCENDANT_OF(def_local_relpath, ?2))

-- STMT_DELETE_EXTERNAL
DELETE FROM externals
WHERE wc_id = ?1 AND local_relpath = ?2

-- STMT_SELECT_EXTERNAL_PROPERTIES
/* ### It would be nice if Sqlite would handle
 * SELECT IFNULL((SELECT properties FROM actual_node a
 *                WHERE a.wc_id = ?1 AND A.local_relpath = n.local_relpath),
 *               properties),
 *        local_relpath, depth
 * FROM nodes_current n
 * WHERE wc_id = ?1
 *   AND (local_relpath = ?2
 *        OR IS_STRICT_DESCENDANT_OF(local_relpath, ?2))
 *   AND kind = 'dir' AND presence IN ('normal', 'incomplete')
 * ### But it would take a double table scan execution plan for it.
 * ### Maybe there is something else going on? */
SELECT IFNULL((SELECT properties FROM actual_node a
               WHERE a.wc_id = ?1 AND A.local_relpath = n.local_relpath),
              properties),
       local_relpath, depth
FROM nodes_current n
WHERE wc_id = ?1 AND local_relpath = ?2
  AND kind = 'dir' AND presence IN ('normal', 'incomplete')
UNION ALL
SELECT IFNULL((SELECT properties FROM actual_node a
               WHERE a.wc_id = ?1 AND A.local_relpath = n.local_relpath),
              properties),
       local_relpath, depth
FROM nodes_current n
WHERE wc_id = ?1 AND IS_STRICT_DESCENDANT_OF(local_relpath, ?2)
  AND kind = 'dir' AND presence IN ('normal', 'incomplete')

-- STMT_SELECT_CURRENT_PROPS_RECURSIVE
/* ### Ugly OR to make sqlite use the proper optimizations */
SELECT IFNULL((SELECT properties FROM actual_node a
               WHERE a.wc_id = ?1 AND A.local_relpath = n.local_relpath),
              properties),
       local_relpath
FROM nodes_current n
WHERE (wc_id = ?1 AND local_relpath = ?2)
   OR (wc_id = ?1 AND IS_STRICT_DESCENDANT_OF(local_relpath, ?2))

/* ------------------------------------------------------------------------- */

/* these are used in entries.c  */

-- STMT_INSERT_ACTUAL_NODE
INSERT OR REPLACE INTO actual_node (
  wc_id, local_relpath, parent_relpath, properties, conflict_old,
  conflict_new,
  conflict_working, prop_reject, changelist, text_mod,
  tree_conflict_data)
VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, NULL, ?10)

/* ------------------------------------------------------------------------- */

/* these are used in upgrade.c  */

-- STMT_UPDATE_ACTUAL_CONFLICT_DATA
UPDATE actual_node SET conflict_data = ?3
WHERE wc_id = ?1 AND local_relpath = ?2

-- STMT_INSERT_ACTUAL_CONFLICT_DATA
INSERT INTO actual_node (
  wc_id, local_relpath, conflict_data, parent_relpath)
VALUES (?1, ?2, ?3, ?4)

-- STMT_SELECT_ALL_FILES
SELECT local_relpath FROM nodes_current
WHERE wc_id = ?1 AND parent_relpath = ?2 AND kind = 'file'

-- STMT_UPDATE_NODE_PROPS
UPDATE nodes SET properties = ?4
WHERE wc_id = ?1 AND local_relpath = ?2 AND op_depth = ?3

-- STMT_HAS_WORKING_NODES
SELECT 1 FROM nodes WHERE op_depth > 0
LIMIT 1

/* --------------------------------------------------------------------------
 * Complex queries for callback walks, caching results in a temporary table.
 *
 * These target table are then used for joins against NODES, or for reporting
 */

-- STMT_CREATE_TARGET_PROP_CACHE
DROP TABLE IF EXISTS target_prop_cache;
CREATE TEMPORARY TABLE target_prop_cache (
  local_relpath TEXT NOT NULL PRIMARY KEY,
  kind TEXT NOT NULL,
  properties BLOB
);
/* ###  Need index?
CREATE UNIQUE INDEX temp__node_props_cache_unique
  ON temp__node_props_cache (local_relpath) */

-- STMT_CACHE_TARGET_PROPS
INSERT INTO target_prop_cache(local_relpath, kind, properties)
 SELECT n.local_relpath, n.kind,
        IFNULL((SELECT properties FROM actual_node AS a
                 WHERE a.wc_id = n.wc_id
                   AND a.local_relpath = n.local_relpath),
               n.properties)
   FROM targets_list AS t
   JOIN nodes_current AS n ON t.wc_id= n.wc_id
                          AND t.local_relpath = n.local_relpath
  WHERE t.wc_id = ?1
    AND (presence='normal' OR presence='incomplete')

-- STMT_CACHE_TARGET_PRISTINE_PROPS
INSERT INTO target_prop_cache(local_relpath, kind, properties)
 SELECT n.local_relpath, n.kind,
        CASE n.presence
          WHEN 'base-deleted'
          THEN (SELECT properties FROM nodes AS p
                 WHERE p.wc_id = n.wc_id
                   AND p.local_relpath = n.local_relpath
                   AND p.op_depth < n.op_depth
                 ORDER BY p.op_depth DESC /* LIMIT 1 */)
          ELSE properties END
  FROM targets_list AS t
  JOIN nodes_current AS n ON t.wc_id= n.wc_id
                          AND t.local_relpath = n.local_relpath
  WHERE t.wc_id = ?1
    AND (presence = 'normal'
         OR presence = 'incomplete'
         OR presence = 'base-deleted')

-- STMT_SELECT_ALL_TARGET_PROP_CACHE
SELECT local_relpath, properties FROM target_prop_cache
ORDER BY local_relpath

-- STMT_DROP_TARGET_PROP_CACHE
DROP TABLE IF EXISTS target_prop_cache;


-- STMT_CREATE_REVERT_LIST
DROP TABLE IF EXISTS revert_list;
CREATE TEMPORARY TABLE revert_list (
   /* need wc_id if/when revert spans multiple working copies */
   local_relpath TEXT NOT NULL,
   actual INTEGER NOT NULL,         /* 1 if an actual row, 0 if a nodes row */
   conflict_old TEXT,
   conflict_new TEXT,
   conflict_working TEXT,
   prop_reject TEXT,
   notify INTEGER,         /* 1 if an actual row had props or tree conflict */
   op_depth INTEGER,
   repos_id INTEGER,
   kind TEXT,
   PRIMARY KEY (local_relpath, actual)
   );
DROP TRIGGER IF EXISTS   trigger_revert_list_nodes;
CREATE TEMPORARY TRIGGER trigger_revert_list_nodes
BEFORE DELETE ON nodes
BEGIN
   INSERT OR REPLACE INTO revert_list(local_relpath, actual, op_depth,
                                      repos_id, kind)
   SELECT OLD.local_relpath, 0, OLD.op_depth, OLD.repos_id, OLD.kind;
END;
DROP TRIGGER IF EXISTS   trigger_revert_list_actual_delete;
CREATE TEMPORARY TRIGGER trigger_revert_list_actual_delete
BEFORE DELETE ON actual_node
BEGIN
   INSERT OR REPLACE INTO revert_list(local_relpath, actual, conflict_old,
                                       conflict_new, conflict_working,
                                       prop_reject, notify)
   SELECT OLD.local_relpath, 1,
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
   INSERT OR REPLACE INTO revert_list(local_relpath, actual, conflict_old,
                                       conflict_new, conflict_working,
                                       prop_reject, notify)
   SELECT OLD.local_relpath, 1,
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
SELECT conflict_old, conflict_new, conflict_working, prop_reject, notify,
       actual, op_depth, repos_id, kind
FROM revert_list
WHERE local_relpath = ?1
ORDER BY actual DESC

-- STMT_SELECT_REVERT_LIST_COPIED_CHILDREN
SELECT local_relpath, kind
FROM revert_list
WHERE IS_STRICT_DESCENDANT_OF(local_relpath, ?1)
  AND op_depth >= ?2
  AND repos_id IS NOT NULL
ORDER BY local_relpath

-- STMT_DELETE_REVERT_LIST
DELETE FROM revert_list WHERE local_relpath = ?1

-- STMT_SELECT_REVERT_LIST_RECURSIVE
SELECT DISTINCT local_relpath
FROM revert_list
WHERE (local_relpath = ?1
       OR IS_STRICT_DESCENDANT_OF(local_relpath, ?1))
  AND (notify OR actual = 0)
ORDER BY local_relpath

-- STMT_DELETE_REVERT_LIST_RECURSIVE
DELETE FROM revert_list
WHERE (local_relpath = ?1
       OR IS_STRICT_DESCENDANT_OF(local_relpath, ?1))

-- STMT_DROP_REVERT_LIST
DROP TABLE IF EXISTS revert_list

-- STMT_CREATE_DELETE_LIST
DROP TABLE IF EXISTS delete_list;
CREATE TEMPORARY TABLE delete_list (
/* ### we should put the wc_id in here in case a delete spans multiple
   ### working copies. queries, etc will need to be adjusted.  */
   local_relpath TEXT PRIMARY KEY NOT NULL UNIQUE
   )

/* This matches the selection in STMT_INSERT_DELETE_FROM_NODE_RECURSIVE.
   A subquery is used instead of nodes_current to avoid a table scan */
-- STMT_INSERT_DELETE_LIST
INSERT INTO delete_list(local_relpath)
SELECT local_relpath FROM nodes AS n
WHERE wc_id = ?1
  AND (local_relpath = ?2
       OR IS_STRICT_DESCENDANT_OF(local_relpath, ?2))
  AND op_depth >= ?3
  AND op_depth = (SELECT MAX(s.op_depth) FROM nodes AS s
                  WHERE s.wc_id = ?1
                    AND s.local_relpath = n.local_relpath)
  AND presence NOT IN ('base-deleted', 'not-present', 'excluded', 'absent')

-- STMT_SELECT_DELETE_LIST
SELECT local_relpath FROM delete_list
ORDER BY local_relpath

-- STMT_FINALIZE_DELETE
DROP TABLE IF EXISTS delete_list


/* ------------------------------------------------------------------------- */

/* Queries for revision status. */

-- STMT_SELECT_MIN_MAX_REVISIONS
SELECT MIN(revision), MAX(revision),
       MIN(changed_revision), MAX(changed_revision) FROM nodes
  WHERE wc_id = ?1
    AND (local_relpath = ?2
         OR IS_STRICT_DESCENDANT_OF(local_relpath, ?2))
    AND presence IN ('normal', 'incomplete')
    AND file_external IS NULL
    AND op_depth = 0

-- STMT_HAS_SPARSE_NODES
SELECT 1 FROM nodes
WHERE wc_id = ?1
  AND (local_relpath = ?2
       OR IS_STRICT_DESCENDANT_OF(local_relpath, ?2))
  AND op_depth = 0
  AND (presence IN ('absent', 'excluded')
        OR depth NOT IN ('infinity', 'unknown'))
  AND file_external IS NULL
LIMIT 1

-- STMT_SUBTREE_HAS_TREE_MODIFICATIONS
SELECT 1 FROM nodes
WHERE wc_id = ?1
  AND (local_relpath = ?2
       OR IS_STRICT_DESCENDANT_OF(local_relpath, ?2))
  AND op_depth > 0
LIMIT 1

-- STMT_SUBTREE_HAS_PROP_MODIFICATIONS
SELECT 1 FROM actual_node
WHERE wc_id = ?1
  AND (local_relpath = ?2
       OR IS_STRICT_DESCENDANT_OF(local_relpath, ?2))
  AND properties IS NOT NULL
LIMIT 1

/* Determine if there is some switched subtree in just SQL. This looks easy,
   but it really isn't, because we don't have a simple (and optimizable)
   path join operation in SQL.

   To work around that we have 4 different cases:
      * Check on a node that is neither wcroot nor repos root
      * Check on a node that is repos_root, but not wcroot.
      * Check on a node that is wcroot, but not repos root.
      * Check on a node that is both wcroot and repos root.

   To make things easier, our testsuite is usually in that last category,
   while normal working copies are almost always in one of the others.
*/
-- STMT_HAS_SWITCHED
SELECT o.repos_path || '/' || SUBSTR(s.local_relpath, LENGTH(?2)+2) AS expected
       /*,s.local_relpath, s.repos_path, o.local_relpath, o.repos_path*/
FROM nodes AS o
LEFT JOIN nodes AS s
ON o.wc_id = s.wc_id
   AND IS_STRICT_DESCENDANT_OF(s.local_relpath, ?2)
   AND s.op_depth = 0
   AND s.repos_id = o.repos_id
   AND s.file_external IS NULL
WHERE o.wc_id = ?1 AND o.local_relpath=?2 AND o.op_depth=0
  AND s.repos_path != expected
LIMIT 1

-- STMT_HAS_SWITCHED_REPOS_ROOT
SELECT SUBSTR(s.local_relpath, LENGTH(?2)+2) AS expected
       /*,s.local_relpath, s.repos_path, o.local_relpath, o.repos_path*/
FROM nodes AS o
LEFT JOIN nodes AS s
ON o.wc_id = s.wc_id
   AND IS_STRICT_DESCENDANT_OF(s.local_relpath, ?2)
   AND s.op_depth = 0
   AND s.repos_id = o.repos_id
   AND s.file_external IS NULL
WHERE o.wc_id = ?1 AND o.local_relpath=?2 AND o.op_depth=0
  AND s.repos_path != expected
LIMIT 1

-- STMT_HAS_SWITCHED_WCROOT
SELECT o.repos_path || '/' || s.local_relpath AS expected
       /*,s.local_relpath, s.repos_path, o.local_relpath, o.repos_path*/
FROM nodes AS o
LEFT JOIN nodes AS s
ON o.wc_id = s.wc_id
   AND s.local_relpath != ''
   AND s.op_depth = 0
   AND s.repos_id = o.repos_id
   AND s.file_external IS NULL
WHERE o.wc_id = ?1 AND o.local_relpath=?2 AND o.op_depth=0
  AND s.repos_path != expected
LIMIT 1

-- STMT_HAS_SWITCHED_WCROOT_REPOS_ROOT
SELECT s.local_relpath AS expected
       /*,s.local_relpath, s.repos_path, o.local_relpath, o.repos_path*/
FROM nodes AS o
LEFT JOIN nodes AS s
ON o.wc_id = s.wc_id
   AND s.local_relpath != ''
   AND s.op_depth = 0
   AND s.repos_id = o.repos_id
   AND s.file_external IS NULL
WHERE o.wc_id = ?1 AND o.local_relpath=?2 AND o.op_depth=0
  AND s.repos_path != expected
LIMIT 1

-- STMT_SELECT_BASE_FILES_RECURSIVE
SELECT local_relpath, translated_size, last_mod_time FROM nodes AS n
WHERE wc_id = ?1
  AND (local_relpath = ?2
       OR IS_STRICT_DESCENDANT_OF(local_relpath, ?2))
  AND op_depth = 0
  AND kind='file'
  AND presence='normal'
  AND file_external IS NULL

/* ### FIXME: op-depth?  What about multiple moves? */
-- STMT_SELECT_MOVED_FROM_RELPATH
SELECT local_relpath, op_depth FROM nodes
WHERE wc_id = ?1 AND moved_to = ?2 AND op_depth > 0

-- STMT_UPDATE_MOVED_TO_RELPATH
UPDATE nodes SET moved_to = ?4
WHERE wc_id = ?1 AND local_relpath = ?2 AND op_depth = ?3

/* ### FIXME: op-depth?  What about multiple moves? */
-- STMT_CLEAR_MOVED_TO_RELPATH
UPDATE nodes SET moved_to = NULL
WHERE wc_id = ?1 AND local_relpath = ?2

/* This statement returns pairs of move-roots below the path ?2 in WC_ID ?1.
 * Each row returns a moved-here path (always a child of ?2) in the first
 * column, and its matching moved-away (deleted) path in the second column. */
-- STMT_SELECT_MOVED_HERE_CHILDREN
SELECT moved_to, local_relpath FROM nodes
WHERE wc_id = ?1 AND op_depth > 0
  AND IS_STRICT_DESCENDANT_OF(moved_to, ?2)

/* This statement returns pairs of paths that define a move where the
   destination of the move is within the subtree rooted at path ?2 in
   WC_ID ?1. */
-- STMT_SELECT_MOVED_PAIR
SELECT local_relpath, moved_to, op_depth FROM nodes_current
WHERE wc_id = ?1
  AND (IS_STRICT_DESCENDANT_OF(moved_to, ?2)
       OR (IS_STRICT_DESCENDANT_OF(local_relpath, ?2)
           AND moved_to IS NOT NULL))

/* This statement returns pairs of move-roots below the path ?2 in WC_ID ?1,
 * where the source of the move is within the subtree rooted at path ?2, and
 * the destination of the move is outside the subtree rooted at path ?2. */
-- STMT_SELECT_MOVED_PAIR2
SELECT local_relpath, moved_to FROM nodes_current
WHERE wc_id = ?1
  AND IS_STRICT_DESCENDANT_OF(local_relpath, ?2)
  AND moved_to IS NOT NULL
  AND NOT IS_STRICT_DESCENDANT_OF(moved_to, ?2)

/* ------------------------------------------------------------------------- */

/* Queries for verification. */

-- STMT_SELECT_ALL_NODES
SELECT op_depth, local_relpath, parent_relpath, file_external FROM nodes
WHERE wc_id == ?1

/* ------------------------------------------------------------------------- */

/* Grab all the statements related to the schema.  */

-- include: wc-metadata
-- include: wc-checks
