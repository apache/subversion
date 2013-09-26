/*
 * file.c :  routines for efficient file handling
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

#define ENABLE_SVN_FILE

#include <assert.h>
#include <apr_file_io.h>

#include "svn_pools.h"
#include "svn_io.h"
#include "svn_sorts.h"
#include "svn_dirent_uri.h"

#include "private/svn_atomic.h"
#include "private/svn_mutex.h"
#include "private/svn_file.h"

#include "svn_private_config.h"

#define DEFAULT_CAPACITY 16
#define BUFFER_COUNT 2
#define INVALID_OFFSET (-1)

typedef struct shared_handle_t
{
  apr_file_t *file;

  const char *file_name;
  apr_int32_t reopen_flags;
  apr_uint32_t name_hash;
  apr_off_t position;

  int idx;
  struct shared_handle_t *next;
  struct shared_handle_t *previous;

  apr_pool_t *pool;
} shared_handle_t;

typedef struct shared_handle_pool_t
{
  svn_mutex__t *mutex;

  apr_array_header_t *handles;

  shared_handle_t *first_open;
  shared_handle_t *last_open;
  shared_handle_t *first_unused;
  
  int capacity;
  int unused_count; /* shared_handle_t instances w/o an open file handle */
  int open_count;   /* shared_handle_t instances with an open file handle */
  int used_count;   /* shared_handle_t instances currently handed out */

  apr_pool_t *pool;
} shared_handle_pool_t;

typedef struct buffer_t
{
  unsigned char *data;
  apr_size_t size;
  apr_size_t used;

  apr_off_t start_offset;
  svn_boolean_t modified;
} buffer_t;

struct svn_file_t
{
  buffer_t *buffers[BUFFER_COUNT];
  apr_size_t buffer_count;

  apr_off_t position;
  apr_off_t size;

  const char *file_name;
  apr_uint32_t name_hash;
  apr_int32_t reopen_flags;
  apr_size_t buffer_size;
  apr_pool_t *pool;

  int handle_hint;
};

static volatile svn_atomic_t handle_pool_init_state = 0;
static shared_handle_pool_t global_handle_pool = { 0 };

static svn_error_t *
init_handle_pool(void *baton, apr_pool_t *scratch_pool)
{
  /* Global pool for the temp path */
  apr_pool_t *pool = svn_pool_create(NULL);

  SVN_ERR(svn_mutex__init(&global_handle_pool.mutex, TRUE, pool));
  global_handle_pool.handles
    = apr_array_make(pool, DEFAULT_CAPACITY, sizeof(shared_handle_t *));

  global_handle_pool.capacity = DEFAULT_CAPACITY;
  global_handle_pool.first_open = 0;
  global_handle_pool.last_open = 0;
  global_handle_pool.first_unused = 0;

  global_handle_pool.pool = pool;

  return SVN_NO_ERROR;
}

static shared_handle_pool_t *
get_handle_pool(void)
{
  SVN_ERR_ASSERT_NO_RETURN(   SVN_NO_ERROR
                           == svn_atomic__init_once(&handle_pool_init_state,
                                                    init_handle_pool,
                                                    NULL, NULL));

  return &global_handle_pool;
}

#define FNV1_PRIME_32 0x01000193
#define FNV1_BASE_32 2166136261u

/* FNV-1a core implementation returning a 32 bit checksum over the first
 * LEN bytes in INPUT.  HASH is the checksum over preceding data (if any).
 */
static apr_uint32_t
calc_hash(const void *input)
{
  const unsigned char *data = input;
  apr_uint32_t hash = FNV1_BASE_32;

  for (; *data; ++data)
    {
      hash ^= *data;
      hash *= FNV1_PRIME_32;
    }

  return hash;
}

static shared_handle_t *
created_shared_handle(shared_handle_pool_t *handle_pool)
{
  shared_handle_t *result = apr_pcalloc(handle_pool->pool, sizeof(*result));
  result->pool = svn_pool_create(handle_pool->pool);

  result->idx = handle_pool->handles->nelts;
  APR_ARRAY_PUSH(handle_pool->handles, shared_handle_t *) = NULL;
  ++handle_pool->open_count;

  return result;
}

