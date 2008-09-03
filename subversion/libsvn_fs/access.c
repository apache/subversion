/*
 * access.c:  shared code to manipulate svn_fs_access_t objects
 *
 * ====================================================================
 * Copyright (c) 2000-2004 CollabNet.  All rights reserved.
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


#include <apr_hash.h>

#include "svn_types.h"
#include "svn_pools.h"
#include "svn_fs.h"
#include "private/svn_fs_private.h"

#include "fs-loader.h"



svn_error_t *
svn_fs_create_access(svn_fs_access_t **access_ctx,
                     const char *username,
                     apr_pool_t *pool)
{
  svn_fs_access_t *ac;

  SVN_ERR_ASSERT(username != NULL);

  ac = apr_pcalloc(pool, sizeof(*ac));
  ac->username = apr_pstrdup(pool, username);
  ac->lock_tokens = apr_hash_make(pool);
  *access_ctx = ac;

  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_set_access(svn_fs_t *fs,
                  svn_fs_access_t *access_ctx)
{
  fs->access_ctx = access_ctx;

  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_get_access(svn_fs_access_t **access_ctx,
                  svn_fs_t *fs)
{
  *access_ctx = fs->access_ctx;

  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_access_get_username(const char **username,
                           svn_fs_access_t *access_ctx)
{
  *username = access_ctx->username;

  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_access_add_lock_token2(svn_fs_access_t *access_ctx,
                              const char *path,
                              const char *token)
{
  apr_hash_set(access_ctx->lock_tokens,
               token, APR_HASH_KEY_STRING, (void *) path);
  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_access_add_lock_token(svn_fs_access_t *access_ctx,
                             const char *token)
{
  return svn_fs_access_add_lock_token2(access_ctx, (const char *) 1, token);
}

apr_hash_t *
svn_fs__access_get_lock_tokens(svn_fs_access_t *access_ctx)
{
  return access_ctx->lock_tokens;
}
