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
 */

#ifndef SVN_JAVAHL_UTILITY_HPP
#define SVN_JAVAHL_UTILITY_HPP

#include <apr_hash.h>

#include "Pool.h"

namespace JavaHL {
namespace Util {

/**
 * Converts keyword/valuue pairs in the the Java map @a jkeywords to
 * an APR hash table allocated in @a pool. The keys in the resulting
 * table are @c const @c char*, the values are @c svn_string_t*. Null
 * values in the Java map are converted to empty strings.
 *
 * @since New in 1.9.
 */
apr_hash_t*
make_keyword_hash(::Java::Env env, jobject jkeywords, apr_pool_t* pool);

/**
 * Converts keyword/valuue pairs in the the Java map @a jkeywords to
 * an APR hash table allocated in @a pool. The keys in the resulting
 * table are @c const @c char*, the values are @c svn_string_t*. Null
 * values in the Java map are converted to empty strings.
 *
 * @since New in 1.9.
 */
inline apr_hash_t*
make_keyword_hash(::Java::Env env, jobject jkeywords,
                   const ::SVN::Pool& pool)
{
  return make_keyword_hash(env, jkeywords, pool.getPool());
}


/**
 * Converts property/value pairs the Java map @a jproperties to an APR
 * hash table allocated in @a pool. The keys in the resulting table
 * are @c const @c char*, the values are @c svn_string_t*. Null values
 * in the Java map will not appear in the converted map.
 *
 * @since New in 1.9.
 */
apr_hash_t*
make_property_hash(::Java::Env env, jobject jproperties, apr_pool_t* pool);

/**
 * Converts property/value pairs the Java map @a jproperties to an APR
 * hash table allocated in @a pool. The keys in the resulting table
 * are @c const @c char*, the values are @c svn_string_t*. Null values
 * in the Java map will not appear in the converted map.
 *
 * @since New in 1.9.
 */
inline apr_hash_t*
make_property_hash(::Java::Env env, jobject jproperties,
                   const ::SVN::Pool& pool)
{
  return make_property_hash(env, jproperties, pool.getPool());
}

} // namespace Util
} // namespace JavaHL

#endif // SVN_JAVAHL_JNI_UTILITY_HPP
