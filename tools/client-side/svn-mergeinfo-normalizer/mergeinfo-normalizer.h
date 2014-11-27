/*
 * mergeinfo-normalizer.h:  tool-global functions and structures.
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



#ifndef SVN_MERGEINFO_NORMALIZER_H
#define SVN_MERGEINFO_NORMALIZER_H

/*** Includes. ***/
#include <apr_tables.h>
#include <apr_getopt.h>

#include "svn_client.h"
#include "svn_opt.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/*** Command dispatch. ***/

/* Hold results of option processing that are shared by multiple
   commands. */
typedef struct svn_min__opt_state_t
{
  /* After option processing is done, reflects the switch actually
     given on the command line, or svn_depth_unknown if none. */
  svn_depth_t depth;

  svn_boolean_t quiet;           /* sssh...avoid unnecessary output */
  svn_boolean_t version;         /* print version information */
  svn_boolean_t help;            /* print usage message */
  const char *auth_username;     /* auth username */
  const char *auth_password;     /* auth password */
  apr_array_header_t *targets;
  svn_boolean_t no_auth_cache;   /* do not cache authentication information */
  svn_boolean_t dry_run;         /* try operation but make no changes */
  const char *config_dir;        /* over-riding configuration directory */
  apr_array_header_t *config_options; /* over-riding configuration options */

  /* trust server SSL certs that would otherwise be rejected as "untrusted" */
  svn_boolean_t trust_server_cert_unknown_ca;
  svn_boolean_t trust_server_cert_cn_mismatch;
  svn_boolean_t trust_server_cert_expired;
  svn_boolean_t trust_server_cert_not_yet_valid;
  svn_boolean_t trust_server_cert_other_failure;
  svn_boolean_t allow_mixed_rev;   /* Allow operation on mixed-revision WC */
  svn_boolean_t non_interactive;
} svn_min__opt_state_t;


typedef struct svn_min__cmd_baton_t
{
  svn_min__opt_state_t *opt_state;
  svn_client_ctx_t *ctx;

  const char *local_abspath;
  const char *wc_root;
  const char *repo_root;
} svn_min__cmd_baton_t;


/* Declare all the command procedures */
svn_opt_subcommand_t
  svn_min__help,
  svn_min__normalize,
  svn_min__clear_obsolete,
  svn_min__combine_ranges,
  svn_min__analyze;

/* See definition in svn.c for documentation. */
extern const svn_opt_subcommand_desc2_t svn_min__cmd_table[];

/* See definition in svn.c for documentation. */
extern const int svn_min__global_options[];

/* See definition in svn.c for documentation. */
extern const apr_getopt_option_t svn_min__options[];


/* Our cancellation callback. */
svn_error_t *
svn_min__check_cancel(void *baton);


/*** Command-line output functions -- printing to the user. ***/

svn_error_t *
svn_min__add_wc_info(svn_min__cmd_baton_t* baton,
                     int idx,
                     apr_pool_t* result_pool,
                     apr_pool_t* scratch_pool);

svn_error_t *
svn_min__read_mergeinfo(apr_array_header_t **result,
                        svn_min__cmd_baton_t *baton,
                        apr_pool_t *result_pool,
                        apr_pool_t *scratch_pool);

const char *
svn_min__common_parent(apr_array_header_t *mergeinfo,
                       apr_pool_t *result_pool,
                       apr_pool_t *scratch_pool);

svn_mergeinfo_t
svn_min__get_mergeinfo(apr_array_header_t *mergeinfo,
                       int idx);

const char *
svn_min__get_mergeinfo_path(apr_array_header_t *mergeinfo,
                            int idx);

svn_boolean_t
svn_min__get_mergeinfo_pair(const char **parent_path,
                            const char **subtree_relpath,
                            svn_mergeinfo_t *parent_mergeinfo,
                            svn_mergeinfo_t *subtree_mergeinfo,
                            apr_array_header_t *mergeinfo,
                            int idx);

svn_error_t *
svn_min__write_mergeinfo(svn_min__cmd_baton_t *baton,
                         apr_array_header_t *mergeinfo,
                         apr_pool_t *scratch_pool);

svn_error_t *
svn_min__print_mergeinfo_stats(apr_array_header_t *wc_mergeinfo,
                               apr_pool_t *scratch_pool);

typedef struct svn_min__log_t svn_min__log_t;

svn_error_t *
svn_min__log(svn_min__log_t **log,
             const char *url,
             svn_min__cmd_baton_t *baton,
             apr_pool_t *result_pool,
             apr_pool_t *scratch_pool);

svn_rangelist_t *
svn_min__operative(svn_min__log_t *log,
                   const char *path,
                   svn_rangelist_t *ranges,
                   apr_pool_t *result_pool);

svn_rangelist_t *
svn_min__operative_outside_subtree(svn_min__log_t *log,
                                   const char *path,
                                   const char *subtree,
                                   svn_rangelist_t *ranges,
                                   apr_pool_t *result_pool);

svn_error_t *
svn_min__print_log_stats(svn_min__log_t *log,
                         apr_pool_t *scratch_pool);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_MERGEINFO_NORMALIZER_H */
