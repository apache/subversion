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
select repos_id, repos_relpath, presence, kind, revnum, checksum,
  translated_size, changed_rev, changed_date, changed_author, depth,
  symlink_target, last_mod_time, properties
from base_node
where wc_id = ?1 and local_relpath = ?2;

-- STMT_SELECT_BASE_NODE_1
select repos_id, repos_path, presence, kind, revision, checksum,
  translated_size, changed_revision, changed_date, changed_author, depth,
  symlink_target, last_mod_time, properties
from nodes
where wc_id = ?1 and local_relpath = ?2 and op_depth = 0;

-- STMT_SELECT_BASE_NODE_WITH_LOCK
select base_node.repos_id, base_node.repos_relpath, presence, kind,
  revnum, checksum, translated_size, changed_rev, changed_date,
  changed_author, depth, symlink_target, last_mod_time, properties,
  lock_token, lock_owner, lock_comment, lock_date
from base_node
left outer join lock on base_node.repos_id = lock.repos_id
  and base_node.repos_relpath = lock.repos_relpath
where wc_id = ?1 and local_relpath = ?2;

-- STMT_SELECT_WORKING_NODE
select presence, kind, checksum, translated_size,
  changed_rev, changed_date, changed_author, depth, symlink_target,
  copyfrom_repos_id, copyfrom_repos_path, copyfrom_revnum,
  moved_here, moved_to, last_mod_time, properties
from working_node
where wc_id = ?1 and local_relpath = ?2;

-- STMT_SELECT_ACTUAL_NODE
select prop_reject, changelist, conflict_old, conflict_new,
conflict_working, tree_conflict_data, properties
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
  depth, checksum, translated_size, symlink_target, dav_cache)
values (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14,
        ?15, ?16, ?17);

-- STMT_INSERT_NODE
insert or replace into nodes (
  wc_id, local_relpath, op_depth, parent_relpath, repos_id, repos_path,
  revision, presence, depth, kind, changed_revision, changed_date,
  changed_author, checksum, properties, translated_size, last_mod_time,
  dav_cache, symlink_target )
values (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14,
        ?15, ?16, ?17, ?18, ?19);

-- STMT_INSERT_BASE_NODE_INCOMPLETE
insert or ignore into base_node (
  wc_id, local_relpath, parent_relpath, presence, kind, revnum)
values (?1, ?2, ?3, 'incomplete', 'unknown', ?4);

-- STMT_INSERT_BASE_NODE_INCOMPLETE_DIR
insert or ignore into base_node (
  wc_id, local_relpath, repos_id, repos_relpath, parent_relpath, presence,
  kind, revnum, depth)
values (?1, ?2, ?3, ?4, ?5, 'incomplete', 'dir', ?6, ?7);

-- STMT_INSERT_WORKING_NODE_INCOMPLETE
INSERT OR IGNORE INTO WORKING_NODE (
  wc_id, local_relpath, parent_relpath, presence, kind)
VALUES (?1, ?2, ?3, 'incomplete', 'unknown');

-- STMT_COUNT_BASE_NODE_CHILDREN
SELECT COUNT(*) FROM BASE_NODE
WHERE wc_id = ?1 AND parent_relpath = ?2;

-- STMT_COUNT_WORKING_NODE_CHILDREN
SELECT COUNT(*) FROM WORKING_NODE
WHERE wc_id = ?1 AND parent_relpath = ?2;

-- STMT_SELECT_BASE_NODE_CHILDREN
select local_relpath from base_node
where wc_id = ?1 and parent_relpath = ?2;

-- STMT_SELECT_WORKING_NODE_CHILDREN
SELECT local_relpath FROM WORKING_NODE
WHERE wc_id = ?1 AND parent_relpath = ?2;

-- STMT_SELECT_WORKING_IS_FILE
select kind == 'file' from working_node
where wc_id = ?1 and local_relpath = ?2;

-- STMT_SELECT_BASE_IS_FILE
select kind == 'file' from base_node
where wc_id = ?1 and local_relpath = ?2;

-- STMT_SELECT_BASE_PROPS
select properties from base_node
where wc_id = ?1 and local_relpath = ?2;

-- STMT_SELECT_WORKING_PROPS
SELECT properties, presence FROM WORKING_NODE
WHERE wc_id = ?1 AND local_relpath = ?2;

