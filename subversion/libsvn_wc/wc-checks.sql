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

CREATE TRIGGER no_repository_updates BEFORE UPDATE ON REPOSITORY
BEGIN
  SELECT RAISE(FAIL, 'Updates to REPOSITORY are not allowed.');
END;

/* ------------------------------------------------------------------------- */

/* Verify: on every NODES row: parent_relpath is parent of local_relpath */
CREATE TRIGGER validation_01 BEFORE INSERT ON NODES
WHEN NOT ((new.local_relpath = '' AND new.parent_relpath IS NULL)
          OR (relpath_depth(new.local_relpath)
              = relpath_depth(new.parent_relpath) + 1))
BEGIN
  SELECT RAISE(FAIL, 'WC DB validity check 01 failed');
END;

/* Verify: on every NODES row: its op-depth <= its own depth */
CREATE TRIGGER validation_02 BEFORE INSERT ON NODES
WHEN NOT new.op_depth <= relpath_depth(new.local_relpath)
BEGIN
  SELECT RAISE(FAIL, 'WC DB validity check 02 failed');
END;

/* Verify: on every NODES row: it is an op-root or its op-root row exists. */
CREATE TRIGGER validation_03 BEFORE INSERT ON NODES
WHEN NOT (
    (new.op_depth = relpath_depth(new.local_relpath))
    OR
    ((SELECT COUNT(*) FROM nodes
      WHERE wc_id = new.wc_id AND op_depth = new.op_depth
        AND local_relpath = new.parent_relpath) == 1)
  )
BEGIN
  SELECT RAISE(FAIL, 'WC DB validity check 03 failed');
END;

