/*
 * checksum.c: working with WC checksums
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

#include "svn_wc.h"

static svn_wc_checksum_kind_t *
make_checksum_kind(svn_checksum_kind_t value,
                   const svn_string_t *salt,
                   apr_pool_t *result_pool)
{
  svn_wc_checksum_kind_t *result;

  result = apr_pcalloc(result_pool, sizeof(*result));
  result->value = value;
  result->salt = svn_string_dup(salt, result_pool);

  return result;
}

svn_wc_checksum_kind_t *
svn_wc_checksum_kind_create(svn_checksum_kind_t value,
                            const svn_string_t *salt,
                            apr_pool_t *result_pool)
{
  return make_checksum_kind(value, salt, result_pool);
}

svn_wc_checksum_kind_t *
svn_wc_checksum_kind_dup(const svn_wc_checksum_kind_t *kind,
                         apr_pool_t *result_pool)
{
  if (kind)
    return make_checksum_kind(kind->value, kind->salt, result_pool);
  else
    return NULL;
}
