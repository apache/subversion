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

}
}
}
