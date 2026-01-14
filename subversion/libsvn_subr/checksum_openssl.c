/*
 * checksum_openssl.c:   OpenSSL backed checksums
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

#include "svn_private_config.h"
#ifdef SVN_CHECKSUM_BACKEND_OPENSSL

#define APR_WANT_BYTEFUNC

#include <apr_md5.h>
#include <apr_sha1.h>

#include "svn_error.h"
#include "checksum.h"

/* There is an alternative way to compute checksums in OpenSSL which is to use
 * their EVP interface. Here are the arguments why we are sticking with this
 * one even though it's deprecated in OpenSSL v3.0:
 *
 * - EVP provides much more complicated interface.
 *
 * - We don't need the most of the features it gives us.
 *
 * - It might affect performance because there are vtable calls involved.
 *
 * - The default implementation used in EVP actually relies the exact same set
 *   of functions under the hood.
 */

#define OPENSSL_SUPPRESS_DEPRECATED
#include <openssl/sha.h>
#include <openssl/md5.h>


/*** MD5 checksum ***/
svn_error_t *
svn_checksum__md5(unsigned char *digest,
                  const void *data,
                  apr_size_t len)
{
  MD5_CTX ctx;

  MD5_Init(&ctx);
  MD5_Update(&ctx, data, len);
  MD5_Final(digest, &ctx);

  return SVN_NO_ERROR;
}

struct svn_checksum__md5_ctx_t
{
  MD5_CTX md5_ctx;
};

svn_checksum__md5_ctx_t *
svn_checksum__md5_ctx_create(apr_pool_t *pool)
{
  svn_checksum__md5_ctx_t *result = apr_palloc(pool, sizeof(*result));
  MD5_Init(&result->md5_ctx);
  return result;
}

svn_error_t *
svn_checksum__md5_ctx_reset(svn_checksum__md5_ctx_t *ctx)
{
  memset(ctx, 0, sizeof(*ctx));
  MD5_Init(&ctx->md5_ctx);
  return SVN_NO_ERROR;
}

svn_error_t *
svn_checksum__md5_ctx_update(svn_checksum__md5_ctx_t *ctx,
                             const void *data,
                             apr_size_t len)
{
  MD5_Update(&ctx->md5_ctx, data, len);
  return SVN_NO_ERROR;
}

svn_error_t *
svn_checksum__md5_ctx_final(unsigned char *digest,
                            svn_checksum__md5_ctx_t *ctx)
{
  MD5_Final(digest, &ctx->md5_ctx);
  return SVN_NO_ERROR;
}


/*** SHA1 checksum ***/
svn_error_t *
svn_checksum__sha1(unsigned char *digest,
                   const void *data,
                   apr_size_t len)
{
  SHA_CTX ctx;

  SHA1_Init(&ctx);
  SHA1_Update(&ctx, data, len);
  SHA1_Final(digest, &ctx);

  return SVN_NO_ERROR;
}

struct svn_checksum__sha1_ctx_t
{
  SHA_CTX sha_ctx;
};

svn_checksum__sha1_ctx_t *
svn_checksum__sha1_ctx_create(apr_pool_t *pool)
{
  svn_checksum__sha1_ctx_t *result = apr_palloc(pool, sizeof(*result));
  SHA1_Init(&result->sha_ctx);
  return result;
}

svn_error_t *
svn_checksum__sha1_ctx_reset(svn_checksum__sha1_ctx_t *ctx)
{
  memset(ctx, 0, sizeof(*ctx));
  SHA1_Init(&ctx->sha_ctx);
  return SVN_NO_ERROR;
}

svn_error_t *
svn_checksum__sha1_ctx_update(svn_checksum__sha1_ctx_t *ctx,
                              const void *data,
                              apr_size_t len)
{
  SHA1_Update(&ctx->sha_ctx, data, len);
  return SVN_NO_ERROR;
}

svn_error_t *
svn_checksum__sha1_ctx_final(unsigned char *digest,
                             svn_checksum__sha1_ctx_t *ctx)
{

  SHA1_Final(digest, &ctx->sha_ctx);
  return SVN_NO_ERROR;
}

#endif /* SVN_CHECKSUM_BACKEND_OPENSSL */

