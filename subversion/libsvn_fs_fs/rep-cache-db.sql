/* rep-cache-db.sql -- schema for use in rep-caching
 *   This is intended for use with SQLite 3
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

-- STMT_CREATE_SCHEMA
pragma auto_vacuum = 1;

/* A table mapping representation hashes to locations in a rev file. */
create table rep_cache (hash text not null primary key,
                        revision integer not null,
                        offset integer not null,
                        size integer not null,
                        expanded_size integer not null);

pragma user_version = 1;


-- STMT_GET_REP
select revision, offset, size, expanded_size
from rep_cache
where hash = ?1;


-- STMT_SET_REP
insert into rep_cache (hash, revision, offset, size, expanded_size)
values (?1, ?2, ?3, ?4, ?5);
