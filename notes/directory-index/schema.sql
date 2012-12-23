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

DROP VIEW IF EXISTS nodeview;
DROP TABLE IF EXISTS noderev;
DROP TABLE IF EXISTS string;
DROP TABLE IF EXISTS branch;
DROP TABLE IF EXISTS txn;


-- Transactions
CREATE TABLE txn (
  -- transaction number
  id        integer NOT NULL PRIMARY KEY,

  -- the version of the tree associated with this transaction;
  -- initially the same as id, but may refer to the originator
  -- transaction when tracking revprop changes and/or modified trees
  -- (q.v., obliterate)
  treeid    integer NULL REFERENCES txn(id),

  -- the revision that this transaction represents; for uncommitted
  -- transactions, the revision in which it was created
  revision  integer NULL,

  -- creation date, independent of the svn:date property
  created   timestamp NOT NULL,

  -- transaction author, independent of the svn:author property; may
  -- be null if the repository allows anonymous modifications
  author    varchar NULL,

  -- transaction state
  -- T = transient (uncommitted), P = permanent (committed), D = dead
  state     character(1) NOT NULL DEFAULT 'T',

  -- sanity check: enumerated value validation
  CONSTRAINT enumeration_validation CHECK (state IN ('T', 'P', 'D'))

  -- other attributes:
     -- revision properties
);

CREATE INDEX txn_revision_idx ON txn(revision);


-- Branches -- unique forks in the nodes' history
CREATE TABLE branch (
  -- branch identifier
  id        integer NOT NULL PRIMARY KEY,

  -- the transaction in which the branch was created
  treeid    integer NOT NULL REFERENCES txn(id),

  -- the node to which this branch belongs; refers to the initial
  -- branch of the node
  nodeid    integer NULL REFERENCES branch(id),

  -- the source branch from which this branch was forked
  origin    integer NULL REFERENCES branch(id),

  -- mark branches in uncommitted transactions so that they can be
  -- ignored by branch traversals
  -- T = transient (uncommitted), P = permanent (committed)
  state     character(1) NOT NULL DEFAULT 'T',

  -- sanity check: enumerated value validation
  CONSTRAINT enumeration_validation CHECK (state IN ('T', 'P')),

  -- sanity check: ye can't be yer own daddy
  CONSTRAINT genetic_diversity CHECK (id <> origin)
);

CREATE INDEX branch_txn_idx ON branch(treeid);
CREATE INDEX branch_node_idx ON branch(nodeid);


-- File names -- lookup table of strings
CREATE TABLE string (
  id        integer NOT NULL PRIMARY KEY,
  val       varchar NOT NULL UNIQUE
);


