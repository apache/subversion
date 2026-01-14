/*
 * checksum_bcrypt.c:   BCrypt backed checksums
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
#ifdef SVN_CHECKSUM_BACKEND_BCRYPT

#include <windows.h>
#include <bcrypt.h>

#include "svn_error.h"
#include "private/svn_atomic.h"
#include "checksum.h"

static svn_error_t *
handle_error(NTSTATUS status)
{
  if (BCRYPT_SUCCESS(status))
    return SVN_NO_ERROR;
  else
    return svn_error_create(SVN_ERR_BCRYPT, NULL, NULL);
}


/* State of the algorithm as we load it. */
typedef struct algorithm_state_t
{
  LPCWSTR alg_name;
  BCRYPT_ALG_HANDLE alg_handle;

  svn_atomic_t initialized;

  DWORD hash_length;
  DWORD object_length;
} algorithm_state_t;

static algorithm_state_t md5 = { BCRYPT_MD5_ALGORITHM, 0, 0, 0, 0 };
static algorithm_state_t sha1 = { BCRYPT_SHA1_ALGORITHM, 0, 0, 0, 0 };

/* This implements svn_atomic__err_init_func_t */
static svn_error_t *
algorithm_init(void *baton, apr_pool_t *null_pool)
{
  algorithm_state_t *state = (algorithm_state_t *)baton;
  ULONG cb_result;

  SVN_ERR(handle_error(BCryptOpenAlgorithmProvider(&state->alg_handle,
                                                   state->alg_name,
                                                   MS_PRIMITIVE_PROVIDER,
                                                   /* dwFlags */ 0)));

  SVN_ERR(handle_error(BCryptGetProperty(state->alg_handle,
                                         BCRYPT_HASH_LENGTH,
                                         (PUCHAR) &state->hash_length,
                                         sizeof(state->hash_length),
                                         &cb_result,
                                         /* dwFlags */ 0)));

  SVN_ERR(handle_error(BCryptGetProperty(state->alg_handle,
                                         BCRYPT_OBJECT_LENGTH,
                                         (PUCHAR) &state->object_length,
                                         sizeof(state->object_length),
                                         &cb_result,
                                         /* dwFlags */ 0)));
  return SVN_NO_ERROR;
}


/* An abstract wrapper over BCrypt checksum API. */
typedef struct bcrypt_ctx_t
{
  BCRYPT_HASH_HANDLE handle;
  void *object_buf;
  apr_pool_t *pool;
} bcrypt_ctx_t;

static svn_error_t *
bcrypt_ctx_init(algorithm_state_t *algorithm,
                bcrypt_ctx_t *ctx)
{
  BCRYPT_HASH_HANDLE handle;

  SVN_ERR(svn_atomic__init_once(&algorithm->initialized, algorithm_init,
                                algorithm, NULL));

  if (! ctx->object_buf)
    ctx->object_buf = apr_pcalloc(ctx->pool, algorithm->object_length);

  SVN_ERR(handle_error(BCryptCreateHash(algorithm->alg_handle,
                                        &handle,
                                        ctx->object_buf, algorithm->object_length,
                                        /* pbSecret */ NULL,
                                        /* cbSecret */ 0,
                                        /* dwFlags */ 0)));

  ctx->handle = handle;
  return SVN_NO_ERROR;
}

static svn_error_t *
bcrypt_ctx_update(algorithm_state_t *algorithm,
                  bcrypt_ctx_t *ctx,
                  const void *data,
                  apr_size_t len)
{
  SVN_ERR_ASSERT(len <= ULONG_MAX);

  if (! ctx->handle)
    SVN_ERR(bcrypt_ctx_init(algorithm, ctx));

  SVN_ERR(handle_error(BCryptHashData(ctx->handle,
                                      (PUCHAR) data,
                                      (ULONG) len,
                                      /* dwFlags */ 0)));

  return SVN_NO_ERROR;
}

static svn_error_t *
bcrypt_ctx_final(algorithm_state_t *algorithm,
                 bcrypt_ctx_t *ctx,
                 unsigned char *digest)
{
  if (! ctx->handle)
    SVN_ERR(bcrypt_ctx_init(algorithm, ctx));

  SVN_ERR(handle_error(BCryptFinishHash(ctx->handle,
                                        (PUCHAR) digest,
                                        algorithm->hash_length,
                                        /* dwFlags */ 0)));

  return SVN_NO_ERROR;
}

