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

#include "svn_error.h"

/* Hash keys for certificate information returned by svn_x509_parse_cert().
 * @since New in 1.9 */
#define SVN_X509_CERTINFO_KEY_SUBJECT     "subject"
#define SVN_X509_CERTINFO_KEY_ISSUER      "issuer"
#define SVN_X509_CERTINFO_KEY_VALID_FROM  "valid-from"
#define SVN_X509_CERTINFO_KEY_VALID_TO    "valid-to"
#define SVN_X509_CERTINFO_KEY_SHA1_DIGEST "sha1-digest"
#define SVN_X509_CERTINFO_KEY_HOSTNAMES   "hostnames"

#ifdef __cplusplus
extern "C" {
#endif

/* Parse x509 @a der certificate data from @a buf with length @a buflen
 * and return certificate information in @a *cert, allocated in
 * @a result_pool. The certinfo hash contains values of type
 * 'const char *' keyed by SVN_X509_CERTINFO_KEY_* macros. */
svn_error_t *
svn_x509_parse_cert(apr_hash_t **certinfo,
                    const char *buf,
                    apr_size_t buflen,
                    apr_pool_t *result_pool,
                    apr_pool_t *scratch_pool);

#ifdef __cplusplus
}
#endif
#endif        /* SVN_X509_H */
