/* verify.c --- verification of FSFS filesystems
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

#include "verify.h"
#include "fs_fs.h"

#include "cached_data.h"
#include "rep-cache.h"
#include "util.h"
#include "index.h"

#include "../libsvn_fs/fs-loader.h"

#include "svn_private_config.h"


/** Verifying. **/

/* Baton type expected by verify_walker().  The purpose is to reuse open
 * rev / pack file handles between calls.  Its contents need to be cleaned
 * periodically to limit resource usage.
 */
typedef struct verify_walker_baton_t
{
  /* number of calls to verify_walker() since the last clean */
  int iteration_count;

  /* number of files opened since the last clean */
  int file_count;

  /* progress notification callback to invoke periodically (may be NULL) */
  svn_fs_progress_notify_func_t notify_func;

  /* baton to use with NOTIFY_FUNC */
  void *notify_baton;

  /* remember the last revision for which we called notify_func */
  svn_revnum_t last_notified_revision;

  /* cached hint for successive calls to svn_fs_fs__check_rep() */
  void *hint;

  /* pool to use for the file handles etc. */
  apr_pool_t *pool;
} verify_walker_baton_t;

/* Used by svn_fs_fs__verify().
   Implements svn_fs_fs__walk_rep_reference().walker.  */
static svn_error_t *
verify_walker(representation_t *rep,
              void *baton,
              svn_fs_t *fs,
              apr_pool_t *scratch_pool)
{
  verify_walker_baton_t *walker_baton = baton;
  void *previous_hint;

  /* notify and free resources periodically */
  if (   walker_baton->iteration_count > 1000
      || walker_baton->file_count > 16)
    {
      if (   walker_baton->notify_func
          && rep->revision != walker_baton->last_notified_revision)
        {
          walker_baton->notify_func(rep->revision,
                                    walker_baton->notify_baton,
                                    scratch_pool);
          walker_baton->last_notified_revision = rep->revision;
        }

      svn_pool_clear(walker_baton->pool);

      walker_baton->iteration_count = 0;
      walker_baton->file_count = 0;
      walker_baton->hint = NULL;
    }

  /* access the repo data */
  previous_hint = walker_baton->hint;
  SVN_ERR(svn_fs_fs__check_rep(rep, fs, &walker_baton->hint,
                               walker_baton->pool));

  /* update resource usage counters */
  walker_baton->iteration_count++;
  if (previous_hint != walker_baton->hint)
    walker_baton->file_count++;

  return SVN_NO_ERROR;
}

/* Verify the rep cache DB's consistency with our rev / pack data.
 * The function signature is similar to svn_fs_fs__verify.
 * The values of START and END have already been auto-selected and
 * verified.
 */
static svn_error_t *
verify_rep_cache(svn_fs_t *fs,
                 svn_revnum_t start,
                 svn_revnum_t end,
                 svn_fs_progress_notify_func_t notify_func,
                 void *notify_baton,
                 svn_cancel_func_t cancel_func,
                 void *cancel_baton,
                 apr_pool_t *pool)
{
  svn_boolean_t exists;

  /* rep-cache verification. */
  SVN_ERR(svn_fs_fs__exists_rep_cache(&exists, fs, pool));
  if (exists)
    {
      /* provide a baton to allow the reuse of open file handles between
         iterations (saves 2/3 of OS level file operations). */
      verify_walker_baton_t *baton = apr_pcalloc(pool, sizeof(*baton));
      baton->pool = svn_pool_create(pool);
      baton->last_notified_revision = SVN_INVALID_REVNUM;
      baton->notify_func = notify_func;
      baton->notify_baton = notify_baton;

      /* tell the user that we are now ready to do *something* */
      if (notify_func)
        notify_func(SVN_INVALID_REVNUM, notify_baton, baton->pool);

      /* Do not attempt to walk the rep-cache database if its file does
         not exist,  since doing so would create it --- which may confuse
         the administrator.   Don't take any lock. */
      SVN_ERR(svn_fs_fs__walk_rep_reference(fs, start, end,
                                            verify_walker, baton,
                                            cancel_func, cancel_baton,
                                            pool));

      /* walker resource cleanup */
      svn_pool_destroy(baton->pool);
    }

  return SVN_NO_ERROR;
}

/* Verify that for all log-to-phys index entries for revisions START to
 * START + COUNT-1 in FS there is a consistent entry in the phys-to-log
 * index.  If given, invoke CANCEL_FUNC with CANCEL_BATON at regular
 * intervals. Use POOL for allocations.
 */