static svn_error_t *
bcrypt_ctx_reset(algorithm_state_t *algorithm, bcrypt_ctx_t *ctx)
{
  memset(ctx->object_buf, 0, algorithm->object_length);
  ctx->handle = NULL;
  return SVN_NO_ERROR;
}

static svn_error_t *
bcrypt_checksum(algorithm_state_t *algorithm,
                unsigned char *digest,
                const void *data,
                apr_size_t len)
{
  BCRYPT_HANDLE handle;
  void *object_buf;

  SVN_ERR_ASSERT(len <= ULONG_MAX);

  SVN_ERR(svn_atomic__init_once(&algorithm->initialized, algorithm_init,
                                algorithm, NULL));

  SVN_ERR_ASSERT(algorithm->object_length < 4096);
  object_buf = alloca(algorithm->object_length);

  SVN_ERR(handle_error(BCryptCreateHash(algorithm->alg_handle,
                                        &handle,
                                        object_buf, algorithm->object_length,
                                        /* pbSecret */ NULL,
                                        /* cbSecret */ 0,
                                        /* dwFlags */ 0)));

  SVN_ERR(handle_error(BCryptHashData(handle,
                                      (PUCHAR) data,
                                      (ULONG) len,
                                      /* dwFlags */ 0)));

  SVN_ERR(handle_error(BCryptFinishHash(handle,
                                        (PUCHAR) digest,
                                        algorithm->hash_length,
                                        /* dwFlags */ 0)));

  return SVN_NO_ERROR;
}


/*** MD5 checksum ***/
svn_error_t *
svn_checksum__md5(unsigned char *digest,
                  const void *data,
                  apr_size_t len)
{
  return svn_error_trace(bcrypt_checksum(&md5, digest, data, len));
}

struct svn_checksum__md5_ctx_t
{
  bcrypt_ctx_t bcrypt_ctx;
};

svn_checksum__md5_ctx_t *
svn_checksum__md5_ctx_create(apr_pool_t *pool)
{
  svn_checksum__md5_ctx_t *ctx = apr_pcalloc(pool, sizeof(*ctx));
  ctx->bcrypt_ctx.pool = pool;
  return ctx;
}

svn_error_t *
svn_checksum__md5_ctx_reset(svn_checksum__md5_ctx_t *ctx)
{
  return svn_error_trace(bcrypt_ctx_reset(&md5, &ctx->bcrypt_ctx));
}

svn_error_t *
svn_checksum__md5_ctx_update(svn_checksum__md5_ctx_t *ctx,
                             const void *data,
                             apr_size_t len)
{
  return svn_error_trace(bcrypt_ctx_update(&md5, &ctx->bcrypt_ctx,
                                           data, len));
}

svn_error_t *
svn_checksum__md5_ctx_final(unsigned char *digest,
                            svn_checksum__md5_ctx_t *ctx)
{
  return svn_error_trace(bcrypt_ctx_final(&md5, &ctx->bcrypt_ctx,
                                          digest));
}


/*** SHA1 checksum ***/
svn_error_t *
svn_checksum__sha1(unsigned char *digest,
                   const void *data,
                   apr_size_t len)
{
  return svn_error_trace(bcrypt_checksum(&sha1, digest, data, len));
}

struct svn_checksum__sha1_ctx_t
{
  bcrypt_ctx_t bcrypt_ctx;
};

svn_checksum__sha1_ctx_t *
svn_checksum__sha1_ctx_create(apr_pool_t *pool)
{
  svn_checksum__sha1_ctx_t *ctx = apr_pcalloc(pool, sizeof(*ctx));
  ctx->bcrypt_ctx.pool = pool;
  return ctx;
}

svn_error_t *
svn_checksum__sha1_ctx_reset(svn_checksum__sha1_ctx_t *ctx)
{
  return svn_error_trace(bcrypt_ctx_reset(&sha1, &ctx->bcrypt_ctx));
}

svn_error_t *
svn_checksum__sha1_ctx_update(svn_checksum__sha1_ctx_t *ctx,
                              const void *data,
                              apr_size_t len)
{
  return svn_error_trace(bcrypt_ctx_update(&sha1, &ctx->bcrypt_ctx,
                                           data, len));
}

svn_error_t *
svn_checksum__sha1_ctx_final(unsigned char *digest,
                             svn_checksum__sha1_ctx_t *ctx)
{
  return svn_error_trace(bcrypt_ctx_final(&sha1, &ctx->bcrypt_ctx,
                                          digest));
}

#endif /* SVN_CHECKSUM_BACKEND_APR */
