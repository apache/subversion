/* wc-metadata.sql -- schema used in the wc-metadata SQLite database
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

/* One big list of statements to create our (current) schema.  */
-- STMT_CREATE_SCHEMA

CREATE TABLE REPOSITORY (
  id INTEGER PRIMARY KEY AUTOINCREMENT CHECK(id=1),

  /* the UUID of the repository */
  uuid  TEXT NOT NULL
);

CREATE TABLE REVMAP (
  /* The Subversion revision number */
  revnum INTEGER PRIMARY KEY AUTOINCREMENT,

  /* The git commit mapped to the revision */
  commit_id BINARY NOT NULL,

  /* The relpath below which we express this commit (E.g. 'trunk') */
  relpath TEXT NOT NULL,

  prev_revnum INTEGER NULL
);

CREATE UNIQUE INDEX I_REVMAP_COMMIT_ID ON REVMAP (commit_id);
CREATE UNIQUE INDEX I_REVMAP_RELPATH_ID ON REVMAP (relpath, revnum);

CREATE TABLE TAGMAP (
  /* The revision in which the tag was created */
  revnum INTEGER PRIMARY KEY AUTOINCREMENT,
  from_rev INTEGER NOT NULL,
  relpath TEXT NOT NULL
);

CREATE UNIQUE INDEX I_TAGMAP_RELPATH ON TAGMAP (relpath);

CREATE TABLE BRANCHMAP (
  id INTEGER PRIMARY KEY AUTOINCREMENT,

  relpath TEXT NOT NULL,

  from_rev INTEGER NOT NULL,
  to_rev INTEGER NULL
);

CREATE UNIQUE INDEX I_BRANCHMAP_RELPATH ON BRANCHMAP (relpath, from_rev);
CREATE UNIQUE INDEX I_BRANCHMAP_FROM_REV ON BRANCHMAP (from_rev, relpath);

CREATE TABLE CHECKSUMMAP (
  blob_id BINARY NOT NULL PRIMARY KEY,

  md5_checksum TEXT NOT NULL,
  sha1_checksum TEXT NOT NULL
);

PRAGMA user_version =
-- define: SVN_FS_GIT__VERSION
;
