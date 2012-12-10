/**
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
 *
 * This header is parsed by transform-sql.py to allow SQLite
 * statements to refer to string values by symbolic names.
 */

#include "svn_types.h"
#include "wc_db.h"
#include "private/svn_token.h"

static const svn_token_map_t kind_map[] = {
  { "file", svn_kind_file }, /* MAP_FILE */
  { "dir", svn_kind_dir },
  { "symlink", svn_kind_symlink },
  { "unknown", svn_kind_unknown },
  { NULL }
};

/* Note: we only decode presence values from the database. These are a
   subset of all the status values. */
static const svn_token_map_t presence_map[] = {
  { "normal", svn_wc__db_status_normal },
  { "server-excluded", svn_wc__db_status_server_excluded },
  { "excluded", svn_wc__db_status_excluded },
  { "not-present", svn_wc__db_status_not_present },
  { "incomplete", svn_wc__db_status_incomplete },
  { "base-deleted", svn_wc__db_status_base_deleted },
  { NULL }
};


