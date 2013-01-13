/* index.c indexing support for FSFS support
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

#include "svn_io.h"
#include "svn_pools.h"

#include "index.h"
#include "util.h"
#include "pack.h"

#include "private/svn_subr_private.h"
#include "private/svn_temp_serializer.h"

#include "svn_private_config.h"
#include "temp_serializer.h"

#include "../libsvn_fs/fs-loader.h"

#define ENCODED_INT_LENGTH 10

typedef struct l2_index_page_table_entry_t
{
  apr_uint64_t offset;
  apr_uint32_t entry_count;
  apr_uint32_t size;
} l2_index_page_table_entry_t;

typedef struct l2p_index_header_t
{
  svn_revnum_t first_revision;
  apr_size_t revision_count;
  l2_index_page_table_entry_t ** page_tables;
  l2_index_page_table_entry_t * page_table;
} l2p_index_header_t;

typedef struct l2p_index_page_t
{
  apr_uint32_t entry_count;
  apr_off_t *offsets;
} l2p_index_page_t;

typedef struct l2_proto_index_entry_t
{
  apr_uint64_t offset;
  apr_uint64_t item_index;
} l2_proto_index_entry_t;

typedef struct p2l_index_header_t
{
  svn_revnum_t first_revision;
  apr_size_t page_count;
  apr_off_t *offsets;
} p2l_index_header_t;

svn_error_t *
svn_fs_fs__l2p_proto_index_open(apr_file_t **proto_index,
                                const char *file_name,
                                apr_pool_t *pool)
{
  SVN_ERR(svn_io_file_open(proto_index, file_name,
                           APR_READ | APR_WRITE
                           | APR_CREATE | APR_APPEND | APR_BUFFERED,
                           APR_OS_DEFAULT, pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
write_entry_to_proto_index(apr_file_t *proto_index,
                           l2_proto_index_entry_t entry,
                           apr_pool_t *pool)
{
  apr_size_t written = sizeof(entry);

  SVN_ERR(svn_io_file_write(proto_index, &entry, &written, pool));
  SVN_ERR_ASSERT(written == sizeof(entry));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__l2p_proto_index_add_revision(apr_file_t *proto_index,
                                        apr_pool_t *pool)
{
  l2_proto_index_entry_t entry;
  entry.offset = 0;
  entry.item_index = 0;

  return svn_error_trace(write_entry_to_proto_index(proto_index, entry,
                                                    pool));
}

svn_error_t *
svn_fs_fs__l2p_proto_index_add_entry(apr_file_t *proto_index,
                                     apr_off_t offset,
                                     apr_uint64_t item_index,
                                     apr_pool_t *pool)
{
  l2_proto_index_entry_t entry;

  /* make sure the conversion to uint64 works */
  SVN_ERR_ASSERT(offset >= 0);
  entry.offset = (apr_uint64_t)offset;

  /* make sure we can use item_index as an array index when building the
   * final index file */
  SVN_ERR_ASSERT(item_index < UINT_MAX / 2);
  entry.item_index = item_index;

  return svn_error_trace(write_entry_to_proto_index(proto_index, entry,
                                                    pool));
}

static apr_size_t
encode_uint(unsigned char *p, apr_uint64_t value)
{
  unsigned char *start = p;
  while (value >= 0x80)
    {
      *p = (unsigned char)((value % 0x80) + 0x80);
      value /= 0x80;
      ++p;
    }

  *p = (unsigned char)(value % 0x80);
  return (p - start) + 1;
}

