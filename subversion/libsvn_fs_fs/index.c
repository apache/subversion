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

#include "util.h"
#include "svn_private_config.h"

/* maximum length of a uint64 in an 7/8b encoding */
#define ENCODED_INT_LENGTH 10

/*
 * packed stream array
 */

/* How many numbers we will pre-fetch and buffer in a packed number stream.
 */
enum { MAX_NUMBER_PREFETCH = 64 };

/* Prefetched number entry in a packed number stream.
 */
typedef struct value_position_pair_t
{
  /* prefetched number */
  apr_uint64_t value;

  /* number of bytes read, *including* this number, since the buffer start */
  apr_size_t total_len;
} value_position_pair_t;

/* State of a prefetching packed number stream.  It will read compressed
 * index data efficiently and present it as a series of non-packed uint64.
 */
typedef struct packed_number_stream_t
{
  /* underlying data file containing the packed values */
  apr_file_t *file;

  /* number of used entries in BUFFER (starting at index 0) */
  apr_size_t used;

  /* index of the next number to read from the BUFFER (0 .. USED).
   * If CURRENT == USED, we need to read more data upon get() */
  apr_size_t current;

  /* offset in FILE from which the first entry in BUFFER has been read */
  apr_off_t start_offset;

  /* offset in FILE from which the next number has to be read */
  apr_off_t next_offset;

  /* read the file in chunks of this size */
  apr_size_t block_size;

  /* pool to be used for file ops etc. */
  apr_pool_t *pool;

  /* buffer for prefetched values */
  value_position_pair_t buffer[MAX_NUMBER_PREFETCH];
} packed_number_stream_t;

/* Return an svn_error_t * object for error ERR on STREAM with the given
 * MESSAGE string.  The latter must have a placeholder for the index file
 * name ("%s") and the current read offset (e.g. "0x%lx").
 */
static svn_error_t *
stream_error_create(packed_number_stream_t *stream,
                    apr_status_t err,
                    const char *message)
{
  const char *file_name;
  apr_off_t offset = 0;
  SVN_ERR(svn_io_file_name_get(&file_name, stream->file,
                               stream->pool));
  SVN_ERR(svn_io_file_seek(stream->file, SEEK_CUR, &offset, stream->pool));

  return svn_error_createf(err, NULL, message, file_name,
                           (apr_uint64_t)offset);
}

/* Read up to MAX_NUMBER_PREFETCH numbers from the STREAM->NEXT_OFFSET in
 * STREAM->FILE and buffer them.
 *
 * We don't want GCC and others to inline this function into get() because
 * it prevents get() from being inlined itself.
 */
SVN__PREVENT_INLINE
static svn_error_t *
packed_stream_read(packed_number_stream_t *stream)
{
  unsigned char buffer[MAX_NUMBER_PREFETCH];
  apr_size_t read = 0;
  apr_size_t i;
  value_position_pair_t *target;
  apr_off_t block_start = 0;
  apr_off_t block_left = 0;
  apr_status_t err;

  /* all buffered data will have been read starting here */
  stream->start_offset = stream->next_offset;

  /* packed numbers are usually not aligned to MAX_NUMBER_PREFETCH blocks,
   * i.e. the last number has been incomplete (and not buffered in stream)
   * and need to be re-read.  Therefore, always correct the file pointer.
   */
  SVN_ERR(svn_io_file_aligned_seek(stream->file, stream->block_size,
                                   &block_start, stream->next_offset,
                                   stream->pool));

  /* prefetch at least one number but, if feasible, don't cross block
   * boundaries.  This shall prevent jumping back and forth between two
   * blocks because the extra data was not actually request _now_.
   */
  read = sizeof(buffer);
  block_left = stream->block_size - (stream->next_offset - block_start);
  if (block_left >= 10 && block_left < read)
    read = block_left;

  err = apr_file_read(stream->file, buffer, &read);
  if (err && !APR_STATUS_IS_EOF(err))
    return stream_error_create(stream, err,
      _("Can't read index file '%s' at offset 0x%" APR_UINT64_T_HEX_FMT));

  /* if the last number is incomplete, trim it from the buffer */
  while (read > 0 && buffer[read-1] >= 0x80)
    --read;

  /* we call read() only if get() requires more data.  So, there must be
   * at least *one* further number. */
  if SVN__PREDICT_FALSE(read == 0)
    return stream_error_create(stream, err,
      _("Unexpected end of index file %s at offset 0x%"APR_UINT64_T_HEX_FMT));

  /* parse file buffer and expand into stream buffer */
  target = stream->buffer;
  for (i = 0; i < read;)
    {
      if (buffer[i] < 0x80)
        {
          /* numbers < 128 are relatively frequent and particularly easy
           * to decode.  Give them special treatment. */
          target->value = buffer[i];
          ++i;
          target->total_len = i;
          ++target;
        }
      else
        {
          apr_uint64_t value = 0;
          apr_uint64_t shift = 0;
          while (buffer[i] >= 0x80)
            {
              value += ((apr_uint64_t)buffer[i] & 0x7f) << shift;
              shift += 7;
              ++i;
            }

          target->value = value + ((apr_uint64_t)buffer[i] << shift);
          ++i;
          target->total_len = i;
          ++target;

          /* let's catch corrupted data early.  It would surely cause
           * havoc further down the line. */
          if SVN__PREDICT_FALSE(shift > 8 * sizeof(value))
            return svn_error_createf(SVN_ERR_FS_ITEM_INDEX_CORRUPTION, NULL,
                                     _("Corrupt index: number too large"));
       }
    }

  /* update stream state */
  stream->used = target - stream->buffer;
  stream->next_offset = stream->start_offset + i;
  stream->current = 0;

  return SVN_NO_ERROR;
};

