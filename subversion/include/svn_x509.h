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
 * @file svn_x509.h
 * @brief Subversion's X509 parser
 */

#ifndef SVN_X509_H
#define SVN_X509_H

#include <apr_pools.h>
#include <apr_tables.h>
#include <apr_time.h>

#include "svn_error.h"
#include "svn_checksum.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SVN_X509_OID_COMMON_NAME  "2.5.4.3"
#define SVN_X509_OID_COUNTRY      "2.5.4.6"
#define SVN_X509_OID_LOCALITY     "2.5.4.7"
#define SVN_X509_OID_STATE        "2.5.4.8"
#define SVN_X509_OID_ORGANIZATION "2.5.4.10"
#define SVN_X509_OID_ORG_UNIT     "2.5.4.11"
#define SVN_X509_OID_EMAIL        "1.2.840.113549.1.9.1"

/**
 * Representation of parsed certificate info.
 *
 * @since New in 1.9.
 */
typedef struct svn_x509_certinfo_t svn_x509_certinfo_t;

/**
 * Parse x509 @a der certificate data from @a buf with length @a
 * buflen and return certificate information in @a *certinfo,
 * allocated in @a result_pool.
 *
 * @note This function has been written with the intent of display data in a
 *       certificate for a user to see.  As a result, it does not do much
 *       validation on the data it parses from the certificate.  It does not
 *       for instance verify that the certificate is signed by the issuer.  It
 *       does not verify a trust chain.  It does not error on critical
 *       extensions it does not know how to parse.  So while it can be used as
 *       part of a certificate validation scheme, it can't be used alone for
 *       that purpose.
 *
 * @since New in 1.9.
 */
svn_error_t *
svn_x509_parse_cert(svn_x509_certinfo_t **certinfo,
                    const char *buf,
                    apr_size_t buflen,
                    apr_pool_t *result_pool,
                    apr_pool_t *scratch_pool);

/**
 * Returns a deep copy of @a certinfo, allocated in @a result_pool.
 * May use @a scratch_pool for temporary allocations.
 * @since New in 1.9.
 */
svn_x509_certinfo_t *
svn_x509_certinfo_dup(const svn_x509_certinfo_t *certinfo,
                      apr_pool_t *result_pool,
                      apr_pool_t *scratch_pool);

/**
 * Returns the subject DN from @a certinfo.
 * @since New in 1.9.
 */
const char *
svn_x509_certinfo_get_subject(const svn_x509_certinfo_t *certinfo,
                              apr_pool_t *result_pool);

/**
 * Returns a list of the the object IDs of the attributes available
 * for the subject in the @a certinfo.  The oids in the list are C
 * strings with dot separated integers.
 *
 * @since New in 1.9.
 */
const apr_array_header_t *
svn_x509_certinfo_get_subject_oids(const svn_x509_certinfo_t *certinfo);

/**
 * Returns the value of the attribute with the object ID specified in
 * @a oid of the subject from @a certinfo.  @a oid is a string of dot
 * separated integers.
 *
 * @since New in 1.9.
 */
const char *
svn_x509_certinfo_get_subject_attr(const svn_x509_certinfo_t *certinfo,
                                   const char *oid);

/**
 * Returns the cerficiate issuer DN from @a certinfo.
 * @since New in 1.9.
 */
const char *
svn_x509_certinfo_get_issuer(const svn_x509_certinfo_t *certinfo,
                             apr_pool_t *result_pool);

/**
 * Returns a list of the the object IDs of the attributes available
 * for the issuer in the @a certinfo.  The oids in the list are C
 * strings with dot separated integers.
 *
 * @since New in 1.9.
 */
const apr_array_header_t *
svn_x509_certinfo_get_issuer_oids(const svn_x509_certinfo_t *certinfo);

/**
 * Returns the value of the attribute with the object ID specified in
 * @a oid of the issuer from @a certinfo.  @a oid is a string of dot
 * separated integers.
 *
 * @since New in 1.9.
 */
const char *
svn_x509_certinfo_get_issuer_attr(const svn_x509_certinfo_t *certinfo,
                                  const char *oid);

/**
 * Returns the start of the certificate validity period from @a certinfo.
 *
 * @since New in 1.9.
 */
apr_time_t
svn_x509_certinfo_get_valid_from(const svn_x509_certinfo_t *certinfo);

/**
 * Returns the end of the certificate validity period from @a certinfo.
 *
 * @since New in 1.9.
 */
const apr_time_t
svn_x509_certinfo_get_valid_to(const svn_x509_certinfo_t *certinfo);

/**
 * Returns the digest (fingerprint) from @a certinfo
 * @since New in 1.9.
 */
const svn_checksum_t *
svn_x509_certinfo_get_digest(const svn_x509_certinfo_t *certinfo);

/**
 * Returns an array of (const char*) host names from @a certinfo.
 *
 * @since New in 1.9.
 */
const apr_array_header_t *
svn_x509_certinfo_get_hostnames(const svn_x509_certinfo_t *certinfo);

#ifdef __cplusplus
}
#endif
#endif        /* SVN_X509_H */
