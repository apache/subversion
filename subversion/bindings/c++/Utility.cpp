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
 * @file Utility.cpp
 * @brief Implementation of the class Utility
 */

#include "Utility.h"
#include "Pool.h"

#include <map>
#include <iostream>


static svn_error_t *
write_handler_ostream(void *baton, const char *data, apr_size_t *len)
{
  std::ostream *ostream = reinterpret_cast<std::ostream *>(baton);

  ostream->write(data, *len);
  return SVN_NO_ERROR;
}

namespace SVN
{

namespace Private
{

namespace Utility
{

svn_stream_t *
ostream_wrapper(std::ostream &ostream, Pool &pool)
{
  svn_stream_t *stream;

  stream = svn_stream_create(&ostream, pool.pool());
  svn_stream_set_write(stream, write_handler_ostream);

  return stream;
}

apr_array_header_t *
make_string_array(const std::vector<std::string> &vec, Pool &pool)
{
  apr_array_header_t *arr = apr_array_make(pool.pool(), vec.size(),
                                           sizeof(const char *));

  for (std::vector<std::string>::const_iterator it = vec.begin();
       it < vec.end(); ++it)
    {
      APR_ARRAY_PUSH(arr, const char *) = it->c_str();
    }

  return arr;
}

apr_hash_t *
make_prop_table(const PropTable &props, Pool &pool)
{
  apr_hash_t *hash = apr_hash_make(pool.pool());

  for (PropTable::const_iterator it = props.begin();
       it != props.end(); ++it)
    {
      svn_string_t *str = svn_string_ncreate(it->second.data(),
                                             it->second.size(), pool.pool());
      apr_hash_set(hash, it->first.c_str(), APR_HASH_KEY_STRING, str);
    }

  return hash;
}

}
}
}
