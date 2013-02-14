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
#include <apr_poll.h>

#include "svn_pools.h"
#include "svn_dirent_uri.h"
#include "svn_sorts.h"
#include "private/svn_temp_serializer.h"

#include "fs_fs.h"
#include "pack.h"
#include "util.h"
#include "revprops.h"
#include "transaction.h"
#include "index.h"
#include "low_level.h"
#include "cached_data.h"

#include "../libsvn_fs/fs-loader.h"

#include "svn_private_config.h"
#include "temp_serializer.h"

typedef struct rep_info_t
{
  struct svn_fs_fs__p2l_entry_t *entry;
  struct rep_info_t *base;
  struct rep_info_t *next;
} rep_info_t;

typedef struct pack_context_t
{
  svn_fs_t *fs;
  svn_cancel_func_t cancel_func;
  void *cancel_baton;

  svn_revnum_t shard_rev;
  svn_revnum_t start_rev;
  svn_revnum_t end_rev;
  svn_revnum_t shard_end_rev;
  
  apr_file_t *proto_l2p_index;
  apr_file_t *proto_p2l_index;

  const char *shard_dir;
  const char *pack_file_dir;
  const char *pack_file_path;
  apr_off_t pack_offset;
  apr_file_t *pack_file;

  apr_array_header_t *changes;
  apr_file_t *changes_file;
  apr_array_header_t *file_props;
  apr_file_t *file_props_file;
  apr_array_header_t *dir_props;
  apr_file_t *dir_props_file;
  
  apr_array_header_t *rev_offsets;
  apr_array_header_t *reps_infos;
  apr_array_header_t *reps;
  apr_file_t *reps_file;

  apr_pool_t *info_pool;
} pack_context_t;

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
  
  SVN_ERR(svn_io_temp_dir(&temp_dir, pool));

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

  /* Index information files */
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

  context->rev_offsets = apr_array_make(pool, max_revs, sizeof(int));
  context->reps_infos = apr_array_make(pool, max_items, sizeof(rep_info_t *));
  context->reps = apr_array_make(pool, max_items,
                                 sizeof(svn_fs_fs__p2l_entry_t *));
  SVN_ERR(svn_io_open_unique_file3(&context->reps_file, NULL, temp_dir,
                                   svn_io_file_del_on_close, pool, pool));

  context->info_pool = svn_pool_create(pool);

  return SVN_NO_ERROR;
};

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
      /* using streaming copies for larger data blocks.  That may require
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

static svn_error_t *
copy_item_to_temp(pack_context_t *context,
                  apr_array_header_t *entries,
                  apr_file_t *temp_file,
                  apr_file_t *rev_file,
                  svn_fs_fs__p2l_entry_t *entry,
                  apr_pool_t *pool)
{
  svn_fs_fs__p2l_entry_t *new_entry = apr_palloc(context->info_pool,
                                                 sizeof(*new_entry));
  *new_entry = *entry;
  new_entry->offset = 0;
  SVN_ERR(svn_io_file_seek(temp_file, SEEK_CUR, &new_entry->offset, pool));
  APR_ARRAY_PUSH(entries, svn_fs_fs__p2l_entry_t *) = new_entry;
  
  SVN_ERR(copy_file_data(context, temp_file, rev_file, entry->size, pool));
  
  return SVN_NO_ERROR;
}

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

static void
add_item_rep_mapping(pack_context_t *context,
                     rep_info_t *info)
{
  int idx = get_item_array_index(context,
                                 info->entry->revision,
                                 info->entry->item_index);

  while (context->reps_infos->nelts <= idx)
    APR_ARRAY_PUSH(context->reps_infos, rep_info_t *) = NULL;

  assert(!APR_ARRAY_IDX(context->reps_infos, idx, rep_info_t *));
  APR_ARRAY_IDX(context->reps_infos, idx, rep_info_t *) = info;
}

static svn_error_t *
copy_rep_to_temp(pack_context_t *context,
                 apr_file_t *rev_file,
                 svn_fs_fs__p2l_entry_t *entry,
                 apr_pool_t *pool)
{
  rep_info_t *rep_info = apr_pcalloc(context->info_pool, sizeof(*rep_info));
  svn_fs_fs__rep_header_t *rep_header;
  svn_stream_t *stream;

  rep_info->entry = apr_palloc(context->info_pool, sizeof(*rep_info->entry));
  *rep_info->entry = *entry;
  rep_info->entry->offset = 0;
  SVN_ERR(svn_io_file_seek(context->reps_file, SEEK_CUR,
                           &rep_info->entry->offset, pool));
  add_item_rep_mapping(context, rep_info);

  stream = svn_stream_from_aprfile2(rev_file, TRUE, pool);
  SVN_ERR(svn_fs_fs__read_rep_header(&rep_header, stream, pool));
  svn_stream_close(stream);

  if (   rep_header->is_delta
      && !rep_header->is_delta_vs_empty
      && rep_header->base_revision >= context->start_rev)
    {
      int idx = get_item_array_index(context, rep_header->base_revision,
                                       rep_header->base_item_index);
      if (idx < context->reps_infos->nelts)
        rep_info->base = APR_ARRAY_IDX(context->reps_infos, idx, rep_info_t *);
    }

  SVN_ERR(svn_io_file_seek(rev_file, SEEK_SET, &entry->offset, pool));
  SVN_ERR(copy_file_data(context, context->reps_file, rev_file, entry->size,
                         pool));

  return SVN_NO_ERROR;
}

/* Directories first, dirs / files sorted by name.  This maximizes the
 * chance of two items being located close to one another in *all* pack
 * files independent of their change order.  It also groups multi-project
 * repos nicely according to their sub-projects.
 */
