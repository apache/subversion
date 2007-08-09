/*
 * diff_memory.c :  routines for doing diffs on in-memory data
 *
 * ====================================================================
 * Copyright (c) 2007 CollabNet.  All rights reserved.
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

#define WANT_MEMFUNC
#include <apr.h>
#include <apr_want.h>
#include <apr_tables.h>

#include "svn_diff.h"
#include "svn_pools.h"
#include "svn_types.h"
#include "svn_string.h"
#include "svn_utf.h"
#include "diff.h"
#include "svn_private_config.h"

typedef struct source_tokens_t
{
  /* A token simply is an svn_string_t pointing to
     the data of the in-memory data source, containing
     the raw token text, with length stored in the string */
  /*###TODO: Note we currently don't support normalization. */
  apr_array_header_t *tokens;

  /* Next token to be consumed */
  apr_size_t next_token;

  /* The source, containing the in-memory data to be diffed */
  svn_string_t *source;

  /* The last token ends with a newline character (sequence) */
  svn_boolean_t ends_without_eol;
} source_tokens_t;

typedef struct diff_mem_baton_t
{
  /* The tokens for each of the sources */
  source_tokens_t sources[4];
} diff_mem_baton_t;


static int
datasource_to_index(svn_diff_datasource_e datasource)
{
  switch (datasource)
    {
    case svn_diff_datasource_original:
      return 0;

    case svn_diff_datasource_modified:
      return 1;

    case svn_diff_datasource_latest:
      return 2;

    case svn_diff_datasource_ancestor:
      return 3;
    }

  return -1;
}


/* Implements svn_diff_fns_t::datasource_open */
static svn_error_t *
datasource_open(void *baton, svn_diff_datasource_e datasource)
{
  /* Do nothing: everything is already there and initialized to 0 */
  return SVN_NO_ERROR;
}


/* Implements svn_diff_fns_t::datasource_close */
static svn_error_t *
datasource_close(void *baton, svn_diff_datasource_e datasource)
{
  /* Do nothing.  The compare_token function needs previous datasources
   * to stay available until all datasources are processed.
   */

  return SVN_NO_ERROR;
}


/* Implements svn_diff_fns_t::datasource_get_next_token */
static svn_error_t *
datasource_get_next_token(apr_uint32_t *hash, void **token, void *baton,
                          svn_diff_datasource_e datasource)
{
  diff_mem_baton_t *mem_baton = baton;
  source_tokens_t *src = &(mem_baton->sources[datasource_to_index(datasource)]);

  if (src->tokens->nelts > src->next_token)
    {
      /* There are actually tokens to be returned */
      (*token) = APR_ARRAY_IDX(src->tokens, src->next_token, svn_string_t *);
      *hash = svn_diff__adler32(0,
                                ((svn_string_t *)*token)->data,
                                ((svn_string_t *)*token)->len);
      src->next_token++;
    }
  else
    *token = NULL;

  return SVN_NO_ERROR;
}

/* Implements svn_diff_fns_t::token_compare */
static svn_error_t *
token_compare(void *baton, void *token1, void *token2, int *result)
{
  /* Implement the same behaviour as diff_file.c:token_compare(),
     but be simpler, because we know we'll have all data in memory */
  svn_string_t *t1 = token1;
  svn_string_t *t2 = token2;

  if (t1->len != t2->len)
    *result = (t1->len < t2->len) ? -1 : 1;
  else
    *result = (t1->len == 0) ? 0 : memcmp(t1->data, t2->data, t2->len);

  return SVN_NO_ERROR;
}

/* Implements svn_diff_fns_t::token_discard */
static void
token_discard(void *baton, void *token)
{
  /* No-op, we have no use for discarded tokens... */
}


/* Implements svn_diff_fns_t::token_discard_all */
static void
token_discard_all(void *baton)
{
  /* Do nothing.
     Note that in the file case, this function discards all
     tokens allocated, but we're geared toward small in-memory diffs.
     Meaning that there's no special pool to clear.
  */
}


static const svn_diff_fns_t svn_diff__mem_vtable =
{
  datasource_open,
  datasource_close,
  datasource_get_next_token,
  token_compare,
  token_discard,
  token_discard_all
};

