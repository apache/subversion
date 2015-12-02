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
svn_fs_git__db_fetch_oid(svn_boolean_t *found,
                         const git_oid **oid,
                         const char **path,
                         svn_fs_t *fs,
                         svn_revnum_t revnum,
                         apr_pool_t *result_pool,
                         apr_pool_t *scratch_pool)
{
  svn_fs_git_fs_t *fgf = fs->fsap_data;
  svn_sqlite__stmt_t *stmt;
  svn_boolean_t got_row;

  SVN_ERR(svn_sqlite__get_statement(&stmt, fgf->sdb,
                                    STMT_SELECT_COMMIT_BY_REV));
  SVN_ERR(svn_sqlite__bind_revnum(stmt, 1, revnum));
  SVN_ERR(svn_sqlite__step(&got_row, stmt));

  if (got_row)
    {
      if (found)
        *found = (revnum == svn_sqlite__column_revnum(stmt, 2));
      if (oid)
        {
          apr_size_t len;
          *oid = svn_sqlite__column_blob(stmt, 0, &len, result_pool);
          if (len != sizeof(**oid))
            *oid = NULL;
        }
      if (path)
        *path = svn_sqlite__column_text(stmt, 1, result_pool);
    }
  else
    {
      if (found)
        *found = FALSE;
      if (oid)
        *oid = NULL;
      if (path)
        *path = NULL;
    }
  SVN_ERR(svn_sqlite__reset(stmt));
  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_git__db_fetch_rev(svn_revnum_t *revnum,
                         const char **path,
                         svn_fs_t *fs,
                         const git_oid *oid,
                         apr_pool_t *result_pool,
                         apr_pool_t *scratch_pool)
{
  svn_fs_git_fs_t *fgf = fs->fsap_data;
  svn_sqlite__stmt_t *stmt;
  svn_boolean_t got_row;

  SVN_ERR(svn_sqlite__get_statement(&stmt, fgf->sdb,
                                    STMT_SELECT_REV_BY_COMMITID));
  SVN_ERR(svn_sqlite__bind_blob(stmt, 1, oid, sizeof(*oid)));
  SVN_ERR(svn_sqlite__step(&got_row, stmt));

  if (got_row)
    {
      if (revnum)
        *revnum = svn_sqlite__column_revnum(stmt, 0);
      if (path)
        *path = svn_sqlite__column_text(stmt, 1, result_pool);
    }
  else
    {
      if (revnum)
        *revnum = SVN_INVALID_REVNUM;
      if (path)
        *path = NULL;
    }
  SVN_ERR(svn_sqlite__reset(stmt));
  return SVN_NO_ERROR;
}



static svn_error_t *
db_fetch_checksum(svn_checksum_t **checksum,
                  svn_fs_t *fs,
                  const git_oid *oid,
                  int idx,
                  apr_pool_t *result_pool,
                  apr_pool_t *scratch_pool)
{
  svn_fs_git_fs_t *fgf = fs->fsap_data;
  svn_sqlite__stmt_t *stmt;
  svn_boolean_t got_row;
  svn_stream_t *stream;
  svn_checksum_t *sha1_checksum, *md5_checksum;

  SVN_ERR(svn_sqlite__get_statement(&stmt, fgf->sdb,
                                    STMT_SELECT_CHECKSUM));
  SVN_ERR(svn_sqlite__bind_blob(stmt, 1, oid, sizeof(*oid)));
  SVN_ERR(svn_sqlite__step(&got_row, stmt));

  if (got_row)
    SVN_ERR(svn_sqlite__column_checksum(checksum, stmt, idx,
                                        result_pool));
  else
    *checksum = NULL;
  SVN_ERR(svn_sqlite__reset(stmt));

  if (got_row)
    return SVN_NO_ERROR;

  SVN_ERR(svn_fs_git__get_blob_stream(&stream, fs, oid, scratch_pool));

  stream = svn_stream_checksummed2(stream, &sha1_checksum, NULL,
                                   svn_checksum_sha1, TRUE, scratch_pool);
  stream = svn_stream_checksummed2(stream, &md5_checksum, NULL,
                                   svn_checksum_md5, TRUE, scratch_pool);

  SVN_ERR(svn_stream_copy3(stream, svn_stream_empty(scratch_pool),
                           NULL, NULL, scratch_pool));


  SVN_ERR(svn_sqlite__get_statement(&stmt, fgf->sdb,
                                    STMT_INSERT_CHECKSUM));
  SVN_ERR(svn_sqlite__bind_blob(stmt, 1, oid, sizeof(*oid)));
  SVN_ERR(svn_sqlite__bind_checksum(stmt, 2, md5_checksum, scratch_pool));
  SVN_ERR(svn_sqlite__bind_checksum(stmt, 3, sha1_checksum, scratch_pool));
  SVN_ERR(svn_sqlite__update(NULL, stmt));

  if (idx == 1)
    *checksum = svn_checksum_dup(md5_checksum, result_pool);
  else
    *checksum = svn_checksum_dup(sha1_checksum, result_pool);
  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_git__db_fetch_checksum(svn_checksum_t **checksum,
                              svn_fs_t *fs,
                              const git_oid *oid,
                              svn_checksum_kind_t kind,
                              apr_pool_t *result_pool,
                              apr_pool_t *scratch_pool)
{
  svn_fs_git_fs_t *fgf = fs->fsap_data;
  int idx;

  if (kind == svn_checksum_md5)
    idx = 1;
  else if (kind == svn_checksum_sha1)
    idx = 2;
  else
    {
      *checksum = NULL;
      return SVN_NO_ERROR;
    }

  SVN_SQLITE__WITH_LOCK(db_fetch_checksum(checksum, fs, oid, idx,
                                          result_pool, scratch_pool),
                        fgf->sdb);

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

svn_error_t *
svn_fs_git__db_set_uuid(svn_fs_t *fs,
                        const char *uuid,
                        apr_pool_t *scratch_pool)
{
  svn_fs_git_fs_t *fgf = fs->fsap_data;
  svn_sqlite__stmt_t *stmt;

  SVN_ERR(svn_sqlite__get_statement(&stmt, fgf->sdb, STMT_INSERT_UUID));
  SVN_ERR(svn_sqlite__bind_text(stmt, 1, uuid));
  SVN_ERR(svn_sqlite__update(NULL, stmt));

  fs->uuid = apr_pstrdup(fs->pool, uuid);
  return SVN_NO_ERROR;
}


static svn_error_t *
create_schema(svn_fs_t *fs,
              apr_pool_t *scratch_pool)
{
  svn_fs_git_fs_t *fgf = fs->fsap_data;

  SVN_ERR(svn_sqlite__exec_statements(fgf->sdb,
                                      STMT_CREATE_SCHEMA));

  SVN_ERR(svn_fs_git__db_set_uuid(fs, svn_uuid_generate(scratch_pool),
                                  scratch_pool));

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
