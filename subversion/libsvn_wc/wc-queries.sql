/* wc-queries.sql -- queries used to interact with the wc-metadata
 *                   SQLite database
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

-- STMT_LOOK_FOR_WORK
SELECT id FROM WORK_QUEUE LIMIT 1;

-- STMT_INSERT_WORK_ITEM
INSERT INTO WORK_QUEUE (work) values (?1);

-- STMT_SELECT_WORK_ITEM
SELECT id, work FROM WORK_QUEUE LIMIT 1;

-- STMT_DELETE_WORK_ITEM
DELETE FROM WORK_QUEUE WHERE id = ?1;


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
