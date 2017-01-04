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
  svn_boolean_t verbose;         /* be verbose */
  svn_boolean_t help;            /* print usage message */
  const char *auth_username;     /* auth username */
  const char *auth_password;     /* auth password */
  apr_array_header_t *targets;
  svn_boolean_t no_auth_cache;   /* do not cache authentication information */
  svn_boolean_t dry_run;         /* try operation but make no changes */
  const char *config_dir;        /* over-riding configuration directory */
  apr_array_header_t *config_options; /* over-riding configuration options */
  svn_stringbuf_t *filedata;     /* contents read from --file argument */

  /* Selected normalization operations. */
  svn_boolean_t remove_obsoletes;
  svn_boolean_t combine_ranges;
  svn_boolean_t remove_redundants;
  svn_boolean_t remove_redundant_misaligned;
  svn_boolean_t run_analysis;

  /* trust server SSL certs that would otherwise be rejected as "untrusted" */
  svn_boolean_t trust_server_cert_unknown_ca;
  svn_boolean_t trust_server_cert_cn_mismatch;
  svn_boolean_t trust_server_cert_expired;
  svn_boolean_t trust_server_cert_not_yet_valid;
  svn_boolean_t trust_server_cert_other_failure;
  svn_boolean_t allow_mixed_rev;   /* Allow operation on mixed-revision WC */
  svn_boolean_t non_interactive;
} svn_min__opt_state_t;

/* Opaque structure allowing to check efficiently whether a given path
 * exists in the repository @HEAD. */
typedef struct svn_min__branch_lookup_t svn_min__branch_lookup_t;

/* Type of the baton passed to any of our sub-commands. */
typedef struct svn_min__cmd_baton_t
{
  /* Preprocessed line options. */
  svn_min__opt_state_t *opt_state;

  /* Client context. */
  svn_client_ctx_t *ctx;

  /* Base path of the directory tree currently being processed. */
  const char *local_abspath;

  /* Working copy root path of LOCAL_ABSPATH. */
  const char *wc_root;

  /* Root of the corresponding working copy. */
  const char *repo_root;

  /* If the sub-command, e.g. the local lookup only 'remove-branches',
   * needs a specific repository lookup data structure, set it here.
   * If this is NULL, the sub-command will use remove lookup to REPO_ROOT. */
  svn_min__branch_lookup_t *lookup;
} svn_min__cmd_baton_t;


/* Declare all the command procedures */
svn_opt_subcommand_t
  svn_min__help,
  svn_min__normalize,
  svn_min__analyze,
  svn_min__remove_branches;

/* See definition in svn.c for documentation. */
extern const svn_opt_subcommand_desc2_t svn_min__cmd_table[];

/* See definition in svn.c for documentation. */
extern const int svn_min__global_options[];

/* See definition in svn.c for documentation. */
extern const apr_getopt_option_t svn_min__options[];


/* Our cancellation callback. */
svn_error_t *
svn_min__check_cancel(void *baton);


/*** Internal API linking the various modules. ***/

/* Scan the working copy sub-tree specified in BATON for mergeinfo and
 * return them in *RESULT, allocated in RESULT_POOL.  The element type is
 * opaque.  Use SCRATCH_POOL for temporary allocations. */
svn_error_t *
svn_min__read_mergeinfo(apr_array_header_t **result,
                        svn_min__cmd_baton_t *baton,
                        apr_pool_t *result_pool,
                        apr_pool_t *scratch_pool);

/* For the MERGEINFO as returned by svn_min__read_mergeinfo() return the
 * FS path that is parent to the working copy and all branches mentioned
 * in the mergeinfo.  Allocate the return value in RESULT_POOL and use
 * SCRATCH_POOL for temporaries. */
const char *
svn_min__common_parent(apr_array_header_t *mergeinfo,
                       apr_pool_t *result_pool,
                       apr_pool_t *scratch_pool);

/* Return the mergeinfo at index IDX in MERGEINFO.
 * IDX must be 0 .. MERGEINFO->NELTS-1. */
svn_mergeinfo_t
svn_min__get_mergeinfo(apr_array_header_t *mergeinfo,
                       int idx);

/* Return the full info on the mergeinfo at IDX in MERGEINFO.  Set *FS_PATH
 * to the FS path of the respective working copy node, *SUBTREE_RELPATH to
 * its local absolute path and *PARENT_PATH to the local absolute path of
 * the working copy node that carries the closest parent mergeinfo.
 * return the working.  Set *SUBTREE_MERGEINFO to the parsed mergeinfo at
 * *SUBTREE_RELPATH and *PARENT_MERGEINFO to the parsed mergeinfo at
 * *PARENT_PATH.  In *SIBLING_MERGEINFO return the list of immediate sub-node
 * mergeinfo below *PARENT_PATH, including the *SUBTREE_MERGEINFO.
 *
 * If there is no parent mergeinfo, *PARENT_PATH will be "" and
 * *PARENT_MERGEINFO will be NULL.  If IDX is not a valid array index,
 * "" will be returned for all paths and all mergeinfo will be NULL.
 *
 * Note that the returned data is shared with MERGEINFO and has the same
 * lifetime.  It is perfectly legal to modify the svn_mergeinfo_t hashes
 * and store the result using svn_min__write_mergeinfo. */
