/**
 * @copyright
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
 * @endcopyright
 *
 * @file svn_fs_fs_private.h
 * @brief Private API for tools that access FSFS internals and can't use
 *        the svn_fs_t API for that.
 */


#ifndef SVN_FS_FS_PRIVATE_H
#define SVN_FS_FS_PRIVATE_H

#include <apr_pools.h>
#include <apr_hash.h>

#include "svn_types.h"
#include "svn_error.h"
#include "svn_iter.h"
#include "svn_config.h"
#include "svn_string.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */



/* Description of one large representation.  It's content will be reused /
 * overwritten when it gets replaced by an even larger representation.
 */
typedef struct svn_fs_fs__large_change_info_t
{
  /* size of the (deltified) representation */
  apr_size_t size;

  /* revision of the representation */
  svn_revnum_t revision;

  /* node path. "" for unused instances */
  svn_stringbuf_t *path;
} svn_fs_fs__large_change_info_t;

/* Container for the largest representations found so far.  The capacity
 * is fixed and entries will be inserted by reusing the last one and
 * reshuffling the entry pointers.
 */
typedef struct svn_fs_fs__largest_changes_t
{
  /* number of entries allocated in CHANGES */
  apr_size_t count;

  /* size of the smallest change */
  apr_size_t min_size;

  /* changes kept in this struct */
  svn_fs_fs__large_change_info_t **changes;
} svn_fs_fs__largest_changes_t;

/* Information we gather per size bracket.
 */
typedef struct svn_fs_fs__histogram_line_t
{
  /* number of item that fall into this bracket */
  apr_int64_t count;

  /* sum of values in this bracket */
  apr_int64_t sum;
} svn_fs_fs__histogram_line_t;

/* A histogram of 64 bit integer values.
 */
typedef struct svn_fs_fs__histogram_t
{
  /* total sum over all brackets */
  svn_fs_fs__histogram_line_t total;

  /* one bracket per binary step.
   * line[i] is the 2^(i-1) <= x < 2^i bracket */
  svn_fs_fs__histogram_line_t lines[64];
} svn_fs_fs__histogram_t;

/* Information we collect per file ending.
 */
typedef struct svn_fs_fs__extension_info_t
{
  /* file extension, including leading "."
   * "(none)" in the container for files w/o extension. */
  const char *extension;

  /* histogram of representation sizes */
  svn_fs_fs__histogram_t rep_histogram;

  /* histogram of sizes of changed files */
  svn_fs_fs__histogram_t node_histogram;
} svn_fs_fs__extension_info_t;

/* Compression statistics we collect over a given set of representations.
 */
typedef struct svn_fs_fs__rep_pack_stats_t
{
  /* number of representations */
  apr_int64_t count;

  /* total size after deltification (i.e. on disk size) */
  apr_int64_t packed_size;

  /* total size after de-deltification (i.e. plain text size) */
  apr_int64_t expanded_size;

  /* total on-disk header size */
  apr_int64_t overhead_size;
} svn_fs_fs__rep_pack_stats_t;

/* Statistics we collect over a given set of representations.
 * We group them into shared and non-shared ("unique") reps.
 */
typedef struct svn_fs_fs__representation_stats_t
{
  /* stats over all representations */
  svn_fs_fs__rep_pack_stats_t total;

  /* stats over those representations with ref_count == 1 */
  svn_fs_fs__rep_pack_stats_t uniques;

  /* stats over those representations with ref_count > 1 */
  svn_fs_fs__rep_pack_stats_t shared;

  /* sum of all ref_counts */
  apr_int64_t references;

  /* sum of ref_count * expanded_size,
   * i.e. total plaintext content if there was no rep sharing */
  apr_int64_t expanded_size;
} svn_fs_fs__representation_stats_t;

/* Basic statistics we collect over a given set of noderevs.
 */
typedef struct svn_fs_fs__node_stats_t
{
  /* number of noderev structs */
  apr_int64_t count;

  /* their total size on disk (structs only) */
  apr_int64_t size;
} svn_fs_fs__node_stats_t;

/* Comprises all the information needed to create the output of the
 * 'svnfsfs stats' command.
 */
typedef struct svn_fs_fs__stats_t
{
  /* sum total of all rev / pack file sizes in bytes */
  apr_int64_t total_size;

  /* number of revisions in the repository */
  apr_int64_t revision_count;

  /* total number of changed paths */
  apr_int64_t change_count;

  /* sum of all changed path list sizes on disk in bytes */
  apr_int64_t change_len;

  /* stats on all representations */
  svn_fs_fs__representation_stats_t total_rep_stats;

  /* stats on all file text representations */
  svn_fs_fs__representation_stats_t file_rep_stats;

  /* stats on all directory text representations */
  svn_fs_fs__representation_stats_t dir_rep_stats;

  /* stats on all file prop representations */
  svn_fs_fs__representation_stats_t file_prop_rep_stats;

  /* stats on all directory prop representations */
  svn_fs_fs__representation_stats_t dir_prop_rep_stats;

  /* size and count summary over all noderevs */
  svn_fs_fs__node_stats_t total_node_stats;

  /* size and count summary over all file noderevs */
  svn_fs_fs__node_stats_t file_node_stats;

  /* size and count summary over all directory noderevs */
  svn_fs_fs__node_stats_t dir_node_stats;

  /* the biggest single contributors to repo size */
  svn_fs_fs__largest_changes_t *largest_changes;

  /* histogram of representation sizes */
  svn_fs_fs__histogram_t rep_size_histogram;

  /* histogram of sizes of changed nodes */
  svn_fs_fs__histogram_t node_size_histogram;

  /* histogram of representation sizes */
  svn_fs_fs__histogram_t added_rep_size_histogram;

  /* histogram of sizes of changed nodes */
  svn_fs_fs__histogram_t added_node_size_histogram;

  /* histogram of unused representations */
  svn_fs_fs__histogram_t unused_rep_histogram;

  /* histogram of sizes of changed files */
  svn_fs_fs__histogram_t file_histogram;

  /* histogram of sizes of file representations */
  svn_fs_fs__histogram_t file_rep_histogram;

  /* histogram of sizes of changed file property sets */
  svn_fs_fs__histogram_t file_prop_histogram;

  /* histogram of sizes of file property representations */
  svn_fs_fs__histogram_t file_prop_rep_histogram;

  /* histogram of sizes of changed directories (in bytes) */
  svn_fs_fs__histogram_t dir_histogram;

  /* histogram of sizes of directories representations */
  svn_fs_fs__histogram_t dir_rep_histogram;

  /* histogram of sizes of changed directories property sets */
  svn_fs_fs__histogram_t dir_prop_histogram;

  /* histogram of sizes of directories property representations */
  svn_fs_fs__histogram_t dir_prop_rep_histogram;

  /* extension -> extension_info_t* map */
  apr_hash_t *by_extension;
} svn_fs_fs__stats_t;


/* Scan all contents of the repository FS and return statistics in *STATS,
 * allocated in RESULT_POOL.  Report progress through PROGRESS_FUNC with
 * PROGRESS_BATON, if PROGRESS_FUNC is not NULL.
 * Use SCRATCH_POOL for temporary allocations.
 */
svn_error_t *
svn_fs_fs__get_stats(svn_fs_fs__stats_t **stats,
                     svn_fs_t *fs,
                     svn_fs_progress_notify_func_t progress_func,
                     void *progress_baton,
                     svn_cancel_func_t cancel_func,
                     void *cancel_baton,
                     apr_pool_t *result_pool,
                     apr_pool_t *scratch_pool);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_FS_FS_PRIVATE_H */
