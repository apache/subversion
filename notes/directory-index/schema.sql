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

DROP TABLE IF EXISTS noderev;
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
  txnid     integer NULL REFERENCES txn(id),

  -- the revision that this transaction represents; for uncommitted
  -- transactions, the revision in which it was created
  revision  integer NULL,

  -- transaction state
  -- T = transient (uncommitted), P = permanent (committed), D = dead
  state     character(1) NOT NULL DEFAULT 'T',

  -- creation date, independent of the svn:date property
  created   timestamp NOT NULL,

  -- transaction author, independent of the svn:author property; may
  -- be null if the repository allows anonymous modifications
  author    varchar NULL,

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

  -- the node to which this branch belongs; refers to the initial
  -- branch of the node
  nodeid    integer NULL REFERENCES branch(id),

  -- the source branch from which this branch was forked
  origin    integer NULL REFERENCES branch(id),

  -- the transaction in which the branch was created
  txnid     integer NOT NULL REFERENCES txn(id),

  -- mark branches in uncommitted transactions so that they can be
  -- ignored by branch traversals
  -- T = transient (uncommitted), P = permanent (committed)
  state     character(1) NOT NULL DEFAULT 'T',

  -- sanity check: enumerated value validation
  CONSTRAINT enumeration_validation CHECK (state IN ('T', 'P')),

  -- sanity check: ye can't be yer own daddy
  CONSTRAINT genetic_diversity CHECK (id <> origin)
);

CREATE INDEX branch_txn_idx ON branch(txnid);
CREATE INDEX branch_node_idx ON branch(nodeid);


-- Node revisions -- DAG of versioned node changes
CREATE TABLE noderev (
  -- node revision identifier
  id        integer NOT NULL PRIMARY KEY,

  -- the node identifier; a new node will get the ID of its initial
  -- branch
  nodeid    integer NOT NULL REFERENCES branch(id),

  -- the node kind; immutable within the node
  -- D = directory, F = file, etc.
  kind      character(1) NOT NULL,

  -- this node revision's immediate predecessor
  origin    integer NULL REFERENCES noderev(id),

  -- the parent (directory) of this node revision -- tree graph
  parent    integer NULL REFERENCES branch(id),

  -- the branch that this node revision belongs to -- history graph
  branch    integer NOT NULL REFERENCES branch(id),

  -- the indexable, NFC-normalized name of this noderev within its parent
  name      varchar NOT NULL,

  -- the original, denormalized, non-indexable name; null if it's ths
  -- same as the name
  dename    varchar NULL,

  -- the transaction in which the node was changed
  txnid     integer NOT NULL REFERENCES txn(id),

  -- the change that produced this node revision
  -- A = added, D = deleted, M = modified, N = renamed, R = replaced
  -- B = branched (added + origin <> null)
  -- L = lazy branch, indicates that child lookup should be performed
  --     on the origin (requires kind=D + added + origin <> null)
  opcode    character(1) NOT NULL,

  -- mark noderevs of uncommitted transactions so that they can be
  -- ignored by tree traversals
  -- T = transient (uncommitted), P = permanent (committed)
  state     character(1) NOT NULL DEFAULT 'T',

  -- sanity check: enumerated value validation
  CONSTRAINT enumeration_validation CHECK (
    kind IN ('D', 'F')
    AND state IN ('T', 'P')
    AND opcode IN ('A', 'D', 'M', 'N', 'R', 'B', 'L')),

  -- sanity check: only directories can be lazy
  CONSTRAINT lazy_copies_make_more_work CHECK (
    opcode <> 'B' AND opcode <> 'L'
    OR (opcode = 'B' AND origin IS NOT NULL)
    OR (opcode = 'L' AND kind = 'D' AND origin IS NOT NULL)),

  -- sanity check: ye can't be yer own daddy
  CONSTRAINT genetic_diversity CHECK (id <> origin),

  -- sanity check: ye can't be yer own stepdaddy, either
  CONSTRAINT escher_avoidance CHECK (parent <> branch)

  -- other attributes:
     -- versioned properties
     -- contents reference
);

