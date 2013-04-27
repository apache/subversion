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

#include "fs_fs.h"
#include "pack.h"
#include "util.h"
#include "revprops.h"
#include "transaction.h"
#include "index.h"
#include "low_level.h"
#include "cached_data.h"
#include "changes.h"
#include "noderevs.h"

#include "../libsvn_fs/fs-loader.h"

#include "svn_private_config.h"
#include "temp_serializer.h"

/* Format 7 packing logic:
 *
 * We pack files on a pack file basis (e.g. 1000 revs) without changing
 * existing pack files nor the revision files outside the range to pack.
 *
 * First, we will scan the revision file indexes to determine the number
 * of items to "place" (i.e. determine their optimal position within the
 * future pack file).  For each item, we will need a constant amount of
 * memory to track it.  A MAX_MEM parameter sets a limit to the number of
 * items we may place in one go.  That means, we may not be able to add
 * all revisions at once.  Instead, we will run the placement for a subset
 * of revisions at a time.  T very unlikely worst case will simply append
 * all revision data with just a little reshuffling inside each revision.
 *
 * In a second step, we read all revisions in the selected range, build
 * the item tracking information and copy the items themselves from the
 * revision files to temporary files.  The latter serve as buckets for a
 * very coarse bucket presort:  Separate change lists, file properties,
 * directory properties and noderevs + representations from one another.
 *
 * The third step will determine an optimized placement for the items in
 * each of the 4 buckets separately.  The first three will simply order
 * their items by revision, starting with the newest once.  Placing rep
 * and noderev items is a more elaborate process documented in the code.
 *
 * Step 4 copies the items from the temporary buckets into the final
 * pack file and write the temporary index files.
 *
 * Finally, after the last range of revisions, create the final indexes.
 */

/* Structure tracking the relations / dependencies between items
 * (noderevs and representations only).
 */
typedef struct rep_info_t
{
  /* item being tracked. Will be set to NULL after being copied from
   * the temp file to the pack file */
  struct svn_fs_fs__p2l_entry_t *entry;

  /* to create the contents of the item, this base item needs to be
   * read as well.  So, place it near the current item.  May be NULL.
   * For noderevs, that is the data representation; for representations,
   * this will be the delta base. */
  struct rep_info_t *base;

  /* given a typical tree traversal, this item will probably be requested
   * soon after ENTRY.  So, place it near the current item.  May be NULL.
   * If this is set on a noderev item, it links to a sibbling.  On a
   * representation item, it links to sub-directory entries. */
  struct rep_info_t *next;
} rep_info_t;

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

  /* array of svn_fs_fs__p2l_entry_t *, all referring to change lists.
   * Will be filled in phase 2 and be cleared after each revision range. */
  apr_array_header_t *changes;

  /* temp file receiving all change list items (referenced by CHANGES).
   * Will be filled in phase 2 and be cleared after each revision range. */
  apr_file_t *changes_file;

  /* array of svn_fs_fs__p2l_entry_t *, all referring to file properties.
   * Will be filled in phase 2 and be cleared after each revision range. */
  apr_array_header_t *file_props;

  /* temp file receiving all file prop items (referenced by FILE_PROPS).
   * Will be filled in phase 2 and be cleared after each revision range.*/
  apr_file_t *file_props_file;

  /* array of svn_fs_fs__p2l_entry_t *, all referring to directory properties.
   * Will be filled in phase 2 and be cleared after each revision range. */
  apr_array_header_t *dir_props;

  /* temp file receiving all directory prop items (referenced by DIR_PROPS).
   * Will be filled in phase 2 and be cleared after each revision range.*/
  apr_file_t *dir_props_file;
  
  /* array of rep_info_t *, all their ENTRYs referring to node revisions or
   * representations. Index is be REV_OFFSETS[rev - START_REV] + item offset.
   * Some entries will be NULL.  Will be filled in phase 2 and be cleared
   * after each revision range. */
  apr_array_header_t *reps_infos;

  /* array of int, marking for each revision, the which offset their items
   * begin in REP_INFOS.  Will be filled in phase 2 and be cleared after
   * each revision range. */
  apr_array_header_t *rev_offsets;

  /* array of svn_fs_fs__p2l_entry_t* from REPS_INFOS, ordered according to
   * our placement strategy.  Will be filled in phase 2 and be cleared after
   * each revision range. */
  apr_array_header_t *reps;

  /* temp file receiving all items referenced by REPS_INFOS.
   * Will be filled in phase 2 and be cleared after each revision range.*/
  apr_file_t *reps_file;

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
                        apr_size_t max_items,
                        svn_cancel_func_t cancel_func,
                        void *cancel_baton,
                        apr_pool_t *pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;
  const char *temp_dir;
  apr_size_t max_revs = MIN(ffd->max_files_per_dir, (int)max_items);
  
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

  /* item buckets: one item info array and one temp file per bucket */
  context->changes = apr_array_make(pool, max_items,
                                    sizeof(svn_fs_fs__p2l_entry_t *));
  SVN_ERR(svn_io_open_unique_file3(&context->changes_file, NULL, temp_dir,
                                   svn_io_file_del_on_close, pool, pool));
  context->file_props = apr_array_make(pool, max_items,
                                       sizeof(svn_fs_fs__p2l_entry_t *));
  SVN_ERR(svn_io_open_unique_file3(&context->file_props_file, NULL, temp_dir,
                                   svn_io_file_del_on_close, pool, pool));
  context->dir_props = apr_array_make(pool, max_items,
                                      sizeof(svn_fs_fs__p2l_entry_t *));
  SVN_ERR(svn_io_open_unique_file3(&context->dir_props_file, NULL, temp_dir,
                                   svn_io_file_del_on_close, pool, pool));

  /* noderev and representation item bucket */
  context->rev_offsets = apr_array_make(pool, max_revs, sizeof(int));
  context->reps_infos = apr_array_make(pool, max_items, sizeof(rep_info_t *));
  context->reps = apr_array_make(pool, max_items,
                                 sizeof(svn_fs_fs__p2l_entry_t *));
  SVN_ERR(svn_io_open_unique_file3(&context->reps_file, NULL, temp_dir,
                                   svn_io_file_del_on_close, pool, pool));

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
  apr_array_clear(context->changes);
  SVN_ERR(svn_io_file_trunc(context->changes_file, 0, pool));
  apr_array_clear(context->file_props);
  SVN_ERR(svn_io_file_trunc(context->file_props_file, 0, pool));
  apr_array_clear(context->dir_props);
  SVN_ERR(svn_io_file_trunc(context->dir_props_file, 0, pool));

  apr_array_clear(context->rev_offsets);
  apr_array_clear(context->reps_infos);
  apr_array_clear(context->reps);
  SVN_ERR(svn_io_file_trunc(context->reps_file, 0, pool));

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

