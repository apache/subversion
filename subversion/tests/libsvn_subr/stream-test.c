/*
 * stream-test.c -- test the stream functions
 *
 * ====================================================================
 *    Licensed to the Subversion Corporation (SVN Corp.) under one
 *    or more contributor license agreements.  See the NOTICE file
 *    distributed with this work for additional information
 *    regarding copyright ownership.  The SVN Corp. licenses this file
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

#include <stdio.h>
#include "svn_pools.h"
#include "svn_io.h"
#include <apr_general.h>

#include "../svn_test.h"


static svn_error_t *
test_stream_from_string(apr_pool_t *pool)
{
  int i;
  apr_pool_t *subpool = svn_pool_create(pool);

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

  /* Test svn_stream_from_stringbuf() as a readable stream. */
  for (i = 0; i < NUM_TEST_STRINGS; i++)
    {
      svn_stream_t *stream;
      char buffer[TEST_BUF_SIZE];
      svn_stringbuf_t *inbuf, *outbuf;
      apr_size_t len;

      inbuf = svn_stringbuf_create(strings[i], subpool);
      outbuf = svn_stringbuf_create("", subpool);
      stream = svn_stream_from_stringbuf(inbuf, subpool);
      len = TEST_BUF_SIZE;
      while (len == TEST_BUF_SIZE)
        {
          /* Read a chunk ... */
          SVN_ERR(svn_stream_read(stream, buffer, &len));

          /* ... and append the chunk to the stringbuf. */
          svn_stringbuf_appendbytes(outbuf, buffer, len);
        }

      if (! svn_stringbuf_compare(inbuf, outbuf))
        return svn_error_create(SVN_ERR_TEST_FAILED, NULL,
                                "Got unexpected result.");

      svn_pool_clear(subpool);
    }

  /* Test svn_stream_from_stringbuf() as a writable stream. */
  for (i = 0; i < NUM_TEST_STRINGS; i++)
    {
      svn_stream_t *stream;
      svn_stringbuf_t *inbuf, *outbuf;
      apr_size_t amt_read, len;

      inbuf = svn_stringbuf_create(strings[i], subpool);
      outbuf = svn_stringbuf_create("", subpool);
      stream = svn_stream_from_stringbuf(outbuf, subpool);
      amt_read = 0;
      while (amt_read < inbuf->len)
        {
          /* Write a chunk ... */
          len = TEST_BUF_SIZE < (inbuf->len - amt_read)
                  ? TEST_BUF_SIZE
                  : inbuf->len - amt_read;
          SVN_ERR(svn_stream_write(stream, inbuf->data + amt_read, &len));
          amt_read += len;
        }

      if (! svn_stringbuf_compare(inbuf, outbuf))
        return svn_error_create(SVN_ERR_TEST_FAILED, NULL,
                                "Got unexpected result.");

      svn_pool_clear(subpool);
    }

#undef NUM_TEST_STRINGS
#undef TEST_BUF_SIZE

  svn_pool_destroy(subpool);
  return SVN_NO_ERROR;
}

/* generate some poorly compressable data */
static svn_stringbuf_t *
generate_test_bytes(int num_bytes, apr_pool_t *pool)
{
  svn_stringbuf_t *buffer = svn_stringbuf_create("", pool);
  int total, repeat, repeat_iter;
  char c;

  for (total = 0, repeat = repeat_iter = 1, c = 0; total < num_bytes; total++)
    {
      svn_stringbuf_appendbytes(buffer, &c, 1);

      repeat_iter--;
      if (repeat_iter == 0)
        {
          if (c == 127)
            repeat++;
          c = (c + 1) % 127;
          repeat_iter = repeat;
        }
    }

  return buffer;
}


