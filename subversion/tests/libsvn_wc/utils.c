/* utils.c --- wc/client test utilities
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

#include "svn_error.h"
#include "svn_client.h"
#include "svn_cmdline.h"
#include "svn_pools.h"

#include "utils.h"

#include "../svn_test_fs.h"

#include "../../libsvn_wc/wc.h"
#include "../../libsvn_wc/wc-queries.h"
#define SVN_WC__I_AM_WC_DB
#include "../../libsvn_wc/wc_db_private.h"
#include "../../libsvn_wc/token-map.h"
svn_error_t *
svn_test__create_client_ctx(svn_client_ctx_t **ctx,
                            svn_test__sandbox_t *sbox,
                            apr_pool_t *result_pool)
{
  SVN_ERR(svn_client_create_context2(ctx, NULL, result_pool));

  SVN_ERR(svn_test__init_auth_baton(&(*ctx)->auth_baton,
                                    result_pool));

  if (sbox)
    (*ctx)->wc_ctx = sbox->wc_ctx;

  return SVN_NO_ERROR;
}

/* Create an empty repository and WC for the test TEST_NAME.  Set *REPOS_URL
 * to the URL of the new repository, *REPOS_DIR to its local path and
 * *WC_ABSPATH to the root path of the
 * new WC.
 *
 * Create the repository and WC in subdirectories called
 * REPOSITORIES_WORK_DIR/TEST_NAME and WCS_WORK_DIR/TEST_NAME respectively,
 * within the current working directory.
 *
 * Register the repo and WC to be cleaned up when the test suite exits. */
static svn_error_t *
create_repos_and_wc(const char **repos_url,
                    const char **repos_dir,
                    const char **wc_abspath,
                    const char *test_name,
                    const svn_test_opts_t *opts,
                    apr_pool_t *pool)
{
  const char *repos_path = svn_relpath_join(REPOSITORIES_WORK_DIR, test_name,
                                            pool);
  const char *wc_path = svn_relpath_join(WCS_WORK_DIR, test_name, pool);

  /* Remove the repo and WC dirs if they already exist, to ensure the test
   * will run even if a previous failed attempt was not cleaned up. */
  SVN_ERR(svn_io_remove_dir2(repos_path, TRUE, NULL, NULL, pool));
  SVN_ERR(svn_io_remove_dir2(wc_path, TRUE, NULL, NULL, pool));

  /* Create the parent dirs of the repo and WC if necessary. */
  SVN_ERR(svn_io_make_dir_recursively(REPOSITORIES_WORK_DIR, pool));
  SVN_ERR(svn_io_make_dir_recursively(WCS_WORK_DIR, pool));

  /* Create a repos. Register it for clean-up. Set *REPOS_URL to its path. */
  {
    /* Use a subpool to create the repository and then destroy the subpool
       so the repository's underlying filesystem is closed.  If opts->fs_type
       is BDB this prevents any attempt to open a second environment handle
       within the same process when we checkout the WC below.  BDB 4.4+ allows
       only a single environment handle to be open per process. */
    apr_pool_t *subpool = svn_pool_create(pool);

    SVN_ERR(svn_test__create_repos2(NULL, repos_url, repos_dir, repos_path,
                                    opts, pool, subpool));
    svn_pool_destroy(subpool);
  }

  /* Create a WC. Set *WC_ABSPATH to its path. */
  {
    apr_pool_t *subpool = svn_pool_create(pool); /* To cleanup CTX */
    svn_client_ctx_t *ctx;
    svn_opt_revision_t head_rev = { svn_opt_revision_head, {0} };

    SVN_ERR(svn_test__create_client_ctx(&ctx, NULL, subpool));
    SVN_ERR(svn_dirent_get_absolute(wc_abspath, wc_path, pool));
    SVN_ERR(svn_client_checkout3(NULL, *repos_url, *wc_abspath,
                                 &head_rev, &head_rev, svn_depth_infinity,
                                 FALSE /* ignore_externals */,
                                 FALSE /* allow_unver_obstructions */,
                                 ctx, subpool));
    svn_pool_destroy(subpool);
  }

  /* Register this WC for cleanup. */
  svn_test_add_dir_cleanup(*wc_abspath);

  return SVN_NO_ERROR;
}

