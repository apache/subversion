/* wc-test-queries.sql -- queries used to verify wc metadata from
 *                        the C tests.
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

-- STMT_SELECT_NODES_INFO
SELECT op_depth, n.presence, n.local_relpath, revision,
       repos_path, file_external, def_local_relpath, moved_to, moved_here,
       properties
FROM nodes n
LEFT OUTER JOIN externals e
            ON n.wc_id = e.wc_id
                AND n.local_relpath = e.local_relpath
WHERE n.wc_id = ?1
  AND (n.local_relpath = ?2 OR IS_STRICT_DESCENDANT_OF(n.local_relpath, ?2))

-- STMT_SELECT_ACTUAL_INFO
SELECT local_relpath
FROM actual_node
WHERE wc_id = ?1
  AND conflict_data is NOT NULL
  AND (local_relpath = ?2 OR IS_STRICT_DESCENDANT_OF(local_relpath, ?2))

-- STMT_DELETE_NODES
DELETE FROM nodes;

-- STMT_INSERT_NODE
INSERT INTO nodes (local_relpath, op_depth, presence, repos_path,
                   revision, parent_relpath, moved_to, moved_here,
                   properties, wc_id, repos_id, kind,
                   depth)
           VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, 1,
                   CASE WHEN ?3 != 'base-deleted' THEN 1 END,
                   'dir',
                   CASE WHEN ?3 in ('normal', 'incomplete')
                        THEN 'infinity' END)

-- STMT_DELETE_ACTUAL
DELETE FROM actual_node;

-- STMT_INSERT_ACTUAL
INSERT INTO actual_node (local_relpath, parent_relpath, changelist, wc_id)
                VALUES (?1, ?2, ?3, 1)

-- STMT_ENSURE_EMPTY_PRISTINE
INSERT OR IGNORE INTO pristine (checksum, md5_checksum, size, refcount)
  VALUES ('$sha1$da39a3ee5e6b4b0d3255bfef95601890afd80709',
          '$md5 $d41d8cd98f00b204e9800998ecf8427e',
          0, 0)

-- STMT_NODES_SET_FILE
UPDATE nodes
   SET kind = 'file',
       checksum = '$sha1$da39a3ee5e6b4b0d3255bfef95601890afd80709',
       depth = NULL
WHERE wc_id = 1 and local_relpath = ?1

-- STMT_SELECT_ALL_ACTUAL
SELECT local_relpath FROM actual_node WHERE wc_id = 1