/* Writes SIZE bytes, all 0, to DEST.  Uses POOL for allocations.
 */
static svn_error_t *
write_null_bytes(apr_file_t *dest,
                 apr_off_t size,
                 apr_pool_t *pool)
{
  /* Have a collection of high-quality, easy to access NUL bytes handy. */
  enum { BUFFER_SIZE = 1024 };
  static const char buffer[BUFFER_SIZE] = { 0 };

  /* copy SIZE of them into the file's buffer */
  while (size)
    {
      apr_size_t to_write = MIN(size, BUFFER_SIZE);
      SVN_ERR(svn_io_file_write_full(dest, buffer, to_write, NULL, pool));
      size -= to_write;
    }

  return SVN_NO_ERROR;
}

/* Copy the "simple" item (changes list or property representation) from
 * the current position in REV_FILE to TEMP_FILE using CONTEXT.  Add a
 * copy of ENTRY to ENTRIES but with an updated offset value that points
 * to the copy destination in TEMP_FILE.  Use POOL for allocations.
 */
static svn_error_t *
copy_item_to_temp(pack_context_t *context,
                  apr_array_header_t *entries,
                  apr_file_t *temp_file,
                  apr_file_t *rev_file,
                  svn_fs_fs__p2l_entry_t *entry,
                  apr_pool_t *pool)
{
  svn_fs_fs__p2l_entry_t *new_entry
    = svn_fs_fs__p2l_entry_dup(entry, context->info_pool);
  new_entry->offset = 0;
  SVN_ERR(svn_io_file_seek(temp_file, SEEK_CUR, &new_entry->offset, pool));
  APR_ARRAY_PUSH(entries, svn_fs_fs__p2l_entry_t *) = new_entry;
  
  SVN_ERR(copy_file_data(context, temp_file, rev_file, entry->size, pool));
  
  return SVN_NO_ERROR;
}

/* Return the offset within CONTEXT->REPS_INFOS that corresponds to item
 * ITEM_INDEX in  REVISION.
 */
static int
get_item_array_index(pack_context_t *context,
                     svn_revnum_t revision,
                     apr_int64_t item_index)
{
  assert(revision >= context->start_rev);
  return (int)item_index + APR_ARRAY_IDX(context->rev_offsets,
                                         revision - context->start_rev,
                                         int);
}

/* Write INFO to the correct position in CONTEXT->REP_INFOS.  The latter
 * may need auto-expanding.  Overwriting an array element is not allowed.
 */
static void
add_item_rep_mapping(pack_context_t *context,
                     rep_info_t *info)
{
  apr_uint32_t i;
  for (i = 0; i < info->entry->item_count; ++i)
    {
      /* index of INFO */
      int idx = get_item_array_index(context,
                                     info->entry->items[i].revision,
                                     info->entry->items[i].number);

      /* make sure the index exists in the array */
      while (context->reps_infos->nelts <= idx)
        APR_ARRAY_PUSH(context->reps_infos, rep_info_t *) = NULL;

      /* set the element.  If there is already an entry, there are probably
       * two items claiming to be the same -> bail out */
      assert(!APR_ARRAY_IDX(context->reps_infos, idx, rep_info_t *));
      APR_ARRAY_IDX(context->reps_infos, idx, rep_info_t *) = info;
    }
}

/* Copy representation item identified by ENTRY from the current position
 * in REV_FILE into CONTEXT->REPS_FILE.  Add all tracking into needed by
 * our placement algorithm to CONTEXT.  Use POOL for temporary allocations.
 */
static svn_error_t *
copy_rep_to_temp(pack_context_t *context,
                 apr_file_t *rev_file,
                 svn_fs_fs__p2l_entry_t *entry,
                 apr_pool_t *pool)
{
  rep_info_t *rep_info = apr_pcalloc(context->info_pool, sizeof(*rep_info));
  svn_fs_fs__rep_header_t *rep_header;
  svn_stream_t *stream;

  /* create a copy of ENTRY, make it point to the copy destination and
   * store it in CONTEXT */
  rep_info->entry = svn_fs_fs__p2l_entry_dup(entry, context->info_pool);
  rep_info->entry->offset = 0;
  SVN_ERR(svn_io_file_seek(context->reps_file, SEEK_CUR,
                           &rep_info->entry->offset, pool));
  add_item_rep_mapping(context, rep_info);

  /* read & parse the representation header */
  stream = svn_stream_from_aprfile2(rev_file, TRUE, pool);
  SVN_ERR(svn_fs_fs__read_rep_header(&rep_header, stream, pool));
  svn_stream_close(stream);

  /* if the representation is a delta against some other rep, link the two */
  if (   rep_header->is_delta
      && !rep_header->is_delta_vs_empty
      && rep_header->base_revision >= context->start_rev)
    {
      int idx = get_item_array_index(context, rep_header->base_revision,
                                       rep_header->base_item_index);
      if (idx < context->reps_infos->nelts)
        rep_info->base = APR_ARRAY_IDX(context->reps_infos, idx, rep_info_t *);
    }

  /* copy the whole rep (including header!) to our temp file */
  SVN_ERR(svn_io_file_seek(rev_file, SEEK_SET, &entry->offset, pool));
  SVN_ERR(copy_file_data(context, context->reps_file, rev_file, entry->size,
                         pool));

  return SVN_NO_ERROR;
}

/* Directories first, dirs / files sorted by name in reverse lexical order.
 * This maximizes the chance of two items being located close to one another
 * in *all* pack files independent of their change order.  It also groups
 * multi-project repos nicely according to their sub-projects.  The reverse
 * order aspect gives "trunk" preference over "tags" and "branches", so
 * trunk-related items are more likely to be contiguous.
 */
static int
compare_dir_entries_format7(const svn_sort__item_t *a,
                            const svn_sort__item_t *b)
{
  const svn_fs_dirent_t *lhs = (const svn_fs_dirent_t *) a->value;
  const svn_fs_dirent_t *rhs = (const svn_fs_dirent_t *) b->value;

  if (lhs->kind != rhs->kind)
    return lhs->kind == svn_node_dir ? -1 : 1;

  return 0 - strcmp(lhs->name, rhs->name);
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

  const svn_fs_fs__id_part_t *lhs_rev_item = svn_fs_fs__id_rev_item(lhs->id);
  const svn_fs_fs__id_part_t *rhs_rev_item = svn_fs_fs__id_rev_item(rhs->id);

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
  fs_fs_data_t *ffd = fs->fsap_data;
  apr_array_header_t *ordered
    = svn_sort__hash(directory,
                     ffd->format >= SVN_FS_FS__MIN_LOG_ADDRESSING_FORMAT
                       ? compare_dir_entries_format7
                       : compare_dir_entries_format6,
                     pool);

  apr_array_header_t *result
    = apr_array_make(pool, ordered->nelts, sizeof(svn_fs_dirent_t *));

  int i;
  for (i = 0; i < ordered->nelts; ++i)
    APR_ARRAY_PUSH(result, svn_fs_dirent_t *)
      = APR_ARRAY_IDX(ordered, i, svn_sort__item_t).value;

  return result;
}