static int
compare_dir_entries_format7(const svn_sort__item_t *a,
                            const svn_sort__item_t *b)
{
  const svn_fs_dirent_t *lhs = (const svn_fs_dirent_t *) a->value;
  const svn_fs_dirent_t *rhs = (const svn_fs_dirent_t *) b->value;

  if (lhs->kind != rhs->kind)
    return lhs->kind == svn_node_dir ? -1 : 1;

  return strcmp(lhs->name, rhs->name);
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

  apr_off_t lhs_offset;
  apr_off_t rhs_offset;

  /* decreasing ("reverse") order on revs */
  svn_revnum_t lhs_revision = svn_fs_fs__id_rev(lhs->id);
  svn_revnum_t rhs_revision = svn_fs_fs__id_rev(rhs->id);
  if (lhs_revision != rhs_revision)
    return lhs_revision > rhs_revision ? -1 : 1;

  /* increasing order on offsets */
  lhs_offset = (apr_off_t)svn_fs_fs__id_item(lhs->id);
  rhs_offset = (apr_off_t)svn_fs_fs__id_item(rhs->id);
  if (lhs_offset != rhs_offset)
    return lhs_offset > rhs_offset ? 1 : -1;

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

static svn_error_t *
copy_node_to_temp(pack_context_t *context,
                  apr_file_t *rev_file,
                  svn_fs_fs__p2l_entry_t *entry,
                  apr_pool_t *pool)
{
  rep_info_t *rep_info = apr_pcalloc(context->info_pool, sizeof(*rep_info));
  node_revision_t *noderev;
  svn_stream_t *stream;

  rep_info->entry = apr_palloc(context->info_pool, sizeof(*rep_info->entry));
  *rep_info->entry = *entry;
  rep_info->entry->offset = 0;
  SVN_ERR(svn_io_file_seek(context->reps_file, SEEK_CUR,
                           &rep_info->entry->offset, pool));
  add_item_rep_mapping(context, rep_info);

  stream = svn_stream_from_aprfile2(rev_file, TRUE, pool);
  SVN_ERR(svn_fs_fs__read_noderev(&noderev, stream, pool));
  svn_stream_close(stream);

  if (noderev->data_rep && noderev->data_rep->revision >= context->start_rev)
    {
      int idx = get_item_array_index(context, noderev->data_rep->revision,
                                     noderev->data_rep->item_index);
      if (idx < context->reps_infos->nelts)
        rep_info->base = APR_ARRAY_IDX(context->reps_infos, idx, rep_info_t *);
    }

  SVN_ERR(svn_io_file_seek(rev_file, SEEK_SET, &entry->offset, pool));
  SVN_ERR(copy_file_data(context, context->reps_file, rev_file, entry->size,
                         pool));

  if (noderev->kind == svn_node_dir && rep_info->base)
    {
      apr_hash_t *directory;
      apr_pool_t *scratch_pool = svn_pool_create(pool);
      apr_array_header_t *sorted;
      int i;

      rep_info = rep_info->base;
      SVN_ERR(svn_fs_fs__rep_contents_dir(&directory, context->fs, noderev,
                                          scratch_pool));
      sorted = svn_fs_fs__order_dir_entries(context->fs, directory,
                                            scratch_pool);
      for (i = 0; i < sorted->nelts; ++i)
        {
          svn_fs_dirent_t *dir_entry
            = APR_ARRAY_IDX(sorted, i, svn_fs_dirent_t *);
          svn_revnum_t revision = svn_fs_fs__id_rev(dir_entry->id);
          apr_int64_t item_index = svn_fs_fs__id_item(dir_entry->id);

          if (revision >= context->start_rev)
            {
              int idx = get_item_array_index(context, revision, item_index);
              if (idx < context->reps_infos->nelts)
                {
                  rep_info->next = APR_ARRAY_IDX(context->reps_infos, idx,
                                                 rep_info_t *);
                  rep_info = rep_info->next;
                }
            }
        }

      svn_pool_destroy(scratch_pool);
    }

  return SVN_NO_ERROR;
}

static int
compare_p2l_info(const svn_fs_fs__p2l_entry_t * const * lhs,
                 const svn_fs_fs__p2l_entry_t * const * rhs)
{
  assert(*lhs != *rhs);
  
  if ((*lhs)->revision == (*rhs)->revision)
    return (*lhs)->item_index > (*rhs)->item_index ? -1 : 1;

  return (*lhs)->revision > (*rhs)->revision ? -1 : 1;
}

static void
sort_items(apr_array_header_t *entries)
{
  qsort(entries->elts, entries->nelts, entries->elt_size,
        (int (*)(const void *, const void *))compare_p2l_info);
}

static int
compare_p2l_info_rev(const svn_fs_fs__p2l_entry_t * const * lhs,
                     const svn_fs_fs__p2l_entry_t * const * rhs)
{
  assert(*lhs != *rhs);

  if ((*lhs)->revision == (*rhs)->revision)
    return 0;

  return (*lhs)->revision < (*rhs)->revision ? -1 : 1;
}

static void
sort_by_rev(apr_array_header_t *entries)
{
  qsort(entries->elts, entries->nelts, entries->elt_size,
        (int (*)(const void *, const void *))compare_p2l_info_rev);
}

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
              current->entry = NULL;
            }

          temp = current->base;
          current->base = NULL;
        }

      /* we basically stored the current node with its dependencies.
         Process sub-directories now. */
      if (below)
        pick_recursively(context, below);
      
      /* continue with sibbling nodes */
      temp = info->next;
      info->next = NULL;
      info = temp;
    }
  while (info);
}