void
svn_min__get_mergeinfo_pair(const char **fs_path,
                            const char **parent_path,
                            const char **subtree_relpath,
                            svn_mergeinfo_t *parent_mergeinfo,
                            svn_mergeinfo_t *subtree_mergeinfo,
                            apr_array_header_t **siblings_mergeinfo,
                            apr_array_header_t *mergeinfo,
                            int idx);

/* Search SIBLING_MERGEINFO for mergeinfo that intersects PARENT_PATH
 * and RELEVANT_RANGES.  Return the FS path to range list hash in
 * *SIBLING_RANGES, allocated in RESULT_POOL.  Use SCRATCH_POOL for
 * temporary allocations
 */
svn_error_t *
svn_min__sibling_ranges(apr_hash_t **sibling_ranges,
                        apr_array_header_t *sibling_mergeinfo,
                        const char *parent_path,
                        svn_rangelist_t *relevant_ranges,
                        apr_pool_t *result_pool,
                        apr_pool_t *scratch_pool);

/* Store the MERGEINFO in the working copy specified by BATON.  Delete
 * the mergeinfo on those nodes where it is empty but keep the empty data
 * in MERGEINFO.  Use SCRATCH_POOL for temporary allocations. */
svn_error_t *
svn_min__write_mergeinfo(svn_min__cmd_baton_t *baton,
                         apr_array_header_t *mergeinfo,
                         apr_pool_t *scratch_pool);

/* Remove entries with empty mergeinfo from MERGEINFO. */
svn_error_t *
svn_min__remove_empty_mergeinfo(apr_array_header_t *mergeinfo);

/* Print statistics for WC_MERGEINFO to console.  Use SCRATCH_POOL for
 * temporaries. */
svn_error_t *
svn_min__print_mergeinfo_stats(apr_array_header_t *wc_mergeinfo,
                               apr_pool_t *scratch_pool);

/* Opaque data structure containing the log / history downloaded from the
 * repository. */
typedef struct svn_min__log_t svn_min__log_t;

/* Data structure describing a copy operation as part of svn_min__log_t. */
typedef struct svn_min__copy_t
{
  /* Copy target FS path. */
  const char *path;

  /* Copy target revision. */
  svn_revnum_t revision;

  /* Copy source FS path. */
  const char *copyfrom_path;

  /* Copy source revision. */
  svn_revnum_t copyfrom_revision;
} svn_min__copy_t;

/* Fetch the full *LOG for the given URL using the context in BATON.
 * Allocate *LOG in RESULT_POOL and use SCRATCH_POOL for temporaries. */
svn_error_t *
svn_min__log(svn_min__log_t **log,
             const char *url,
             svn_min__cmd_baton_t *baton,
             apr_pool_t *result_pool,
             apr_pool_t *scratch_pool);

/* Scan LOG and determine what revisions in RANGES actually operate on PATH
 * or its sub-nodes.  Return those revisions, allocated in RESULT_POOL.
 * Note that parent path changes don't count as operative within PATH. */
svn_rangelist_t *
svn_min__operative(svn_min__log_t *log,
                   const char *path,
                   svn_rangelist_t *ranges,
                   apr_pool_t *result_pool);

/* Scan LOG and determine what revisions in RANGES are operative on PATH
 * but outside SUBTREE (possibly but not exclusively modifying paths within
 * SUBTREE).  Return those revisions, allocated in RESULT_POOL. */
svn_rangelist_t *
svn_min__operative_outside_subtree(svn_min__log_t *log,
                                   const char *path,
                                   const char *subtree,
                                   svn_rangelist_t *ranges,
                                   apr_pool_t *result_pool);

/* Scan LOG and return those revisions from RANGES that have changes
 * operative on the PATH subtree and where at least one of these changes
 * are not covered by any entry in SIBLING_RANGES.
 *
 * Allocate the result in RESULT_POOL and use SCRATCH_POOL for tempoaries.
 */
svn_rangelist_t *
svn_min__operative_outside_all_subtrees(svn_min__log_t *log,
                                        const char *path,
                                        svn_rangelist_t *ranges,
                                        apr_hash_t *sibling_ranges,
                                        apr_pool_t *result_pool,
                                        apr_pool_t *scratch_pool);

/* Scan LOG from START_REV down to END_REV and find the latest deletion of
 * PATH or a parent thereof and return the revision that contains the
 * deletion.  Return SVN_INVALID_REVNUM if no such deletion could be found.
 * Use SCRATCH_POOL for temporaries. */
