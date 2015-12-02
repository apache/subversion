/* fsgit-queries.sql -- queries used to interact with the git-metadata
 *                      SQLite database
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

-- STMT_SELECT_UUID
SELECT uuid from REPOSITORY

/* Use fixed id, to support overwriting */
-- STMT_INSERT_UUID
INSERT OR REPLACE INTO REPOSITORY(id, uuid) VALUES (1, ?1)

-- STMT_SELECT_HEADREV
SELECT MAX(
   IFNULL((SELECT MAX(revnum) FROM REVMAP), 0),
   IFNULL((SELECT MAX(revnum) FROM TAGMAP), 0),
   IFNULL((SELECT MAX(from_rev) FROM BRANCHMAP), 0))

-- STMT_SELECT_REV_BY_COMMITID
SELECT revnum, relpath FROM REVMAP WHERE commit_id = ?1

-- STMT_SELECT_COMMIT_BY_REV
SELECT commit_id, relpath, revnum
FROM REVMAP
WHERE revnum <= ?1
ORDER BY revnum DESC
LIMIT 1

-- STMT_INSERT_COMMIT
INSERT INTO REVMAP (revnum, commit_id, relpath, prev_revnum)
VALUES (?1, ?2, ?3, ?4)

-- STMT_SELECT_CHECKSUM
SELECT md5_checksum, sha1_checksum
FROM CHECKSUMMAP
WHERE blob_id = ?1

-- STMT_INSERT_CHECKSUM
INSERT INTO CHECKSUMMAP (blob_id, md5_checksum, sha1_checksum)
VALUES (?1, ?2, ?3)

-- STMT_SELECT_BRANCH_NAME
SELECT relpath
FROM REVMAP
WHERE (relpath <= ?1 AND relpath || '0' > ?1) AND (relpath = ?1 OR relpath || '/' < ?1)
UNION ALL
SELECT relpath
FROM TAGMAP
WHERE (relpath <= ?1 AND relpath || '0' > ?1) AND (relpath = ?1 OR relpath || '/' < ?1)
LIMIT 1

-- STMT_SELECT_BRANCH
SELECT relpath, (SELECT commit_id FROM revmap r WHERE r.revnum=t.from_rev),
       from_rev
FROM TAGMAP t WHERE relpath = ?1 AND revnum <= ?2
UNION ALL
SELECT relpath, commit_id, revnum
FROM REVMAP
WHERE relpath = ?1 AND revnum <= ?2
ORDER BY relpath DESC, revnum DESC
LIMIT 1

-- STMT_SELECT_TAG
SELECT revnum, from_rev, relpath FROM TAGMAP where relpath = ?1

-- STMT_INSERT_TAG
INSERT INTO TAGMAP (revnum, from_rev, relpath) VALUES (?1, ?2, ?3)

-- STMT_SELECT_BRANCHES
SELECT DISTINCT relpath FROM BRANCHMAP
WHERE relpath > ?1 || '/' AND relpath < ?1 || '0'
AND from_rev <= ?2 AND (to_rev > ?2 OR to_rev IS NULL)

-- STMT_SELECT_TAGS
SELECT DISTINCT relpath FROM TAGMAP
WHERE relpath > ?1 || '/' AND relpath < ?1 || '0'
AND revnum <= ?2

/* Grab all the statements related to the schema.  */

-- include: fsgit-metadata
