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
  txnid     integer NOT NULL REFERENCES txn(id),

  -- the revision that this transaction represents; as long as this is
  -- null, the transaction has not yet been committed.
  revision  integer NULL,

  -- creation date, independent of the svn:date property
  created   timestamp NOT NULL,

  -- transaction author, independent of the svn:author property; may
  -- be null if the repository allows anonymous modifications
  author    varchar NULL

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
  nodeid    integer NOT NULL REFERENCES branch(id),

  -- the source branch from which this branch was forked
  origin    integer NULL REFERENCES branch(id),

  -- the transaction in which the branch was created
  txnid     integer NOT NULL REFERENCES txn(id)

  -- sanity check: ye can't be yer own daddy
  CONSTRAINT genetic_diversity CHECK (id <> origin)
);

CREATE INDEX branch_node_idx ON branch(nodeid);
CREATE INDEX branch_successor_idx ON branch(origin);


-- Node revisions -- DAG of versioned node changes
CREATE TABLE noderev (
  -- node revision identifier
  id        integer NOT NULL PRIMARY KEY,

  -- the node identifier; a new node will get the ID of its initial
  -- branch
  nodeid    integer NOT NULL REFERENCES branch(id),

  -- the node kind; immutable within the node
  -- D = directory, F = file, L = link
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
  -- B = branched (requires kind=D + added + origin <> null)
  -- L = lazy branch, indicates that child lookup should be performed
  --     on the origin (same constraints as for opcode=B)
  opcode    character(1) NOT NULL,

  -- sanity check: only directories can be lazy
  CONSTRAINT minimal_workload CHECK (
    ((opcode = 'B' OR opcode = 'L') AND kind = 'D' AND origin IS NOT NULL)
    OR opcode <> 'B' AND opcode <> 'L'),

  -- sanity check: ye can't be yer own daddy
  CONSTRAINT genetic_diversity CHECK (id <> origin),

  -- sanity check: ye can't be yer own stepdaddy, either
  CONSTRAINT escher_avoidance CHECK (parent <> branch)

  -- other attributes:
     -- versioned properties
     -- contents reference
);

CREATE UNIQUE INDEX noderev_tree_idx ON noderev(parent, name, txnid);
CREATE INDEX nodefev_node_idx ON noderev(nodeid);
CREATE INDEX noderev_successor_idx ON noderev(origin);


-- Root directory

INSERT INTO txn (id, txnid, revision, created) VALUES (0, 0, 0, 'EPOCH');
INSERT INTO branch (id, nodeid, txnid) VALUES (0, 0, 0);
INSERT INTO noderev (id, nodeid, kind, branch, name, txnid, opcode)
  VALUES (0, 0, 'D', 0, '', 0, 'A');


--#  ---STATEMENT INSERT_TXN
--#  INSERT INTO txn (revnum, created, author)
--#    VALUES (:revnum, :created, :author);
--#  
--#  ---STATEMENT GET_TXN
--#  SELECT * FROM txn WHERE id = :id;
--#  
--#  ---STATEMENT FIND_TXN_BY_REVNUM
--#  SELECT * FROM txn WHERE revnum = :revnum;
--#  
--#  ---STATEMENT FIND_NEWEST_REVISION_TXN
--#  SELECT * FROM txn WHERE revnum IS NOT NULL ORDER BY revnum DESC LIMIT 1;
--#  
--#  ---STATEMENT SET_TXN_REVNUM
--#  UPDATE txn SET revnum = :revnum WHERE id = :id;
--#  
--#  ---STATEMENT INSERT_NODE
--#  INSERT INTO node (kind, txnid) VALUES (:kind, :txnid);
--#  
--#  ---STATEMENT GET_NODE
--#  SELECT * FROM node WHERE id = :id;
--#  
--#  ---STATEMENT INSERT_BRANCH
--#  INSERT INTO branch (origin, node, txnid)
--#    VALUES (:origin, :node, :txnid);
--#  
--#  ---STATEMENT GET_BRANCH
--#  SELECT * FROM branch WHERE id = :id;
--#  
--#  ---STATEMENT INSERT_NODEREV
--#  INSERT INTO noderev (origin, parent, branch,
--#                       iname, oname, txnid, change)
--#    VALUES (:origin, :parent, :branch,
--#            :iname, :oname, :txnid, :change);
--#  
--#  ---STATEMENT FIND_NODEREV_BY_NAME_FOR_TXN
--#  SELECT
--#    noderev.*,
--#    node.id AS node,
--#    node.kind AS kind
--#  FROM
--#    noderev JOIN branch ON noderev.branch = branch.id
--#    JOIN node ON branch.node = node.id
--#  WHERE
--#    parent = :parent AND iname = ":iname"
--#    AND noderev.txnid <= :txnid
--#  ORDER BY txnid DESC
--#  LIMIT 1;
--#  
--#  ---STATEMENT LIST_DIRECTORY_FOR_TXN
--#  SELECT
--#    noderev.*,
--#    node.id AS node,
--#    node.kind AS kind,
--#  FROM
--#    noderev JOIN branch ON noderev.branch = branch.id
--#    JOIN node ON branch.node = node.id
--#    JOIN (SELECT iname, MAX(txnid) AS maxtxn FROM noderev
--#          WHERE txnid <= :txnid) AS filter
--#      ON noderev.iname = filter.iname AND txnid = filter.maxtxn
--#  WHERE
--#    noderev.parent = :parent
--#    AND noderev.change <> 'D'
--#  ORDER BY iname ASC;
