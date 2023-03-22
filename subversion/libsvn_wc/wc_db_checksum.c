/*
 * wc_db_checksum.c: working with WC checksums
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

#define SVN_WC__I_AM_WC_DB

#include "wc_db.h"

static svn_wc__db_checksum_kind_t *
make_checksum_kind(svn_checksum_kind_t value,
                   const svn_string_t *salt,
                   apr_pool_t *result_pool)
{
  svn_wc__db_checksum_kind_t *result;

  result = apr_pcalloc(result_pool, sizeof(*result));
  result->value = value;
  result->salt = svn_string_dup(salt, result_pool);

  return result;
}

svn_wc__db_checksum_kind_t *
svn_wc__db_checksum_kind_make(svn_checksum_kind_t value,
                              const svn_string_t *salt,
                              apr_pool_t *result_pool)
{
  return make_checksum_kind(value, salt, result_pool);
}

svn_wc__db_checksum_kind_t *
svn_wc__db_checksum_kind_dup(const svn_wc__db_checksum_kind_t *kind,
                             apr_pool_t *result_pool)
{
  if (kind)
    return make_checksum_kind(kind->value, kind->salt, result_pool);
  else
    return NULL;
}

static svn_wc__db_checksum_t *
make_checksum(const svn_checksum_t *value,
              const svn_string_t *salt,
              apr_pool_t *result_pool)
{
  svn_wc__db_checksum_t *result;

  result = apr_pcalloc(result_pool, sizeof(*result));
  result->value = svn_checksum_dup(value, result_pool);
  result->salt = svn_string_dup(salt, result_pool);

  return result;
}

svn_wc__db_checksum_t *
svn_wc__db_checksum_make(const svn_checksum_t *value,
                         const svn_string_t *salt,
                         apr_pool_t *result_pool)
{
  return make_checksum(value, salt, result_pool);
}

svn_wc__db_checksum_t *
svn_wc__db_checksum_dup(const svn_wc__db_checksum_t *checksum,
                        apr_pool_t *result_pool)
{
  if (checksum)
    return make_checksum(checksum->value, checksum->salt, result_pool);
  else
    return NULL;
}

svn_boolean_t
svn_wc__db_checksum_match(const svn_wc__db_checksum_t *checksum1,
                          const svn_wc__db_checksum_t *checksum2)
{
  return svn_string_compare(checksum1->salt, checksum2->salt) &&
         svn_checksum_match(checksum1->value, checksum2->value);
}

/* Baton for the pristine checksum stream. */
typedef struct checksum_stream_baton_t
{
  apr_pool_t *pool;
  svn_stream_t *stream;
  const svn_string_t *salt;
  svn_checksum_ctx_t *read_ctx;
  svn_wc__db_checksum_t **read_checksum_p;
  svn_checksum_ctx_t *write_ctx;
  svn_wc__db_checksum_t **write_checksum_p;
} checksum_stream_baton_t;

/* Implements svn_read_fn_t. */
static svn_error_t *
checksum_stream_read_fn(void *baton, char *buffer, apr_size_t *len)
{
  checksum_stream_baton_t *b = baton;

  SVN_ERR(svn_stream_read2(b->stream, buffer, len));

  if (b->read_ctx)
    SVN_ERR(svn_checksum_update(b->read_ctx, buffer, *len));

  return SVN_NO_ERROR;
}

/* Implements svn_read_fn_t. */
static svn_error_t *
checksum_stream_read_full_fn(void *baton, char *buffer, apr_size_t *len)
{
  checksum_stream_baton_t *b = baton;

  SVN_ERR(svn_stream_read_full(b->stream, buffer, len));

  if (b->read_ctx)
    SVN_ERR(svn_checksum_update(b->read_ctx, buffer, *len));

  return SVN_NO_ERROR;
}

/* Implements svn_write_fn_t. */
static svn_error_t *
checksum_stream_write_fn(void *baton, const char *buffer, apr_size_t *len)
{
  checksum_stream_baton_t *b = baton;

  SVN_ERR(svn_stream_write(b->stream, buffer, len));

  if (b->write_ctx)
    SVN_ERR(svn_checksum_update(b->write_ctx, buffer, *len));

  return SVN_NO_ERROR;
}

/* Implements svn_stream_data_available_fn_t. */
static svn_error_t *
checksum_stream_data_available_fn(void *baton, svn_boolean_t *data_available)
{
  checksum_stream_baton_t *b = baton;

  SVN_ERR(svn_stream_data_available(b->stream, data_available));

  return SVN_NO_ERROR;
}