/* Copy node revision item identified by ENTRY from the current position
 * in REV_FILE into CONTEXT->REPS_FILE.  Add all tracking into needed by
 * our placement algorithm to CONTEXT.  Use POOL for temporary allocations.
 */
static svn_error_t *
copy_node_to_temp(pack_context_t *context,
                  apr_file_t *rev_file,
                  svn_fs_fs__p2l_entry_t *entry,
                  apr_pool_t *pool)
{
  rep_info_t *rep_info = apr_pcalloc(context->info_pool, sizeof(*rep_info));
  node_revision_t *noderev;
  svn_stream_t *stream;

  /* create a copy of ENTRY, make it point to the copy destination and
   * store it in CONTEXT */
  rep_info->entry = svn_fs_fs__p2l_entry_dup(entry, context->info_pool);
  rep_info->entry->offset = 0;
  SVN_ERR(svn_io_file_seek(context->reps_file, SEEK_CUR,
                           &rep_info->entry->offset, pool));
  add_item_rep_mapping(context, rep_info);

  /* read & parse noderev */
  stream = svn_stream_from_aprfile2(rev_file, TRUE, pool);
  SVN_ERR(svn_fs_fs__read_noderev(&noderev, stream, pool));
  svn_stream_close(stream);

  /* if the node has a data representation, make that the node's "base".
   * This will place (often) cause the noderev to be placed right in front
   * of its data representation. */
  if (noderev->data_rep && noderev->data_rep->revision >= context->start_rev)
    {
      int idx = get_item_array_index(context, noderev->data_rep->revision,
                                     noderev->data_rep->item_index);
      if (idx < context->reps_infos->nelts)
        rep_info->base = APR_ARRAY_IDX(context->reps_infos, idx, rep_info_t *);
    }

  /* copy the noderev to our temp file */
  SVN_ERR(svn_io_file_seek(rev_file, SEEK_SET, &entry->offset, pool));
  SVN_ERR(copy_file_data(context, context->reps_file, rev_file, entry->size,
                         pool));

  /* if this node is a directory, we want all the nodes that it references
   * to be placed in a known order such that retrieval may use the same
   * ordering.  Please note that all noderevs referenced by this directory
   * have already been read from the rev files because directories get
   * written in a "bottom-up" scheme.
   */
  if (noderev->kind == svn_node_dir && rep_info->base)
    {
      apr_hash_t *directory;
      apr_pool_t *scratch_pool = svn_pool_create(pool);
      apr_array_header_t *sorted;
      int i;

      /* this is a sub-directory -> make the data rep item point to it */
      rep_info = rep_info->base;

      /* read the directory contents and sort it */
      SVN_ERR(svn_fs_fs__rep_contents_dir(&directory, context->fs, noderev,
                                          scratch_pool));
      sorted = svn_fs_fs__order_dir_entries(context->fs, directory,
                                            scratch_pool);

      /* link all items in sorted order.
       * This may overwrite existing linkage from older revisions.  But we
       * place data starting with the latest revision, it is only older
       * data that looses some of its coherence.
       */
      for (i = 0; i < sorted->nelts; ++i)
        {
          svn_fs_dirent_t *dir_entry
            = APR_ARRAY_IDX(sorted, i, svn_fs_dirent_t *);
          const svn_fs_fs__id_part_t *rev_item
            = svn_fs_fs__id_rev_item(dir_entry->id);

          /* linkage is only possible within the current revision range ... */
          if (rev_item->revision >= context->start_rev)
            {
              int idx = get_item_array_index(context,
                                             rev_item->revision,
                                             rev_item->number);

              /* ... and also only to previous items (in case directories
               * become able to reference later items in the future). */
              if (idx < context->reps_infos->nelts)
                {
                  /* link to the noderev item */
                  rep_info->next = APR_ARRAY_IDX(context->reps_infos, idx,
                                                 rep_info_t *);

                  /* continue linkage at the noderev item level */
                  rep_info = rep_info->next;
                }
            }
        }

      svn_pool_destroy(scratch_pool);
    }

  return SVN_NO_ERROR;
}

/* implements compare_fn_t. Place LHS before RHS, if the latter is older.
 */
static int
compare_p2l_info(const svn_fs_fs__p2l_entry_t * const * lhs,
                 const svn_fs_fs__p2l_entry_t * const * rhs)
{
  assert(*lhs != *rhs);
  if ((*lhs)->item_count == 0)
    return (*lhs)->item_count == 0 ? 0 : -1;
  if ((*lhs)->item_count == 0)
    return 1;
  
  if ((*lhs)->items[0].revision == (*rhs)->items[0].revision)
    return (*lhs)->items[0].number > (*rhs)->items[0].number ? -1 : 1;

  return (*lhs)->items[0].revision > (*rhs)->items[0].revision ? -1 : 1;
}

/* Sort svn_fs_fs__p2l_entry_t * array ENTRIES by age.  Place the latest
 * items first.
 */
static void
sort_items(apr_array_header_t *entries)
{
  qsort(entries->elts, entries->nelts, entries->elt_size,
        (int (*)(const void *, const void *))compare_p2l_info);
}

/* Decorator for svn_fs_fs__p2l_entry_t that associates it with a sorted
 * variant of its ITEMS array.
 */
typedef struct sub_item_ordered_t
{
  /* ENTRY that got wrapped */
  svn_fs_fs__p2l_entry_t *entry;

  /* Array of pointers into ENTRY->ITEMS, sorted by their revision member
   * _descending_ order.  May be NULL if ENTRY->ITEM_COUNT < 2. */
  svn_fs_fs__id_part_t **order;
} sub_item_ordered_t;

/* implements compare_fn_t. Place LHS before RHS, if the latter is younger.
 * Used to sort sub_item_ordered_t::order
 */
static int
compare_sub_items(const svn_fs_fs__id_part_t * const * lhs,
                  const svn_fs_fs__id_part_t * const * rhs)
{
  return (*lhs)->revision < (*rhs)->revision
       ? 1
       : ((*lhs)->revision > (*rhs)->revision ? -1 : 0);
}

/* implements compare_fn_t. Place LHS before RHS, if the latter belongs to
 * a newer revision.
 */
