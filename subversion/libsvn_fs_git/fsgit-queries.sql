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

-- STMT_INSERT_UUID
INSERT INTO REPOSITORY(uuid) VALUES (?1)

-- STMT_SELECT_HEADREV
SELECT MAX(
   IFNULL((SELECT MAX(revnum) FROM REVMAP), 0),
   IFNULL((SELECT MAX(revnum) FROM TAGMAP), 0),
   IFNULL((SELECT MAX(from_rev) FROM BRANCHMAP), 0))

-- STMT_SELECT_REV_BY_COMMITID
SELECT revnum FROM REVMAP WHERE commit_id = ?1

-- STMT_SELECT_COMMIT_BY_REV
SELECT commit_id, relpath, revnum
FROM REVMAP
WHERE revnum <= ?1
ORDER BY revnum DESC
LIMIT 1

-- STMT_INSERT_COMMIT
INSERT INTO REVMAP (revnum, commit_id, relpath) VALUES (?1, ?2, ?3)


/* Grab all the statements related to the schema.  */

-- include: fsgit-metadata