-- STMT_SELECT_ACTUAL_PROPS
select properties from actual_node
where wc_id = ?1 and local_relpath = ?2;

-- STMT_UPDATE_BASE_PROPS
update base_node set properties = ?3
where wc_id = ?1 and local_relpath = ?2;

-- STMT_UPDATE_NODE_BASE_PROPS
update nodes set properties = ?3
where wc_id = ?1 and local_relpath = ?2 and op_depth = 0;

-- STMT_UPDATE_WORKING_PROPS
update working_node set properties = ?3
where wc_id = ?1 and local_relpath = ?2;

-- STMT_UPDATE_NODE_WORKING_PROPS
update nodes set properties = ?3
where wc_id = ?1 and local_relpath = ?2
  and op_depth in
   (select op_depth from nodes
    where wc_id = ?1 and local_relpath = ?2
    order by op_depth desc
    limit 1);

-- STMT_UPDATE_ACTUAL_PROPS
update actual_node set properties = ?3
where wc_id = ?1 and local_relpath = ?2;

-- STMT_INSERT_ACTUAL_PROPS
insert into actual_node (wc_id, local_relpath, parent_relpath, properties)
values (?1, ?2, ?3, ?4);

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

-- STMT_UPDATE_BASE_NODE_DAV_CACHE
update nodes set dav_cache = ?3
where wc_id = ?1 and local_relpath = ?2 and op_depth = 0;

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

-- STMT_CLEAR_BASE_RECURSIVE_DAV_CACHE
update base_node set dav_cache = null
where dav_cache is not null and wc_id = ?1 and
  (local_relpath = ?2 or
   local_relpath like ?3 escape '#');

-- STMT_CLEAR_BASE_NODE_RECURSIVE_DAV_CACHE
update nodes set dav_cache = null
where dav_cache is not null and wc_id = ?1 and op_depth = 0 and
  (local_relpath = ?2 or
   local_relpath like ?3 escape '#');

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

-- STMT_RECURSIVE_UPDATE_NODE_REPO
update NODES set repos_id = ?5, dav_cache = null
where wc_id = ?1 and repos_id = ?4 and
  (local_relpath = ?2
   or local_relpath like ?3 escape '#');

-- STMT_UPDATE_LOCK_REPOS_ID
update lock set repos_id = ?4
where repos_id = ?1 and
  (repos_relpath = ?2 or
   repos_relpath like ?3 escape '#');

-- STMT_UPDATE_BASE_FILEINFO
UPDATE BASE_NODE SET translated_size = ?3, last_mod_time = ?4
WHERE wc_id = ?1 AND local_relpath = ?2;

-- STMT_UPDATE_BASE_NODE_FILEINFO
update nodes set translated_size = ?3, last_mod_time = ?4
where wc_id = ?1 and local_relpath = ?2 and op_depth = 0;

-- STMT_UPDATE_WORKING_FILEINFO
UPDATE WORKING_NODE SET translated_size = ?3, last_mod_time = ?4
WHERE wc_id = ?1 AND local_relpath = ?2;

-- STMT_UPDATE_WORKING_NODE_FILEINFO
update nodes set translated_size = ?3, last_mod_time = ?4
where wc_id = ?1 and local_relpath = ?2
  and op_depth = (select op_depth from nodes
                  where wc_id = ?1 and local_relpath = ?2
                  order by op_depth desc
                  limit 1);

-- STMT_UPDATE_ACTUAL_TREE_CONFLICTS
update actual_node set tree_conflict_data = ?3
where wc_id = ?1 and local_relpath = ?2;

-- STMT_INSERT_ACTUAL_TREE_CONFLICTS
/* tree conflicts are always recorded on the wcroot node, so the
   parent_relpath will be null.  */
insert into actual_node (
  wc_id, local_relpath, tree_conflict_data)
values (?1, ?2, ?3);

-- STMT_UPDATE_ACTUAL_TEXT_CONFLICTS
update actual_node set conflict_old = ?3, conflict_new = ?4,
conflict_working = ?5
where wc_id = ?1 and local_relpath = ?2;

-- STMT_INSERT_ACTUAL_TEXT_CONFLICTS
insert into actual_node (
  wc_id, local_relpath, conflict_old, conflict_new, conflict_working,
  parent_relpath)
