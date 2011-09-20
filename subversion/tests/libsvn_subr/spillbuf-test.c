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


static svn_error_t *
test_spillbuf_basic(apr_pool_t *pool)
{
  static const char data[] = ("abcdefghijklmnopqrstuvwxyz"
                              "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                              "0123456789");
  svn_spillbuf_t *buf = svn_spillbuf_create(sizeof(data) /* blocksize */,
                                            10 * sizeof(data) /* maxsize */,
                                            pool);
  int i;

  /* It starts empty.  */
  SVN_TEST_ASSERT(svn_spillbuf_is_empty(buf));

  /* Place enough data into the buffer to cause a spill to disk.  */
  for (i = 20; i--; )
    SVN_ERR(svn_spillbuf_write(buf, data, sizeof(data), pool));

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
      SVN_TEST_ASSERT(readlen == sizeof(data));

      /* And it should match each block of data we put in.  */
      SVN_TEST_ASSERT(memcmp(readptr, data, readlen) == 0);
    }

  return SVN_NO_ERROR;
}

/* The test table.  */
struct svn_test_descriptor_t test_funcs[] =
  {
    SVN_TEST_NULL,
    SVN_TEST_PASS2(test_spillbuf_basic, "basic spill buffer test"),
    SVN_TEST_NULL
  };
