/*
 * checksum_context.c:   the checksum context
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

#define APR_WANT_BYTEFUNC

#include <apr_md5.h>
#include <apr_sha1.h>

#include "svn_checksum.h"
#include "svn_error.h"

#include "checksum.h"
#include "fnv1a.h"

#include "svn_private_config.h"



struct svn_checksum_ctx_t
{
  void *apr_ctx;
  svn_checksum_kind_t kind;
};

svn_checksum_ctx_t *
svn_checksum_ctx_create(svn_checksum_kind_t kind,
                        apr_pool_t *pool)
{
  svn_checksum_ctx_t *ctx = apr_palloc(pool, sizeof(*ctx));

  ctx->kind = kind;
  switch (kind)
    {
      case svn_checksum_md5:
        ctx->apr_ctx = svn_checksum__md5_ctx_create(pool);
        break;

      case svn_checksum_sha1:
        ctx->apr_ctx = svn_checksum__sha1_ctx_create(pool);
        break;

      case svn_checksum_fnv1a_32:
        ctx->apr_ctx = svn_fnv1a_32__context_create(pool);
        break;

      case svn_checksum_fnv1a_32x4:
        ctx->apr_ctx = svn_fnv1a_32x4__context_create(pool);
        break;

      default:
        SVN_ERR_MALFUNCTION_NO_RETURN();
    }

  return ctx;
}

svn_error_t *
svn_checksum_ctx_reset(svn_checksum_ctx_t *ctx)
{
  switch (ctx->kind)
    {
      case svn_checksum_md5:
        svn_checksum__md5_ctx_reset(ctx->apr_ctx);
        break;

      case svn_checksum_sha1:
        svn_checksum__sha1_ctx_reset(ctx->apr_ctx);
        break;

      case svn_checksum_fnv1a_32:
        svn_fnv1a_32__context_reset(ctx->apr_ctx);
        break;

      case svn_checksum_fnv1a_32x4:
        svn_fnv1a_32x4__context_reset(ctx->apr_ctx);
        break;

      default:
        SVN_ERR_MALFUNCTION();
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_checksum_update(svn_checksum_ctx_t *ctx,
                    const void *data,
                    apr_size_t len)
{
  switch (ctx->kind)
    {
      case svn_checksum_md5:
        svn_checksum__md5_ctx_update(ctx->apr_ctx, data, len);
        break;

      case svn_checksum_sha1:
        svn_checksum__sha1_ctx_update(ctx->apr_ctx, data, len);
        break;

      case svn_checksum_fnv1a_32:
        svn_fnv1a_32__update(ctx->apr_ctx, data, len);
        break;

      case svn_checksum_fnv1a_32x4:
        svn_fnv1a_32x4__update(ctx->apr_ctx, data, len);
        break;

      default:
        /* We really shouldn't get here, but if we do... */
        return svn_error_create(SVN_ERR_BAD_CHECKSUM_KIND, NULL, NULL);
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_checksum_final(svn_checksum_t **checksum,
                   const svn_checksum_ctx_t *ctx,
                   apr_pool_t *pool)
{
  *checksum = svn_checksum_create(ctx->kind, pool);

  switch (ctx->kind)
    {
      case svn_checksum_md5:
        SVN_ERR(svn_checksum__md5_ctx_final(
            (unsigned char *)(*checksum)->digest, ctx->apr_ctx));
        break;

      case svn_checksum_sha1:
        SVN_ERR(svn_checksum__sha1_ctx_final(
            (unsigned char *)(*checksum)->digest, ctx->apr_ctx));
        break;

      case svn_checksum_fnv1a_32:
        *(apr_uint32_t *)(*checksum)->digest
          = htonl(svn_fnv1a_32__finalize(ctx->apr_ctx));
        break;

      case svn_checksum_fnv1a_32x4:
        *(apr_uint32_t *)(*checksum)->digest
          = htonl(svn_fnv1a_32x4__finalize(ctx->apr_ctx));
        break;

      default:
        /* We really shouldn't get here, but if we do... */
        return svn_error_create(SVN_ERR_BAD_CHECKSUM_KIND, NULL, NULL);
    }

  return SVN_NO_ERROR;
}