static svn_error_t *
compare_l2p_to_p2l_index(svn_fs_t *fs,
                         svn_revnum_t start,
                         svn_revnum_t count,
                         svn_cancel_func_t cancel_func,
                         void *cancel_baton,
                         apr_pool_t *pool)
{
  svn_revnum_t i;
  apr_pool_t *iterpool = svn_pool_create(pool);
  apr_array_header_t *max_ids;

  /* determine the range of items to check for each revision */
  SVN_ERR(svn_fs_fs__l2p_get_max_ids(&max_ids, fs, start, count, pool));

  /* check all items in all revisions if the given range */
  for (i = 0; i < max_ids->nelts; ++i)
    {
      apr_uint64_t k;
      apr_uint64_t max_id = APR_ARRAY_IDX(max_ids, i, apr_uint64_t);
      svn_revnum_t revision = start + i;

      for (k = 0; k < max_id; ++k)
        {
          apr_off_t offset;
          apr_uint32_t sub_item;
          svn_fs_fs__id_part_t *p2l_item;

          /* get L2P entry.  Ignore unused entries. */
          SVN_ERR(svn_fs_fs__item_offset(&offset, &sub_item, fs,
                                         revision, NULL, k, iterpool));
          if (offset == -1)
            continue;

          /* find the corresponding P2L entry */
          SVN_ERR(svn_fs_fs__p2l_item_lookup(&p2l_item, fs, start,
                                             offset, sub_item, iterpool));

          if (p2l_item == NULL)
            return svn_error_createf(SVN_ERR_FS_ITEM_INDEX_INCONSISTENT,
                                     NULL,
                                     _("p2l index entry not found for "
                                       "PHYS o%s:s%ld returned by "
                                       "l2p index for LOG r%ld:i%ld"),
                                     apr_off_t_toa(pool, offset),
                                     (long)sub_item, revision, (long)k);

          if (p2l_item->number != k || p2l_item->revision != revision)
            return svn_error_createf(SVN_ERR_FS_ITEM_INDEX_INCONSISTENT,
                                     NULL,
                                     _("p2l index info LOG r%ld:i%ld"
                                       " does not match "
                                       "l2p index for LOG r%ld:i%ld"),
                                     p2l_item->revision,
                                     (long)p2l_item->number, revision,
                                     (long)k);

          svn_pool_clear(iterpool);
        }

      if (cancel_func)
        SVN_ERR(cancel_func(cancel_baton));
    }

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

/* Verify that for all phys-to-log index entries for revisions START to
 * START + COUNT-1 in FS there is a consistent entry in the log-to-phys
 * index.  If given, invoke CANCEL_FUNC with CANCEL_BATON at regular
 * intervals. Use POOL for allocations.
 *
 * Please note that we can only check on pack / rev file granularity and
 * must only be called for a single rev / pack file.
 */
static svn_error_t *
compare_p2l_to_l2p_index(svn_fs_t *fs,
                         svn_revnum_t start,
                         svn_revnum_t count,
                         svn_cancel_func_t cancel_func,
                         void *cancel_baton,
                         apr_pool_t *pool)
{
  apr_pool_t *iterpool = svn_pool_create(pool);
  apr_off_t max_offset;
  apr_off_t offset = 0;

  /* get the size of the rev / pack file as covered by the P2L index */
  SVN_ERR(svn_fs_fs__p2l_get_max_offset(&max_offset, fs, start, pool));

  /* for all offsets in the file, get the P2L index entries and check
     them against the L2P index */
  for (offset = 0; offset < max_offset; )
    {
      apr_array_header_t *entries;
      svn_fs_fs__p2l_entry_t *last_entry;
      int i;

      /* get all entries for the current block */
      SVN_ERR(svn_fs_fs__p2l_index_lookup(&entries, fs, start, offset,
                                          iterpool));
      if (entries->nelts == 0)
        return svn_error_createf(SVN_ERR_FS_ITEM_INDEX_CORRUPTION,
                                 NULL,
                                 _("p2l does not cover offset %s"
                                   " for revision %ld"),
                                  apr_off_t_toa(pool, offset), start);

      /* process all entries (and later continue with the next block) */
      last_entry
        = &APR_ARRAY_IDX(entries, entries->nelts-1, svn_fs_fs__p2l_entry_t);
      offset = last_entry->offset + last_entry->size;
      
      for (i = 0; i < entries->nelts; ++i)
        {
          apr_uint32_t k;
          svn_fs_fs__p2l_entry_t *entry
            = &APR_ARRAY_IDX(entries, i, svn_fs_fs__p2l_entry_t);

          /* check all sub-items for consist entries in the L2P index */
          for (k = 0; k < entry->item_count; ++k)
            {
              apr_off_t l2p_offset;
              apr_uint32_t sub_item;
              svn_fs_fs__id_part_t *p2l_item = &entry->items[k];

              SVN_ERR(svn_fs_fs__item_offset(&l2p_offset, &sub_item, fs,
                                             p2l_item->revision, NULL,
                                             p2l_item->number, iterpool));

              if (sub_item != k || l2p_offset != entry->offset)
                return svn_error_createf(SVN_ERR_FS_ITEM_INDEX_INCONSISTENT,
                                         NULL,
                                         _("l2p index entry PHYS o%s:s%ld "
                                           "does not match p2l index value "
                                           "LOG r%ld:i%ld for PHYS o%s:s%ld"),
                                         apr_off_t_toa(pool, l2p_offset),
                                         (long)sub_item,
                                         p2l_item->revision,
                                         (long)p2l_item->number,
                                         apr_off_t_toa(pool, entry->offset),
                                         (long)k);
            }
        }

      svn_pool_clear(iterpool);

      if (cancel_func)
        SVN_ERR(cancel_func(cancel_baton));
    }

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

/* Verify that the log-to-phys indexes and phys-to-log indexes are
 * consistent with each other.  The function signature is similar to
 * svn_fs_fs__verify.
 *
 * The values of START and END have already been auto-selected and
 * verified.  You may call this for format7 or higher repos.
 */
static svn_error_t *
verify_index_consistency(svn_fs_t *fs,
                         svn_revnum_t start,
                         svn_revnum_t end,
                         svn_fs_progress_notify_func_t notify_func,
                         void *notify_baton,
                         svn_cancel_func_t cancel_func,
                         void *cancel_baton,
                         apr_pool_t *pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;
  svn_revnum_t revision, pack_start, pack_end;
  apr_pool_t *iterpool = svn_pool_create(pool);

  for (revision = start; revision <= end; revision = pack_end)
    {
      pack_start = packed_base_rev(fs, revision);
      pack_end = pack_start + pack_size(fs, revision);

      if (notify_func && (pack_start % ffd->max_files_per_dir == 0))
        notify_func(pack_start, notify_baton, iterpool);

      /* two-way index check */
      SVN_ERR(compare_l2p_to_p2l_index(fs, pack_start, pack_end - pack_start,
                                       cancel_func, cancel_baton, iterpool));
      SVN_ERR(compare_p2l_to_l2p_index(fs, pack_start, pack_end - pack_start,
                                       cancel_func, cancel_baton, iterpool));

      svn_pool_clear(iterpool);
    }

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__verify(svn_fs_t *fs,
                  svn_revnum_t start,
                  svn_revnum_t end,
                  svn_fs_progress_notify_func_t notify_func,
                  void *notify_baton,
                  svn_cancel_func_t cancel_func,
                  void *cancel_baton,
                  apr_pool_t *pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;
  svn_revnum_t youngest = ffd->youngest_rev_cache; /* cache is current */

  /* Input validation. */
  if (! SVN_IS_VALID_REVNUM(start))
    start = 0;
  if (! SVN_IS_VALID_REVNUM(end))
    end = youngest;
  SVN_ERR(svn_fs_fs__ensure_revision_exists(start, fs, pool));
  SVN_ERR(svn_fs_fs__ensure_revision_exists(end, fs, pool));

  /* log/phys index consistency.  We need to check them first to make
     sure we can access the rev / pack files in format7. */
  if (ffd->format >= SVN_FS_FS__MIN_LOG_ADDRESSING_FORMAT)
    SVN_ERR(verify_index_consistency(fs, start, end,
                                     notify_func, notify_baton,
                                     cancel_func, cancel_baton, pool));

  /* rep cache consistency */
  if (ffd->format >= SVN_FS_FS__MIN_REP_SHARING_FORMAT)
    SVN_ERR(verify_rep_cache(fs, start, end, notify_func, notify_baton,
                             cancel_func, cancel_baton, pool));

  return SVN_NO_ERROR;
}
