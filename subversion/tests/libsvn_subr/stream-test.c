/*
 * stream-test.c -- test the stream functions
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

#include <stdio.h>
#include <svn_pools.h>
#include <svn_io.h>
#include <apr_general.h>
#include "svn_test.h"


static svn_error_t *
test_stream_from_string (const char **msg,
                         svn_boolean_t msg_only,
                         apr_pool_t *pool)
{
  int i;
  apr_pool_t *subpool = svn_pool_create (pool);

#define NUM_TEST_STRINGS 4
#define TEST_BUF_SIZE 10

  static const char * const strings[NUM_TEST_STRINGS] = { 
    /* 0 */
    "",
    /* 1 */
    "This is a string.",
    /* 2 */
    "This is, by comparison to the previous string, a much longer string.",
    /* 3 */
    "And if you thought that last string was long, you just wait until "
    "I'm finished here.  I mean, how can a string really claim to be long "
    "when it fits on a single line of 80-columns?  Give me a break. "
    "Now, I'm not saying that I'm the longest string out there--far from "
    "it--but I feel that it is safe to assume that I'm far longer than my "
    "peers.  And that demands some amount of respect, wouldn't you say?"
  };
  
  *msg = "test svn_stream_from_string";

  if (msg_only)
    return SVN_NO_ERROR;

  /* Test svn_stream_from_stringbuf() as a readable stream. */
  for (i = 0; i < NUM_TEST_STRINGS; i++)
    {
      svn_stream_t *stream;
      char buffer[TEST_BUF_SIZE];
      svn_stringbuf_t *inbuf, *outbuf;
      apr_size_t len;

      inbuf = svn_stringbuf_create (strings[i], subpool);
      outbuf = svn_stringbuf_create ("", subpool);
      stream = svn_stream_from_stringbuf (inbuf, subpool);
      len = TEST_BUF_SIZE;
      while (len == TEST_BUF_SIZE)
        {
          /* Read a chunk ... */
          SVN_ERR (svn_stream_read (stream, buffer, &len));

          /* ... and append the chunk to the stringbuf. */
          svn_stringbuf_appendbytes (outbuf, buffer, len);
        }
      
      if (! svn_stringbuf_compare (inbuf, outbuf))
        return svn_error_create (SVN_ERR_TEST_FAILED, 0, NULL,
                                 "Got unexpected result.");

      svn_pool_clear (subpool);
    }

  /* Test svn_stream_from_stringbuf() as a writable stream. */
  for (i = 0; i < NUM_TEST_STRINGS; i++)
    {
      svn_stream_t *stream;
      svn_stringbuf_t *inbuf, *outbuf;
      apr_size_t amt_read, len;

      inbuf = svn_stringbuf_create (strings[i], subpool);
      outbuf = svn_stringbuf_create ("", subpool);
      stream = svn_stream_from_stringbuf (outbuf, subpool);
      amt_read = 0;
      while (amt_read < inbuf->len)
        {
          /* Write a chunk ... */
          len = TEST_BUF_SIZE < (inbuf->len - amt_read) 
                  ? TEST_BUF_SIZE 
                  : inbuf->len - amt_read;
          SVN_ERR (svn_stream_write (stream, inbuf->data + amt_read, &len));
          amt_read += len;
        }
      
      if (! svn_stringbuf_compare (inbuf, outbuf))
        return svn_error_create (SVN_ERR_TEST_FAILED, 0, NULL,
                                 "Got unexpected result.");

      svn_pool_clear (subpool);
    }

#undef NUM_TEST_STRINGS
#undef TEST_BUF_SIZE

  svn_pool_destroy (subpool);
  return SVN_NO_ERROR;
}


/* The test table.  */

struct svn_test_descriptor_t test_funcs[] =
  {
    SVN_TEST_NULL,
    SVN_TEST_PASS (test_stream_from_string),
    SVN_TEST_NULL
  };
