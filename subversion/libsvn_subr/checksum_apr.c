/*
 * checksum_apr.c:   APR backed checksums
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

#include <limits.h>

#include "svn_private_config.h"
#ifdef SVN_CHECKSUM_BACKEND_APR

#define APR_WANT_BYTEFUNC

#include <apr_md5.h>
#include <apr_sha1.h>

#include "svn_error.h"
#include "checksum.h"


/*** MD5 checksum ***/
svn_error_t *
svn_checksum__md5(unsigned char *digest,
                  const void *data,
                  apr_size_t len)
{
  apr_md5(digest, data, len);
  return SVN_NO_ERROR;
}

struct svn_checksum__md5_ctx_t
{
  apr_md5_ctx_t apr_ctx;
};

svn_checksum__md5_ctx_t *
svn_checksum__md5_ctx_create(apr_pool_t *pool)
{
  svn_checksum__md5_ctx_t *result = apr_palloc(pool, sizeof(*result));
  apr_md5_init(&result->apr_ctx);
  return result;
}

svn_error_t *
svn_checksum__md5_ctx_reset(svn_checksum__md5_ctx_t *ctx)
{
  memset(ctx, 0, sizeof(*ctx));
  apr_md5_init(&ctx->apr_ctx);
  return SVN_NO_ERROR;
}

svn_error_t *
svn_checksum__md5_ctx_update(svn_checksum__md5_ctx_t *ctx,
                             const void *data,
                             apr_size_t len)
{
  apr_md5_update(&ctx->apr_ctx, data, len);
  return SVN_NO_ERROR;
}

svn_error_t *
svn_checksum__md5_ctx_final(unsigned char *digest,
                            svn_checksum__md5_ctx_t *ctx)
{
  apr_md5_final(digest, &ctx->apr_ctx);
  return SVN_NO_ERROR;
}


/*** SHA1 checksum ***/
svn_error_t *
svn_checksum__sha1(unsigned char *digest,
                   const void *data,
                   apr_size_t len)
{
  apr_sha1_ctx_t sha1_ctx;

  /* Do not blindly truncate the data length. */
  SVN_ERR_ASSERT(len < UINT_MAX);

  apr_sha1_init(&sha1_ctx);
  apr_sha1_update(&sha1_ctx, data, (unsigned int)len);
  apr_sha1_final(digest, &sha1_ctx);
  return SVN_NO_ERROR;
}

struct svn_checksum__sha1_ctx_t
{
  apr_sha1_ctx_t apr_ctx;
};

svn_checksum__sha1_ctx_t *
svn_checksum__sha1_ctx_create(apr_pool_t *pool)
{
  svn_checksum__sha1_ctx_t *result = apr_palloc(pool, sizeof(*result));
  apr_sha1_init(&result->apr_ctx);
  return (svn_checksum__sha1_ctx_t *)result;
}

svn_error_t *
svn_checksum__sha1_ctx_reset(svn_checksum__sha1_ctx_t *ctx)
{
  memset(ctx, 0, sizeof(*ctx));
  apr_sha1_init(&ctx->apr_ctx);
  return SVN_NO_ERROR;
}

svn_error_t *
svn_checksum__sha1_ctx_update(svn_checksum__sha1_ctx_t *ctx,
                              const void *data,
                              apr_size_t len)
{
  /* Do not blindly truncate the data length. */
  SVN_ERR_ASSERT(len < UINT_MAX);
  apr_sha1_update(&ctx->apr_ctx, data, (unsigned int)len);
  return SVN_NO_ERROR;
}

svn_error_t *
svn_checksum__sha1_ctx_final(unsigned char *digest,
                             svn_checksum__sha1_ctx_t *ctx)
{
  apr_sha1_final(digest, &ctx->apr_ctx);
  return SVN_NO_ERROR;
}

#endif /* SVN_CHECKSUM_BACKEND_APR */
