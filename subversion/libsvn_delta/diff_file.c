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
#include <apr_file_info.h>
#include <apr_time.h>

#include "svn_error.h"
#include "svn_diff.h"
#include "svn_types.h"
#include "svn_string.h"


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

  if (rv != APR_SUCCESS && ! APR_STATUS_IS_EOF(rv))
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


/** Display unified context diffs **/

#define SVN_DIFF__UNIFIED_CONTEXT_SIZE 3

typedef struct svn_diff__file_output_unified_baton_t
{
  apr_file_t *output_file;

  const char *path[2];
  apr_file_t *file[2];

  apr_off_t   current_line[2];
  
  char        buffer[2][4096];
  apr_size_t  length[2];
  char       *curp[2];
  
  apr_off_t   hunk_start[2];
  apr_off_t   hunk_length[2];
  svn_stringbuf_t *hunk;

  apr_pool_t *pool;
} svn_diff__file_output_baton_t;

typedef enum svn_diff__file_output_unified_type_e
{
  svn_diff__file_output_unified_skip,
  svn_diff__file_output_unified_context,
  svn_diff__file_output_unified_delete,
  svn_diff__file_output_unified_insert
} svn_diff__file_output_unified_type_e;


static
svn_error_t *
svn_diff__file_output_unified_line(svn_diff__file_output_baton_t *baton,
                                   svn_diff__file_output_unified_type_e type, 
                                   int idx)
{
  char *curp;
  char *eol;
  apr_size_t length;
  apr_status_t rv;
  svn_boolean_t bytes_processed = FALSE;
  
  length = baton->length[idx];
  curp = baton->curp[idx];

  /* Lazily update the current line even if we're at EOF.
   * This way we fake output of context at EOF
   */
  baton->current_line[idx]++;
  
  if (length == 0 && apr_file_eof(baton->file[idx]))
    {
      return NULL;
    }
  
  do
    {
      if (length > 0)
        {
          if (!bytes_processed)
            {
              switch (type)
                {
                case svn_diff__file_output_unified_context:
                  svn_stringbuf_appendbytes(baton->hunk, " ", 1);
                  baton->hunk_length[0]++;
                  baton->hunk_length[1]++;
                  break;
                case svn_diff__file_output_unified_delete:
                  svn_stringbuf_appendbytes(baton->hunk, "-", 1);
                  baton->hunk_length[0]++;
                  break;
                case svn_diff__file_output_unified_insert:
                  svn_stringbuf_appendbytes(baton->hunk, "+", 1);
                  baton->hunk_length[1]++;
                  break;
                default:
                  break;
                }
            }
            
          eol = memchr(curp, '\n', length);

          if (eol != NULL)
            {
              apr_size_t len;

              eol++;
              len = (apr_size_t)(eol - curp);
              length -= len;

              if (type != svn_diff__file_output_unified_skip)
                {
                  svn_stringbuf_appendbytes(baton->hunk, curp, len);
                }

              baton->curp[idx] = eol;
              baton->length[idx] = length;

              rv = APR_SUCCESS;

              break;
            }

          if (type != svn_diff__file_output_unified_skip)
            {
              svn_stringbuf_appendbytes(baton->hunk, curp, length);
            }
          
          bytes_processed = TRUE;
        }

      curp = baton->buffer[idx];
      length = sizeof(baton->buffer[idx]);

      rv = apr_file_read(baton->file[idx], curp, &length);
    }
  while (rv == APR_SUCCESS);

  if (rv != APR_SUCCESS && ! APR_STATUS_IS_EOF(rv))
    {
      return svn_error_createf(rv, 0, NULL, baton->pool, 
                               "error reading from '%s'.",
                               baton->path[idx]);
    }

  if (APR_STATUS_IS_EOF(rv))
    {
      /* Special case if we reach the end of file AND the last line is in the
         changed range AND the file doesn't end with a newline */
      if (bytes_processed && (type == svn_diff__file_output_unified_delete
                              || type == svn_diff__file_output_unified_insert))
        {
          svn_stringbuf_appendcstr(baton->hunk,
                                   "\n\\ No newline at end of file\n");
        }

      baton->length[idx] = 0;
    }

  return NULL;
}