values (?1, ?2, ?3, ?4, ?5, ?6);

-- STMT_UPDATE_ACTUAL_PROPERTY_CONFLICTS
update actual_node set prop_reject = ?3
where wc_id = ?1 and local_relpath = ?2;

-- STMT_INSERT_ACTUAL_PROPERTY_CONFLICTS
insert into actual_node (
  wc_id, local_relpath, prop_reject, parent_relpath)
values (?1, ?2, ?3, ?4);

-- STMT_UPDATE_ACTUAL_CHANGELIST
update actual_node set changelist = ?3
where wc_id = ?1 and local_relpath = ?2;

-- STMT_INSERT_ACTUAL_CHANGELIST
insert into actual_node (
  wc_id, local_relpath, changelist, parent_relpath)
values (?1, ?2, ?3, ?4);

-- STMT_RESET_ACTUAL_WITH_CHANGELIST
REPLACE INTO actual_node (
  wc_id, local_relpath, parent_relpath, changelist)
VALUES (?1, ?2, ?3, ?4);

-- STMT_DELETE_BASE_NODE
delete from base_node
where wc_id = ?1 and local_relpath = ?2;

-- STMT_DELETE_BASE_NODE_1
delete from nodes
where wc_id = ?1 and local_relpath = ?2 and op_depth = 0;

-- STMT_DELETE_WORKING_NODE
delete from working_node
where wc_id = ?1 and local_relpath = ?2;

-- STMT_DELETE_WORKING_NODES
delete from nodes
where wc_id = ?1 and local_relpath = ?2 and op_depth > 0;

-- STMT_DELETE_NODES
delete from nodes
where wc_id = ?1 and local_relpath = ?2;

-- STMT_DELETE_ACTUAL_NODE
delete from actual_node
where wc_id = ?1 and local_relpath = ?2;

-- STMT_UPDATE_BASE_DEPTH
UPDATE BASE_NODE SET depth = ?3
WHERE wc_id = ?1 AND local_relpath = ?2;

-- STMT_UPDATE_NODE_BASE_DEPTH
update NODES set depth = ?3
where wc_id = ?1 and local_relpath = ?2 and op_depth = 0;

-- STMT_UPDATE_WORKING_DEPTH
UPDATE WORKING_NODE SET depth = ?3
WHERE wc_id = ?1 AND local_relpath = ?2;

-- STMT_UPDATE_NODE_WORKING_DEPTH
update NODES set depth = ?3
where wc_id = ?1 and local_relpath = ?2 and
      op_depth in (select op_depth from NODES
                   where wc_id = ?1 and local_relpath = ?2
                   order by op_depth desc
                   limit 1);

-- STMT_UPDATE_BASE_EXCLUDED
UPDATE BASE_NODE SET presence = 'excluded', depth = NULL
WHERE wc_id = ?1 AND local_relpath = ?2;

-- STMT_UPDATE_NODE_BASE_EXCLUDED
update NODES set presence = 'excluded', depth = NULL
where wc_id = ?1 and local_relpath = ?2 and op_depth = 0;

-- STMT_UPDATE_WORKING_EXCLUDED
UPDATE WORKING_NODE SET presence = 'excluded', depth = NULL
WHERE wc_id = ?1 AND local_relpath = ?2;

-- STMT_UPDATE_NODE_WORKING_EXCLUDED
update nodes SET presence = 'excluded', depth = NULL
where wc_id = ?1 and local_relpath = ?2 and
      op_depth IN (select op_depth from NODES
                   where wc_id = ?1 and local_relpath = ?2
                   order by op_depth DECSC
                   limit 1);

-- STMT_UPDATE_BASE_PRESENCE
update base_node set presence= ?3
where wc_id = ?1 and local_relpath = ?2;

-- STMT_UPDATE_NODE_BASE_PRESENCE
update nodes set presence = ?3
where wc_id = ?1 and local_relpath = ?2 and op_depth = 0;

-- STMT_UPDATE_BASE_PRESENCE_KIND
update base_node set presence = ?3, kind = ?4
where wc_id = ?1 and local_relpath = ?2;

-- STMT_UPDATE_NODE_BASE_PRESENCE_KIND
update nodes set presence = ?3, kind = ?4
where wc_id = ?1 and local_relpath = ?2 and op_depth = 0;

