/* wc-checks.sql -- trigger-based checks for the wc-metadata database.
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


-- STMT_VERIFICATION_TRIGGERS

/* ------------------------------------------------------------------------- */

CREATE TEMPORARY TRIGGER no_repository_updates BEFORE UPDATE ON repository
BEGIN
  SELECT RAISE(FAIL, 'Updates to REPOSITORY are not allowed.');
END;

/* ------------------------------------------------------------------------- */

/* Verify: on every NODES row: parent_relpath is parent of local_relpath */
CREATE TEMPORARY TRIGGER validation_01 BEFORE INSERT ON nodes
WHEN NOT ((new.local_relpath = '' AND new.parent_relpath IS NULL)
          OR (relpath_depth(new.local_relpath)
              = relpath_depth(new.parent_relpath) + 1))
BEGIN
  SELECT RAISE(FAIL, 'WC DB validity check 01 failed');
END;

/* Verify: on every NODES row: its op-depth <= its own depth */
CREATE TEMPORARY TRIGGER validation_02 BEFORE INSERT ON nodes
WHEN NOT new.op_depth <= relpath_depth(new.local_relpath)
BEGIN
  SELECT RAISE(FAIL, 'WC DB validity check 02 failed');
END;

/* Verify: on every NODES row: it is an op-root or it has a parent with the
    sames op-depth. (Except when the node is a file external) */
CREATE TEMPORARY TRIGGER validation_03 BEFORE INSERT ON nodes
WHEN NOT (
    (new.op_depth = relpath_depth(new.local_relpath))
    OR
    (EXISTS (SELECT 1 FROM nodes
              WHERE wc_id = new.wc_id AND op_depth = new.op_depth
                AND local_relpath = new.parent_relpath))
  )
 AND NOT (new.file_external IS NOT NULL AND new.op_depth = 0)
BEGIN
  SELECT RAISE(FAIL, 'WC DB validity check 03 failed');
END;

/* Verify: on every ACTUAL row (except root): a NODES row exists at its
 * parent path. */
CREATE TEMPORARY TRIGGER validation_04 BEFORE INSERT ON actual_node
WHEN NOT (new.local_relpath = ''
          OR EXISTS (SELECT 1 FROM nodes
                       WHERE wc_id = new.wc_id
                         AND local_relpath = new.parent_relpath))
BEGIN
  SELECT RAISE(FAIL, 'WC DB validity check 04 failed');
END;

-- STMT_STATIC_VERIFY
SELECT local_relpath, op_depth, 'SV001: No ancestor in NODES'
FROM nodes n WHERE local_relpath != ''
 AND file_external IS NULL
 AND NOT EXISTS(SELECT 1 from nodes i
                WHERE i.wc_id=n.wc_id
                  AND i.local_relpath=n.parent_relpath
                  AND i.op_depth <= n.op_depth)

UNION ALL

SELECT local_relpath, -1, 'SV002: No ancestor in ACTUAL'
FROM actual_node a WHERE local_relpath != ''
 AND NOT EXISTS(SELECT 1 from nodes i
                WHERE i.wc_id=a.wc_id
                  AND i.local_relpath=a.parent_relpath)
 AND NOT EXISTS(SELECT 1 from nodes i
                WHERE i.wc_id=a.wc_id
                  AND i.local_relpath=a.local_relpath)

UNION ALL

SELECT a.local_relpath, -1, 'SV003: Bad or Unneeded actual data'
FROM actual_node a
LEFT JOIN nodes n on n.wc_id = a.wc_id AND n.local_relpath = a.local_relpath
   AND n.op_depth = (SELECT MAX(op_depth) from nodes i
                     WHERE i.wc_id=a.wc_id AND i.local_relpath=a.local_relpath)
WHERE (a.properties IS NOT NULL
       AND n.presence NOT IN (MAP_NORMAL, MAP_INCOMPLETE))
   OR (a.changelist IS NOT NULL AND (n.kind IS NOT NULL AND n.kind != MAP_FILE))
   OR (a.conflict_data IS NULL AND a.properties IS NULL AND a.changelist IS NULL)
 AND NOT EXISTS(SELECT 1 from nodes i
                WHERE i.wc_id=a.wc_id
                  AND i.local_relpath=a.parent_relpath)

