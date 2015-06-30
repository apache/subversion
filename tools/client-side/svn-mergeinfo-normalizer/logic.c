/*
 * logic.c -- Mergeinfo normalization / cleanup logic used by the commands.
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

static svn_boolean_t
needs_log(svn_min__opt_state_t *opt_state)
{
  return opt_state->combine_ranges || opt_state->remove_redundants;
}

static svn_boolean_t
needs_session(svn_min__opt_state_t *opt_state)
{
  return opt_state->remove_obsoletes;
}

static const char *
processing_title(svn_min__opt_state_t *opt_state,
                 apr_pool_t *result_pool)
{
  svn_stringbuf_t *result = svn_stringbuf_create_empty(result_pool);
  if (opt_state->remove_obsoletes)
    svn_stringbuf_appendcstr(result, _("Removing obsolete branches"));

  if (opt_state->remove_redundants)
    {
      if (svn_stringbuf_isempty(result))
        svn_stringbuf_appendcstr(result, _("Removing redundant mergeinfo"));
      else
        svn_stringbuf_appendcstr(result, _("and redundant mergeinfo"));
    }

  if (opt_state->combine_ranges)
    {
      if (svn_stringbuf_isempty(result))
        svn_stringbuf_appendcstr(result, _("Combining revision ranges"));
      else
        svn_stringbuf_appendcstr(result, _(", combining revision ranges"));
    }

  svn_stringbuf_appendcstr(result, " ...\n");
  return result->data;
}

/* This implements the `svn_opt_subcommand_t' interface. */
svn_error_t *
svn_min__run_command(apr_getopt_t *os,
                     void *baton,
                     svn_min__process_t processor,
                     apr_pool_t *pool)
{
  svn_min__cmd_baton_t *cmd_baton = baton;
  apr_pool_t *iterpool = svn_pool_create(pool);
  apr_pool_t *subpool = svn_pool_create(pool);

  int i;
  for (i = 0; i < cmd_baton->opt_state->targets->nelts; i++)
    {
      apr_array_header_t *wc_mergeinfo;
      svn_min__log_t *log = NULL;
      svn_ra_session_t *session = NULL;
      const char *url;
      const char *common_path;

      svn_pool_clear(iterpool);
      SVN_ERR(svn_min__add_wc_info(baton, i, iterpool, subpool));

      /* scan working copy */
      svn_pool_clear(subpool);
      SVN_ERR(svn_min__read_mergeinfo(&wc_mergeinfo, cmd_baton, iterpool,
                                      subpool));

      /* Any mergeinfo at all? */
      if (wc_mergeinfo->nelts == 0)
        continue;

      /* fetch log */
      if (needs_log(cmd_baton->opt_state))
        {
          svn_pool_clear(subpool);
          common_path = svn_min__common_parent(wc_mergeinfo, subpool, subpool);
          SVN_ERR_ASSERT(*common_path == '/');
          url = svn_path_url_add_component2(cmd_baton->repo_root,
                                            common_path + 1,
                                            subpool);
          SVN_ERR(svn_min__log(&log, url, cmd_baton, iterpool, subpool));
        }

      /* open RA session */
      if (needs_session(cmd_baton->opt_state))
        {
          svn_pool_clear(subpool);
          SVN_ERR(svn_min__add_wc_info(baton, i, iterpool, subpool));
          SVN_ERR(svn_client_open_ra_session2(&session, cmd_baton->repo_root,
                                              NULL, cmd_baton->ctx, iterpool,
                                              subpool));
        }

      /* actual normalization */
      svn_pool_clear(subpool);
      if (!cmd_baton->opt_state->quiet)
        SVN_ERR(svn_cmdline_fputs(processing_title(cmd_baton->opt_state,
                                                   subpool),
                                  stdout, subpool));

      SVN_ERR((*processor)(wc_mergeinfo, log, session, cmd_baton->opt_state,
                           subpool));

      /* write results to disk */
      svn_pool_clear(subpool);
      if (!cmd_baton->opt_state->dry_run)
        SVN_ERR(svn_min__write_mergeinfo(cmd_baton, wc_mergeinfo, subpool));

      /* show results */
      if (!cmd_baton->opt_state->quiet)
        {
          SVN_ERR(svn_cmdline_printf(subpool, _("\nRemaining mergeinfo:\n")));
          SVN_ERR(svn_min__print_mergeinfo_stats(wc_mergeinfo, subpool));
        }
    }

  svn_pool_destroy(subpool);
  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}
