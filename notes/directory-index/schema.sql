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

CREATE TABLE revision (
  version integer NOT NULL PRIMARY KEY,
  created timestamp NOT NULL,
  author  varchar NULL,
  log     varchar NULL
);

CREATE TABLE dirindex (
  -- unique id of this node revision, used for
  -- predecessor/successor links
  rowid   integer PRIMARY KEY,

  -- absolute (repository) path
  abspath varchar NOT NULL,

  -- revision number
  version integer NOT NULL REFERENCES revision(version),

  -- node deletion flag
  deleted boolean NOT NULL,

  -- node kind (0 = dir, 1 = file, etc.)
  kind    integer NOT NULL,

  -- predecessor link
  origin  integer NULL REFERENCES dirindex(rowid),

  -- the predecessor is a copy source
  copied  boolean NOT NULL,

  -- the index entry is the result of an implicit subtree operation
  subtree boolean NOT NULL
);
CREATE UNIQUE INDEX dirindex_versioned_tree ON dirindex(abspath ASC, version DESC);
CREATE INDEX dirindex_successor_list ON dirindex(origin);

-- repository root
INSERT INTO revision (version, created, author, log)
VALUES (0, 'EPOCH', NULL, NULL);
INSERT INTO dirindex (abspath, version, deleted, kind, origin, copied, implied)
VALUES ('/', 0, 0, 0, NULL, 0, 0);


-- lookup PATH@REVISION

SELECT * FROM dirindex
WHERE
  abspath = '' -- $PATH
  AND version <= 0 -- $REVISION
ORDER BY abspath ASC, version DESC
LIMIT 1;  -- then check .deleted

-- single-revision tree for REVISION

SELECT dirindex.* FROM dirindex
  JOIN (SELECT abspath, MAX(version) AS maxver FROM dirindex
        WHERE version <= 0 -- $REVISION
        GROUP BY abspath)
      AS filtered
    ON dirindex.abspath == filtered.abspath
       AND dirindex.version == filtered.maxver
WHERE NOT dirindex.deleted
ORDER BY dirindex.abspath ASC;