CREATE UNIQUE INDEX noderev_tree_idx ON noderev(parent, name, txnid);
CREATE INDEX noderev_txn_idx ON noderev(txnid);
CREATE INDEX nodefev_node_idx ON noderev(nodeid);
CREATE INDEX noderev_successor_idx ON noderev(origin);


-- Root directory

INSERT INTO txn (id, txnid, revision, state, created) VALUES (0, 0, 0, 'P', 'EPOCH');
INSERT INTO branch (id, nodeid, txnid, state) VALUES (0, 0, 0, 'P');
INSERT INTO noderev (id, nodeid, kind, branch, name, txnid, opcode, state)
  VALUES (0, 0, 'D', 0, '', 0, 'A', 'P');


---STATEMENT TXN_INSERT
INSERT INTO txn (txnid, revision, created, author)
  VALUES (:txnid, :revision, :created, :author);

---STATEMENT TXN_UPDATE_INITIAL_TXNID
UPDATE txn SET txnid = :id WHERE id = :id;

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
  created = :created
  state = 'P',
WHERE id = :id;

---STATEMENT TXN_ABORT
UPDATE txn SET state = 'D' WHERE id = :id;

---STATEMENT TXN_CLEANUP
DELETE FROM txn WHERE id = :txnid;

---STATEMENT BRANCH_INSERT
INSERT INTO branch (nodeid, origin, txnid)
  VALUES (:nodeid, :origin, :txnid);

---STATEMENT BRANCH_UPDATE_INITIAL_NODEID
UPDATE branch SET nodeid = :id WHERE id = :id;

---STATEMENT BRANCH_UPDATE_TXNID
UPDATE branch SET txnid = :new_txnid WHERE txnid = :old_txnid;

---STATEMENT BRANCH_GET
SELECT * FROM branch WHERE id = :id;

---STATEMENT BRANCH_COMMIT
UPDATE branch SET state = 'P' WHERE txnid = :txnid;

---STATEMENT BRANCH_CLEANUP
DELETE FROM branch WHERE txnid = :txnid;

---STATEMENT NODEREV_INSERT
INSERT INTO noderev (nodeid, kind, origin, parent, branch,
                     name, dename, txnid, opcode)
  VALUES (:nodeid, :kind, :origin, :parent, :branch,
          :name, :dename, :txnid, :opcode);

---STATEMENT NODEREV_UPDATE_TXNID
UPDATE noderev SET txnid = :new_txnid WHERE txnid = :old_txnid;

---STATEMENT NODEREV_DELAZIFY
UPDATE noderev SET opcode = 'B' WHERE id = :id;

---STATEMENT NODEREV_GET
SELECT * FROM noderev WHERE id = :id;

---STATEMENT NODEREV_COMMIT
UPDATE noderev SET state = 'P' WHERE txnid = :txnid;

---STATEMENT NODEREV_CLEANUP
DELETE FROM noderev WHERE txnid = :txnid;

---STATEMENT NODEREV_FIND_BY_NAME
SELECT * FROM noderev
WHERE parent = :parent AND name = :name
      AND txnid <= :txnid AND state = 'P'
ORDER BY txnid DESC LIMIT 1;

---STATEMENT NODEREV_FIND_TRANSIENT_BY_NAME
SELECT * FROM noderev
WHERE parent = :parent AND name = :name
      AND txnid <= :txnid AND state = 'T'
ORDER BY txnid DESC LIMIT 1;

---STATEMENT NODEREV_LIST_DIRECTORY
SELECT * FROM noderev
  JOIN (SELECT name, MAX(txnid) AS txnid FROM noderev
        WHERE txnid <= :txnid AND state = 'P') AS filter
    ON noderev.name = filter.name AND noderev.txnid = filter.txnid
WHERE parent = :parent AND opcode <> 'D'
ORDER BY name ASC;
