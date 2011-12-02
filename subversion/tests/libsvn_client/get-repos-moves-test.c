/*
 * get-repos-moves-test.c :  test log scanning
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

/* To avoid warnings... */
#define SVN_DEPRECATED

#include "svn_pools.h"
#include "svn_ra.h"
#include "svn_client.h"

#include "../../libsvn_client/client.h"

#include "../libsvn_wc/utils.h"

#include "../svn_test.h"

static svn_error_t *
mkdir_urls(svn_test__sandbox_t *b,
           svn_client_ctx_t *ctx,
           ...)
{
  va_list va;
  apr_array_header_t *dirs = apr_array_make(b->pool, 10, sizeof(const char *));
  const char *dir;
  svn_client_commit_info_t *cci;

  va_start(va, ctx);
  while ((dir = va_arg(va, const char *)))
    APR_ARRAY_PUSH(dirs, const char *)
      = svn_path_url_add_component2(b->repos_url, dir, b->pool);
  va_end(va);

  SVN_ERR(svn_client_mkdir(&cci, dirs, ctx, b->pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
commit_moves(svn_test__sandbox_t *b,
             svn_client_ctx_t *ctx,
             ...)
{
  va_list va;
  svn_revnum_t revnum;
  svn_opt_revision_t head = { svn_opt_revision_head };
  const char *src_relpath, *dst_relpath;
  apr_array_header_t *target = apr_array_make(b->pool, 1, sizeof(const char *));
  svn_client_commit_info_t *cci;

  SVN_ERR(svn_client_update(&revnum, b->wc_abspath, &head, TRUE, ctx, b->pool));

  va_start(va, ctx);
  while((src_relpath = va_arg(va, const char *))
        && (dst_relpath = va_arg(va, const char *)))
    
    {
      const char *src_abspath = svn_dirent_join(b->wc_abspath, src_relpath,
                                                b->pool);
      const char *dst_abspath = svn_dirent_join(b->wc_abspath, dst_relpath,
                                                b->pool);

      SVN_ERR(svn_client_move4(NULL, src_abspath, dst_abspath, FALSE, ctx,
                               b->pool));
    }
  va_end(va);

  APR_ARRAY_PUSH(target, const char *) = b->wc_abspath;

  SVN_ERR(svn_client_commit(&cci, target, FALSE, ctx, b->pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
verify_move(apr_hash_t *moves,
            svn_revnum_t revnum,
            const char *moved_from,
            const char *moved_to,
            svn_revnum_t copyfrom_revnum)
{
  apr_array_header_t *rev_moves = apr_hash_get(moves, &revnum, sizeof(revnum));
  int i;

  if (!rev_moves)
    return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                             "No moves found for r%d", (int)revnum);

  for (i = 0; i < rev_moves->nelts; ++i)
    {
      const svn_wc_repos_move_info_t *move
        = APR_ARRAY_IDX(rev_moves, i, const svn_wc_repos_move_info_t *);
      if (move->copyfrom_rev == copyfrom_revnum
          && !strcmp(move->moved_from_repos_relpath, moved_from)
          && !strcmp(move->moved_to_repos_relpath, moved_to))
        break;
    }
  if (i >= rev_moves->nelts)
    return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                             "Move of '%s@%d' to '%s' in r%d not found",
                             moved_from, (int)copyfrom_revnum,
                             moved_to, (int)revnum);

  return SVN_NO_ERROR;
}

static svn_error_t *
test_moving_dirs(const svn_test_opts_t *opts,
                 apr_pool_t *pool)
{
  svn_test__sandbox_t b;
  svn_client_ctx_t *ctx;
  svn_ra_callbacks2_t *racb;
  svn_ra_session_t *ra;
  apr_hash_t *moves;

  SVN_ERR(svn_test__sandbox_create(&b, "moving_dirs", opts, pool));
  SVN_ERR(svn_client_create_context(&ctx, pool));

  SVN_ERR(mkdir_urls(&b, ctx, "A", "A/B", "A/B/C", (const char *)NULL));
  SVN_ERR(mkdir_urls(&b, ctx, "X", "X/Y", "X/Y/Z", (const char *)NULL));

  SVN_ERR(commit_moves(&b, ctx, "A/B", "A/B2", (const char *)NULL));
  SVN_ERR(commit_moves(&b, ctx, "A/B2", "A/B3", (const char *)NULL));
  SVN_ERR(commit_moves(&b, ctx, "A", "A2", (const char *)NULL));
  SVN_ERR(commit_moves(&b, ctx, "A2/B3/C", "A2/B3/C2", "X/Y/Z", "X/Y/Z2",
                       (const char *)NULL));
  SVN_ERR(commit_moves(&b, ctx, "A2/B3/C2", "X/Y/C3", "X/Y/Z2", "A2/B3/Z3",
                       (const char *)NULL));

  SVN_ERR(svn_ra_create_callbacks(&racb, pool));
  SVN_ERR(svn_ra_open4(&ra, NULL, b.repos_url, NULL, racb, NULL, NULL, pool));
  SVN_ERR(svn_client__get_repos_moves(&moves, b.wc_abspath, ra, 1, 7, ctx,
                                      pool, pool));

  SVN_ERR(verify_move(moves, 3, "A/B", "A/B2", 2));
  SVN_ERR(verify_move(moves, 4, "A/B2", "A/B3", 3));
  SVN_ERR(verify_move(moves, 5, "A", "A2", 4));
  SVN_ERR(verify_move(moves, 6, "A2/B3/C", "A2/B3/C2", 5));
  SVN_ERR(verify_move(moves, 6, "X/Y/Z", "X/Y/Z2", 5));
  SVN_ERR(verify_move(moves, 7, "A2/B3/C2", "X/Y/C3", 6));
  SVN_ERR(verify_move(moves, 7, "X/Y/Z2", "A2/B3/Z3", 6));

  return SVN_NO_ERROR;
}

static svn_error_t *
test_nested_moves(const svn_test_opts_t *opts,
                  apr_pool_t *pool)
{
  svn_test__sandbox_t b;
  svn_client_ctx_t *ctx;
  svn_ra_callbacks2_t *racb;
  svn_ra_session_t *ra;
  apr_hash_t *moves;

  SVN_ERR(svn_test__sandbox_create(&b, "nested_moves", opts, pool));
  SVN_ERR(svn_client_create_context(&ctx, pool));

  SVN_ERR(mkdir_urls(&b, ctx, "A", "A/B", "A/B/C", (const char *)NULL));
  SVN_ERR(commit_moves(&b, ctx, "A/B/C", "A/B/C2", "A/B", "A/B2", "A", "A2",
                       (const char *)NULL));

  SVN_ERR(svn_ra_create_callbacks(&racb, pool));
  SVN_ERR(svn_ra_open4(&ra, NULL, b.repos_url, NULL, racb, NULL, NULL, pool));
  SVN_ERR(svn_client__get_repos_moves(&moves, b.wc_abspath, ra, 2, 2, ctx,
                                      pool, pool));

  SVN_ERR(verify_move(moves, 2, "A", "A2", 1));
  SVN_ERR(verify_move(moves, 2, "A/B", "A2/B2", 1));
  SVN_ERR(verify_move(moves, 2, "A/B/C", "A2/B2/C2", 1));

  return SVN_NO_ERROR;
}

struct svn_test_descriptor_t test_funcs[] =
  {
    SVN_TEST_NULL,
    SVN_TEST_OPTS_PASS(test_moving_dirs, "test moving dirs"),
    SVN_TEST_OPTS_PASS(test_nested_moves, "test nested moves"),
    SVN_TEST_NULL,
  };
