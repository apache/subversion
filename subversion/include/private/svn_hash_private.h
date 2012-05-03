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
 * @file svn_hash_private.h
 * @brief Hash table related private functions.
 */


#ifndef SVN_HASH_PRIVATE_H
#define SVN_HASH_PRIVATE_H

#include <apr.h>
#include <apr_pools.h>
#include <apr_hash.h>
#include <apr_tables.h>
#include <apr_file_io.h>  /* for apr_file_t */

#include "svn_types.h"
#include "svn_io.h"       /* for svn_stream_t */


#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/** The longest the "K <number>" line can be in one of our hashdump files. */
#define SVN_KEYLINE_MAXLEN 100

/**
 * @defgroup svn_hash_support Hash table serialization support
 * @{
 */

/*----------------------------------------------------*/

/**
 * @defgroup svn_hash_misc Miscellaneous hash APIs
 * @{
 */

/**
 * Clear any key/value pairs in the hash table.  A wrapper for a
 * apr_hash_clear(), which isn't available until APR 1.3.0.
 *
 * @since New in 1.5.
 */
svn_error_t *
svn_hash__clear(apr_hash_t *hash, apr_pool_t *pool);

/** @} */


/**
 * @defgroup svn_hash_getters Specialized getter APIs for hashes
 * @{
 */

/** Find the value of a @a key in @a hash, return the value.
 *
 * If @a hash is @c NULL or if the @a key cannot be found, the
 * @a default_value will be returned.
 *
 * @since New in 1.7.
 */
const char *
svn_hash__get_cstring(apr_hash_t *hash,
                      const char *key,
                      const char *default_value);

/** Like svn_hash_get_cstring(), but for boolean values.
 *
 * Parses the value as a boolean value. The recognized representations
 * are 'TRUE'/'FALSE', 'yes'/'no', 'on'/'off', '1'/'0'; case does not
 * matter.
 *
 * @since New in 1.7.
 */
svn_boolean_t
svn_hash__get_bool(apr_hash_t *hash,
                   const char *key,
                   svn_boolean_t default_value);

/** @} */

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_HASH_PRIVATE_H */
