/*
 *  util.c: A few utility functions.
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

#include "svn_error.h"
#include "svn_pools.h"
#include "svn_string.h"
#include "svn_props.h"
#include "svn_subst.h"

#include "svnrdump.h"


/* Normalize the line ending style of the values of properties in PROPS
 * that "need translation" (according to svn_prop_needs_translation(),
 * currently all svn:* props) so that they contain only LF (\n) line endings.
 */
svn_error_t *
svn_rdump__normalize_props(apr_hash_t *props,
                           apr_pool_t *pool)
{
  apr_hash_index_t *hi;

  for (hi = apr_hash_first(pool, props); hi; hi = apr_hash_next(hi))
    {
      const char *key = svn__apr_hash_index_key(hi);
      const svn_string_t *value = svn__apr_hash_index_val(hi);

      if (svn_prop_needs_translation(key))
        {
          const char *cstring;

          SVN_ERR(svn_subst_translate_cstring2(value->data, &cstring,
                                               "\n", TRUE,
                                               NULL, FALSE,
                                               pool));
          value = svn_string_create(cstring, pool);
          apr_hash_set(props, key, APR_HASH_KEY_STRING, value);
        }
    }
  return SVN_NO_ERROR;
}
