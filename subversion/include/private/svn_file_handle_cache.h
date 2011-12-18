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
 * @file svn_file_handle_cache.h
 * @brief File handle cache API
 */

#ifndef SVN_FILE_HANDLE_CACHE_H
#define SVN_FILE_HANDLE_CACHE_H

#include "svn_io.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */



/**
 * @defgroup svn_file_handle_cache Caching of open file handles
 * @{
 */

/**
 * An opaque structure representing a cache for open file handles.
 */
typedef struct svn_file_handle_cache_t svn_file_handle_cache_t;

/**
 * An opaque structure representing a cached file handle being used
 * by the calling application.
 */
typedef
struct svn_file_handle_cache__handle_t svn_file_handle_cache__handle_t;

/**
 * Get an open buffered read file handle in @a f, for the file named
 * @a fname. The file pointer will be moved to the specified @a offset,
 * if it is different from -1.
 */
svn_error_t *
svn_file_handle_cache__open(svn_file_handle_cache__handle_t **f,
                            svn_file_handle_cache_t *cache,
                            const char *fname,
                            apr_off_t offset,
                            apr_pool_t *pool);

/**
 * Return the APR level file handle underlying the cache file handle @a f.
 * Returns NULL, if @a f is NULL, has already been closed or otherwise
 * invalidated.
 */
apr_file_t *
svn_file_handle_cache__get_apr_handle(svn_file_handle_cache__handle_t *f);

/**
 * Return the name of the file that the cached handle @a f refers to.
 * Returns NULL, if @a f is NULL, has already been closed or otherwise
 * invalidated.
 */
const char *
svn_file_handle_cache__get_name(svn_file_handle_cache__handle_t *f);

/** Create a stream from a cached file handle.  For convenience, if @a file
 * is @c NULL, an empty stream created by svn_stream_empty() is returned.
 *
 * This function should normally be called with @a disown set to FALSE,
 * in which case closing the stream will also return the file handle to
 * the respective cache object.
 *
 * If @a disown is TRUE, the stream will disown the file handle, meaning 
 * that svn_stream_close() will not close the cached file handle.
 */
svn_stream_t *
svn_stream__from_cached_file_handle(svn_file_handle_cache__handle_t *file,
                                    svn_boolean_t disown,
                                    apr_pool_t *pool);

/**
 * Return the cached file handle @a f to the cache. Depending on the number
 * of open handles, the underlying handle may actually get closed. If @a f
 * is NULL, already closed or an invalidated handle, this is a no-op.
 */
svn_error_t *
svn_file_handle_cache__close(svn_file_handle_cache__handle_t *f);

/**
 * Close all file handles pertaining to the given FILE_NAME currently not
 * held by the application and disconnect those held by the application
 * from CACHE.
 */
svn_error_t *
svn_file_handle_cache__flush(svn_file_handle_cache_t *cache, 
                             const char *file_name);

/**
 * Creates a new file handle cache in @a cache. Up to @a max_handles
 * file handles will be kept open. All cache-internal memory allocations 
 * during the caches lifetime will be done from @a pool.
 *
 * If the caller ensures that there are no concurrent accesses to the
 * cache, @a thread_safe may be @c FALSE. Otherwise, it must be @c TRUE.
 */
svn_error_t *
svn_file_handle_cache__create_cache(svn_file_handle_cache_t **cache,
                                    size_t max_handles,
                                    svn_boolean_t thread_safe,
                                    apr_pool_t *pool);

/** @} */


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_FILE_HANDLE_CACHE_H */