WC_QUERIES_SQL_DECLARE_STATEMENTS(statements);

svn_error_t *
svn_test__create_fake_wc(const char *wc_abspath,
                         const char *extra_statements,
                         const svn_test__nodes_data_t nodes[],
                         const svn_test__actual_data_t actuals[],

                         apr_pool_t *scratch_pool)
{
  const char *dotsvn_abspath = svn_dirent_join(wc_abspath, ".svn",
                                               scratch_pool);
  svn_sqlite__db_t *sdb;
  const char **my_statements;
  int i;
  svn_sqlite__stmt_t *stmt;
  const apr_int64_t wc_id = 1;

  /* Allocate MY_STATEMENTS in RESULT_POOL because the SDB will continue to
   * refer to it over its lifetime. */
  my_statements = apr_palloc(scratch_pool, 7 * sizeof(const char *));
  my_statements[0] = statements[STMT_CREATE_SCHEMA];
  my_statements[1] = statements[STMT_INSTALL_SCHEMA_STATISTICS];
  my_statements[2] = extra_statements;
  my_statements[3] = NULL;

  /* Create fake-wc/SUBDIR/.svn/ for placing the metadata. */
  SVN_ERR(svn_io_make_dir_recursively(dotsvn_abspath, scratch_pool));
  SVN_ERR(svn_wc__db_util_open_db(&sdb, wc_abspath, "wc.db",
                                  svn_sqlite__mode_rwcreate,
                                  FALSE /* exclusive */, 0 /* timeout */,
                                  my_statements,
                                  scratch_pool, scratch_pool));
  for (i = 0; my_statements[i] != NULL; i++)
    SVN_ERR(svn_sqlite__exec_statements(sdb, /* my_statements[] */ i));

  SVN_ERR(svn_sqlite__close(sdb));

  if (!nodes && !actuals)
    return SVN_NO_ERROR;

  /* Re-open with normal set of statements */
  SVN_ERR(svn_wc__db_util_open_db(&sdb, wc_abspath, "wc.db",
                                  svn_sqlite__mode_readwrite,
                                  FALSE /* exclusive */, 0 /* timeout */,
                                  statements,
                                  scratch_pool, scratch_pool));

  if (nodes)
    {
      SVN_ERR(svn_sqlite__get_statement(&stmt, sdb,
                                        STMT_INSERT_NODE));

      for (i = 0; nodes[i].local_relpath; i++)
        {
          SVN_ERR(svn_sqlite__bindf(stmt, "isdsnnns",
                                wc_id,
                                nodes[i].local_relpath,
                                nodes[i].op_depth,
                                nodes[i].local_relpath[0]
                                  ? svn_relpath_dirname(nodes[i].local_relpath,
                                                        scratch_pool)
                                  : NULL,
                                nodes[i].presence));

          if (nodes[i].repos_relpath)
            {
                SVN_ERR(svn_sqlite__bind_int64(stmt, 5, nodes[i].repos_id));
                SVN_ERR(svn_sqlite__bind_text(stmt, 6, nodes[i].repos_relpath));
                SVN_ERR(svn_sqlite__bind_revnum(stmt, 7, nodes[i].revision));
            }

          if (nodes[i].depth)
            SVN_ERR(svn_sqlite__bind_text(stmt, 9, nodes[i].depth));

          if (nodes[i].kind != 0)
            SVN_ERR(svn_sqlite__bind_token(stmt, 10, kind_map, nodes[i].kind));

          if (nodes[i].last_author || nodes[i].last_date)
            {
              SVN_ERR(svn_sqlite__bind_revnum(stmt, 11, nodes[i].last_revision));
              SVN_ERR(svn_sqlite__bind_int64(stmt, 12, nodes[i].last_date));
              SVN_ERR(svn_sqlite__bind_text(stmt, 13, nodes[i].last_author));
            }

          if (nodes[i].checksum)
            SVN_ERR(svn_sqlite__bind_text(stmt, 14, nodes[i].checksum));

          if (nodes[i].properties)
            SVN_ERR(svn_sqlite__bind_text(stmt, 15, nodes[i].properties));

          if (nodes[i].recorded_size || nodes[i].recorded_time)
            {
              SVN_ERR(svn_sqlite__bind_int64(stmt, 16, nodes[i].recorded_size));
              SVN_ERR(svn_sqlite__bind_int64(stmt, 17, nodes[i].recorded_time));
            }

          /* 18 is DAV cache */

          if (nodes[i].symlink_target)
            SVN_ERR(svn_sqlite__bind_text(stmt, 19, nodes[i].symlink_target));

          if (nodes[i].file_external)
            SVN_ERR(svn_sqlite__bind_int(stmt, 20, 1));

          if (nodes[i].moved_to)
            SVN_ERR(svn_sqlite__bind_text(stmt, 21, nodes[i].moved_to));

          if (nodes[i].moved_here)
            SVN_ERR(svn_sqlite__bind_int(stmt, 22, 1));

          if (nodes[i].inherited_props)
            SVN_ERR(svn_sqlite__bind_text(stmt, 23, nodes[i].inherited_props));

          SVN_ERR(svn_sqlite__step_done(stmt));
        }
    }

  if (actuals)
    {
      SVN_ERR(svn_sqlite__get_statement(&stmt, sdb,
                                        STMT_INSERT_ACTUAL_NODE));

      for (i = 0; actuals[i].local_relpath; i++)
        {
          SVN_ERR(svn_sqlite__bindf(stmt, "isssss",
                                wc_id,
                                actuals[i].local_relpath,
                                actuals[i].local_relpath[0]
                                  ? svn_relpath_dirname(actuals[i].local_relpath,
                                                        scratch_pool)
                                  : NULL,
                                actuals[i].properties,
                                actuals[i].changelist,
                                actuals[i].conflict_data));

          SVN_ERR(svn_sqlite__step_done(stmt));
        }
    }

  SVN_ERR(svn_sqlite__close(sdb));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_test__sandbox_create(svn_test__sandbox_t *sandbox,
                         const char *test_name,
                         const svn_test_opts_t *opts,
                         apr_pool_t *pool)
{
  sandbox->pool = pool;
  SVN_ERR(create_repos_and_wc(&sandbox->repos_url, &sandbox->repos_dir,
                              &sandbox->wc_abspath,
                              test_name, opts, pool));
  SVN_ERR(svn_wc_context_create(&sandbox->wc_ctx, NULL, pool, pool));
  return SVN_NO_ERROR;
}

svn_error_t *
sbox_file_write(svn_test__sandbox_t *b, const char *path, const char *text)
{
  apr_file_t *f;

  SVN_ERR(svn_io_file_open(&f, sbox_wc_path(b, path),
                           (APR_WRITE | APR_CREATE | APR_TRUNCATE),
                           APR_OS_DEFAULT,
                           b->pool));

  SVN_ERR(svn_io_file_write_full(f, text, strlen(text), NULL, b->pool));

  SVN_ERR(svn_io_file_close(f, b->pool));

  return SVN_NO_ERROR;
}

svn_error_t *
sbox_wc_add(svn_test__sandbox_t *b, const char *path)
{
  const char *parent_abspath;

  path = sbox_wc_path(b, path);
  parent_abspath = svn_dirent_dirname(path, b->pool);
  SVN_ERR(svn_wc__acquire_write_lock(NULL, b->wc_ctx, parent_abspath, FALSE,
                                     b->pool, b->pool));
  SVN_ERR(svn_wc_add_from_disk3(b->wc_ctx, path, NULL /*props*/,
                                FALSE /* skip checks */,
                                NULL, NULL, b->pool));
  SVN_ERR(svn_wc__release_write_lock(b->wc_ctx, parent_abspath, b->pool));
  return SVN_NO_ERROR;
}

svn_error_t *
sbox_disk_mkdir(svn_test__sandbox_t *b, const char *path)
{
  path = sbox_wc_path(b, path);
  SVN_ERR(svn_io_dir_make(path, APR_FPROT_OS_DEFAULT, b->pool));
  return SVN_NO_ERROR;
}

svn_error_t *
sbox_wc_mkdir(svn_test__sandbox_t *b, const char *path)
{
  SVN_ERR(sbox_disk_mkdir(b, path));
  SVN_ERR(sbox_wc_add(b, path));
  return SVN_NO_ERROR;
}

#if 0 /* not used */
/* Copy the file or directory tree FROM_PATH to TO_PATH which must not exist
 * beforehand. */
svn_error_t *
sbox_disk_copy(svn_test__sandbox_t *b, const char *from_path, const char *to_path)
{
  const char *to_dir, *to_name;

  from_path = sbox_wc_path(b, from_path);
  to_path = sbox_wc_path(b, to_path);
  svn_dirent_split(&to_dir, &to_name, to_path, b->pool);
  return svn_io_copy_dir_recursively(from_path, to_dir, to_name,
                                     FALSE, NULL, NULL, b->pool);
}
#endif

svn_error_t *
sbox_wc_copy(svn_test__sandbox_t *b, const char *from_path, const char *to_path)
{
  const char *parent_abspath;

  from_path = sbox_wc_path(b, from_path);
  to_path = sbox_wc_path(b, to_path);
  parent_abspath = svn_dirent_dirname(to_path, b->pool);
  SVN_ERR(svn_wc__acquire_write_lock(NULL, b->wc_ctx, parent_abspath, FALSE,
                                     b->pool, b->pool));
  SVN_ERR(svn_wc_copy3(b->wc_ctx, from_path, to_path, FALSE,
                       NULL, NULL, NULL, NULL, b->pool));
  SVN_ERR(svn_wc__release_write_lock(b->wc_ctx, parent_abspath, b->pool));
  return SVN_NO_ERROR;
}

svn_error_t *
sbox_wc_copy_url(svn_test__sandbox_t *b, const char *from_url,
                 svn_revnum_t revision, const char *to_path)
{
  apr_pool_t *scratch_pool = b->pool;
  svn_client_ctx_t *ctx;
  svn_opt_revision_t rev = { svn_opt_revision_unspecified, {0} };
  svn_client_copy_source_t* src;
  apr_array_header_t *sources = apr_array_make(
                                        scratch_pool, 1,
                                        sizeof(svn_client_copy_source_t *));

  SVN_ERR(svn_test__create_client_ctx(&ctx, b, scratch_pool));

  if (SVN_IS_VALID_REVNUM(revision))
    {
      rev.kind = svn_opt_revision_number;
      rev.value.number = revision;
    }

  src = apr_pcalloc(scratch_pool, sizeof(*src));

  src->path = from_url;
  src->revision = &rev;
  src->peg_revision = &rev;

  APR_ARRAY_PUSH(sources, svn_client_copy_source_t *) = src;

  SVN_ERR(svn_client_copy7(sources, sbox_wc_path(b, to_path),
                           FALSE /* copy_as_child */,
                           FALSE /* make_parents */,
                           FALSE /* ignore_externals */,
                           FALSE /* metadata_only */,
                           FALSE, NULL /* pin_external */,
                           NULL /* revprops */,
                           NULL, NULL, /* commit_callback */
                           ctx, scratch_pool));

  ctx->wc_ctx = NULL;

  return SVN_NO_ERROR;
}


svn_error_t *
sbox_wc_revert(svn_test__sandbox_t *b, const char *path, svn_depth_t depth)
{
  const char *abspath = sbox_wc_path(b, path);
  const char *dir_abspath;
  const char *lock_root_abspath;

  if (strcmp(abspath, b->wc_abspath))
    dir_abspath = svn_dirent_dirname(abspath, b->pool);
  else
    dir_abspath = abspath;

  SVN_ERR(svn_wc__acquire_write_lock(&lock_root_abspath, b->wc_ctx,
                                     dir_abspath, FALSE /* lock_anchor */,
                                     b->pool, b->pool));
  SVN_ERR(svn_wc_revert5(b->wc_ctx, abspath, depth,
                         FALSE /* use_commit_times */,
                         NULL /* changelist_filter */,
                         FALSE /* clear_changelists */,
                         FALSE /* metadata_only */,
                         NULL, NULL, /* cancel baton + func */
                         NULL, NULL, /* notify baton + func */
                         b->pool));
  SVN_ERR(svn_wc__release_write_lock(b->wc_ctx, lock_root_abspath, b->pool));
  return SVN_NO_ERROR;
}

svn_error_t *
sbox_wc_delete(svn_test__sandbox_t *b, const char *path)
{
  const char *abspath = sbox_wc_path(b, path);
  const char *dir_abspath = svn_dirent_dirname(abspath, b->pool);
  const char *lock_root_abspath;

  SVN_ERR(svn_wc__acquire_write_lock(&lock_root_abspath, b->wc_ctx,
                                     dir_abspath, FALSE,
                                     b->pool, b->pool));
  SVN_ERR(svn_wc_delete4(b->wc_ctx, abspath, FALSE, TRUE,
                         NULL, NULL, /* cancel baton + func */
                         NULL, NULL, /* notify baton + func */
                         b->pool));
  SVN_ERR(svn_wc__release_write_lock(b->wc_ctx, lock_root_abspath, b->pool));
  return SVN_NO_ERROR;
}

svn_error_t *
sbox_wc_exclude(svn_test__sandbox_t *b, const char *path)
{
  const char *abspath = sbox_wc_path(b, path);
  const char *lock_root_abspath;

  SVN_ERR(svn_wc__acquire_write_lock(&lock_root_abspath, b->wc_ctx,
                                     abspath, TRUE,
                                     b->pool, b->pool));
  SVN_ERR(svn_wc_exclude(b->wc_ctx, abspath,
                         NULL, NULL, /* cancel baton + func */
                         NULL, NULL, /* notify baton + func */
                         b->pool));
  SVN_ERR(svn_wc__release_write_lock(b->wc_ctx, lock_root_abspath, b->pool));
  return SVN_NO_ERROR;
}

svn_error_t *
sbox_wc_commit_ex(svn_test__sandbox_t *b,
                  apr_array_header_t *targets,
                  svn_depth_t depth)
{
  svn_client_ctx_t *ctx;
  apr_pool_t *scratch_pool = svn_pool_create(b->pool);
  svn_error_t *err;

  SVN_ERR(svn_test__create_client_ctx(&ctx, b, scratch_pool));

  /* A successfull commit doesn't close the ra session, but leaves that
     to the caller. This leaves the BDB handle open, which might cause
     problems in further test code. (op_depth_tests.c's repo_wc_copy) */
  err = svn_client_commit6(targets, depth,
                           FALSE /* keep_locks */,
                           FALSE /* keep_changelist */,
                           TRUE  /* commit_as_operations */,
                           TRUE  /* include_file_externals */,
                           FALSE /* include_dir_externals */,
                           NULL, NULL, NULL, NULL, ctx, scratch_pool);

  svn_pool_destroy(scratch_pool);

  return svn_error_trace(err);
}

svn_error_t *
sbox_wc_commit(svn_test__sandbox_t *b, const char *path)
{
  apr_array_header_t *targets = apr_array_make(b->pool, 1,
                                               sizeof(const char *));

  APR_ARRAY_PUSH(targets, const char *) = sbox_wc_path(b, path);
  return sbox_wc_commit_ex(b, targets, svn_depth_infinity);
}

svn_error_t *
sbox_wc_update_depth(svn_test__sandbox_t *b,
                     const char *path,
                     svn_revnum_t revnum,
                     svn_depth_t depth,
                     svn_boolean_t sticky)
{
  svn_client_ctx_t *ctx;
  apr_array_header_t *result_revs;
  apr_array_header_t *paths = apr_array_make(b->pool, 1,
                                             sizeof(const char *));
  svn_opt_revision_t revision;

  if (SVN_IS_VALID_REVNUM(revnum))
    {
      revision.kind = svn_opt_revision_number;
      revision.value.number = revnum;
    }
  else
    {
      revision.kind = svn_opt_revision_head;
    }

  APR_ARRAY_PUSH(paths, const char *) = sbox_wc_path(b, path);
  SVN_ERR(svn_test__create_client_ctx(&ctx, b, b->pool));

  /* Note: Tree conflict resolver tests for libsvn_client depend on this
   * passing FALSE for adds_as_modifications so that tree conflicts are
   * created in case of add vs add upon update. */
  return svn_client_update4(&result_revs, paths, &revision, depth,
                            sticky, FALSE, FALSE, FALSE, FALSE,
                            ctx, b->pool);
}

svn_error_t *
sbox_wc_update(svn_test__sandbox_t *b, const char *path, svn_revnum_t revnum)
{
  SVN_ERR(sbox_wc_update_depth(b, path, revnum, svn_depth_unknown, FALSE));
  return SVN_NO_ERROR;
}

svn_error_t *
sbox_wc_switch(svn_test__sandbox_t *b,
               const char *path,
               const char *url,
               svn_depth_t depth)
{
  svn_client_ctx_t *ctx;
  svn_revnum_t result_rev;
  svn_opt_revision_t head_rev = { svn_opt_revision_head, {0} };

  url = apr_pstrcat(b->pool, b->repos_url, url, SVN_VA_NULL);
  SVN_ERR(svn_test__create_client_ctx(&ctx, b, b->pool));

  return svn_client_switch3(&result_rev, sbox_wc_path(b, path), url,
                            &head_rev, &head_rev, depth,
                            FALSE /* depth_is_sticky */,
                            TRUE /* ignore_externals */,
                            FALSE /* allow_unver_obstructions */,
                            TRUE /* ignore_ancestry */,
                            ctx, b->pool);
}

svn_error_t *
sbox_wc_resolved(svn_test__sandbox_t *b, const char *path)
{
  return sbox_wc_resolve(b, path, svn_depth_infinity,
                         svn_wc_conflict_choose_merged);
}

svn_error_t *
sbox_wc_resolve(svn_test__sandbox_t *b, const char *path, svn_depth_t depth,
                svn_wc_conflict_choice_t conflict_choice)
{
  const char *lock_abspath;
  svn_error_t *err;

  SVN_ERR(svn_wc__acquire_write_lock_for_resolve(&lock_abspath, b->wc_ctx,
                                                 sbox_wc_path(b, path),
                                                 b->pool, b->pool));
  err = svn_wc__resolve_conflicts(b->wc_ctx, sbox_wc_path(b, path),
                                  depth,
                                  TRUE /* resolve_text */,
                                  "" /* resolve_prop (ALL props) */,
                                  TRUE /* resolve_tree */,
                                  conflict_choice,
                                  NULL, NULL, /* conflict func */
                                  NULL, NULL, /* cancellation */
                                  NULL, NULL, /* notification */
                                  b->pool);

  err = svn_error_compose_create(err, svn_wc__release_write_lock(b->wc_ctx,
                                                                 lock_abspath,
                                                                 b->pool));
  return err;
}

svn_error_t *
sbox_wc_resolve_prop(svn_test__sandbox_t *b, const char *path,
                     const char *propname,
                     svn_wc_conflict_choice_t conflict_choice)
{
  const char *lock_abspath;
  svn_error_t *err;

  SVN_ERR(svn_wc__acquire_write_lock_for_resolve(&lock_abspath, b->wc_ctx,
                                                 sbox_wc_path(b, path),
                                                 b->pool, b->pool));
  err = svn_wc__resolve_conflicts(b->wc_ctx, sbox_wc_path(b, path),
                                  svn_depth_empty,
                                  FALSE,
                                  propname,
                                  FALSE,
                                  conflict_choice,
                                  NULL, NULL, /* conflict func */
                                  NULL, NULL, /* cancellation */
                                  NULL, NULL, /* notification */
                                  b->pool);

  err = svn_error_compose_create(err, svn_wc__release_write_lock(b->wc_ctx,
                                                                 lock_abspath,
                                                                 b->pool));
  return err;
}


svn_error_t *
sbox_wc_move(svn_test__sandbox_t *b, const char *src, const char *dst)
{
  svn_client_ctx_t *ctx;
  apr_array_header_t *paths = apr_array_make(b->pool, 1,
                                             sizeof(const char *));

  SVN_ERR(svn_test__create_client_ctx(&ctx, b, b->pool));

  APR_ARRAY_PUSH(paths, const char *) = sbox_wc_path(b, src);
  return svn_client_move7(paths, sbox_wc_path(b, dst),
                          FALSE /* move_as_child */,
                          FALSE /* make_parents */,
                          TRUE /* allow_mixed_revisions */,
                          FALSE /* metadata_only */,
                          NULL /* revprop_table */,
                          NULL, NULL, /* commit callback */
                          ctx, b->pool);
}

svn_error_t *
sbox_wc_propset(svn_test__sandbox_t *b,
           const char *name,
           const char *value,
           const char *path)
{
  svn_client_ctx_t *ctx;
  apr_array_header_t *paths = apr_array_make(b->pool, 1,
                                             sizeof(const char *));
  svn_string_t *pval = value ? svn_string_create(value, b->pool) : NULL;

  SVN_ERR(svn_test__create_client_ctx(&ctx, b, b->pool));

  APR_ARRAY_PUSH(paths, const char *) = sbox_wc_path(b, path);
  return svn_client_propset_local(name, pval, paths, svn_depth_empty,
                                  TRUE /* skip_checks */,
                                  NULL, ctx, b->pool);
}

svn_error_t *
sbox_wc_relocate(svn_test__sandbox_t *b,
                 const char *new_repos_url)
{
  apr_pool_t *scratch_pool = b->pool;
  svn_client_ctx_t *ctx;

  SVN_ERR(svn_test__create_client_ctx(&ctx, b, scratch_pool));

  SVN_ERR(svn_client_relocate2(b->wc_abspath, b->repos_url,
                               new_repos_url, FALSE, ctx,scratch_pool));

  b->repos_url = apr_pstrdup(b->pool, new_repos_url);

  return SVN_NO_ERROR;
}

svn_error_t *
sbox_add_and_commit_greek_tree(svn_test__sandbox_t *b)
{
  const struct svn_test__tree_entry_t *node;

  for (node = svn_test__greek_tree_nodes; node->path; node++)
    {
      if (node->contents)
        {
          SVN_ERR(sbox_file_write(b, node->path, node->contents));
          SVN_ERR(sbox_wc_add(b, node->path));
        }
      else
        {
          SVN_ERR(sbox_wc_mkdir(b, node->path));
        }
    }

  SVN_ERR(sbox_wc_commit(b, ""));

  return SVN_NO_ERROR;
}
