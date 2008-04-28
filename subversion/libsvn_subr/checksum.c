/*
 * checksum.c:   checksum routines
 *
 * ====================================================================
 * Copyright (c) 2008 CollabNet.  All rights reserved.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution.  The terms
 * are also available at http://subversion.tigris.org/license-1.html.
 * If newer versions of this license are posted there, you may use a
 * newer version instead, at your option.
 *
 * This software consists of voluntary contributions made by many
 * individuals.  For exact contribution history, see the revision
 * history and logs, available at http://subversion.tigris.org/.
 * ====================================================================
 */


#include "svn_checksum.h"
#include "svn_md5.h"
#include "svn_sha1.h"



/* A useful macro:  returns the greater of its arguments. */
#define MAX(x,y) ((x)>(y)?(x):(y))

svn_checksum_t *
svn_checksum_create(svn_checksum_kind_t kind,
                    apr_pool_t *pool)
{
  svn_checksum_t *checksum = apr_palloc(pool, sizeof(*checksum));

  switch (kind)
    {
      case svn_checksum_md5:
        checksum->digest = apr_palloc(pool, APR_MD5_DIGESTSIZE);
        break;

      case svn_checksum_sha1:
        checksum->digest = apr_palloc(pool, APR_SHA1_DIGESTSIZE);
        break;

      default:
        return NULL;
    }

  checksum->kind = kind;
  checksum->pool = pool;

  return checksum;
}

svn_error_t *
svn_checksum_clear(svn_checksum_t *checksum)
{
  switch (checksum->kind)
    {
      case svn_checksum_md5:
        memset(checksum->digest, 0, APR_MD5_DIGESTSIZE);
        break;

      case svn_checksum_sha1:
        memset(checksum->digest, 0, APR_SHA1_DIGESTSIZE);
        break;

      default:
        /* We really shouldn't get here, but if we do... */
        return svn_error_create(SVN_ERR_BAD_CHECKSUM_KIND, NULL, NULL);
    }

  return SVN_NO_ERROR;
}

svn_boolean_t
svn_checksum_match(svn_checksum_t *d1,
                   svn_checksum_t *d2)
{
  if (d1 == NULL || d1 == NULL)
    return TRUE;

  if (d1->kind != d2->kind)
    return FALSE;

  switch (d1->kind)
    {
      case svn_checksum_md5:
        return svn_md5_digests_match(d1->digest, d2->digest);
      case svn_checksum_sha1:
        return svn_sha1_digests_match(d1->digest, d2->digest);
      default:
        /* We really shouldn't get here, but if we do... */
        return FALSE;
    }
}

const char *
svn_checksum_to_cstring_display(svn_checksum_t *checksum,
                                apr_pool_t *pool)
{
  switch (checksum->kind)
    {
      case svn_checksum_md5:
        return svn_md5_digest_to_cstring_display(checksum->digest, pool);
      case svn_checksum_sha1:
        return svn_sha1_digest_to_cstring_display(checksum->digest, pool);
      default:
        /* We really shouldn't get here, but if we do... */
        return NULL;
    }
}

const char *
svn_checksum_to_cstring(svn_checksum_t *checksum,
                        apr_pool_t *pool)
{
  switch (checksum->kind)
    {
      case svn_checksum_md5:
        return svn_md5_digest_to_cstring(checksum->digest, pool);
      case svn_checksum_sha1:
        return svn_sha1_digest_to_cstring(checksum->digest, pool);
      default:
        /* We really shouldn't get here, but if we do... */
        return NULL;
    }
}