-- STMT_UPDATE_WORKING_PRESENCE
update working_node set presence = ?3
where wc_id = ?1 and local_relpath =?2;

-- STMT_UPDATE_NODE_WORKING_PRESENCE
update nodes set presence = ?3
where wc_id = ?1 and local_relpath = ?2
  and op_depth in (select op_depth from nodes
                   where wc_id = ?1 and local_relpath = ?2
                   order by op_depth desc
                   limit 1);

-- STMT_UPDATE_BASE_PRESENCE_AND_REVNUM
update base_node set presence = ?3, revnum = ?4
where wc_id = ?1 and local_relpath = ?2;

-- STMT_UPDATE_BASE_NODE_PRESENCE_AND_REVNUM
update nodes set presence = ?3, revision = ?4
where wc_id = ?1 and local_relpath = ?2 and op_depth = 0;

-- STMT_UPDATE_BASE_PRESENCE_REVNUM_AND_REPOS_RELPATH
update base_node set presence = ?3, revnum = ?4, repos_relpath = ?5
where wc_id = ?1 and local_relpath = ?2;

-- STMT_UPDATE_BASE_NODE_PRESENCE_REVNUM_AND_REPOS_PATH
update nodes set presence = ?3, revision = ?4, repos_path = ?5
where wc_id = ?1 and local_relpath = ?2 and op_depth = 0;

-- STMT_LOOK_FOR_WORK
SELECT id FROM WORK_QUEUE LIMIT 1;

-- STMT_INSERT_WORK_ITEM
INSERT INTO WORK_QUEUE (work) values (?1);

-- STMT_SELECT_WORK_ITEM
SELECT id, work FROM WORK_QUEUE ORDER BY id LIMIT 1;

-- STMT_DELETE_WORK_ITEM
DELETE FROM WORK_QUEUE WHERE id = ?1;

-- STMT_INSERT_PRISTINE
INSERT OR IGNORE INTO PRISTINE (checksum, md5_checksum, size, refcount)
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

-- STMT_DELETE_PRISTINE
DELETE FROM PRISTINE
WHERE checksum = ?1

-- STMT_SELECT_ACTUAL_CONFLICT_VICTIMS
SELECT local_relpath
FROM actual_node
WHERE wc_id = ?1 AND parent_relpath = ?2 AND
NOT((prop_reject IS NULL) AND (conflict_old IS NULL)
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
UPDATE ACTUAL_NODE SET
  conflict_old = null,
  conflict_new = null,
  conflict_working = null
WHERE wc_id = ?1 AND local_relpath = ?2;

-- STMT_CLEAR_PROPS_CONFLICT
UPDATE ACTUAL_NODE SET
  prop_reject = null
WHERE wc_id = ?1 AND local_relpath = ?2;

-- STMT_INSERT_WC_LOCK
INSERT INTO WC_LOCK (wc_id, local_dir_relpath, locked_levels)
VALUES (?1, ?2, ?3);

-- STMT_SELECT_WC_LOCK
SELECT locked_levels FROM WC_LOCK
WHERE wc_id = ?1 AND local_dir_relpath = ?2;

-- STMT_DELETE_WC_LOCK
DELETE FROM WC_LOCK
WHERE wc_id = ?1 AND local_dir_relpath = ?2;

-- STMT_FIND_WC_LOCK
SELECT local_dir_relpath FROM WC_LOCK
WHERE wc_id = ?1 AND local_dir_relpath LIKE ?2 ESCAPE '#';

-- STMT_APPLY_CHANGES_TO_BASE
/* translated_size and last_mod_time are not mentioned here because they will
   be tweaked after the working-file is installed.
   ### what to do about file_external?  */
INSERT OR REPLACE INTO BASE_NODE (
  wc_id, local_relpath, parent_relpath, presence, kind, revnum, changed_rev,
  changed_author, properties, repos_id, repos_relpath, checksum, changed_date,
  depth, symlink_target, dav_cache)
VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14, ?15, ?16);

-- STMT_APPLY_CHANGES_TO_BASE_NODE
insert or replace into NODES (
  wc_id, local_relpath, op_depth, parent_relpath, repos_id, repos_path,
  revision, presence, depth, kind, changed_revision, changed_date,
  changed_author, checksum, properties, dav_cache, symlink_target )