svn_revnum_t
svn_min__find_deletion(svn_min__log_t *log,
                       const char *path,
                       svn_revnum_t start_rev,
                       svn_revnum_t end_rev,
                       apr_pool_t *scratch_pool);

/* Scan LOG for deletions of PATH or any of its parents.  Return an array,
 * allocated in RESULT_POOL, containing all svn_revnum_t that contain such
 * deletions in ascending order.  Use SCRATCH_POOL for temporaries. */
apr_array_header_t *
svn_min__find_deletions(svn_min__log_t *log,
                        const char *path,
                        apr_pool_t *result_pool,
                        apr_pool_t *scratch_pool);

/* Scan LOG for the latest copy of PATH or any of its parents no later than
 * START_REV and no earlier than END_REV.  Return SVN_INVALID_REVNUM if no
 * such revision exists.  Use SCRATCH_POOL for temporaries. */
svn_revnum_t
svn_min__find_copy(svn_min__log_t *log,
                   const char *path,
                   svn_revnum_t start_rev,
                   svn_revnum_t end_rev,
                   apr_pool_t *scratch_pool);

/* Scan LOG for copies of PATH, any of its parents or sub-nodes made from
 * revisions no later than START_REV and no earlier than END_REV.  Return
 * those copies as svn_min__copy_t* in an array allocated in RESULT_POOL.
 * The copy objects themselves are shared with LOG.  Use SCRATCH_POOL
 * for temporary allocations. */
apr_array_header_t *
svn_min__get_copies(svn_min__log_t *log,
                    const char *path,
                    svn_revnum_t start_rev,
                    svn_revnum_t end_rev,
                    apr_pool_t *result_pool,
                    apr_pool_t *scratch_pool);

/* Return the opaque history object for PATH, starting at START_REV and
 * going back to its initial creation or END_REV - whichever is latest.
 *
 * The history is a sequence of segments, each describing that the node
 * existed at a given path for a given range of revisions.  Between two
 * segments, there must have been a copy operation.
 *
 * Allocate the result in RESULT_POOL and use SCRATCH_POOL for temporaries.
 */
apr_array_header_t *
svn_min__get_history(svn_min__log_t *log,
                     const char *path,
                     svn_revnum_t start_rev,
                     svn_revnum_t end_rev,
                     apr_pool_t *result_pool,
                     apr_pool_t *scratch_pool);

/* Return the (potentially empty) parts of LHS and RHS where their history
 * overlaps, i.e. those (partial) history segments where they have the same
 * path for in the same revisions.  Allocate the result in RESULT_POOL. */
apr_array_header_t *
svn_min__intersect_history(apr_array_header_t *lhs,
                           apr_array_header_t *rhs,
                           apr_pool_t *result_pool);

/* Return the revision ranges that are covered by the segments in HISTORY.
 * Allocate the result in RESULT_POOL. */
svn_rangelist_t *
svn_min__history_ranges(apr_array_header_t *history,
                        apr_pool_t *result_pool);

/* Allocate a new path lookup object in RESULT_POOL and make it use SESSION
 * for any future repository lookups. */
svn_min__branch_lookup_t *
svn_min__branch_lookup_create(svn_ra_session_t *session,
                              apr_pool_t *result_pool);

/* Allocate a new path lookup object in RESULT_POOL and set the list of
 * missing paths to PATHS.  This object will never contact the repository. */
svn_min__branch_lookup_t *
svn_min__branch_lookup_from_paths(apr_array_header_t *paths,
                                  apr_pool_t *result_pool);

/* Set *DELETED to TRUE, if we can confirm using LOOKUP that BRANCH does
 * not exist @HEAD.  If LOCAL_ONLY is set or if LOOKUP has not been created
 * with a repository session, base that judgement on information in LOOKUP
 * alone and report FALSE for unknonw path.  Otherwise contact the
 * repository for unknown paths and store the result in LOOKUP.
 * Use SCRATCH_POOL for temporary allocations. */
svn_error_t *
svn_min__branch_lookup(svn_boolean_t *deleted,
                       svn_min__branch_lookup_t *lookup,
                       const char *branch,
                       svn_boolean_t local_only,
                       apr_pool_t *scratch_pool);

/* Return an array of const char *, allocated in RESULT_POOL, of all deleted
 * FS paths we encountered using LOOKUP.  We only return the respective
 * top-most missing paths - not any of their sub-nodes.  Use SCRATCH_POOL
 * for temporary allocations. */
apr_array_header_t *
svn_min__branch_deleted_list(svn_min__branch_lookup_t *lookup,
                             apr_pool_t *result_pool,
                             apr_pool_t *scratch_pool);

/* Run our common processing code shared between all sub-commands.
 * Take the sub-command behaviour from the flags in BATON. */
svn_error_t *
svn_min__run_normalize(void *baton,
                       apr_pool_t *pool);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_MERGEINFO_NORMALIZER_H */
