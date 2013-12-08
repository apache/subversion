/**
 * @copyright
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
 * @endcopyright
 *
 * @file svn_io_private.h
 * @brief Private IO API
 */

#ifndef SVN_IO_PRIVATE_H
#define SVN_IO_PRIVATE_H

#include <apr.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* The flags to pass to apr_stat to check for executable and/or readonly */
#if defined(WIN32) || defined(__OS2__)
#define SVN__APR_FINFO_EXECUTABLE (0)
#define SVN__APR_FINFO_READONLY (0)
#define SVN__APR_FINFO_MASK_OUT (APR_FINFO_PROT | APR_FINFO_OWNER)
#else
#define SVN__APR_FINFO_EXECUTABLE (APR_FINFO_PROT)
#define SVN__APR_FINFO_READONLY (APR_FINFO_PROT | APR_FINFO_OWNER)
#define SVN__APR_FINFO_MASK_OUT (0)
#endif

/* 90% of the lines we encounter will be less than this many chars.
 *
 * Line-based functions like svn_stream_readline should fetch data in
 * blocks no longer than this.  Although using a larger prefetch size is
 * not illegal and must not break any functionality, it may be
 * significantly less efficient in certain situations.
 */
#define SVN__LINE_CHUNK_SIZE 80


/** Set @a *executable TRUE if @a file_info is executable for the
 * user, FALSE otherwise.
 *
 * Always returns FALSE on Windows or platforms without user support.
 */
svn_error_t *
svn_io__is_finfo_executable(svn_boolean_t *executable,
                            apr_finfo_t *file_info,
                            apr_pool_t *pool);

/** Set @a *read_only TRUE if @a file_info is read-only for the user,
 * FALSE otherwise.
 */
svn_error_t *
svn_io__is_finfo_read_only(svn_boolean_t *read_only,
                           apr_finfo_t *file_info,
                           apr_pool_t *pool);


/** Buffer test handler function for a generic stream. @see svn_stream_t
 * and svn_stream__is_buffered().
 *
 * @since New in 1.7.
 */
typedef svn_boolean_t (*svn_stream__is_buffered_fn_t)(void *baton);

/** Set @a stream's buffer test function to @a is_buffered_fn
 *
 * @since New in 1.7.
 */
void
svn_stream__set_is_buffered(svn_stream_t *stream,
                            svn_stream__is_buffered_fn_t is_buffered_fn);

/** Return whether this generic @a stream uses internal buffering.
 * This may be used to work around subtle differences between buffered
 * an non-buffered APR files.  A lazy-open stream cannot report the
 * true buffering state until after the lazy open: a stream that
 * initially reports as non-buffered may report as buffered later.
 *
 * @since New in 1.7.
 */
svn_boolean_t
svn_stream__is_buffered(svn_stream_t *stream);

/** Return the underlying file, if any, associated with the stream, or
 * NULL if not available.  Accessing the file bypasses the stream.
 */
apr_file_t *
svn_stream__aprfile(svn_stream_t *stream);

/** Parse a user defined command to contain dynamically created labels
 *  and filenames.  This function serves both diff and diff3 parsing
 *  requirements.
 *
 *  When used in a diff context: (responding parse tokens in braces)
 *
 *  @a label1 (%svn_label_old) refers to the label of @a tmpfile1
 *  (%svn_old) which is the pristine copy.
 *
 *  @a label2 (%svn_label_new) refers to the label of @a tmpfile2
 *  (%svn_new) which is the altered copy.
 *
 *  When used in a diff3 context:
 *
 *  @a label1 refers to the label of @a tmpfile1 which is the 'mine'
 *  copy.
 *
 *  @a label2 refers to the label of @a tmpfile2 which is the 'older'
 *  copy.
 *
 *  @a label3 (%svn_label_base) refers to the label of @a base
 *  (%svn_base) which is the 'base' copy.
 *
 *  In general:
 *
 *  @a cmd is a user defined string containing 0 or more parse tokens
 *  which are expanded by the required labels and filenames.
 * 
 *  @a pool is used for temporary allocations.
 *
 *  @return A NULL-terminated character array.
 * 
 * @since New in 1.9.
 */
const char **
svn_io__create_custom_diff_cmd(const char *label1,
                               const char *label2,
                               const char *label3,
                               const char *from,
                               const char *to,
                               const char *base,
                               const char *cmd,
                               apr_pool_t *pool);


#ifdef __cplusplus
}
#endif /* __cplusplus */


#endif /* SVN_IO_PRIVATE_H */