/* Create and open a packed number stream reading from FILE_NAME and
 * return it in *STREAM.  Access the file in chunks of BLOCK_SIZE bytes.
 * Use POOL for allocations.
 */
static svn_error_t *
packed_stream_open(packed_number_stream_t **stream,
                   const char *file_name,
                   apr_size_t block_size,
                   apr_pool_t *pool)
{
  packed_number_stream_t *result = apr_palloc(pool, sizeof(*result));
  result->pool = svn_pool_create(pool);

  SVN_ERR(svn_io_file_open(&result->file, file_name,
                           APR_READ | APR_BUFFERED, APR_OS_DEFAULT,
                           result->pool));
  
  result->used = 0;
  result->current = 0;
  result->start_offset = 0;
  result->next_offset = 0;
  result->block_size = block_size;

  *stream = result;
  
  return SVN_NO_ERROR;
}

/* Close STREAM which may be NULL.
 */
SVN__FORCE_INLINE
static svn_error_t *
packed_stream_close(packed_number_stream_t *stream)
{
  if (stream)
    {
      SVN_ERR(svn_io_file_close(stream->file, stream->pool));
      svn_pool_destroy(stream->pool);
    }

  return SVN_NO_ERROR;
}

/*
 * The forced inline is required as e.g. GCC would inline read() into here
 * instead of lining the simple buffer access into callers of get().
 */
SVN__FORCE_INLINE
static svn_error_t*
packed_stream_get(apr_uint64_t *value,
                  packed_number_stream_t *stream)
{
  if (stream->current == stream->used)
    SVN_ERR(packed_stream_read(stream));

  *value = stream->buffer[stream->current].value;
  ++stream->current;

  return SVN_NO_ERROR;
}

/* Navigate STREAM to packed file offset OFFSET.  There will be no checks
 * whether the given OFFSET is valid.
 */
static void
packed_stream_seek(packed_number_stream_t *stream,
                   apr_off_t offset)
{
  if (   stream->used == 0
      || offset < stream->start_offset
      || offset >= stream->next_offset)
    {
      /* outside buffered data.  Next get() will read() from OFFSET. */
      stream->start_offset = offset;
      stream->next_offset = offset;
      stream->current = 0;
      stream->used = 0;
    }
  else
    {
      /* Find the suitable location in the stream buffer.
       * Since our buffer is small, it is efficient enough to simply scan
       * it for the desired position. */
      apr_size_t i;
      for (i = 0; i < stream->used; ++i)
        if (stream->buffer[i].total_len > offset - stream->start_offset)
          break;

      stream->current = i;
    }
}

/* Return the packed file offset of at which the next number in the stream
 * can be found.
 */
static apr_off_t
packed_stream_offset(packed_number_stream_t *stream)
{
  return stream->current == 0
       ? stream->start_offset
       : stream->buffer[stream->current-1].total_len + stream->start_offset;
}

/* Encode VALUE as 7/8b into P and return the number of bytes written.
 * This will be used when _writing_ packed data.  packed_stream_* is for
 * read operations only.
 */
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

/* Encode VALUE as 7/8b into P and return the number of bytes written.
 * This maps signed ints onto unsigned ones.
 */
static apr_size_t
encode_int(unsigned char *p, apr_int64_t value)
{
  return encode_uint(p, (apr_uint64_t)(value < 0 ? -1 - 2*value : 2*value));
}

/* Map unsigned VALUE back to signed integer.
 */
static apr_int64_t
decode_int(apr_uint64_t value)
{
  return (apr_int64_t)(value % 2 ? -1 - value / 2 : value / 2);
}

