/* gitdb.c --- manage the mapping db of the git filesystem
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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <apr_general.h>
#include <apr_pools.h>

#include "svn_fs.h"
#include "svn_version.h"
#include "svn_pools.h"
#include "svn_dirent_uri.h"

#include "svn_private_config.h"

#include "private/svn_fs_util.h"

#include "../libsvn_fs/fs-loader.h"
#include "fs_git.h"

#include "fsgit-queries.h"

#define SVN_FS_GIT__VERSION 1
FSGIT_QUERIES_SQL_DECLARE_STATEMENTS(statements);


svn_error_t *
svn_fs_git__db_youngest_rev(svn_revnum_t *youngest_p,
                            svn_fs_t *fs,
                            apr_pool_t *pool)
{
  svn_fs_git_fs_t *fgf = fs->fsap_data;
  svn_sqlite__stmt_t *stmt;

  SVN_ERR(svn_sqlite__get_statement(&stmt, fgf->sdb, STMT_SELECT_HEADREV));
  SVN_ERR(svn_sqlite__step_row(stmt));

  *youngest_p = svn_sqlite__column_revnum(stmt, 0);
  SVN_ERR(svn_sqlite__reset(stmt));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_git__db_ensure_commit(svn_fs_t *fs,
                             git_oid *oid,
                             svn_revnum_t *latest_rev,
                             git_reference *ref)
{
  svn_fs_git_fs_t *fgf = fs->fsap_data;
  svn_sqlite__stmt_t *stmt;
  svn_boolean_t got_row;
  svn_revnum_t new_rev;

  SVN_ERR(svn_sqlite__get_statement(&stmt, fgf->sdb, STMT_SELECT_REV_BY_COMMITID));
  SVN_ERR(svn_sqlite__bind_blob(stmt, 1, oid, sizeof(*oid)));
  SVN_ERR(svn_sqlite__step(&got_row, stmt));
  SVN_ERR(svn_sqlite__reset(stmt));

  if (got_row)
    return SVN_NO_ERROR;

  new_rev = *latest_rev + 1;
  SVN_ERR(svn_sqlite__get_statement(&stmt, fgf->sdb, STMT_INSERT_COMMIT));
  SVN_ERR(svn_sqlite__bind_revnum(stmt, 1, new_rev));
  SVN_ERR(svn_sqlite__bind_blob(stmt, 2, oid, sizeof(*oid)));
  SVN_ERR(svn_sqlite__bind_text(stmt, 3, "trunk"));
  SVN_ERR(svn_sqlite__update(NULL, stmt));

  *latest_rev = new_rev;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_git__db_open(svn_fs_t *fs,
                    apr_pool_t *scratch_pool)
{
  svn_fs_git_fs_t *fgf = fs->fsap_data;
  svn_sqlite__stmt_t *stmt;
  const char *db_path = svn_dirent_join(fs->path, ".svn-git-fs.db",
                                        scratch_pool);

  SVN_ERR(svn_sqlite__open(&fgf->sdb, db_path, svn_sqlite__mode_readwrite,
                           statements, 0, NULL, 0,
                           fs->pool, scratch_pool));

  SVN_ERR(svn_sqlite__get_statement(&stmt, fgf->sdb, STMT_SELECT_UUID));
  SVN_ERR(svn_sqlite__step_row(stmt));

  fs->uuid = svn_sqlite__column_text(stmt, 0, fs->pool);
  SVN_ERR(svn_sqlite__reset(stmt));

  return SVN_NO_ERROR;
}

static svn_error_t *
create_schema(svn_fs_t *fs,
              apr_pool_t *scratch_pool)
{
  svn_fs_git_fs_t *fgf = fs->fsap_data;
  svn_sqlite__stmt_t *stmt;
  const char *uuid;

  SVN_ERR(svn_sqlite__exec_statements(fgf->sdb,
                                      STMT_CREATE_SCHEMA));

  uuid = svn_uuid_generate(fs->pool);

  SVN_ERR(svn_sqlite__get_statement(&stmt, fgf->sdb, STMT_INSERT_UUID));
  SVN_ERR(svn_sqlite__bind_text(stmt, 1, uuid));
  SVN_ERR(svn_sqlite__update(NULL, stmt));

  fs->uuid = uuid;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_git__db_create(svn_fs_t *fs,
                      apr_pool_t *scratch_pool)
{
  svn_fs_git_fs_t *fgf = fs->fsap_data;
  const char *db_path = svn_dirent_join(fs->path, ".svn-git-fs.db",
                                        scratch_pool);

  SVN_ERR(svn_sqlite__open(&fgf->sdb, db_path, svn_sqlite__mode_rwcreate,
                           statements, 0, NULL, 0,
                           scratch_pool, scratch_pool));

  SVN_SQLITE__WITH_LOCK(create_schema(fs, scratch_pool), fgf->sdb);

  return SVN_NO_ERROR;
}
