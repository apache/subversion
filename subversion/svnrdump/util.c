/*
 *  util.c: A collection of utility functions for svnrdump.
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

#include "svn_repos.h"
#include "svn_hash.h"

#include "svnrdump.h"

svn_error_t *
dump_props(struct dump_edit_baton *eb,
           svn_boolean_t *trigger_var,
           svn_boolean_t dump_data_too,
           apr_pool_t *pool)
{
  svn_stream_t *propstream;

  if (trigger_var && !*trigger_var)
    return SVN_NO_ERROR;

  svn_stringbuf_setempty(eb->propstring);
  propstream = svn_stream_from_stringbuf(eb->propstring, eb->pool);
  SVN_ERR(svn_hash_write_incremental(eb->properties, eb->del_properties,
                                     propstream, "PROPS-END", pool));
  SVN_ERR(svn_stream_close(propstream));
  
  /* Prop-delta: true */
  SVN_ERR(svn_stream_printf(eb->stream, pool,
          SVN_REPOS_DUMPFILE_PROP_DELTA
          ": true\n"));

  /* Prop-content-length: 193 */
  SVN_ERR(svn_stream_printf(eb->stream, pool,
          SVN_REPOS_DUMPFILE_PROP_CONTENT_LENGTH
          ": %" APR_SIZE_T_FMT "\n", eb->propstring->len));

  if (dump_data_too) {
    /* Content-length: 14 */
    SVN_ERR(svn_stream_printf(eb->stream, pool,
            SVN_REPOS_DUMPFILE_CONTENT_LENGTH
            ": %" APR_SIZE_T_FMT "\n\n",
            eb->propstring->len));

    /* The properties. */
    SVN_ERR(svn_stream_write(eb->stream, eb->propstring->data,
           &(eb->propstring->len)));

    /* Cleanup so that data is never dumped twice. */
    svn_hash__clear(eb->properties, pool);
    svn_hash__clear(eb->del_properties, pool);
    if (trigger_var)
      *trigger_var = FALSE;
  }
  return SVN_NO_ERROR;
}