svn_error_t *
svn_checksum_dup(svn_checksum_t *dest,
                 svn_checksum_t *src)
{
  apr_size_t size;

  dest->kind = src->kind;

  if (src->kind == svn_checksum_md5)
    size = APR_MD5_DIGESTSIZE;
  else if (src->kind == svn_checksum_sha1)
    size = APR_SHA1_DIGESTSIZE;
  else
    return svn_error_create(SVN_ERR_BAD_CHECKSUM_KIND, NULL, NULL);

  dest->digest = apr_palloc(dest->pool, size);
  memcpy(dest->digest, src->digest, size);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_checksum(svn_checksum_t **checksum,
             svn_checksum_kind_t kind,
             const void *data,
             apr_size_t len,
             apr_pool_t *pool)
{
  apr_sha1_ctx_t sha1_ctx;

  if ( !(kind == svn_checksum_md5 || kind == svn_checksum_sha1) )
    return svn_error_create(SVN_ERR_BAD_CHECKSUM_KIND, NULL, NULL);

  *checksum = svn_checksum_create(kind, pool);

  switch (kind)
    {
      case svn_checksum_md5:
        apr_md5((*checksum)->digest, data, len);
        break;

      case svn_checksum_sha1:
        apr_sha1_init(&sha1_ctx);
        apr_sha1_update(&sha1_ctx, data, len);
        apr_sha1_final((*checksum)->digest, &sha1_ctx);
        break;

      default:
        /* We really shouldn't get here, but if we do... */
        return svn_error_create(SVN_ERR_BAD_CHECKSUM_KIND, NULL, NULL);
    }

  return SVN_NO_ERROR;
}


svn_checksum_t *
svn_checksum_empty_checksum(svn_checksum_kind_t kind,
                            apr_pool_t *pool)
{
  svn_checksum_t *checksum = svn_checksum_create(kind, pool);

  switch (kind)
    {
      case svn_checksum_md5:
        memcpy(checksum->digest, svn_md5_empty_string_digest(),
               APR_MD5_DIGESTSIZE);
        break;

      case svn_checksum_sha1:
        memcpy(checksum->digest, svn_sha1_empty_string_digest(),
               APR_SHA1_DIGESTSIZE);
        break;

      default:
        /* We really shouldn't get here, but if we do... */
        return NULL;
    }

  return checksum;
}

struct svn_checksum_ctx_t
{
  void *apr_ctx;
  svn_checksum_t *checksum;
};

svn_checksum_ctx_t *
svn_checksum_ctx_create(svn_checksum_t *checksum,
                        apr_pool_t *pool)
{
  svn_checksum_ctx_t *ctx = apr_palloc(pool, sizeof(*ctx));

  ctx->checksum = checksum;
  switch (checksum->kind)
    {
      case svn_checksum_md5:
        ctx->apr_ctx = apr_palloc(pool, sizeof(apr_md5_ctx_t));
        apr_md5_init(ctx->apr_ctx);
        break;

      case svn_checksum_sha1:
        ctx->apr_ctx = apr_palloc(pool, sizeof(apr_sha1_ctx_t));
        apr_sha1_init(ctx->apr_ctx);
        break;

      default:
        return NULL;
    }

  return ctx;
}

svn_error_t *
svn_checksum_update(svn_checksum_ctx_t *ctx,
                    const void *data,
                    apr_size_t len)
{
  switch (ctx->checksum->kind)
    {
      case svn_checksum_md5:
        apr_md5_update(ctx->apr_ctx, data, len);
        break;

      case svn_checksum_sha1:
        apr_sha1_update(ctx->apr_ctx, data, len);
        break;

      default:
        /* We really shouldn't get here, but if we do... */
        return svn_error_create(SVN_ERR_BAD_CHECKSUM_KIND, NULL, NULL);
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_checksum_final(svn_checksum_ctx_t *ctx)
{
  switch (ctx->checksum->kind)
    {
      case svn_checksum_md5:
        apr_md5_final(ctx->checksum->digest, ctx->apr_ctx);
        break;

      case svn_checksum_sha1:
        apr_sha1_final(ctx->checksum->digest, ctx->apr_ctx);
        break;

      default:
        /* We really shouldn't get here, but if we do... */
        return svn_error_create(SVN_ERR_BAD_CHECKSUM_KIND, NULL, NULL);
    }

  return SVN_NO_ERROR;
}