values (?1, ?2, 0,
        ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14, ?15, ?16);

-- STMT_INSERT_WORKING_NODE_FROM_BASE_NODE
INSERT INTO WORKING_NODE (
    wc_id, local_relpath, parent_relpath, presence, kind, checksum,
    translated_size, changed_rev, changed_date, changed_author, depth,
    symlink_target, last_mod_time )
SELECT wc_id, local_relpath, parent_relpath, ?3 AS presence, kind, checksum,
    translated_size, changed_rev, changed_date, changed_author, depth,
    symlink_target, last_mod_time FROM BASE_NODE
WHERE wc_id = ?1 AND local_relpath = ?2;

-- STMT_INSERT_WORKING_NODE_FROM_BASE
INSERT INTO NODES (
    wc_id, local_relpath, op_depth, parent_relpath, presence, kind, checksum,
    changed_revision, changed_date, changed_author, depth, symlink_target,
    translated_size, last_mod_time, properties)
SELECT wc_id, local_relpath, ?3 as op_depth, parent_relpath, ?4 as presence,
       kind, checksum, changed_revision, changed_date, changed_author, depth,
       symlink_target, translated_size, last_mod_time, properties
FROM NODES
WHERE wc_id = ?1 AND local_relpath = ?2 AND op_depth = 0;

-- STMT_INSERT_WORKING_NODE_NORMAL_FROM_BASE_NODE
INSERT INTO WORKING_NODE (
    wc_id, local_relpath, parent_relpath, presence, kind, checksum,
    translated_size, changed_rev, changed_date, changed_author, depth,
    symlink_target, last_mod_time, properties, copyfrom_repos_id,
    copyfrom_repos_path, copyfrom_revnum )
SELECT wc_id, local_relpath, parent_relpath, 'normal', kind, checksum,
    translated_size, changed_rev, changed_date, changed_author, depth,
    symlink_target, last_mod_time, properties, repos_id,
    repos_relpath, revnum FROM BASE_NODE
WHERE wc_id = ?1 AND local_relpath = ?2;

-- STMT_INSERT_WORKING_NODE_NORMAL_FROM_BASE
insert into NODES (
    wc_id, local_relpath, op_depth, parent_relpath, repos_id, repos_path,
    revision, presence, depth, kind, changed_revision, changed_date,
    changed_author, checksum, properties, translated_size, last_mod_time,
    symlink_target )
select wc_id, local_relpath, ?3 as op_depth, parent_relpath, repos_id,
    repos_path, revision, 'normal', depth, kind, changed_revision,
    changed_date, changed_author, checksum, properties, translated_size,
    last_mod_time, symlink_target
from NODES
where wc_id = ?1 and local_relpath = ?2 and op_depth = 0;


-- STMT_INSERT_WORKING_NODE_NOT_PRESENT_FROM_BASE_NODE
INSERT INTO WORKING_NODE (
    wc_id, local_relpath, parent_relpath, presence, kind, changed_rev,
    changed_date, changed_author, copyfrom_repos_id,
    copyfrom_repos_path, copyfrom_revnum )
SELECT wc_id, local_relpath, parent_relpath, 'not-present', kind, changed_rev,
    changed_date, changed_author, repos_id,
    repos_relpath, revnum FROM BASE_NODE
WHERE wc_id = ?1 AND local_relpath = ?2;

-- STMT_INSERT_WORKING_NODE_NOT_PRESENT_FROM_BASE
insert into NODES (
    wc_id, local_relpath, op_depth, parent_relpath, repos_id, repos_path,
    revision, presence, kind, changed_revision, changed_date, changed_author )
select wc_id, local_relpath, ?3 as op_depth, parent_relpath, repos_id,
       repos_path, revision, 'not-present', kind, changed_revision,
       changed_date, changed_author
from NODES
where wc_id = ?1 and local_relpath = ?2 and op_depth = 0;


-- ### the statement below should be setting copyfrom_revision!
-- STMT_UPDATE_COPYFROM
UPDATE WORKING_NODE set copyfrom_repos_id = ?3, copyfrom_repos_path = ?4
WHERE wc_id = ?1 AND local_relpath = ?2;