static svn_error_t *
test_stream_compressed(apr_pool_t *pool)
{
#define NUM_TEST_STRINGS 5
#define TEST_BUF_SIZE 10
#define GENERATED_SIZE 20000

  int i;
  svn_stringbuf_t *bufs[NUM_TEST_STRINGS];
  apr_pool_t *subpool = svn_pool_create(pool);

  static const char * const strings[NUM_TEST_STRINGS - 1] = {
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


  for (i = 0; i < (NUM_TEST_STRINGS - 1); i++)
    bufs[i] = svn_stringbuf_create(strings[i], pool);

  /* the last buffer is for the generated data */
  bufs[NUM_TEST_STRINGS - 1] = generate_test_bytes(GENERATED_SIZE, pool);

  for (i = 0; i < NUM_TEST_STRINGS; i++)
    {
      svn_stream_t *stream;
      svn_stringbuf_t *origbuf, *inbuf, *outbuf;
      char buf[TEST_BUF_SIZE];
      apr_size_t len;

      origbuf = bufs[i];
      inbuf = svn_stringbuf_create("", subpool);
      outbuf = svn_stringbuf_create("", subpool);

      stream = svn_stream_compressed(svn_stream_from_stringbuf(outbuf,
                                                               subpool),
                                     subpool);
      len = origbuf->len;
      SVN_ERR(svn_stream_write(stream, origbuf->data, &len));
      SVN_ERR(svn_stream_close(stream));

      stream = svn_stream_compressed(svn_stream_from_stringbuf(outbuf,
                                                               subpool),
                                     subpool);
      len = TEST_BUF_SIZE;
      while (len >= TEST_BUF_SIZE)
        {
          len = TEST_BUF_SIZE;
          SVN_ERR(svn_stream_read(stream, buf, &len));
          if (len > 0)
            svn_stringbuf_appendbytes(inbuf, buf, len);
        }

      if (! svn_stringbuf_compare(inbuf, origbuf))
        return svn_error_create(SVN_ERR_TEST_FAILED, NULL,
                                "Got unexpected result.");

      SVN_ERR(svn_stream_close(stream));

      svn_pool_clear(subpool);
    }

#undef NUM_TEST_STRINGS
#undef TEST_BUF_SIZE
#undef GENEREATED_SIZE

  svn_pool_destroy(subpool);
  return SVN_NO_ERROR;
}

static svn_error_t *
test_stream_range(apr_pool_t *pool)
{
  static const char *file_data[3] = {"Before", "Now", "After"};
  const char *before, *now, *after;
  char buf[14 + 1] = {0}; /* Enough to hold file data + '\0' */
  static const char *fname = "test_stream_range.txt";
  apr_off_t start, end;
  apr_file_t *f;
  apr_status_t status;
  unsigned int i, j;
  apr_size_t len;
  svn_stream_t *stream;

  status = apr_file_open(&f, fname, (APR_READ | APR_WRITE | APR_CREATE |
                         APR_TRUNCATE | APR_DELONCLOSE), APR_OS_DEFAULT, pool);
  if (status != APR_SUCCESS)
    return svn_error_createf(SVN_ERR_TEST_FAILED, NULL, "Cannot open '%s'",
                             fname);

  /* Create the file. */
  for (j = 0; j < 3; j++)
    {
      len = strlen(file_data[j]);
      status = apr_file_write(f, file_data[j], &len);
      if (status || len != strlen(file_data[j]))
        return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                                 "Cannot write to '%s'", fname);
    }

    /* Create a stream to read from a range of the file. */
    before = file_data[0];
    now = file_data[1];
    after = file_data[2];

    start = strlen(before);
    end = start + strlen(now);

    stream = svn_stream_from_aprfile_range_readonly(f, TRUE, start, end, pool);

    /* Even when requesting more data than contained in the range,
     * we should only receive data from the range. */
    len = strlen(now) + strlen(after);

    for (i = 0; i < 2; i++)
      {
        /* Read the range. */
        SVN_ERR(svn_stream_read(stream, buf, &len));
        if (len > strlen(now))
          return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                                   "Read past range");
        if (strcmp(buf, now))
          return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                                   "Unexpected data");

        /* Reading past the end of the range should be impossible. */
        SVN_ERR(svn_stream_read(stream, buf, &len));
        if (len != 0)
          return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                                   "Read past range");

        /* Resetting the stream should allow us to read the range again. */
        SVN_ERR(svn_stream_reset(stream));
      }

    SVN_ERR(svn_stream_close(stream));

    /* The attempt to create a stream with invalid ranges should result
     * in an empty stream. */
    stream = svn_stream_from_aprfile_range_readonly(f, TRUE, 0, -1, pool);
    len = 42;
    SVN_ERR(svn_stream_read(stream, buf, &len));
    SVN_ERR_ASSERT(len == 0);
    stream = svn_stream_from_aprfile_range_readonly(f, TRUE, -1, 0, pool);
    len = 42;
    SVN_ERR(svn_stream_read(stream, buf, &len));
    SVN_ERR_ASSERT(len == 0);

    SVN_ERR(svn_stream_close(stream));
    apr_file_close(f);
    return SVN_NO_ERROR;
}

/* An implementation of svn_io_line_filter_cb_t */
static svn_error_t *
line_filter(svn_boolean_t *filtered, const char *line, apr_pool_t *scratch_pool)
{
  *filtered = strchr(line, '!') != NULL;
  return SVN_NO_ERROR;
}

static svn_error_t *
test_stream_line_filter(apr_pool_t *pool)
{
  static const char *lines[4] = {"Not filtered.", "Filtered!",
                                 "Not filtered either.", "End of the lines!"};
  svn_string_t *string;
  svn_stream_t *stream;
  svn_stringbuf_t *line;
  svn_boolean_t eof;

  string = svn_string_createf(pool, "%s\n%s\n%s\n%s", lines[0], lines[1],
                              lines[2], lines[3]);
  stream = svn_stream_from_string(string, pool);

  svn_stream_set_line_filter_callback(stream, line_filter);

  svn_stream_readline(stream, &line, "\n", &eof, pool);
  SVN_ERR_ASSERT(strcmp(line->data, lines[0]) == 0);
  /* line[1] should be filtered */
  svn_stream_readline(stream, &line, "\n", &eof, pool);
  SVN_ERR_ASSERT(strcmp(line->data, lines[2]) == 0);

  /* The last line should also be filtered, and the resulting
   * stringbuf should be empty. */
  svn_stream_readline(stream, &line, "\n", &eof, pool);
  SVN_ERR_ASSERT(eof && svn_stringbuf_isempty(line));

  return SVN_NO_ERROR;
}


/* The test table.  */

struct svn_test_descriptor_t test_funcs[] =
  {
    SVN_TEST_NULL,
    SVN_TEST_PASS2(test_stream_from_string,
                   "test svn_stream_from_string"),
    SVN_TEST_PASS2(test_stream_compressed,
                   "test compressed streams"),
    SVN_TEST_PASS2(test_stream_range,
                   "test streams reading from range of file"),
    SVN_TEST_PASS2(test_stream_line_filter,
                   "test stream line filtering"),
    SVN_TEST_NULL
  };