static int
compare_p2l_info_rev(const sub_item_ordered_t * lhs,
                     const sub_item_ordered_t * rhs)
{
  svn_fs_fs__id_part_t *lhs_part;
  svn_fs_fs__id_part_t *rhs_part;
  
  assert(lhs != rhs);
  if (lhs->entry->item_count == 0)
    return rhs->entry->item_count == 0 ? 0 : -1;
  if (rhs->entry->item_count == 0)
    return 1;

  lhs_part = lhs->order ? lhs->order[lhs->entry->item_count - 1]
                        : &lhs->entry->items[0];
  rhs_part = rhs->order ? rhs->order[rhs->entry->item_count - 1]
                        : &rhs->entry->items[0];

  if (lhs_part->revision == rhs_part->revision)
    return 0;

  return lhs_part->revision < rhs_part->revision ? -1 : 1;
}

/* Part of the placement algorithm: starting at INFO, place all items
 * referenced by it that have not been placed yet, in CONTEXT.  I.e.
 * recursively add them to CONTEXT->REPS.
 */
static void
pick_recursively(pack_context_t *context,
                 rep_info_t *info)
{
  rep_info_t *temp;
  rep_info_t *current;
  rep_info_t *below = NULL;

  do
    {
      /* go down _1_ level but not on rep deltification bases */
      if (   info->entry
          && info->entry->type == SVN_FS_FS__ITEM_TYPE_NODEREV
          && info->base)
        below = info->base->next;

      /* store the dependency / deltification chain in proper order */
      for (current = info; current; current = temp)
        {
          if (current->entry)
            {
              APR_ARRAY_PUSH(context->reps, svn_fs_fs__p2l_entry_t *)
                = current->entry;

              /* mark as "placed" */
              current->entry = NULL;
            }

          temp = current->base;
          current->base = NULL;
        }

      /* we basically stored the current node with its dependencies.
         Process sub-directories now. */
      if (below)
        pick_recursively(context, below);
      
      /* continue with sibling nodes */
      temp = info->next;
      info->next = NULL;
      info = temp;
    }
  while (info);
}

/* Apply the placement algorithm for noderevs and data representations to
 * CONTEXT.  Afterwards, CONTEXT->REPS contains all these items in the
 * desired order.
 */
static void
sort_reps(pack_context_t *context)
{
  int i;

  /* Place all root directories and root nodes first (but don't recurse) */
  for (i = context->reps_infos->nelts - 1; i >= 0; --i)
    {
      rep_info_t *info = APR_ARRAY_IDX(context->reps_infos, i, rep_info_t *);
      if (   info
          && info->entry
          && info->entry->item_count == 1
          && info->entry->items[0].number == SVN_FS_FS__ITEM_INDEX_ROOT_NODE)
        do
          {
            APR_ARRAY_PUSH(context->reps, svn_fs_fs__p2l_entry_t *)
              = info->entry;
            info->entry = NULL;
            info = info->base;
          }
        while (info && info->entry);
    }

  /* 2nd run: recursively place nodes along the directory tree structure */
  for (i = context->reps_infos->nelts - 1; i >= 0; --i)
    {
      rep_info_t *info = APR_ARRAY_IDX(context->reps_infos, i, rep_info_t *);
      if (info && info->entry == NULL)
        pick_recursively(context, info);
    }

  /* 3rd place: place any left-overs */
  for (i = context->reps_infos->nelts - 1; i >= 0; --i)
    {
      rep_info_t *info = APR_ARRAY_IDX(context->reps_infos, i, rep_info_t *);
      if (info && info->entry)
        pick_recursively(context, info);
    }
}

/* To prevent items from overlapping a block boundary, we will usually
 * put them into the next block and top up the old one with NUL bytes.
 * Pad CONTEXT's pack file to the end of the current block, if that padding
 * is short enough.  Use POOL for allocations.
 */
static svn_error_t *
auto_pad_block(pack_context_t *context,
               apr_pool_t *pool)
{
  fs_fs_data_t *ffd = context->fs->fsap_data;
  
  /* This is the maximum number of bytes "wasted" that way per block.
   * Larger items will cross the block boundaries. */
  const apr_off_t max_padding = MAX(ffd->block_size / 50, 512);

  /* Is wasted space small enough to align the current item to the next
   * block? */
  apr_off_t padding
    = ffd->block_size - (context->pack_offset % ffd->block_size);

  if (padding < max_padding)
    {
      /* Yes. To up with NUL bytes and don't forget to create
       * an P2L index entry marking this section as unused. */
      svn_fs_fs__p2l_entry_t null_entry;
      svn_fs_fs__id_part_t rev_item = { 0, SVN_FS_FS__ITEM_INDEX_UNUSED };

      null_entry.offset = context->pack_offset;
      null_entry.size = padding;
      null_entry.type = SVN_FS_FS__ITEM_TYPE_UNUSED;
      null_entry.item_count = 0;
      null_entry.items = NULL;

      SVN_ERR(write_null_bytes(context->pack_file, padding, pool));
      SVN_ERR(svn_fs_fs__p2l_proto_index_add_entry
                  (context->proto_p2l_index, &null_entry, pool));
      context->pack_offset += padding;
    }

  return SVN_NO_ERROR;
}

/* Determine, how many items from ENTRIES, beginning at START_INDEX, will
 * still fit into the block currently written in CONTEXT->PACK_FILE and
 * return that value in *ENTRIES_IN_BLOCK.  The item data can be read from
 * TEMP_FILE and POOL is being used for tempoary allocations.
 *
 * This function will put all noderevs into a single container (if it's more
 * than one such item) and write that container to the pack file.  The
 * corresponding items in ENTRIES will be replaced and mostly set to NULL.
 *
 * The caller may then copy the remaining items (up to *ENTRIES_IN_BLOCK).
 */
