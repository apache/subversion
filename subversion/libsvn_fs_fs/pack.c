/* pack.c --- FSFS shard packing functionality
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
#include <assert.h>

#include "svn_pools.h"
#include "svn_dirent_uri.h"
#include "svn_sorts.h"
#include "private/svn_temp_serializer.h"
#include "private/svn_subr_private.h"
#include "private/svn_string_private.h"

#include "fs_fs.h"
#include "pack.h"
#include "util.h"
#include "id.h"
#include "index.h"
#include "low_level.h"
#include "revprops.h"
#include "transaction.h"

#include "../libsvn_fs/fs-loader.h"

#include "svn_private_config.h"
#include "temp_serializer.h"

/* This structure keeps track of all the temporary data and status that
 * needs to be kept around during the creation of one pack file.  After
 * each revision range (in case we can't process all revs at once due to
 * memory restrictions), parts of the data will get re-initialized.
 */
typedef struct pack_context_t
{
  /* file system that we operate on */
  svn_fs_t *fs;

  /* cancel function to invoke at regular intervals. May be NULL */
  svn_cancel_func_t cancel_func;

  /* baton to pass to CANCEL_FUNC */
  void *cancel_baton;

  /* first revision in the shard (and future pack file) */
  svn_revnum_t shard_rev;

  /* first revision in the range to process (>= SHARD_REV) */
  svn_revnum_t start_rev;

  /* first revision after the range to process (<= SHARD_END_REV) */
  svn_revnum_t end_rev;

  /* first revision after the current shard */
  svn_revnum_t shard_end_rev;

  /* log-to-phys proto index for the whole pack file */
  apr_file_t *proto_l2p_index;

  /* phys-to-log proto index for the whole pack file */
  apr_file_t *proto_p2l_index;

  /* full shard directory path (containing the unpacked revisions) */
  const char *shard_dir;

  /* full packed shard directory path (containing the pack file + indexes) */
  const char *pack_file_dir;

  /* full pack file path (including PACK_FILE_DIR) */
  const char *pack_file_path;

  /* current write position (i.e. file length) in the pack file */
  apr_off_t pack_offset;

  /* the pack file to ultimately write all data to */
  apr_file_t *pack_file;

  /* pool used for temporary data structures that will be cleaned up when
   * the next range of revisions is being processed */
  apr_pool_t *info_pool;
} pack_context_t;

/* Create and initialize a new pack context for packing shard SHARD_REV in
 * SHARD_DIR into PACK_FILE_DIR within filesystem FS.  Allocate it in POOL
 * and return the structure in *CONTEXT.
 *
 * Limit the number of items being copied per iteration to MAX_ITEMS.
 * Set CANCEL_FUNC and CANCEL_BATON as well.
 */
static svn_error_t *
initialize_pack_context(pack_context_t *context,
                        svn_fs_t *fs,
                        const char *pack_file_dir,
                        const char *shard_dir,
                        svn_revnum_t shard_rev,
                        svn_cancel_func_t cancel_func,
                        void *cancel_baton,
                        apr_pool_t *pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;
  const char *temp_dir;
  
  SVN_ERR_ASSERT(ffd->format >= SVN_FS_FS__MIN_LOG_ADDRESSING_FORMAT);
  SVN_ERR_ASSERT(shard_rev % ffd->max_files_per_dir == 0);
  
  /* where we will place our various temp files */
  SVN_ERR(svn_io_temp_dir(&temp_dir, pool));

  /* store parameters */
  context->fs = fs;
  context->cancel_func = cancel_func;
  context->cancel_baton = cancel_baton;

  context->shard_rev = shard_rev;
  context->start_rev = shard_rev;
  context->end_rev = shard_rev;
  context->shard_end_rev = shard_rev + ffd->max_files_per_dir;
  
  /* Create the new directory and pack file. */
  context->shard_dir = shard_dir;
  context->pack_file_dir = pack_file_dir;
  context->pack_file_path
    = svn_dirent_join(pack_file_dir, PATH_PACKED, pool);
  SVN_ERR(svn_io_file_open(&context->pack_file, context->pack_file_path,
                           APR_WRITE | APR_BUFFERED | APR_BINARY | APR_EXCL
                             | APR_CREATE, APR_OS_DEFAULT, pool));

  /* Proto index files */
  SVN_ERR(svn_fs_fs__l2p_proto_index_open
            (&context->proto_l2p_index,
             svn_dirent_join(pack_file_dir,
                             PATH_INDEX PATH_EXT_L2P_INDEX,
                             pool),
             pool));
  SVN_ERR(svn_fs_fs__p2l_proto_index_open
            (&context->proto_p2l_index,
             svn_dirent_join(pack_file_dir,
                             PATH_INDEX PATH_EXT_P2L_INDEX,
                             pool),
             pool));

  /* the pool used for temp structures */
  context->info_pool = svn_pool_create(pool);

  return SVN_NO_ERROR;
};

