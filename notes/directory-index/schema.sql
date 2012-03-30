-- -*- mode: sql; sql-product: sqlite; coding: utf-8 -*-
-- Licensed to the Apache Software Foundation (ASF) under one
-- or more contributor license agreements.  See the NOTICE file
-- distributed with this work for additional information
-- regarding copyright ownership.  The ASF licenses this file
-- to you under the Apache License, Version 2.0 (the
-- "License"); you may not use this file except in compliance
-- with the License.  You may obtain a copy of the License at
--
--   http://www.apache.org/licenses/LICENSE-2.0
--
-- Unless required by applicable law or agreed to in writing,
-- software distributed under the License is distributed on an
-- "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
-- KIND, either express or implied.  See the License for the
-- specific language governing permissions and limitations
-- under the License.


---SCRIPT CREATE_SCHEMA

DROP TABLE IF EXISTS dirindex;
DROP TABLE IF EXISTS strindex;
DROP TABLE IF EXISTS revision;

-- Revision record

CREATE TABLE revision (
  version integer NOT NULL PRIMARY KEY,
  created timestamp NOT NULL,
  author  varchar NULL,
  log     varchar NULL
);

-- Path lookup table

CREATE TABLE strindex (
  strid   integer NOT NULL PRIMARY KEY,
  content varchar NOT NULL UNIQUE
);

-- Versioned directory tree

CREATE TABLE dirindex (
  -- unique id of this node revision, used for
  -- predecessor/successor links
  rowid   integer NOT NULL PRIMARY KEY,

  -- link to this node's immediate predecessor
  origin  integer NULL REFERENCES dirindex(rowid),

  -- absolute (repository) path
  pathid  integer NOT NULL REFERENCES strindex(strid),

  -- revision number
  version integer NOT NULL REFERENCES revision(version),

  -- node kind (D = dir, F = file, etc.)
  kind    character(1) NOT NULL,

  -- the operation that produced this entry:
  -- A = add, R = replace, M = modify, D = delete, N = rename
  opcode  character(1) NOT NULL,

  -- the index entry is the result of an implicit subtree operation
  subtree boolean NOT NULL
);
CREATE UNIQUE INDEX dirindex_versioned_tree ON dirindex(pathid, version DESC);
CREATE INDEX dirindex_successor_list ON dirindex(origin);
CREATE INDEX dirindex_operation ON dirindex(opcode);

-- Repository root

INSERT INTO revision (version, created, author, log)
  VALUES (0, 'EPOCH', NULL, NULL);
INSERT INTO strindex (strid, content) VALUES (0, '/');
INSERT INTO dirindex (rowid, origin, pathid, version, kind, opcode, subtree)
  VALUES (0, NULL, 0, 0, 'D', 'A', 0);


---STATEMENT INSERT_REVISION_RECORD

INSERT INTO revision (version, created, author, log)
  VALUES (?, ?, ?, ?);

---STATEMENT GET_REVENT_BY_VERSION

SELECT * FROM revision WHERE version = ?;

---STATEMENT INSERT_STRINDEX_RECORD

INSERT INTO strindex (content) VALUES (?);

---STATEMENT GET_STRENT_BY_STRID

SELECT * FROM strindex WHERE strid = ?;

---STATEMENT GET_STRENT_BY_CONTENT

SELECT * FROM strindex WHERE content = ?;

---STATEMENT INSERT_DIRINDEX_RECORD

INSERT INTO dirindex (origin, pathid, version, kind, opcode, subtree)
  VALUES (?, ?, ?, ?, ?, ?);

---STATEMENT GET_DIRENT_BY_ROWID

SELECT dirindex.*, strindex.content FROM dirindex
  JOIN strindex ON dirindex.pathid = strindex.strid
WHERE dirindex.rowid = ?;

---STATEMENT GET_DIRENT_BY_ABSPATH_AND_VERSION

SELECT dirindex.*, strindex.content AS abspath FROM dirindex
  JOIN strindex ON dirindex.pathid = strindex.strid
WHERE abspath = ? AND dirindex.version = ?;

---STATEMENT LOOKUP_ABSPATH_AT_REVISION

SELECT dirindex.*, strindex.content AS abspath FROM dirindex
  JOIN strindex ON dirindex.pathid = strindex.strid
WHERE abspath = ? AND dirindex.version <= ?
ORDER BY abspath ASC, dirindex.version DESC
LIMIT 1;

---STATEMENT LIST_SUBTREE_AT_REVISION

SELECT dirindex.*, strindex.content AS abspath FROM dirindex
  JOIN strindex ON dirindex.pathid = strindex.strid
  JOIN (SELECT pathid, MAX(version) AS maxver FROM dirindex
        WHERE version <= ? GROUP BY pathid)
    AS filtered
    ON dirindex.pathid == filtered.pathid
        AND dirindex.version == filtered.maxver
WHERE abspath LIKE ? ESCAPE '#'
      AND dirindex.opcode <> 'D'
ORDER BY abspath ASC;

---STATEMENT LIST_DIRENT_SUCCESSORS

SELECT dirindex.*, strindex.content AS abspath FROM dirindex
  JOIN strindex ON dirindex.pathid = strindex.strid
WHERE dirindex.origin = ?
ORDER BY abspath ASC, dirindex.version ASC;


-- Temporary transaction

---SCRIPT CREATE_TRANSACTION_CONTEXT

CREATE TEMPORARY TABLE txncontext (
  origin  integer NULL,
  abspath varchar NOT NULL UNIQUE,
  kind    character(1) NOT NULL,
  opcode  character(1) NOT NULL,
  subtree boolean NOT NULL
);

---SCRIPT REMOVE_TRANSACTION_CONTEXT

DROP TABLE IF EXISTS temp.txncontext;

---STATEMENT INSERT_TRANSACTION_RECORD

INSERT INTO temp.txncontext (origin, abspath, kind, opcode, subtree)
  VALUES (?, ?, ?, ?, ?);

---STATEMENT GET_TRANSACTION_RECORD

SELECT * FROM temp.txncontext WHERE abspath = ?;

---STATEMENT REMOVE_TRANSACTION_RECORD

DELETE FROM temp.txncontext WHERE abspath = ?;

---STATEMENT REMOVE_TRANSACTION_SUBTREE

DELETE FROM temp.txncontext WHERE abspath LIKE ? ESCAPE '#';

---STATEMENT LIST_TRANSACTION_RECORDS

SELECT * FROM temp.txncontext ORDER BY abspath ASC;