static svn_error_t *
select_block_entries(int *entries_in_block,
                     pack_context_t *context,
                     apr_array_header_t *entries,
                     int start_index,
                     apr_file_t *temp_file,
                     apr_pool_t *pool)
{
  int i;
  fs_fs_data_t *ffd = context->fs->fsap_data;

  svn_stream_t *stream = svn_stream_from_aprfile2(temp_file, TRUE, pool);

  /* 1 container for all noderevs in the current block */
  svn_fs_fs__noderevs_t *container = svn_fs_fs__noderevs_create(16, pool);

  /* indexes of noderevs that were put into the CONTAINER */
  apr_array_header_t *noderev_entries = apr_array_make(pool, 16, sizeof(int));

  /* number of bytes in the current block not being spent on fixed-size
     items (i.e. those not put into the container). */
  apr_size_t capacity_left = ffd->block_size
                          - (context->pack_offset % ffd->block_size);

  /* Estimated noderev container size */
  apr_size_t last_container_size = 0, container_size = 0;

  /* Estimate extra capacity we will gain from container compression. */
  apr_size_t pack_savings = 0;

  /* If the next item does not fit into the current block, auto-pad it.
     Take special care of textual noderevs since their parsers may prefetch
     up to 80 bytes and we don't want them to cross block boundaries. */
  svn_fs_fs__p2l_entry_t *first_entry
    = APR_ARRAY_IDX(entries, start_index, svn_fs_fs__p2l_entry_t *);
  apr_off_t safety_margin
    = first_entry->type == SVN_FS_FS__ITEM_TYPE_NODEREV ? 80 : 0;
  if (first_entry->size + safety_margin > capacity_left)
    {
      SVN_ERR(auto_pad_block(context, pool));
      capacity_left = ffd->block_size
                    - (context->pack_offset % ffd->block_size);
    }

  /* try pulling in items from the next block if the first item does not fit
     but is small enough that it might be packed nicely with the next block.
   */
  if (   first_entry->size > capacity_left
      && first_entry->size < ffd->block_size / 2)
    {
      /* frist, try to pull in the first N elements from the next block */
      apr_off_t pulled_in = 0;
      for (i = start_index + 1; i < entries->nelts; ++i)
        {
          svn_fs_fs__p2l_entry_t *entry
            = APR_ARRAY_IDX(entries, i, svn_fs_fs__p2l_entry_t *);
          if (   pulled_in + entry->size > 2 * capacity_left
              || entry->size > capacity_left)
            break;

          pulled_in += entry->size;
        }

      /* if the first one is already to large, look for the largest entry
         in the next block that still does fit. */
      if (--i == start_index)
        {
          apr_off_t checked = 0;
          apr_off_t best_size = 0;
          int best_fit = start_index;
          for (i = start_index + 1; i < entries->nelts && checked < ffd->block_size; ++i)
            {
              svn_fs_fs__p2l_entry_t *entry
                = APR_ARRAY_IDX(entries, i, svn_fs_fs__p2l_entry_t *);
              if (entry->size < capacity_left && entry->size > best_size)
                {
                  best_fit = i;
                  best_size = entry->size;
                }

              checked += entry->size;
            }

          i = best_fit;
        }

      /* if we found a such entry(es), swap them with the current one. */
      if (i != start_index)
        {
          APR_ARRAY_IDX(entries, start_index, svn_fs_fs__p2l_entry_t *)
            = APR_ARRAY_IDX(entries, i, svn_fs_fs__p2l_entry_t *);
          APR_ARRAY_IDX(entries, i, svn_fs_fs__p2l_entry_t *)
            = first_entry;
        }
    }

  /* try to fit as many items into the current block as possible */
  for (i = start_index; i < entries->nelts; ++i)
    {
      svn_fs_fs__p2l_entry_t *entry
        = APR_ARRAY_IDX(entries, i, svn_fs_fs__p2l_entry_t *);

      /* if we reached the limit, check whether we saved some space
         through the container. */
      if (capacity_left + pack_savings < container_size + entry->size)
        container_size = svn_fs_fs__noderevs_estimate_size(container);

      /* If necessary and the container is large enough, try harder
         by actually serializing the container and determine current
         savings due to compression. */
      if (   capacity_left + pack_savings < container_size + entry->size
          && container_size > last_container_size + 2000)
        {
          apr_pool_t *temp_pool = svn_pool_create(pool);
          svn_stringbuf_t *serialized
            = svn_stringbuf_create_ensure(container_size, temp_pool);
          svn_stream_t *temp_stream
            = svn_stream_from_stringbuf(serialized, temp_pool);

          SVN_ERR(svn_fs_fs__write_noderevs_container(temp_stream, container, temp_pool));
          SVN_ERR(svn_stream_close(temp_stream));

          last_container_size = container_size;
          pack_savings = container_size - serialized->len;

          svn_pool_destroy(temp_pool);
        }

      /* still doesn't fit? -> block is full */
      if (capacity_left + pack_savings < container_size + entry->size)
        break;

      /* item will fit into the block. */
      if (entry->type == SVN_FS_FS__ITEM_TYPE_NODEREV)
        {
          apr_size_t sub_item_idx;
          node_revision_t *noderev;

          SVN_ERR(svn_io_file_seek(temp_file, APR_SET, &entry->offset, pool));
          SVN_ERR(svn_fs_fs__read_noderev(&noderev, stream, pool));

          sub_item_idx = svn_fs_fs__noderevs_add(container, noderev);
          container_size += entry->size;

          SVN_ERR_ASSERT(sub_item_idx == noderev_entries->nelts);
          APR_ARRAY_PUSH(noderev_entries, int) = i;
        }
      else
        {
          capacity_left -= entry->size;
        }
    }

  /* return number of items to copy into the pack file.
   * Must be at least 1 to make progress. */
  *entries_in_block = MAX(1, i - start_index);

  /* serialize noderevs container and update ENTRIES */
  if (noderev_entries->nelts > 1)
    {
      apr_off_t offset = 0;
      svn_fs_fs__p2l_entry_t *container_entry
        = apr_palloc(context->info_pool, sizeof(*container_entry));

      /* serialize container */
      svn_stream_t *pack_stream
        = svn_stream_from_aprfile2(context->pack_file, TRUE, pool);

      SVN_ERR(svn_fs_fs__write_noderevs_container(pack_stream, container, pool));
      SVN_ERR(svn_io_file_seek(context->pack_file, APR_CUR, &offset, pool));

      /* replace first noderev item in ENTRIES with the container
         and set all others to NULL */
      container_entry->offset = context->pack_offset;
      container_entry->size = offset - container_entry->offset;
      container_entry->type = SVN_FS_FS__ITEM_TYPE_NODEREVS_CONT;
      container_entry->item_count = noderev_entries->nelts;
      container_entry->items = apr_palloc(context->info_pool,
          sizeof(svn_fs_fs__id_part_t) * container_entry->item_count);

      for (i = 0; i < noderev_entries->nelts; ++i)
        {
          int idx = APR_ARRAY_IDX(noderev_entries, i, int);
          svn_fs_fs__p2l_entry_t **entry
            = &APR_ARRAY_IDX(entries, idx, svn_fs_fs__p2l_entry_t *);
          container_entry->items[i] = (*entry)->items[0];

          *entry = i == 0 ? container_entry : NULL;
        }

      context->pack_offset = offset;

      /* Write P2L index for copied items, i.e. the 1 container */
      SVN_ERR(svn_fs_fs__p2l_proto_index_add_entry
                (context->proto_p2l_index, container_entry, pool));
    }
  else if (*entries_in_block > 1)
    {
      /* due to the way our parsers prefetch data, it's a bad idea to end
       * a block with a textual noderev representations */
      svn_fs_fs__p2l_entry_t *entry
        = APR_ARRAY_IDX(entries, i - 1, svn_fs_fs__p2l_entry_t *);
      if (entry->type == SVN_FS_FS__ITEM_TYPE_NODEREV)
        --*entries_in_block;
    }

  return SVN_NO_ERROR;
}