/* Clean up / free all revision range specific data and files in CONTEXT.
 * Use POOL for temporary allocations.
 */
static svn_error_t *
reset_pack_context(pack_context_t *context,
                   apr_pool_t *pool)
{
  svn_pool_clear(context->info_pool);
  
  return SVN_NO_ERROR;
};

/* Call this after the last revision range.  It will finalize all index files
 * for CONTEXT and close any open files.  Use POOL for temporary allocations.
 */
static svn_error_t *
close_pack_context(pack_context_t *context,
                   apr_pool_t *pool)
{
  const char *l2p_index_path
    = apr_pstrcat(pool, context->pack_file_path, PATH_EXT_L2P_INDEX, NULL);
  const char *p2l_index_path
    = apr_pstrcat(pool, context->pack_file_path, PATH_EXT_P2L_INDEX, NULL);
  const char *proto_l2p_index_path;
  const char *proto_p2l_index_path;

  /* need the file names for the actual index creation call further down */
  SVN_ERR(svn_io_file_name_get(&proto_l2p_index_path,
                               context->proto_l2p_index, pool));
  SVN_ERR(svn_io_file_name_get(&proto_p2l_index_path,
                               context->proto_p2l_index, pool));
  
  /* finalize proto index files */
  SVN_ERR(svn_io_file_close(context->proto_l2p_index, pool));
  SVN_ERR(svn_io_file_close(context->proto_p2l_index, pool));

  /* Create the actual index files*/
  SVN_ERR(svn_fs_fs__l2p_index_create(context->fs, l2p_index_path,
                                      proto_l2p_index_path,
                                      context->shard_rev, pool));
  SVN_ERR(svn_fs_fs__p2l_index_create(context->fs, p2l_index_path,
                                      proto_p2l_index_path,
                                      context->shard_rev, pool));

  /* remove proto index files */
  SVN_ERR(svn_io_remove_file2(proto_l2p_index_path, FALSE, pool));
  SVN_ERR(svn_io_remove_file2(proto_p2l_index_path, FALSE, pool));

  SVN_ERR(svn_io_file_close(context->pack_file, pool));

  return SVN_NO_ERROR;
};

/* Efficiently copy SIZE bytes from SOURCE to DEST.  Invoke the CANCEL_FUNC
 * from CONTEXT at regular intervals.  Use POOL for allocations.
 */