-- Node revisions -- DAG of versioned node changes
CREATE TABLE noderev (
  -- node revision identifier
  id        integer NOT NULL PRIMARY KEY,

  -- the transaction in which the node was changed
  treeid    integer NOT NULL REFERENCES txn(id),

  -- the node identifier; a new node will get the ID of its initial
  -- branch
  nodeid    integer NOT NULL REFERENCES branch(id),

  -- this node revision's immediate predecessor
  origin    integer NULL REFERENCES noderev(id),

  -- the parent (directory) of this node revision -- tree graph
  parent    integer NULL REFERENCES branch(id),

  -- the branch that this node revision belongs to -- history graph
  branch    integer NOT NULL REFERENCES branch(id),

  -- the indexable, NFC-normalized name of this noderev within its parent
  nameid    integer NOT NULL REFERENCES string(id),

  -- the original, denormalized, non-indexable name
  denameid  integer NOT NULL REFERENCES string(id),

  -- the node kind; immutable within the node
  -- D = directory, F = file, etc.
  kind      character(1) NOT NULL,

  -- the change that produced this node revision
  -- A = added, D = deleted, M = modified, N = renamed, R = replaced
  -- B = branched (added + origin <> null)
  -- L = lazy branch, indicates that child lookup should be performed
  --     on the origin (requires kind=D + added + origin <> null)
  -- X = replaced by branch (R + B)
  -- Z = lazy replace by branch (Like L but implies X instead of B)
  opcode    character(1) NOT NULL,

  -- mark noderevs of uncommitted transactions so that they can be
  -- ignored by tree traversals
  -- T = transient (uncommitted), P = permanent (committed)
  state     character(1) NOT NULL DEFAULT 'T',

  -- sanity check: enumerated value validation
  CONSTRAINT enumeration_validation CHECK (
    kind IN ('D', 'F')
    AND state IN ('T', 'P')
    AND opcode IN ('A', 'D', 'M', 'N', 'R', 'B', 'L', 'X', 'Z')),

  -- sanity check: only directories can be lazy
  CONSTRAINT lazy_copies_make_more_work CHECK (
    opcode NOT IN ('B', 'L', 'X', 'Z')
    OR (opcode IN ('B', 'X') AND origin IS NOT NULL)
    OR (opcode IN ('L', 'Z') AND kind = 'D' AND origin IS NOT NULL)),

  -- sanity check: ye can't be yer own daddy
  CONSTRAINT genetic_diversity CHECK (id <> origin),

  -- sanity check: ye can't be yer own stepdaddy, either
  CONSTRAINT escher_avoidance CHECK (parent <> branch)

  -- other attributes:
     -- versioned properties
     -- contents reference
);

CREATE UNIQUE INDEX noderev_tree_idx ON noderev(parent,nameid,treeid,opcode);
CREATE INDEX noderev_txn_idx ON noderev(treeid);
CREATE INDEX nodefev_node_idx ON noderev(nodeid);
CREATE INDEX noderev_successor_idx ON noderev(origin);


CREATE VIEW nodeview AS
  SELECT
    noderev.*,
    ns.val AS name,
    ds.val AS dename
  FROM
    noderev JOIN string AS ns ON noderev.nameid = ns.id
    JOIN string AS ds ON noderev.denameid = ds.id;


-- Root directory

INSERT INTO txn (id, treeid, revision, created, state)
  VALUES (0, 0, 0, 'EPOCH', 'P');
INSERT INTO branch (id, treeid, nodeid, state) VALUES (0, 0, 0, 'P');
INSERT INTO string (id, val) VALUES (0, '');
INSERT INTO noderev (id, treeid, nodeid, branch,
                     nameid, denameid, kind, opcode, state)
  VALUES (0, 0, 0, 0, 0, 0, 'D', 'A', 'P');


---STATEMENT TXN_INSERT
INSERT INTO txn (treeid, revision, created, author)
  VALUES (:treeid, :revision, :created, :author);

---STATEMENT TXN_UPDATE_INITIAL_TREEID
UPDATE txn SET treeid = :id WHERE id = :id;

---STATEMENT TXN_GET
SELECT * FROM txn WHERE id = :id;

---STATEMENT TXN_FIND_NEWEST
SELECT * FROM txn WHERE state = 'P' ORDER BY id DESC LIMIT 1;

---STATEMENT TXN_FIND_BY_REVISION
SELECT * FROM txn WHERE revision = :revision AND state = 'P'
ORDER BY id DESC LIMIT 1;

---STATEMENT TXN_FIND_BY_REVISION_AND_TIMESTAMP
SELECT * FROM txn
WHERE revision = :revision AND created <= :created AND state = 'P'
ORDER BY id DESC LIMIT 1;

---STATEMENT TXN_COMMIT
UPDATE txn SET
  revision = :revision,
  created = :created,
  state = 'P'
WHERE id = :id;

---STATEMENT TXN_ABORT
UPDATE txn SET state = 'D' WHERE id = :id;

---STATEMENT TXN_CLEANUP
DELETE FROM txn WHERE id = :id;

---STATEMENT BRANCH_INSERT
INSERT INTO branch (nodeid, treeid, origin)
  VALUES (:nodeid, :treeid, :origin);