-- STMT_UPDATE_COPYFROM_TO_INHERIT
UPDATE WORKING_NODE SET
  copyfrom_repos_id = null,
  copyfrom_repos_path = null,
  copyfrom_revnum = null
WHERE wc_id = ?1 AND local_relpath = ?2;

-- STMT_DETERMINE_TREE_FOR_RECORDING
SELECT 0 FROM BASE_NODE WHERE wc_id = ?1 AND local_relpath = ?2
UNION
SELECT 1 FROM WORKING_NODE WHERE wc_id = ?1 AND local_relpath = ?2;


/* ### Why can't this query not just use the BASE repository
   location values, instead of taking 3 additional parameters?! */
-- STMT_INSERT_WORKING_NODE_COPY_FROM_BASE
INSERT OR REPLACE INTO WORKING_NODE (
    wc_id, local_relpath, parent_relpath, presence, kind, checksum,
    translated_size, changed_rev, changed_date, changed_author, depth,
    symlink_target, last_mod_time, properties, copyfrom_repos_id,
    copyfrom_repos_path, copyfrom_revnum )
SELECT wc_id, ?3 AS local_relpath, ?4 AS parent_relpath, ?5 AS presence, kind,
    checksum, translated_size, changed_rev, changed_date, changed_author, depth,
    symlink_target, last_mod_time, properties, ?6 AS copyfrom_repos_id,
    ?7 AS copyfrom_repos_path, ?8 AS copyfrom_revnum FROM BASE_NODE
WHERE wc_id = ?1 AND local_relpath = ?2;

-- STMT_INSERT_WORKING_NODE_COPY_FROM_BASE_1
insert or replace into NODES (
    wc_id, local_relpath, op_depth, parent_relpath, repos_id,
    repos_path, revision, presence, depth, kind, changed_revision,
    changed_date, changed_author, checksum, properties, translated_size,
    last_mod_time, symlink_target )
select wc_id, ?3 as local_relpath, ?4 as op_depth, ?5 as parent_relpath,
    ?6 as repos_id, ?7 as repos_path, ?8 as revision, ?9 as presence, depth,
    kind, changed_revision, changed_date, changed_author, checksum, properties,
    translated_size, last_mod_time, symlink_target
from NODES
where wc_id = ?1 and local_relpath = ?2 and op_depth = 0;

-- STMT_INSERT_WORKING_NODE_COPY_FROM_WORKING
INSERT OR REPLACE INTO WORKING_NODE (
    wc_id, local_relpath, parent_relpath, presence, kind, checksum,
    translated_size, changed_rev, changed_date, changed_author, depth,
    symlink_target, last_mod_time, properties, copyfrom_repos_id,
    copyfrom_repos_path, copyfrom_revnum )
SELECT wc_id, ?3 AS local_relpath, ?4 AS parent_relpath, ?5 AS presence, kind,
    checksum, translated_size, changed_rev, changed_date, changed_author, depth,
    symlink_target, last_mod_time, properties, ?6 AS copyfrom_repos_id,
    ?7 AS copyfrom_repos_path, ?8 AS copyfrom_revnum FROM WORKING_NODE
WHERE wc_id = ?1 AND local_relpath = ?2;

-- STMT_INSERT_WORKING_NODE_COPY_FROM_WORKING_1
insert or replace into NODES (
    wc_id, local_relpath, op_depth, parent_relpath, repos_id, repos_path,
    revision, presence, depth, kind, changed_revision, changed_date,
    changed_author, checksum, properties, translated_size, last_mod_time,
    symlink_target )
select wc_id, ?3 as local_relpath, ?4 as op_depth, ?5 as parent_relpath,
    ?6 as repos_id, ?7 as repos_path, ?8 as revision, ?9 as presence, depth,
    kind, changed_revision, changed_date, changed_author, checksum, properties,
    translated_size, last_mod_time, symlink_target
from NODES
where wc_id = ?1 and local_relpath = ?2
order by op_depth desc
limit 1;

-- STMT_INSERT_ACTUAL_NODE_FROM_ACTUAL_NODE
INSERT OR REPLACE INTO ACTUAL_NODE (
     wc_id, local_relpath, parent_relpath, properties,
     conflict_old, conflict_new, conflict_working,
     prop_reject, changelist, text_mod, tree_conflict_data )