static svn_error_t *
copy_file_data(pack_context_t *context,
               apr_file_t *dest,
               apr_file_t *source,
               apr_off_t size,
               apr_pool_t *pool)
{
  /* most non-representation items will be small.  Minimize the buffer
   * and infrastructure overhead in that case. */
  enum { STACK_BUFFER_SIZE = 1024 };
 
  if (size < STACK_BUFFER_SIZE)
    {
      /* copy small data using a fixed-size buffer on stack */
      char buffer[STACK_BUFFER_SIZE];
      SVN_ERR(svn_io_file_read_full2(source, buffer, (apr_size_t)size,
                                     NULL, NULL, pool));
      SVN_ERR(svn_io_file_write_full(dest, buffer, (apr_size_t)size,
                                     NULL, pool));
    }
  else
    {
      /* use streaming copies for larger data blocks.  That may require
       * the allocation of larger buffers and we should make sure that
       * this extra memory is released asap. */
      fs_fs_data_t *ffd = context->fs->fsap_data;
      apr_pool_t *copypool = svn_pool_create(pool);
      char *buffer = apr_palloc(copypool, ffd->block_size);

      while (size)
        {
          apr_size_t to_copy = (apr_size_t)(MIN(size, ffd->block_size));
          if (context->cancel_func)
            SVN_ERR(context->cancel_func(context->cancel_baton));

          SVN_ERR(svn_io_file_read_full2(source, buffer, to_copy,
                                         NULL, NULL, pool));
          SVN_ERR(svn_io_file_write_full(dest, buffer, to_copy,
                                         NULL, pool));

          size -= to_copy;
        }

      svn_pool_destroy(copypool);
    }

  return SVN_NO_ERROR;
}

/* Directories entries sorted by revision (decreasing - to max cache hits)
 * and offset (increasing - to max benefit from APR file buffering).
 */
static int
compare_dir_entries_format6(const svn_sort__item_t *a,
                            const svn_sort__item_t *b)
{
  const svn_fs_dirent_t *lhs = (const svn_fs_dirent_t *) a->value;
  const svn_fs_dirent_t *rhs = (const svn_fs_dirent_t *) b->value;

  const svn_fs_fs__id_part_t *lhs_rev_item
    = svn_fs_fs__id_rev_item(lhs->id);
  const svn_fs_fs__id_part_t *rhs_rev_item
    = svn_fs_fs__id_rev_item(rhs->id);

  /* decreasing ("reverse") order on revs */
  if (lhs_rev_item->revision != rhs_rev_item->revision)
    return lhs_rev_item->revision > rhs_rev_item->revision ? -1 : 1;

  /* increasing order on offsets */
  if (lhs_rev_item->number != rhs_rev_item->number)
    return lhs_rev_item->number > rhs_rev_item->number ? 1 : -1;

  return 0;
}

apr_array_header_t *
svn_fs_fs__order_dir_entries(svn_fs_t *fs,
                             apr_hash_t *directory,
                             apr_pool_t *pool)
{
  apr_array_header_t *ordered
    = svn_sort__hash(directory, compare_dir_entries_format6, pool);

  apr_array_header_t *result
    = apr_array_make(pool, ordered->nelts, sizeof(svn_fs_dirent_t *));

  int i;
  for (i = 0; i < ordered->nelts; ++i)
    APR_ARRAY_PUSH(result, svn_fs_dirent_t *)
      = APR_ARRAY_IDX(ordered, i, svn_sort__item_t).value;

  return result;
}

/* Append CONTEXT->START_REV to the context's pack file with no re-ordering.
 * This function will only be used for very large revisions (>>100k changes).
 * Use POOL for temporary allocations.
 */
