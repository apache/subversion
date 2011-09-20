/*
 * svn_subr_private.h : private definitions from libsvn_subr
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

#ifndef SVN_SUBR_PRIVATE_H
#define SVN_SUBR_PRIVATE_H

#include "svn_types.h"


#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/** Spill-to-file Buffers
 *
 * @defgroup svn_spillbuf_t Spill-to-file Buffers
 * @{
 */

/** A buffer that collects blocks of content, possibly using a file.
 *
 * The spill-buffer is created with two basic parameters: the size of the
 * blocks that will be written into the spill-buffer ("blocksize"), and
 * the (approximate) maximum size that will be allowed in memory ("maxsize").
 * Once the maxsize is reached, newly written content will be "spilled"
 * into a temporary file.
 *
 * When writing, content will be buffered into memory unless a given write
 * will cause the amount of in-memory content to exceed the specified
 * maxsize. At that point, the file is created, and the content will be
 * written to that file.
 *
 * To read information back out of a spill buffer, there are two approaches
 * available to the application:
 *
 *   *) reading blocks using svn_spillbuf_read() (a "pull" model)
 *   *) having blocks passed to a callback via svn_spillbuf_process()
 *      (a "push" model to your application)
 *
 * In both cases, the spill-buffer will provide you with a block of N bytes
 * that you must fully consume before asking for more data. The callback
 * style provides for a "stop" parameter to temporarily pause the reading
 * until another read is desired. The two styles of reading may be mixed,
 * as the caller desires.
 *
 * Writes may be interleaved with reading, and content will be returned
 * in a FIFO manner. Thus, if content has been placed into the spill-buffer
 * you will always read the earliest-written data, and any newly-written
 * content will be appended to the buffer.
 *
 * Note: the file is created in the same pool where the spill-buffer was
 * created. If the content is completely read from that file, it will be
 * closed and deleted. Should writing further content cause another spill
 * file to be created, that will increase the size of the pool. There is
 * no bound on the amount of file-related resources that may be consumed
 * from the pool. It is entirely related to the read/write pattern and
 * whether spill files are repeatedly created.
 */
typedef struct svn_spillbuf_t svn_spillbuf_t;


/* Create a spill buffer.  */
svn_spillbuf_t *
svn_spillbuf_create(apr_size_t blocksize,
                    apr_size_t maxsize,
                    apr_pool_t *result_pool);


/* Determine whether the spill buffer has any content.

   Note: there is an edge case for a false positive. If the spill file was
   read *just* to the end of the file, but not past... then the spill
   buffer will not realize that no further content exists in the spill file.
   In this situation, svn_spillbuf_is_empty() will return TRUE, but an
   attempt to read content will detect that it has been exhausted.  */
svn_boolean_t
svn_spillbuf_is_empty(const svn_spillbuf_t *buf);


/* Write some data into the spill buffer.  */
svn_error_t *
svn_spillbuf_write(svn_spillbuf_t *buf,
                   const char *data,
                   apr_size_t len,
                   apr_pool_t *scratch_pool);


/* Read a block of memory from the spill buffer. @a data will be set to
   NULL if no content remains. Otherwise, @data and @len will point to
   data that must be fully-consumed by the caller. This data will remain
   valid until another call to svn_spillbuf_write(), svn_spillbuf_read(),
   or svn_spillbuf_process(), or if the spill buffer's pool is cleared.  */
svn_error_t *
svn_spillbuf_read(const char **data,
                  apr_size_t *len,
                  svn_spillbuf_t *buf,
                  apr_pool_t *scratch_pool);


/* Callback for reading content out of the spill buffer. Set @a stop if
   you want to stop the processing (and will call svn_spillbuf_process
   again, at a later time).  */
typedef svn_error_t * (*svn_spillbuf_read_t)(svn_boolean_t *stop,
                                             void *baton,
                                             const char *data,
                                             apr_size_t len,
                                             apr_pool_t *scratch_pool);


/* Process the content stored in the spill buffer. @a exhausted will be
   set to TRUE if all of the content is processed by @a read_func. This
   function may return early if the callback returns TRUE for its 'stop'
   parameter.  */
svn_error_t *
svn_spillbuf_process(svn_boolean_t *exhausted,
                     svn_spillbuf_t *buf,
                     svn_spillbuf_read_t read_func,
                     void *read_baton,
                     apr_pool_t *scratch_pool);

/** @} */


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_SUBR_PRIVATE_H */
