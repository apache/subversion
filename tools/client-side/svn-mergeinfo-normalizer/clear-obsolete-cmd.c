/*
 * clear-obsolete-cmd.c -- Remove branches from MI that don't exist in HEAD.
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

/* ==================================================================== */



/*** Includes. ***/

#include "svn_dirent_uri.h"
#include "svn_hash.h"
#include "svn_path.h"
#include "svn_pools.h"
#include "private/svn_fspath.h"

#include "mergeinfo-normalizer.h"

#include "svn_private_config.h"
#include <apr_poll.h>


/*** Code. ***/

static svn_error_t *
remove_obsolete_lines(svn_ra_session_t *session,
                      svn_mergeinfo_t mergeinfo,
                      apr_pool_t *scratch_pool)
{
  apr_array_header_t *to_remove
    = apr_array_make(scratch_pool, 16, sizeof(const char *));

  int i;
  apr_hash_index_t *hi;
  for (hi = apr_hash_first(scratch_pool, mergeinfo);
       hi;
       hi = apr_hash_next(hi))
    {
      const char *path = apr_hash_this_key(hi);
      svn_node_kind_t kind;

      SVN_ERR_ASSERT(*path == '/');
      SVN_ERR(svn_ra_check_path(session, path + 1, SVN_INVALID_REVNUM, &kind,
                                scratch_pool));
      if (kind == svn_node_none)
        APR_ARRAY_PUSH(to_remove, const char *) = path;
    }

  for (i = 0; i < to_remove->nelts; ++i)
    {
      const char *path = APR_ARRAY_IDX(to_remove, i, const char *);
      svn_hash_sets(mergeinfo, path, NULL);
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
remove_obsoletes(apr_array_header_t *wc_mergeinfo,
                 svn_ra_session_t *session,
                 apr_pool_t *scratch_pool)
{
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);

  int i;
  for (i = 0; i < wc_mergeinfo->nelts; ++i)
    {
      svn_mergeinfo_t mergeinfo = svn_min__get_mergeinfo(wc_mergeinfo, i);
      svn_pool_clear(iterpool);

      /* Combine mergeinfo ranges */
      SVN_ERR(remove_obsolete_lines(session, mergeinfo, iterpool));
    }

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

/* This implements the `svn_opt_subcommand_t' interface. */
svn_error_t *
svn_min__clear_obsolete(apr_getopt_t *os,
                        void *baton,
                        apr_pool_t *pool)
{
  svn_min__cmd_baton_t *cmd_baton = baton;
  apr_pool_t *iterpool = svn_pool_create(pool);
  apr_pool_t *subpool = svn_pool_create(pool);

  int i;
  for (i = 0; i < cmd_baton->opt_state->targets->nelts; i++)
    {
      svn_ra_session_t *session;
      apr_array_header_t *wc_mergeinfo;

      svn_pool_clear(iterpool);
      SVN_ERR(svn_min__add_wc_info(baton, i, iterpool, subpool));
      SVN_ERR(svn_client_open_ra_session2(&session, cmd_baton->repo_root,
                                          NULL, cmd_baton->ctx, iterpool,
                                          subpool));

      /* scan working copy */
      svn_pool_clear(subpool);
      SVN_ERR(svn_min__read_mergeinfo(&wc_mergeinfo, cmd_baton, iterpool,
                                      subpool));

      /* actual normalization */
      svn_pool_clear(subpool);
      SVN_ERR(remove_obsoletes(wc_mergeinfo, session, subpool));

      /* write results to disk */
      svn_pool_clear(subpool);
      if (!cmd_baton->opt_state->dry_run)
        SVN_ERR(svn_min__write_mergeinfo(cmd_baton, wc_mergeinfo, subpool));
    }

  svn_pool_destroy(subpool);
  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}