SELECT wc_id, ?3 AS local_relpath, ?4 AS parent_relpath, properties,
     conflict_old, conflict_new, conflict_working,
     prop_reject, changelist, text_mod, tree_conflict_data FROM ACTUAL_NODE
WHERE wc_id = ?1 AND local_relpath = ?2;

-- STMT_SELECT_SUBDIR
SELECT 1 FROM BASE_NODE WHERE wc_id = ?1 and local_relpath = ?2 and kind = 'subdir'
UNION
SELECT 0 FROM WORKING_NODE WHERE wc_id = ?1 and local_relpath = ?2 and kind = 'subdir';

-- STMT_UPDATE_BASE_REVISION
UPDATE BASE_NODE SET revnum=?3
WHERE wc_id = ?1 AND local_relpath = ?2;

-- STMT_UPDATE_BASE_REVISION_1
update nodes set revision = ?3
where wc_id = ?1 and local_relpath = ?2 and op_depth = 0;

-- STMT_UPDATE_BASE_REPOS
UPDATE BASE_NODE SET repos_id = ?3, repos_relpath = ?4
WHERE wc_id = ?1 AND local_relpath = ?2;

-- STMT_UPDATE_BASE_REPOS_1
update nodes set repos_id = ?3, repos_path = ?4
where wc_id = ?1 and local_relpath = ?2 and op_depth = 0;

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

-- STMT_INSERT_BASE_NODE_FOR_ENTRY_1
/* The BASE tree has a fixed op_depth '0' */
insert or replace into nodes (
  wc_id, local_relpath, op_depth, parent_relpath, repos_id, repos_path,
  revision, presence, kind, checksum,
  changed_revision, changed_date, changed_author, depth, properties,
  translated_size, last_mod_time )
values (?1, ?2, 0, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13,
       ?14, ?15, ?16 );

-- STMT_INSERT_WORKING_NODE
insert or replace into working_node (
  wc_id, local_relpath, parent_relpath, presence, kind,
  copyfrom_repos_id,
  copyfrom_repos_path, copyfrom_revnum, moved_here, moved_to, checksum,
  translated_size, changed_rev, changed_date, changed_author, depth,
  last_mod_time, properties, keep_local, symlink_target)
values (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14,
  ?15, ?16, ?17, ?18, ?19, ?20);

-- STMT_INSERT_WORKING_NODE_DATA_1
insert or replace into node_data (
  wc_id, local_relpath, op_depth, parent_relpath, presence, kind,
  original_repos_id, original_repos_path, original_revision, checksum,
  changed_revision, changed_date, changed_author, depth, properties,
  symlink_target )
values (?1,  ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9,
        ?10, ?11, ?12, ?13, ?14, ?15, ?16 );

-- STMT_INSERT_WORKING_NODE_DATA_2
insert or replace into working_node (
  wc_id, local_relpath, parent_relpath, moved_here, moved_to, translated_size,
  last_mod_time, keep_local )
values (?1,  ?2, ?3, ?4, ?5, ?6, ?7, ?8 );


-- STMT_INSERT_ACTUAL_NODE
insert or replace into actual_node (
  wc_id, local_relpath, parent_relpath, properties, conflict_old,
  conflict_new,
  conflict_working, prop_reject, changelist, text_mod,
  tree_conflict_data)
values (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11);

-- STMT_SELECT_KEEP_LOCAL_FLAG
select keep_local from working_node
where wc_id = ?1 and local_relpath = ?2;

-- STMT_UPDATE_KEEP_LOCAL_FLAG
update working_node set keep_local= ?3
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

-- STMT_SELECT_ALL_FILES
/* Should this select on wc_id as well? */
SELECT local_relpath FROM BASE_NODE
WHERE kind = 'file' AND parent_relpath = ?1
UNION
SELECT local_relpath FROM WORKING_NODE
WHERE kind = 'file' AND parent_relpath = ?1;

-- STMT_PLAN_PROP_UPGRADE
SELECT 0, presence, wc_id FROM BASE_NODE WHERE local_relpath = ?1
UNION ALL
SELECT 1, presence, wc_id FROM WORKING_NODE WHERE local_relpath = ?1;


/* ------------------------------------------------------------------------- */

/* Grab all the statements related to the schema.  */

-- include: wc-metadata
