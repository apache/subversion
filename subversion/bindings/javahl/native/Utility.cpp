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

#include <string>
#include <apr_strings.h>

#include "svn_string.h"

#include "jniwrapper/jni_array.hpp"
#include "jniwrapper/jni_string.hpp"
#include "jniwrapper/jni_string_map.hpp"

#include "Utility.hpp"


namespace JavaHL {
namespace Util {

namespace {
class MapToHashIteration
{
public:
  explicit MapToHashIteration(const svn_string_t* default_value,
                              apr_pool_t* pool)
    : m_pool(pool),
      m_hash(apr_hash_make(pool)),
      m_default(default_value)
    {}

  void operator()(const std::string& key, const Java::ByteArray& value)
    {
      const char* const safe_key =
        apr_pstrmemdup(m_pool, key.c_str(), key.size() + 1);
      if (!value.get())
        {
          if (m_default != NULL)
            apr_hash_set(m_hash, safe_key, key.size(), m_default);
        }
      else
        {
          Java::ByteArray::Contents val(value);
          apr_hash_set(m_hash, safe_key, key.size(), val.get_string(m_pool));
        }
    }

  apr_hash_t* get() const
    {
      return m_hash;
    }

private:
  apr_pool_t* const m_pool;
  apr_hash_t* const m_hash;
  const svn_string_t* const m_default;
};

typedef ::Java::ImmutableMap< ::Java::ByteArray, jbyteArray> ImmutableByteArrayMap;
} // anonymous namespace

apr_hash_t*
make_keyword_hash(::Java::Env env, jobject jkeywords, apr_pool_t* pool)
{
  const svn_string_t* const empty = svn_string_create_empty(pool);
  const ImmutableByteArrayMap keywords(env, jkeywords);
  return keywords.for_each(MapToHashIteration(empty, pool)).get();
}

apr_hash_t*
make_property_hash(::Java::Env env, jobject jproperties, apr_pool_t* pool)
{
  const ImmutableByteArrayMap props(env, jproperties);
  return props.for_each(MapToHashIteration(NULL, pool)).get();
}

} // namespace Util
} // namespace JavaHL
