/*
 * diff_file.c :  routines for doing diffs on files
 *
 * ====================================================================
 * Copyright (c) 2000-2002 CollabNet.  All rights reserved.
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


#include <apr.h>
#include <apr_pools.h>
#include <apr_general.h>
#include <apr_md5.h>
#include <apr_file_io.h>

#include "svn_error.h"
#include "svn_diff.h"
#include "svn_types.h"

typedef struct svn_diff__file_token_t
{
  apr_off_t length;

  unsigned char md5[MD5_DIGESTSIZE];
} svn_diff__file_token_t;


typedef struct svn_diff__file_baton_t
{
  const char *path[3];

  apr_file_t *file[3];
  apr_size_t  length[3];

  char buffer[3][4096];
  char *curp[3];

  svn_diff__file_token_t *token;
  svn_boolean_t reuse_token;

  apr_pool_t *pool;
} svn_diff__file_baton_t;


static
int
svn_diff__file_datasource_to_index(svn_diff_datasource_e datasource)
{
  switch (datasource)
    {
    case svn_diff_datasource_original:
      return 0;

    case svn_diff_datasource_modified:
      return 1;

    case svn_diff_datasource_latest:
      return 2;
    }

  return -1;
}

static
svn_error_t *
svn_diff__file_datasource_open(void *baton,
                               svn_diff_datasource_e datasource)
{
  svn_diff__file_baton_t *file_baton = baton;
  int idx;
  apr_status_t rv;

  idx = svn_diff__file_datasource_to_index(datasource);

  file_baton->length[idx] = 0;

  rv = apr_file_open(&file_baton->file[idx], file_baton->path[idx],
                     APR_READ, APR_OS_DEFAULT, file_baton->pool);
  if (rv != APR_SUCCESS)
    {
      return svn_error_createf(rv, 0, NULL, file_baton->pool, 
                               "failed to open file '%s'.",
                               file_baton->path[idx]);
    }

  return NULL;
}

static
svn_error_t *
svn_diff__file_datasource_close(void *baton,
                                svn_diff_datasource_e datasource)
{
  svn_diff__file_baton_t *file_baton = baton;
  int idx;
  apr_status_t rv;

  idx = svn_diff__file_datasource_to_index(datasource);

  rv = apr_file_close(file_baton->file[idx]);
  if (rv != APR_SUCCESS)
    {
      return svn_error_createf(rv, 0, NULL, file_baton->pool, 
                               "failed to close file '%s'.",
                               file_baton->path[idx]);
    }

  return NULL;
}

static
svn_error_t *
svn_diff__file_datasource_get_next_token(void **token, void *baton,
                                         svn_diff_datasource_e datasource)
{
  svn_diff__file_baton_t *file_baton = baton;
  svn_diff__file_token_t *file_token;
  apr_md5_ctx_t md5_ctx;
  apr_status_t rv;
  int idx;
  apr_file_t *file;
  apr_size_t length;
  char *curp;
  char *eol;

  *token = NULL;

  idx = svn_diff__file_datasource_to_index(datasource);

  length = file_baton->length[idx];
  file = file_baton->file[idx];
  curp = file_baton->curp[idx];

  if (length == 0 && apr_file_eof(file))
    {
      return NULL;
    }

  if (!file_baton->reuse_token)
    {
      file_token = apr_palloc(file_baton->pool, sizeof(*file_token));
      file_baton->token = file_token;
    }
  else
    {
      file_token = file_baton->token;
      file_baton->reuse_token = FALSE;
    }

  file_token->length = 0;

  apr_md5_init(&md5_ctx);

  do
    {
      if (length > 0)
        {
          eol = memchr(curp, '\n', length);

          if (eol != NULL)
            {
              apr_size_t len;
             
              eol++;
              len = (apr_size_t)(eol - curp);
              
              file_token->length += len;
              length -= len;
              apr_md5_update(&md5_ctx, curp, len);

              file_baton->curp[idx] = eol;
              file_baton->length[idx] = length;

              rv = APR_SUCCESS;
              break;
            }

          file_token->length += length;
          apr_md5_update(&md5_ctx, curp, length);
        }

      file_baton->length[idx] = 0;
      curp = file_baton->buffer[idx];
      length = sizeof(file_baton->buffer[idx]);

      rv = apr_file_read(file, curp, &length);
    }
  while (rv == APR_SUCCESS);

  if (rv != APR_SUCCESS && rv != APR_EOF)
    {
      return svn_error_createf(rv, 0, NULL, file_baton->pool, 
                               "error reading from '%s'.",
                               file_baton->path[idx]);
    }

  if (file_token->length > 0)
    {
      apr_md5_final(file_token->md5, &md5_ctx);
      *token = file_token;
    }

  return NULL;
}

static
int
svn_diff__file_token_compare(void *baton,
                             void *token1,
                             void *token2)
{
  svn_diff__file_token_t *file_token1 = token1;
  svn_diff__file_token_t *file_token2 = token2;

  if (file_token1->length < file_token2->length)
    return -1;

  if (file_token1->length > file_token2->length)
    return 1;

  return memcmp(file_token1->md5, file_token2->md5, MD5_DIGESTSIZE);
}

static
void
svn_diff__file_token_discard(void *baton,
                             void *token)
{
  svn_diff__file_baton_t *file_baton = baton;

  file_baton->reuse_token = file_baton->token == token;
}

static const svn_diff_fns_t svn_diff__file_vtable =
{
  svn_diff__file_datasource_open,
  svn_diff__file_datasource_close,
  svn_diff__file_datasource_get_next_token,
  svn_diff__file_token_compare,
  svn_diff__file_token_discard,
  NULL
};

svn_error_t *
svn_diff_file(svn_diff_t **diff,
              const char *original,
              const char *modified,
              apr_pool_t *pool)
{
  svn_diff__file_baton_t baton;

  memset(&baton, 0, sizeof(baton));
  baton.path[0] = original;
  baton.path[1] = modified;
  baton.pool = pool;

  return svn_diff(diff, &baton, &svn_diff__file_vtable, pool);
}

svn_error_t *
svn_diff3_file(svn_diff_t **diff,
               const char *original,
               const char *modified1,
               const char *modified2,
               apr_pool_t *pool)
{
  svn_diff__file_baton_t baton;

  memset(&baton, 0, sizeof(baton));
  baton.path[0] = original;
  baton.path[1] = modified1;
  baton.path[2] = modified2;

  return svn_diff3(diff, &baton, &svn_diff__file_vtable, pool);
}