static svn_error_t *
append_revision(pack_context_t *context,
                apr_pool_t *pool)
{
  apr_off_t offset = 0;
  apr_pool_t *iterpool = svn_pool_create(pool);
  apr_file_t *rev_file;
  apr_finfo_t finfo;

  /* Get the size of the file. */
  const char *path = svn_dirent_join(context->shard_dir,
                                     apr_psprintf(iterpool, "%ld",
                                                  context->start_rev),
                                     pool);
  SVN_ERR(svn_io_stat(&finfo, path, APR_FINFO_SIZE, pool));

  /* Copy all the bits from the rev file to the end of the pack file. */
  SVN_ERR(svn_io_file_open(&rev_file, path,
                           APR_READ | APR_BUFFERED | APR_BINARY,
                           APR_OS_DEFAULT, pool));
  SVN_ERR(copy_file_data(context, context->pack_file, rev_file, finfo.size, 
                         iterpool));

  /* mark the start of a new revision */
  SVN_ERR(svn_fs_fs__l2p_proto_index_add_revision(context->proto_l2p_index,
                                                  pool));

  /* read the phys-to-log index file until we covered the whole rev file.
   * That index contains enough info to build both target indexes from it. */
  while (offset < finfo.size)
    {
      /* read one cluster */
      int i;
      apr_array_header_t *entries;
      SVN_ERR(svn_fs_fs__p2l_index_lookup(&entries, context->fs,
                                          context->start_rev, offset,
                                          iterpool));

      for (i = 0; i < entries->nelts; ++i)
        {
          svn_fs_fs__p2l_entry_t *entry
            = &APR_ARRAY_IDX(entries, i, svn_fs_fs__p2l_entry_t);

          /* skip first entry if that was duplicated due crossing a
             cluster boundary */
          if (offset > entry->offset)
            continue;

          /* process entry while inside the rev file */
          offset = entry->offset;
          if (offset < finfo.size)
            {
              entry->offset += context->pack_offset;
              offset += entry->size;
              SVN_ERR(svn_fs_fs__l2p_proto_index_add_entry
                        (context->proto_l2p_index, entry->offset,
                         entry->item.number, iterpool));
              SVN_ERR(svn_fs_fs__p2l_proto_index_add_entry
                        (context->proto_p2l_index, entry, iterpool));
            }
        }

      svn_pool_clear(iterpool);
    }

  svn_pool_destroy(iterpool);
  context->pack_offset += finfo.size;

  return SVN_NO_ERROR;
}

/* Logical addressing mode packing logic.
 *
 * Pack the revision shard starting at SHARD_REV in filesystem FS from
 * SHARD_DIR into the PACK_FILE_DIR, using POOL for allocations.  Limit
 * the extra memory consumption to MAX_MEM bytes.  CANCEL_FUNC and
 * CANCEL_BATON are what you think they are.
 */
