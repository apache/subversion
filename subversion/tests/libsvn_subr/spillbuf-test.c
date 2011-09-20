/*
 * spillbuf-test.c : test the spill buffer code
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

#include "svn_types.h"

#include "private/svn_subr_private.h"

#include "../svn_test.h"


static const char basic_data[] = ("abcdefghijklmnopqrstuvwxyz"
                                  "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                                  "0123456789");


static svn_error_t *
test_spillbuf_basic(apr_pool_t *pool)
{
  svn_spillbuf_t *buf = svn_spillbuf_create(
                          sizeof(basic_data) /* blocksize */,
                          10 * sizeof(basic_data) /* maxsize */,
                          pool);
  int i;

  /* It starts empty.  */
  SVN_TEST_ASSERT(svn_spillbuf_is_empty(buf));

  /* Place enough data into the buffer to cause a spill to disk.  */
  for (i = 20; i--; )
    SVN_ERR(svn_spillbuf_write(buf, basic_data, sizeof(basic_data), pool));

  /* And now has content.  */
  SVN_TEST_ASSERT(!svn_spillbuf_is_empty(buf));

  while (TRUE)
    {
      const char *readptr;
      apr_size_t readlen;

      SVN_ERR(svn_spillbuf_read(&readptr, &readlen, buf, pool));
      if (readptr == NULL)
        break;

      /* We happen to know that the spill buffer reads data in lengths
         of BLOCKSIZE.  */
      SVN_TEST_ASSERT(readlen == sizeof(basic_data));

      /* And it should match each block of data we put in.  */
      SVN_TEST_ASSERT(memcmp(readptr, basic_data, readlen) == 0);
    }

  SVN_TEST_ASSERT(svn_spillbuf_is_empty(buf));

  return SVN_NO_ERROR;
}


static svn_error_t *
test_spillbuf_file(apr_pool_t *pool)
{
  svn_spillbuf_t *buf = svn_spillbuf_create(
                          sizeof(basic_data) + 2 /* blocksize */,
                          2 * sizeof(basic_data) /* maxsize */,
                          pool);
  int i;
  const char *readptr;
  apr_size_t readlen;
  int cur_index;

  /* Place enough data into the buffer to cause a spill to disk. Note that
     we are writing data that is *smaller* than the blocksize.  */
  for (i = 7; i--; )
    SVN_ERR(svn_spillbuf_write(buf, basic_data, sizeof(basic_data), pool));

  /* The first three reads will be in-memory blocks, so they will match
     what we wrote into the spill buffer.  */
  SVN_ERR(svn_spillbuf_read(&readptr, &readlen, buf, pool));
  SVN_TEST_ASSERT(readptr != NULL);
  SVN_TEST_ASSERT(readlen == sizeof(basic_data));
  SVN_ERR(svn_spillbuf_read(&readptr, &readlen, buf, pool));
  SVN_TEST_ASSERT(readptr != NULL);
  SVN_TEST_ASSERT(readlen == sizeof(basic_data));
  SVN_ERR(svn_spillbuf_read(&readptr, &readlen, buf, pool));
  SVN_TEST_ASSERT(readptr != NULL);
  SVN_TEST_ASSERT(readlen == sizeof(basic_data));

  /* Current index into basic_data[] that we compare against.  */
  cur_index = 0;

  while (TRUE)
    {
      /* This will read more bytes (from the spill file into a temporary
         in-memory block) than the blocks of data that we wrote. This makes
         it trickier to verify that the right data is being returned.  */
      SVN_ERR(svn_spillbuf_read(&readptr, &readlen, buf, pool));
      if (readptr == NULL)
        break;

      while (TRUE)
        {
          apr_size_t amt;

          /* Compute the slice of basic_data that we will compare against,
             given the readlen and cur_index.  */
          if (cur_index + readlen >= sizeof(basic_data))
            amt = sizeof(basic_data) - cur_index;
          else
            amt = readlen;
          SVN_TEST_ASSERT(memcmp(readptr, &basic_data[cur_index], amt) == 0);
          if ((cur_index += amt) == sizeof(basic_data))
            cur_index = 0;
          if ((readlen -= amt) == 0)
            break;
          readptr += amt;
        }
    }

  SVN_TEST_ASSERT(svn_spillbuf_is_empty(buf));

  return SVN_NO_ERROR;
}


/* The test table.  */
struct svn_test_descriptor_t test_funcs[] =
  {
    SVN_TEST_NULL,
    SVN_TEST_PASS2(test_spillbuf_basic, "basic spill buffer test"),
    SVN_TEST_PASS2(test_spillbuf_file, "spill buffer file test"),
    SVN_TEST_NULL
  };
