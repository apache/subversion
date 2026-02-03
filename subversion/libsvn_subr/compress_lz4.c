/*
 * compress_lz4.c:  LZ4 data compression routines
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

#include <assert.h>

#include "svn_error.h"
#include "private/svn_subr_private.h"

#include "svn_private_config.h"
#include "svn_types.h"

#ifdef SVN_INTERNAL_LZ4
#include "lz4/lz4internal.h"
#include "lz4/lz4frame.h"
#include "lz4/lz4hc.h"
#else
#include <lz4.h>
#include <lz4frame.h>
#include <lz4hc.h>
#endif

/*
 * Simple compression and decompression
 */

svn_error_t *
svn__compress_lz4(const void *data, apr_size_t len,
                  svn_stringbuf_t *out)
{
  apr_size_t hdrlen;
  unsigned char buf[SVN__MAX_ENCODED_UINT_LEN];
  unsigned char *p;
  int compressed_data_len;
  int max_compressed_data_len;

  assert(len <= LZ4_MAX_INPUT_SIZE);

  p = svn__encode_uint(buf, (apr_uint64_t)len);
  hdrlen = p - buf;
  max_compressed_data_len = LZ4_compressBound((int)len);
  svn_stringbuf_setempty(out);
  svn_stringbuf_ensure(out, max_compressed_data_len + hdrlen);
  svn_stringbuf_appendbytes(out, (const char *)buf, hdrlen);
  compressed_data_len = LZ4_compress_default(data, out->data + out->len,
                                             (int)len, max_compressed_data_len);
  if (!compressed_data_len)
    return svn_error_create(SVN_ERR_LZ4_COMPRESSION_FAILED, NULL, NULL);

  if (compressed_data_len >= (int)len)
    {
      /* Compression didn't help :(, just append the original text */
      svn_stringbuf_appendbytes(out, data, len);
    }
  else
    {
      out->len += compressed_data_len;
      out->data[out->len] = 0;
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn__decompress_lz4(const void *data, apr_size_t len,
                    svn_stringbuf_t *out,
                    apr_size_t limit)
{
  apr_size_t hdrlen;
  int compressed_data_len;
  int decompressed_data_len;
  apr_uint64_t u64;
  const unsigned char *p = data;
  int rv;

  assert(len <= INT_MAX);
  assert(limit <= INT_MAX);

  /* First thing in the string is the original length.  */
  p = svn__decode_uint(&u64, p, p + len);
  if (p == NULL)
    return svn_error_create(SVN_ERR_SVNDIFF_INVALID_COMPRESSED_DATA, NULL,
                            _("Decompression of compressed data failed: "
                              "no size"));
  if (u64 > limit)
    return svn_error_create(SVN_ERR_SVNDIFF_INVALID_COMPRESSED_DATA, NULL,
                            _("Decompression of compressed data failed: "
                              "size too large"));
  decompressed_data_len = (int)u64;
  hdrlen = p - (const unsigned char *)data;
  compressed_data_len = (int)(len - hdrlen);

  svn_stringbuf_setempty(out);
  svn_stringbuf_ensure(out, decompressed_data_len);

  if (compressed_data_len == decompressed_data_len)
    {
      /* Data is in the original, uncompressed form. */
      memcpy(out->data, p, decompressed_data_len);
    }
  else
    {
      rv = LZ4_decompress_safe((const char *)p, out->data, compressed_data_len,
                               decompressed_data_len);
      if (rv < 0)
        return svn_error_create(SVN_ERR_LZ4_DECOMPRESSION_FAILED, NULL, NULL);

      if (rv != decompressed_data_len)
        return svn_error_create(SVN_ERR_SVNDIFF_INVALID_COMPRESSED_DATA,
                                NULL,
                                _("Size of uncompressed data "
                                  "does not match stored original length"));
    }

  out->data[decompressed_data_len] = 0;
  out->len = decompressed_data_len;

  return SVN_NO_ERROR;
}

/*
 * Streamy compression
 */

apr_size_t
svn_lz4__header_size_max(void)
{
  return LZ4F_HEADER_SIZE_MAX;
}

struct svn_lz4__compress_ctx_t
{
  LZ4F_cctx *ctx;
  unsigned started;
  LZ4F_preferences_t prefs;
  LZ4F_compressOptions_t options;
};

static apr_status_t free_lz4_cctx(void *data)
{
  svn_lz4__compress_ctx_t *const cctx = data;
  const LZ4F_errorCode_t code = LZ4F_freeCompressionContext(cctx->ctx);
  if (LZ4F_isError(code))
    return SVN_ERR_LZ4_COMPRESSION_FAILED;
  return APR_SUCCESS;
}

svn_error_t *
svn_lz4__compress_create(svn_lz4__compress_ctx_t **cctx_out,
                         svn_boolean_t stable_input,
                         apr_pool_t *pool)
{
  static const LZ4F_preferences_t compression_prefs = {
    {
      LZ4F_max64KB,               /* frameInfo.blockSizeID */
      LZ4F_blockLinked,           /* frameInfo.blockMode */
      LZ4F_noContentChecksum,     /* frameInfo.contentChecksumFlag */
      LZ4F_frame,                 /* frameInfo.frameType */
      0,                          /* frameInfo.contentSize */
      0,                          /* frameInfo.dictID */
      LZ4F_blockChecksumEnabled   /* frameInfo.blockChecksumFlag */
    },
    /* NOTE: With LZ4 1.10+, the compression level will be 2, faster but less
             compressed than with older versions of LZ4, where the value of
             this constant was 3. This does not affect compression format
             backward compatibility. */
    LZ4HC_CLEVEL_MIN,           /* compressionLevel */
    0,                          /* autoFlush */
    1,                          /* favorDecSpeed */
    { 0 }                       /* reserved */
  };

  LZ4F_cctx *ctx;
  svn_lz4__compress_ctx_t *cctx;
  LZ4F_errorCode_t code = LZ4F_createCompressionContext(&ctx, LZ4F_VERSION);

  if (LZ4F_isError(code))
    return svn_error_createf(SVN_ERR_LZ4_COMPRESSION_FAILED, NULL,
                             _("Create LZ4 compression context: %s"),
                             LZ4F_getErrorName(code));

  cctx = apr_pcalloc(pool, sizeof(*cctx));
  cctx->ctx = ctx;
  cctx->prefs = compression_prefs;
  cctx->options.stableSrc = !!stable_input;
  apr_pool_cleanup_register(pool, cctx, free_lz4_cctx, apr_pool_cleanup_null);
  *cctx_out = cctx;
  return SVN_NO_ERROR;
}

static SVN__FORCE_INLINE svn_error_t *
check_compress_status(apr_size_t *length, apr_size_t status, apr_size_t extra)
{
  if (LZ4F_isError(status))
    return svn_error_createf(SVN_ERR_LZ4_COMPRESSION_FAILED, NULL,
                             _("LZ4 compress: %s"),
                             LZ4F_getErrorName(status));

  *length = status + extra;
  return SVN_NO_ERROR;
}

apr_size_t
svn_lz4__compress_bound(svn_lz4__compress_ctx_t *cctx,
                        apr_size_t size)
{
  return LZ4F_compressBound(size, &cctx->prefs);
}

svn_error_t *
svn_lz4__compress_update(apr_size_t *length,
                         svn_lz4__compress_ctx_t *cctx,
                         void *output, apr_size_t capacity,
                         const void *input, apr_size_t size)
{
  apr_size_t header_size = 0;
  apr_size_t status;

  if (!cctx->started)
    {
      if (capacity < LZ4F_HEADER_SIZE_MAX)
        return svn_error_create(SVN_ERR_LZ4_COMPRESSION_FAILED, NULL,
                                _("LZ4 compress: no space for frame header"));

      status = LZ4F_compressBegin(cctx->ctx, output, capacity, &cctx->prefs);
      SVN_ERR(check_compress_status(&header_size, status, 0));
      output = (char*)output + header_size;
      capacity -= header_size;
      cctx->started = TRUE;
    }

  status = LZ4F_compressUpdate(cctx->ctx, output, capacity,
                               input, size, &cctx->options);
  return svn_error_trace(check_compress_status(length, status, header_size));
}

svn_error_t *
svn_lz4__compress_flush(apr_size_t *length,
                        svn_lz4__compress_ctx_t *cctx,
                        void *output, apr_size_t capacity)
{
  const apr_size_t status = LZ4F_flush(cctx->ctx,
                                       output, capacity,
                                       &cctx->options);
  return svn_error_trace(check_compress_status(length, status, 0));
}

svn_error_t *
svn_lz4__compress_end(apr_size_t *length,
                      svn_lz4__compress_ctx_t *cctx,
                      void *output, apr_size_t capacity)
{
  const apr_size_t status = LZ4F_compressEnd(cctx->ctx,
                                             output, capacity,
                                             &cctx->options);
  svn_error_t *const err = check_compress_status(length, status, 0);
  if (!err)
    cctx->started = FALSE;
  return svn_error_trace(err);
}

/*
 * Streamy decompression
 */

struct svn_lz4__decompress_ctx_t
{
  LZ4F_dctx* ctx;
  LZ4F_decompressOptions_t options;
};

static apr_status_t free_lz4_dctx(void *data)
{
  svn_lz4__decompress_ctx_t *const dctx = data;
  const LZ4F_errorCode_t code = LZ4F_freeDecompressionContext(dctx->ctx);
  if (LZ4F_isError(code))
    return SVN_ERR_LZ4_DECOMPRESSION_FAILED;
  return APR_SUCCESS;
}

svn_error_t *
svn_lz4__decompress_create(svn_lz4__decompress_ctx_t **dctx_out,
                           svn_boolean_t stable_output,
                           apr_pool_t *pool)
{
  LZ4F_dctx* ctx;
  svn_lz4__decompress_ctx_t *dctx;
  LZ4F_errorCode_t code = LZ4F_createDecompressionContext(&ctx, LZ4F_VERSION);

  if (LZ4F_isError(code))
    return svn_error_createf(SVN_ERR_LZ4_DECOMPRESSION_FAILED, NULL,
                             _("Create LZ4 decompression context: %s"),
                             LZ4F_getErrorName(code));

  dctx = apr_pcalloc(pool, sizeof(*dctx));
  dctx->ctx = ctx;
  dctx->options.stableDst = !!stable_output;
  apr_pool_cleanup_register(pool, dctx, free_lz4_dctx, apr_pool_cleanup_null);
  *dctx_out = dctx;
  return SVN_NO_ERROR;
}

svn_error_t *
svn_lz4__decompress(apr_size_t *size_hint,
                    svn_lz4__decompress_ctx_t *dctx,
                    void *output, apr_size_t *output_size,
                    const void *input, apr_size_t *input_size)
{
  const apr_size_t status = LZ4F_decompress(dctx->ctx,
                                            output, output_size,
                                            input, input_size,
                                            &dctx->options);

  if (LZ4F_isError(status))
    return svn_error_createf(SVN_ERR_LZ4_DECOMPRESSION_FAILED, NULL,
                             _("LZ4 decompress: %s"),
                             LZ4F_getErrorName(status));

  *size_hint = status;
  return SVN_NO_ERROR;
}

/*
 * Library version
 */

const char *
svn_lz4__compiled_version(void)
{
  static const char lz4_version_str[] = APR_STRINGIFY(LZ4_VERSION_MAJOR) "." \
                                        APR_STRINGIFY(LZ4_VERSION_MINOR) "." \
                                        APR_STRINGIFY(LZ4_VERSION_RELEASE);

  return lz4_version_str;
}

int
svn_lz4__runtime_version(void)
{
  return LZ4_versionNumber();
}