static
svn_error_t *
svn_diff__file_output_unified_flush_hunk(svn_diff__file_output_baton_t *baton)
{
  apr_off_t target_line;
  apr_size_t hunk_len;
  apr_status_t rv;
  int i;
  
  if (svn_stringbuf_isempty(baton->hunk))
    {
      return NULL;
    }

  target_line = baton->hunk_start[0] + baton->hunk_length[0]
                + SVN_DIFF__UNIFIED_CONTEXT_SIZE;

  /* Add trailing context to the hunk */  
  while (baton->current_line[0] < target_line)
    {
      SVN_ERR(svn_diff__file_output_unified_line(baton, 
                svn_diff__file_output_unified_context, 0));
    }

  /* If the file is non-empty, convert the line indexes from
     zero based to one based */
  for (i = 0; i < 2; i++)
    {
      if (baton->hunk_length[i] > 0)
        baton->hunk_start[i]++;
    }

  /* Output the hunk header.  If the hunk length is 1, the file is a one line
     file.  In this case, surpress the number of lines in the hunk (it is
     1 implicitly) */
  apr_file_printf(baton->output_file, "@@ -%" APR_OFF_T_FMT,
                  baton->hunk_start[0]);
  if (baton->hunk_length[0] != 1)
    {
      apr_file_printf(baton->output_file, ",%" APR_OFF_T_FMT,
                      baton->hunk_length[0]);
    }

  apr_file_printf(baton->output_file, " +%" APR_OFF_T_FMT,
                  baton->hunk_start[1]);
  if (baton->hunk_length[1] != 1)
    {
      apr_file_printf(baton->output_file, ",%" APR_OFF_T_FMT,
                      baton->hunk_length[1]);
    }

  apr_file_printf(baton->output_file, " @@\n");
  
  /* Output the hunk content */
  hunk_len = baton->hunk->len;
  rv = apr_file_write(baton->output_file, baton->hunk->data, &hunk_len);
  if (rv != APR_SUCCESS)
    {
      return svn_error_create(rv, 0, NULL, baton->pool,
               "svn_diff_file_output_unified: error writing hunk.");
    }

  /* Prepare for the next hunk */
  baton->hunk_length[0] = 0;
  baton->hunk_length[1] = 0;
  svn_stringbuf_setempty(baton->hunk);

  return NULL;
}

static
svn_error_t *
svn_diff__file_output_unified_diff_modified(void *baton,
  apr_off_t original_start, apr_off_t original_length,
  apr_off_t modified_start, apr_off_t modified_length,
  apr_off_t latest_start, apr_off_t latest_length)
{
  svn_diff__file_output_baton_t *output_baton = baton;
  apr_off_t target_line[2];
  int i;

  target_line[0] = original_start >= SVN_DIFF__UNIFIED_CONTEXT_SIZE
                   ? original_start - SVN_DIFF__UNIFIED_CONTEXT_SIZE : 0;
  target_line[1] = modified_start;

  /* If the changed ranges are far enough apart (no overlapping context),
     flush the current hunk. */
  if (output_baton->hunk_start[0] + output_baton->hunk_length[0] 
      + SVN_DIFF__UNIFIED_CONTEXT_SIZE < target_line[0])
    {
      SVN_ERR(svn_diff__file_output_unified_flush_hunk(output_baton));

      output_baton->hunk_start[0] = target_line[0];
      output_baton->hunk_start[1] = target_line[1] + target_line[0] 
                                    - original_start;
      
      /* Skip lines until we are at the beginning of the context we want to
         display */
      while (output_baton->current_line[0] < target_line[0])
        {
          SVN_ERR(svn_diff__file_output_unified_line(output_baton,
                    svn_diff__file_output_unified_skip, 0));
        }
    }

  /* Skip lines until we are at the start of the changed range */
  while (output_baton->current_line[1] < target_line[1])
    {
      SVN_ERR(svn_diff__file_output_unified_line(output_baton,
                svn_diff__file_output_unified_skip, 1));
    }

  /* Output the context preceding the changed range */
  while (output_baton->current_line[0] < original_start)
    {
      SVN_ERR(svn_diff__file_output_unified_line(output_baton, 
                svn_diff__file_output_unified_context, 0));
    }
  
  target_line[0] = original_start + original_length;
  target_line[1] = modified_start + modified_length;

  /* Output the changed range */
  for (i = 0; i < 2; i++)
    {
      while (output_baton->current_line[i] < target_line[i])
        {
          SVN_ERR(svn_diff__file_output_unified_line(output_baton, i == 0 
                    ? svn_diff__file_output_unified_delete
                    : svn_diff__file_output_unified_insert, i));
        }
    }

  return NULL;
}

