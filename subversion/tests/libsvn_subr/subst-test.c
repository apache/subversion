/* subst-test.c --- tests for the subst functions
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

#include <apr.h>

#include "svn_pools.h"
#include "svn_string.h"
#include "svn_io.h"
#include "svn_subst.h"

#include "../svn_test.h"



static svn_error_t *
test_detect_file_eol(apr_pool_t *pool)
{
#define N_TESTS 5
  apr_file_t *file;
  static const char *file_data[N_TESTS] = {"Before\n", "Now\r\n", "After\r",
                                     "No EOL", ""};
  static const char *expected_eol[N_TESTS] = {"\n", "\r\n", "\r", NULL, NULL};
  static const char *fname = "test_detect_file_eol.txt";
  apr_off_t pos;
  apr_size_t len;
  unsigned int i;

  SVN_ERR(svn_io_file_open(&file, fname, (APR_READ | APR_WRITE | APR_CREATE |
                           APR_TRUNCATE | APR_DELONCLOSE), APR_OS_DEFAULT,
                           pool));

  for (i = 0; i < N_TESTS; i++)
    {
      const char *eol;
      unsigned int data_len;

      pos = 0;
      data_len = file_data[i] ? strlen(file_data[i]) : 0;
      SVN_ERR(svn_io_file_seek(file, APR_SET, &pos, pool));
      len = data_len;
      SVN_ERR(svn_io_file_write(file, file_data[i], &len, pool));
      SVN_ERR_ASSERT(len == data_len);
      SVN_ERR(svn_io_file_seek(file, APR_CUR, &pos, pool));
      SVN_ERR(svn_io_file_trunc(file, pos, pool));

      pos = 0;
      SVN_ERR(svn_io_file_seek(file, APR_SET, &pos, pool));

      SVN_ERR(svn_subst_detect_file_eol(&eol, file, pool));
      if (eol && expected_eol[i])
        SVN_ERR_ASSERT(strcmp(eol, expected_eol[i]) == 0);
      else
        SVN_ERR_ASSERT(eol == expected_eol[i]);
    }

  SVN_ERR(svn_io_file_close(file, pool));
  return SVN_NO_ERROR;
}


/* The test table.  */

struct svn_test_descriptor_t test_funcs[] =
  {
    SVN_TEST_NULL,
    SVN_TEST_PASS2(test_detect_file_eol,
                   "detect EOL style of a file"),
    SVN_TEST_NULL
  };