static shared_handle_t *
reclaim_shared_handle(shared_handle_pool_t *handle_pool)
{
  shared_handle_t *result = handle_pool->last_open;
  assert(result);
  
  handle_pool->last_open = result->previous;
  result->previous = NULL;
  assert(result->next == NULL);

  APR_ARRAY_IDX(handle_pool->handles, result->idx,
                shared_handle_t *) = NULL;

  /* implicitly closes the file */
  apr_pool_clear(result->pool);

  return result;
}

static shared_handle_t *
recycle_shared_handle(shared_handle_pool_t *handle_pool)
{
  shared_handle_t *result = handle_pool->first_unused;
  assert(handle_pool->first_unused);

  handle_pool->first_unused = result->next;
  result->next = NULL;
  assert(result->previous == NULL);

  ++handle_pool->open_count;
  --handle_pool->unused_count;

  return result;
}

static svn_error_t *
allocate_handle_internal(shared_handle_t **handle,
                         shared_handle_pool_t *handle_pool,
                         svn_file_t *file)
{
  shared_handle_t *result;

  if (handle_pool->capacity <= handle_pool->open_count)
      /* only open a new new handle if we have no other choice */
      result = handle_pool->open_count == handle_pool->used_count
             ? created_shared_handle(handle_pool)
             : reclaim_shared_handle(handle_pool);
  else
      /* open a new handle while keeping existing ones untouched */
      result = handle_pool->unused_count == 0
             ? created_shared_handle(handle_pool)
             : recycle_shared_handle(handle_pool);

  result->file_name = apr_pstrdup(result->pool, file->file_name);
  result->name_hash = calc_hash(result->file_name);
  result->reopen_flags = ((APR_READ | APR_WRITE) & file->reopen_flags)
                       | (APR_BINARY | APR_EXCL | APR_XTHREAD);
  result->position = 0;
  SVN_ERR(svn_io_file_open(&result->file,
                           result->file_name,
                           file->reopen_flags,
                           APR_OS_DEFAULT,
                           result->pool));

  ++handle_pool->used_count;

  file->reopen_flags = result->reopen_flags;
  file->handle_hint = result->idx;

  *handle = result;
  return SVN_NO_ERROR;
}

static svn_error_t *
allocate_handle(shared_handle_t **handle,
                svn_file_t *file)
{
  shared_handle_pool_t *handle_pool = get_handle_pool();
  SVN_MUTEX__WITH_LOCK(handle_pool->mutex,
                       allocate_handle_internal(handle, handle_pool, file));

  return SVN_NO_ERROR;
}

static svn_boolean_t
handle_matches(shared_handle_t *handle,
               svn_file_t *file)
{
  if (file->name_hash != handle->name_hash)
    return FALSE;
  if (file->reopen_flags != handle->reopen_flags)
    return FALSE;

  return strcmp(file->file_name, handle->file_name) == 0;
}

static svn_error_t *
get_handle_internal(shared_handle_t **handle,
                    shared_handle_pool_t *handle_pool,
                    svn_boolean_t auto_create,
                    svn_file_t *file)
{
  shared_handle_t *result;

  /* try quick match */
  result = APR_ARRAY_IDX(handle_pool->handles, file->handle_hint,
                         shared_handle_t *);
  if (!result || !handle_matches(result, file))
    /* crawl open files */
    for (result = handle_pool->first_open; result; result = result->next)
      if (handle_matches(result, file))
        break;

  if (result)
    {
      /* can we reuse the entry? */
      if (result->next)
        result->next->previous = result->previous;
      else
        handle_pool->last_open = result->previous;

      if (result->previous)
        result->previous->next = result->next;
      else
        handle_pool->first_open = result->next;

      result->previous = NULL;
      result->next = NULL;

      APR_ARRAY_IDX(handle_pool->handles, result->idx,
                    shared_handle_t *) = NULL;
      ++handle_pool->used_count;
    }
  else if (auto_create)
    {
      /* we need a new handle */
      SVN_ERR(allocate_handle_internal(&result, handle_pool, file));
    }

  *handle = result;

  return SVN_NO_ERROR;
}

static svn_error_t *
get_handle(shared_handle_t **handle,
           svn_file_t *file)
{
  shared_handle_pool_t *handle_pool = get_handle_pool();
  SVN_MUTEX__WITH_LOCK(handle_pool->mutex,
                       get_handle_internal(handle, handle_pool,
                                           TRUE, file));

  return SVN_NO_ERROR;
}

