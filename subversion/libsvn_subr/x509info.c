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

#include "svn_string.h"
#include "svn_hash.h"
#include "x509.h"



/* Array elements are assumed to be nul-terminated C strings. */
static apr_array_header_t *
deep_copy_array(apr_array_header_t *s, apr_pool_t *result_pool)
{
  int i;
  apr_array_header_t *d;

  if (!s)
    return NULL;

  d = apr_array_copy(result_pool, s);

  /* Make a deep copy of the strings in the array. */
  for (i = 0; i < s->nelts; ++i)
    {
      APR_ARRAY_IDX(d, i, const char *) =
        apr_pstrdup(result_pool, APR_ARRAY_IDX(s, i, const char *));
    }

  return d;
}

/* Hash key and value are assumed to be nul-terminated C strings. */
static apr_hash_t *deep_copy_hash(apr_hash_t *s,
                                  apr_pool_t *scratch_pool, 
                                  apr_pool_t *result_pool)
{
  apr_hash_t *d;
  apr_hash_index_t *i;

  if (!s)
    return NULL;

  d = apr_hash_make(result_pool);
  i = apr_hash_first(scratch_pool, s);

  /* Make a deep copy of the hash keys and values. */
  while (i)
    {
      const void *key;
      void *val;
      apr_ssize_t klen;

      apr_hash_this(i, &key, &klen, &val);
      apr_hash_set(d, apr_pstrndup(result_pool, key, klen), klen,
                   apr_pstrdup(result_pool, val));
      i = apr_hash_next(i);
    }

  return d;
}

svn_x509_certinfo_t *
svn_x509_certinfo_dup(const svn_x509_certinfo_t *certinfo,
                      apr_pool_t *result_pool,
                      apr_pool_t *scratch_pool)
{
  svn_x509_certinfo_t *result = apr_palloc(result_pool, sizeof(*result));
  result->subject_oids = deep_copy_array(certinfo->subject_oids, result_pool);
  result->subject = deep_copy_hash(certinfo->subject, scratch_pool, result_pool);
  result->issuer_oids = deep_copy_array(certinfo->issuer_oids, result_pool);
  result->issuer = deep_copy_hash(certinfo->issuer, scratch_pool, result_pool);
  result->valid_from = certinfo->valid_from;
  result->valid_to = certinfo->valid_to;
  result->digest = svn_checksum_dup(certinfo->digest, result_pool);
  result->hostnames = deep_copy_array(certinfo->hostnames, result_pool);

  return result;
}

typedef struct asn1_oid {
  const char *oid_string;
  const char *short_label;
  const char *long_label;
} asn1_oid;

static const asn1_oid asn1_oids[] = {
  { SVN_X509_OID_COMMON_NAME,  "CN", "commonName" },
  { SVN_X509_OID_COUNTRY,      "C",  "countryName" },
  { SVN_X509_OID_LOCALITY,     "L",  "localityName" },
  { SVN_X509_OID_STATE,        "ST", "stateOrProvinceName" },
  { SVN_X509_OID_ORGANIZATION, "O",  "organizationName" },
  { SVN_X509_OID_ORG_UNIT,     "OU", "organizationalUnitName"},
  { SVN_X509_OID_EMAIL,        NULL, "emailAddress" },
  { NULL },
};

static const asn1_oid *oid_string_to_asn1_oid(const char *oid_string)
{
  const asn1_oid *oid;

  for (oid = asn1_oids; oid->oid_string; oid++)
    {
      if (strcmp(oid_string, oid->oid_string) == 0)
        return oid;
    }

  return NULL;
}

static const char *oid_string_to_best_label(const char *oid_string)
{
  const asn1_oid *oid = oid_string_to_asn1_oid(oid_string);

  if (oid)
    {
      if (oid->short_label)
        return oid->short_label;

      if (oid->long_label)
        return oid->long_label;
    }

  return oid_string;
}

/*
 * Store the name from dn in printable form into buf,
 * using scratch_pool for any temporary allocations.
 * If CN is not NULL, return any common name in CN
 */
static const char *
get_dn(apr_array_header_t *oids,
       apr_hash_t *hash,
       apr_pool_t *result_pool)
{
  svn_stringbuf_t *buf = svn_stringbuf_create_empty(result_pool);
  int n;

  for (n = 0; n < oids->nelts; n++)
    {
      const char *field = APR_ARRAY_IDX(oids, n, const char *);

      if (n > 0)
        svn_stringbuf_appendcstr(buf, ", ");

      svn_stringbuf_appendcstr(buf, oid_string_to_best_label(field));
      svn_stringbuf_appendbyte(buf, '=');
      svn_stringbuf_appendcstr(buf, svn_hash_gets(hash, field));
    }

  return buf->data;
}

const char *
svn_x509_certinfo_get_subject(const svn_x509_certinfo_t *certinfo,
                              apr_pool_t *result_pool)
{
  return get_dn(certinfo->subject_oids, certinfo->subject, result_pool);
}

const apr_array_header_t *
svn_x509_certinfo_get_subject_oids(const svn_x509_certinfo_t *certinfo)
{
  return certinfo->subject_oids;
}

const char *
svn_x509_certinfo_get_subject_attr(const svn_x509_certinfo_t *certinfo,
                                   const char *oid)
{
  return svn_hash_gets(certinfo->subject, oid);
}

const char *
svn_x509_certinfo_get_issuer(const svn_x509_certinfo_t *certinfo,
                             apr_pool_t *result_pool)
{
  return get_dn(certinfo->issuer_oids, certinfo->issuer, result_pool);
}

const apr_array_header_t *
svn_x509_certinfo_get_issuer_oids(const svn_x509_certinfo_t *certinfo)
{
  return certinfo->issuer_oids;
}

const char *
svn_x509_certinfo_get_issuer_attr(const svn_x509_certinfo_t *certinfo,
                                  const char *oid)
{
  return svn_hash_gets(certinfo->issuer, oid);
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