/* Implements svn_stream_seek_fn_t. */
static svn_error_t *
checksum_stream_seek_fn(void *baton, const svn_stream_mark_t *mark)
{
  checksum_stream_baton_t *b = baton;

  /* Only reset support. */
  if (mark)
    {
      return svn_error_create(SVN_ERR_STREAM_SEEK_NOT_SUPPORTED,
                              NULL, NULL);
    }
  else
    {
      if (b->read_ctx)
        SVN_ERR(svn_checksum_ctx_reset(b->read_ctx));

      if (b->write_ctx)
        SVN_ERR(svn_checksum_ctx_reset(b->write_ctx));

      SVN_ERR(svn_stream_reset(b->stream));
    }

  return SVN_NO_ERROR;
}

/* Implements svn_close_fn_t. */
static svn_error_t *
checksum_stream_close_fn(void *baton)
{
  checksum_stream_baton_t *b = baton;

  if (b->read_ctx)
    {
      svn_checksum_t *checksum;
      SVN_ERR(svn_checksum_final(&checksum, b->read_ctx, b->pool));
      *b->read_checksum_p = make_checksum(checksum, b->salt, b->pool);
    }

  if (b->write_ctx)
    {
      svn_checksum_t *checksum;
      SVN_ERR(svn_checksum_final(&checksum, b->write_ctx, b->pool));
      *b->write_checksum_p = make_checksum(checksum, b->salt, b->pool);
    }

  SVN_ERR(svn_stream_close(b->stream));

  return SVN_NO_ERROR;
}

static svn_stream_t *
make_checksum_stream(svn_wc__db_checksum_t **read_checksum_p,
                     svn_wc__db_checksum_t **write_checksum_p,
                     svn_stream_t *inner_stream,
                     svn_checksum_kind_t checksum_kind,
                     const svn_string_t *salt,
                     apr_pool_t *result_pool,
                     apr_pool_t *scratch_pool)
{
  checksum_stream_baton_t *baton;
  svn_stream_t *checksum_stream;

  if (!read_checksum_p && !write_checksum_p)
    return inner_stream;

  baton = apr_pcalloc(result_pool, sizeof(*baton));
  baton->pool = result_pool;
  baton->stream = inner_stream;
  baton->salt = svn_string_dup(salt, result_pool);

  if (read_checksum_p)
    baton->read_ctx = svn_checksum_ctx_create2(checksum_kind, salt, result_pool);
  else
    baton->read_ctx = NULL;

  baton->read_checksum_p = read_checksum_p;

  if (write_checksum_p)
    baton->write_ctx = svn_checksum_ctx_create2(checksum_kind, salt, result_pool);
  else
    baton->write_ctx = NULL;

  baton->write_checksum_p = write_checksum_p;

  checksum_stream = svn_stream_create(baton, result_pool);

  if (svn_stream_supports_partial_read(inner_stream))
    {
      svn_stream_set_read2(checksum_stream,
                           checksum_stream_read_fn,
                           checksum_stream_read_full_fn);
    }
  else
    {
      svn_stream_set_read2(checksum_stream,
                           NULL,
                           checksum_stream_read_full_fn);
    }

  svn_stream_set_write(checksum_stream, checksum_stream_write_fn);
  svn_stream_set_data_available(checksum_stream, checksum_stream_data_available_fn);
  if (svn_stream_supports_reset(inner_stream))
    svn_stream_set_seek(checksum_stream, checksum_stream_seek_fn);
  svn_stream_set_close(checksum_stream, checksum_stream_close_fn);

  return checksum_stream;
}

svn_stream_t *
svn_wc__db_checksum_stream(svn_wc__db_checksum_t **read_checksum_p,
                           svn_wc__db_checksum_t **write_checksum_p,
                           svn_stream_t *stream,
                           svn_checksum_kind_t checksum_kind,
                           const svn_string_t *salt,
                           apr_pool_t *result_pool,
                           apr_pool_t *scratch_pool)
{
  return make_checksum_stream(read_checksum_p, write_checksum_p, stream,
                              checksum_kind, salt,
                              result_pool, scratch_pool);
}

svn_error_t *
svn_wc__db_checksum_stream_contents(svn_wc__db_checksum_t **checksum_p,
                                    svn_stream_t *stream,
                                    svn_checksum_kind_t checksum_kind,
                                    const svn_string_t *salt,
                                    apr_pool_t *result_pool,
                                    apr_pool_t *scratch_pool)
{
  svn_stream_t *checksum_stream;

  checksum_stream =  make_checksum_stream(checksum_p, NULL, stream,
                                          checksum_kind, salt,
                                          result_pool, scratch_pool);
  SVN_ERR(svn_stream_copy3(checksum_stream, svn_stream_empty(scratch_pool),
                           NULL, NULL, scratch_pool));

  return SVN_NO_ERROR;
}