static void
close_handle(shared_handle_pool_t *handle_pool,
             shared_handle_t *handle)
{
  /* implicitly closes the file */
  apr_pool_clear(handle->pool);

  handle_pool->last_open = handle->previous;
  handle->next = handle_pool->first_unused;
  handle_pool->first_unused = handle;

  ++handle_pool->unused_count;
}

static svn_error_t *
release_handle_internal(shared_handle_pool_t *handle_pool,
                        shared_handle_t *handle,
                        svn_boolean_t keep_open)
{
  --handle_pool->used_count;
  if (!keep_open || handle_pool->capacity <= handle_pool->used_count)
    {
      close_handle(handle_pool, handle);
    }
  else
    {
      if (handle_pool->first_open)
        {
          handle->next = handle_pool->first_open;
          handle_pool->first_open->previous = handle;
          handle_pool->first_open = handle;
        }
      else
        {
          handle_pool->first_open = handle;
          handle_pool->last_open = handle;
        }
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
release_handle(shared_handle_t *handle,
               svn_boolean_t keep_open)
{
  shared_handle_pool_t *handle_pool = get_handle_pool();
  SVN_MUTEX__WITH_LOCK(handle_pool->mutex,
                       release_handle_internal(handle_pool, handle,
                                               keep_open));

  return SVN_NO_ERROR;
}

static svn_error_t *
close_file_internal(shared_handle_pool_t *handle_pool,
                    svn_file_t *file)
{
  shared_handle_t *handle;
  SVN_ERR(get_handle_internal(&handle, handle_pool, FALSE, file));

  if (handle)
    close_handle(handle_pool, handle);

  return SVN_NO_ERROR;
}

static svn_error_t *
close_file(svn_file_t *file)
{
  shared_handle_pool_t *handle_pool = get_handle_pool();
  SVN_MUTEX__WITH_LOCK(handle_pool->mutex,
                       close_file_internal(handle_pool, file));

  return SVN_NO_ERROR;
}

apr_size_t
svn_file__get_max_shared_handles(void)
{
  return get_handle_pool()->capacity;
}

static svn_error_t *
set_max_shared_handles_internal(shared_handle_pool_t *handle_pool,
                                apr_size_t new_max)
{
  handle_pool->capacity = new_max;
  while (   (handle_pool->capacity > handle_pool->open_count)
         && (handle_pool->used_count < handle_pool->open_count))
    {
      reclaim_shared_handle(handle_pool);
      --handle_pool->open_count;
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_file__set_max_shared_handles(apr_size_t new_max)
{
  shared_handle_pool_t *handle_pool = get_handle_pool();
  SVN_MUTEX__WITH_LOCK(handle_pool->mutex,
                       set_max_shared_handles_internal(handle_pool, new_max));

  return SVN_NO_ERROR;
}

static svn_error_t *
handle_seek(shared_handle_t *handle,
            apr_off_t offset)
{
  if (handle->position != offset)
    {
      apr_off_t actual_offset = offset;
      SVN_ERR(svn_io_file_seek(handle->file, APR_SET,
                               &actual_offset, handle->pool));
      handle->position = actual_offset;

      SVN_ERR_ASSERT(actual_offset == offset);
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
flush_buffer(shared_handle_t *handle,
             buffer_t *buffer,
             apr_pool_t *pool)
{
  assert(buffer->modified);
  assert(buffer->size >= buffer->used);

  SVN_ERR(handle_seek(handle, buffer->start_offset));
  SVN_ERR(svn_io_file_write_full(handle->file, buffer->data,
                                 buffer->used, NULL, pool));
  handle->position += buffer->used;

  buffer->modified = FALSE;

  return SVN_NO_ERROR;
}

static svn_error_t *
sort_buffers(buffer_t **buffers,
             apr_size_t count)
{
  apr_size_t i, k;

  for (i = 0; i < count - 1; ++i)
    for (k = i + 1; k < count; ++k)
      if (buffers[i]->start_offset > buffers[k]->start_offset)
        {
          buffer_t *temp = buffers[i];
          buffers[i] = buffers[k];
          buffers[k] = temp;
        }

  return SVN_NO_ERROR;
}

static svn_error_t *
flush_all_buffers(shared_handle_t *handle,
                  svn_file_t *file)
{
  apr_size_t i;

  sort_buffers(file->buffers, file->buffer_count);
  for (i = 0; i < file->buffer_count; ++i)
    if (file->buffers[i]->modified)
      SVN_ERR(flush_buffer(handle, file->buffers[i], file->pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
file_close_internal(svn_file_t *file)
{
  apr_size_t i;
  svn_boolean_t buffers_dirty = FALSE;

  for (i = 0; i < file->buffer_count; ++i)
    buffers_dirty |= file->buffers[i]->modified;

  if (buffers_dirty)
    {
      shared_handle_t *handle;

      SVN_ERR(get_handle(&handle, file));
      SVN_ERR(flush_all_buffers(handle, file));
      SVN_ERR(release_handle(handle, FALSE));
    }
  else
    {
      SVN_ERR(close_file(file));
    }

  /* free all buffer & temp memory */
  svn_pool_destroy(file->pool);

  return SVN_NO_ERROR;
}

static apr_status_t
file_destructor(void *arg)
{
  svn_file_t *file = arg;
  svn_error_t *err = file_close_internal(file);
  apr_status_t apr_err = 0;

  if (err)
    {
      apr_err = err->apr_err;
      svn_error_clear(err);
    }

  return apr_err;
}

svn_error_t *
svn_file__open(svn_file_t **result,
               const char *name,
               apr_int32_t flag,
               apr_size_t buffer_size,
               svn_boolean_t defer_creation,
               apr_pool_t *pool)
{
  shared_handle_t *handle;
  svn_file_t *file;

  /* using any of the unsupported flags may result in unspecified behavior */
  SVN_ERR_ASSERT((flag & SVN_FILE__SUPPORTED_FLAGS) == flag);

  /* buffer size must be a power of two*/
  SVN_ERR_ASSERT((buffer_size & (buffer_size - 1)) == 0);

  /* init the file data structure */
  file = apr_pcalloc(pool, sizeof(*file));
  file->size = INVALID_OFFSET;
  file->file_name = apr_pstrdup(pool, name);
  file->name_hash = calc_hash(name);
  file->reopen_flags = (flag & ~APR_BUFFERED) | APR_BINARY | APR_XTHREAD;
  file->buffer_size = buffer_size;
  file->pool = svn_pool_create(pool);

  /* sometimes, we know that the file will be empty when opened */
  if (flag & (APR_CREATE | APR_TRUNCATE))
    file->size = 0;

  /* force file creation & check its existence */
  if (!defer_creation)
    {
      SVN_ERR(allocate_handle(&handle, file));
      SVN_ERR(release_handle(handle, TRUE));
    }

  /* auto-close on pool destruction */
  apr_pool_cleanup_register(file->pool, file, file_destructor,
                            apr_pool_cleanup_null);
  *result = file;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_file__close(svn_file_t *file)
{
  /* don't try to close the file twice */
  apr_pool_cleanup_kill(file->pool, file, file_destructor);

  /* flush buffers & close the underlying file */
  SVN_ERR(file_close_internal(file));

  /* free all buffer & temp memory */
  svn_pool_destroy(file->pool);

  return SVN_NO_ERROR;
}

static svn_boolean_t
is_single_buffer_access(svn_file_t *file,
                        apr_size_t to_read)
{
  return ((file->position + to_read) ^ file->position) < file->buffer_size;
}

static svn_error_t *
report_bytes_read(svn_file_t *file,
                  apr_size_t to_read,
                  apr_size_t actual,
                  apr_size_t *read)
{
  if (read)
    *read = actual;
  else if (to_read != actual)
    return svn_error_wrap_apr(APR_EOF, _("Incomplete read in file '%s'"),
                              svn_dirent_local_style(file->file_name,
                                                     file->pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
get_buffer(buffer_t **buffer,
           shared_handle_t **handle,
           svn_file_t *file,
           apr_off_t start_offset)
{
  apr_size_t i;
  apr_size_t to_read;
  svn_boolean_t hit_eof;
  buffer_t *result;
  assert(start_offset % file->buffer_size == 0);

  for (i = 0; i < file->buffer_count; ++i)
    if (file->buffers[i]->start_offset == start_offset)
      {
        *buffer = file->buffers[i];
        if (i > 0)
          {
            memmove(file->buffers + i, file->buffers + i - 1,
                    i * sizeof(*buffer));
            file->buffers[0] = *buffer;
          }

        return SVN_NO_ERROR;
      }

  if (!*handle)
    SVN_ERR(get_handle(handle, file));

  if (file->buffer_count < BUFFER_COUNT)
    {
      result = apr_pcalloc(file->pool, sizeof(*result));
      result->data = apr_pcalloc(file->pool, file->buffer_size);
      result->size = file->buffer_size;
    }
  else
    {
      result = file->buffers[BUFFER_COUNT-1];
      if (result->modified)
        SVN_ERR(flush_buffer(*handle, result, file->pool));
    }

  result->start_offset = start_offset;
  result->used = 0;

  SVN_ERR(handle_seek(*handle, start_offset));

  to_read = file->size == INVALID_OFFSET
          ? result->size
          : result->start_offset + result->size <= file->size
              ? result->size
              : (apr_size_t)(file->size - result->start_offset);
    
  SVN_ERR(svn_io_file_read_full2((*handle)->file, result->data, to_read,
                                 &result->used, &hit_eof, file->pool));
  (*handle)->position += result->used;

  if (result->used < result->size)
    file->size = result->used + result->start_offset;

  if (file->buffer_count > 1)
    memmove(file->buffers + file->buffer_count - 1,
            file->buffers + file->buffer_count - 2,
            (file->buffer_count - 1) * sizeof(result));

  file->buffers[0] = result;
  *buffer = result;

  return SVN_NO_ERROR;
}

static svn_error_t *
buffered_read(shared_handle_t **handle,
              svn_file_t *file,
              void *data,
              apr_size_t to_read,
              apr_size_t *read)
{
  buffer_t *buffer;
  apr_size_t to_copy;
  apr_size_t offset = file->position & ~file->buffer_size;

  SVN_ERR(get_buffer(&buffer, handle, file, file->position - offset));
  SVN_ERR_ASSERT(offset <= buffer->used);

  to_copy = MIN(buffer->used - offset, to_read);
  memcpy(data, buffer->data + offset, to_copy);
  file->position += to_copy;

  SVN_ERR(report_bytes_read(file, to_read, to_copy, read));

  return SVN_NO_ERROR;
}

static svn_error_t *
require_read_access(svn_file_t *file)
{
  return (file->reopen_flags & APR_READ)
    ? SVN_NO_ERROR
    : svn_error_wrap_apr(APR_EACCES, _("No read access to file '%s'"),
                         svn_dirent_local_style(file->file_name, file->pool));
}

svn_error_t *
svn_file__read(svn_file_t *file,
               void *data,
               apr_size_t to_read,
               apr_size_t *read,
               svn_boolean_t *hit_eof)
{
  shared_handle_t *handle = NULL;
  SVN_ERR(require_read_access(file));

  if (is_single_buffer_access(file, to_read))
    {
      SVN_ERR(buffered_read(&handle, file, data, to_read, read));
    }
  else
    {
      buffer_t *buffer;
      apr_size_t i;
      apr_off_t file_size = 0;
      apr_off_t final_position = 0;
      apr_size_t initial_to_read = to_read;

      /* restrict the read operation to what we can do inside EOF */
      SVN_ERR(svn_file__get_size(&file_size, file));
      if (file_size < file->position + to_read)
        to_read = file->size - file->position;

      SVN_ERR(report_bytes_read(file, initial_to_read, to_read, read));
      final_position = file->position + to_read;

      /* copy data from existing buffers to DATA */
      sort_buffers(file->buffers, file->buffer_count);

      for (i = 0; i < BUFFER_COUNT && file->buffers[i]; ++i)
        {
          buffer = file->buffers[i];
          if (   buffer->start_offset <= file->position
              && buffer->start_offset + buffer->used > file->position)
            {
              apr_size_t offset = file->position - buffer->start_offset;
              apr_size_t to_copy = buffer->used - offset;
              memcpy(data, file->buffers[i]->data + offset, to_copy);

              file->position += to_copy;
              data = (char *)data + to_copy;
            }
        }
          
      for (i = BUFFER_COUNT; i > 0; --i)
        {
          apr_off_t end = file->position + to_read;
          buffer = file->buffers[i-1];
          if (   buffer->start_offset < end
              && buffer->start_offset + buffer->used >= end)
            {
              apr_size_t to_copy = end - buffer->start_offset;
              assert(buffer->used == to_copy);
              memcpy(data, file->buffers[i]->data, to_copy);
              to_copy -= to_copy;
            }
        }

      /* flush data from middle buffers */
      for (i = 0; i < BUFFER_COUNT && file->buffers[i]; ++i)
        {
          buffer = file->buffers[i];
          if (   buffer->start_offset + buffer->used > file->position
              && buffer->start_offset < file->position + to_read)
            {
              if (!handle)
                SVN_ERR(get_handle(&handle, file));
              SVN_ERR(flush_buffer(handle, buffer, file->pool));
            }
        }

      /* read & buffer incomplete start block */
      if (to_read && (file->position & ~file->buffer_size))
        {
          apr_size_t data_read;
          SVN_ERR(buffered_read(&handle, file, data, to_read, &data_read));
          data = (char *)data + data_read;
          to_read -= data_read;
        }

      /* Read complete inner blocks without buffering them.
       * If the last block is a full block, we will read it into a
       * buffer further down to allow for back & forth navigation.
       */
      if (to_read > file->buffer_size)
        {
          apr_size_t data_read;
          if (!handle)
            SVN_ERR(get_handle(&handle, file));

          SVN_ERR(handle_seek(handle, file->position));
          SVN_ERR(svn_io_file_read_full2(handle->file, data,
                                         to_read & -file->buffer_size,
                                         &data_read, NULL, file->pool));

          file->position += data_read;
          handle->position = file->position;

          data = (char *)data + data_read;
          to_read -= data_read;
        }

      /* read & buffer incomplete end block */
      if (to_read)
        {
          apr_size_t data_read;
          SVN_ERR(buffered_read(&handle, file, data, to_read, &data_read));
        }

      file->position = final_position;
    }

  if (handle)
    SVN_ERR(release_handle(handle, TRUE));

  /* do this *after* we released the handle to prevent the creation of
   * a second handle
   */
  if (hit_eof)
    SVN_ERR(svn_file__at_eof(hit_eof, file));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_file__getc(svn_file_t *file,
               char *data)
{
  apr_size_t offset = file->position & ~file->buffer_size;
  apr_off_t block_start = file->position - offset;

  SVN_ERR(require_read_access(file));

  /* if we read a file linearly using getc(), the data will almost
   * certainly (>99.99%) be in the first buffer.
   */
  if (   file->buffers[0]
      && file->buffers[0]->start_offset == block_start
      && file->buffers[0]->used > offset)
    {
      *data = file->buffers[0]->data[offset];
      ++file->position;

      return SVN_NO_ERROR;
    }

  /* Handle all other cases using the standard read mechanism.
   * This will also prime the first buffer for future getc() - if there
   * is any data left to be read.
   */
  SVN_ERR(svn_file__read(file, &data, 1, NULL, NULL));

  return SVN_NO_ERROR;
}

static svn_error_t *
require_write_access(svn_file_t *file)
{
  return (file->reopen_flags & APR_WRITE)
    ? SVN_NO_ERROR
    : svn_error_wrap_apr(APR_EACCES, _("No write access to file '%s'"),
                         svn_dirent_local_style(file->file_name, file->pool));
}

static svn_error_t *
buffered_write(shared_handle_t **handle,
               svn_file_t *file,
               const char *data,
               apr_size_t to_write)
{
  buffer_t *buffer;
  apr_size_t offset = file->position & ~file->buffer_size;

  SVN_ERR(get_buffer(&buffer, handle, file, file->position - offset));
  SVN_ERR_ASSERT(offset <= buffer->used);

  memcpy(buffer->data + offset, data, to_write);
  buffer->modified = TRUE;
  buffer->used = MAX(buffer->used, offset + to_write);
  
  file->position += to_write;
  if (file->size != INVALID_OFFSET && file->size < file->position)
    file->size = file->position;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_file__write(svn_file_t *file,
                const void *data,
                apr_size_t to_write)
{
  shared_handle_t *handle = NULL;
  SVN_ERR(require_write_access(file));

  if (is_single_buffer_access(file, to_write))
    {
      SVN_ERR(buffered_write(&handle, file, data, to_write));
    }
  else
    {
      buffer_t *buffer;
      apr_size_t i;

      /* update existing buffers with data from existing buffers to DATA */
      sort_buffers(file->buffers, file->buffer_count);

      for (i = 0; i < BUFFER_COUNT && file->buffers[i]; ++i)
        {
          buffer = file->buffers[i];
          if (buffer->start_offset <= file->position)
            {
              if (buffer->start_offset + buffer->used >= file->position)
                {
                  apr_size_t buffer_left = buffer->start_offset + buffer->size
                                         - file->position;
                  apr_size_t to_copy = MIN(to_write, buffer_left);
                  apr_size_t offset = file->position - buffer->start_offset;

                  if (to_copy)
                    {
                      memcpy(buffer->data + offset, data, to_copy);
                      buffer->used = MAX(buffer->used, offset + to_copy);
                      buffer->modified = TRUE;

                      if (buffer->used == buffer->size)
                        {
                          file->position += to_copy;
                          to_write -= to_copy;
                          data = (const char *)data + to_copy;
                        }
                    }
                }
            }
          else if (buffer->start_offset < file->position + to_write)
            {
              apr_size_t offset = file->position - buffer->start_offset;
              apr_size_t to_copy = MIN(buffer->size, to_write - offset);

              memcpy(buffer->data, (const char *)data + offset, to_copy);
              buffer->used = MAX(buffer->used, to_copy);
              buffer->modified = TRUE;
            }
        }

      /* write remaining data to disk */
      if (to_write)
        {
          SVN_ERR(get_handle(&handle, file));
          SVN_ERR(handle_seek(handle, file->position));
          SVN_ERR(svn_io_file_write_full(handle->file, data, to_write,
                                         NULL, file->pool));

          file->position += to_write;
        }

      /* update file size info */
      if (file->size != INVALID_OFFSET)
        file->size = MAX(file->size, file->position);
    }

  if (handle)
    SVN_ERR(release_handle(handle, TRUE));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_file__putc(svn_file_t *file,
               char data)
{
  apr_size_t offset = file->position & ~file->buffer_size;

  SVN_ERR(require_write_access(file));

  /* if we write a file linearly using putc(), the data will almost
   * certainly (>99.99%) be in the first buffer.
   */
  if ((file->buffers[0]->start_offset ^ file->position) < file->buffer_size)
    {
      assert(offset <= file->buffers[0]->used);
      file->buffers[0]->data[offset] = data;

      if (offset == file->buffers[0]->used)
        file->buffers[0]->used++;

      if (file->size == file->position)
        ++file->size;
      ++file->position;

      return SVN_NO_ERROR;
    }

  /* Handle all other cases using the standard read mechanism.
   * This will also prime the first buffer for future putc().
   */
  SVN_ERR(svn_file__write(file, &data, 1));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_file__get_size(apr_off_t *file_size,
                   svn_file_t *file)
{
  if (file->size == INVALID_OFFSET)
    {
      shared_handle_t *handle;
      apr_off_t offset = 0;
      
      SVN_ERR(get_handle(&handle, file));
      SVN_ERR(svn_io_file_seek(handle->file, APR_END, &offset, file->pool));
      handle->position = offset;
      file->size = offset;
      SVN_ERR(release_handle(handle, TRUE));
    }

  *file_size = file->size;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_file__seek(svn_file_t *file,
               apr_off_t position)
{
  SVN_ERR_ASSERT(position >= 0);
  file->position = position;

  return SVN_NO_ERROR;
}

apr_off_t
svn_file__get_position(svn_file_t *file)
{
  return file->position;
}

svn_error_t *
svn_file__truncate(svn_file_t *file)
{
  shared_handle_t *handle;
  apr_size_t i;

  if (file->position == file->size)
    return SVN_NO_ERROR;

  /* shorten the file */
  SVN_ERR(get_handle(&handle, file));
  SVN_ERR(svn_io_file_trunc(handle->file, file->position, file->pool));
  handle->position = file->position;
  SVN_ERR(release_handle(handle, TRUE));
  
  file->size = file->position;

  /* truncate buffers accordingly */
  for (i = 0; i < file->buffer_count; ++i)
    if (file->buffers[i]->start_offset >= file->position)
      {
        file->buffers[i]->start_offset = file->position;
        file->buffers[i]->used = 0;
        file->buffers[i]->modified = FALSE;
      }
    else if (   file->buffers[i]->start_offset + file->buffers[i]->used
             >= file->position)
      {
        file->buffers[i]->used
          = file->position - file->buffers[i]->start_offset;
      }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_file__at_eof(svn_boolean_t *eof,
                 svn_file_t *file)
{
  apr_off_t file_size;
  SVN_ERR(svn_file__get_size(&file_size, file));
  *eof = file_size <= file->position;

  return SVN_NO_ERROR;
}
