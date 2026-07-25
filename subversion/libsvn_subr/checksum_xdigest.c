/*
 * checksum_xdigest.c:   xdigest backed checksums
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
#ifdef SVN_CHECKSUM_BACKEND_XDIGEST

#include <apr_md5.h>
#include <apr_sha1.h>

#include "private/svn_atomic.h"
#include "svn_error.h"
#include "checksum.h"

#include <xdigest.h>
#include <xdigest_md5.h>
#include <xdigest_sha1.h>

static svn_atomic_t init_status = 0;

/* This implements svn_atomic__void_init_func_t */
static void
xdigest_init_once(void *null_baton)
{
  xdig_init();
}

static void
ensure_xdigest_init(void)
{
  svn_atomic__init_once_void(&init_status, xdigest_init_once, NULL);
}

/*** MD5 checksum ***/
svn_error_t *
svn_checksum__md5(unsigned char *digest,
                  const void *data,
                  apr_size_t len)
{
  ensure_xdigest_init();

  xdig_md5(digest, data, len);
  return SVN_NO_ERROR;
}

svn_checksum__md5_ctx_t *
svn_checksum__md5_ctx_create(apr_pool_t *pool)
{
  ensure_xdigest_init();

  xdig_md5_ctx_t *result = apr_palloc(pool, xdig_md5_ctx_size());
  xdig_md5_ctx_init(result);
  return (svn_checksum__md5_ctx_t *)result;
}

svn_error_t *
svn_checksum__md5_ctx_reset(svn_checksum__md5_ctx_t *ctx)
{
  memset(ctx, 0, xdig_md5_ctx_size());
  xdig_md5_ctx_init((xdig_md5_ctx_t *)ctx);
  return SVN_NO_ERROR;
}

svn_error_t *
svn_checksum__md5_ctx_update(svn_checksum__md5_ctx_t *ctx,
                             const void *data,
                             apr_size_t len)
{
  xdig_md5_ctx_update((xdig_md5_ctx_t *)ctx, data, len);
  return SVN_NO_ERROR;
}

svn_error_t *
svn_checksum__md5_ctx_final(unsigned char *digest,
                            svn_checksum__md5_ctx_t *ctx)
{
  xdig_md5_ctx_final((xdig_md5_ctx_t *)ctx, digest);
  return SVN_NO_ERROR;
}


/*** SHA1 checksum ***/
svn_error_t *
svn_checksum__sha1(unsigned char *digest,
                   const void *data,
                   apr_size_t len)
{
  ensure_xdigest_init();

  xdig_sha1(digest, data, len);
  return SVN_NO_ERROR;
}

svn_checksum__sha1_ctx_t *
svn_checksum__sha1_ctx_create(apr_pool_t *pool)
{
  ensure_xdigest_init();

  xdig_sha1_ctx_t *result = apr_palloc(pool, xdig_sha1_ctx_size());
  xdig_sha1_ctx_init(result);
  return (svn_checksum__sha1_ctx_t *)result;
}

svn_error_t *
svn_checksum__sha1_ctx_reset(svn_checksum__sha1_ctx_t *ctx)
{
  memset(ctx, 0, xdig_sha1_ctx_size());
  xdig_sha1_ctx_init((xdig_sha1_ctx_t *)ctx);
  return SVN_NO_ERROR;
}

svn_error_t *
svn_checksum__sha1_ctx_update(svn_checksum__sha1_ctx_t *ctx,
                              const void *data,
                              apr_size_t len)
{
  xdig_sha1_ctx_update((xdig_sha1_ctx_t *)ctx , data, len);
  return SVN_NO_ERROR;
}

svn_error_t *
svn_checksum__sha1_ctx_final(unsigned char *digest,
                             svn_checksum__sha1_ctx_t *ctx)
{
  xdig_sha1_ctx_final((xdig_sha1_ctx_t *)ctx, digest);
  return SVN_NO_ERROR;
}

#endif /* SVN_CHECKSUM_BACKEND_XDIGEST */
