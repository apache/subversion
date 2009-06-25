/* subst-test.c --- tests for the subst functions
 *
 * ====================================================================
 * Copyright (c) 2000-2004, 2009 CollabNet.  All rights reserved.
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
