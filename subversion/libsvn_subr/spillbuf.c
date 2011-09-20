/*
 * spillbuf.c : an in-memory buffer that can spill to disk
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

#include <apr_file_io.h>

#include "svn_io.h"
#include "private/svn_subr_private.h"


struct memblock_t {
  apr_size_t size;
  char *data;

  struct memblock_t *next;
};


struct svn_spillbuf_t {
  /* Pool for allocating blocks and the spill file.  */
  apr_pool_t *pool;

  /* Size of in-memory blocks.  */
  apr_size_t blocksize;

  /* Maximum in-memory size; start spilling when we reach this size.  */
  apr_size_t maxsize;

  /* The amount of content in memory.  */
  apr_size_t memory_size;

  /* HEAD points to the first block of the linked list of buffers.
     TAIL points to the last block, for quickly appending more blocks
     to the overall list.  */
  struct memblock_t *head;
  struct memblock_t *tail;

  /* Available blocks for storing pending data. These were allocated
     previously, then the data consumed and returned to this list.  */
  struct memblock_t *avail;

  /* When a block is borrowed for reading, it is listed here.  */
  struct memblock_t *out_for_reading;

  /* Once MEMORY_SIZE exceeds SPILL_SIZE, then arriving content will be
     appended to the (temporary) file indicated by SPILL.  */
  apr_file_t *spill;

  /* As we consume content from SPILL, this value indicates where we
     will begin reading.  */
  apr_off_t spill_start;
};


svn_spillbuf_t *
svn_spillbuf_create(apr_size_t blocksize,
                    apr_size_t maxsize,
                    apr_pool_t *result_pool)
{
  svn_spillbuf_t *buf = apr_pcalloc(result_pool, sizeof(*buf));

  buf->pool = result_pool;
  buf->blocksize = blocksize;
  buf->maxsize = maxsize;

  return buf;
}


svn_boolean_t
svn_spillbuf_is_empty(const svn_spillbuf_t *buf)
{
  return buf->head == NULL && buf->spill == NULL;
}


/* Get a memblock from the spill-buffer. It will be the block that we
   passed out for reading, come from the free list, or allocated.  */
static struct memblock_t *
get_buffer(svn_spillbuf_t *buf)
{
  struct memblock_t *mem = buf->out_for_reading;

  if (mem != NULL)
    {
      buf->out_for_reading = NULL;
      return mem;
    }

  if (buf->avail == NULL)
    {
      mem = apr_palloc(buf->pool, sizeof(*mem));
      mem->data = apr_palloc(buf->pool, buf->blocksize);
      return mem;
    }

  mem = buf->avail;
  buf->avail = mem->next;
  return mem;
}


/* Return MEM to the list of available buffers in BUF.  */
static void
return_buffer(svn_spillbuf_t *buf,
              struct memblock_t *mem)
{
  mem->next = buf->avail;
  buf->avail = mem;
}


svn_error_t *
svn_spillbuf_write(svn_spillbuf_t *buf,
                   const char *data,
                   apr_size_t len,
                   apr_pool_t *scratch_pool)
{
  struct memblock_t *mem;

  /* The caller should not have provided us more than we can store into
     a single memory block.  */
  SVN_ERR_ASSERT(len <= buf->blocksize);

  /* We do not (yet) have a spill file, but the amount stored in memory
     has grown too large. Create the file and place the pending data into
     the temporary file.  */
  if (buf->spill == NULL
      && buf->memory_size > buf->maxsize)
    {
      SVN_ERR(svn_io_open_unique_file3(&buf->spill,
                                       NULL /* temp_path */,
                                       NULL /* dirpath */,
                                       svn_io_file_del_on_pool_cleanup,
                                       buf->pool, scratch_pool));
    }

  /* Once a spill file has been constructed, then we need to put all
     arriving data into the file. We will no longer attempt to hold it
     in memory.  */
  if (buf->spill != NULL)
    {
      /* NOTE: we assume the file position is at the END. The caller should
         ensure this, so that we will append.  */
      SVN_ERR(svn_io_file_write_full(buf->spill, data, len,
                                     NULL, scratch_pool));
      return SVN_NO_ERROR;
    }

  /* We're still within bounds of holding the pending information in
     memory. Get a buffer, copy the data there, and link it into our
     pending data.  */
  mem = get_buffer(buf);
  /* NOTE: mem's size/next are uninitialized.  */

  mem->size = len;
  memcpy(mem->data, data, len);
  mem->next = NULL;

  /* Start a list of buffers, or append to the end of the linked list
     of buffers.  */
  if (buf->tail == NULL)
    {
      buf->head = mem;
      buf->tail = mem;
    }
  else
    {
      buf->tail->next = mem;
      buf->tail = mem;
    }

  /* We need to record how much is buffered in memory. Once we reach
     buf->maxsize (or thereabouts, it doesn't have to be precise), then
     we'll switch to putting the content into a file.  */
  buf->memory_size += len;

  return SVN_NO_ERROR;
}