svn_error_t *
svn_fs_fs__l2p_index_create(apr_file_t *proto_index,
                            svn_fs_t *fs,
                            svn_revnum_t revision,
                            apr_pool_t *pool)
{
  apr_off_t offset = 0;
  int i;
  apr_uint64_t entry;
  svn_boolean_t eof = FALSE;
  apr_file_t *index_file;
  unsigned char encoded[ENCODED_INT_LENGTH];

  int last_page_count = 0;          /* total page count at the start of
                                       the current revision */

  /* temporary data structures that collect the data which will be moved
     to the target file in a second step */
  apr_pool_t *local_pool = svn_pool_create(pool);
  apr_array_header_t *page_counts
     = apr_array_make(local_pool, 16, sizeof(apr_uint64_t));
  apr_array_header_t *page_sizes
     = apr_array_make(local_pool, 16, sizeof(apr_uint64_t));
  apr_array_header_t *entry_counts
     = apr_array_make(local_pool, 16, sizeof(apr_uint64_t));

  /* collects the item offsets for the current revision */
  apr_array_header_t *offsets
     = apr_array_make(local_pool, 256, sizeof(apr_uint64_t));

  /* 64k blocks, spill after 16MB */
  svn_spillbuf_t *buffer
     = svn_spillbuf__create(0x10000, 0x1000000, local_pool);

  /* start at the beginning of the source file */
  offset = 0;
  SVN_ERR(svn_io_file_seek(proto_index, SEEK_SET, &offset, local_pool));

  /* process all entries until we fail due to EOF */
  for (entry = 0; !eof; ++entry)
    {
      l2_proto_index_entry_t proto_entry;
      apr_size_t read = 0;

      /* (attempt to) read the next entry from the source */
      SVN_ERR(svn_io_file_read_full2(proto_index,
                                     &proto_entry, sizeof(proto_entry),
                                     &read, &eof, local_pool));
      SVN_ERR_ASSERT(read == sizeof(proto_entry));

      /* handle new revision */
      if ((entry > 0 && proto_entry.offset == -1) || eof)
        {
          /* dump entries, grouped into pages */

          int k = 0;
          for (i = 0; i < offsets->nelts; i = k)
            {
              /* 1 page with up to 8k entries */
              int entry_count = offsets->nelts - i < 0x2000
                              ? offsets->nelts
                              : 0x2000;
              apr_size_t last_buffer_size = svn_spillbuf__get_size(buffer);

              for (k = i; k < i + entry_count; ++k)
                {
                  apr_uint64_t value = APR_ARRAY_IDX(offsets, k, apr_uint64_t);
                  SVN_ERR(svn_spillbuf__write(buffer, (const char *)encoded,
                                              encode_uint(encoded, value),
                                              local_pool));
                }

              APR_ARRAY_PUSH(entry_counts, apr_uint64_t) = entry_count;
              APR_ARRAY_PUSH(page_sizes, apr_uint64_t)
                = svn_spillbuf__get_size(buffer) - last_buffer_size;
            }

          /* store the number of pages in this revision */
          APR_ARRAY_PUSH(page_counts, apr_uint64_t)
            = page_sizes->nelts - last_page_count;

          last_page_count = page_sizes->nelts;
        }
      else
        {
          int idx = (apr_size_t)proto_entry.item_index;
          while (idx <= offsets->nelts)
            APR_ARRAY_PUSH(offsets, apr_uint64_t) = 0;

          APR_ARRAY_IDX(offsets, idx, apr_uint64_t) = proto_entry.offset + 1;
        }
    }

  /* create the target file */
  SVN_ERR(svn_io_file_open(&index_file,
                           path_l2p_index(fs, revision, local_pool),
                           APR_WRITE | APR_CREATE | APR_TRUNCATE
                           | APR_BUFFERED,
                           APR_OS_DEFAULT, local_pool));

  /* write the start revision */
  SVN_ERR(svn_io_file_write_full(index_file, encoded,
                                 encode_uint(encoded, revision),
                                 NULL, local_pool));

  /* write the revision table */
  SVN_ERR(svn_io_file_write_full(index_file, encoded,
                                 encode_uint(encoded, page_counts->nelts),
                                 NULL, local_pool));
  for (i = 0; i < page_counts->nelts; ++i)
    {
      apr_uint64_t value = APR_ARRAY_IDX(page_counts, i, apr_uint64_t);
      SVN_ERR(svn_io_file_write_full(index_file, encoded,
                                     encode_uint(encoded, value),
                                     NULL, local_pool));
    }
    
  /* write the page table */
  SVN_ERR(svn_io_file_write_full(index_file, encoded,
                                 encode_uint(encoded, page_sizes->nelts),
                                 NULL, local_pool));
  for (i = 0; i < page_sizes->nelts; ++i)
    {
      apr_uint64_t value = APR_ARRAY_IDX(page_sizes, i, apr_uint64_t);
      SVN_ERR(svn_io_file_write_full(index_file, encoded,
                                     encode_uint(encoded, value),
                                     NULL, local_pool));
      value = APR_ARRAY_IDX(entry_counts, i, apr_uint64_t);
      SVN_ERR(svn_io_file_write_full(index_file, encoded,
                                     encode_uint(encoded, value),
                                     NULL, local_pool));
    }

  /* append page contents */
  SVN_ERR(svn_stream_copy3(svn_stream__from_spillbuf(buffer, local_pool),
                           svn_stream_from_aprfile2(index_file, TRUE,
                                                    local_pool),
                           NULL, NULL, local_pool));
  
  svn_pool_destroy(local_pool);

  return SVN_NO_ERROR;
}

