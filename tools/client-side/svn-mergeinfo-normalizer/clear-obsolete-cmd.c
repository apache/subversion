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

#include "svn_cmdline.h"
#include "svn_dirent_uri.h"
#include "svn_hash.h"
#include "svn_path.h"
#include "svn_pools.h"
#include "private/svn_fspath.h"

#include "mergeinfo-normalizer.h"

#include "svn_private_config.h"


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
                 svn_min__log_t *log,
                 svn_ra_session_t *session,
                 svn_min__opt_state_t *opt_state,
                 apr_pool_t *scratch_pool)
{
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);

  int i;
  apr_int64_t removed = 0;
  for (i = 0; i < wc_mergeinfo->nelts; ++i)
    {
      svn_mergeinfo_t mergeinfo = svn_min__get_mergeinfo(wc_mergeinfo, i);
      unsigned initial_count = apr_hash_count(mergeinfo);
      svn_pool_clear(iterpool);

      /* Combine mergeinfo ranges */
      SVN_ERR(remove_obsolete_lines(session, mergeinfo, iterpool));
      removed += initial_count - apr_hash_count(mergeinfo);

      /* Show progress after every 1000 nodes and after the last one. */
      if (!opt_state->quiet
          && ((i+1) % 1000 == 0 || (i+1) == wc_mergeinfo->nelts))
        SVN_ERR(svn_cmdline_printf(iterpool,
                  _("    Processed %d nodes, removed %s branch entries.\n"),
                  i+1,
                  apr_psprintf(iterpool, "%" APR_UINT64_T_FMT, removed)));
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
  cmd_baton->opt_state->remove_obsoletes = TRUE;
  SVN_ERR(svn_min__run_command(os, baton, remove_obsoletes, pool));

  return SVN_NO_ERROR;
}