/* Return a memblock of content, if any is available. *mem will be NULL if
   no further content is available. The memblock should eventually be
   passed to return_buffer() (or stored into buf->out_for_reading which
   will grab that block at the next get_buffer() call). */
static svn_error_t *
read_data(struct memblock_t **mem,
          svn_spillbuf_t *buf,
          apr_pool_t *scratch_pool)
{
  svn_error_t *err;

  /* If we have some in-memory blocks, then return one.  */
  if (buf->head != NULL)
    {
      *mem = buf->head;
      if (buf->tail == *mem)
        buf->head = buf->tail = NULL;
      else
        buf->head = (*mem)->next;

      /* We're using less memory now. If we haven't hit the spill file,
         then we may be able to keep using memory.  */
      buf->memory_size -= (*mem)->size;

      return SVN_NO_ERROR;
    }

  /* No file? Done.  */
  if (buf->spill == NULL)
    {
      *mem = NULL;
      return SVN_NO_ERROR;
    }

  /* Assume that the caller has seeked the spill file to the correct pos.  */

  /* Get a buffer that we can read content into.  */
  *mem = get_buffer(buf);
  /* NOTE: mem's size/next are uninitialized.  */

  (*mem)->size = buf->blocksize;  /* The size of (*mem)->data  */
  (*mem)->next = NULL;

  /* Read some data from the spill file into the memblock.  */
  err = svn_io_file_read(buf->spill, (*mem)->data, &(*mem)->size,
                         scratch_pool);
  if (err != NULL && APR_STATUS_IS_EOF(err->apr_err))
    {
      /* We've exhausted the file. Close it, so any new content will go
         into memory rather than the file.  */
      svn_error_clear(err);
      SVN_ERR(svn_io_file_close(buf->spill, scratch_pool));
      buf->spill = NULL;
    }
  else if (err)
    {
      return_buffer(buf, *mem);
      return svn_error_trace(err);
    }

  /* If we didn't read anything from the file, then avoid returning a
     memblock (ie. just like running out of content).  */
  if ((*mem)->size == 0)
    {
      return_buffer(buf, *mem);
      *mem = NULL;
      return SVN_NO_ERROR;
    }

  /* Mark the data that we consumed from the spill file.  */
  buf->spill_start += (*mem)->size;

  /* *mem has been initialized. Done.  */
  return SVN_NO_ERROR;
}


/* If the next read would consume data from the file, then seek to the
   correct position.  */
static svn_error_t *
maybe_seek(svn_boolean_t *seeked,
           const svn_spillbuf_t *buf,
           apr_pool_t *scratch_pool)
{
  if (buf->head == NULL && buf->spill != NULL)
    {
      apr_off_t output_unused;

      /* Seek to where we left off reading.  */
      output_unused = buf->spill_start;  /* ### stupid API  */
      SVN_ERR(svn_io_file_seek(buf->spill,
                               APR_SET, &output_unused,
                               scratch_pool));
      if (seeked != NULL)
        *seeked = TRUE;
    }
  else if (seeked != NULL)
    {
      *seeked = FALSE;
    }

  return SVN_NO_ERROR;
}


svn_error_t *
svn_spillbuf_read(const char **data,
                  apr_size_t *len,
                  svn_spillbuf_t *buf,
                  apr_pool_t *scratch_pool)
{
  struct memblock_t *mem;

  /* Possibly seek... */
  SVN_ERR(maybe_seek(NULL, buf, scratch_pool));

  SVN_ERR(read_data(&mem, buf, scratch_pool));
  if (mem == NULL)
    {
      *data = NULL;
      *len = 0;
    }
  else
    {
      *data = mem->data;
      *len = mem->size;

      /* If a block was out for reading, then return it.  */
      if (buf->out_for_reading != NULL)
        return_buffer(buf, buf->out_for_reading);

      /* Remember that we've passed this block out for reading.  */
      buf->out_for_reading = mem;
    }

  return SVN_NO_ERROR;
}


svn_error_t *
svn_spillbuf_process(svn_boolean_t *exhausted,
                     svn_spillbuf_t *buf,
                     svn_spillbuf_read_t read_func,
                     void *read_baton,
                     apr_pool_t *scratch_pool)
{
  svn_boolean_t has_seeked = FALSE;

  *exhausted = FALSE;

  while (TRUE)
    {
      struct memblock_t *mem;
      svn_error_t *err;
      svn_boolean_t stop;

      /* If this call to read_data() will read from the spill file, and we
         have not seek'd the file... then do it now.  */
      if (!has_seeked)
        SVN_ERR(maybe_seek(&has_seeked, buf, scratch_pool));

      /* Get some content to pass to the read callback.  */
      SVN_ERR(read_data(&mem, buf, scratch_pool));
      if (mem == NULL)
        {
          *exhausted = TRUE;
          return SVN_NO_ERROR;
        }

      err = read_func(&stop, read_baton, mem->data, mem->size);

      return_buffer(buf, mem);
      
      if (err)
        return svn_error_trace(err);

      /* If the callbacks told us to stop, then we're done for now.  */
      if (stop)
        return SVN_NO_ERROR;
    }

  /* NOTREACHED */
}