UNION ALL

SELECT local_relpath, op_depth, 'SV004: Unneeded node data'
FROM nodes
WHERE presence NOT IN (MAP_NORMAL, MAP_INCOMPLETE)
AND (properties IS NOT NULL
     OR checksum IS NOT NULL
     OR depth IS NOT NULL
     OR symlink_target IS NOT NULL
     OR changed_revision IS NOT NULL
     OR (changed_date IS NOT NULL AND changed_date != 0)
     OR changed_author IS NOT NULL
     OR translated_size IS NOT NULL
     OR last_mod_time IS NOT NULL
     OR dav_cache IS NOT NULL
     OR file_external IS NOT NULL
     OR inherited_props IS NOT NULL)

UNION ALL

SELECT local_relpath, op_depth, 'SV005: Unneeded base-deleted node data'
FROM nodes
WHERE presence IN (MAP_BASE_DELETED)
AND (repos_id IS NOT NULL
     OR repos_path IS NOT NULL
     OR revision IS NOT NULL)

UNION ALL

SELECT local_relpath, op_depth, 'SV006: Kind specific data invalid on normal'
FROM nodes
WHERE presence IN (MAP_NORMAL, MAP_INCOMPLETE)
AND (kind IS NULL
     OR (repos_path IS NULL
         AND (properties IS NOT NULL
              OR changed_revision IS NOT NULL
              OR changed_author IS NOT NULL
              OR (changed_date IS NOT NULL AND changed_date != 0)))
     OR (CASE WHEN kind = MAP_FILE AND repos_path IS NOT NULL
                                   THEN checksum IS NULL
                                   ELSE checksum IS NOT NULL END)
     OR (CASE WHEN kind = MAP_DIR THEN depth IS NULL
                                  ELSE depth IS NOT NULL END)
     OR (CASE WHEN kind = MAP_SYMLINK THEN symlink_target IS NULL
                                      ELSE symlink_target IS NOT NULL END))

UNION ALL

SELECT local_relpath, op_depth, 'SV007: Invalid op-depth for local add'
FROM nodes
WHERE presence IN (MAP_NORMAL, MAP_INCOMPLETE)
  AND repos_path IS NULL
  AND op_depth != relpath_depth(local_relpath)

UNION ALL

SELECT local_relpath, op_depth, 'SV008: Node missing ancestor'
FROM nodes n
WHERE op_depth < relpath_depth(local_relpath)
  AND file_external IS NULL
  AND NOT EXISTS(SELECT 1 FROM nodes p
                 WHERE p.wc_id=n.wc_id AND p.local_relpath=n.parent_relpath
                   AND p.op_depth=n.op_depth
                   AND (p.presence IN (MAP_NORMAL, MAP_INCOMPLETE)
                        OR (p.presence = MAP_BASE_DELETED
                            AND n.presence = MAP_BASE_DELETED)))

UNION all

SELECT n.local_relpath, n.op_depth, 'SV009: Copied descendant mismatch'
FROM nodes n
JOIN nodes p
  ON p.wc_id=n.wc_id AND p.local_relpath=n.parent_relpath
  AND n.op_depth=p.op_depth
WHERE n.op_depth > 0 AND n.presence IN (MAP_NORMAL, MAP_INCOMPLETE)
   AND (n.repos_id != p.repos_id
        OR n.repos_path !=
           RELPATH_SKIP_JOIN(n.parent_relpath, p.repos_path, n.local_relpath)
        OR n.revision != p.revision)

UNION all

SELECT n.local_relpath, n.op_depth, 'SV010: Invalid op-root presence'
FROM nodes n
WHERE n.op_depth = relpath_depth(local_relpath)
  AND presence NOT IN (MAP_NORMAL, MAP_INCOMPLETE, MAP_BASE_DELETED)