static svn_error_t *
pack_log_addressed(svn_fs_t *fs,
                   const char *pack_file_dir,
                   const char *shard_dir,
                   svn_revnum_t shard_rev,
                   svn_cancel_func_t cancel_func,
                   void *cancel_baton,
                   apr_pool_t *pool)
{
  pack_context_t context = { 0 };
  svn_revnum_t rev;
  apr_pool_t *iterpool = svn_pool_create(pool);

  /* set up a pack context */
  SVN_ERR(initialize_pack_context(&context, fs, pack_file_dir, shard_dir,
                                  shard_rev, cancel_func,
                                  cancel_baton, pool));

  /* pack revisions in ranges that don't exceed MAX_MEM */
  for (rev = context.shard_rev; rev < context.shard_end_rev; ++rev)
    {
      context.start_rev = rev;
      context.end_rev = rev + 1;

      SVN_ERR(append_revision(&context, iterpool));

      svn_pool_clear(iterpool);
    }

  /* last phase: finalize indexes and clean up */
  SVN_ERR(reset_pack_context(&context, iterpool));
  SVN_ERR(close_pack_context(&context, iterpool));
  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

/* Given REV in FS, set *REV_OFFSET to REV's offset in the packed file.
   Use POOL for temporary allocations. */
svn_error_t *
svn_fs_fs__get_packed_offset(apr_off_t *rev_offset,
                             svn_fs_t *fs,
                             svn_revnum_t rev,
                             apr_pool_t *pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;
  svn_stream_t *manifest_stream;
  svn_boolean_t is_cached;
  svn_revnum_t shard;
  apr_int64_t shard_pos;
  apr_array_header_t *manifest;
  apr_pool_t *iterpool;

  shard = rev / ffd->max_files_per_dir;

  /* position of the shard within the manifest */
  shard_pos = rev % ffd->max_files_per_dir;

  /* fetch exactly that element into *rev_offset, if the manifest is found
     in the cache */
  SVN_ERR(svn_cache__get_partial((void **) rev_offset, &is_cached,
                                 ffd->packed_offset_cache, &shard,
                                 svn_fs_fs__get_sharded_offset, &shard_pos,
                                 pool));

  if (is_cached)
      return SVN_NO_ERROR;

  /* Open the manifest file. */
  SVN_ERR(svn_stream_open_readonly(&manifest_stream,
                                   svn_fs_fs__path_rev_packed(fs, rev,
                                                              PATH_MANIFEST,
                                                              pool),
                                   pool, pool));

  /* While we're here, let's just read the entire manifest file into an array,
     so we can cache the entire thing. */
  iterpool = svn_pool_create(pool);
  manifest = apr_array_make(pool, ffd->max_files_per_dir, sizeof(apr_off_t));
  while (1)
    {
      svn_boolean_t eof;
      apr_int64_t val;

      svn_pool_clear(iterpool);
      SVN_ERR(svn_fs_fs__read_number_from_stream(&val, &eof, manifest_stream,
                                                 iterpool));
      if (eof)
        break;

      APR_ARRAY_PUSH(manifest, apr_off_t) = (apr_off_t)val;
    }
  svn_pool_destroy(iterpool);

  *rev_offset = APR_ARRAY_IDX(manifest, rev % ffd->max_files_per_dir,
                              apr_off_t);

  /* Close up shop and cache the array. */
  SVN_ERR(svn_stream_close(manifest_stream));
  return svn_cache__set(ffd->packed_offset_cache, &shard, manifest, pool);
}

/* Packing logic for physical addresssing mode:
 * Simply concatenate all revision contents.
 * 
 * Pack the revision shard starting at SHARD_REV containing exactly
 * MAX_FILES_PER_DIR revisions from SHARD_PATH into the PACK_FILE_DIR,
 * using POOL for allocations.  CANCEL_FUNC and CANCEL_BATON are what you
 * think they are.
 */
static svn_error_t *
pack_phys_addressed(const char *pack_file_dir,
                    const char *shard_path,
                    svn_revnum_t start_rev,
                    int max_files_per_dir,
                    svn_cancel_func_t cancel_func,
                    void *cancel_baton,
                    apr_pool_t *pool)
{
  const char *pack_file_path, *manifest_file_path;
  svn_stream_t *pack_stream, *manifest_stream;
  svn_revnum_t end_rev, rev;
  apr_off_t next_offset;
  apr_pool_t *iterpool;

  /* Some useful paths. */
  pack_file_path = svn_dirent_join(pack_file_dir, PATH_PACKED, pool);
  manifest_file_path = svn_dirent_join(pack_file_dir, PATH_MANIFEST, pool);

  /* Create the new directory and pack file. */
  SVN_ERR(svn_stream_open_writable(&pack_stream, pack_file_path, pool,
                                    pool));

  /* Create the manifest file. */
  SVN_ERR(svn_stream_open_writable(&manifest_stream, manifest_file_path,
                                   pool, pool));

  end_rev = start_rev + max_files_per_dir - 1;
  next_offset = 0;
  iterpool = svn_pool_create(pool);

  /* Iterate over the revisions in this shard, squashing them together. */
  for (rev = start_rev; rev <= end_rev; rev++)
    {
      svn_stream_t *rev_stream;
      apr_finfo_t finfo;
      const char *path;

      svn_pool_clear(iterpool);

      /* Get the size of the file. */
      path = svn_dirent_join(shard_path, apr_psprintf(iterpool, "%ld", rev),
                             iterpool);
      SVN_ERR(svn_io_stat(&finfo, path, APR_FINFO_SIZE, iterpool));

      /* build manifest */
      SVN_ERR(svn_stream_printf(manifest_stream, iterpool,
                                "%" APR_OFF_T_FMT "\n", next_offset));
      next_offset += finfo.size;

      /* Copy all the bits from the rev file to the end of the pack file. */
      SVN_ERR(svn_stream_open_readonly(&rev_stream, path, iterpool, iterpool));
      SVN_ERR(svn_stream_copy3(rev_stream, svn_stream_disown(pack_stream,
                                                             iterpool),
                               cancel_func, cancel_baton, iterpool));
    }

  /* disallow write access to the manifest file */
  SVN_ERR(svn_stream_close(manifest_stream));
  SVN_ERR(svn_io_set_file_read_only(manifest_file_path, FALSE, iterpool));

  SVN_ERR(svn_stream_close(pack_stream));

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

/* In filesystem FS, pack the revision SHARD containing exactly
 * MAX_FILES_PER_DIR revisions from SHARD_PATH into the PACK_FILE_DIR,
 * using POOL for allocations.  Try to limit the amount of temporary
 * memory needed to MAX_MEM bytes.  CANCEL_FUNC and CANCEL_BATON are what
 * you think they are.
 *
 * If for some reason we detect a partial packing already performed, we
 * remove the pack file and start again.
 *
 * The actual packing will be done in a format-specific sub-function.
 */
static svn_error_t *
pack_rev_shard(svn_fs_t *fs,
               const char *pack_file_dir,
               const char *shard_path,
               apr_int64_t shard,
               int max_files_per_dir,
               apr_size_t max_mem,
               svn_cancel_func_t cancel_func,
               void *cancel_baton,
               apr_pool_t *pool)
{
  const char *pack_file_path;
  svn_revnum_t shard_rev = (svn_revnum_t) (shard * max_files_per_dir);

  /* Some useful paths. */
  pack_file_path = svn_dirent_join(pack_file_dir, PATH_PACKED, pool);

  /* Remove any existing pack file for this shard, since it is incomplete. */
  SVN_ERR(svn_io_remove_dir2(pack_file_dir, TRUE, cancel_func, cancel_baton,
                             pool));

  /* Create the new directory and pack file. */
  SVN_ERR(svn_io_dir_make(pack_file_dir, APR_OS_DEFAULT, pool));

  /* Index information files */
  if (svn_fs_fs__use_log_addressing(fs, shard_rev))
    SVN_ERR(pack_log_addressed(fs, pack_file_dir, shard_path, shard_rev,
                               cancel_func, cancel_baton, pool));
  else
    SVN_ERR(pack_phys_addressed(pack_file_dir, shard_path, shard_rev,
                                max_files_per_dir, cancel_func,
                                cancel_baton, pool));
  
  SVN_ERR(svn_io_copy_perms(shard_path, pack_file_dir, pool));
  SVN_ERR(svn_io_set_file_read_only(pack_file_path, FALSE, pool));

  return SVN_NO_ERROR;
}

/* In the file system at FS_PATH, pack the SHARD in REVS_DIR and
 * REVPROPS_DIR containing exactly MAX_FILES_PER_DIR revisions, using POOL
 * for allocations.  REVPROPS_DIR will be NULL if revprop packing is not
 * supported.  COMPRESSION_LEVEL and MAX_PACK_SIZE will be ignored in that
 * case.
 *
 * CANCEL_FUNC and CANCEL_BATON are what you think they are; similarly
 * NOTIFY_FUNC and NOTIFY_BATON.
 *
 * If for some reason we detect a partial packing already performed, we
 * remove the pack file and start again.
 */
static svn_error_t *
pack_shard(const char *revs_dir,
           const char *revsprops_dir,
           svn_fs_t *fs,
           apr_int64_t shard,
           int max_files_per_dir,
           apr_off_t max_pack_size,
           int compression_level,
           svn_fs_pack_notify_t notify_func,
           void *notify_baton,
           svn_cancel_func_t cancel_func,
           void *cancel_baton,
           apr_pool_t *pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;
  const char *rev_shard_path, *rev_pack_file_dir;
  const char *revprops_shard_path, *revprops_pack_file_dir;

  /* Notify caller we're starting to pack this shard. */
  if (notify_func)
    SVN_ERR(notify_func(notify_baton, shard, svn_fs_pack_notify_start,
                        pool));

  /* Some useful paths. */
  rev_pack_file_dir = svn_dirent_join(revs_dir,
                  apr_psprintf(pool,
                               "%" APR_INT64_T_FMT PATH_EXT_PACKED_SHARD,
                               shard),
                  pool);
  rev_shard_path = svn_dirent_join(revs_dir,
                           apr_psprintf(pool, "%" APR_INT64_T_FMT, shard),
                           pool);

  /* pack the revision content */
  SVN_ERR(pack_rev_shard(fs, rev_pack_file_dir, rev_shard_path,
                         shard, max_files_per_dir, 64 * 1024 * 1024,
                         cancel_func, cancel_baton, pool));

  /* if enabled, pack the revprops in an equivalent way */
  if (revsprops_dir)
    {
      revprops_pack_file_dir = svn_dirent_join(revsprops_dir,
                   apr_psprintf(pool,
                                "%" APR_INT64_T_FMT PATH_EXT_PACKED_SHARD,
                                shard),
                   pool);
      revprops_shard_path = svn_dirent_join(revsprops_dir,
                           apr_psprintf(pool, "%" APR_INT64_T_FMT, shard),
                           pool);

      SVN_ERR(svn_fs_fs__pack_revprops_shard(revprops_pack_file_dir,
                                             revprops_shard_path,
                                             shard, max_files_per_dir,
                                             (int)(0.9 * max_pack_size),
                                             compression_level,
                                             cancel_func, cancel_baton,
                                             pool));
    }

  /* Update the min-unpacked-rev file to reflect our newly packed shard. */
  SVN_ERR(svn_fs_fs__write_min_unpacked_rev(fs,
                          (svn_revnum_t)((shard + 1) * max_files_per_dir),
                          pool));
  ffd->min_unpacked_rev = (svn_revnum_t)((shard + 1) * max_files_per_dir);

  /* Finally, remove the existing shard directories.
   * For revprops, clean up older obsolete shards as well as they might
   * have been left over from an interrupted FS upgrade. */
  SVN_ERR(svn_io_remove_dir2(rev_shard_path, TRUE,
                             cancel_func, cancel_baton, pool));
  if (revsprops_dir)
    {
      svn_node_kind_t kind = svn_node_dir;
      apr_int64_t to_cleanup = shard;
      do
        {
          SVN_ERR(svn_fs_fs__delete_revprops_shard(revprops_shard_path,
                                                   to_cleanup,
                                                   max_files_per_dir,
                                                   cancel_func,
                                                   cancel_baton,
                                                   pool));

          /* If the previous shard exists, clean it up as well.
             Don't try to clean up shard 0 as it we can't tell quickly
             whether it actually needs cleaning up. */
          revprops_shard_path = svn_dirent_join(revsprops_dir,
                      apr_psprintf(pool, "%" APR_INT64_T_FMT, --to_cleanup),
                      pool);
          SVN_ERR(svn_io_check_path(revprops_shard_path, &kind, pool));
        }
      while (kind == svn_node_dir && to_cleanup > 0);
    }

  /* Notify caller we're starting to pack this shard. */
  if (notify_func)
    SVN_ERR(notify_func(notify_baton, shard, svn_fs_pack_notify_end,
                        pool));

  return SVN_NO_ERROR;
}

struct pack_baton
{
  svn_fs_t *fs;
  svn_fs_pack_notify_t notify_func;
  void *notify_baton;
  svn_cancel_func_t cancel_func;
  void *cancel_baton;
};


/* The work-horse for svn_fs_fs__pack, called with the FS write lock.
   This implements the svn_fs_fs__with_write_lock() 'body' callback
   type.  BATON is a 'struct pack_baton *'.

   WARNING: if you add a call to this function, please note:
     The code currently assumes that any piece of code running with
     the write-lock set can rely on the ffd->min_unpacked_rev and
     ffd->min_unpacked_revprop caches to be up-to-date (and, by
     extension, on not having to use a retry when calling
     svn_fs_fs__path_rev_absolute() and friends).  If you add a call
     to this function, consider whether you have to call
     svn_fs_fs__update_min_unpacked_rev().
     See this thread: http://thread.gmane.org/1291206765.3782.3309.camel@edith
 */
static svn_error_t *
pack_body(void *baton,
          apr_pool_t *pool)
{
  struct pack_baton *pb = baton;
  fs_fs_data_t *ffd = pb->fs->fsap_data;
  apr_int64_t completed_shards;
  apr_int64_t i;
  svn_revnum_t youngest;
  apr_pool_t *iterpool;
  const char *rev_data_path;
  const char *revprops_data_path = NULL;

  /* If the repository isn't a new enough format, we don't support packing.
     Return a friendly error to that effect. */
  if (ffd->format < SVN_FS_FS__MIN_PACKED_FORMAT)
    return svn_error_createf(SVN_ERR_UNSUPPORTED_FEATURE, NULL,
      _("FSFS format (%d) too old to pack; please upgrade the filesystem."),
      ffd->format);

  /* If we aren't using sharding, we can't do any packing, so quit. */
  if (!ffd->max_files_per_dir)
    return SVN_NO_ERROR;

  SVN_ERR(svn_fs_fs__read_min_unpacked_rev(&ffd->min_unpacked_rev, pb->fs,
                                           pool));

  SVN_ERR(svn_fs_fs__youngest_rev(&youngest, pb->fs, pool));
  completed_shards = (youngest + 1) / ffd->max_files_per_dir;

  /* See if we've already completed all possible shards thus far. */
  if (ffd->min_unpacked_rev == (completed_shards * ffd->max_files_per_dir))
    return SVN_NO_ERROR;

  rev_data_path = svn_dirent_join(pb->fs->path, PATH_REVS_DIR, pool);
  if (ffd->format >= SVN_FS_FS__MIN_PACKED_REVPROP_FORMAT)
    revprops_data_path = svn_dirent_join(pb->fs->path, PATH_REVPROPS_DIR,
                                         pool);

  iterpool = svn_pool_create(pool);
  for (i = ffd->min_unpacked_rev / ffd->max_files_per_dir;
       i < completed_shards;
       i++)
    {
      svn_pool_clear(iterpool);

      if (pb->cancel_func)
        SVN_ERR(pb->cancel_func(pb->cancel_baton));

      SVN_ERR(pack_shard(rev_data_path, revprops_data_path,
                         pb->fs, i, ffd->max_files_per_dir,
                         ffd->revprop_pack_size,
                         ffd->compress_packed_revprops
                           ? SVN__COMPRESSION_ZLIB_DEFAULT
                           : SVN__COMPRESSION_NONE,
                         pb->notify_func, pb->notify_baton,
                         pb->cancel_func, pb->cancel_baton, iterpool));
    }

  svn_pool_destroy(iterpool);
  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__pack(svn_fs_t *fs,
                svn_fs_pack_notify_t notify_func,
                void *notify_baton,
                svn_cancel_func_t cancel_func,
                void *cancel_baton,
                apr_pool_t *pool)
{
  struct pack_baton pb = { 0 };
  pb.fs = fs;
  pb.notify_func = notify_func;
  pb.notify_baton = notify_baton;
  pb.cancel_func = cancel_func;
  pb.cancel_baton = cancel_baton;
  return svn_fs_fs__with_write_lock(fs, pack_body, &pb, pool);
}