---STATEMENT BRANCH_UPDATE_INITIAL_NODEID
UPDATE branch SET nodeid = :id WHERE id = :id;

---STATEMENT BRANCH_UPDATE_TREEID
UPDATE branch SET treeid = :new_treeid WHERE treeid = :old_treeid;

---STATEMENT BRANCH_GET
SELECT * FROM branch WHERE id = :id;

---STATEMENT BRANCH_HISTORY
SELECT * from branch WHERE nodeid = :nodeid ORDER BY id ASC;

---STATEMENT BRANCH_COMMIT
UPDATE branch SET state = 'P' WHERE treeid = :treeid;

---STATEMENT BRANCH_CLEANUP
DELETE FROM branch WHERE treeid = :treeid;

---STATEMENT STRING_INSERT
INSERT INTO string (val) VALUES (:val);

---STATEMENT STRING_FIND
SELECT * FROM string WHERE val = :val;

---STATEMENT NODEREV_INSERT
INSERT INTO noderev (nodeid, treeid, origin, parent, branch,
                     nameid, denameid, kind, opcode)
  VALUES (:nodeid, :treeid, :origin, :parent, :branch,
          :nameid, :denameid, :kind, :opcode);

---STATEMENT NODEREV_UPDATE_TREEID
UPDATE noderev SET treeid = :new_treeid WHERE treeid = :old_treeid;

---STATEMENT NODEREV_UPDATE_OPCODE
UPDATE noderev SET opcode = :opcode WHERE id = :id;

---STATEMENT NODEVIEW_GET
SELECT * FROM nodeview WHERE id = :id;

---STATEMENT NODEREV_COUNT_SUCCESSORS
SELECT COUNT(id) FROM noderev WHERE origin = :origin;

---STATEMENT NODEREV_COMMIT
UPDATE noderev SET state = 'P' WHERE treeid = :treeid;

---STATEMENT NODEREV_CLEANUP
DELETE FROM noderev WHERE treeid = :treeid;

---STATEMENT NODEVIEW_FIND_ROOT
SELECT * FROM nodeview
WHERE parent IS NULL AND name = ''
      AND treeid <= :treeid AND state = 'P'
ORDER BY treeid DESC LIMIT 1;

---STATEMENT NODEVIEW_FIND_BY_NAME
SELECT * FROM nodeview
WHERE parent = :parent AND name = :name
      AND treeid <= :treeid AND state = 'P'
ORDER BY treeid DESC LIMIT 1;

---STATEMENT NODEVIEW_FIND_TRANSIENT_ROOT
SELECT * FROM nodeview
WHERE parent IS NULL AND name = ''
      AND (treeid < :treeid AND state = 'P' OR treeid = :treeid)
ORDER BY treeid DESC LIMIT 1;

---STATEMENT NODEVIEW_FIND_TRANSIENT_BY_NAME
SELECT * FROM nodeview
WHERE parent = :parent AND name = :name
      AND (treeid < :treeid AND state = 'P' OR treeid = :treeid)
ORDER BY treeid DESC LIMIT 1;

---STATEMENT NODEVIEW_LIST_DIRECTORY
SELECT * FROM nodeview
  JOIN (SELECT nameid, MAX(treeid) AS treeid FROM noderev
        WHERE treeid <= :treeid AND state = 'P') AS filter
    ON nodeview.nameid = filter.nameid AND nodeview.treeid = filter.treeid
WHERE parent = :parent AND opcode <> 'D'
ORDER BY nodeview.name ASC;

---STATEMENT NODEVIEW_LIST_TRANSIENT_DIRECTORY
SELECT * FROM nodeview
  JOIN (SELECT nameid, MAX(treeid) AS treeid FROM noderev
        WHERE treeid < :treeid AND state = 'P' OR treeid = :treeid) AS filter
    ON nodeview.nameid = filter.name AND nodeview.treeid = filter.treeid
WHERE parent = :parent AND opcode <> 'D'
ORDER BY nodeview.name ASC;
