/*
 * stream-test.c -- test the stream functions
 *
 * ====================================================================
 * Copyright (c) 2000-2001 CollabNet.  All rights reserved.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution.  The terms
 * are also available at http://subversion.tigris.org/license-1.html.
 * If newer versions of this license are posted there, you may use a
 * newer version instead, at your option.
 * ====================================================================
 */

#include <svn_pools.h>
#include <svn_io.h>
#include <stdio.h>
#include <apr_general.h>
#include "svn_test.h"


typedef struct stream_baton_t
{
  apr_pool_t *pool;
  svn_stringbuf_t *buffer;
}
stream_baton_t;


static stream_baton_t *
make_baton (apr_pool_t *pool)
{
  stream_baton_t *new_baton = apr_palloc (pool, sizeof (*new_baton));
  new_baton->pool = pool;
  new_baton->buffer = svn_string_create ("", pool);
  return new_baton;
}


static svn_error_t *
read_func (void *baton, 
           char *buffer, 
           apr_size_t *len)
{
  stream_baton_t *sb = (stream_baton_t *)baton;

  if ((! buffer) || (! *len))
    return SVN_NO_ERROR;

  /* If asked for more than we have, give only what we have. */
  if (*len > sb->buffer->len)
    *len = sb->buffer->len;

  /* Copy the requested amount into the buffer. */
  memcpy (buffer, sb->buffer->data, *len);

  /* Now, lose the bytes that were read from the buffer. */
  memcpy (sb->buffer->data, 
          sb->buffer->data + *len, 
          sb->buffer->len - *len);
  sb->buffer->len -= *len;

  return SVN_NO_ERROR;
}


static svn_error_t *
write_func (void *baton, 
            const char *data, 
            apr_size_t *len)
{
  stream_baton_t *sb = (stream_baton_t *)baton;
  svn_string_appendbytes (sb->buffer, data, *len);
  return SVN_NO_ERROR;
}


static svn_error_t *
close_func (void *baton)
{
  return SVN_NO_ERROR;
}


/* Helper function for test_feedback_stream */
static svn_error_t *
binary_recurse (int depth, 
                int limit, 
                int *next_branch_number,
                apr_pool_t *pool)
{
  /* Make a subpool */
  svn_stream_t *fb_stream;
  svn_stringbuf_t *output;
  apr_size_t len;

  /* Get the feedback stream from our subpool, and print our branch
     number. */
  fb_stream = svn_pool_get_feedback_stream (pool);
  output = svn_string_createf (pool, "%d\n", *next_branch_number);
  len = output->len;
  svn_stream_write (fb_stream, output->data, &len);

  /* Don't forget to increment the next branch number!  The fate of
     the Universe depends on this one ultra-important step! */
  (*next_branch_number)++;

  /* If there's room to recurse...do it. */
  if (depth < limit)
    {
      apr_pool_t *subpool = svn_pool_create (pool);
      SVN_ERR (binary_recurse (depth + 1, limit, next_branch_number, subpool));
      svn_pool_clear (subpool);
      SVN_ERR (binary_recurse (depth + 1, limit, next_branch_number, subpool));
      svn_pool_destroy (subpool);
    }

  return SVN_NO_ERROR;
}


static svn_error_t *
test_feedback_stream (const char **msg,
                      apr_pool_t *pool)
{
  svn_stream_t *fb_stream;
  stream_baton_t *baton;
  int next_branch_number = 1;
  int max_depth = 4;
  int i, last_branch;
  svn_stringbuf_t *expected_output, *read_output;
  *msg = "test global feedback stream";

  fb_stream = svn_pool_get_feedback_stream (pool);
 
  baton = make_baton (pool);
  svn_stream_set_baton (fb_stream, baton);
  svn_stream_set_read (fb_stream, &read_func);
  svn_stream_set_write (fb_stream, &write_func);
  svn_stream_set_close (fb_stream, &close_func);

  /* Call the binary recursion routine.  This routine prints, to the
     global feedback stream, the value of NEXT_BRANCH_NUMBER + a
     newline.  It then increments NEXT_BRANCH_NUMBER and forks into two
     recursive calls, until a given maximum depth is reached.   The
     net result should be that the integers from 1 to ( 2^max_depth -
     1) should be printed, one per line, to the feedback stream. */
  SVN_ERR (binary_recurse (0, max_depth - 1, &next_branch_number, pool));
  
  /* Cheap pow() call. */
  for (i = 0, last_branch = 1; i < max_depth; i++, last_branch *= 2)
    {;}

  /* Make a big ol' string with the same number list we expected to be
     created by the binary_recurse() function. */
  expected_output = svn_string_create ("", pool);
  for (i = 1; i < last_branch; i++)
    {
      svn_stringbuf_t *tmp = svn_string_createf (pool, "%d\n", i);
      svn_string_appendstr (expected_output, tmp);
    }

  /* Compare the expected output with what we really got.  We're going
     to do so the easy way first, then the hard way.  The easy way is
     just to compare the buffer in the stream baton with our expected
     output string.  */
  if (! svn_string_compare (expected_output, baton->buffer))
    return svn_error_create (SVN_ERR_TEST_FAILED, 0, 0, pool, 
                             "Easy compare failed");

  /* The hard way is to actually use the stream's read() function to
     build up yet another comparison string. */
  fb_stream = svn_pool_get_feedback_stream (pool);
  read_output = svn_string_create ("", pool);
  while (1)
    {
      apr_size_t len;
      char tmpbuf[257];

      len = 256;
      SVN_ERR (svn_stream_read (fb_stream, tmpbuf, &len));
      svn_string_appendbytes (read_output, tmpbuf, len);
      if (len < 256)
        break;
    }
  if (! svn_string_compare (expected_output, read_output))
    return svn_error_create (SVN_ERR_TEST_FAILED, 0, 0, pool, 
                             "Hard compare failed");
  
  return SVN_NO_ERROR;
}




/* The test table.  */

svn_error_t * (*test_funcs[]) (const char **msg,
                               apr_pool_t *pool) = {
  0,
  test_feedback_stream,
  0
};



/* 
 * local variables:
 * eval: (load-file "../../svn-dev.el")
 * end:
 */

