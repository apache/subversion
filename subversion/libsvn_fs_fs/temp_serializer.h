/* temp_serializer.h : serialization functions for caching of FSFS structures
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

#ifndef SVN_LIBSVN_FS__TEMP_SERIALIZER_H
#define SVN_LIBSVN_FS__TEMP_SERIALIZER_H

#include "fs.h"

/**
 * Prepend the @a number to the @c string in a space efficient way that
 * no other (number,string) combination can produce the same result.
 * Allocate temporaries as well as the result from @a pool.
 */
const char*
svn_fs_fs__combine_number_and_string(apr_int64_t number,
                                     const char *string,
                                     apr_pool_t *pool);

/**
 * Combine the numbers @a a and @a b a space efficient way that no other
 * combination of numbers can produce the same result.
 * Allocate temporaries as well as the result from @a pool.
 */
const char*
svn_fs_fs__combine_two_numbers(apr_int64_t a,
                               apr_int64_t b,
                               apr_pool_t *pool);

/**
 * Serialize a @a noderev_p within the serialization @a context.
 */
void
svn_fs_fs__noderev_serialize(struct svn_temp_serializer__context_t *context,
                             node_revision_t * const *noderev_p);

/**
 * Deserialize a @a noderev_p within the @a buffer.
 */
void
svn_fs_fs__noderev_deserialize(void *buffer,
                               node_revision_t **noderev_p);

/**
 * @ref svn_txdelta_window_t is not sufficient for caching the data it
 * represents because data read process needs auxilliary information.
 */
typedef struct
{
  /* the txdelta window information cached / to be cached */
  svn_txdelta_window_t *window;

  /* the revision file read pointer position right after reading the window */
  apr_off_t end_offset;
} svn_fs_fs__txdelta_cached_window_t;

/**
 * Implements @ref svn_cache__serialize_fn_t for
 * @ref svn_fs_fs__txdelta_cached_window_t.
 */
svn_error_t *
svn_fs_fs__serialize_txdelta_window(char **buffer,
                                    apr_size_t *buffer_size,
                                    void *item,
                                    apr_pool_t *pool);

/**
 * Implements @ref svn_cache__deserialize_fn_t for
 * @ref svn_fs_fs__txdelta_cached_window_t.
 */
svn_error_t *
svn_fs_fs__deserialize_txdelta_window(void **item,
                                      const char *buffer,
                                      apr_size_t buffer_size,
                                      apr_pool_t *pool);

/**
 * Implements @ref svn_cache__serialize_fn_t for manifests
 * (@a apr_array_header_t).
 */
svn_error_t *
svn_fs_fs__serialize_manifest(char **data,
                              apr_size_t *data_len,
                              void *in,
                              apr_pool_t *pool);

/**
 * Implements @ref svn_cache__deserialize_fn_t for manifests
 * (@a apr_array_header_t).
 */
svn_error_t *
svn_fs_fs__deserialize_manifest(void **out,
                                const char *data,
                                apr_size_t data_len,
                                apr_pool_t *pool);

/**
 * Implements @ref svn_cache__serialize_fn_t for @ref svn_fs_id_t
 */
svn_error_t *
svn_fs_fs__serialize_id(char **data,
                        apr_size_t *data_len,
                        void *in,
                        apr_pool_t *pool);

/**
 * Implements @ref svn_cache__deserialize_fn_t for @ref svn_fs_id_t
 */
svn_error_t *
svn_fs_fs__deserialize_id(void **out,
                          const char *data,
                          apr_size_t data_len,
                          apr_pool_t *pool);

/**
 * Implements @ref svn_cache__serialize_fn_t for @ref node_revision_t
 */
svn_error_t *
svn_fs_fs__serialize_node_revision(char **buffer,
                                   apr_size_t *buffer_size,
                                   void *item,
                                   apr_pool_t *pool);

/**
 * Implements @ref svn_cache__deserialize_fn_t for @ref node_revision_t
 */
svn_error_t *
svn_fs_fs__deserialize_node_revision(void **item,
                                     const char *buffer,
                                     apr_size_t buffer_size,
                                     apr_pool_t *pool);

/**
 * Implements @ref svn_cache__serialize_func_t for a directory contents hash
 */
svn_error_t *
svn_fs_fs__serialize_dir_entries(char **data,
                                 apr_size_t *data_len,
                                 void *in,
                                 apr_pool_t *pool);

/**
 * Implements @ref svn_cache__deserialize_func_t for a directory contents hash
 */
svn_error_t *
svn_fs_fs__deserialize_dir_entries(void **out,
                                   const char *data,
                                   apr_size_t data_len,
                                   apr_pool_t *pool);

#endif