static void
sort_reps(pack_context_t *context)
{
  int i;

  /* Place all root directories and root nodes first */
  for (i = context->reps_infos->nelts - 1; i >= 0; --i)
    {
      rep_info_t *info = APR_ARRAY_IDX(context->reps_infos, i, rep_info_t *);
      if (   info
          && info->entry
          && info->entry->item_index == SVN_FS_FS__ITEM_INDEX_ROOT_NODE)
        do
          {
            APR_ARRAY_PUSH(context->reps, svn_fs_fs__p2l_entry_t *)
              = info->entry;
            info->entry = NULL;
            info = info->base;
          }
        while (info && info->entry);
    }

  /* 2nd run: place nodes along the directory tree structure */
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
  int i;

  /* To prevent items from overlapping a block boundary, we will usually
   * put them into the next block and top up the old one with NUL bytes.
   * This is the maximum number of bytes "wasted" that way per block.
   * Larger items will cross the block boundaries. */
  const apr_off_t alignment_limit = MAX(ffd->block_size / 50, 512);

  /* copy all items in strict order */
  for (i = 0; i < entries->nelts; ++i)
    {
      svn_fs_fs__p2l_entry_t *entry
        = APR_ARRAY_IDX(entries, i, svn_fs_fs__p2l_entry_t *);

      apr_off_t in_block_offset = context->pack_offset % ffd->block_size;

      /* Determine how many bytes must still be available in the current
       * block to be able to insert the current item without crossing the
       * boundary.  Also, add 80 extra bytes (i.e. the size our line-based
       * parser prefetch) for items that get parsed such that there will
       * be no back-and-forth between blocks during parsing. */
      apr_off_t safe_size = entry->size;
      if (entry->type == SVN_FS_FS__ITEM_TYPE_NODEREV ||
          entry->type == SVN_FS_FS__ITEM_TYPE_CHANGES)
        safe_size += 80;

      /* still enough space in current block? */
      if (in_block_offset + safe_size > ffd->block_size)
        {
          /* No.  Is wasted space small enough to align the current item
           * to the next block? */
          apr_off_t bytes_to_alignment = ffd->block_size - in_block_offset;
          if (bytes_to_alignment < alignment_limit)
            {
              /* Yes. To up with NUL bytes and don't forget to create
               * an P2L index entry marking this section as unused. */
              svn_fs_fs__p2l_entry_t null_entry;
              null_entry.offset = context->pack_offset;
              null_entry.size = bytes_to_alignment;
              null_entry.type = SVN_FS_FS__ITEM_TYPE_UNUSED;
              null_entry.revision = 0;
              null_entry.item_index = SVN_FS_FS__ITEM_INDEX_UNUSED;
              
              SVN_ERR(write_null_bytes(context->pack_file,
                                       bytes_to_alignment, iterpool));
              SVN_ERR(svn_fs_fs__p2l_proto_index_add_entry
                          (context->proto_p2l_index, &null_entry, iterpool));
              context->pack_offset += bytes_to_alignment;
            }
        }

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

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

static void
append_entries(apr_array_header_t *dest,
               apr_array_header_t *to_append)
{
  int i;
  for (i = 0; i < to_append->nelts; ++i)
    APR_ARRAY_PUSH(dest, svn_fs_fs__p2l_entry_t *)
      = APR_ARRAY_IDX(to_append, i, svn_fs_fs__p2l_entry_t *);
}

static svn_error_t *
write_l2p_index(pack_context_t *context,
                apr_pool_t *pool)
{
  apr_pool_t *iterpool = svn_pool_create(pool);
  svn_revnum_t prev_rev = SVN_INVALID_REVNUM;
  int i;

  append_entries(context->reps, context->changes);
  append_entries(context->reps, context->file_props);
  append_entries(context->reps, context->dir_props);
  sort_by_rev(context->reps);
  
  for (i = 0; i < context->reps->nelts; ++i)
    {
      svn_fs_fs__p2l_entry_t *entry
        = APR_ARRAY_IDX(context->reps, i, svn_fs_fs__p2l_entry_t *);

      if (prev_rev != entry->revision)
        {
          prev_rev = entry->revision;
          SVN_ERR(svn_fs_fs__l2p_proto_index_add_revision
                      (context->proto_l2p_index, iterpool));
        }

      SVN_ERR(svn_fs_fs__l2p_proto_index_add_entry
                  (context->proto_l2p_index,
                   entry->offset, entry->item_index, iterpool));

      if (i % 256 == 0)
        svn_pool_clear(iterpool);
    }

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

static svn_error_t *
pack_section(pack_context_t *context,
             apr_pool_t *pool)
{
  apr_pool_t *revpool = svn_pool_create(pool);
  apr_pool_t *iterpool = svn_pool_create(pool);

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

  sort_items(context->changes);
  sort_items(context->file_props);
  sort_items(context->dir_props);
  sort_reps(context);
  
  SVN_ERR(copy_items_from_temp(context, context->changes,
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
  SVN_ERR(write_l2p_index(context, revpool));

  svn_pool_destroy(revpool);
  
  return SVN_NO_ERROR;
}

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
                        (context->proto_l2p_index,
                         entry->offset, entry->item_index, iterpool));
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

  SVN_ERR(initialize_pack_context(&context, fs, pack_file_dir, shard_dir,
                                  shard_rev, max_items, cancel_func,
                                  cancel_baton, pool));
 
  SVN_ERR(svn_fs_fs__l2p_get_max_ids(&max_ids, fs, shard_rev,
                                     context.shard_end_rev - shard_rev,
                                     pool));

  for (i = 0; i < max_ids->nelts; ++i)
    if (APR_ARRAY_IDX(max_ids, i, apr_uint64_t) + item_count <= max_items)
      {
        context.end_rev++;
      }
    else
      {
        if (context.start_rev < context.end_rev)
          {
            SVN_ERR(pack_section(&context, iterpool));
            SVN_ERR(reset_pack_context(&context, iterpool));
            item_count = 0;
          }

        context.start_rev = i + context.shard_rev;
        context.end_rev = context.start_rev + 1;

        if (APR_ARRAY_IDX(max_ids, i, apr_uint64_t) > max_items)
          {
            SVN_ERR(append_revision(&context, iterpool));
            context.start_rev++;
          }
        else
          item_count += (apr_size_t)APR_ARRAY_IDX(max_ids, i, apr_uint64_t);

        svn_pool_clear(iterpool);
      }
      
  if (context.start_rev < context.end_rev)
    SVN_ERR(pack_section(&context, iterpool));

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

static svn_error_t *
pack_phys_addressed(svn_fs_t *fs,
                    const char *pack_file_dir,
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

/* Pack the revision SHARD containing exactly MAX_FILES_PER_DIR revisions
 * from SHARD_PATH into the PACK_FILE_DIR, using POOL for allocations.
 * CANCEL_FUNC and CANCEL_BATON are what you think they are.
 *
 * If for some reason we detect a partial packing already performed, we
 * remove the pack file and start again.
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
    SVN_ERR(pack_phys_addressed(fs, pack_file_dir, shard_path, shard_rev,
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
                           ? SVN_DELTA_COMPRESSION_LEVEL_DEFAULT
                           : SVN_DELTA_COMPRESSION_LEVEL_NONE,
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