static svn_error_t *
read_number(apr_uint64_t *value,
            apr_file_t *file,
            apr_pool_t *pool)
{
  unsigned char byte;
  apr_uint64_t shift = 0;
  
  *value = 0;
  do
    {
      if (shift > 8 * sizeof(value))
        return svn_error_createf(SVN_ERR_FS_ITEM_INDEX_CORRUPTION, NULL,
                                 _("Corrupt index: number too large"));

      SVN_ERR(svn_io_file_getc((char*)&byte, file, pool));
      *value += ((apr_uint64_t)byte) << shift;
      shift += 7;
    }
  while (byte >= 0x80);

  return SVN_NO_ERROR;
}            

static svn_error_t *
get_l2p_header(l2p_index_header_t **header,
               svn_fs_t *fs,
               svn_revnum_t revision,
               apr_pool_t *pool)
{
  apr_uint64_t value;
  int i;
  apr_size_t page, page_count;
  apr_off_t offset;
  l2p_index_header_t *result = apr_pcalloc(pool, sizeof(*result));
  
  apr_file_t *file = NULL;
  SVN_ERR(svn_io_file_open(&file, path_l2p_index(fs, revision, pool),
                           APR_READ | APR_BUFFERED, APR_OS_DEFAULT, pool));

  SVN_ERR(read_number(&value, file, pool));
  result->first_revision = (svn_revnum_t)value;
  SVN_ERR(read_number(&value, file, pool));
  result->revision_count = (int)value;
  SVN_ERR(read_number(&value, file, pool));
  page_count = (apr_size_t)value;

  result->page_table
    = apr_pcalloc(pool, page_count * sizeof(*result->page_table));
  result->page_tables
    = apr_pcalloc(pool, (result->revision_count + 1)
                      * sizeof(*result->page_tables));

  result->page_tables[0] = result->page_table;
  for (i = 0; i < result->revision_count; ++i)
    {
      SVN_ERR(read_number(&value, file, pool));
      result->page_tables[i+1] = result->page_tables[i] + (apr_size_t)value;
    }

  for (page = 0; page < page_count; ++page)
    {
      SVN_ERR(read_number(&value, file, pool));
      result->page_table[page].size = (apr_uint32_t)value;
      SVN_ERR(read_number(&value, file, pool));
      result->page_table[page].entry_count = (apr_uint32_t)value;
    }

  offset = 0;
  SVN_ERR(svn_io_file_seek(file, SEEK_CUR, &offset, pool));
  for (page = 0; page < page_count; ++page)
    {
      result->page_table[page].offset = offset;
      offset += result->page_table[page].size;
    }

  SVN_ERR(svn_io_file_close(file, pool));
  *header = result;

  return SVN_NO_ERROR;
}

