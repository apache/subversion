/*
 * svn_subr_privae.h : private definitions from libsvn_subr
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


typedef struct svn_spillbuf_t svn_spillbuf_t;


/* Create a spill buffer.  */
svn_spillbuf_t *
svn_spillbuf_create(apr_size_t blocksize,
                    apr_size_t maxsize,
                    apr_pool_t *result_pool);


/* Determine whether the spill buffer has any content.

   Note: once content spills to a file, this will always return TRUE, even
   if the spill file has been read/consumed.  */
svn_boolean_t
svn_spillbuf_is_empty(const svn_spillbuf_t *buf);


/* Write some data into the spill buffer.  */
svn_error_t *
svn_spillbuf_write(svn_spillbuf_t *buf,
                   const char *data,
                   apr_size_t len,
                   apr_pool_t *scratch_pool);


/* Callback for reading content out of the spill buffer. Set @a stop if
   you want to stop the processing (and will call svn_spillbuf_process
   again, at a later time).  */
typedef svn_error_t * (*svn_spillbuf_read_t)(svn_boolean_t *stop,
                                             void *baton,
                                             const char *data,
                                             apr_size_t len);


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


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_SUBR_PRIVATE_H */