/* Fill SRC with the diff tokens (e.g. lines).

   TEXT is assumed to live long enough for the tokens to
   stay valid during their lifetime: no data is copied,
   instead, svn_string_t's are allocated pointing straight
   into TEXT.
*/
static void
fill_source_tokens(source_tokens_t *src,
                   svn_string_t *text,
                   apr_pool_t *pool)
{
  const char *curp;
  const char *endp;
  const char *startp;

  src->tokens = apr_array_make(pool, 0, sizeof(svn_string_t *));
  src->next_token = 0;
  src->source = text;

  for (startp = curp = text->data, endp = curp + text->len;
       curp != endp; curp++)
    {
      if (curp != endp && *curp == '\r' && *(curp + 1) == '\n')
        curp++;

      if (*curp == '\r' || *curp == '\n')
        {
          APR_ARRAY_PUSH(src->tokens, svn_string_t *) =
            svn_string_ncreate(startp, curp - startp + 1, pool);

          startp = curp + 1;
        }
    }

  /* If there's anything remaining (ie last line doesn't have a newline) */
  if (startp != endp)
    {
      APR_ARRAY_PUSH(src->tokens, svn_string_t *) =
        svn_string_ncreate(startp, endp - startp, pool);
      src->ends_without_eol = TRUE;
    }
  else
    src->ends_without_eol = FALSE;
}

svn_error_t *
svn_diff_mem_string_diff(svn_diff_t **diff,
                         svn_string_t *original,
                         svn_string_t *modified,
                         apr_pool_t *pool)
{
  diff_mem_baton_t baton;

  fill_source_tokens(&(baton.sources[0]), original, pool);
  fill_source_tokens(&(baton.sources[1]), modified, pool);

  SVN_ERR(svn_diff_diff(diff, &baton, &svn_diff__mem_vtable, pool));

  return SVN_NO_ERROR;
}


typedef enum unified_output_e
{
  unified_output_context = 0,
  unified_output_delete,
  unified_output_insert
} unified_output_e;

/* Baton for generating unified diffs */
typedef struct output_baton_t
{
  svn_stream_t *output_stream;
  const char *header_encoding;
  source_tokens_t sources[2]; /* 0 == original; 1 == modified */
  apr_size_t next_token; /* next token in original source */

  /* Cached markers, in header_encoding,
     indexed using unified_output_e */
  const char *prefix_str[3];

  svn_stringbuf_t *hunk;    /* in-progress hunk data */
  apr_off_t hunk_length[2]; /* 0 == original; 1 == modified */
  apr_off_t hunk_start[2];  /* 0 == original; 1 == modified */

  /* Pool for allocation of temporary memory in the callbacks
     Should be cleared on entry of each iteration of a callback */
  apr_pool_t *pool;
} output_baton_t;


/* Append tokens (lines) FIRST up to PAST_LAST
   from token-source index TOKENS with change-type TYPE
   to the current hunk.
*/
static void
output_unified_token_range(output_baton_t *btn,
                           int tokens,
                           unified_output_e type,
                           apr_off_t first,
                           apr_off_t past_last)
{
  source_tokens_t *source = &btn->sources[tokens];
  apr_off_t idx;

  past_last = (past_last > source->tokens->nelts)
    ? source->tokens->nelts : past_last;

  if (tokens == 0)
    /* We get context from the original source, don't expect
       to be asked to output a block which starts before
       what we already have written. */
    first = (first < btn->next_token) ? btn->next_token : first;

  if (first >= past_last)
    return;

  /* Do the loop with prefix and token */
  for (idx = first; idx < past_last; idx++)
    {
      svn_string_t *token
        = APR_ARRAY_IDX(source->tokens, idx, svn_string_t *);
      svn_stringbuf_appendcstr(btn->hunk, btn->prefix_str[type]);
      svn_stringbuf_appendbytes(btn->hunk, token->data, token->len);

      if (type == unified_output_context)
        {
          btn->hunk_length[0]++;
          btn->hunk_length[1]++;
        }
      else if (type == unified_output_delete)
        btn->hunk_length[0]++;
      else
        btn->hunk_length[1]++;

    }
  if (past_last == source->tokens->nelts && source->ends_without_eol)
    svn_stringbuf_appendcstr
      (btn->hunk, APR_EOL_STR "\\ No newline at end of file" APR_EOL_STR);

  if (tokens == 0)
    btn->next_token = past_last;
}

/* Flush the hunk currently built up in BATON
   into the baton's output_stream */