static
const char *
svn_diff__file_output_unified_default_hdr(apr_pool_t *pool,
                                          const char *path)
{
  apr_finfo_t file_info;
  apr_time_exp_t exploded_time;
  char time_buffer[64];
  apr_size_t time_len;

  apr_stat(&file_info, path, APR_FINFO_MTIME, pool);
  apr_time_exp_lt(&exploded_time, file_info.mtime);
          
  apr_strftime(time_buffer, &time_len, sizeof(time_buffer) - 1,
               "%a %b %e %H:%M:%S %Y", &exploded_time);

  return apr_psprintf(pool, "%s\t%s", path, time_buffer);
}

static const svn_diff_output_fns_t svn_diff__file_output_unified_vtable =
{
  NULL, /* output_common */
  svn_diff__file_output_unified_diff_modified,
  NULL, /* output_diff_latest */
  NULL, /* output_diff_common */
  NULL  /* output_diff_conflict */
};

svn_error_t *
svn_diff_file_output_unified(apr_file_t *output_file,
                             svn_diff_t *diff,
                             const char *original_path,
                             const char *modified_path,
                             const char *original_header,
                             const char *modified_header,
                             apr_pool_t *pool)
{
  svn_diff__file_output_baton_t baton;
  apr_status_t rv;
  int i;
    
  if (svn_diff_contains_diffs(diff)) 
    {
      memset(&baton, 0, sizeof(baton));
      baton.output_file = output_file;
      baton.pool = pool;
      baton.path[0] = original_path;
      baton.path[1] = modified_path;
      baton.hunk = svn_stringbuf_create("", pool);

      for (i = 0; i < 2; i++)
        { 
          rv = apr_file_open(&baton.file[i], baton.path[i],
                             APR_READ, APR_OS_DEFAULT, pool);
          if (rv != APR_SUCCESS)
            {
              return svn_error_createf(rv, 0, NULL, pool, 
                                       "failed to open file '%s'.",
                                       baton.path[i]);
            }
        }

      if (original_header == NULL)
        {
          original_header = 
            svn_diff__file_output_unified_default_hdr(pool, original_path);
        }

      if (modified_header == NULL)
        {
          modified_header =
            svn_diff__file_output_unified_default_hdr(pool, modified_path);
        }

      apr_file_printf(output_file,
                      "--- %s\n"
                      "+++ %s\n",
                      original_header, modified_header);

      SVN_ERR(svn_diff_output(diff, &baton,
                              &svn_diff__file_output_unified_vtable));
      SVN_ERR(svn_diff__file_output_unified_flush_hunk(&baton));

      for (i = 0; i < 2; i++)
        { 
          rv = apr_file_close(baton.file[i]);
          if (rv != APR_SUCCESS)
            {
              return svn_error_createf(rv, 0, NULL, pool, 
                                       "failed to close file '%s'.",
                                       baton.path[i]);
            }
        }
    }

  return NULL;
}