static svn_error_t *
get_l2p_page(l2p_index_page_t **page,
             svn_fs_t *fs,
             svn_revnum_t start_revision,
             l2_index_page_table_entry_t *table_entry,
             apr_pool_t *pool)
{
  apr_uint64_t value;
  apr_uint32_t i;
  apr_off_t offset;
  l2p_index_page_t *result = apr_pcalloc(pool, sizeof(*result));

  apr_file_t *file = NULL;
  SVN_ERR(svn_io_file_open(&file, path_l2p_index(fs, start_revision, pool),
                           APR_READ | APR_BUFFERED, APR_OS_DEFAULT, pool));

  offset = table_entry->offset;
  SVN_ERR(svn_io_file_seek(file, SEEK_SET, &offset, pool));

  result->entry_count = table_entry->entry_count;
  result->offsets = apr_pcalloc(pool, (result->entry_count + 1)
                                    * sizeof(*result->offsets));
  for (i = 0; i < result->entry_count; ++i)
    {
      result->offsets[i] = offset;
      SVN_ERR(read_number(&value, file, pool));
      offset += (apr_off_t)value;
    }

  SVN_ERR(svn_io_file_close(file, pool));
  *page = result;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__l2p_index_lookup(apr_off_t *offset,
                            svn_fs_t *fs,
                            svn_revnum_t revision,
                            apr_uint64_t item_index,
                            apr_pool_t *pool)
{
  l2p_index_header_t *header = NULL;
  l2p_index_page_t *page = NULL;
  l2_index_page_table_entry_t *entry, *first_entry, *last_entry;

  SVN_ERR(get_l2p_header(&header, fs, revision, pool));
  if (   (header->first_revision > revision)
      || (header->first_revision + header->revision_count <= revision))
    return svn_error_createf(SVN_ERR_FS_ITEM_INDEX_REVISION , NULL,
                             _("Revision %ld not covered by item index"),
                             revision);

  first_entry = header->page_tables[revision - header->first_revision];
  last_entry = header->page_tables[revision + 1 - header->first_revision];
  for (entry = first_entry; entry < last_entry; ++entry)
    if (entry->entry_count > item_index)
      break;
    else
      item_index -= entry->entry_count;

  SVN_ERR(get_l2p_page(&page, fs, header->first_revision, entry, pool));
  if (page->entry_count <= item_index)
    return svn_error_createf(SVN_ERR_FS_ITEM_INDEX_OVERFLOW , NULL,
                             _("Item index %" APR_UINT64_T_FMT
                               " too large in revision %ld"),
                             item_index, revision);

  *offset = page->offsets[item_index] - 1;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__p2l_proto_index_open(apr_file_t **proto_index,
                                const char *file_name,
                                apr_pool_t *pool)
{
  SVN_ERR(svn_io_file_open(proto_index, file_name,
                           APR_READ | APR_WRITE
                           | APR_CREATE | APR_APPEND | APR_BUFFERED,
                           APR_OS_DEFAULT, pool));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_fs__p2l_proto_index_add_entry(apr_file_t *proto_index,
                                     svn_fs_fs__p2l_entry_t *entry,
                                     apr_pool_t *pool)
{
  apr_size_t written = sizeof(*entry);

  SVN_ERR(svn_io_file_write_full(proto_index, entry, sizeof(*entry),
                                 &written, pool));
  SVN_ERR_ASSERT(written == sizeof(*entry));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__p2l_index_create(apr_file_t *proto_index,
                            svn_fs_t *fs,
                            svn_revnum_t revision,
                            apr_pool_t *pool)
{
  apr_off_t offset = 0;
  int i;
  svn_boolean_t eof = FALSE;
  apr_file_t *index_file;
  unsigned char encoded[ENCODED_INT_LENGTH];
  apr_uint64_t page_size = 0x10000;

  apr_uint64_t last_entry_end = 0;
  apr_uint64_t last_page_end = 0;
  apr_size_t last_buffer_size = 0;  /* byte offset in the spill buffer at
                                       the begin of the current revision */

  /* temporary data structures that collect the data which will be moved
     to the target file in a second step */
  apr_pool_t *local_pool = svn_pool_create(pool);
  apr_array_header_t *table_sizes
     = apr_array_make(local_pool, 16, sizeof(apr_uint64_t));

  /* 64k blocks, spill after 16MB */
  svn_spillbuf_t *buffer
     = svn_spillbuf__create(0x10000, 0x1000000, local_pool);

  /* start at the beginning of the source file */
  offset = 0;
  SVN_ERR(svn_io_file_seek(proto_index, SEEK_SET, &offset, local_pool));

  /* process all entries until we fail due to EOF */
  while (!eof)
    {
      svn_fs_fs__p2l_entry_t entry;
      apr_size_t read = 0;
      apr_uint64_t entry_end;
      svn_boolean_t new_page = table_sizes->nelts > 0;

      /* (attempt to) read the next entry from the source */
      SVN_ERR(svn_io_file_read_full2(proto_index, &entry, sizeof(entry),
                                     &read, &eof, local_pool));
      SVN_ERR_ASSERT(read == sizeof(entry));

      /* "unused" (and usually non-existent) section to cover the offsets
         at the end the of the last page. */
      if (eof)
        {
          entry.offset = last_entry_end;
          entry.size = APR_ALIGN(entry.offset, page_size) - entry.offset;
          entry.type = 0;
          entry.revision = 0;
          entry.item_index = 0;
        }

      /* end tables while entry is extending behind them */
      entry_end = entry.offset + entry.size;
      while (entry_end - last_page_end > page_size)
        {
          apr_uint64_t buffer_size = svn_spillbuf__get_size(buffer);
          APR_ARRAY_PUSH(table_sizes, apr_uint64_t)
             = buffer_size - last_buffer_size;

          last_buffer_size = buffer_size;
          last_page_end += page_size;
          new_page = TRUE;
        }

      /* this entry starts a new table -> store its offset
         (all following entries in the same table will store sizes only) */
      if (new_page)
        SVN_ERR(svn_spillbuf__write(buffer, (const char *)encoded,
                                    encode_uint(encoded, entry.offset),
                                    local_pool));

      /* write entry */
      SVN_ERR(svn_spillbuf__write(buffer, (const char *)encoded,
                                  encode_uint(encoded, entry.size),
                                  local_pool));
      SVN_ERR(svn_spillbuf__write(buffer, (const char *)encoded,
                                  encode_uint(encoded, entry.type),
                                  local_pool));
      SVN_ERR(svn_spillbuf__write(buffer, (const char *)encoded,
                                  encode_uint(encoded, entry.revision),
                                  local_pool));
      SVN_ERR(svn_spillbuf__write(buffer, (const char *)encoded,
                                  encode_uint(encoded, entry.item_index),
                                  local_pool));

      last_entry_end = entry_end;
    }

  /* store length of last table */
  APR_ARRAY_PUSH(table_sizes, apr_uint64_t)
      = svn_spillbuf__get_size(buffer) - last_buffer_size;

  /* write the start revision */
  SVN_ERR(svn_io_file_write_full(index_file, encoded,
                                 encode_uint(encoded, revision),
                                 NULL, local_pool));

  /* create the target file */
  SVN_ERR(svn_io_file_open(&index_file,
                           path_l2p_index(fs, revision, local_pool),
                           APR_WRITE | APR_CREATE | APR_TRUNCATE
                           | APR_BUFFERED,
                           APR_OS_DEFAULT, local_pool));

  /* write the page table (actually, the sizes of each page description) */
  SVN_ERR(svn_io_file_write_full(index_file, encoded,
                                 encode_uint(encoded, table_sizes->nelts),
                                 NULL, local_pool));
  for (i = 0; i < table_sizes->nelts; ++i)
    {
      apr_uint64_t value = APR_ARRAY_IDX(table_sizes, i, apr_uint64_t);
      SVN_ERR(svn_io_file_write_full(index_file, encoded,
                                     encode_uint(encoded, value),
                                     NULL, local_pool));
    }

  /* append page contents */
  SVN_ERR(svn_stream_copy3(svn_stream__from_spillbuf(buffer, local_pool),
                           svn_stream_from_aprfile2(index_file, TRUE,
                                                    local_pool),
                           NULL, NULL, local_pool));

  svn_pool_destroy(local_pool);

  return SVN_NO_ERROR;
}

static svn_error_t *
get_p2l_header(p2l_index_header_t **header,
               svn_fs_t *fs,
               svn_revnum_t revision,
               apr_pool_t *pool)
{
  apr_uint64_t value;
  apr_size_t i;
  apr_off_t offset;
  p2l_index_header_t *result = apr_pcalloc(pool, sizeof(*result));

  apr_file_t *file = NULL;
  SVN_ERR(svn_io_file_open(&file, path_p2l_index(fs, revision, pool),
                           APR_READ | APR_BUFFERED, APR_OS_DEFAULT, pool));

  SVN_ERR(read_number(&value, file, pool));
  result->first_revision = (svn_revnum_t)value;
  SVN_ERR(read_number(&value, file, pool));
  result->page_count = (apr_size_t)value;
  result->offsets
    = apr_pcalloc(pool, (result->page_count + 1) * sizeof(*result->offsets));

  result->offsets[0] = 0;
  for (i = 0; i < result->page_count; ++i)
    {
      SVN_ERR(read_number(&value, file, pool));
      result->offsets[i+1] = result->offsets[i] + (apr_off_t)value;
    }

  offset = 0;
  SVN_ERR(svn_io_file_seek(file, SEEK_CUR, &offset, pool));
  for (i = 0; i < result->page_count; ++i)
    result->offsets[i] += offset;

  SVN_ERR(svn_io_file_close(file, pool));
  *header = result;

  return SVN_NO_ERROR;
}

static svn_error_t *
read_entry(apr_file_t *file,
           apr_off_t *item_offset,
           apr_array_header_t *result,
           apr_pool_t *pool)
{
  apr_uint64_t value;

  svn_fs_fs__p2l_entry_t entry;

  entry.offset = *item_offset;
  SVN_ERR(read_number(&value, file, pool));
  entry.size = (apr_off_t)value;
  SVN_ERR(read_number(&value, file, pool));
  entry.type = (int)value;
  SVN_ERR(read_number(&value, file, pool));
  entry.revision = (svn_revnum_t)value;
  SVN_ERR(read_number(&value, file, pool));
  entry.item_index = value;

  APR_ARRAY_PUSH(result, svn_fs_fs__p2l_entry_t) = entry;
  *item_offset += entry.size;

  return SVN_NO_ERROR;
}

static svn_error_t *
get_p2l_page(apr_array_header_t **entries,
             svn_fs_t *fs,
             svn_revnum_t start_revision,
             apr_off_t start_offset,
             apr_off_t next_offset,
             apr_off_t page_start,
             apr_pool_t *pool)
{
  apr_uint64_t value;
  apr_array_header_t *result
    = apr_array_make(pool, 16, sizeof(svn_fs_fs__p2l_entry_t));
  apr_off_t item_offset;
  apr_uint64_t page_size = 0x10000;
  apr_off_t offset;

  apr_file_t *file = NULL;
  SVN_ERR(svn_io_file_open(&file, path_p2l_index(fs, start_revision, pool),
                           APR_READ | APR_BUFFERED, APR_OS_DEFAULT, pool));
  SVN_ERR(svn_io_file_seek(file, SEEK_SET, &start_offset, pool));

  SVN_ERR(read_number(&value, file, pool));
  item_offset = (apr_off_t)value;

  do
    {
      SVN_ERR(read_entry(file, &item_offset, result, pool));
      SVN_ERR(svn_io_file_seek(file, SEEK_CUR, &offset, pool));
    }
  while (offset < next_offset);

  if (item_offset < page_start + page_size)
    {
      SVN_ERR(read_number(&value, file, pool));
      SVN_ERR(read_entry(file, &item_offset, result, pool));
    }

  SVN_ERR(svn_io_file_close(file, pool));
  *entries = result;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__p2l_index_lookup(apr_array_header_t **entries,
                            svn_fs_t *fs,
                            svn_revnum_t revision,
                            apr_off_t offset,
                            apr_pool_t *pool)
{
  p2l_index_header_t *header = NULL;
  apr_uint64_t page_size = 0x10000;
  apr_size_t page_no = offset / page_size;

  SVN_ERR(get_p2l_header(&header, fs, revision, pool));
  if (header->page_count <= page_no)
    return svn_error_createf(SVN_ERR_FS_ITEM_INDEX_OVERFLOW , NULL,
                             _("Offset %" APR_OFF_T_FMT
                               " too large in revision %ld"),
                             offset, revision);

  SVN_ERR(get_p2l_page(entries, fs, header->first_revision,
                       header->offsets[page_no],
                       header->offsets[page_no + 1],
                       (apr_off_t)(page_no * page_size), pool));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_fs__item_offset(apr_off_t *offset,
                       svn_fs_t *fs,
                       svn_revnum_t revision,
                       apr_uint64_t item_index,
                       apr_pool_t *pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;
  if (ffd->format < SVN_FS_FS__MIN_LOG_ADDRESSING_FORMAT)
    {
      *offset = (apr_off_t)item_index - 1;
      if (is_packed_rev(fs, revision))
        {
          apr_off_t rev_offset;

          SVN_ERR(svn_fs_fs__get_packed_offset(&rev_offset, fs, revision,
                                               pool));
          *offset += rev_offset;
        }

      return SVN_NO_ERROR;
    }

  return svn_error_trace(svn_fs_fs__l2p_index_lookup(offset, fs, revision,
                                                     item_index, pool));
}