static svn_error_t *
output_unified_flush_hunk(output_baton_t *baton)
{
  apr_off_t target_token;
  apr_uint32_t hunk_len;

  if (svn_stringbuf_isempty(baton->hunk))
    return SVN_NO_ERROR;

  svn_pool_clear(baton->pool);

  /* Write the trailing context */
  target_token = baton->hunk_start[0] + baton->hunk_length[0]
    + SVN_DIFF__UNIFIED_CONTEXT_SIZE;
  output_unified_token_range(baton, 0 /*original*/, unified_output_context,
                             baton->next_token, target_token);

  /* Write the hunk header */
  if (baton->hunk_length[0] > 0)
    /* Convert our 0-based line numbers into unidiff 1-based numbers */
    baton->hunk_start[0]++;
  SVN_ERR(svn_stream_printf_from_utf8
          (baton->output_stream, baton->header_encoding,
           baton->pool,
           /* Hunk length 1 is implied, don't show the
              length field if we have a hunk that long */
           (baton->hunk_length[0] == 1)
           ? ("@@ -%" APR_OFF_T_FMT)
           : ("@@ -%" APR_OFF_T_FMT ",%" APR_OFF_T_FMT),
           baton->hunk_start[0], baton->hunk_length[0]));

  if (baton->hunk_length[1] > 0)
    /* Convert our 0-based line numbers into unidiff 1-based numbers */
    baton->hunk_start[1]++;
  SVN_ERR(svn_stream_printf_from_utf8
          (baton->output_stream, baton->header_encoding,
           baton->pool,
           /* Hunk length 1 is implied, don't show the
              length field if we have a hunk that long */
           (baton->hunk_length[1] == 1)
           ? (" +%" APR_OFF_T_FMT " @@" APR_EOL_STR)
           : (" +%" APR_OFF_T_FMT ",%" APR_OFF_T_FMT " @@" APR_EOL_STR),
           baton->hunk_start[1], baton->hunk_length[1]));

  hunk_len = baton->hunk->len;
  SVN_ERR(svn_stream_write(baton->output_stream,
                           baton->hunk->data, &hunk_len));

  baton->hunk_length[0] = baton->hunk_length[1] = 0;
  svn_stringbuf_setempty(baton->hunk);

  return SVN_NO_ERROR;
}

/* Implements svn_diff_output_fns_t::output_diff_modified */
static svn_error_t *
output_unified_diff_modified(void *baton,
                             apr_off_t original_start,
                             apr_off_t original_length,
                             apr_off_t modified_start,
                             apr_off_t modified_length,
                             apr_off_t latest_start,
                             apr_off_t latest_length)
{
  output_baton_t *btn = baton;
  apr_off_t targ_orig, targ_mod;

  targ_orig = original_start - SVN_DIFF__UNIFIED_CONTEXT_SIZE;
  targ_orig = (targ_orig < 0) ? 0 : targ_orig;
  targ_mod = modified_start;

  if (btn->next_token + SVN_DIFF__UNIFIED_CONTEXT_SIZE < targ_orig)
    SVN_ERR(output_unified_flush_hunk(btn));

  if (btn->hunk_length[0] == 0
      && btn->hunk_length[1] == 0)
    {
      btn->hunk_start[0] = targ_orig;
      btn->hunk_start[1] = targ_mod + targ_orig - original_start;
    }

  output_unified_token_range(btn, 0/*original*/, unified_output_context,
                             targ_orig, original_start);
  output_unified_token_range(btn, 0/*original*/, unified_output_delete,
                             original_start, original_start + original_length);
  output_unified_token_range(btn, 1/*modified*/, unified_output_insert,
                             modified_start, modified_start + modified_length);

  return SVN_NO_ERROR;
}

static const svn_diff_output_fns_t mem_output_unified_vtable =
{
  NULL, /* output_common */
  output_unified_diff_modified,
  NULL, /* output_diff_latest */
  NULL, /* output_diff_common */
  NULL  /* output_conflict */
};


svn_error_t *
svn_diff_mem_string_output_unified(svn_stream_t *output_stream,
                                   svn_diff_t *diff,
                                   const char *original_header,
                                   const char *modified_header,
                                   const char *header_encoding,
                                   svn_string_t *original,
                                   svn_string_t *modified,
                                   apr_pool_t *pool)
{

  if (svn_diff_contains_diffs(diff))
    {
      output_baton_t baton;

      memset(&baton, 0, sizeof(baton));
      baton.output_stream = output_stream;
      baton.pool = svn_pool_create(pool);
      baton.header_encoding = header_encoding;
      baton.hunk = svn_stringbuf_create("", pool);

      SVN_ERR(svn_utf_cstring_from_utf8_ex2
              (&(baton.prefix_str[unified_output_context]), " ",
               header_encoding, pool));
      SVN_ERR(svn_utf_cstring_from_utf8_ex2
              (&(baton.prefix_str[unified_output_delete]), "-",
               header_encoding, pool));
      SVN_ERR(svn_utf_cstring_from_utf8_ex2
              (&(baton.prefix_str[unified_output_insert]), "+",
               header_encoding, pool));

      fill_source_tokens(&baton.sources[0], original, pool);
      fill_source_tokens(&baton.sources[1], modified, pool);

      SVN_ERR(svn_stream_printf_from_utf8
              (output_stream, header_encoding, pool,
               "--- %s" APR_EOL_STR
               "+++ %s" APR_EOL_STR,
               original_header, modified_header));

      SVN_ERR(svn_diff_output(diff, &baton,
                              &mem_output_unified_vtable));
      SVN_ERR(output_unified_flush_hunk(&baton));

      svn_pool_destroy(baton.pool);
    }

  return SVN_NO_ERROR;
}
