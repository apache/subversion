/*
 * x509info.c:  Accessors for svn_x509_certinfo_t
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



#include <string.h>

#include <apr_pools.h>
#include <apr_tables.h>

#include "svn_x509.h"
#include "x509.h"



svn_x509_certinfo_t *
svn_x509_certinfo_dup(const svn_x509_certinfo_t *certinfo,
                      apr_pool_t *result_pool,
                      apr_pool_t *scratch_pool)
{
  svn_x509_certinfo_t *result = apr_palloc(result_pool, sizeof(*result));
  result->subject = apr_pstrdup(result_pool, certinfo->subject);
  result->subject_cn = apr_pstrdup(result_pool, certinfo->subject_cn);
  result->issuer = apr_pstrdup(result_pool, certinfo->issuer);
  result->valid_from = certinfo->valid_from;
  result->valid_to = certinfo->valid_to;
  result->digest = svn_checksum_dup(certinfo->digest, result_pool);

  if (!certinfo->hostnames)
    result->hostnames = NULL;
  else
    {
      int i;
      result->hostnames = apr_array_copy(result_pool, certinfo->hostnames);

      /* Make a deep copy of the strings in the array. */
      for (i = 0; i < result->hostnames->nelts; ++i)
        APR_ARRAY_IDX(result->hostnames, i, const char*) =
          apr_pstrdup(result_pool,
                      APR_ARRAY_IDX(result->hostnames, i, const char*));
    }

  return result;
}

const char *
svn_x509_certinfo_get_subject(const svn_x509_certinfo_t *certinfo)
{
  return certinfo->subject;
}

const char *
svn-x509_certinfo_get_subject_cn(const svn_x509_certinfo_t *certinfo)
{
  return certinfo->subject_cn;
}

const char *
svn_x509_certinfo_get_issuer(const svn_x509_certinfo_t *certinfo)
{
  return certinfo->issuer;
}

apr_time_t
svn_x509_certinfo_get_valid_from(const svn_x509_certinfo_t *certinfo)
{
  return certinfo->valid_from;
}

const apr_time_t
svn_x509_certinfo_get_valid_to(const svn_x509_certinfo_t *certinfo)
{
  return certinfo->valid_to;
}

const svn_checksum_t *
svn_x509_certinfo_get_digest(const svn_x509_certinfo_t *certinfo)
{
  return certinfo->digest;
}

const apr_array_header_t *
svn_x509_certinfo_get_hostnames(const svn_x509_certinfo_t *certinfo)
{
  return certinfo->hostnames;
}