/* Copy (append) the items identified by svn_fs_fs__p2l_entry_t * elements
 * in ENTRIES strictly in order from TEMP_FILE into CONTEXT->PACK_FILE.
 * Use POOL for temporary allocations.
 */
static svn_error_t *
copy_items_from_temp(pack_context_t *context,
                     apr_array_header_t *entries,
                     apr_file_t *temp_file,
                     apr_pool_t *pool)
{
  fs_fs_data_t *ffd = context->fs->fsap_data;
  apr_pool_t *iterpool = svn_pool_create(pool);
  int i, k;

  /* copy all items in strict order */
  for (i = 0; i < entries->nelts; i = k)
    {
      /* determine number of items that fit into the current block.
         Containers may already get written as a side-effect. */
      int entries_in_block = 1;
      SVN_ERR(select_block_entries(&entries_in_block, context, entries,
                                   i, temp_file, iterpool));
      svn_pool_clear(iterpool);

      /* Copy the remaining non-NULL, non-container items to the pack file */
      for (k = i; k < i + entries_in_block; ++k)
        {
          apr_off_t in_block_offset = context->pack_offset % ffd->block_size;

          svn_fs_fs__p2l_entry_t *entry
            = APR_ARRAY_IDX(entries, k, svn_fs_fs__p2l_entry_t *);
          if (!entry || entry->type == SVN_FS_FS__ITEM_TYPE_NODEREVS_CONT)
            continue;

          /* select the item in the source file and copy it into the target
          * pack file */
          SVN_ERR(svn_io_file_seek(temp_file, SEEK_SET, &entry->offset,
                                  iterpool));
          SVN_ERR(copy_file_data(context, context->pack_file, temp_file,
                                entry->size, pool));

          /* write index entry and update current position */
          entry->offset = context->pack_offset;
          context->pack_offset += entry->size;

          SVN_ERR(svn_fs_fs__p2l_proto_index_add_entry
                      (context->proto_p2l_index, entry, iterpool));
        }

      svn_pool_clear(iterpool);
    }

  /* vaccum ENTRIES array: eliminate NULL entries */
  for (i = 0, k = 0; i < entries->nelts; ++i)
    {
      svn_fs_fs__p2l_entry_t *entry
        = APR_ARRAY_IDX(entries, i, svn_fs_fs__p2l_entry_t *);
      if (entry)
        {
          APR_ARRAY_IDX(entries, k, svn_fs_fs__p2l_entry_t *) = entry;
          ++k;
        }
    }
  entries->nelts = k;

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

/* Finalize CONTAINER and write it to CONTEXT's pack file.
 * Append an P2L entry containing the given SUB_ITEMS to NEW_ENTRIES.
 * Use POOL for temporary allocations.
 */
static svn_error_t *
write_changes_container(pack_context_t *context,
                        svn_fs_fs__changes_t *container,
                        apr_array_header_t *sub_items,
                        apr_array_header_t *new_entries,
                        apr_pool_t *pool)
{
  apr_off_t offset = 0;
  svn_fs_fs__p2l_entry_t container_entry;

  svn_stream_t *pack_stream
    = svn_stream_from_aprfile2(context->pack_file, TRUE, pool);

  SVN_ERR(svn_fs_fs__write_changes_container(pack_stream,
                                             container,
                                             pool));
  SVN_ERR(svn_io_file_seek(context->pack_file, SEEK_CUR, &offset, pool));

  container_entry.offset = context->pack_offset;
  container_entry.size = offset - container_entry.offset;
  container_entry.type = SVN_FS_FS__ITEM_TYPE_CHANGES_CONT;
  container_entry.item_count = sub_items->nelts;
  container_entry.items = (svn_fs_fs__id_part_t *)sub_items->elts;

  context->pack_offset = offset;
  APR_ARRAY_PUSH(new_entries, svn_fs_fs__p2l_entry_t *)
    = svn_fs_fs__p2l_entry_dup(&container_entry, context->info_pool);

  SVN_ERR(svn_fs_fs__p2l_proto_index_add_entry
            (context->proto_p2l_index, &container_entry, pool));

  return SVN_NO_ERROR;
}

/* Return the remaining unused bytes in the current block in CONTEXT's
 * pack file.
 */
static apr_ssize_t
get_block_left(pack_context_t *context)
{
  fs_fs_data_t *ffd = context->fs->fsap_data;
  return ffd->block_size - (context->pack_offset % ffd->block_size);
}

/* Read the change lists identified by svn_fs_fs__p2l_entry_t * elements
 * in ENTRIES strictly in from TEMP_FILE, aggregate them and write them
 * into CONTEXT->PACK_FILE.  Use POOL for temporary allocations.
 */
static svn_error_t *
write_changes_containers(pack_context_t *context,
                         apr_array_header_t *entries,
                         apr_file_t *temp_file,
                         apr_pool_t *pool)
{
  fs_fs_data_t *ffd = context->fs->fsap_data;
  apr_pool_t *iterpool = svn_pool_create(pool);
  apr_pool_t *container_pool = svn_pool_create(pool);
  int i;

  apr_ssize_t block_left = get_block_left(context);
  svn_fs_fs__changes_t *container
    = svn_fs_fs__changes_create(1000, container_pool);
  apr_array_header_t *sub_items
    = apr_array_make(pool, 64, sizeof(svn_fs_fs__id_part_t));
  apr_array_header_t *new_entries
    = apr_array_make(context->info_pool, 16, entries->elt_size);
  svn_stream_t *temp_stream
    = svn_stream_from_aprfile2(temp_file, TRUE, pool);

  /* copy all items in strict order */
  for (i = entries->nelts-1; i >= 0; --i)
    {
      apr_array_header_t *changes;
      apr_size_t list_index;
      svn_fs_fs__p2l_entry_t *entry
        = APR_ARRAY_IDX(entries, i, svn_fs_fs__p2l_entry_t *);

      if (block_left < entry->size)
        block_left = get_block_left(context)
                   - svn_fs_fs__changes_estimate_size(container);

      if ((block_left < entry->size) && sub_items->elts)
        {
          SVN_ERR(write_changes_container(context, container, sub_items,
                                          new_entries, iterpool));

          apr_array_clear(sub_items);
          svn_pool_clear(container_pool);
          container = svn_fs_fs__changes_create(1000, container_pool);
          block_left = get_block_left(context);
        }

      /* still enough space in current block? */
      if (block_left < entry->size)
        {
          SVN_ERR(auto_pad_block(context, iterpool));
          block_left = get_block_left(context);
        }

      /* select the change list in the source file, parse it and add it to
       * the container */
      SVN_ERR(svn_io_file_seek(temp_file, SEEK_SET, &entry->offset,
                               iterpool));
      SVN_ERR(svn_fs_fs__read_changes(&changes, temp_stream, iterpool));
      SVN_ERR(svn_fs_fs__changes_append_list(&list_index, container, changes));
      SVN_ERR_ASSERT(list_index == sub_items->nelts);
      
      APR_ARRAY_PUSH(sub_items, svn_fs_fs__id_part_t) = entry->items[0];

      svn_pool_clear(iterpool);
    }

  if (sub_items->elts)
    SVN_ERR(write_changes_container(context, container, sub_items,
                                    new_entries, iterpool));

  *entries = *new_entries;
  svn_pool_destroy(iterpool);
  svn_pool_destroy(container_pool);

  return SVN_NO_ERROR;
}

/* Append all entries of svn_fs_fs__p2l_entry_t * array TO_APPEND to
 * svn_fs_fs__p2l_entry_t * array DEST.
 */
static void
append_entries(apr_array_header_t *dest,
               apr_array_header_t *to_append)
{
  int i;
  for (i = 0; i < to_append->nelts; ++i)
    APR_ARRAY_PUSH(dest, svn_fs_fs__p2l_entry_t *)
      = APR_ARRAY_IDX(to_append, i, svn_fs_fs__p2l_entry_t *);
}

/* Write the log-to-phys proto index file for CONTEXT and use POOL for
 * temporary allocations.  All items in all buckets must have been placed
 * by now.
 */
static svn_error_t *
write_l2p_index(pack_context_t *context,
                apr_pool_t *pool)
{
  apr_pool_t *iterpool = svn_pool_create(pool);
  svn_revnum_t prev_rev = SVN_INVALID_REVNUM;
  int i;
  apr_uint32_t k;
  svn__priority_queue_t *queue;
  apr_size_t count = 0;
  apr_array_header_t *sub_item_orders;

  /* lump all items into one bucket.  As target, use the bucket that
   * probably has the most entries already. */
  append_entries(context->reps, context->changes);
  append_entries(context->reps, context->file_props);
  append_entries(context->reps, context->dir_props);

  /* wrap P2L entries such that we have access to the sub-items in revision
     order.  The ENTRY_COUNT member will point to the next item to read+1. */
  sub_item_orders
    = apr_array_make(pool, context->reps->nelts, sizeof(sub_item_ordered_t));
  sub_item_orders->nelts = context->reps->nelts;

  for (i = 0; i < context->reps->nelts; ++i)
    {
      svn_fs_fs__p2l_entry_t *entry
        = APR_ARRAY_IDX(context->reps, i, svn_fs_fs__p2l_entry_t *);
      sub_item_ordered_t *ordered
        = &APR_ARRAY_IDX(sub_item_orders, i, sub_item_ordered_t);

      ordered->entry = entry;
      count += entry->item_count;

      if (entry->item_count > 1)
        {
          ordered->order
            = apr_palloc(pool, sizeof(*ordered->order) * entry->item_count);
          for (k = 0; k < entry->item_count; ++k)
            ordered->order[k] = &entry->items[k];

          qsort(ordered->order, entry->item_count, sizeof(*ordered->order),
                (int (*)(const void *, const void *))compare_sub_items);
        }
    }

  /* we need to write the index in ascending revision order */
  queue = svn__priority_queue_create
            (sub_item_orders,
             (int (*)(const void *, const void *))compare_p2l_info_rev);

  /* write index entries */
  for (i = 0; i < count; ++i)
    {
      svn_fs_fs__id_part_t *sub_item;
      sub_item_ordered_t *ordered = svn__priority_queue_peek(queue);

      if (ordered->entry->item_count > 0)
        {
          /* if there is only one item, we skip the overhead of having an
             extra array for the item order */
          sub_item = ordered->order
                   ? ordered->order[ordered->entry->item_count - 1]
                   : &ordered->entry->items[0];

          /* next revision? */
          if (prev_rev != sub_item->revision)
            {
              prev_rev = sub_item->revision;
              SVN_ERR(svn_fs_fs__l2p_proto_index_add_revision
                          (context->proto_l2p_index, iterpool));
            }

          /* add entry */
          SVN_ERR(svn_fs_fs__l2p_proto_index_add_entry
                      (context->proto_l2p_index, ordered->entry->offset,
                      (apr_uint32_t)(sub_item - ordered->entry->items),
                      sub_item->number, iterpool));

          /* make ITEM_COUNT point the next sub-item to use+1 */
          --ordered->entry->item_count;
        }
        
      /* process remaining sub-items (if any) of that container later */
      if (ordered->entry->item_count)
        svn__priority_queue_update(queue);
      else
        svn__priority_queue_pop(queue);

      /* keep memory usage in check */
      if (i % 256 == 0)
        svn_pool_clear(iterpool);
    }

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

/* Pack the current revision range of CONTEXT, i.e. this covers phases 2
 * to 4.  Use POOL for allocations.
 */
static svn_error_t *
pack_range(pack_context_t *context,
           apr_pool_t *pool)
{
  apr_pool_t *revpool = svn_pool_create(pool);
  apr_pool_t *iterpool = svn_pool_create(pool);

  /* Phase 2: Copy items into various buckets and build tracking info */
  svn_revnum_t revision;
  for (revision = context->start_rev; revision < context->end_rev; ++revision)
    {
      apr_off_t offset = 0;
      apr_finfo_t finfo;
      apr_file_t *rev_file;
      
      /* Get the size of the file. */
      const char *path = svn_dirent_join(context->shard_dir,
                                         apr_psprintf(revpool, "%ld",
                                                      revision),
                                         revpool);
      SVN_ERR(svn_io_stat(&finfo, path, APR_FINFO_SIZE, revpool));

      SVN_ERR(svn_io_file_open(&rev_file, path,
                               APR_READ | APR_BUFFERED | APR_BINARY,
                               APR_OS_DEFAULT, revpool));

      /* store the indirect array index */
      APR_ARRAY_PUSH(context->rev_offsets, int) = context->reps_infos->nelts;
  
      /* read the phys-to-log index file until we covered the whole rev file.
       * That index contains enough info to build both target indexes from it. */
      while (offset < finfo.size)
        {
          /* read one cluster */
          int i;
          apr_array_header_t *entries;
          SVN_ERR(svn_fs_fs__p2l_index_lookup(&entries, context->fs,
                                              revision, offset,
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
                  SVN_ERR(svn_io_file_seek(rev_file, SEEK_SET, &offset,
                                           iterpool));

                  if (entry->type == SVN_FS_FS__ITEM_TYPE_CHANGES)
                    SVN_ERR(copy_item_to_temp(context,
                                              context->changes,
                                              context->changes_file,
                                              rev_file, entry, iterpool));
                  else if (entry->type == SVN_FS_FS__ITEM_TYPE_FILE_PROPS)
                    SVN_ERR(copy_item_to_temp(context,
                                              context->file_props,
                                              context->file_props_file,
                                              rev_file, entry, iterpool));
                  else if (entry->type == SVN_FS_FS__ITEM_TYPE_DIR_PROPS)
                    SVN_ERR(copy_item_to_temp(context,
                                              context->dir_props,
                                              context->dir_props_file,
                                              rev_file, entry, iterpool));
                  else if (   entry->type == SVN_FS_FS__ITEM_TYPE_FILE_REP
                           || entry->type == SVN_FS_FS__ITEM_TYPE_DIR_REP)
                    SVN_ERR(copy_rep_to_temp(context, rev_file, entry,
                                             iterpool));
                  else if (entry->type == SVN_FS_FS__ITEM_TYPE_NODEREV)
                    SVN_ERR(copy_node_to_temp(context, rev_file, entry,
                                              iterpool));
                  else
                    SVN_ERR_ASSERT(entry->type == SVN_FS_FS__ITEM_TYPE_UNUSED);
                    
                  offset += entry->size;
                }
            }

          if (context->cancel_func)
            SVN_ERR(context->cancel_func(context->cancel_baton));

          svn_pool_clear(iterpool);
        }

      svn_pool_clear(revpool);
    }

  svn_pool_destroy(iterpool);

  /* phase 3: placement.
   * Use "newest first" placement for simple items. */
  sort_items(context->changes);
  sort_items(context->file_props);
  sort_items(context->dir_props);

  /* follow dependencies recursively for noderevs and data representations */
  sort_reps(context);

  /* phase 4: copy bucket data to pack file.  Write P2L index. */
  SVN_ERR(write_changes_containers(context, context->changes,
                                   context->changes_file, revpool));
  svn_pool_clear(revpool);
  SVN_ERR(copy_items_from_temp(context, context->file_props,
                               context->file_props_file, revpool));
  svn_pool_clear(revpool);
  SVN_ERR(copy_items_from_temp(context, context->dir_props,
                               context->dir_props_file, revpool));
  svn_pool_clear(revpool);
  SVN_ERR(copy_items_from_temp(context, context->reps,
                               context->reps_file, revpool));
  svn_pool_clear(revpool);

  /* write L2P index as well (now that we know all target offsets) */
  SVN_ERR(write_l2p_index(context, revpool));

  svn_pool_destroy(revpool);
  
  return SVN_NO_ERROR;
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
              /* there should be true containers */
              SVN_ERR_ASSERT(entry->item_count == 1);

              entry->offset += context->pack_offset;
              offset += entry->size;
              SVN_ERR(svn_fs_fs__l2p_proto_index_add_entry
                        (context->proto_l2p_index, entry->offset, 0,
                         entry->items[0].number, iterpool));
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

/* Format 7 packing logic.
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
                   apr_size_t max_mem,
                   svn_cancel_func_t cancel_func,
                   void *cancel_baton,
                   apr_pool_t *pool)
{
  enum
    {
      /* estimated amount of memory used to represent one item in memory
       * during rev file packing */
      PER_ITEM_MEM = APR_ALIGN_DEFAULT(sizeof(rep_info_t))
                   + APR_ALIGN_DEFAULT(sizeof(svn_fs_fs__p2l_entry_t))
                   + 6 * sizeof(void*)
    };

  apr_size_t max_items = max_mem / PER_ITEM_MEM;
  apr_array_header_t *max_ids;
  pack_context_t context = { 0 };
  int i;
  apr_size_t item_count = 0;
  apr_pool_t *iterpool = svn_pool_create(pool);

  /* set up a pack context */
  SVN_ERR(initialize_pack_context(&context, fs, pack_file_dir, shard_dir,
                                  shard_rev, max_items, cancel_func,
                                  cancel_baton, pool));

  /* phase 1: determine the size of the revisions to pack */
  SVN_ERR(svn_fs_fs__l2p_get_max_ids(&max_ids, fs, shard_rev,
                                     context.shard_end_rev - shard_rev,
                                     pool));

  /* pack revisions in ranges that don't exceed MAX_MEM */
  for (i = 0; i < max_ids->nelts; ++i)
    if (APR_ARRAY_IDX(max_ids, i, apr_uint64_t) + item_count <= max_items)
      {
        context.end_rev++;
      }
    else
      {
        /* some unpacked revisions before this one? */
        if (context.start_rev < context.end_rev)
          {
            /* pack them intelligently (might be just 1 rev but still ...) */
            SVN_ERR(pack_range(&context, iterpool));
            SVN_ERR(reset_pack_context(&context, iterpool));
            item_count = 0;
          }

        /* next revision range is to start with the current revision */
        context.start_rev = i + context.shard_rev;
        context.end_rev = context.start_rev + 1;

        /* if this is a very large revision, we must place it as is */
        if (APR_ARRAY_IDX(max_ids, i, apr_uint64_t) > max_items)
          {
            SVN_ERR(append_revision(&context, iterpool));
            context.start_rev++;
          }
        else
          item_count += (apr_size_t)APR_ARRAY_IDX(max_ids, i, apr_uint64_t);

        svn_pool_clear(iterpool);
      }

  /* non-empty revision range at the end? */
  if (context.start_rev < context.end_rev)
    SVN_ERR(pack_range(&context, iterpool));

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
                                   path_rev_packed(fs, rev, PATH_MANIFEST,
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
      SVN_ERR(read_number_from_stream(&val, &eof, manifest_stream, iterpool));
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

/* Format 6 and earlier packing logic:  Simply concatenate all revision
 * contents.
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
  fs_fs_data_t *ffd = fs->fsap_data;
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
  if (ffd->format >= SVN_FS_FS__MIN_LOG_ADDRESSING_FORMAT)
    SVN_ERR(pack_log_addressed(fs, pack_file_dir, shard_path, shard_rev,
                               max_mem, cancel_func, cancel_baton, pool));
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

      SVN_ERR(pack_revprops_shard(revprops_pack_file_dir, revprops_shard_path,
                                  shard, max_files_per_dir,
                                  (int)(0.9 * max_pack_size),
                                  compression_level,
                                  cancel_func, cancel_baton, pool));
    }

  /* Update the min-unpacked-rev file to reflect our newly packed shard. */
  SVN_ERR(write_revnum_file(fs,
                            (svn_revnum_t)((shard + 1) * max_files_per_dir),
                            pool));
  ffd->min_unpacked_rev = (svn_revnum_t)((shard + 1) * max_files_per_dir);

  /* Finally, remove the existing shard directories. */
  SVN_ERR(svn_io_remove_dir2(rev_shard_path, TRUE,
                             cancel_func, cancel_baton, pool));
  if (revsprops_dir)
    SVN_ERR(delete_revprops_shard(revprops_shard_path,
                                  shard, max_files_per_dir,
                                  cancel_func, cancel_baton, pool));

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
     update_min_unpacked_rev().
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

  SVN_ERR(read_min_unpacked_rev(&ffd->min_unpacked_rev, pb->fs, pool));

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
